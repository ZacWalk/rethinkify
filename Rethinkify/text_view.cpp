#include "pch.h"
#include "text_view.h"
#include "text_buffer.h"
#include "resource.h"
#include "ui.h"

const auto REG_FIND_SUBKEY = L"Rethinkify\\Find";
const auto REG_REPLACE_SUBKEY = L"Rethinkify\\Replace";
const auto REG_MATCH_CASE = L"MatchCase";
const auto REG_WHOLE_WORD = L"WholeWord";
const auto REG_FIND_WHAT = L"FindWhat";
const auto REG_REPLACE_WITH = L"ReplaceWith";

const auto REG_PAGE_SUBKEY = L"Rethinkify\\PageSetup";
const auto REG_MARGIN_LEFT = L"LeftMargin";
const auto REG_MARGIN_RIGHT = L"RightMargin";
const auto REG_MARGIN_TOP = L"TopMargin";
const auto REG_MARGIN_BOTTOM = L"BottomMargin";

const auto TAB_CHARACTER = 0xBB;
const auto SPACE_CHARACTER = 0x95;
const auto DEFAULT_PRINT_MARGIN = 1000; //	10 millimeters
const auto DRAG_BORDER_X = 5;
const auto DRAG_BORDER_Y = 5;

class CEditDropTargetImpl : public IDropTarget
{
private:
    text_view *m_pOwner;
public:
    CEditDropTargetImpl(text_view *pOwner) { m_pOwner = pOwner; };

    DROPEFFECT OnDragEnter(CWindow wnd, COleDataObject* pDataObject, DWORD dwKeyState, CPoint point);
    void OnDragLeave(CWindow wnd);
    DROPEFFECT OnDragOver(CWindow wnd, COleDataObject* pDataObject, DWORD dwKeyState, CPoint point);
    bool OnDrop(CWindow wnd, COleDataObject* pDataObject, DROPEFFECT dropEffect, CPoint point);
    DROPEFFECT OnDragScroll(CWindow wnd, DWORD dwKeyState, CPoint point);
};

static auto s_textHighlighter = std::make_shared<TextHighight>();

text_view::text_view(text_buffer &buffer) : _buffer(buffer), _highlight(s_textHighlighter)
{
    _dropTarget = nullptr;
    _backBuffer = nullptr;

    memset(m_apFonts, 0, sizeof(HFONT) * 4);

    m_bSelMargin = true;
    _buffer.AddView(this);

    ResetView();
}

text_view::~text_view()
{
    assert(_dropTarget == nullptr);
    assert(_backBuffer == nullptr);
}

const text_selection &text_view::GetSelection() const
{
    PrepareSelBounds();
    return m_ptDrawSel;
}

int text_view::expanded_line_length(int lineIndex) const
{
    if (_actualLineLengths.size() != _buffer.size())
    {
        _actualLineLengths.clear();
        _actualLineLengths.insert(_actualLineLengths.begin(), _buffer.size(), 0);
    }

    if (_actualLineLengths[lineIndex] == 0)
    {
        auto nActualLength = 0;
        const auto &line = _buffer[lineIndex];

        if (!line.empty())
        {
            auto nLength = line.size();
            auto pszCurrent = line.c_str();
            auto tabSize = tab_size();

            for (;;)
            {
                auto psz = wcschr(pszCurrent, L'\t');

                if (psz == nullptr)
                {
                    nActualLength += (line.c_str() + nLength - pszCurrent);
                    break;
                }

                nActualLength += (psz - pszCurrent);
                nActualLength += (tabSize - nActualLength % tabSize);
                pszCurrent = psz + 1;
            }
        }

        _actualLineLengths[lineIndex] = nActualLength;
    }

    return _actualLineLengths[lineIndex];
}

void text_view::ScrollToChar(int nNewOffsetChar, bool bTrackScrollBar)
{
    if (m_nOffsetChar != nNewOffsetChar)
    {
        int nScrollChars = m_nOffsetChar - nNewOffsetChar;
        m_nOffsetChar = nNewOffsetChar;
        CRect rcScroll;
        GetClientRect(&rcScroll);
        rcScroll.left += margin_width();
        ScrollWindowEx(nScrollChars * char_width(), 0, &rcScroll, &rcScroll, nullptr, nullptr, SW_INVALIDATE);
        UpdateWindow();
        if (bTrackScrollBar)
            RecalcHorzScrollBar(true);
    }
}

void text_view::ScrollToLine(int nNewTopLine, bool bTrackScrollBar)
{
    if (m_nTopLine != nNewTopLine)
    {
        int nScrollLines = m_nTopLine - nNewTopLine;
        m_nTopLine = nNewTopLine;
        ScrollWindowEx(0, nScrollLines * font_height(), nullptr, nullptr, nullptr, nullptr, SW_INVALIDATE);
        UpdateWindow();
        if (bTrackScrollBar)
            RecalcVertScrollBar(true);
    }
}

std::wstring text_view::ExpandChars(const std::wstring &text, int nOffset, int nCount) const
{
    std::wstring result;

    if (nCount > 0)
    {
        auto pszChars = text.c_str();
        int tabSize = tab_size();
        int nActualOffset = 0;
        int i = 0;

        for (i = 0; i < nOffset; i++)
        {
            if (pszChars[i] == _T('\t'))
                nActualOffset += (tabSize - nActualOffset % tabSize);
            else
                nActualOffset++;
        }

        pszChars += nOffset;
        int nLength = nCount;

        int nTabCount = 0;
        for (i = 0; i < nLength; i++)
        {
            if (pszChars[i] == _T('\t'))
                nTabCount++;
        }

        int nCurPos = 0;

        if (nTabCount > 0 || m_bViewTabs)
        {
            for (i = 0; i < nLength; i++)
            {
                if (pszChars[i] == _T('\t'))
                {
                    int nSpaces = tabSize - (nActualOffset + nCurPos) % tabSize;

                    if (m_bViewTabs)
                    {
                        result += TAB_CHARACTER;
                        nCurPos++;
                        nSpaces--;
                    }
                    while (nSpaces > 0)
                    {
                        result += _T(' ');
                        nCurPos++;
                        nSpaces--;
                    }
                }
                else
                {
                    result += (pszChars[i] == _T(' ') && m_bViewTabs) ? SPACE_CHARACTER : pszChars[i];
                    nCurPos++;
                }
            }
        }
        else
        {
            result.append(pszChars, nLength);
            nCurPos = nLength;
        }
    }

    return result;
}

void text_view::DrawLineHelperImpl(HDC pdc, text_location &ptOrigin, const CRect &rcClip, const wchar_t * pszChars, int nOffset, int nCount) const
{
    if (nCount > 0)
    {
        auto line = ExpandChars(pszChars, nOffset, nCount);
        auto nWidth = rcClip.right - ptOrigin.x;

        if (nWidth > 0)
        {
            auto nCharWidth = char_width();
            auto nCount = line.size();
            auto nCountFit = nWidth / nCharWidth + 1;

            if (nCount > nCountFit)
                nCount = nCountFit;

            /*
            CRect rcBounds = rcClip;
            rcBounds.left = ptOrigin.x;
            rcBounds.right = rcBounds.left + char_width() * nCount;
            pdc->ExtTextOut(rcBounds.left, rcBounds.top, ETO_OPAQUE, &rcBounds, nullptr, 0, nullptr);
            */
            ::ExtTextOut(pdc, ptOrigin.x, ptOrigin.y, ETO_CLIPPED, &rcClip, line.c_str(), nCount, nullptr);
        }
        ptOrigin.x += char_width() * line.size();
    }
}

void text_view::DrawLineHelper(HDC pdc, text_location &ptOrigin, const CRect &rcClip, int nColorIndex, const wchar_t * pszChars, int nOffset, int nCount, text_location ptTextPos) const
{
    if (nCount > 0)
    {
        if (m_bFocused || m_bShowInactiveSelection)
        {
            int nSelBegin = 0, nSelEnd = 0;
            if (m_ptDrawSel._start.y > ptTextPos.y)
            {
                nSelBegin = nCount;
            }
            else
            if (m_ptDrawSel._start.y == ptTextPos.y)
            {
                nSelBegin = m_ptDrawSel._start.x - ptTextPos.x;
                if (nSelBegin < 0)
                    nSelBegin = 0;
                if (nSelBegin > nCount)
                    nSelBegin = nCount;
            }
            if (m_ptDrawSel._end.y > ptTextPos.y)
            {
                nSelEnd = nCount;
            }
            else
            if (m_ptDrawSel._end.y == ptTextPos.y)
            {
                nSelEnd = m_ptDrawSel._end.x - ptTextPos.x;
                if (nSelEnd < 0)
                    nSelEnd = 0;
                if (nSelEnd > nCount)
                    nSelEnd = nCount;
            }

            assert(nSelBegin >= 0 && nSelBegin <= nCount);
            assert(nSelEnd >= 0 && nSelEnd <= nCount);
            assert(nSelBegin <= nSelEnd);

            //	Draw part of the text before selection
            if (nSelBegin > 0)
            {
                DrawLineHelperImpl(pdc, ptOrigin, rcClip, pszChars, nOffset, nSelBegin);
            }
            if (nSelBegin < nSelEnd)
            {
                COLORREF crOldBk = SetBkColor(pdc, GetColor(IHighlight::COLORINDEX_SELBKGND));
                COLORREF crOldText = SetTextColor(pdc, GetColor(IHighlight::COLORINDEX_SELTEXT));
                DrawLineHelperImpl(pdc, ptOrigin, rcClip, pszChars, nOffset + nSelBegin, nSelEnd - nSelBegin);
                SetBkColor(pdc, crOldBk);
                SetTextColor(pdc, crOldText);
            }
            if (nSelEnd < nCount)
            {
                DrawLineHelperImpl(pdc, ptOrigin, rcClip, pszChars, nOffset + nSelEnd, nCount - nSelEnd);
            }
        }
        else
        {
            DrawLineHelperImpl(pdc, ptOrigin, rcClip, pszChars, nOffset, nCount);
        }
    }
}

void text_view::GetLineColors(int lineIndex, COLORREF &crBkgnd, COLORREF &crText, bool &bDrawWhitespace) const
{
    bDrawWhitespace = true;
    crText = RGB(255, 255, 255);
    crBkgnd = CLR_NONE;
    crText = CLR_NONE;
    bDrawWhitespace = false;
}

DWORD text_view::GetParseCookie(int lineIndex) const
{
    const auto invalid = (DWORD) -1;
    int line_count = _buffer.size();

    if (_parseCookies.empty())
    {
        _parseCookies.insert(_parseCookies.begin(), line_count, invalid);
    }

    if (lineIndex < 0)
        return 0;

    if (_parseCookies[lineIndex] != invalid)
        return _parseCookies[lineIndex];

    int i = lineIndex;
    while (i >= 0 && _parseCookies[i] == invalid)
        i--;
    i++;

    int nBlocks;
    while (i <= lineIndex)
    {
        DWORD dwCookie = 0;
        if (i > 0)
            dwCookie = _parseCookies[i - 1];
        assert(dwCookie != (DWORD) -1);

        const auto &line = _buffer[i];
        _parseCookies[i] = _highlight->ParseLine(dwCookie, line, nullptr, nBlocks);
        assert(_parseCookies[i] != (DWORD) -1);
        i++;
    }

    return _parseCookies[lineIndex];
}



