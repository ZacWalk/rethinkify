
#include "pch.h"
#include "document.h"
#include "utf.h"
#include "resource.h"
#include "ui.h"

#include <fstream>
#include <clocale>
#include <codecvt>

const TCHAR crlf [] = _T("\r\n");
const int UNDO_BUF_SIZE = 1000;

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

static auto s_textHighlighter = std::make_shared<TextHighight>();


document::document(IView &view, const std::wstring &text, int nCrlfStyle) : _highlight(s_textHighlighter), _view(view), _font_extent(1, 1)
{
    memset(m_apFonts, 0, sizeof(HFONT) * 4);

    m_bSelMargin = true;
    _modified = false;
    m_bCreateBackupFile = false;
    m_nUndoPosition = 0;
    m_nCRLFMode = nCrlfStyle;

    if (text.empty())
    {
        append_line(L"");
    }
    else
    {
        std::wstringstream lines(text);
        std::wstring line;

        while (std::getline(lines, line))
        {
            append_line(line);
        }
    }
}

document::~document()
{
}


const text_selection &document::selection() const
{
    PrepareSelBounds();
    return m_ptDrawSel;
}

int document::expanded_line_length(int lineIndex) const
{
    const auto &line = _lines[lineIndex];

    if (line._expanded_length == -1)
    {
        auto nActualLength = 0;

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

        line._expanded_length = nActualLength;
    }

    return line._expanded_length;
}

std::wstring document::ExpandChars(const std::wstring &text, int nOffset, int nCount) const
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

void document::draw_line(HDC pdc, text_location &ptOrigin, const CRect &rcClip, const wchar_t * pszChars, int nOffset, int nCount) const
{
    if (nCount > 0)
    {
        auto line = ExpandChars(pszChars, nOffset, nCount);
        auto nWidth = rcClip.right - ptOrigin.x;

        if (nWidth > 0)
        {
            auto nCharWidth = _font_extent.cx;
            auto nCount = line.size();
            auto nCountFit = nWidth / nCharWidth + 1;

            if (nCount > nCountFit)
                nCount = nCountFit;

            /*
            CRect rcBounds = rcClip;
            rcBounds.left = ptOrigin.x;
            rcBounds.right = rcBounds.left + _font_extent.cx * nCount;
            pdc->ExtTextOut(rcBounds.left, rcBounds.top, ETO_OPAQUE, &rcBounds, nullptr, 0, nullptr);
            */
            ::ExtTextOut(pdc, ptOrigin.x, ptOrigin.y, ETO_CLIPPED, &rcClip, line.c_str(), nCount, nullptr);
        }
        ptOrigin.x += _font_extent.cx * line.size();
    }
}

void document::draw_line(HDC pdc, text_location &ptOrigin, const CRect &rcClip, int nColorIndex, const wchar_t * pszChars, int nOffset, int nCount, text_location ptTextPos) const
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
                draw_line(pdc, ptOrigin, rcClip, pszChars, nOffset, nSelBegin);
            }
            if (nSelBegin < nSelEnd)
            {
                COLORREF crOldBk = SetBkColor(pdc, GetColor(IHighlight::COLORINDEX_SELBKGND));
                COLORREF crOldText = SetTextColor(pdc, GetColor(IHighlight::COLORINDEX_SELTEXT));
                draw_line(pdc, ptOrigin, rcClip, pszChars, nOffset + nSelBegin, nSelEnd - nSelBegin);
                SetBkColor(pdc, crOldBk);
                SetTextColor(pdc, crOldText);
            }
            if (nSelEnd < nCount)
            {
                draw_line(pdc, ptOrigin, rcClip, pszChars, nOffset + nSelEnd, nCount - nSelEnd);
            }
        }
        else
        {
            draw_line(pdc, ptOrigin, rcClip, pszChars, nOffset, nCount);
        }
    }
}

void document::line_color(int lineIndex, COLORREF &crBkgnd, COLORREF &crText, bool &bDrawWhitespace) const
{
    bDrawWhitespace = true;
    crText = RGB(255, 255, 255);
    crBkgnd = CLR_NONE;
    crText = CLR_NONE;
    bDrawWhitespace = false;
}

DWORD document::prse_cookie(int lineIndex) const
{
    int line_count = _lines.size();

    if (lineIndex < 0 || lineIndex >= line_count)
        return 0;

    int i = lineIndex;
    while (i >= 0 && _lines[i]._parseCookie == invalid)
        i--;
    i++;

    int nBlocks;
    while (i <= lineIndex && _lines[i]._parseCookie == invalid)
    {
        auto dwCookie = 0;

        if (i > 0)
            dwCookie = _lines[i - 1]._parseCookie;

        const auto &line = _lines[i];
        line._parseCookie = _highlight->ParseLine(dwCookie, line, nullptr, nBlocks);

        assert(line._parseCookie != invalid);
        i++;
    }

    return _lines[lineIndex]._parseCookie;
}



void document::draw_line(HDC pdc, const CRect &rc, int lineIndex) const
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
        line_color(lineIndex, crBkgnd, crText, bDrawWhitespace);

        if (crBkgnd == CLR_NONE)
            crBkgnd = GetColor(IHighlight::COLORINDEX_BKGND);

        const auto &line = _lines[lineIndex];

        if (line.empty())
        {
            //	Draw the empty line
            CRect rect = rc;
            if ((m_bFocused || m_bShowInactiveSelection) && IsInsideSelBlock(text_location(0, lineIndex)))
            {
                FillSolidRect(pdc, rect.left, rect.top, _font_extent.cx, rect.Height(), GetColor(IHighlight::COLORINDEX_SELBKGND));
                rect.left += _font_extent.cx;
            }
            FillSolidRect(pdc, rect, bDrawWhitespace ? crBkgnd : GetColor(IHighlight::COLORINDEX_WHITESPACE));
            return;
        }

        //	Parse the line
        auto nLength = line.size();
        auto pBuf = (IHighlight::TEXTBLOCK *) _malloca(sizeof(IHighlight::TEXTBLOCK) * nLength * 3);
        auto nBlocks = 0;
        auto cookie = prse_cookie(lineIndex - 1);

        line._parseCookie = _highlight->ParseLine(cookie, line, pBuf, nBlocks);

        //	Draw the line text
        text_location origin(rc.left - _char_offset.cx * _font_extent.cx, rc.top);
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
            draw_line(pdc, origin, rc, IHighlight::COLORINDEX_NORMALTEXT, pszChars, 0, pBuf[0].m_nCharPos, text_location(0, lineIndex));
            for (int i = 0; i < nBlocks - 1; i++)
            {
                assert(pBuf[i].m_nCharPos >= 0 && pBuf[i].m_nCharPos <= nLength);
                if (crText == CLR_NONE)
                    SetTextColor(pdc, GetColor(pBuf[i].m_nColorIndex));
                SelectObject(pdc, GetFont(GetItalic(pBuf[i].m_nColorIndex), GetBold(pBuf[i].m_nColorIndex)));
                draw_line(pdc, origin, rc, pBuf[i].m_nColorIndex, pszChars,
                    pBuf[i].m_nCharPos, pBuf[i + 1].m_nCharPos - pBuf[i].m_nCharPos,
                    text_location(pBuf[i].m_nCharPos, lineIndex));
            }
            assert(pBuf[nBlocks - 1].m_nCharPos >= 0 && pBuf[nBlocks - 1].m_nCharPos <= nLength);
            if (crText == CLR_NONE)
                SetTextColor(pdc, GetColor(pBuf[nBlocks - 1].m_nColorIndex));
            SelectObject(pdc, GetFont(GetItalic(pBuf[nBlocks - 1].m_nColorIndex),
                GetBold(pBuf[nBlocks - 1].m_nColorIndex)));
            draw_line(pdc, origin, rc, pBuf[nBlocks - 1].m_nColorIndex, pszChars,
                pBuf[nBlocks - 1].m_nCharPos, nLength - pBuf[nBlocks - 1].m_nCharPos,
                text_location(pBuf[nBlocks - 1].m_nCharPos, lineIndex));
        }
        else
        {
            if (crText == CLR_NONE)
                SetTextColor(pdc, GetColor(IHighlight::COLORINDEX_NORMALTEXT));
            SelectObject(pdc, GetFont(GetItalic(IHighlight::COLORINDEX_NORMALTEXT), GetBold(IHighlight::COLORINDEX_NORMALTEXT)));
            draw_line(pdc, origin, rc, IHighlight::COLORINDEX_NORMALTEXT, pszChars, 0, nLength, text_location(0, lineIndex));
        }

        //	Draw whitespaces to the left of the text
        auto frect = rc;
        if (origin.x > frect.left)
            frect.left = origin.x;
        if (frect.right > frect.left)
        {
            if ((m_bFocused || m_bShowInactiveSelection) && IsInsideSelBlock(text_location(nLength, lineIndex)))
            {
                FillSolidRect(pdc, frect.left, frect.top, _font_extent.cx, frect.Height(), GetColor(IHighlight::COLORINDEX_SELBKGND));
                frect.left += _font_extent.cx;
            }
            if (frect.right > frect.left)
                FillSolidRect(pdc, frect, bDrawWhitespace ? crBkgnd : GetColor(IHighlight::COLORINDEX_WHITESPACE));
        }

        _freea(pBuf);
    }
}