void text_view::DrawSingleLine(HDC pdc, const CRect &rc, int lineIndex) const
{
    if (lineIndex == -1)
    {
        //	Draw line beyond the text
        FillSolidRect(pdc, rc, GetColor(IHighlight::COLORINDEX_WHITESPACE));
    }
    else
    {
        //	Acquire the background color for the current line
        auto bDrawWhitespace = false;
        COLORREF crBkgnd, crText;
        GetLineColors(lineIndex, crBkgnd, crText, bDrawWhitespace);

        if (crBkgnd == CLR_NONE)
            crBkgnd = GetColor(IHighlight::COLORINDEX_BKGND);

        const auto &line = _buffer[lineIndex];

        if (line.empty())
        {
            //	Draw the empty line
            CRect rect = rc;
            if ((m_bFocused || m_bShowInactiveSelection) && IsInsideSelBlock(text_location(0, lineIndex)))
            {
                FillSolidRect(pdc, rect.left, rect.top, char_width(), rect.Height(), GetColor(IHighlight::COLORINDEX_SELBKGND));
                rect.left += char_width();
            }
            FillSolidRect(pdc, rect, bDrawWhitespace ? crBkgnd : GetColor(IHighlight::COLORINDEX_WHITESPACE));
            return;
        }

        //	Parse the line
        auto nLength = line.size();
        auto pBuf = (IHighlight::TEXTBLOCK *) _malloca(sizeof(IHighlight::TEXTBLOCK) * nLength * 3);
        auto nBlocks = 0;
        auto cookie = GetParseCookie(lineIndex - 1);

        _parseCookies[lineIndex] = _highlight->ParseLine(cookie, line, pBuf, nBlocks);

        //	Draw the line text
        text_location origin(rc.left - m_nOffsetChar * char_width(), rc.top);
        SetBkColor(pdc, crBkgnd);
        if (crText != CLR_NONE)
            SetTextColor(pdc, crText);

        auto bColorSet = false;
        auto pszChars = line.c_str();

        if (nBlocks > 0)
        {
            assert(pBuf[0].m_nCharPos >= 0 && pBuf[0].m_nCharPos <= nLength);
            if (crText == CLR_NONE)
                SetTextColor(pdc, GetColor(IHighlight::COLORINDEX_NORMALTEXT));
            SelectObject(pdc, GetFont(GetItalic(IHighlight::COLORINDEX_NORMALTEXT), GetBold(IHighlight::COLORINDEX_NORMALTEXT)));
            DrawLineHelper(pdc, origin, rc, IHighlight::COLORINDEX_NORMALTEXT, pszChars, 0, pBuf[0].m_nCharPos, text_location(0, lineIndex));
            for (int i = 0; i < nBlocks - 1; i++)
            {
                assert(pBuf[i].m_nCharPos >= 0 && pBuf[i].m_nCharPos <= nLength);
                if (crText == CLR_NONE)
                    SetTextColor(pdc, GetColor(pBuf[i].m_nColorIndex));
                SelectObject(pdc, GetFont(GetItalic(pBuf[i].m_nColorIndex), GetBold(pBuf[i].m_nColorIndex)));
                DrawLineHelper(pdc, origin, rc, pBuf[i].m_nColorIndex, pszChars,
                    pBuf[i].m_nCharPos, pBuf[i + 1].m_nCharPos - pBuf[i].m_nCharPos,
                    text_location(pBuf[i].m_nCharPos, lineIndex));
            }
            assert(pBuf[nBlocks - 1].m_nCharPos >= 0 && pBuf[nBlocks - 1].m_nCharPos <= nLength);
            if (crText == CLR_NONE)
                SetTextColor(pdc, GetColor(pBuf[nBlocks - 1].m_nColorIndex));
            SelectObject(pdc, GetFont(GetItalic(pBuf[nBlocks - 1].m_nColorIndex),
                GetBold(pBuf[nBlocks - 1].m_nColorIndex)));
            DrawLineHelper(pdc, origin, rc, pBuf[nBlocks - 1].m_nColorIndex, pszChars,
                pBuf[nBlocks - 1].m_nCharPos, nLength - pBuf[nBlocks - 1].m_nCharPos,
                text_location(pBuf[nBlocks - 1].m_nCharPos, lineIndex));
        }
        else
        {
            if (crText == CLR_NONE)
                SetTextColor(pdc, GetColor(IHighlight::COLORINDEX_NORMALTEXT));
            SelectObject(pdc, GetFont(GetItalic(IHighlight::COLORINDEX_NORMALTEXT), GetBold(IHighlight::COLORINDEX_NORMALTEXT)));
            DrawLineHelper(pdc, origin, rc, IHighlight::COLORINDEX_NORMALTEXT, pszChars, 0, nLength, text_location(0, lineIndex));
        }

        //	Draw whitespaces to the left of the text
        auto frect = rc;
        if (origin.x > frect.left)
            frect.left = origin.x;
        if (frect.right > frect.left)
        {
            if ((m_bFocused || m_bShowInactiveSelection) && IsInsideSelBlock(text_location(nLength, lineIndex)))
            {
                FillSolidRect(pdc, frect.left, frect.top, char_width(), frect.Height(), GetColor(IHighlight::COLORINDEX_SELBKGND));
                frect.left += char_width();
            }
            if (frect.right > frect.left)
                FillSolidRect(pdc, frect, bDrawWhitespace ? crBkgnd : GetColor(IHighlight::COLORINDEX_WHITESPACE));
        }

        _freea(pBuf);
    }
}

COLORREF text_view::GetColor(int nColorIndex) const
{
    switch (nColorIndex)
    {
    case IHighlight::COLORINDEX_WHITESPACE:
    case IHighlight::COLORINDEX_BKGND:
        return RGB(30, 30, 30);
    case IHighlight::COLORINDEX_NORMALTEXT:
        return RGB(240, 240, 240);
    case IHighlight::COLORINDEX_SELMARGIN:
        return RGB(44, 44, 44);
    case IHighlight::COLORINDEX_PREPROCESSOR:
        return RGB(128, 128, 192);
    case IHighlight::COLORINDEX_COMMENT:
        return RGB(128, 128, 128);
    case IHighlight::COLORINDEX_NUMBER:
        return RGB(255, 128, 128);
    case IHighlight::COLORINDEX_OPERATOR:
        return RGB(128, 255, 128);
    case IHighlight::COLORINDEX_KEYWORD:
        return RGB(128, 128, 255);
    case IHighlight::COLORINDEX_SELBKGND:
        return RGB(88, 88, 88);
    case IHighlight::COLORINDEX_SELTEXT:
        return RGB(255, 255, 255);
    }
    return RGB(255, 0, 0);
}

//COLORREF text_view::GetColor(int nColorIndex)
//{
//	switch (nColorIndex)
//	{
//	case IHighlight::COLORINDEX_WHITESPACE:
//	case IHighlight::COLORINDEX_BKGND:
//		return ::GetSysColor(COLOR_WINDOW);
//	case IHighlight::COLORINDEX_NORMALTEXT:
//		return ::GetSysColor(COLOR_WINDOWTEXT);
//	case IHighlight::COLORINDEX_SELMARGIN:
//		return ::GetSysColor(COLOR_SCROLLBAR);
//	case IHighlight::COLORINDEX_PREPROCESSOR:
//		return RGB(0, 128, 192);
//	case IHighlight::COLORINDEX_COMMENT:
//		return RGB(128, 128, 128);
//		//	[JRT]: Enabled Support For Numbers...
//	case IHighlight::COLORINDEX_NUMBER:
//		return RGB(0x80, 0x00, 0x00);
//	case IHighlight::COLORINDEX_OPERATOR:
//		return RGB(0x00, 0x00, 0x00);
//	case IHighlight::COLORINDEX_KEYWORD:
//		return RGB(0, 0, 255);
//	case IHighlight::COLORINDEX_SELBKGND:
//		return RGB(0, 0, 0);
//	case IHighlight::COLORINDEX_SELTEXT:
//		return RGB(255, 255, 255);
//	}
//	return RGB(255, 0, 0);
//}


void text_view::DrawMargin(HDC pdc, const CRect &rect, int lineIndex) const
{
    if (!m_bSelMargin)
    {
        FillSolidRect(pdc, rect, GetColor(IHighlight::COLORINDEX_BKGND));
    }
    else
    {
        FillSolidRect(pdc, rect, GetColor(IHighlight::COLORINDEX_SELMARGIN));
    }
}

bool text_view::IsInsideSelBlock(text_location ptTextPos) const
{
    if (ptTextPos.y < m_ptDrawSel._start.y)
        return false;
    if (ptTextPos.y > m_ptDrawSel._end.y)
        return false;
    if (ptTextPos.y < m_ptDrawSel._end.y && ptTextPos.y > m_ptDrawSel._start.y)
        return true;
    if (m_ptDrawSel._start.y < m_ptDrawSel._end.y)
    {
        if (ptTextPos.y == m_ptDrawSel._end.y)
            return ptTextPos.x < m_ptDrawSel._end.x;
        assert(ptTextPos.y == m_ptDrawSel._start.y);
        return ptTextPos.x >= m_ptDrawSel._start.x;
    }
    assert(m_ptDrawSel._start.y == m_ptDrawSel._end.y);
    return ptTextPos.x >= m_ptDrawSel._start.x && ptTextPos.x < m_ptDrawSel._end.x;
}

bool text_view::IsInsideSelection(const text_location &ptTextPos) const
{
    PrepareSelBounds();
    return IsInsideSelBlock(ptTextPos);
}

void text_view::PrepareSelBounds() const
{
    if (_selection._start.y < _selection._end.y || (_selection._start.y == _selection._end.y && _selection._start.x < _selection._end.x))
    {
        m_ptDrawSel._start = _selection._start;
        m_ptDrawSel._end = _selection._end;
    }
    else
    {
        m_ptDrawSel._start = _selection._end;
        m_ptDrawSel._end = _selection._start;
    }
}

void text_view::OnDraw(HDC pdc)
{
    CRect rcClient;
    GetClientRect(rcClient);

    auto line_count = _buffer.size();
    PrepareSelBounds();

    auto cacheDC = CreateCompatibleDC(pdc);

    if (_backBuffer == nullptr)
    {
        _backBuffer = CreateCompatibleBitmap(pdc, rcClient.Width(), rcClient.Height());
    }

    auto pOldBitmap = SelectObject(cacheDC, _backBuffer);
    auto y = 0;
    auto nCurrentLine = m_nTopLine;

    while (y < rcClient.bottom)
    {
        auto nLineHeight = line_height(nCurrentLine);
        auto rcLine = rcClient;
        rcLine.bottom = rcLine.top + nLineHeight;

        CRect rcCacheMargin(0, y, margin_width(), y + nLineHeight);
        CRect rcCacheLine(margin_width(), y, rcLine.Width(), y + nLineHeight);

        if (nCurrentLine < line_count)
        {
            DrawMargin(cacheDC, rcCacheMargin, nCurrentLine);
            DrawSingleLine(cacheDC, rcCacheLine, nCurrentLine);
        }
        else
        {
            DrawMargin(cacheDC, rcCacheMargin, -1);
            DrawSingleLine(cacheDC, rcCacheLine, -1);
        }

        nCurrentLine++;
        y += nLineHeight;
    }

    BitBlt(pdc, 0, 0, rcClient.Width(), rcClient.Height(), cacheDC, 0, 0, SRCCOPY);
    SelectObject(cacheDC, pOldBitmap);
    DeleteDC(cacheDC);
}

void text_view::ResetView()
{
    m_bAutoIndent = true;
    m_nTopLine = 0;
    m_nOffsetChar = 0;
    m_nLineHeight = -1;
    m_nCharWidth = -1;
    m_tabSize = 4;
    m_nMaxLineLength = -1;
    m_nScreenLines = -1;
    m_nScreenChars = -1;
    m_nIdealCharPos = -1;
    m_ptAnchor.x = 0;
    m_ptAnchor.y = 0;

    _parseCookies.clear();
    _actualLineLengths.clear();

    m_ptCursorPos.x = 0;
    m_ptCursorPos.y = 0;
    _selection._start = _selection._end = m_ptCursorPos;
    m_bDragSelection = false;
    
    if (IsWindow())
    {
        update_caret();
        layout();
    }

    m_bLastSearch = false;
    m_bShowInactiveSelection = false;

    m_bMultipleSearch = false;	// More search

    
}

void text_view::update_caret()
{
    if (m_bFocused && !m_bCursorHidden &&
        CalculateActualOffset(m_ptCursorPos.y, m_ptCursorPos.x) >= m_nOffsetChar)
    {
        CreateSolidCaret(2, font_height());

        auto pt = text_to_client(m_ptCursorPos);
        SetCaretPos(pt.x, pt.y);
        ShowCaret();
    }
    else
    {
        HideCaret();
    }
}

int text_view::tab_size() const
{
    assert(m_tabSize >= 0 && m_tabSize <= 64);
    return m_tabSize;
}

void text_view::SetTabSize(int tabSize)
{
    assert(tabSize >= 0 && tabSize <= 64);
    if (m_tabSize != tabSize)
    {
        m_tabSize = tabSize;
        _actualLineLengths.clear();
        m_nMaxLineLength = -1;
        RecalcHorzScrollBar();
        Invalidate();
        update_caret();
    }
}

HFONT text_view::GetFont(bool bItalic /*= false*/, bool bBold /*= false*/) const
{
    int i = 0;
    if (bBold) i |= 1;
    if (bItalic) i |= 2;

    if (m_apFonts[i] == nullptr)
    {
        auto f = _font;
        f.lfWeight = bBold ? FW_BOLD : FW_NORMAL;
        f.lfItalic = (BYTE) bItalic;
        m_apFonts[i] = ::CreateFontIndirect(&f);
    }

    return m_apFonts[i];
}

void text_view::CalcLineCharDim() const
{
    win_dc dc(m_hWnd);
    auto pOldFont = dc.SelectFont(GetFont());

    CSize extent;
    GetTextExtentExPoint(dc, _T("X"), 1, 1, nullptr, nullptr, &extent);

    m_nLineHeight = std::max((int)extent.cy, 1);
    m_nCharWidth = extent.cx;
    /*
    TEXTMETRIC tm;
    if (pdc->GetTextMetrics(&tm))
    m_nCharWidth -= tm.tmOverhang;
    */
    dc.SelectFont(pOldFont);
}

int text_view::font_height() const
{
    if (m_nLineHeight == -1)
        CalcLineCharDim();

    return m_nLineHeight;
}

int text_view::line_offset(int lineIndex) const
{
    auto max = _buffer.size();
    auto line = Clamp(lineIndex, 0, max - 1);
    return _buffer[line]._y;
}

int text_view::line_height(int lineIndex) const
{
    if (m_nLineHeight == -1)
    {
        CalcLineCharDim();
    }

    return m_nLineHeight;
}

int text_view::char_width() const
{
    if (m_nCharWidth == -1)
    {
        CalcLineCharDim();
    }

    return m_nCharWidth;
}

int text_view::max_line_length() const
{
    if (m_nMaxLineLength == -1)
    {
        m_nMaxLineLength = 0;
        auto line_count = _buffer.size();

        for (int i = 0; i < line_count; i++)
        {
            int nActualLength = expanded_line_length(i);

            if (m_nMaxLineLength < nActualLength)
                m_nMaxLineLength = nActualLength;
        }
    }

    return m_nMaxLineLength;
}



int text_view::GetScreenLines() const
{
    
    return m_nScreenLines;
}

bool text_view::GetItalic(int nColorIndex) const
{
    return false;
}

bool text_view::GetBold(int nColorIndex) const
{
    return false;
}

int text_view::GetScreenChars() const
{
    if (m_nScreenChars == -1)
    {
        CRect rect;
        GetClientRect(&rect);
        m_nScreenChars = (rect.Width() - margin_width()) / char_width();
    }
    return m_nScreenChars;
}

void text_view::OnDestroy()
{
    /*if (_dropTarget != nullptr)
    {
    _dropTarget->Revoke();
    delete _dropTarget;
    _dropTarget = nullptr;
    }*/

    for (int i = 0; i < 4; i++)
    {
        if (m_apFonts[i] != nullptr)
        {
            DeleteObject(m_apFonts[i]);
            m_apFonts[i] = nullptr;
        }
    }
    if (_backBuffer != nullptr)
    {
        DeleteObject(_backBuffer);
        _backBuffer = nullptr;
    }
}

bool text_view::OnEraseBkgnd(HDC pdc)
{
    return true;
}

void text_view::OnSize(UINT nType, int cx, int cy)
{
    if (_backBuffer != nullptr)
    {
        DeleteObject(_backBuffer);
        _backBuffer = nullptr;
    }

    layout();

    RecalcVertScrollBar();
    RecalcHorzScrollBar();
}

void text_view::layout()
{
    CRect rect;
    GetClientRect(&rect);

    auto line_count = _buffer.size();
    auto y = 0;
    auto cy = font_height();

    for (int i = 0; i < line_count; i++)
    {
        auto &line = _buffer[i];

        line._y = y;
        line._cy = cy;

        y += cy;
    }

    m_nScreenLines = rect.Height() / font_height();
}


void text_view::RecalcVertScrollBar(bool bPositionOnly /*= false*/)
{
    SCROLLINFO si;
    si.cbSize = sizeof(si);
    if (bPositionOnly)
    {
        si.fMask = SIF_POS;
        si.nPos = m_nTopLine;
    }
    else
    {
        if (GetScreenLines() >= _buffer.size() && m_nTopLine > 0)
        {
            m_nTopLine = 0;
            Invalidate();
            update_caret();
        }
        si.fMask = SIF_PAGE | SIF_POS | SIF_RANGE;
        si.nMin = 0;
        si.nMax = _buffer.size() - 1;
        si.nPage = GetScreenLines();
        si.nPos = m_nTopLine;
    }
    SetScrollInfo(SB_VERT, &si);
}

void text_view::OnVScroll(UINT nSBCode, UINT nPos, HWND pScrollBar)
{

    //	Note we cannot use nPos because of its 16-bit nature
    SCROLLINFO si;
    si.cbSize = sizeof(si);
    si.fMask = SIF_ALL;
    GetScrollInfo(SB_VERT, &si);

    int nPageLines = GetScreenLines();
    int line_count = _buffer.size();

    int nNewTopLine;
    switch (nSBCode)
    {
    case SB_TOP:
        nNewTopLine = 0;
        break;
    case SB_BOTTOM:
        nNewTopLine = line_count - nPageLines + 1;
        break;
    case SB_LINEUP:
        nNewTopLine = m_nTopLine - 1;
        break;
    case SB_LINEDOWN:
        nNewTopLine = m_nTopLine + 1;
        break;
    case SB_PAGEUP:
        nNewTopLine = m_nTopLine - si.nPage + 1;
        break;
    case SB_PAGEDOWN:
        nNewTopLine = m_nTopLine + si.nPage - 1;
        break;
    case SB_THUMBPOSITION:
    case SB_THUMBTRACK:
        nNewTopLine = si.nTrackPos;
        break;
    default:
        return;
    }

    if (nNewTopLine < 0)
        nNewTopLine = 0;
    if (nNewTopLine >= line_count)
        nNewTopLine = line_count - 1;

    ScrollToLine(nNewTopLine);
}

void text_view::RecalcHorzScrollBar(bool bPositionOnly /*= false*/)
{
    //	Again, we cannot use nPos because it's 16-bit
    SCROLLINFO si;
    si.cbSize = sizeof(si);
    if (bPositionOnly)
    {
        si.fMask = SIF_POS;
        si.nPos = m_nOffsetChar;
    }
    else
    {
        if (GetScreenChars() >= max_line_length() && m_nOffsetChar > 0)
        {
            m_nOffsetChar = 0;
            Invalidate();
            update_caret();
        }
        si.fMask = SIF_PAGE | SIF_POS | SIF_RANGE;
        si.nMin = 0;
        si.nMax = max_line_length() - 1;
        si.nPage = GetScreenChars();
        si.nPos = m_nOffsetChar;
    }
    SetScrollInfo(SB_HORZ, &si);
}

void text_view::OnHScroll(UINT nSBCode, UINT nPos, HWND pScrollBar)
{

    SCROLLINFO si;
    si.cbSize = sizeof(si);
    si.fMask = SIF_ALL;
    GetScrollInfo(SB_HORZ, &si);

    int nPageChars = GetScreenChars();
    int nMaxLineLength = max_line_length();

    int nNewOffset;
    switch (nSBCode)
    {
    case SB_LEFT:
        nNewOffset = 0;
        break;
    case SB_BOTTOM:
        nNewOffset = nMaxLineLength - nPageChars + 1;
        break;
    case SB_LINEUP:
        nNewOffset = m_nOffsetChar - 1;
        break;
    case SB_LINEDOWN:
        nNewOffset = m_nOffsetChar + 1;
        break;
    case SB_PAGEUP:
        nNewOffset = m_nOffsetChar - si.nPage + 1;
        break;
    case SB_PAGEDOWN:
        nNewOffset = m_nOffsetChar + si.nPage - 1;
        break;
    case SB_THUMBPOSITION:
    case SB_THUMBTRACK:
        nNewOffset = si.nTrackPos;
        break;
    default:
        return;
    }

    if (nNewOffset >= nMaxLineLength)
        nNewOffset = nMaxLineLength - 1;
    if (nNewOffset < 0)
        nNewOffset = 0;
    ScrollToChar(nNewOffset, true);
    update_caret();
}

bool text_view::OnSetCursor(CWindow wnd, UINT nHitTest, UINT message)
{
    static auto arrow = ::LoadCursor(nullptr, MAKEINTRESOURCE(IDC_ARROW));
    static auto beam = ::LoadCursor(nullptr, MAKEINTRESOURCE(IDC_IBEAM));

    if (nHitTest == HTCLIENT)
    {
        CPoint pt;
        ::GetCursorPos(&pt);
        ScreenToClient(&pt);

        if (pt.x < margin_width())
        {
            ::SetCursor(arrow);
        }
        else
        {
            auto ptText = client_to_text(pt);
            PrepareSelBounds();

            if (IsInsideSelBlock(ptText))
            {
                if (!m_bDisableDragAndDrop)
                {
                    ::SetCursor(arrow);
                }
            }
            else
            {
                ::SetCursor(beam);
            }
        }
        return true;
    }
    return false;
}

int text_view::client_to_line(const CPoint &point) const
{
    auto line_count = _buffer.size();
    auto y = m_nTopLine;

    while (y < line_count)
    {
        auto const &line = _buffer[y];

        if (line._y >= point.y && (line._y + line._cy) < point.y)
        {
            return y;
        }

        y += 1;
    }

    return -1;
}

text_location text_view::client_to_text(const CPoint &point) const
{
    auto line_count = _buffer.size();

    text_location pt;
    pt.y = client_to_line(point);

    if (pt.y >= 0 && pt.y < line_count)
    {
        const auto &line = _buffer[pt.y];

        int nPos = m_nOffsetChar + (point.x - margin_width()) / char_width();
        if (nPos < 0)
            nPos = 0;

        int i = 0, nCurPos = 0;
        int tabSize = tab_size();

        while (i < line.size())
        {
            if (line[i] == _T('\t'))
                nCurPos += (tabSize - nCurPos % tabSize);
            else
                nCurPos++;

            if (nCurPos > nPos)
                break;

            i++;
        }

        pt.x = i;
    }

    return pt;
}

int text_view::top_offset() const
{
    auto result = 0;

    if (!_buffer.empty() || m_nTopLine <= 0)
    {
        result = _buffer[m_nTopLine]._y;
    }

    return result;
}

CPoint text_view::text_to_client(const text_location &point) const
{
    CPoint pt;   

    if (point.y >= 0 && point.y < _buffer.size())
    {
        pt.y = line_offset(point.y) - top_offset();
        pt.x = 0;

        auto tabSize = tab_size();
        const auto &line = _buffer[point.y];

        for (int i = 0; i < point.x; i++)
        {
            if (line[i] == _T('\t'))
            {
                pt.x += (tabSize - pt.x % tabSize);
            }
            else
            {
                pt.x++;
            }
        }

        pt.x = (pt.x - m_nOffsetChar) * char_width() + margin_width();
    }

    return pt;
}

void text_view::invalidate_lines(int nLine1, int nLine2, bool bInvalidateMargin /*= false*/)
{
    bInvalidateMargin = true;

    CRect rcInvalid;
    GetClientRect(&rcInvalid);

    if (nLine2 == -1)
    {
        if (!bInvalidateMargin)
            rcInvalid.left += margin_width();

        rcInvalid.top = line_offset(nLine1) - top_offset();
    }
    else
    {
        if (nLine2 < nLine1)
        {
            std::swap(nLine1, nLine2);
        }

        if (!bInvalidateMargin)
            rcInvalid.left += margin_width();

        rcInvalid.top = line_offset(nLine1) - top_offset();
        rcInvalid.bottom = line_offset(nLine2) + line_height(nLine2) - top_offset();
    }

    InvalidateRect(rcInvalid, false);
}

void text_view::select(const text_selection &sel)
{
    invalidate_lines(sel._start.y, sel._end.y);
    invalidate_lines(_selection._start.y, _selection._end.y);

    _selection = sel;
}

void text_view::OnSetFocus(CWindow oldWnd)
{
    m_bFocused = true;
    if (_selection._start != _selection._end)
        invalidate_lines(_selection._start.y, _selection._end.y);
    update_caret();
}


int text_view::CalculateActualOffset(int lineIndex, int nCharIndex)
{
    int nOffset = 0;

    if (lineIndex >= 0 && lineIndex < _buffer.size())
    {
        const auto &line = _buffer[lineIndex];
        auto tabSize = tab_size();

        for (auto i = 0; i < nCharIndex; i++)
        {
            if (line[i] == _T('\t'))
                nOffset += (tabSize - nOffset % tabSize);
            else
                nOffset++;
        }
    }

    return nOffset;
}