COLORREF document::GetColor(int nColorIndex) const
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

//COLORREF document::GetColor(int nColorIndex)
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


    void document::draw_margin(HDC pdc, const CRect &rect, int lineIndex) const
    {
        FillSolidRect(pdc, rect, GetColor(m_bSelMargin ? IHighlight::COLORINDEX_SELMARGIN : IHighlight::COLORINDEX_BKGND));
    }

bool document::IsInsideSelBlock(text_location ptTextPos) const
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

bool document::IsInsideSelection(const text_location &ptTextPos) const
{
    PrepareSelBounds();
    return IsInsideSelBlock(ptTextPos);
}

void document::PrepareSelBounds() const
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

void document::draw(HDC dc)
{
    PrepareSelBounds();

    auto rcClient = client_rect();
    auto line_count = _lines.size();
    auto y = 0;
    auto nCurrentLine = _char_offset.cy;

    while (y < rcClient.bottom)
    {
        auto nLineHeight = line_height(nCurrentLine);
        auto rcLine = rcClient;
        rcLine.bottom = rcLine.top + nLineHeight;

        CRect rcCacheMargin(0, y, margin_width(), y + nLineHeight);
        CRect rcCacheLine(margin_width(), y, rcLine.Width(), y + nLineHeight);

        if (nCurrentLine < line_count)
        {
            draw_margin(dc, rcCacheMargin, nCurrentLine);
            draw_line(dc, rcCacheLine, nCurrentLine);
        }
        else
        {
            draw_margin(dc, rcCacheMargin, -1);
            draw_line(dc, rcCacheLine, -1);
        }

        nCurrentLine++;
        y += nLineHeight;
    }
}

void document::reset()
{
    m_bAutoIndent = true;
    _char_offset.cy = 0;
    _char_offset.cx = 0;
    m_tabSize = 4;
    m_nMaxLineLength = -1;
    m_nScreenLines = -1;
    m_nScreenChars = -1;
    m_nIdealCharPos = -1;
    m_ptAnchor.x = 0;
    m_ptAnchor.y = 0;

    for (auto &line : _lines)
    {
        line._parseCookie = invalid;
        line._expanded_length = invalid;
    }

    m_ptCursorPos.x = 0;
    m_ptCursorPos.y = 0;
    _selection._start = _selection._end = m_ptCursorPos;
    m_bLastSearch = false;
    m_bShowInactiveSelection = false;
    m_bMultipleSearch = false;
}



int document::tab_size() const
{
    assert(m_tabSize >= 0 && m_tabSize <= 64);
    return m_tabSize;
}

void document::tab_size(int tabSize)
{
    assert(tabSize >= 0 && tabSize <= 64);
    if (m_tabSize != tabSize)
    {
        m_tabSize = tabSize;

        for (auto &line : _lines)
        {
            line._expanded_length = invalid;
        }

        m_nMaxLineLength = -1;
        _view.RecalcHorzScrollBar();
        _view.invalidate();
        _view.update_caret();
    }
}

HFONT document::GetFont(bool bItalic /*= false*/, bool bBold /*= false*/) const
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

int document::line_offset(int lineIndex) const
{
    auto max = _lines.size();
    auto line = Clamp(lineIndex, 0, max - 1);
    return _lines[line]._y;
}

int document::line_height(int lineIndex) const
{
    return _font_extent.cy;
}

int document::max_line_length() const
{
    if (m_nMaxLineLength == -1)
    {
        m_nMaxLineLength = 0;
        auto line_count = _lines.size();

        for (int i = 0; i < line_count; i++)
        {
            int nActualLength = expanded_line_length(i);

            if (m_nMaxLineLength < nActualLength)
                m_nMaxLineLength = nActualLength;
        }
    }

    return m_nMaxLineLength;
}

int document::screen_lines() const
{
    return m_nScreenLines;
}

bool document::GetItalic(int nColorIndex) const
{
    return false;
}

bool document::GetBold(int nColorIndex) const
{
    return false;
}

int document::screen_chars() const
{
    if (m_nScreenChars == -1)
    {
        auto rect = client_rect();
        m_nScreenChars = (rect.Width() - margin_width()) / _font_extent.cx;
    }
    return m_nScreenChars;
}

void document::OnDestroy()
{
    for (int i = 0; i < 4; i++)
    {
        if (m_apFonts[i] != nullptr)
        {
            DeleteObject(m_apFonts[i]);
            m_apFonts[i] = nullptr;
        }
    }
}

void document::layout(const CSize &extent, const CSize &font_extent)
{
    _extent = extent;
    _font_extent = font_extent;

    layout();    
}

void document::layout()
{
    auto rect = client_rect();
    auto line_count = _lines.size();
    auto y = 0;
    auto cy = _font_extent.cy;

    for (int i = 0; i < line_count; i++)
    {
        auto &line = _lines[i];

        line._y = y;
        line._cy = cy;

        y += cy;
    }

    m_nScreenLines = rect.Height() / _font_extent.cy;
 
    _view.RecalcVertScrollBar();
    _view.RecalcHorzScrollBar();
}


int document::client_to_line(const CPoint &point) const
{
    auto line_count = _lines.size();
    auto y = _char_offset.cy;

    while (y < line_count)
    {
        auto const &line = _lines[y];

        if (point.y >= line._y && point.y < (line._y + line._cy))
        {
            return y;
        }

        y += 1;
    }

    return -1;
}