int text_view::ApproxActualOffset(int lineIndex, int nOffset)
{
    if (nOffset == 0)
        return 0;

    const auto &line = _buffer[lineIndex];
    const auto nLength = line.size();

    int nCurrentOffset = 0;
    int tabSize = tab_size();

    for (int i = 0; i < nLength; i++)
    {
        if (line[i] == _T('\t'))
        {
            nCurrentOffset += (tabSize - nCurrentOffset % tabSize);
        }
        else
        {
            nCurrentOffset++;
        }

        if (nCurrentOffset >= nOffset)
        {
            if (nOffset <= nCurrentOffset - tabSize / 2)
                return i;
            return i + 1;
        }
    }
    return nLength;
}

void text_view::EnsureVisible(text_location pt)
{
    //	Scroll vertically
    int line_count = _buffer.size();
    int nNewTopLine = m_nTopLine;

    if (pt.y >= nNewTopLine + GetScreenLines())
    {
        nNewTopLine = pt.y - GetScreenLines() + 1;
    }
    else if (pt.y < nNewTopLine)
    {
        nNewTopLine = pt.y;
    }

    if (nNewTopLine < 0)
        nNewTopLine = 0;
    if (nNewTopLine >= line_count)
        nNewTopLine = line_count - 1;

    if (m_nTopLine != nNewTopLine)
    {
        ScrollToLine(nNewTopLine);
    }

    //	Scroll horizontally
    int nActualPos = CalculateActualOffset(pt.y, pt.x);
    int nNewOffset = m_nOffsetChar;
    if (nActualPos > nNewOffset + GetScreenChars())
    {
        nNewOffset = nActualPos - GetScreenChars();
    }
    if (nActualPos < nNewOffset)
    {
        nNewOffset = nActualPos;
    }

    if (nNewOffset >= max_line_length())
        nNewOffset = max_line_length() - 1;
    if (nNewOffset < 0)
        nNewOffset = 0;

    if (m_nOffsetChar != nNewOffset)
    {
        ScrollToChar(nNewOffset);
        update_caret();
    }
}

void text_view::OnKillFocus(CWindow newWnd)
{
    m_bFocused = false;
    update_caret();
    if (_selection._start != _selection._end)
        invalidate_lines(_selection._start.y, _selection._end.y);
    if (m_bDragSelection)
    {
        ReleaseCapture();
        KillTimer(m_nDragSelTimer);
        m_bDragSelection = false;
    }
}

void text_view::OnSysColorChange()
{
    Invalidate();
}

std::vector<std::wstring> text_view::Text(const text_selection &selection) const
{
    return _buffer.text(selection);
}

void text_view::invalidate_line(int index)
{
    if (_parseCookies.size() > index) _parseCookies[index] = (DWORD) -1;
    if (_actualLineLengths.size() > index) _actualLineLengths[index] = 0;

    invalidate_lines(index, index + 1, true);

    int nActualLength = expanded_line_length(index);

    if (m_nMaxLineLength < nActualLength)
        m_nMaxLineLength = nActualLength;

    RecalcHorzScrollBar();
}

void text_view::invalidate_view()
{
    ResetView();
    RecalcVertScrollBar();
    RecalcHorzScrollBar();
    Invalidate(false);
}

HINSTANCE text_view::GetResourceHandle()
{
    return _AtlBaseModule.GetResourceInstance();
}

int text_view::OnCreate()
{
    memset(&_font, 0, sizeof(_font));
    _font.lfWeight = FW_NORMAL;
    _font.lfCharSet = ANSI_CHARSET;
    _font.lfOutPrecision = OUT_DEFAULT_PRECIS;
    _font.lfClipPrecision = CLIP_DEFAULT_PRECIS;
    _font.lfQuality = CLEARTYPE_NATURAL_QUALITY;
    _font.lfPitchAndFamily = DEFAULT_PITCH | FF_DONTCARE;
    wcscpy_s(_font.lfFaceName, L"Consolas");

    m_nScreenLines = -1;
    m_nScreenChars = -1;
    m_nCharWidth = -1;
    m_nLineHeight = -1;

    /*_dropTarget = new CEditDropTargetImpl(this);
    if (! _dropTarget->Register(this))
    {
    TRACE0("Warning: Unable to register drop target for text_view.\n");
    delete _dropTarget;
    _dropTarget = nullptr;
    }*/

    return 0;
}

void text_view::SetAnchor(const text_location &ptNewAnchor)
{
    m_ptAnchor = ptNewAnchor;
}

void text_view::SetCursorPos(const text_location &ptCursorPos)
{
    m_ptCursorPos = ptCursorPos;
    m_nIdealCharPos = CalculateActualOffset(m_ptCursorPos.y, m_ptCursorPos.x);
    update_caret();
}

void text_view::SetSelectionMargin(bool bSelMargin)
{
    if (m_bSelMargin != bSelMargin)
    {
        m_bSelMargin = bSelMargin;
        if (::IsWindow(m_hWnd))
        {
            m_nScreenChars = -1;
            Invalidate();
            RecalcHorzScrollBar();
        }
    }
}

void text_view::ShowCursor()
{
    m_bCursorHidden = false;
    update_caret();
}

void text_view::HideCursor()
{
    m_bCursorHidden = true;
    update_caret();
}

HGLOBAL text_view::PrepareDragData()
{
    PrepareSelBounds();
    if (m_ptDrawSel._start == m_ptDrawSel._end)
        return nullptr;

    auto text = Combine(Text(m_ptDrawSel));
    auto len = text.size() + 1;
    auto hData = ::GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, len * sizeof(wchar_t));

    if (hData == nullptr)
        return nullptr;

    auto pszData = (wchar_t*) ::GlobalLock(hData);
    wcscpy_s(pszData, len, text.c_str());
    ::GlobalUnlock(hData);

    m_ptDraggedText = m_ptDrawSel;

    return hData;
}



bool text_view::HighlightText(const text_location &ptStartPos, int nLength)
{
    m_ptCursorPos = ptStartPos;
    m_ptCursorPos.x += nLength;
    m_ptAnchor = m_ptCursorPos;
    select(text_selection(ptStartPos, m_ptCursorPos));
    update_caret();
    EnsureVisible(m_ptCursorPos);
    return true;
}


void text_view::Find(const std::wstring &text, DWORD flags)
{
    text_location loc;

    if (_buffer.Find(text, m_ptCursorPos, flags, true, &loc))
    {
        auto end = loc;
        end.x += text.size();
        Select(text_selection(loc, end));
        m_ptCursorPos = loc;
        update_caret();

        _lastFindWhat = text;
        m_dwLastSearchFlags = flags;
        m_bShowInactiveSelection = true;
    }
    //	CWinApp *pApp = AfxGetApp();
    //	assert(pApp != nullptr);
    //
    //	CFindTextDlg dlg(this);
    //	if (m_bLastSearch)
    //	{
    //		//	Get the latest search parameters
    //		dlg.m_bMatchCase = (m_dwLastSearchFlags & FIND_MATCH_CASE) != 0;
    //		dlg.m_bWholeWord = (m_dwLastSearchFlags & FIND_WHOLE_WORD) != 0;
    //		dlg.m_nDirection = (m_dwLastSearchFlags & FIND_DIRECTION_UP) != 0 ? 0 : 1;
    //		if (_lastFindWhat != nullptr)
    //			dlg.m_sText = _lastFindWhat;
    //	}
    //	else
    //	{
    //		//	Take search parameters from registry
    //		dlg.m_bMatchCase = pApp->GetProfileInt(REG_FIND_SUBKEY, REG_MATCH_CASE, false);
    //		dlg.m_bWholeWord = pApp->GetProfileInt(REG_FIND_SUBKEY, REG_WHOLE_WORD, false);
    //		dlg.m_nDirection = 1;		//	Search down
    //		dlg.m_sText = pApp->GetProfileString(REG_FIND_SUBKEY, REG_FIND_WHAT, _T(""));
    //	}
    //
    //	//	Take the current selection, if any
    //	if (HasSelection())
    //	{
    //		text_location ptSelStart, ptSelEnd;
    //		GetSelection(ptSelStart, ptSelEnd);		if (ptSelStart.y == ptSelEnd.y)
    //		{
    //			const wchar_t * pszChars = GetLineChars(ptSelStart.y);
    //			int nChars = ptSelEnd.x - ptSelStart.x;
    //			lstrcpyn(dlg.m_sText.GetBuffer(nChars + 1), pszChars + ptSelStart.x, nChars + 1);
    //			dlg.m_sText.ReleaseBuffer();
    //		}
    //	}
    //
    //	//	Execute Find dialog
    //	dlg.m_ptCurrentPos = m_ptCursorPos;		//	Search from cursor position
    //	m_bShowInactiveSelection = true;
    //	dlg.DoModal();
    //	m_bShowInactiveSelection = false;
    //
    //	//	Save search parameters for 'F3' command
    //	m_bLastSearch = true;
    //	if (_lastFindWhat != nullptr)
    //		free(_lastFindWhat);
    //	_lastFindWhat = _wcsdup(dlg.m_sText);
    //
    //	m_dwLastSearchFlags = 0;
    //	if (dlg.m_bMatchCase)
    //		m_dwLastSearchFlags |= FIND_MATCH_CASE;
    //	if (dlg.m_bWholeWord)
    //		m_dwLastSearchFlags |= FIND_WHOLE_WORD;
    //	if (dlg.m_nDirection == 0)
    //		m_dwLastSearchFlags |= FIND_DIRECTION_UP;
    //
    //	//	Save search parameters to registry
    //	pApp->WriteProfileInt(REG_FIND_SUBKEY, REG_MATCH_CASE, dlg.m_bMatchCase);
    //	pApp->WriteProfileInt(REG_FIND_SUBKEY, REG_WHOLE_WORD, dlg.m_bWholeWord);
    //	pApp->WriteProfileString(REG_FIND_SUBKEY, REG_FIND_WHAT, dlg.m_sText);
}

void text_view::OnEditRepeat()
{
    //if (m_bLastSearch)
    //{
    //	text_location ptFoundPos;
    //	if (! FindText(_lastFindWhat, m_ptCursorPos, m_dwLastSearchFlags, true, &ptFoundPos))
    //	{
    //		std::wstring prompt;
    //		prompt.Format(IDS_EDIT_TEXT_NOT_FOUND, _lastFindWhat);
    //		AfxMessageBox(prompt);
    //		return;
    //	}
    //	HighlightText(ptFoundPos, lstrlen(_lastFindWhat));
    //	m_bMultipleSearch = true;       // More search       
    //}
}



void text_view::OnEditFindPrevious()
{
    DWORD dwSaveSearchFlags = m_dwLastSearchFlags;
    if ((m_dwLastSearchFlags & FIND_DIRECTION_UP) != 0)
        m_dwLastSearchFlags &= ~FIND_DIRECTION_UP;
    else
        m_dwLastSearchFlags |= FIND_DIRECTION_UP;
    OnEditRepeat();
    m_dwLastSearchFlags = dwSaveSearchFlags;
}

void text_view::OnFilePageSetup()
{
    /*CWinApp *pApp = AfxGetApp();
    assert(pApp != nullptr);

    CPageSetupDialog dlg;
    dlg.m_psd.Flags &= ~PSD_INTHOUSANDTHSOFINCHES;
    dlg.m_psd.Flags |= PSD_INHUNDREDTHSOFMILLIMETERS | PSD_DISABLEORIENTATION | PSD_DISABLEPAPER;
    dlg.m_psd.rtMargin.left = pApp->GetProfileInt(REG_PAGE_SUBKEY, REG_MARGIN_LEFT, DEFAULT_PRINT_MARGIN);
    dlg.m_psd.rtMargin.right = pApp->GetProfileInt(REG_PAGE_SUBKEY, REG_MARGIN_RIGHT, DEFAULT_PRINT_MARGIN);
    dlg.m_psd.rtMargin.top = pApp->GetProfileInt(REG_PAGE_SUBKEY, REG_MARGIN_TOP, DEFAULT_PRINT_MARGIN);
    dlg.m_psd.rtMargin.bottom = pApp->GetProfileInt(REG_PAGE_SUBKEY, REG_MARGIN_BOTTOM, DEFAULT_PRINT_MARGIN);
    if (dlg.DoModal() == IDOK)
    {
    pApp->WriteProfileInt(REG_PAGE_SUBKEY, REG_MARGIN_LEFT, dlg.m_psd.rtMargin.left);
    pApp->WriteProfileInt(REG_PAGE_SUBKEY, REG_MARGIN_RIGHT, dlg.m_psd.rtMargin.right);
    pApp->WriteProfileInt(REG_PAGE_SUBKEY, REG_MARGIN_TOP, dlg.m_psd.rtMargin.top);
    pApp->WriteProfileInt(REG_PAGE_SUBKEY, REG_MARGIN_BOTTOM, dlg.m_psd.rtMargin.bottom);
    }*/
}

bool text_view::GetViewTabs() const
{
    return m_bViewTabs;
}

void text_view::SetViewTabs(bool bViewTabs)
{
    if (bViewTabs != m_bViewTabs)
    {
        m_bViewTabs = bViewTabs;
        if (::IsWindow(m_hWnd))
            Invalidate();
    }
}

bool text_view::GetSelectionMargin() const
{
    return m_bSelMargin;
}

int text_view::margin_width() const
{
    return m_bSelMargin ? 20 : 1;
}

bool text_view::GetDisableDragAndDrop() const
{
    return m_bDisableDragAndDrop;
}

void text_view::SetDisableDragAndDrop(bool bDDAD)
{
    m_bDisableDragAndDrop = bDDAD;
}

void text_view::MoveLeft(bool selecting)
{
    PrepareSelBounds();

    if (m_ptDrawSel._start != m_ptDrawSel._end && !selecting)
    {
        m_ptCursorPos = m_ptDrawSel._start;
    }
    else
    {
        if (m_ptCursorPos.x == 0)
        {
            if (m_ptCursorPos.y > 0)
            {
                m_ptCursorPos.y--;
                m_ptCursorPos.x = _buffer[m_ptCursorPos.y].size();
            }
        }
        else
            m_ptCursorPos.x--;
    }
    m_nIdealCharPos = CalculateActualOffset(m_ptCursorPos.y, m_ptCursorPos.x);
    EnsureVisible(m_ptCursorPos);
    update_caret();
    if (!selecting)
        m_ptAnchor = m_ptCursorPos;
    select(text_selection(m_ptAnchor, m_ptCursorPos));
}

void text_view::MoveRight(bool selecting)
{
    PrepareSelBounds();
    if (m_ptDrawSel._start != m_ptDrawSel._end && !selecting)
    {
        m_ptCursorPos = m_ptDrawSel._end;
    }
    else
    {
        if (m_ptCursorPos.x == _buffer[m_ptCursorPos.y].size())
        {
            if (m_ptCursorPos.y < _buffer.size() - 1)
            {
                m_ptCursorPos.y++;
                m_ptCursorPos.x = 0;
            }
        }
        else
            m_ptCursorPos.x++;
    }
    m_nIdealCharPos = CalculateActualOffset(m_ptCursorPos.y, m_ptCursorPos.x);
    EnsureVisible(m_ptCursorPos);
    update_caret();
    if (!selecting)
        m_ptAnchor = m_ptCursorPos;
    select(text_selection(m_ptAnchor, m_ptCursorPos));
}

void text_view::MoveWordLeft(bool selecting)
{
    PrepareSelBounds();

    if (m_ptDrawSel._start != m_ptDrawSel._end && !selecting)
    {
        MoveLeft(selecting);
        return;
    }

    if (m_ptCursorPos.x == 0)
    {
        if (m_ptCursorPos.y == 0)
            return;

        m_ptCursorPos.y--;
        m_ptCursorPos.x = _buffer[m_ptCursorPos.y].size();
    }

    const auto &line = _buffer[m_ptCursorPos.y];
    auto nPos = m_ptCursorPos.x;

    WordToLeft(m_ptCursorPos);

    while (nPos > 0 && iswspace(line[nPos - 1]))
        nPos--;

    if (nPos > 0)
    {
        nPos--;
        if (iswalnum(line[nPos]) || line[nPos] == _T('_'))
        {
            while (nPos > 0 && (iswalnum(line[nPos - 1]) || line[nPos - 1] == _T('_')))
                nPos--;
        }
        else
        {
            while (nPos > 0 && !iswalnum(line[nPos - 1])
                && line[nPos - 1] != _T('_') && !iswspace(line[nPos - 1]))
                nPos--;
        }
    }

    m_ptCursorPos.x = nPos;
    m_nIdealCharPos = CalculateActualOffset(m_ptCursorPos.y, m_ptCursorPos.x);
    EnsureVisible(m_ptCursorPos);
    update_caret();
    if (!selecting)
        m_ptAnchor = m_ptCursorPos;
    select(text_selection(m_ptAnchor, m_ptCursorPos));
}

void text_view::MoveWordRight(bool selecting)
{
    PrepareSelBounds();
    if (m_ptDrawSel._start != m_ptDrawSel._end && !selecting)
    {
        MoveRight(selecting);
        return;
    }

    if (m_ptCursorPos.x == _buffer[m_ptCursorPos.y].size())
    {
        if (m_ptCursorPos.y == _buffer.size() - 1)
            return;
        m_ptCursorPos.y++;
        m_ptCursorPos.x = 0;
    }

    auto nLength = _buffer[m_ptCursorPos.y].size();

    if (m_ptCursorPos.x == nLength)
    {
        MoveRight(selecting);
        return;
    }

    const auto &line = _buffer[m_ptCursorPos.y];
    int nPos = m_ptCursorPos.x;

    if (iswalnum(line[nPos]) || line[nPos] == _T('_'))
    {
        while (nPos < nLength && iswalnum(line[nPos]) || line[nPos] == _T('_'))
            nPos++;
    }
    else
    {
        while (nPos < nLength && !iswalnum(line[nPos])
            && line[nPos] != _T('_') && !iswspace(line[nPos]))
            nPos++;
    }

    while (nPos < nLength && iswspace(line[nPos]))
        nPos++;

    m_ptCursorPos.x = nPos;
    m_nIdealCharPos = CalculateActualOffset(m_ptCursorPos.y, m_ptCursorPos.x);
    EnsureVisible(m_ptCursorPos);
    update_caret();
    if (!selecting)
        m_ptAnchor = m_ptCursorPos;
    select(text_selection(m_ptAnchor, m_ptCursorPos));
}

void text_view::MoveUp(bool selecting)
{
    PrepareSelBounds();
    if (m_ptDrawSel._start != m_ptDrawSel._end && !selecting)
        m_ptCursorPos = m_ptDrawSel._start;

    if (m_ptCursorPos.y > 0)
    {
        if (m_nIdealCharPos == -1)
            m_nIdealCharPos = CalculateActualOffset(m_ptCursorPos.y, m_ptCursorPos.x);
        m_ptCursorPos.y--;
        m_ptCursorPos.x = ApproxActualOffset(m_ptCursorPos.y, m_nIdealCharPos);

        auto size = _buffer[m_ptCursorPos.y].size();

        if (m_ptCursorPos.x > size)
            m_ptCursorPos.x = size;
    }
    EnsureVisible(m_ptCursorPos);
    update_caret();
    if (!selecting)
        m_ptAnchor = m_ptCursorPos;

    select(text_selection(m_ptAnchor, m_ptCursorPos));
}

void text_view::MoveDown(bool selecting)
{
    PrepareSelBounds();
    if (m_ptDrawSel._start != m_ptDrawSel._end && !selecting)
        m_ptCursorPos = m_ptDrawSel._end;

    if (m_ptCursorPos.y < _buffer.size() - 1)
    {
        if (m_nIdealCharPos == -1)
            m_nIdealCharPos = CalculateActualOffset(m_ptCursorPos.y, m_ptCursorPos.x);

        m_ptCursorPos.y++;
        m_ptCursorPos.x = ApproxActualOffset(m_ptCursorPos.y, m_nIdealCharPos);

        auto size = _buffer[m_ptCursorPos.y].size();

        if (m_ptCursorPos.x > size)
            m_ptCursorPos.x = size;
    }
    EnsureVisible(m_ptCursorPos);
    update_caret();
    if (!selecting)
        m_ptAnchor = m_ptCursorPos;
    select(text_selection(m_ptAnchor, m_ptCursorPos));
}

void text_view::MoveHome(bool selecting)
{
    const auto &line = _buffer[m_ptCursorPos.y];

    int nHomePos = 0;
    while (nHomePos < line.size() && iswspace(line[nHomePos]))
        nHomePos++;
    if (nHomePos == line.size() || m_ptCursorPos.x == nHomePos)
        m_ptCursorPos.x = 0;
    else
        m_ptCursorPos.x = nHomePos;
    m_nIdealCharPos = CalculateActualOffset(m_ptCursorPos.y, m_ptCursorPos.x);
    EnsureVisible(m_ptCursorPos);
    update_caret();
    if (!selecting)
        m_ptAnchor = m_ptCursorPos;
    select(text_selection(m_ptAnchor, m_ptCursorPos));
}

void text_view::MoveEnd(bool selecting)
{
    m_ptCursorPos.x = _buffer[m_ptCursorPos.y].size();
    m_nIdealCharPos = CalculateActualOffset(m_ptCursorPos.y, m_ptCursorPos.x);
    EnsureVisible(m_ptCursorPos);
    update_caret();
    if (!selecting)
        m_ptAnchor = m_ptCursorPos;
    select(text_selection(m_ptAnchor, m_ptCursorPos));
}

void text_view::MovePgUp(bool selecting)
{
    int nNewTopLine = m_nTopLine - GetScreenLines() + 1;
    if (nNewTopLine < 0)
        nNewTopLine = 0;
    if (m_nTopLine != nNewTopLine)
    {
        ScrollToLine(nNewTopLine);
    }

    m_ptCursorPos.y -= GetScreenLines() - 1;
    if (m_ptCursorPos.y < 0)
        m_ptCursorPos.y = 0;

    auto size = _buffer[m_ptCursorPos.y].size();

    if (m_ptCursorPos.x > size)
        m_ptCursorPos.x = size;

    m_nIdealCharPos = CalculateActualOffset(m_ptCursorPos.y, m_ptCursorPos.x);
    EnsureVisible(m_ptCursorPos);	//todo: no vertical scroll
    update_caret();
    if (!selecting)
        m_ptAnchor = m_ptCursorPos;
    select(text_selection(m_ptAnchor, m_ptCursorPos));
}

void text_view::MovePgDn(bool selecting)
{
    int nNewTopLine = m_nTopLine + GetScreenLines() - 1;
    if (nNewTopLine >= _buffer.size())
        nNewTopLine = _buffer.size() - 1;
    if (m_nTopLine != nNewTopLine)
    {
        ScrollToLine(nNewTopLine);
    }

    m_ptCursorPos.y += GetScreenLines() - 1;
    if (m_ptCursorPos.y >= _buffer.size())
        m_ptCursorPos.y = _buffer.size() - 1;

    auto size = _buffer[m_ptCursorPos.y].size();

    if (m_ptCursorPos.x > size)
        m_ptCursorPos.x = size;
    m_nIdealCharPos = CalculateActualOffset(m_ptCursorPos.y, m_ptCursorPos.x);
    EnsureVisible(m_ptCursorPos);	//todo: no vertical scroll
    update_caret();
    if (!selecting)
        m_ptAnchor = m_ptCursorPos;
    select(text_selection(m_ptAnchor, m_ptCursorPos));
}

void text_view::MoveCtrlHome(bool selecting)
{
    m_ptCursorPos.x = 0;
    m_ptCursorPos.y = 0;
    m_nIdealCharPos = CalculateActualOffset(m_ptCursorPos.y, m_ptCursorPos.x);
    EnsureVisible(m_ptCursorPos);
    update_caret();
    if (!selecting)
        m_ptAnchor = m_ptCursorPos;
    select(text_selection(m_ptAnchor, m_ptCursorPos));
}

void text_view::MoveCtrlEnd(bool selecting)
{
    m_ptCursorPos.y = _buffer.size() - 1;
    m_ptCursorPos.x = _buffer[m_ptCursorPos.y].size();
    m_nIdealCharPos = CalculateActualOffset(m_ptCursorPos.y, m_ptCursorPos.x);
    EnsureVisible(m_ptCursorPos);
    update_caret();
    if (!selecting)
        m_ptAnchor = m_ptCursorPos;
    select(text_selection(m_ptAnchor, m_ptCursorPos));
}

void text_view::ScrollUp()
{
    if (m_nTopLine > 0)
    {
        ScrollToLine(m_nTopLine - 1);
    }
}

void text_view::ScrollDown()
{
    if (m_nTopLine < _buffer.size() - 1)
    {
        ScrollToLine(m_nTopLine + 1);
    }
}