text_location document::client_to_text(const CPoint &point) const
{
    auto line_count = _lines.size();

    text_location pt;
    pt.y = client_to_line(point);

    if (pt.y >= 0 && pt.y < line_count)
    {
        const auto &line = _lines[pt.y];

        int nPos = _char_offset.cx + (point.x - margin_width()) / _font_extent.cx;
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

int document::top_offset() const
{
    auto result = 0;

    if (!_lines.empty() || _char_offset.cy <= 0)
    {
        result = _lines[_char_offset.cy]._y;
    }

    return result;
}

CPoint document::text_to_client(const text_location &point) const
{
    CPoint pt;

    if (point.y >= 0 && point.y < _lines.size())
    {
        pt.y = line_offset(point.y) - top_offset();
        pt.x = 0;

        auto tabSize = tab_size();
        const auto &line = _lines[point.y];

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

        pt.x = (pt.x - _char_offset.cx) * _font_extent.cx + margin_width();
    }

    return pt;
}

void document::invalidate_lines(int nLine1, int nLine2, bool bInvalidateMargin /*= false*/)
{
    bInvalidateMargin = true;

    auto rcInvalid = client_rect();

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

    _view.invalidate(rcInvalid);
}

int document::CalculateActualOffset(int lineIndex, int nCharIndex)
{
    int nOffset = 0;

    if (lineIndex >= 0 && lineIndex < _lines.size())
    {
        const auto &line = _lines[lineIndex];
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

int document::ApproxActualOffset(int lineIndex, int nOffset)
{
    if (nOffset == 0)
        return 0;

    const auto &line = _lines[lineIndex];
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

void document::EnsureVisible(text_location pt)
{
    //	Scroll vertically
    int line_count = _lines.size();
    int nNewTopLine = _char_offset.cy;

    if (pt.y >= nNewTopLine + screen_lines())
    {
        nNewTopLine = pt.y - screen_lines() + 1;
    }
    else if (pt.y < nNewTopLine)
    {
        nNewTopLine = pt.y;
    }

    if (nNewTopLine < 0)
        nNewTopLine = 0;
    if (nNewTopLine >= line_count)
        nNewTopLine = line_count - 1;

    if (_char_offset.cy != nNewTopLine)
    {
        _view.ScrollToLine(nNewTopLine);
    }

    //	Scroll horizontally
    int nActualPos = CalculateActualOffset(pt.y, pt.x);
    int nNewOffset = _char_offset.cx;
    if (nActualPos > nNewOffset + screen_chars())
    {
        nNewOffset = nActualPos - screen_chars();
    }
    if (nActualPos < nNewOffset)
    {
        nNewOffset = nActualPos;
    }

    if (nNewOffset >= max_line_length())
        nNewOffset = max_line_length() - 1;
    if (nNewOffset < 0)
        nNewOffset = 0;

    if (_char_offset.cx != nNewOffset)
    {
        _view.ScrollToChar(nNewOffset);
        _view.update_caret();
    }
}


void document::invalidate_line(int index)
{
    auto &line = _lines[index];
    line._expanded_length = -1;
    line._parseCookie = -1;

    invalidate_lines(index, index + 1, true);

    int nActualLength = expanded_line_length(index);

    if (m_nMaxLineLength < nActualLength)
        m_nMaxLineLength = nActualLength;

    _view.RecalcHorzScrollBar();
}

void document::invalidate_view()
{
    reset();
    layout();
    
    _view.update_caret();
    _view.RecalcVertScrollBar();
    _view.RecalcHorzScrollBar();
    _view.invalidate();
}

HINSTANCE document::GetResourceHandle()
{
    return _AtlBaseModule.GetResourceInstance();
}

int document::OnCreate()
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

    return 0;
}

void document::SetAnchor(const text_location &ptNewAnchor)
{
    m_ptAnchor = ptNewAnchor;
}

void document::SetCursorPos(const text_location &ptCursorPos)
{
    m_ptCursorPos = ptCursorPos;
    m_nIdealCharPos = CalculateActualOffset(m_ptCursorPos.y, m_ptCursorPos.x);
    _view.update_caret();
}

void document::selection_margin(bool bSelMargin)
{
    if (m_bSelMargin != bSelMargin)
    {
        m_bSelMargin = bSelMargin;

        m_nScreenChars = -1;
        _view.invalidate();
        _view.RecalcHorzScrollBar();
    }
}

void document::ShowCursor()
{
    m_bCursorHidden = false;
    _view.update_caret();
}

void document::HideCursor()
{
    m_bCursorHidden = true;
    _view.update_caret();
}

HGLOBAL document::PrepareDragData()
{
    PrepareSelBounds();
    if (m_ptDrawSel._start == m_ptDrawSel._end)
        return nullptr;

    auto text = Combine(this->text(m_ptDrawSel));
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



bool document::HighlightText(const text_location &ptStartPos, int nLength)
{
    m_ptCursorPos = ptStartPos;
    m_ptCursorPos.x += nLength;
    m_ptAnchor = m_ptCursorPos;
    select(text_selection(ptStartPos, m_ptCursorPos));
    _view.update_caret();
    EnsureVisible(m_ptCursorPos);
    return true;
}


void document::Find(const std::wstring &text, DWORD flags)
{
    text_location loc;

    if (Find(text, m_ptCursorPos, flags, true, &loc))
    {
        auto end = loc;
        end.x += text.size();
        select(text_selection(loc, end));
        m_ptCursorPos = loc;
        _view.update_caret();

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
    //		selection(ptSelStart, ptSelEnd);		if (ptSelStart.y == ptSelEnd.y)
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

void document::OnEditRepeat()
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



void document::OnEditFindPrevious()
{
    DWORD dwSaveSearchFlags = m_dwLastSearchFlags;
    if ((m_dwLastSearchFlags & FIND_DIRECTION_UP) != 0)
        m_dwLastSearchFlags &= ~FIND_DIRECTION_UP;
    else
        m_dwLastSearchFlags |= FIND_DIRECTION_UP;
    OnEditRepeat();
    m_dwLastSearchFlags = dwSaveSearchFlags;
}

bool document::view_tabs() const
{
    return m_bViewTabs;
}

void document::view_tabs(bool bViewTabs)
{
    if (bViewTabs != m_bViewTabs)
    {
        m_bViewTabs = bViewTabs;
        _view.invalidate();
    }
}

int document::margin_width() const
{
    return m_bSelMargin ? 20 : 1;
}

bool document::GetDisableDragAndDrop() const
{
    return m_bDisableDragAndDrop;
}

void document::SetDisableDragAndDrop(bool bDDAD)
{
    m_bDisableDragAndDrop = bDDAD;
}

void document::MoveLeft(bool selecting)
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
                m_ptCursorPos.x = _lines[m_ptCursorPos.y].size();
            }
        }
        else
            m_ptCursorPos.x--;
    }
    m_nIdealCharPos = CalculateActualOffset(m_ptCursorPos.y, m_ptCursorPos.x);
    EnsureVisible(m_ptCursorPos);
    _view.update_caret();
    if (!selecting)
        m_ptAnchor = m_ptCursorPos;
    select(text_selection(m_ptAnchor, m_ptCursorPos));
}

void document::MoveRight(bool selecting)
{
    PrepareSelBounds();
    if (m_ptDrawSel._start != m_ptDrawSel._end && !selecting)
    {
        m_ptCursorPos = m_ptDrawSel._end;
    }
    else
    {
        if (m_ptCursorPos.x == _lines[m_ptCursorPos.y].size())
        {
            if (m_ptCursorPos.y < _lines.size() - 1)
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
    _view.update_caret();
    if (!selecting)
        m_ptAnchor = m_ptCursorPos;
    select(text_selection(m_ptAnchor, m_ptCursorPos));
}

void document::MoveWordLeft(bool selecting)
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
        m_ptCursorPos.x = _lines[m_ptCursorPos.y].size();
    }

    const auto &line = _lines[m_ptCursorPos.y];
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
    _view.update_caret();
    if (!selecting)
        m_ptAnchor = m_ptCursorPos;
    select(text_selection(m_ptAnchor, m_ptCursorPos));
}

void document::MoveWordRight(bool selecting)
{
    PrepareSelBounds();
    if (m_ptDrawSel._start != m_ptDrawSel._end && !selecting)
    {
        MoveRight(selecting);
        return;
    }

    if (m_ptCursorPos.x == _lines[m_ptCursorPos.y].size())
    {
        if (m_ptCursorPos.y == _lines.size() - 1)
            return;
        m_ptCursorPos.y++;
        m_ptCursorPos.x = 0;
    }

    auto nLength = _lines[m_ptCursorPos.y].size();

    if (m_ptCursorPos.x == nLength)
    {
        MoveRight(selecting);
        return;
    }

    const auto &line = _lines[m_ptCursorPos.y];
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
    _view.update_caret();
    if (!selecting)
        m_ptAnchor = m_ptCursorPos;
    select(text_selection(m_ptAnchor, m_ptCursorPos));
}

void document::MoveUp(bool selecting)
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

        auto size = _lines[m_ptCursorPos.y].size();

        if (m_ptCursorPos.x > size)
            m_ptCursorPos.x = size;
    }
    EnsureVisible(m_ptCursorPos);
    _view.update_caret();
    if (!selecting)
        m_ptAnchor = m_ptCursorPos;

    select(text_selection(m_ptAnchor, m_ptCursorPos));
}

void document::MoveDown(bool selecting)
{
    PrepareSelBounds();
    if (m_ptDrawSel._start != m_ptDrawSel._end && !selecting)
        m_ptCursorPos = m_ptDrawSel._end;

    if (m_ptCursorPos.y < _lines.size() - 1)
    {
        if (m_nIdealCharPos == -1)
            m_nIdealCharPos = CalculateActualOffset(m_ptCursorPos.y, m_ptCursorPos.x);

        m_ptCursorPos.y++;
        m_ptCursorPos.x = ApproxActualOffset(m_ptCursorPos.y, m_nIdealCharPos);

        auto size = _lines[m_ptCursorPos.y].size();

        if (m_ptCursorPos.x > size)
            m_ptCursorPos.x = size;
    }
    EnsureVisible(m_ptCursorPos);
    _view.update_caret();
    if (!selecting)
        m_ptAnchor = m_ptCursorPos;
    select(text_selection(m_ptAnchor, m_ptCursorPos));
}

void document::MoveHome(bool selecting)
{
    const auto &line = _lines[m_ptCursorPos.y];

    int nHomePos = 0;
    while (nHomePos < line.size() && iswspace(line[nHomePos]))
        nHomePos++;
    if (nHomePos == line.size() || m_ptCursorPos.x == nHomePos)
        m_ptCursorPos.x = 0;
    else
        m_ptCursorPos.x = nHomePos;
    m_nIdealCharPos = CalculateActualOffset(m_ptCursorPos.y, m_ptCursorPos.x);
    EnsureVisible(m_ptCursorPos);
    _view.update_caret();
    if (!selecting)
        m_ptAnchor = m_ptCursorPos;
    select(text_selection(m_ptAnchor, m_ptCursorPos));
}

void document::MoveEnd(bool selecting)
{
    m_ptCursorPos.x = _lines[m_ptCursorPos.y].size();
    m_nIdealCharPos = CalculateActualOffset(m_ptCursorPos.y, m_ptCursorPos.x);
    EnsureVisible(m_ptCursorPos);
    _view.update_caret();
    if (!selecting)
        m_ptAnchor = m_ptCursorPos;
    select(text_selection(m_ptAnchor, m_ptCursorPos));
}

void document::MovePgUp(bool selecting)
{
    int nNewTopLine = _char_offset.cy - screen_lines() + 1;
    if (nNewTopLine < 0)
        nNewTopLine = 0;
    if (_char_offset.cy != nNewTopLine)
    {
        _view.ScrollToLine(nNewTopLine);
    }

    m_ptCursorPos.y -= screen_lines() - 1;
    if (m_ptCursorPos.y < 0)
        m_ptCursorPos.y = 0;

    auto size = _lines[m_ptCursorPos.y].size();

    if (m_ptCursorPos.x > size)
        m_ptCursorPos.x = size;

    m_nIdealCharPos = CalculateActualOffset(m_ptCursorPos.y, m_ptCursorPos.x);
    EnsureVisible(m_ptCursorPos);	//todo: no vertical scroll
    _view.update_caret();
    if (!selecting)
        m_ptAnchor = m_ptCursorPos;
    select(text_selection(m_ptAnchor, m_ptCursorPos));
}

void document::MovePgDn(bool selecting)
{
    int nNewTopLine = _char_offset.cy + screen_lines() - 1;
    if (nNewTopLine >= _lines.size())
        nNewTopLine = _lines.size() - 1;
    if (_char_offset.cy != nNewTopLine)
    {
        _view.ScrollToLine(nNewTopLine);
    }

    m_ptCursorPos.y += screen_lines() - 1;
    if (m_ptCursorPos.y >= _lines.size())
        m_ptCursorPos.y = _lines.size() - 1;

    auto size = _lines[m_ptCursorPos.y].size();

    if (m_ptCursorPos.x > size)
        m_ptCursorPos.x = size;
    m_nIdealCharPos = CalculateActualOffset(m_ptCursorPos.y, m_ptCursorPos.x);
    EnsureVisible(m_ptCursorPos);	//todo: no vertical scroll
    _view.update_caret();
    if (!selecting)
        m_ptAnchor = m_ptCursorPos;
    select(text_selection(m_ptAnchor, m_ptCursorPos));
}

void document::MoveCtrlHome(bool selecting)
{
    m_ptCursorPos.x = 0;
    m_ptCursorPos.y = 0;
    m_nIdealCharPos = CalculateActualOffset(m_ptCursorPos.y, m_ptCursorPos.x);
    EnsureVisible(m_ptCursorPos);
    _view.update_caret();
    if (!selecting)
        m_ptAnchor = m_ptCursorPos;
    select(text_selection(m_ptAnchor, m_ptCursorPos));
}

void document::MoveCtrlEnd(bool selecting)
{
    m_ptCursorPos.y = _lines.size() - 1;
    m_ptCursorPos.x = _lines[m_ptCursorPos.y].size();
    m_nIdealCharPos = CalculateActualOffset(m_ptCursorPos.y, m_ptCursorPos.x);
    EnsureVisible(m_ptCursorPos);
    _view.update_caret();
    if (!selecting)
        m_ptAnchor = m_ptCursorPos;
    select(text_selection(m_ptAnchor, m_ptCursorPos));
}

void document::ScrollUp()
{
    if (_char_offset.cy > 0)
    {
        _view.ScrollToLine(_char_offset.cy - 1);
    }
}

void document::ScrollDown()
{
    if (_char_offset.cy < _lines.size() - 1)
    {
        _view.ScrollToLine(_char_offset.cy + 1);
    }
}

void document::ScrollLeft()
{
    if (_char_offset.cx > 0)
    {
        _view.ScrollToChar(_char_offset.cx - 1);
        _view.update_caret();
    }
}

void document::ScrollRight()
{
    if (_char_offset.cx < max_line_length() - 1)
    {
        _view.ScrollToChar(_char_offset.cx + 1);
        _view.update_caret();
    }
}

text_location document::WordToRight(text_location pt) const
{
    const auto &line = _lines[pt.y];

    while (pt.x < line.size())
    {
        if (!iswalnum(line[pt.x]) && line[pt.x] != _T('_'))
            break;
        pt.x++;
    }
    return pt;
}

text_location document::WordToLeft(text_location pt) const
{
    const auto &line = _lines[pt.y];

    while (pt.x > 0)
    {
        if (!iswalnum(line[pt.x - 1]) && line[pt.x - 1] != _T('_'))
            break;
        pt.x--;
    }
    return pt;
}

void document::SelectAll()
{
    int line_count = _lines.size();
    m_ptCursorPos.x = _lines[line_count - 1].size();
    m_ptCursorPos.y = line_count - 1;
    select(text_selection(text_location(0, 0), m_ptCursorPos));
    _view.update_caret();
}



void document::Copy()
{
    if (_selection._start == _selection._end)
        return;

    PrepareSelBounds();
    _view.text_to_clipboard(Combine(text(m_ptDrawSel)));
}

bool document::CanPaste()
{
    return IsClipboardFormatAvailable(CF_UNICODETEXT) != 0;
}





bool document::QueryEditable()
{
    return true;
}

void document::Paste()
{
    if (QueryEditable())
    {
        auto text = _view.text_from_clipboard();

        if (!text.empty())
        {
            undo_group ug(*this);
            auto pos = delete_text(ug, selection());
            locate(insert_text(ug, pos, text));
        }
    }
}

void document::Cut()
{
    if (QueryEditable() && HasSelection())
    {
        auto sel = selection();
        _view.text_to_clipboard(Combine(text(sel)));

        undo_group ug(*this);
        locate(delete_text(ug, sel));
    }
}

void document::OnEditDelete()
{
    if (QueryEditable())
    {
        auto sel = selection();

        if (sel.empty())
        {
            if (sel._end.x == _lines[sel._end.y].size())
            {
                if (sel._end.y == _lines.size() - 1)
                    return;

                sel._end.y++;
                sel._end.x = 0;
            }
            else
            {
                sel._end.x++;
            }
        }

        undo_group ug(*this);
        locate(delete_text(ug, sel));
    }
}

void document::OnChar(UINT nChar, UINT nRepCnt, UINT nFlags)
{
    if ((::GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0 ||
        (::GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0)
        return;

    bool bTranslated = false;

    if (nChar == VK_RETURN)
    {
        if (QueryEditable())
        {
            undo_group ug(*this);
            auto pos = delete_text(ug, selection());
            locate(insert_text(ug, pos, L'\n'));
        }

        return;
    }

    if (nChar > 31)
    {
        if (QueryEditable())
        {
            undo_group ug(*this);
            auto pos = delete_text(ug, selection());
            locate(insert_text(ug, pos, nChar));
        }
    }
}

void document::OnEditDeleteBack()
{
    if (QueryEditable())
    {
        if (HasSelection())
        {
            OnEditDelete();
        }
        else
        {
            undo_group ug(*this);
            locate(delete_text(ug, cursor_pos()));
        }
    }
}

void document::OnEditTab()
{
    if (QueryEditable())
    {
        auto sel = selection();

        if (sel._end.y > sel._start.y)
        {
            undo_group ug(*this);

            int nStartLine = sel._start.y;
            int nEndLine = sel._end.y;
            sel._start.x = 0;

            if (sel._end.x > 0)
            {
                if (sel._end.y == _lines.size() - 1)
                {
                    sel._end.x = _lines[sel._end.y].size();
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
                insert_text(ug, text_location(0, i), pszText);
            }

            _view.RecalcHorzScrollBar();
        }
        else
        {
            undo_group ug(*this);
            auto pos = delete_text(ug, selection());
            locate(insert_text(ug, pos, L'\t'));
        }
    }
}

void document::OnEditUntab()
{
    if (QueryEditable())
    {
        auto sel = selection();

        if (sel._end.y > sel._start.y)
        {
            undo_group ug(*this);

            int nStartLine = sel._start.y;
            int nEndLine = sel._end.y;
            sel._start.x = 0;
            if (sel._end.x > 0)
            {
                if (sel._end.y == _lines.size() - 1)
                {
                    sel._end.x = _lines[sel._end.y].size();
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
                const auto &line = _lines[i];

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
                        delete_text(ug, text_selection(0, i, nPos, i));
                    }
                }
            }

            _view.RecalcHorzScrollBar();
        }
        else
        {
            auto ptCursorPos = cursor_pos();

            if (ptCursorPos.x > 0)
            {
                int tabSize = tab_size();
                int nOffset = CalculateActualOffset(ptCursorPos.y, ptCursorPos.x);
                int nNewOffset = nOffset / tabSize * tabSize;
                if (nOffset == nNewOffset && nNewOffset > 0)
                    nNewOffset -= tabSize;
                assert(nNewOffset >= 0);

                const auto &line = _lines[ptCursorPos.y];
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
                locate(ptCursorPos);
            }
        }
    }
}

void document::DoDragScroll(const CPoint &point)
{
    auto rcClientRect = client_rect();

    if (point.y < rcClientRect.top + DRAG_BORDER_Y)
    {
        HideDropIndicator();
        ScrollUp();
        _view.ShowDropIndicator(point);
        return;
    }
    if (point.y >= rcClientRect.bottom - DRAG_BORDER_Y)
    {
        HideDropIndicator();
        ScrollDown();
        _view.ShowDropIndicator(point);
        return;
    }
    if (point.x < rcClientRect.left + margin_width() + DRAG_BORDER_X)
    {
        HideDropIndicator();
        ScrollLeft();
        _view.ShowDropIndicator(point);
        return;
    }
    if (point.x >= rcClientRect.right - DRAG_BORDER_X)
    {
        HideDropIndicator();
        ScrollRight();
        _view.ShowDropIndicator(point);
        return;
    }
}





void document::HideDropIndicator()
{
    if (m_bDropPosVisible)
    {
        SetCursorPos(m_ptSavedCaretPos);
        ShowCursor();
        m_bDropPosVisible = false;
    }
}

DROPEFFECT document::GetDropEffect()
{
    return DROPEFFECT_COPY | DROPEFFECT_MOVE;
}

void document::OnDropSource(DROPEFFECT de)
{
    if (m_bDraggingText && de == DROPEFFECT_MOVE)
    {
        undo_group ug(*this);
        delete_text(ug, m_ptDraggedText);
    }
}


void document::OnEditReplace()
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
    //	selection(m_ptSavedSelStart, m_ptSavedSelEnd);
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
    //	dlg.m_ptCurrentPos = cursor_pos();
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

bool document::ReplaceSelection(const wchar_t * pszNewText)
{
    //assert(pszNewText != nullptr);
    //if (! HasSelection())
    //	return false;

    //DeleteCurrentSelection();

    //text_location ptCursorPos = cursor_pos();
    //int x, y;
    //_lines.insert_text(this, ptCursorPos.y, ptCursorPos.x, pszNewText, y, x, CE_ACTION_REPLACE);
    //text_location ptEndOfBlock = text_location(x, y);
    //SetAnchor(ptEndOfBlock);
    //select(ptCursorPos, ptEndOfBlock);
    //SetCursorPos(ptEndOfBlock);
    //EnsureVisible(ptEndOfBlock);
    return true;
}



void document::OnEditUndo()
{
    if (can_undo())
    {
        locate(undo());
    }
}


void document::OnEditRedo()
{
    if (can_redo())
    {
        locate(redo());
    }
}



//void document::OnEditOperation(int nAction, const std::wstring &text)
//{
//	if (m_bAutoIndent)
//	{
//		//	Analyse last action...
//		if (nAction == CE_ACTION_TYPING && _tcscmp(text.c_str(), _T("\r\n")) == 0 && !_overtype)
//		{
//			//	Enter stroke!
//			text_location ptCursorPos = cursor_pos();
//			assert(ptCursorPos.y > 0);
//
//			//	Take indentation from the previos line
//			const auto &line = _lines.GetLineChars(ptCursorPos.y - 1);
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
//				_lines.insert_text(ug, ptCursorPos.y, ptCursorPos.x, pszInsertStr, y, x, CE_ACTION_AUTOINDENT);
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

bool document::GetOverwriteMode() const
{
    return _overtype;
}

void document::SetOverwriteMode(bool bOvrMode /*= true*/)
{
    _overtype = bOvrMode;
}

bool document::GetAutoIndent() const
{
    return m_bAutoIndent;
}

void document::SetAutoIndent(bool bAutoIndent)
{
    m_bAutoIndent = bAutoIndent;
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

void document::HighlightFromExtension(const wchar_t *ext)
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

void document::append_line(const std::wstring &text)
{
	_lines.push_back(document_line(text));
}

void document::clear()
{
	_lines.clear();
	_undo.clear();
}

static const char *crlfs [] =
{
	"\x0d\x0a",			//	DOS/Windows style
	"\x0a\x0d",			//	UNIX style
	"\x0a"				//	Macintosh style
};

static std::istream& safeGetline(std::istream& is, std::string& t)
{
	t.clear();

	// The characters in the stream are read one-by-one using a std::streambuf.
	// That is faster than reading them one-by-one using the std::istream.
	// Code that uses streambuf this way must be guarded by a sentry object.
	// The sentry object performs various tasks,
	// such as thread synchronization and updating the stream state.

	std::istream::sentry se(is, true);
	std::streambuf* sb = is.rdbuf();

	for (;;) {
		int c = sb->sbumpc();
		switch (c) {
		case '\n':
			return is;
		case '\r':
			if (sb->sgetc() == '\n')
				sb->sbumpc();
			return is;
		case EOF:
			// Also handle the case when the last line has no line ending
			if (t.empty())
				is.setstate(std::ios::eofbit);
			return is;
		default:
			t += (char) c;
		}
	}
}



struct _BOM_LOOKUP
{
	DWORD  bom;
	ULONG  len;
	Encoding    type;
};

struct _BOM_LOOKUP BOMLOOK [] =
{
	// define longest headers first
	{ 0x0000FEFF, 4, NCP_UTF32 },
	{ 0xFFFE0000, 4, NCP_UTF32BE },
	{ 0xBFBBEF, 3, NCP_UTF8 },
	{ 0xFFFE, 2, NCP_UTF16BE },
	{ 0xFEFF, 2, NCP_UTF16 },
	{ 0, 0, NCP_ASCII },
};

//
//	00 00 FE FF			UTF-32, big-endian 
//	FF FE 00 00			UTF-32, little-endian 
//	FE FF				UTF-16, big-endian 
//	FF FE				UTF-16, little-endian 
//	EF BB BF			UTF-8 
//
static Encoding detect_encoding(const unsigned char *header, size_t filesize, int &headerLen)
{
	for (int i = 0; BOMLOOK[i].len; i++)
	{
		if (filesize >= BOMLOOK[i].len &&
			memcmp(header, &BOMLOOK[i].bom, BOMLOOK[i].len) == 0)
		{
			headerLen = BOMLOOK[i].len;
			return BOMLOOK[i].type;
		}
	}

	headerLen = 0;

	if (header[0] == 0 && header[1] != 0 && header[2] == 0 && header[3] != 0)
	{
		return NCP_UTF16;
	}

	if (header[0] != 0 && header[1] == 0 && header[2] != 0 && header[3] == 0)
	{
		return NCP_UTF16BE;
	}
	
	return NCP_ASCII;
}

static line_endings detect_line_endings(const char *buffer, const int len)
{
	int I = 0;

	for (I = 0; I < len; I++)
	{
		if (buffer[I] == 0x0a)
			break;
	}
	if (I < len)
	{
		if (I > 0 && buffer[I - 1] == 0x0d)
		{
			return CRLF_STYLE_DOS;
		}
		else
		{
			if (I < len - 1 && buffer[I + 1] == 0x0d)
				return CRLF_STYLE_UNIX;
			else
				return CRLF_STYLE_MAC;
		}
	}

	return CRLF_STYLE_DOS; // guess
}

bool document::LoadFromFile(const std::wstring &path)
{
	clear();

	auto success = false;
	//std::wifstream f(path);

	//size_t bom = 0;
	//bom = f.get() + (bom << 8);
	//bom = f.get() + (bom << 8);
	//bom = f.get() + (bom << 8);

	////f.read((char*) &bom, 3);
	////bom &= 0xFFFFFF;

	//if (bom == 0xEFBBBF) //UTF8
	//{
	//	f.imbue(std::locale(f.getloc(), new std::codecvt_utf8<wchar_t, 1114111UL>));
	//}
	//else
	//{	
	//	bom &= 0xFFFF;

	//	if (bom == 0xFEFF) //UTF16LE
	//	{
	//		f.imbue(std::locale(f.getloc(), new std::codecvt_utf16<wchar_t, 1114111UL, std::little_endian>));
	//		f.seekg(2, std::ios::beg);
	//	}
	//	else if (bom == 0xFFFE) //UTF16BE
	//	{
	//		f.imbue(std::locale(f.getloc(), new std::codecvt_utf16<wchar_t, 1114111UL>));
	//		f.seekg(2, std::ios::beg);
	//	}
	//	else //ANSI
	//	{
	//		bom = 0;
	//		//f.imbue(std::locale(f.getloc()));
	//		f.seekg(std::ios::beg);
	//	}
	//}

	//std::u16string line;

	//if (f)
	//{
	//	while (f.good())
	//	{
	//		std::getline(f, line);
	//		append_line(line);
	//	}

	//	success = true;
	//}

	

	auto hFile = ::CreateFile(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);

	if (hFile != INVALID_HANDLE_VALUE)
	{
		const int bufferLen = 1024 * 64;
		char buffer[bufferLen];

		DWORD readLen;
		if (::ReadFile(hFile, buffer, bufferLen, &readLen, nullptr))
		{
			auto headerLen = 0;
			auto size = GetFileSize(hFile, nullptr);
			auto encoding = detect_encoding((const unsigned char *)buffer, size, headerLen);

			m_nCRLFMode = detect_line_endings(buffer, readLen);
			const char *crlf = crlfs[m_nCRLFMode];

			auto bufferPos = headerLen;

			if (encoding == NCP_UTF8 || encoding == NCP_ASCII)
			{
				std::string line;

				while (readLen > 0)
				{
					int c = buffer[bufferPos];

					if (c != 0x0A && c != 0x0D)
					{
						line += (char) c;
					}

					if (c == 0x0D)
					{
						append_line((encoding == NCP_ASCII) ? AsciiToUtf16(line) : UTF8ToUtf16(line));
						line.clear();
					}

					bufferPos++;

					if (bufferPos == readLen)
					{
						if (::ReadFile(hFile, buffer, bufferLen, &readLen, nullptr))
						{
							bufferPos = 0;
						}
						else
						{
							readLen = 0;
						}
					}
				}

				append_line((encoding == NCP_ASCII) ? AsciiToUtf16(line) : UTF8ToUtf16(line));
			}
			else if (encoding == NCP_UTF16BE || encoding == NCP_UTF16)
			{
				auto buffer16 = (const wchar_t *) buffer;
				readLen /= 2;

				std::wstring line;

				while (readLen > 0)
				{
					wchar_t c = buffer16[bufferPos];

					if (encoding == NCP_UTF16)
					{
						c = _byteswap_ushort(c);
					}

					if (c != 0x0A && c != 0x0D)
					{
						line += c;
					}

					if (c == 0x0D)
					{
						append_line(line);
						line.clear();
					}

					bufferPos++;

					if (bufferPos == readLen)
					{
						if (::ReadFile(hFile, buffer, bufferLen, &readLen, nullptr))
						{
							bufferPos = 0;
							readLen /= 2;
						}
						else
						{
							readLen = 0;
						}
					}
				}

				append_line(line);
			}

			_modified = false;
			m_nUndoPosition = 0;

			success = true;

			invalidate_view();
		}

		if (hFile != nullptr)
			::CloseHandle(hFile);
	}

	return success;
}


static std::wstring TempPathName()
{
	wchar_t result[MAX_PATH + 1] = { 0 };
	GetTempPath(MAX_PATH, result);
	GetTempFileName(result, L"CC_", 0, result);
	return result;
}

// Create a string with last error message
static std::wstring GetLastErrorMessage()
{
	std::wstring result;
	auto error = GetLastError();

	if (error)
	{
		LPVOID lpMsgBuf;
		DWORD bufLen = FormatMessageW(
			FORMAT_MESSAGE_ALLOCATE_BUFFER |
			FORMAT_MESSAGE_FROM_SYSTEM |
			FORMAT_MESSAGE_IGNORE_INSERTS,
			NULL,
			error,
			MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
			(LPWSTR) &lpMsgBuf,
			0, NULL);

		if (bufLen)
		{
			result = (LPCWSTR) lpMsgBuf;
			LocalFree(lpMsgBuf);
		}
	}
	return result;
}

bool document::SaveToFile(const std::wstring &path, int nCrlfStyle /*= CRLF_STYLE_AUTOMATIC*/, bool bClearModifiedFlag /*= true*/) const
{
	auto success = false;
	auto tempPath = TempPathName();
	std::ofstream fout(tempPath);

	if (fout)
	{
		bool first = true;

		for (const auto &line : _lines)
		{
			if (!first) fout << std::endl;
			fout << UTF16ToAscii(line._text);
			first = false;
		}

		fout.close();

		success = ::MoveFileEx(tempPath.c_str(), path.c_str(), MOVEFILE_COPY_ALLOWED | MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;

		if (success && bClearModifiedFlag)
		{
			_modified = false;
		}

		if (!success)
		{
			MessageBox(GetActiveWindow(), GetLastErrorMessage().c_str(), L"Diffractor", MB_OK);
		}
	}

	//assert(nCrlfStyle == CRLF_STYLE_AUTOMATIC || nCrlfStyle == CRLF_STYLE_DOS ||
	//	nCrlfStyle == CRLF_STYLE_UNIX || nCrlfStyle == CRLF_STYLE_MAC);

	//TCHAR szTempFileDir[_MAX_PATH + 1];
	//TCHAR szTempFileName[_MAX_PATH + 1];
	//TCHAR szBackupFileName[_MAX_PATH + 1];
	//auto success = false;

	//TCHAR drive[_MAX_PATH], dir[_MAX_PATH], name[_MAX_PATH], ext[_MAX_PATH];
	//_wsplitpath_s(pszFileName, drive, dir, name, ext);

	//lstrcpy(szTempFileDir, drive);
	//lstrcat(szTempFileDir, dir);
	//lstrcpy(szBackupFileName, pszFileName);
	//lstrcat(szBackupFileName, _T(".bak"));

	//if (::GetTempFileName(szTempFileDir, _T("CRE"), 0, szTempFileName) != 0)
	//{
	//	auto hTempFile = ::CreateFile(szTempFileName, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

	//	if (hTempFile != INVALID_HANDLE_VALUE)
	//	{
	//		if (nCrlfStyle == CRLF_STYLE_AUTOMATIC)
	//			nCrlfStyle = m_nCRLFMode;

	//		assert(nCrlfStyle >= 0 && nCrlfStyle <= 2);

	//		auto pszCRLF = crlfs[nCrlfStyle];
	//		auto nCRLFLength = strlen(pszCRLF);
	//		auto first = true;

	//		for (const auto &line : _lines)
	//		{
	//			auto len = line._text.size();
	//			DWORD dwWrittenBytes;

	//			if (!first)
	//			{
	//				::WriteFile(hTempFile, pszCRLF, nCRLFLength, &dwWrittenBytes, nullptr);
	//			}
	//			else
	//			{
	//				first = false;
	//			}

	//			if (!line._text.empty())
	//			{
	//				auto utf8 = ToUtf8(line._text);
	//				::WriteFile(hTempFile, utf8.c_str(), utf8.size(), &dwWrittenBytes, nullptr);
	//			}
	//		}

	//		::CloseHandle(hTempFile);
	//		hTempFile = INVALID_HANDLE_VALUE;

	//		if (m_bCreateBackupFile)
	//		{
	//			WIN32_FIND_DATA wfd;
	//			auto hSearch = ::FindFirstFile(pszFileName, &wfd);
	//			if (hSearch != INVALID_HANDLE_VALUE)
	//			{
	//				//	File exist - create backup file
	//				::DeleteFile(szBackupFileName);
	//				::MoveFile(pszFileName, szBackupFileName);
	//				::FindClose(hSearch);
	//			}
	//		}
	//		else
	//		{
	//			::DeleteFile(pszFileName);
	//		}

	//		//	Move temporary file to target name
	//		success = ::MoveFile(szTempFileName, pszFileName) != 0;

	//		if (bClearModifiedFlag)
	//		{
	//			_modified = false;
	//		}
	//	}
	//}

	return success;
}


std::vector<std::wstring> document::text(const text_selection &selection) const
{
	std::vector<std::wstring> result;

	if (!selection.empty())
	{
		if (selection._start.y == selection._end.y)
		{
			result.push_back(_lines[selection._start.y]._text.substr(selection._start.x, selection._end.x - selection._start.x));
		}
		else
		{
			for (int y = selection._start.y; y <= selection._end.y; y++)
			{
				const auto &text = _lines[y]._text;

				if (y == selection._start.y)
				{
					result.push_back(text.substr(selection._start.x, text.size() - selection._start.x));
				}
				else if (y == selection._end.y)
				{
					result.push_back(text.substr(0, selection._end.x));
				}
				else
				{
					result.push_back(text);
				}
			}
		}
	}

	return result;
}

std::vector<std::wstring> document::text() const
{
	std::vector<std::wstring> result;

	for (const auto &line : _lines)
	{
		result.push_back(line._text);
	}

	return result;
}

bool document::can_undo() const
{
	assert(m_nUndoPosition >= 0 && m_nUndoPosition <= _undo.size());
	return m_nUndoPosition > 0;
}

bool document::can_redo() const
{
	assert(m_nUndoPosition >= 0 && m_nUndoPosition <= _undo.size());
	return m_nUndoPosition < _undo.size();
}

text_location document::undo()
{
	assert(can_undo());
	m_nUndoPosition--;
	_modified = true;
	return _undo[m_nUndoPosition].undo(*this);
}

text_location document::redo()
{
	assert(can_redo());	
	_modified = true;
	auto result = _undo[m_nUndoPosition].redo(*this);
	m_nUndoPosition++;
	return result;
}

text_location document::insert_text(const text_location &location, const std::wstring &text)
{
	text_location resultLocation = location;

	for (const auto &c : text)
	{
		resultLocation = insert_text(resultLocation, c);
	}

	_modified = true;

	return resultLocation;
}

text_location document::insert_text(undo_group &ug, const text_location &location, const std::wstring &text)
{	
	text_location resultLocation = location;

	for (const auto &c : text)
	{
		resultLocation = insert_text(resultLocation, c);
	}

	ug.insert(text_selection(location, resultLocation), text);
	_modified = true;

	return resultLocation;
}

text_location document::insert_text(const text_location &location, const wchar_t &c)
{
	text_location resultLocation = location;
	auto &li = _lines[location.y];

	if (c == L'\n')
	{
		// Split
		document_line newLine(li._text.substr(location.x, li._text.size()));
		_lines.insert(_lines.begin() + location.y + 1, newLine);
		_lines[location.y]._text = _lines[location.y]._text.substr(0, location.x);

		resultLocation.y = location.y + 1;
		resultLocation.x = 0;

		invalidate_view();
	}
	else if (c != '\r')
	{
		auto before = li._text;
		auto after = li._text;

		after.insert(after.begin() + location.x, c);

		li._text = after;

		resultLocation.y = location.y;
		resultLocation.x = location.x + 1;

		invalidate_line(location.y);
	}

	return resultLocation;
}

text_location document::insert_text(undo_group &ug, const text_location &location, const wchar_t &c)
{
	ug.insert(location, c);
	_modified = true;
	return insert_text(location, c);
}

text_location document::delete_text(undo_group &ug, const text_location &location)
{
	if (location.x == 0)
	{
		if (location.y > 0)
		{
			ug.erase(text_location(_lines[location.y - 1].size(), location.y - 1), location.x > 0 ? _lines[location.y][location.x - 1] : '\n');
		}
	}
	else
	{
		ug.erase(text_location(location.x - 1, location.y), _lines[location.y][location.x - 1]);
	}

	_modified = true;
	return delete_text(location);
}

text_location document::delete_text(const text_selection &selection)
{
	if (!selection.empty())
	{
		if (selection._start.y == selection._end.y)
		{
			auto &li = _lines[selection._start.y];
			auto before = li._text;
			auto after = li._text;

			after.erase(after.begin() + selection._start.x, after.begin() + selection._end.x);
			li._text = after;

			invalidate_line(selection._start.y);
		}
		else
		{
			_lines[selection._start.y]._text.erase(_lines[selection._start.y]._text.begin() + selection._start.x, _lines[selection._start.y]._text.end());
			_lines[selection._start.y]._text.append(_lines[selection._end.y]._text.begin() + selection._end.x, _lines[selection._end.y]._text.end());

			if (selection._start.y + 1 < selection._end.y + 1)
			{
				_lines.erase(_lines.begin() + selection._start.y + 1, _lines.begin() + selection._end.y + 1);
			}

			invalidate_view();
		}
	}

	return selection._start;
}

text_location document::delete_text(undo_group &ug, const text_selection &selection)
{
	ug.erase(selection, Combine(text(selection)));
	_modified = true;
	return delete_text(selection);
}

text_location document::delete_text(const text_location &location)
{
	auto &line = _lines[location.y];
	auto resultPos = location;

	if (location.x == 0)
	{
		if (location.y > 0)
		{
			auto &previous = _lines[location.y - 1];

			resultPos.x = previous.size();
			resultPos.y = location.y - 1;

			previous._text.insert(previous._text.end(), line._text.begin(), line._text.end());
			_lines.erase(_lines.begin() + location.y);

			invalidate_view();
		}
	}
	else
	{
		auto &li = _lines[location.y];
		auto before = li._text;
		auto after = li._text;

		after.erase(after.begin() + location.x - 1, after.begin() + location.x);
		li._text = after;

		resultPos.x = location.x - 1;
		resultPos.y = location.y;

		invalidate_line(location.y);
	}


	return resultPos;
}



static wchar_t* wcsistr(wchar_t const* s1, wchar_t const* s2)
{
	auto s = s1;
	auto p = s2;

	do
	{
		if (!*p) return (wchar_t*) s1;
		if ((*p == *s) || (towlower(*p) == towlower(*s)))
		{
			++p;
			++s;
		}
		else
		{
			p = s2;
			if (!*s) return nullptr;
			s = ++s1;
		}

	} while (1);

	return nullptr;
}


static int FindStringHelper(const wchar_t * pszFindWhere, const wchar_t * pszFindWhat, bool bWholeWord)
{
	assert(pszFindWhere != nullptr);
	assert(pszFindWhat != nullptr);

	auto nCur = 0;
	auto nLength = wcslen(pszFindWhat);

	for (;;)
	{
		auto pszPos = wcsistr(pszFindWhere, pszFindWhat);

		if (pszPos == nullptr)
		{
			return -1;
		}

		if (!bWholeWord)
		{
			return nCur + (pszPos - pszFindWhere);
		}

		if (pszPos > pszFindWhere && (iswalnum(pszPos[-1]) || pszPos[-1] == _T('_')))
		{
			nCur += (pszPos - pszFindWhere);
			pszFindWhere = pszPos + 1;
			continue;
		}

		if (iswalnum(pszPos[nLength]) || pszPos[nLength] == _T('_'))
		{
			nCur += (pszPos - pszFindWhere + 1);
			pszFindWhere = pszPos + 1;
			continue;
		}

		return nCur + (pszPos - pszFindWhere);
	}

	assert(false);		// Unreachable
	return -1;
}

bool document::Find(const std::wstring &text, const text_location &ptStartPos, DWORD dwFlags, bool bWrapSearch, text_location *pptFoundPos)
{
	int line_count = _lines.size();

	return FindInBlock(text, ptStartPos, text_location(0, 0), text_location(_lines[line_count - 1]._text.size(), line_count - 1), dwFlags, bWrapSearch, pptFoundPos);
}

bool document::FindInBlock(const std::wstring &what, const text_location &ptStartPosition, const text_location &ptBlockBegin, const text_location &ptBlockEnd, DWORD dwFlags, bool bWrapSearch, text_location *pptFoundPos)
{
	text_location ptCurrentPos = ptStartPosition;

	assert(ptBlockBegin.y < ptBlockEnd.y || (ptBlockBegin.y == ptBlockEnd.y && ptBlockBegin.x <= ptBlockEnd.x));

	if (ptBlockBegin == ptBlockEnd)
		return false;

	if (ptCurrentPos.y < ptBlockBegin.y || (ptCurrentPos.y == ptBlockBegin.y && ptCurrentPos.x < ptBlockBegin.x))
	{
		ptCurrentPos = ptBlockBegin;
	}

	auto matchCase = (dwFlags & FIND_MATCH_CASE) != 0;
	auto wholeWord = (dwFlags & FIND_WHOLE_WORD) != 0;

	if (dwFlags & FIND_DIRECTION_UP)
	{
		//	Let's check if we deal with whole text.
		//	At this point, we cannot search *up* in selection
		//	Proceed as if we have whole text search.

		for (;;)
		{
			if (ptCurrentPos.x == 0)
			{
				ptCurrentPos.y--;
			}

			while (ptCurrentPos.y >= 0)
			{
				const auto &line = _lines[ptCurrentPos.y];
				const auto nLineLength = line._text.size() - ptCurrentPos.x;

				if (nLineLength > 0)
				{
					auto nPos = ::FindStringHelper(line._text.c_str() + ptCurrentPos.x, what.c_str(), wholeWord);

					if (nPos >= 0)		//	Found text!
					{
						ptCurrentPos.x += nPos;
						*pptFoundPos = ptCurrentPos;
						return true;
					}
				}

				ptCurrentPos.x = 0;
				ptCurrentPos.y--;
			}

			//	Beginning of text reached
			if (!bWrapSearch)
				return false;

			//	Start again from the end of text
			bWrapSearch = false;
			ptCurrentPos = text_location(0, _lines.size() - 1);
		}
	}
	else
	{
		for (;;)
		{
			while (ptCurrentPos.y <= ptBlockEnd.y)
			{
				const auto &line = _lines[ptCurrentPos.y];
				const auto nLineLength = line._text.size() - ptCurrentPos.x;

				if (nLineLength > 0)
				{
					int nPos = ::FindStringHelper(line._text.c_str() + ptCurrentPos.x, what.c_str(), wholeWord);

					if (nPos >= 0)
					{
						ptCurrentPos.x += nPos;
						//	Check of the text found is outside the block.
						if (ptCurrentPos.y == ptBlockEnd.y && ptCurrentPos.x >= ptBlockEnd.x)
							break;

						*pptFoundPos = ptCurrentPos;
						return true;
					}
				}

				//	Go further, text was not found
				ptCurrentPos.x = 0;
				ptCurrentPos.y++;
			}

			//	End of text reached
			if (!bWrapSearch)
				return false;

			//	Start from the beginning
			bWrapSearch = false;
			ptCurrentPos = ptBlockBegin;
		}
	}

	assert(false);		// Unreachable
	return false;
}