void text_view::ScrollLeft()
{
    if (m_nOffsetChar > 0)
    {
        ScrollToChar(m_nOffsetChar - 1);
        update_caret();
    }
}

void text_view::ScrollRight()
{
    if (m_nOffsetChar < max_line_length() - 1)
    {
        ScrollToChar(m_nOffsetChar + 1);
        update_caret();
    }
}

text_location text_view::WordToRight(text_location pt) const
{
    const auto &line = _buffer[pt.y];

    while (pt.x < line.size())
    {
        if (!iswalnum(line[pt.x]) && line[pt.x] != _T('_'))
            break;
        pt.x++;
    }
    return pt;
}

text_location text_view::WordToLeft(text_location pt) const
{
    const auto &line = _buffer[pt.y];

    while (pt.x > 0)
    {
        if (!iswalnum(line[pt.x - 1]) && line[pt.x - 1] != _T('_'))
            break;
        pt.x--;
    }
    return pt;
}

void text_view::SelectAll()
{
    int line_count = _buffer.size();
    m_ptCursorPos.x = _buffer[line_count - 1].size();
    m_ptCursorPos.y = line_count - 1;
    select(text_selection(text_location(0, 0), m_ptCursorPos));
    update_caret();
}

//struct SpError
//{
//	Crect rcArea;
//	string::wstring word;
//	int posn;
//
//	SpError() {};
//	SpError(const SpError &other) : rcArea(other.rcArea), word(other.word), posn(other.posn) {};
//	SpError &operator=(const SpError &other) { rcArea = other.rcArea; word = other.word; posn = other.posn; return *this; };
//};

void text_view::OnContextMenu(const CPoint &point, UINT nFlags)
{
    CPoint clientLocation(point);
    ScreenToClient(&clientLocation);

    // Find out if we're over any errors
    /*SpError thisError;
    bool bFound = false;

    for (auto split = _errors.begin(); !bFound && split != _errors.end(); ++split)
    {
    if (split->rcArea.Contains(clientPt))
    {
    thisError = *split;
    bFound = true;
    }
    }

    if (!bFound)
    {
    bHandled = FALSE;
    return 0;
    }*/


    auto menu = CreatePopupMenu();

    if (menu)
    {
        m_ptAnchor = m_ptCursorPos = client_to_text(clientLocation);

        auto selection = WordSelection();
        Select(selection);

        auto text = Combine(Text(selection));

        std::map<UINT, std::wstring> replacements;

        if (!selection.empty())
        {
            auto id = 1000U;


            for (auto option : _highlight->Suggest(text))
            {
                auto word = Replace(option, L"&", L"&&");
                AppendMenu(menu, MF_ENABLED, id, word.c_str());
                replacements[id] = option;
                id++;
            }

            if (replacements.size() > 0)
            {
                AppendMenu(menu, MF_SEPARATOR, 0, nullptr);
            }

            if (_highlight->CanAdd(text))
            {
                AppendMenu(menu, MF_ENABLED, ID_FILE_NEW, String::Format(L"Add '%s'", text.c_str()).c_str());
                AppendMenu(menu, MF_SEPARATOR, 0, nullptr);
            }
        }

        if (_buffer.can_undo())
        {
            AppendMenu(menu, MF_ENABLED, ID_EDIT_UNDO, L"undo");
            AppendMenu(menu, MF_SEPARATOR, 0, nullptr);
        }

        AppendMenu(menu, MF_ENABLED, ID_EDIT_CUT, L"Cut");
        AppendMenu(menu, MF_ENABLED, ID_EDIT_COPY, L"Copy");
        AppendMenu(menu, MF_ENABLED, ID_EDIT_PASTE, L"Paste");
        AppendMenu(menu, MF_ENABLED, ID_EDIT_DELETE, L"erase");
        AppendMenu(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenu(menu, MF_ENABLED, ID_EDIT_SELECT_ALL, L"Select All");

        auto result = TrackPopupMenu(menu, TPM_TOPALIGN | TPM_LEFTALIGN | TPM_NONOTIFY | TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_VERNEGANIMATION, point.x, point.y, 0, m_hWnd, NULL);
        DestroyMenu(menu);

        switch (result)
        {
        case ID_EDIT_UNDO:
        case ID_EDIT_CUT:
        case ID_EDIT_COPY:
        case ID_EDIT_PASTE:
        case ID_EDIT_DELETE:
        case ID_EDIT_SELECT_ALL:
            OnCommand(result);
            break;

        case ID_FILE_NEW:
            _highlight->AddWord(text);
            invalidate_line(selection._start.y);
            break;

        default:

            if (replacements.find(result) != replacements.end())
            {
                _buffer.delete_text(selection);
                _buffer.insert_text(selection._start, replacements[result]);

                auto selection = WordSelection();
                Select(WordSelection());
            }
            break;
        }
    }
}

void text_view::OnLButtonDown(const CPoint &point, UINT nFlags)
{
    bool bShift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    bool bControl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;

    SetFocus();

    if (point.x < margin_width())
    {
        if (bControl)
        {
            SelectAll();
        }
        else
        {
            m_ptCursorPos = client_to_text(point);
            m_ptCursorPos.x = 0;				//	Force beginning of the line
            if (!bShift)
                m_ptAnchor = m_ptCursorPos;

            text_location ptStart, ptEnd;
            ptStart = m_ptAnchor;
            if (ptStart.y == _buffer.size() - 1)
                ptStart.x = _buffer[ptStart.y].size();
            else
            {
                ptStart.y++;
                ptStart.x = 0;
            }

            ptEnd = m_ptCursorPos;
            ptEnd.x = 0;

            m_ptCursorPos = ptEnd;
            update_caret();
            EnsureVisible(m_ptCursorPos);
            select(text_selection(ptStart, ptEnd));

            SetCapture();
            m_nDragSelTimer = SetTimer(RETHINKIFY_TIMER_DRAGSEL, 100, nullptr);
            assert(m_nDragSelTimer != 0);
            m_bWordSelection = false;
            m_bLineSelection = true;
            m_bDragSelection = true;
        }
    }
    else
    {
        text_location ptText = client_to_text(point);
        PrepareSelBounds();

        if ((IsInsideSelBlock(ptText)) &&				// If Inside Selection Area
            (!m_bDisableDragAndDrop))				// And D&D Not Disabled
        {
            m_bPreparingToDrag = true;
        }
        else
        {
            m_ptCursorPos = client_to_text(point);
            if (!bShift)
                m_ptAnchor = m_ptCursorPos;

            auto selection = bControl ? WordSelection() : text_selection(m_ptAnchor, m_ptCursorPos);

            m_ptCursorPos = selection._end;
            update_caret();
            EnsureVisible(m_ptCursorPos);
            select(selection);

            SetCapture();
            m_nDragSelTimer = SetTimer(RETHINKIFY_TIMER_DRAGSEL, 100, nullptr);
            assert(m_nDragSelTimer != 0);
            m_bWordSelection = bControl;
            m_bLineSelection = false;
            m_bDragSelection = true;
        }
    }
}



void text_view::OnMouseWheel(const CPoint &point, int zDelta)
{
    ScrollToLine(Clamp(m_nTopLine + zDelta, 0, _buffer.size()));
}

void text_view::OnMouseMove(const CPoint &point, UINT nFlags)
{
    if (m_bDragSelection)
    {
        bool bOnMargin = point.x < margin_width();
        text_location ptNewCursorPos = client_to_text(point);
        text_location ptStart, ptEnd;

        if (m_bLineSelection)
        {
            if (bOnMargin)
            {
                if (ptNewCursorPos.y < m_ptAnchor.y ||
                    ptNewCursorPos.y == m_ptAnchor.y && ptNewCursorPos.x < m_ptAnchor.x)
                {
                    ptEnd = m_ptAnchor;
                    if (ptEnd.y == _buffer.size() - 1)
                    {
                        ptEnd.x = _buffer[ptEnd.y].size();
                    }
                    else
                    {
                        ptEnd.y++;
                        ptEnd.x = 0;
                    }
                    ptNewCursorPos.x = 0;
                    m_ptCursorPos = ptNewCursorPos;
                }
                else
                {
                    ptEnd = m_ptAnchor;
                    ptEnd.x = 0;
                    m_ptCursorPos = ptNewCursorPos;
                    if (ptNewCursorPos.y == _buffer.size() - 1)
                    {
                        ptNewCursorPos.x = _buffer[ptNewCursorPos.y].size();
                    }
                    else
                    {
                        ptNewCursorPos.y++;
                        ptNewCursorPos.x = 0;
                    }
                    m_ptCursorPos.x = 0;
                }
                update_caret();
                select(text_selection(ptNewCursorPos, ptEnd));
                return;
            }

            //	Moving to normal selection mode
            ::SetCursor(::LoadCursor(nullptr, MAKEINTRESOURCE(IDC_IBEAM)));
            m_bLineSelection = m_bWordSelection = false;
        }


        if (m_bWordSelection)
        {
            if (ptNewCursorPos.y < m_ptAnchor.y ||
                ptNewCursorPos.y == m_ptAnchor.y && ptNewCursorPos.x < m_ptAnchor.x)
            {
                ptStart = WordToLeft(ptNewCursorPos);
                ptEnd = WordToRight(m_ptAnchor);
            }
            else
            {
                ptStart = WordToLeft(m_ptAnchor);
                ptEnd = WordToRight(ptNewCursorPos);
            }
        }
        else
        {
            ptStart = m_ptAnchor;
            ptEnd = ptNewCursorPos;
        }

        m_ptCursorPos = ptEnd;
        update_caret();
        select(text_selection(ptStart, ptEnd));
    }

    if (m_bPreparingToDrag)
    {
        m_bPreparingToDrag = false;
        HGLOBAL hData = PrepareDragData();
        if (hData != nullptr)
        {
            undo_group ug(_buffer);


            /*COleDataSource ds;
            ds.CacheGlobalData(CF_UNICODETEXT, hData);
            m_bDraggingText = true;
            DROPEFFECT de = ds.DoDragDrop(GetDropEffect());
            if (de != DROPEFFECT_NONE)
            OnDropSource(de);
            m_bDraggingText = false;

            if (_buffer != nullptr)
            _buffer.FlushUndoGroup(this);*/
        }
    }
}

void text_view::OnLButtonUp(const CPoint &point, UINT nFlags)
{
    if (m_bDragSelection)
    {
        text_location ptNewCursorPos = client_to_text(point);

        if (m_bLineSelection)
        {
            text_location ptStart, ptEnd;

            if (ptNewCursorPos.y < m_ptAnchor.y ||
                ptNewCursorPos.y == m_ptAnchor.y && ptNewCursorPos.x < m_ptAnchor.x)
            {
                ptEnd = m_ptAnchor;
                if (ptEnd.y == _buffer.size() - 1)
                {
                    ptEnd.x = _buffer[ptEnd.y].size();
                }
                else
                {
                    ptEnd.y++;
                    ptEnd.x = 0;
                }
                ptNewCursorPos.x = 0;
                m_ptCursorPos = ptNewCursorPos;
            }
            else
            {
                ptEnd = m_ptAnchor;
                ptEnd.x = 0;
                if (ptNewCursorPos.y == _buffer.size() - 1)
                {
                    ptNewCursorPos.x = _buffer[ptNewCursorPos.y].size();
                }
                else
                {
                    ptNewCursorPos.y++;
                    ptNewCursorPos.x = 0;
                }
                m_ptCursorPos = ptNewCursorPos;
            }

            EnsureVisible(m_ptCursorPos);
            update_caret();
            select(text_selection(ptNewCursorPos, ptEnd));
        }
        else
        {
            auto selection = m_bWordSelection ? WordSelection() : text_selection(m_ptAnchor, m_ptCursorPos);
            m_ptCursorPos = selection._end;
            EnsureVisible(m_ptCursorPos);
            update_caret();
            select(selection);
        }

        ReleaseCapture();
        KillTimer(m_nDragSelTimer);
        m_bDragSelection = false;
    }

    if (m_bPreparingToDrag)
    {
        m_bPreparingToDrag = false;
        m_ptCursorPos = client_to_text(point);
        EnsureVisible(m_ptCursorPos);
        update_caret();
        select(text_selection(m_ptCursorPos, m_ptCursorPos));
    }
}

void text_view::OnTimer(UINT nIDEvent)
{
    if (nIDEvent == RETHINKIFY_TIMER_DRAGSEL)
    {
        assert(m_bDragSelection);
        CPoint pt;
        ::GetCursorPos(&pt);
        ScreenToClient(&pt);

        CRect rcClient;
        GetClientRect(&rcClient);

        bool bChanged = false;

        //	Scroll vertically, if necessary
        auto nNewTopLine = m_nTopLine;
        auto line_count = _buffer.size();

        if (pt.y < rcClient.top)
        {
            nNewTopLine--;

            if (pt.y < rcClient.top - font_height())
                nNewTopLine -= 2;
        }
        else if (pt.y >= rcClient.bottom)
        {
            nNewTopLine++;

            if (pt.y >= rcClient.bottom + font_height())
                nNewTopLine += 2;
        }

        nNewTopLine = Clamp(nNewTopLine, 0, line_count - 1);            

        if (m_nTopLine != nNewTopLine)
        {
            ScrollToLine(nNewTopLine);
            bChanged = true;
        }

        //	Scroll horizontally, if necessary
        auto nNewOffsetChar = m_nOffsetChar;
        auto nMaxLineLength = max_line_length();

        if (pt.x < rcClient.left)
        {
            nNewOffsetChar--;
        }
        else if (pt.x >= rcClient.right)
        {
            nNewOffsetChar++;
        }
        
        nNewOffsetChar = Clamp(nNewOffsetChar, 0, nMaxLineLength - 1);

        if (m_nOffsetChar != nNewOffsetChar)
        {
            ScrollToChar(nNewOffsetChar);
            update_caret();
            bChanged = true;
        }

        //	Fix changes
        if (bChanged)
        {
            text_location ptNewCursorPos = client_to_text(pt);

            if (ptNewCursorPos != m_ptCursorPos)
            {
                m_ptCursorPos = ptNewCursorPos;
                update_caret();
            }
            select(text_selection(m_ptAnchor, m_ptCursorPos));
        }
    }
}

void text_view::OnLButtonDblClk(const CPoint &point, UINT nFlags)
{
    if (!m_bDragSelection)
    {
        m_ptAnchor = m_ptCursorPos = client_to_text(point);

        auto selection = WordSelection();

        m_ptCursorPos = selection._end;
        update_caret();
        EnsureVisible(m_ptCursorPos);
        select(selection);

        SetCapture();
        m_nDragSelTimer = SetTimer(RETHINKIFY_TIMER_DRAGSEL, 100, nullptr);
        assert(m_nDragSelTimer != 0);
        m_bWordSelection = true;
        m_bLineSelection = false;
        m_bDragSelection = true;
    }
}


void text_view::OnRButtonDown(const CPoint &point, UINT nFlags)
{
    auto pt = client_to_text(point);

    if (!IsInsideSelBlock(pt))
    {
        m_ptAnchor = m_ptCursorPos = pt;
        select(text_selection(m_ptCursorPos, m_ptCursorPos));
        EnsureVisible(m_ptCursorPos);
        update_caret();
    }
}

void text_view::Copy()
{
    if (_selection._start == _selection._end)
        return;

    PrepareSelBounds();
    PutToClipboard(Combine(Text(m_ptDrawSel)));
}

bool text_view::TextInClipboard()
{
    return IsClipboardFormatAvailable(CF_UNICODETEXT) != 0;
}

bool text_view::PutToClipboard(const std::wstring &text)
{
    // TODO CWaitCursor wc;
    auto success = false;

    if (OpenClipboard())
    {
        EmptyClipboard();

        auto len = text.size() + 1;
        auto hData = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, len * sizeof(wchar_t));

        if (hData != nullptr)
        {
            auto pszData = (wchar_t*) ::GlobalLock(hData);
            wcscpy_s(pszData, len, text.c_str());
            GlobalUnlock(hData);
            success = SetClipboardData(CF_UNICODETEXT, hData) != nullptr;
        }
        CloseClipboard();
    }

    return success;
}

std::wstring text_view::GetFromClipboard() const
{
    std::wstring result;
    auto pThis = const_cast<text_view*>(this);

    if (pThis->OpenClipboard())
    {
        auto hData = GetClipboardData(CF_UNICODETEXT);

        if (hData != nullptr)
        {
            auto pszData = (const wchar_t *) GlobalLock(hData);

            if (pszData != nullptr)
            {
                result = pszData;
                GlobalUnlock(hData);
            }
        }

        CloseClipboard();
    }

    return result;
}

bool text_view::QueryEditable()
{
    return true;
}

void text_view::Paste()
{
    if (QueryEditable())
    {
        auto text = GetFromClipboard();

        if (!text.empty())
        {
            undo_group ug(_buffer);
            auto pos = _buffer.delete_text(ug, GetSelection());
            Locate(_buffer.insert_text(ug, pos, text));
        }
    }
}

void text_view::Cut()
{
    if (QueryEditable() && HasSelection())
    {
        auto sel = GetSelection();
        PutToClipboard(Combine(Text(sel)));

        undo_group ug(_buffer);
        Locate(_buffer.delete_text(ug, sel));
    }
}

void text_view::OnEditDelete()
{
    if (QueryEditable())
    {
        auto sel = GetSelection();

        if (sel.empty())
        {
            if (sel._end.x == _buffer[sel._end.y].size())
            {
                if (sel._end.y == _buffer.size() - 1)
                    return;

                sel._end.y++;
                sel._end.x = 0;
            }
            else
            {
                sel._end.x++;
            }
        }

        undo_group ug(_buffer);
        Locate(_buffer.delete_text(ug, sel));
    }
}

void text_view::OnChar(UINT nChar, UINT nRepCnt, UINT nFlags)
{
    if ((::GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0 ||
        (::GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0)
        return;

    bool bTranslated = false;

    if (nChar == VK_RETURN)
    {
        if (QueryEditable())
        {
            undo_group ug(_buffer);
            auto pos = _buffer.delete_text(ug, GetSelection());
            Locate(_buffer.insert_text(ug, pos, L'\n'));
        }

        return;
    }

    if (nChar > 31)
    {
        if (QueryEditable())
        {
            undo_group ug(_buffer);
            auto pos = _buffer.delete_text(ug, GetSelection());
            Locate(_buffer.insert_text(ug, pos, nChar));
        }
    }
}

void text_view::OnEditDeleteBack()
{
    if (QueryEditable())
    {
        if (HasSelection())
        {
            OnEditDelete();
        }
        else
        {
            undo_group ug(_buffer);
            Locate(_buffer.delete_text(ug, GetCursorPos()));
        }
    }
}

void text_view::OnEditTab()
{
    if (QueryEditable())
    {
        auto sel = GetSelection();

        if (sel._end.y > sel._start.y)
        {
            undo_group ug(_buffer);

            int nStartLine = sel._start.y;
            int nEndLine = sel._end.y;
            sel._start.x = 0;

            if (sel._end.x > 0)
            {
                if (sel._end.y == _buffer.size() - 1)
                {
                    sel._end.x = _buffer[sel._end.y].size();
                }
                else
                {
                    sel._end.x = 0;
                    sel._end.y++;
                }
            }
            else
            {
                nEndLine--;
            }

            select(sel);
            SetCursorPos(sel._end);
            EnsureVisible(sel._end);

            static const TCHAR pszText [] = _T("\t");

            for (int i = nStartLine; i <= nEndLine; i++)
            {
                _buffer.insert_text(ug, text_location(0, i), pszText);
            }

            RecalcHorzScrollBar();
        }
        else
        {
            undo_group ug(_buffer);
            auto pos = _buffer.delete_text(ug, GetSelection());
            Locate(_buffer.insert_text(ug, pos, L'\t'));
        }
    }
}

void text_view::OnEditUntab()
{
    if (QueryEditable())
    {
        auto sel = GetSelection();

        if (sel._end.y > sel._start.y)
        {
            undo_group ug(_buffer);

            int nStartLine = sel._start.y;
            int nEndLine = sel._end.y;
            sel._start.x = 0;
            if (sel._end.x > 0)
            {
                if (sel._end.y == _buffer.size() - 1)
                {
                    sel._end.x = _buffer[sel._end.y].size();
                }
                else
                {
                    sel._end.x = 0;
                    sel._end.y++;
                }
            }
            else
                nEndLine--;
            select(sel);
            SetCursorPos(sel._end);
            EnsureVisible(sel._end);

            for (int i = nStartLine; i <= nEndLine; i++)
            {
                const auto &line = _buffer[i];

                if (!line.empty())
                {
                    int nPos = 0, nOffset = 0;

                    while (nPos < line.size())
                    {
                        if (line[nPos] == _T(' '))
                        {
                            nPos++;
                            if (++nOffset >= tab_size())
                                break;
                        }
                        else
                        {
                            if (line[nPos] == _T('\t'))
                                nPos++;
                            break;
                        }
                    }

                    if (nPos > 0)
                    {
                        _buffer.delete_text(ug, text_selection(0, i, nPos, i));
                    }
                }
            }

            RecalcHorzScrollBar();
        }
        else
        {
            auto ptCursorPos = GetCursorPos();

            if (ptCursorPos.x > 0)
            {
                int tabSize = tab_size();
                int nOffset = CalculateActualOffset(ptCursorPos.y, ptCursorPos.x);
                int nNewOffset = nOffset / tabSize * tabSize;
                if (nOffset == nNewOffset && nNewOffset > 0)
                    nNewOffset -= tabSize;
                assert(nNewOffset >= 0);

                const auto &line = _buffer[ptCursorPos.y];
                int nCurrentOffset = 0;
                int i = 0;

                while (nCurrentOffset < nNewOffset)
                {
                    if (line[i] == _T('\t'))
                    {
                        nCurrentOffset = nCurrentOffset / tabSize * tabSize + tabSize;
                    }
                    else
                    {
                        nCurrentOffset++;
                    }

                    i++;
                }

                assert(nCurrentOffset == nNewOffset);

                ptCursorPos.x = i;
                Locate(ptCursorPos);
            }
        }
    }
}

DROPEFFECT CEditDropTargetImpl::OnDragEnter(CWindow wnd, COleDataObject* pDataObject, DWORD dwKeyState, CPoint point)
{
    /*if (! pDataObject->IsDataAvailable(CF_UNICODETEXT))
    {
    m_pOwner->HideDropIndicator();
    return DROPEFFECT_NONE;
    }
    m_pOwner->ShowDropIndicator(point);
    if (dwKeyState & MK_CONTROL)
    return DROPEFFECT_COPY;*/
    return DROPEFFECT_MOVE;
}

void CEditDropTargetImpl::OnDragLeave(CWindow wnd)
{
    m_pOwner->HideDropIndicator();
}

DROPEFFECT CEditDropTargetImpl::OnDragOver(CWindow wnd, COleDataObject* pDataObject, DWORD dwKeyState, CPoint point)
{
    ///*
    //	if (! pDataObject->IsDataAvailable(CF_UNICODETEXT))
    //	{
    //		m_pOwner->HideDropIndicator();
    //		return DROPEFFECT_NONE;
    //	}
    //*/
    //	//
    //	
    //	//
    //	bool bDataSupported = false;
    //
    //	if ((!m_pOwner) ||									// If No Owner 
    //			(!( m_pOwner->QueryEditable())) ||			// Or Not Editable
    //			(m_pOwner->GetDisableDragAndDrop()))		// Or Drag And Drop Disabled
    //	{
    //		m_pOwner -> HideDropIndicator();					// Hide Drop Caret
    //		return DROPEFFECT_NONE;							    // Return DE_NONE
    //	}
    ////	if ((pDataObject->IsDataAvailable( CF_UNICODETEXT ) ) ||	    // If Text Available
    ////			( pDataObject -> IsDataAvailable( xxx ) ) ||	// Or xxx Available
    ////			( pDataObject -> IsDataAvailable( yyy ) ) )		// Or yyy Available
    //	if (pDataObject->IsDataAvailable(CF_UNICODETEXT))		  	    // If Text Available
    //	{
    //		bDataSupported = true;								// Set Flag
    //	}
    //	if (!bDataSupported)									// If No Supported Formats Available
    //	{
    //		m_pOwner->HideDropIndicator();					    // Hide Drop Caret
    //		return DROPEFFECT_NONE;						   	    // Return DE_NONE
    //	}
    //	m_pOwner->ShowDropIndicator(point);
    //	if (dwKeyState & MK_CONTROL)
    //		return DROPEFFECT_COPY;
    return DROPEFFECT_MOVE;
}

bool CEditDropTargetImpl::OnDrop(CWindow wnd, COleDataObject* pDataObject, DROPEFFECT dropEffect, CPoint point)
{
    //	//
    //	 			( m_pOwner -> GetDisableDragAndDrop() ) )		// Or Drag And Drop Disabled
    //	//
    //	bool bDataSupported = false;
    //
    //	m_pOwner->HideDropIndicator();						// Hide Drop Caret
    //	if ((!m_pOwner) ||									// If No Owner 
    //			(!( m_pOwner->QueryEditable())) ||			// Or Not Editable
    //			(m_pOwner->GetDisableDragAndDrop()))		// Or Drag And Drop Disabled
    //	{
    //		return DROPEFFECT_NONE;							// Return DE_NONE
    //	}
    ////	if( ( pDataObject -> IsDataAvailable( CF_UNICODETEXT ) ) ||	// If Text Available
    ////			( pDataObject -> IsDataAvailable( xxx ) ) ||	// Or xxx Available
    ////			( pDataObject -> IsDataAvailable( yyy ) ) )		// Or yyy Available
    //	if (pDataObject->IsDataAvailable(CF_UNICODETEXT))			    // If Text Available
    //	{
    //		bDataSupported = true;								// Set Flag
    //	}
    //	if (!bDataSupported)									// If No Supported Formats Available
    //	{
    //		return DROPEFFECT_NONE;							    // Return DE_NONE
    //	}
    //	return (m_pOwner->DoDropText(pDataObject, point));	    // Return Result Of Drop
    return 0;
}

DROPEFFECT CEditDropTargetImpl::OnDragScroll(CWindow wnd, DWORD dwKeyState, CPoint point)
{
    assert(m_pOwner->m_hWnd == wnd.m_hWnd);

    m_pOwner->DoDragScroll(point);

    if (dwKeyState & MK_CONTROL)
        return DROPEFFECT_COPY;
    return DROPEFFECT_MOVE;
}

void text_view::DoDragScroll(const CPoint &point)
{
    CRect rcClientRect;
    GetClientRect(rcClientRect);
    if (point.y < rcClientRect.top + DRAG_BORDER_Y)
    {
        HideDropIndicator();
        ScrollUp();
        UpdateWindow();
        ShowDropIndicator(point);
        return;
    }
    if (point.y >= rcClientRect.bottom - DRAG_BORDER_Y)
    {
        HideDropIndicator();
        ScrollDown();
        UpdateWindow();
        ShowDropIndicator(point);
        return;
    }
    if (point.x < rcClientRect.left + margin_width() + DRAG_BORDER_X)
    {
        HideDropIndicator();
        ScrollLeft();
        UpdateWindow();
        ShowDropIndicator(point);
        return;
    }
    if (point.x >= rcClientRect.right - DRAG_BORDER_X)
    {
        HideDropIndicator();
        ScrollRight();
        UpdateWindow();
        ShowDropIndicator(point);
        return;
    }
}

bool text_view::DoDropText(COleDataObject *pDataObject, const CPoint &ptClient)
{
    //HGLOBAL hData = pDataObject->GetGlobalData(CF_UNICODETEXT);
    //if (hData == nullptr)
    //	return false;

    //text_location ptDropPos = client_to_text(ptClient);
    //if (IsDraggingText() && IsInsideSelection(ptDropPos))
    //{
    //	SetAnchor(ptDropPos);
    //	select(ptDropPos, ptDropPos);
    //	SetCursorPos(ptDropPos);
    //	EnsureVisible(ptDropPos);
    //	return false;
    //}

    //auto pszText = (const char *) ::GlobalLock(hData);
    //if (pszText == nullptr)
    //	return false;

    //int x, y;
    //_buffer.insert_text(this, ptDropPos.y, ptDropPos.x, A2T(pszText), y, x, CE_ACTION_DRAGDROP);
    //text_location ptCurPos(x, y);
    //SetAnchor(ptDropPos);
    //select(ptDropPos, ptCurPos);
    //SetCursorPos(ptCurPos);
    //EnsureVisible(ptCurPos);

    //::GlobalUnlock(hData);
    return true;
}


void text_view::ShowDropIndicator(const CPoint &point)
{
    if (!m_bDropPosVisible)
    {
        HideCursor();
        m_ptSavedCaretPos = GetCursorPos();
        m_bDropPosVisible = true;
        ::CreateCaret(m_hWnd, (HBITMAP) 1, 2, font_height());
    }
    m_ptDropPos = client_to_text(point);
    if (m_ptDropPos.x >= m_nOffsetChar)
    {
        auto pt = text_to_client(m_ptDropPos);
        SetCaretPos(pt.x, pt.y);
        ShowCaret();
    }
    else
    {
        HideCaret();
    }
}

void text_view::HideDropIndicator()
{
    if (m_bDropPosVisible)
    {
        SetCursorPos(m_ptSavedCaretPos);
        ShowCursor();
        m_bDropPosVisible = false;
    }
}

DROPEFFECT text_view::GetDropEffect()
{
    return DROPEFFECT_COPY | DROPEFFECT_MOVE;
}

void text_view::OnDropSource(DROPEFFECT de)
{
    if (m_bDraggingText && de == DROPEFFECT_MOVE)
    {
        undo_group ug(_buffer);
        _buffer.delete_text(ug, m_ptDraggedText);
    }
}


void text_view::OnEditReplace()
{
    //if (! QueryEditable())
    //	return;

    //CWinApp *pApp = AfxGetApp();
    //assert(pApp != nullptr);

    //CEditReplaceDlg dlg(this);

    ////	Take search parameters from registry
    //dlg.m_bMatchCase = pApp->GetProfileInt(REG_REPLACE_SUBKEY, REG_MATCH_CASE, false);
    //dlg.m_bWholeWord = pApp->GetProfileInt(REG_REPLACE_SUBKEY, REG_WHOLE_WORD, false);
    //dlg.m_sText = pApp->GetProfileString(REG_REPLACE_SUBKEY, REG_FIND_WHAT, _T(""));
    //dlg.m_sNewText = pApp->GetProfileString(REG_REPLACE_SUBKEY, REG_REPLACE_WITH, _T(""));

    //if (HasSelection())
    //{
    //	GetSelection(m_ptSavedSelStart, m_ptSavedSelEnd);
    //	m_bSelectionPushed = true;

    //	dlg.m_nScope = 0;	//	Replace in current selection
    //	dlg.m_ptCurrentPos = m_ptSavedSelStart;
    //	dlg.m_bEnableScopeSelection = true;
    //	dlg.m_ptBlockBegin = m_ptSavedSelStart;
    //	dlg.m_ptBlockEnd = m_ptSavedSelEnd;
    //}
    //else
    //{
    //	dlg.m_nScope = 1;	//	Replace in whole text
    //	dlg.m_ptCurrentPos = GetCursorPos();
    //	dlg.m_bEnableScopeSelection = false;
    //}

    ////	Execute Replace dialog
    //m_bShowInactiveSelection = true;
    //dlg.DoModal();
    //m_bShowInactiveSelection = false;

    ////	Restore selection
    //if (m_bSelectionPushed)
    //{
    //	select(m_ptSavedSelStart, m_ptSavedSelEnd);
    //	m_bSelectionPushed = false;
    //}

    ////	Save search parameters to registry
    //pApp->WriteProfileInt(REG_REPLACE_SUBKEY, REG_MATCH_CASE, dlg.m_bMatchCase);
    //pApp->WriteProfileInt(REG_REPLACE_SUBKEY, REG_WHOLE_WORD, dlg.m_bWholeWord);
    //pApp->WriteProfileString(REG_REPLACE_SUBKEY, REG_FIND_WHAT, dlg.m_sText);
    //pApp->WriteProfileString(REG_REPLACE_SUBKEY, REG_REPLACE_WITH, dlg.m_sNewText);
}

bool text_view::ReplaceSelection(const wchar_t * pszNewText)
{
    //assert(pszNewText != nullptr);
    //if (! HasSelection())
    //	return false;

    //DeleteCurrentSelection();

    //text_location ptCursorPos = GetCursorPos();
    //int x, y;
    //_buffer.insert_text(this, ptCursorPos.y, ptCursorPos.x, pszNewText, y, x, CE_ACTION_REPLACE);
    //text_location ptEndOfBlock = text_location(x, y);
    //SetAnchor(ptEndOfBlock);
    //select(ptCursorPos, ptEndOfBlock);
    //SetCursorPos(ptEndOfBlock);
    //EnsureVisible(ptEndOfBlock);
    return true;
}



void text_view::OnEditUndo()
{
    if (_buffer.can_undo())
    {
        Locate(_buffer.undo());
    }
}


void text_view::OnEditRedo()
{
    if (_buffer.can_redo())
    {
        Locate(_buffer.redo());
    }
}



//void text_view::OnEditOperation(int nAction, const std::wstring &text)
//{
//	if (m_bAutoIndent)
//	{
//		//	Analyse last action...
//		if (nAction == CE_ACTION_TYPING && _tcscmp(text.c_str(), _T("\r\n")) == 0 && !_overtype)
//		{
//			//	Enter stroke!
//			text_location ptCursorPos = GetCursorPos();
//			assert(ptCursorPos.y > 0);
//
//			//	Take indentation from the previos line
//			const auto &line = _buffer.GetLineChars(ptCursorPos.y - 1);
//			const auto len = line.size();
//
//			int nPos = 0;
//			while (nPos < len && iswspace(line[nPos]))
//				nPos++;
//
//			if (nPos > 0)
//			{
//				//	insert part of the previos line
//				TCHAR *pszInsertStr = (TCHAR *) _alloca(sizeof(TCHAR) * (len + 1));
//				_tcsncpy_s(pszInsertStr, (len + 1), line.c_str(), nPos);
//				pszInsertStr[nPos] = 0;
//
//				int x, y;
//				_buffer.insert_text(ug, ptCursorPos.y, ptCursorPos.x, pszInsertStr, y, x, CE_ACTION_AUTOINDENT);
//
//				text_location pt(x, y);
//				SetCursorPos(pt);
//				select(pt, pt);
//				SetAnchor(pt);
//				EnsureVisible(pt);
//			}
//		}
//	}
//}

bool text_view::GetOverwriteMode() const
{
    return _overtype;
}

void text_view::SetOverwriteMode(bool bOvrMode /*= true*/)
{
    _overtype = bOvrMode;
}

bool text_view::GetAutoIndent() const
{
    return m_bAutoIndent;
}

void text_view::SetAutoIndent(bool bAutoIndent)
{
    m_bAutoIndent = bAutoIndent;
}

void text_view::EnableMenuItems(HMENU hMenu)
{
    auto count = GetMenuItemCount(hMenu);

    for (auto i = 0; i < count; i++)
    {
        auto id = GetMenuItemID(hMenu, i);
        auto enable = true;

        switch (id)
        {
        case ID_EDIT_COPY: enable = HasSelection(); break;
        case ID_EDIT_CUT: enable = HasSelection(); break;
        case ID_EDIT_FIND_PREVIOUS: enable = m_bLastSearch; break;
        case ID_EDIT_PASTE: enable = TextInClipboard(); break;
        case ID_EDIT_REDO: enable = _buffer.can_redo(); break;
        case ID_EDIT_REPEAT: enable = m_bLastSearch; break;
        case ID_EDIT_SELECT_ALL: enable = true; break;
        case ID_EDIT_UNDO: enable = _buffer.can_undo(); break;
        }

        EnableMenuItem(hMenu, i, MF_BYPOSITION | (enable ? MF_ENABLED : MF_DISABLED));
    }
}

static bool IsCppExtension(const wchar_t *ext)
{
    static auto comp = [](const wchar_t *l, const wchar_t *r) { return _wcsicmp(l, r) < 0; };
    static std::set<const wchar_t*, std::function<bool(const wchar_t *, const wchar_t *)>> extensions(comp);

    if (extensions.empty())
    {
        extensions.insert(L"c");
        extensions.insert(L"cpp");
        extensions.insert(L"cxx");
        extensions.insert(L"cc");
        extensions.insert(L"h");
        extensions.insert(L"hh");
        extensions.insert(L"hpp");
        extensions.insert(L"hxx");
        extensions.insert(L"inl");
    }

    return extensions.find(ext) != extensions.end();
};

void text_view::HighlightFromExtension(const wchar_t *ext)
{
    if (*ext == L'.') ext++;

    if (IsCppExtension(ext))
    {
        static auto highlighter = std::make_shared<CppSyntax>();
        _highlight = highlighter;
    }
    else
    {
        _highlight = s_textHighlighter;
    }
}