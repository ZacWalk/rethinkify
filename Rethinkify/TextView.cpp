#include "pch.h"
#include "TextView.h"
#include "TextBuffer.h"
#include "resource.h"

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
	TextView *m_pOwner;
public:
	CEditDropTargetImpl(TextView *pOwner) { m_pOwner = pOwner; };

	DROPEFFECT OnDragEnter(CWindow wnd, COleDataObject* pDataObject, DWORD dwKeyState, CPoint point);
	void OnDragLeave(CWindow wnd);
	DROPEFFECT OnDragOver(CWindow wnd, COleDataObject* pDataObject, DWORD dwKeyState, CPoint point);
	bool OnDrop(CWindow wnd, COleDataObject* pDataObject, DROPEFFECT dropEffect, CPoint point);
	DROPEFFECT OnDragScroll(CWindow wnd, DWORD dwKeyState, CPoint point);
};

static auto s_textHighlighter = std::make_shared<TextHighight>();

TextView::TextView(TextBuffer &buffer) : _buffer(buffer), _highlight(s_textHighlighter)
{
	_dropTarget = nullptr;
	_backBuffer = nullptr;

	memset(m_apFonts, 0, sizeof(HFONT) * 4);

	m_bSelMargin = true;
	_buffer.AddView(this);

	ResetView();
}

TextView::~TextView()
{
	assert(_dropTarget == nullptr);
	assert(_backBuffer == nullptr);
}

const TextSelection &TextView::GetSelection() const
{
	PrepareSelBounds();
	return m_ptDrawSel;
}

int TextView::GetLineActualLength(int lineIndex) const
{
	if (_actualLineLengths.size() != _buffer.LineCount())
	{
		_actualLineLengths.clear();
		_actualLineLengths.insert(_actualLineLengths.begin(), _buffer.LineCount(), 0);
	}

	if (_actualLineLengths[lineIndex] == 0)
	{
		auto nActualLength = 0;
		const auto &line = _buffer[lineIndex];

		if (!line.empty())
		{
			auto nLength = line.size();
			auto pszCurrent = line.c_str();
			auto tabSize = GetTabSize();

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

void TextView::ScrollToChar(int nNewOffsetChar, bool bTrackScrollBar)
{
	if (m_nOffsetChar != nNewOffsetChar)
	{
		int nScrollChars = m_nOffsetChar - nNewOffsetChar;
		m_nOffsetChar = nNewOffsetChar;
		CRect rcScroll;
		GetClientRect(&rcScroll);
		rcScroll.left += GetMarginWidth();
		ScrollWindowEx(nScrollChars * GetCharWidth(), 0, &rcScroll, &rcScroll, nullptr, nullptr, SW_INVALIDATE);
		UpdateWindow();
		if (bTrackScrollBar)
			RecalcHorzScrollBar(true);
	}
}

void TextView::ScrollToLine(int nNewTopLine, bool bTrackScrollBar)
{
	if (m_nTopLine != nNewTopLine)
	{
		int nScrollLines = m_nTopLine - nNewTopLine;
		m_nTopLine = nNewTopLine;
		ScrollWindowEx(0, nScrollLines * GetLineHeight(), nullptr, nullptr, nullptr, nullptr, SW_INVALIDATE);
		UpdateWindow();
		if (bTrackScrollBar)
			RecalcVertScrollBar(true);
	}
}

std::wstring TextView::ExpandChars(const std::wstring &text, int nOffset, int nCount) const
{
	std::wstring result;

	if (nCount > 0)
	{
		auto pszChars = text.c_str();
		int tabSize = GetTabSize();
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

void TextView::DrawLineHelperImpl(HDC pdc, TextLocation &ptOrigin, const CRect &rcClip, const wchar_t * pszChars, int nOffset, int nCount) const
{
	if (nCount > 0)
	{
		auto line = ExpandChars(pszChars, nOffset, nCount);
		auto nWidth = rcClip.right - ptOrigin.x;

		if (nWidth > 0)
		{
			auto nCharWidth = GetCharWidth();
			auto nCount = line.size();
			auto nCountFit = nWidth / nCharWidth + 1;

			if (nCount > nCountFit)
				nCount = nCountFit;

			/*
			CRect rcBounds = rcClip;
			rcBounds.left = ptOrigin.x;
			rcBounds.right = rcBounds.left + GetCharWidth() * nCount;
			pdc->ExtTextOut(rcBounds.left, rcBounds.top, ETO_OPAQUE, &rcBounds, nullptr, 0, nullptr);
			*/
			::ExtTextOut(pdc, ptOrigin.x, ptOrigin.y, ETO_CLIPPED, &rcClip, line.c_str(), nCount, nullptr);
		}
		ptOrigin.x += GetCharWidth() * line.size();
	}
}

void TextView::DrawLineHelper(HDC pdc, TextLocation &ptOrigin, const CRect &rcClip, int nColorIndex, const wchar_t * pszChars, int nOffset, int nCount, TextLocation ptTextPos) const
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

void TextView::GetLineColors(int lineIndex, COLORREF &crBkgnd, COLORREF &crText, bool &bDrawWhitespace) const
{
	bDrawWhitespace = true;
	crText = RGB(255, 255, 255);
	crBkgnd = CLR_NONE;
	crText = CLR_NONE;
	bDrawWhitespace = false;
}

DWORD TextView::GetParseCookie(int lineIndex) const
{
	const auto invalid = (DWORD) -1;
	int nLineCount = _buffer.LineCount();

	if (_parseCookies.empty())
	{
		_parseCookies.insert(_parseCookies.begin(), nLineCount, invalid);
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



void TextView::DrawSingleLine(HDC pdc, const CRect &rc, int lineIndex) const
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
			if ((m_bFocused || m_bShowInactiveSelection) && IsInsideSelBlock(TextLocation(0, lineIndex)))
			{
				FillSolidRect(pdc, rect.left, rect.top, GetCharWidth(), rect.Height(), GetColor(IHighlight::COLORINDEX_SELBKGND));
				rect.left += GetCharWidth();
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
		TextLocation origin(rc.left - m_nOffsetChar * GetCharWidth(), rc.top);
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
			DrawLineHelper(pdc, origin, rc, IHighlight::COLORINDEX_NORMALTEXT, pszChars, 0, pBuf[0].m_nCharPos, TextLocation(0, lineIndex));
			for (int i = 0; i < nBlocks - 1; i++)
			{
				assert(pBuf[i].m_nCharPos >= 0 && pBuf[i].m_nCharPos <= nLength);
				if (crText == CLR_NONE)
					SetTextColor(pdc, GetColor(pBuf[i].m_nColorIndex));
				SelectObject(pdc, GetFont(GetItalic(pBuf[i].m_nColorIndex), GetBold(pBuf[i].m_nColorIndex)));
				DrawLineHelper(pdc, origin, rc, pBuf[i].m_nColorIndex, pszChars,
					pBuf[i].m_nCharPos, pBuf[i + 1].m_nCharPos - pBuf[i].m_nCharPos,
					TextLocation(pBuf[i].m_nCharPos, lineIndex));
			}
			assert(pBuf[nBlocks - 1].m_nCharPos >= 0 && pBuf[nBlocks - 1].m_nCharPos <= nLength);
			if (crText == CLR_NONE)
				SetTextColor(pdc, GetColor(pBuf[nBlocks - 1].m_nColorIndex));
			SelectObject(pdc, GetFont(GetItalic(pBuf[nBlocks - 1].m_nColorIndex),
				GetBold(pBuf[nBlocks - 1].m_nColorIndex)));
			DrawLineHelper(pdc, origin, rc, pBuf[nBlocks - 1].m_nColorIndex, pszChars,
				pBuf[nBlocks - 1].m_nCharPos, nLength - pBuf[nBlocks - 1].m_nCharPos,
				TextLocation(pBuf[nBlocks - 1].m_nCharPos, lineIndex));
		}
		else
		{
			if (crText == CLR_NONE)
				SetTextColor(pdc, GetColor(IHighlight::COLORINDEX_NORMALTEXT));
			SelectObject(pdc, GetFont(GetItalic(IHighlight::COLORINDEX_NORMALTEXT), GetBold(IHighlight::COLORINDEX_NORMALTEXT)));
			DrawLineHelper(pdc, origin, rc, IHighlight::COLORINDEX_NORMALTEXT, pszChars, 0, nLength, TextLocation(0, lineIndex));
		}

		//	Draw whitespaces to the left of the text
		auto frect = rc;
		if (origin.x > frect.left)
			frect.left = origin.x;
		if (frect.right > frect.left)
		{
			if ((m_bFocused || m_bShowInactiveSelection) && IsInsideSelBlock(TextLocation(nLength, lineIndex)))
			{
				FillSolidRect(pdc, frect.left, frect.top, GetCharWidth(), frect.Height(), GetColor(IHighlight::COLORINDEX_SELBKGND));
				frect.left += GetCharWidth();
			}
			if (frect.right > frect.left)
				FillSolidRect(pdc, frect, bDrawWhitespace ? crBkgnd : GetColor(IHighlight::COLORINDEX_WHITESPACE));
		}

		_freea(pBuf);
	}
}

COLORREF TextView::GetColor(int nColorIndex) const
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

//COLORREF TextView::GetColor(int nColorIndex)
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


void TextView::DrawMargin(HDC pdc, const CRect &rect, int lineIndex) const
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

bool TextView::IsInsideSelBlock(TextLocation ptTextPos) const
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

bool TextView::IsInsideSelection(const TextLocation &ptTextPos) const
{
	PrepareSelBounds();
	return IsInsideSelBlock(ptTextPos);
}

void TextView::PrepareSelBounds() const
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

void TextView::OnDraw(HDC pdc)
{
	CRect rcClient;
	GetClientRect(rcClient);

	auto nLineCount = _buffer.LineCount();
	auto nLineHeight = GetLineHeight();
	PrepareSelBounds();

	auto cacheDC = CreateCompatibleDC(pdc);

	if (_backBuffer == nullptr)
	{
		_backBuffer = CreateCompatibleBitmap(pdc, rcClient.Width(), nLineHeight);
	}

	auto pOldBitmap = SelectObject(cacheDC, _backBuffer);

	auto rcLine = rcClient;
	rcLine.bottom = rcLine.top + nLineHeight;
	CRect rcCacheMargin(0, 0, GetMarginWidth(), nLineHeight);
	CRect rcCacheLine(GetMarginWidth(), 0, rcLine.Width(), nLineHeight);

	auto nCurrentLine = m_nTopLine;

	while (rcLine.top < rcClient.bottom)
	{
		if (nCurrentLine < nLineCount)
		{
			DrawMargin(cacheDC, rcCacheMargin, nCurrentLine);
			DrawSingleLine(cacheDC, rcCacheLine, nCurrentLine);
		}
		else
		{
			DrawMargin(cacheDC, rcCacheMargin, -1);
			DrawSingleLine(cacheDC, rcCacheLine, -1);
		}

		BitBlt(pdc, rcLine.left, rcLine.top, rcLine.Width(), rcLine.Height(), cacheDC, 0, 0, SRCCOPY);

		nCurrentLine++;
		rcLine.OffsetRect(0, nLineHeight);
	}

	SelectObject(cacheDC, pOldBitmap);
	DeleteDC(cacheDC);
}

void TextView::ResetView()
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
	if (::IsWindow(m_hWnd))
		UpdateCaret();
	m_bLastSearch = false;
	m_bShowInactiveSelection = false;
	m_bPrintHeader = false;
	m_bPrintFooter = true;

	m_bMultipleSearch = false;	// More search
}

void TextView::UpdateCaret()
{
	if (m_bFocused && !m_bCursorHidden &&
		CalculateActualOffset(m_ptCursorPos.y, m_ptCursorPos.x) >= m_nOffsetChar)
	{
		CreateSolidCaret(2, GetLineHeight());

		auto pt = TextToClient(m_ptCursorPos);
		SetCaretPos(pt.x, pt.y);
		ShowCaret();
	}
	else
	{
		HideCaret();
	}
}

int TextView::GetTabSize() const
{
	assert(m_tabSize >= 0 && m_tabSize <= 64);
	return m_tabSize;
}

void TextView::SetTabSize(int tabSize)
{
	assert(tabSize >= 0 && tabSize <= 64);
	if (m_tabSize != tabSize)
	{
		m_tabSize = tabSize;
		_actualLineLengths.clear();
		m_nMaxLineLength = -1;
		RecalcHorzScrollBar();
		Invalidate();
		UpdateCaret();
	}
}

HFONT TextView::GetFont(bool bItalic /*= false*/, bool bBold /*= false*/) const
{
	int nIndex = 0;
	if (bBold) nIndex |= 1;
	if (bItalic) nIndex |= 2;

	if (m_apFonts[nIndex] == nullptr)
	{
		auto f = _font;
		f.lfWeight = bBold ? FW_BOLD : FW_NORMAL;
		f.lfItalic = (BYTE) bItalic;
		m_apFonts[nIndex] = ::CreateFontIndirect(&f);
	}

	return m_apFonts[nIndex];
}

void TextView::CalcLineCharDim() const
{
	auto pThis = const_cast<TextView*>(this);
	HDC pdc = pThis->GetDC();

	auto pOldFont = SelectObject(pdc, pThis->GetFont());
	
	CSize extent;
	GetTextExtentExPoint(pdc, _T("X"), 1, 1, nullptr, nullptr, &extent);

	m_nLineHeight = extent.cy;
	if (m_nLineHeight < 1)
		m_nLineHeight = 1;
	m_nCharWidth = extent.cx;
	/*
	TEXTMETRIC tm;
	if (pdc->GetTextMetrics(&tm))
	m_nCharWidth -= tm.tmOverhang;
	*/
	SelectObject(pdc, pOldFont);
	pThis->ReleaseDC(pdc);
}

int TextView::GetLineHeight() const
{
	if (m_nLineHeight == -1)
		CalcLineCharDim();
	return m_nLineHeight;
}

int TextView::GetCharWidth() const
{
	if (m_nCharWidth == -1)
	{
		CalcLineCharDim();
	}

	return m_nCharWidth;
}

int TextView::GetMaxLineLength() const
{
	if (m_nMaxLineLength == -1)
	{
		m_nMaxLineLength = 0;
		auto nLineCount = _buffer.LineCount();

		for (int i = 0; i < nLineCount; i++)
		{
			int nActualLength = GetLineActualLength(i);

			if (m_nMaxLineLength < nActualLength)
				m_nMaxLineLength = nActualLength;
		}
	}

	return m_nMaxLineLength;
}

void TextView::OnPrepareDC(HDC pDC, CPrintInfo* pInfo)
{
	/*CView::OnPrepareDC(pDC, pInfo);

	if (pInfo != nullptr)
	{
	pInfo->m_bContinuePrinting = true;
	if (_pages != nullptr && (int) pInfo->m_nCurPage > _pages.size())
	pInfo->m_bContinuePrinting = false;
	}*/
}

bool TextView::OnPreparePrinting(CPrintInfo* pInfo)
{
	//return DoPreparePrinting(pInfo);
	return 0;
}

int TextView::PrintLineHeight(HDC pdc, int nLine)
{
	assert(nLine >= 0 && nLine < _buffer.LineCount());
	assert(m_nPrintLineHeight > 0);

	const auto &line = _buffer[nLine];

	if (line.empty())
		return m_nPrintLineHeight;

	auto expanded = ExpandChars(line._text, 0, line.size());

	auto rcPrintArea = m_rcPrintArea;
	DrawText(pdc, expanded.c_str(), -1, rcPrintArea, DT_LEFT | DT_NOPREFIX | DT_TOP | DT_WORDBREAK | DT_CALCRECT);
	return rcPrintArea.Height();
}

std::wstring TextView::GetPrintHeaderText(int nPageNum) const
{
	assert(m_bPrintHeader);
	return std::wstring();
}

std::wstring TextView::GetPrintFooterText(int nPageNum) const
{
	assert(m_bPrintFooter);
	return String::Format(L"Page %d of %d", nPageNum, _pages.size());
}

void TextView::PrintHeader(HDC pdc, int nPageNum) const
{
	auto rcHeader = m_rcPrintArea;
	rcHeader.bottom = rcHeader.top;
	rcHeader.top -= (m_nPrintLineHeight + m_nPrintLineHeight / 2);

	auto text = GetPrintHeaderText(nPageNum);
	if (!text.empty())
		DrawText(pdc, text.c_str(), -1, rcHeader, DT_CENTER | DT_NOPREFIX | DT_TOP | DT_SINGLELINE);
}

void TextView::PrintFooter(HDC pdc, int nPageNum) const
{
	auto rcFooter = m_rcPrintArea;
	rcFooter.top = rcFooter.bottom;
	rcFooter.bottom += (m_nPrintLineHeight + m_nPrintLineHeight / 2);

	auto text= GetPrintFooterText(nPageNum);

	if (!text.empty())
		DrawText(pdc, text.c_str(), -1, &rcFooter, DT_CENTER | DT_NOPREFIX | DT_BOTTOM | DT_SINGLELINE);
}

void TextView::RecalcPageLayouts(HDC pdc, CPrintInfo *pInfo)
{
	// http://msdn.microsoft.com/en-us/library/ms646829(v=vs.85).aspx#setting_up

	/*m_ptPageArea = pInfo->m_rectDraw;
	m_ptPageArea.NormalizeRect();

	CSize extent;
	GetTextExtentExPoint(pdc, _T("X"), 1, 1, nullptr, nullptr, &extent);
	m_nPrintLineHeight = extent.cy;

	m_rcPrintArea = m_ptPageArea;
	CSize szTopLeft, szBottomRight;

	szTopLeft.cx = pApp->GetProfileInt(REG_PAGE_SUBKEY, REG_MARGIN_LEFT, DEFAULT_PRINT_MARGIN);
	szBottomRight.cx = pApp->GetProfileInt(REG_PAGE_SUBKEY, REG_MARGIN_RIGHT, DEFAULT_PRINT_MARGIN);
	szTopLeft.cy = pApp->GetProfileInt(REG_PAGE_SUBKEY, REG_MARGIN_TOP, DEFAULT_PRINT_MARGIN);
	szBottomRight.cy = pApp->GetProfileInt(REG_PAGE_SUBKEY, REG_MARGIN_BOTTOM, DEFAULT_PRINT_MARGIN);

	pdc->HIMETRICtoLP(&szTopLeft);
	pdc->HIMETRICtoLP(&szBottomRight);

	m_rcPrintArea.left += szTopLeft.cx;
	m_rcPrintArea.right -= szBottomRight.cx;
	m_rcPrintArea.top += szTopLeft.cy;
	m_rcPrintArea.bottom -= szBottomRight.cy;
	if (m_bPrintHeader)
		m_rcPrintArea.top += m_nPrintLineHeight + m_nPrintLineHeight / 2;
	if (m_bPrintFooter)
		m_rcPrintArea.bottom += m_nPrintLineHeight + m_nPrintLineHeight / 2;

	_pages.clear();

	int nLineCount = _buffer.LineCount();
	int nLine = 1;
	int y = m_rcPrintArea.top + PrintLineHeight(pdc, 0);
	while (nLine < nLineCount)
	{
		int nHeight = PrintLineHeight(pdc, nLine);
		if (y + nHeight <= m_rcPrintArea.bottom)
		{
			y += nHeight;
		}
		else
		{
			_pages.push_back(nLine);
			y = m_rcPrintArea.top + nHeight;
		}
		nLine++;
	}*/
}

void TextView::OnBeginPrinting(HDC pdc, CPrintInfo *pInfo)
{
	/*assert(_pages == nullptr);
	assert(m_pPrintFont == nullptr);
	HFONT pDisplayFont = GetFont();

	LOGFONT lf;
	pDisplayFont->GetLogFont(&lf);

	HDC pDisplayDC = GetDC();
	lf.lfHeight = MulDiv(lf.lfHeight, pdc->GetDeviceCaps(LOGPIXELSY), pDisplayDC->GetDeviceCaps(LOGPIXELSY) * 2);
	lf.lfWidth = MulDiv(lf.lfWidth, pdc->GetDeviceCaps(LOGPIXELSX), pDisplayDC->GetDeviceCaps(LOGPIXELSX) * 2);
	ReleaseDC(pDisplayDC);

	m_pPrintFont = new CFont;
	if (! m_pPrintFont->CreateFontIndirect(&lf))
	{
	delete m_pPrintFont;
	m_pPrintFont = nullptr;
	return;
	}

	pdc->SelectObject(m_pPrintFont);*/
}

void TextView::OnEndPrinting(HDC pdc, CPrintInfo *pInfo)
{
	/*if (m_pPrintFont != nullptr)
	{
	delete m_pPrintFont;
	m_pPrintFont = nullptr;
	}
	if (_pages != nullptr)
	{
	delete _pages;
	_pages = nullptr;
	}
	_pages.size() = 0;
	m_nPrintLineHeight = 0;*/
}

void TextView::OnPrint(HDC pdc, CPrintInfo* pInfo)
{
	/*if (_pages == nullptr)
	{
	RecalcPageLayouts(pdc, pInfo);
	assert(_pages != nullptr);
	}

	assert(pInfo->m_nCurPage >= 1 && (int) pInfo->m_nCurPage <= _pages.size());
	int nLine = _pages[pInfo->m_nCurPage - 1];
	int nEndLine = _buffer.LineCount();
	if ((int) pInfo->m_nCurPage < _pages.size())
	nEndLine = _pages[pInfo->m_nCurPage];
	TRACE(_T("Printing page %d of %d, lines %d - %d\n"), pInfo->m_nCurPage, _pages.size(),
	nLine, nEndLine - 1);

	if (m_bPrintHeader)
	PrintHeader(pdc, pInfo->m_nCurPage);
	if (m_bPrintFooter)
	PrintFooter(pdc, pInfo->m_nCurPage);

	int y = m_rcPrintArea.top;
	for (; nLine < nEndLine; nLine ++)
	{
	int nLineLength = GetLineLength(nLine);
	if (nLineLength == 0)
	{
	y += m_nPrintLineHeight;
	continue;
	}

	CRect rcPrintRect = m_rcPrintArea;
	rcPrintRect.top = y;
	auto line =ExpandChars(GetLineChars(nLine), 0, nLineLength, line);
	y += pdc->DrawText(line, &rcPrintRect, DT_LEFT | DT_NOPREFIX | DT_TOP | DT_WORDBREAK);
	}*/
}

int TextView::GetScreenLines() const
{
	if (m_nScreenLines == -1)
	{
		CRect rect;
		GetClientRect(&rect);
		m_nScreenLines = rect.Height() / GetLineHeight();
	}
	return m_nScreenLines;
}

bool TextView::GetItalic(int nColorIndex) const
{
	return false;
}

bool TextView::GetBold(int nColorIndex) const
{
	return false;
}

int TextView::GetScreenChars() const
{
	if (m_nScreenChars == -1)
	{
		CRect rect;
		GetClientRect(&rect);
		m_nScreenChars = (rect.Width() - GetMarginWidth()) / GetCharWidth();
	}
	return m_nScreenChars;
}

void TextView::OnDestroy()
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

bool TextView::OnEraseBkgnd(HDC pdc)
{
	return true;
}

void TextView::OnSize(UINT nType, int cx, int cy)
{
	if (_backBuffer != nullptr)
	{
		DeleteObject(_backBuffer);
		_backBuffer = nullptr;
	}

	m_nScreenLines = -1;
	m_nScreenChars = -1;
	RecalcVertScrollBar();
	RecalcHorzScrollBar();
}


void TextView::RecalcVertScrollBar(bool bPositionOnly /*= false*/)
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
		if (GetScreenLines() >= _buffer.LineCount() && m_nTopLine > 0)
		{
			m_nTopLine = 0;
			Invalidate();
			UpdateCaret();
		}
		si.fMask = SIF_PAGE | SIF_POS | SIF_RANGE;
		si.nMin = 0;
		si.nMax = _buffer.LineCount() - 1;
		si.nPage = GetScreenLines();
		si.nPos = m_nTopLine;
	}
	SetScrollInfo(SB_VERT, &si);
}

void TextView::OnVScroll(UINT nSBCode, UINT nPos, HWND pScrollBar)
{

	//	Note we cannot use nPos because of its 16-bit nature
	SCROLLINFO si;
	si.cbSize = sizeof(si);
	si.fMask = SIF_ALL;
	GetScrollInfo(SB_VERT, &si);

	int nPageLines = GetScreenLines();
	int nLineCount = _buffer.LineCount();

	int nNewTopLine;
	switch (nSBCode)
	{
	case SB_TOP:
		nNewTopLine = 0;
		break;
	case SB_BOTTOM:
		nNewTopLine = nLineCount - nPageLines + 1;
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
	if (nNewTopLine >= nLineCount)
		nNewTopLine = nLineCount - 1;

	ScrollToLine(nNewTopLine);
}

void TextView::RecalcHorzScrollBar(bool bPositionOnly /*= false*/)
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
		if (GetScreenChars() >= GetMaxLineLength() && m_nOffsetChar > 0)
		{
			m_nOffsetChar = 0;
			Invalidate();
			UpdateCaret();
		}
		si.fMask = SIF_PAGE | SIF_POS | SIF_RANGE;
		si.nMin = 0;
		si.nMax = GetMaxLineLength() - 1;
		si.nPage = GetScreenChars();
		si.nPos = m_nOffsetChar;
	}
	SetScrollInfo(SB_HORZ, &si);
}

void TextView::OnHScroll(UINT nSBCode, UINT nPos, HWND pScrollBar)
{

	SCROLLINFO si;
	si.cbSize = sizeof(si);
	si.fMask = SIF_ALL;
	GetScrollInfo(SB_HORZ, &si);

	int nPageChars = GetScreenChars();
	int nMaxLineLength = GetMaxLineLength();

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
	UpdateCaret();
}

bool TextView::OnSetCursor(CWindow wnd, UINT nHitTest, UINT message)
{
	static auto arrow = ::LoadCursor(nullptr, MAKEINTRESOURCE(IDC_ARROW));
	static auto beam = ::LoadCursor(nullptr, MAKEINTRESOURCE(IDC_IBEAM));

	if (nHitTest == HTCLIENT)
	{
		CPoint pt;
		::GetCursorPos(&pt);
		ScreenToClient(&pt);

		if (pt.x < GetMarginWidth())
		{
			::SetCursor(arrow);
		}
		else
		{
			auto ptText = ClientToText(pt);
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

TextLocation TextView::ClientToText(const CPoint &point) const
{
	int nLineCount = _buffer.LineCount();

	TextLocation pt;
	pt.y = m_nTopLine + point.y / GetLineHeight();
	if (pt.y >= nLineCount)
		pt.y = nLineCount - 1;
	if (pt.y < 0)
		pt.y = 0;


	if (pt.y >= 0 && pt.y < nLineCount)
	{
		const auto &line = _buffer[pt.y];

		int nPos = m_nOffsetChar + (point.x - GetMarginWidth()) / GetCharWidth();
		if (nPos < 0)
			nPos = 0;

		int nIndex = 0, nCurPos = 0;
		int tabSize = GetTabSize();

		while (nIndex < line.size())
		{
			if (line[nIndex] == _T('\t'))
				nCurPos += (tabSize - nCurPos % tabSize);
			else
				nCurPos++;

			if (nCurPos > nPos)
				break;

			nIndex++;
		}

		pt.x = nIndex;
	}

	return pt;
}


CPoint TextView::TextToClient(const TextLocation &point) const
{
	const auto &line = _buffer[point.y];

	CPoint pt;
	pt.y = (point.y - m_nTopLine) * GetLineHeight();
	pt.x = 0;
	int tabSize = GetTabSize();

	for (int nIndex = 0; nIndex < point.x; nIndex++)
	{
		if (line[nIndex] == _T('\t'))
		{
			pt.x += (tabSize - pt.x % tabSize);
		}
		else
		{
			pt.x++;
		}
	}

	pt.x = (pt.x - m_nOffsetChar) * GetCharWidth() + GetMarginWidth();
	return pt;
}

void TextView::InvalidateLines(int nLine1, int nLine2, bool bInvalidateMargin /*= false*/)
{
	bInvalidateMargin = true;

	CRect rcInvalid;
	GetClientRect(&rcInvalid);

	if (nLine2 == -1)
	{
		if (!bInvalidateMargin)
			rcInvalid.left += GetMarginWidth();

		rcInvalid.top = (nLine1 - m_nTopLine) * GetLineHeight();
	}
	else
	{
		if (nLine2 < nLine1)
		{
			std::swap(nLine1, nLine2);
		}

		if (!bInvalidateMargin)
			rcInvalid.left += GetMarginWidth();

		rcInvalid.top = (nLine1 - m_nTopLine) * GetLineHeight();
		rcInvalid.bottom = (nLine2 - m_nTopLine + 1) * GetLineHeight();
	}

	InvalidateRect(rcInvalid, false);
}

void TextView::SetSelection(const TextSelection &sel)
{
	InvalidateLines(sel._start.y, sel._end.y);
	InvalidateLines(_selection._start.y, _selection._end.y);

	_selection = sel;
}

void TextView::OnSetFocus(CWindow oldWnd)
{
	m_bFocused = true;
	if (_selection._start != _selection._end)
		InvalidateLines(_selection._start.y, _selection._end.y);
	UpdateCaret();
}


int TextView::CalculateActualOffset(int lineIndex, int nCharIndex)
{
	const auto &line = _buffer[lineIndex];

	int nOffset = 0;
	int tabSize = GetTabSize();

	for (int i = 0; i < nCharIndex; i++)
	{
		if (line[i] == _T('\t'))
			nOffset += (tabSize - nOffset % tabSize);
		else
			nOffset++;
	}
	return nOffset;
}

int TextView::ApproxActualOffset(int lineIndex, int nOffset)
{
	if (nOffset == 0)
		return 0;

	const auto &line = _buffer[lineIndex];
	const auto nLength = line.size();

	int nCurrentOffset = 0;
	int tabSize = GetTabSize();

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

void TextView::EnsureVisible(TextLocation pt)
{
	//	Scroll vertically
	int nLineCount = _buffer.LineCount();
	int nNewTopLine = m_nTopLine;
	if (pt.y >= nNewTopLine + GetScreenLines())
	{
		nNewTopLine = pt.y - GetScreenLines() + 1;
	}
	if (pt.y < nNewTopLine)
	{
		nNewTopLine = pt.y;
	}

	if (nNewTopLine < 0)
		nNewTopLine = 0;
	if (nNewTopLine >= nLineCount)
		nNewTopLine = nLineCount - 1;

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

	if (nNewOffset >= GetMaxLineLength())
		nNewOffset = GetMaxLineLength() - 1;
	if (nNewOffset < 0)
		nNewOffset = 0;

	if (m_nOffsetChar != nNewOffset)
	{
		ScrollToChar(nNewOffset);
		UpdateCaret();
	}
}

void TextView::OnKillFocus(CWindow newWnd)
{
	m_bFocused = false;
	UpdateCaret();
	if (_selection._start != _selection._end)
		InvalidateLines(_selection._start.y, _selection._end.y);
	if (m_bDragSelection)
	{
		ReleaseCapture();
		KillTimer(m_nDragSelTimer);
		m_bDragSelection = false;
	}
}

void TextView::OnSysColorChange()
{
	Invalidate();
}

std::vector<std::wstring> TextView::Text(const TextSelection &selection) const
{
	return _buffer.Text(selection);
}

void TextView::InvalidateLine(int index)
{
	if (_parseCookies.size() > index) _parseCookies[index] = (DWORD) -1;
	if (_actualLineLengths.size() > index) _actualLineLengths[index] = 0;

	InvalidateLines(index, index + 1, true);

	int nActualLength = GetLineActualLength(index);

	if (m_nMaxLineLength < nActualLength)
		m_nMaxLineLength = nActualLength;

	RecalcHorzScrollBar();
}

void TextView::InvalidateView()
{
	ResetView();
	RecalcVertScrollBar();
	RecalcHorzScrollBar();
	Invalidate(false);
}

HINSTANCE TextView::GetResourceHandle()
{
	return _AtlBaseModule.GetResourceInstance();
}

int TextView::OnCreate()
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
	TRACE0("Warning: Unable to register drop target for TextView.\n");
	delete _dropTarget;
	_dropTarget = nullptr;
	}*/

	return 0;
}

void TextView::SetAnchor(const TextLocation &ptNewAnchor)
{
	m_ptAnchor = ptNewAnchor;
}

void TextView::SetCursorPos(const TextLocation &ptCursorPos)
{
	m_ptCursorPos = ptCursorPos;
	m_nIdealCharPos = CalculateActualOffset(m_ptCursorPos.y, m_ptCursorPos.x);
	UpdateCaret();
}

void TextView::SetSelectionMargin(bool bSelMargin)
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

void TextView::ShowCursor()
{
	m_bCursorHidden = false;
	UpdateCaret();
}

void TextView::HideCursor()
{
	m_bCursorHidden = true;
	UpdateCaret();
}

HGLOBAL TextView::PrepareDragData()
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



bool TextView::HighlightText(const TextLocation &ptStartPos, int nLength)
{
	m_ptCursorPos = ptStartPos;
	m_ptCursorPos.x += nLength;
	m_ptAnchor = m_ptCursorPos;
	SetSelection(TextSelection(ptStartPos, m_ptCursorPos));
	UpdateCaret();
	EnsureVisible(m_ptCursorPos);
	return true;
}


void TextView::Find(const std::wstring &text, DWORD flags)
{
	TextLocation loc;

	if (_buffer.Find(text, m_ptCursorPos, flags, true, &loc))
	{
		auto end = loc;
		end.x += text.size();
		Select(TextSelection(loc, end));
		m_ptCursorPos = loc;
		UpdateCaret();

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
	//		TextLocation ptSelStart, ptSelEnd;
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

void TextView::OnEditRepeat()
{
	//if (m_bLastSearch)
	//{
	//	TextLocation ptFoundPos;
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



void TextView::OnEditFindPrevious()
{
	DWORD dwSaveSearchFlags = m_dwLastSearchFlags;
	if ((m_dwLastSearchFlags & FIND_DIRECTION_UP) != 0)
		m_dwLastSearchFlags &= ~FIND_DIRECTION_UP;
	else
		m_dwLastSearchFlags |= FIND_DIRECTION_UP;
	OnEditRepeat();
	m_dwLastSearchFlags = dwSaveSearchFlags;
}

void TextView::OnFilePageSetup()
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

bool TextView::GetViewTabs() const
{
	return m_bViewTabs;
}

void TextView::SetViewTabs(bool bViewTabs)
{
	if (bViewTabs != m_bViewTabs)
	{
		m_bViewTabs = bViewTabs;
		if (::IsWindow(m_hWnd))
			Invalidate();
	}
}

bool TextView::GetSelectionMargin() const
{
	return m_bSelMargin;
}

int TextView::GetMarginWidth() const
{
	return m_bSelMargin ? 20 : 1;
}

bool TextView::GetDisableDragAndDrop() const
{
	return m_bDisableDragAndDrop;
}

void TextView::SetDisableDragAndDrop(bool bDDAD)
{
	m_bDisableDragAndDrop = bDDAD;
}

void TextView::MoveLeft(bool select)
{
	PrepareSelBounds();

	if (m_ptDrawSel._start != m_ptDrawSel._end && !select)
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
	UpdateCaret();
	if (!select)
		m_ptAnchor = m_ptCursorPos;
	SetSelection(TextSelection(m_ptAnchor, m_ptCursorPos));
}

void TextView::MoveRight(bool select)
{
	PrepareSelBounds();
	if (m_ptDrawSel._start != m_ptDrawSel._end && !select)
	{
		m_ptCursorPos = m_ptDrawSel._end;
	}
	else
	{
		if (m_ptCursorPos.x == _buffer[m_ptCursorPos.y].size())
		{
			if (m_ptCursorPos.y < _buffer.LineCount() - 1)
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
	UpdateCaret();
	if (!select)
		m_ptAnchor = m_ptCursorPos;
	SetSelection(TextSelection(m_ptAnchor, m_ptCursorPos));
}

void TextView::MoveWordLeft(bool select)
{
	PrepareSelBounds();

	if (m_ptDrawSel._start != m_ptDrawSel._end && !select)
	{
		MoveLeft(select);
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
	UpdateCaret();
	if (!select)
		m_ptAnchor = m_ptCursorPos;
	SetSelection(TextSelection(m_ptAnchor, m_ptCursorPos));
}

void TextView::MoveWordRight(bool select)
{
	PrepareSelBounds();
	if (m_ptDrawSel._start != m_ptDrawSel._end && !select)
	{
		MoveRight(select);
		return;
	}

	if (m_ptCursorPos.x == _buffer[m_ptCursorPos.y].size())
	{
		if (m_ptCursorPos.y == _buffer.LineCount() - 1)
			return;
		m_ptCursorPos.y++;
		m_ptCursorPos.x = 0;
	}

	auto nLength = _buffer[m_ptCursorPos.y].size();

	if (m_ptCursorPos.x == nLength)
	{
		MoveRight(select);
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
	UpdateCaret();
	if (!select)
		m_ptAnchor = m_ptCursorPos;
	SetSelection(TextSelection(m_ptAnchor, m_ptCursorPos));
}

void TextView::MoveUp(bool select)
{
	PrepareSelBounds();
	if (m_ptDrawSel._start != m_ptDrawSel._end && !select)
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
	UpdateCaret();
	if (!select)
		m_ptAnchor = m_ptCursorPos;

	SetSelection(TextSelection(m_ptAnchor, m_ptCursorPos));
}

void TextView::MoveDown(bool select)
{
	PrepareSelBounds();
	if (m_ptDrawSel._start != m_ptDrawSel._end && !select)
		m_ptCursorPos = m_ptDrawSel._end;

	if (m_ptCursorPos.y < _buffer.LineCount() - 1)
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
	UpdateCaret();
	if (!select)
		m_ptAnchor = m_ptCursorPos;
	SetSelection(TextSelection(m_ptAnchor, m_ptCursorPos));
}

void TextView::MoveHome(bool select)
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
	UpdateCaret();
	if (!select)
		m_ptAnchor = m_ptCursorPos;
	SetSelection(TextSelection(m_ptAnchor, m_ptCursorPos));
}

void TextView::MoveEnd(bool select)
{
	m_ptCursorPos.x = _buffer[m_ptCursorPos.y].size();
	m_nIdealCharPos = CalculateActualOffset(m_ptCursorPos.y, m_ptCursorPos.x);
	EnsureVisible(m_ptCursorPos);
	UpdateCaret();
	if (!select)
		m_ptAnchor = m_ptCursorPos;
	SetSelection(TextSelection(m_ptAnchor, m_ptCursorPos));
}

void TextView::MovePgUp(bool select)
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
	UpdateCaret();
	if (!select)
		m_ptAnchor = m_ptCursorPos;
	SetSelection(TextSelection(m_ptAnchor, m_ptCursorPos));
}

void TextView::MovePgDn(bool select)
{
	int nNewTopLine = m_nTopLine + GetScreenLines() - 1;
	if (nNewTopLine >= _buffer.LineCount())
		nNewTopLine = _buffer.LineCount() - 1;
	if (m_nTopLine != nNewTopLine)
	{
		ScrollToLine(nNewTopLine);
	}

	m_ptCursorPos.y += GetScreenLines() - 1;
	if (m_ptCursorPos.y >= _buffer.LineCount())
		m_ptCursorPos.y = _buffer.LineCount() - 1;

	auto size = _buffer[m_ptCursorPos.y].size();

	if (m_ptCursorPos.x > size)
		m_ptCursorPos.x = size;
	m_nIdealCharPos = CalculateActualOffset(m_ptCursorPos.y, m_ptCursorPos.x);
	EnsureVisible(m_ptCursorPos);	//todo: no vertical scroll
	UpdateCaret();
	if (!select)
		m_ptAnchor = m_ptCursorPos;
	SetSelection(TextSelection(m_ptAnchor, m_ptCursorPos));
}

void TextView::MoveCtrlHome(bool select)
{
	m_ptCursorPos.x = 0;
	m_ptCursorPos.y = 0;
	m_nIdealCharPos = CalculateActualOffset(m_ptCursorPos.y, m_ptCursorPos.x);
	EnsureVisible(m_ptCursorPos);
	UpdateCaret();
	if (!select)
		m_ptAnchor = m_ptCursorPos;
	SetSelection(TextSelection(m_ptAnchor, m_ptCursorPos));
}

void TextView::MoveCtrlEnd(bool select)
{
	m_ptCursorPos.y = _buffer.LineCount() - 1;
	m_ptCursorPos.x = _buffer[m_ptCursorPos.y].size();
	m_nIdealCharPos = CalculateActualOffset(m_ptCursorPos.y, m_ptCursorPos.x);
	EnsureVisible(m_ptCursorPos);
	UpdateCaret();
	if (!select)
		m_ptAnchor = m_ptCursorPos;
	SetSelection(TextSelection(m_ptAnchor, m_ptCursorPos));
}

void TextView::ScrollUp()
{
	if (m_nTopLine > 0)
	{
		ScrollToLine(m_nTopLine - 1);
	}
}

void TextView::ScrollDown()
{
	if (m_nTopLine < _buffer.LineCount() - 1)
	{
		ScrollToLine(m_nTopLine + 1);
	}
}

void TextView::ScrollLeft()
{
	if (m_nOffsetChar > 0)
	{
		ScrollToChar(m_nOffsetChar - 1);
		UpdateCaret();
	}
}

void TextView::ScrollRight()
{
	if (m_nOffsetChar < GetMaxLineLength() - 1)
	{
		ScrollToChar(m_nOffsetChar + 1);
		UpdateCaret();
	}
}

TextLocation TextView::WordToRight(TextLocation pt) const 
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

TextLocation TextView::WordToLeft(TextLocation pt) const
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

void TextView::SelectAll()
{
	int nLineCount = _buffer.LineCount();
	m_ptCursorPos.x = _buffer[nLineCount - 1].size();
	m_ptCursorPos.y = nLineCount - 1;
	SetSelection(TextSelection(TextLocation(0, 0), m_ptCursorPos));
	UpdateCaret();
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

void TextView::OnContextMenu(const CPoint &point, UINT nFlags)
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
		m_ptAnchor = m_ptCursorPos = ClientToText(clientLocation);

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

		if (_buffer.CanUndo())
		{
			AppendMenu(menu, MF_ENABLED, ID_EDIT_UNDO, L"Undo");
			AppendMenu(menu, MF_SEPARATOR, 0, nullptr);
		}

		AppendMenu(menu, MF_ENABLED, ID_EDIT_CUT, L"Cut");
		AppendMenu(menu, MF_ENABLED, ID_EDIT_COPY, L"Copy");
		AppendMenu(menu, MF_ENABLED, ID_EDIT_PASTE, L"Paste");
		AppendMenu(menu, MF_ENABLED, ID_EDIT_DELETE, L"Delete");
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
			InvalidateLine(selection._start.y);
			break;

		default:

			if (replacements.find(result) != replacements.end())
			{
				_buffer.DeleteText(selection);
				_buffer.InsertText(selection._start, replacements[result]);

				auto selection = WordSelection();
				Select(WordSelection());
			}
			break;
		}
	}
}

void TextView::OnLButtonDown(const CPoint &point, UINT nFlags)
{
	bool bShift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
	bool bControl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;

	SetFocus();

	if (point.x < GetMarginWidth())
	{
		if (bControl)
		{
			SelectAll();
		}
		else
		{
			m_ptCursorPos = ClientToText(point);
			m_ptCursorPos.x = 0;				//	Force beginning of the line
			if (!bShift)
				m_ptAnchor = m_ptCursorPos;

			TextLocation ptStart, ptEnd;
			ptStart = m_ptAnchor;
			if (ptStart.y == _buffer.LineCount() - 1)
				ptStart.x = _buffer[ptStart.y].size();
			else
			{
				ptStart.y++;
				ptStart.x = 0;
			}

			ptEnd = m_ptCursorPos;
			ptEnd.x = 0;

			m_ptCursorPos = ptEnd;
			UpdateCaret();
			EnsureVisible(m_ptCursorPos);
			SetSelection(TextSelection(ptStart, ptEnd));

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
		TextLocation ptText = ClientToText(point);
		PrepareSelBounds();

		if ((IsInsideSelBlock(ptText)) &&				// If Inside Selection Area
			(!m_bDisableDragAndDrop))				// And D&D Not Disabled
		{
			m_bPreparingToDrag = true;
		}
		else
		{
			m_ptCursorPos = ClientToText(point);
			if (!bShift)
				m_ptAnchor = m_ptCursorPos;

			auto selection = bControl ? WordSelection() : TextSelection(m_ptAnchor, m_ptCursorPos);

			m_ptCursorPos = selection._end;
			UpdateCaret();
			EnsureVisible(m_ptCursorPos);
			SetSelection(selection);

			SetCapture();
			m_nDragSelTimer = SetTimer(RETHINKIFY_TIMER_DRAGSEL, 100, nullptr);
			assert(m_nDragSelTimer != 0);
			m_bWordSelection = bControl;
			m_bLineSelection = false;
			m_bDragSelection = true;
		}
	}
}



void TextView::OnMouseWheel(const CPoint &point, int zDelta)
{
	ScrollToLine(Clamp(m_nTopLine + zDelta, 0, _buffer.LineCount()));
}

void TextView::OnMouseMove(const CPoint &point, UINT nFlags)
{
	if (m_bDragSelection)
	{
		bool bOnMargin = point.x < GetMarginWidth();
		TextLocation ptNewCursorPos = ClientToText(point);
		TextLocation ptStart, ptEnd;

		if (m_bLineSelection)
		{
			if (bOnMargin)
			{
				if (ptNewCursorPos.y < m_ptAnchor.y ||
					ptNewCursorPos.y == m_ptAnchor.y && ptNewCursorPos.x < m_ptAnchor.x)
				{
					ptEnd = m_ptAnchor;
					if (ptEnd.y == _buffer.LineCount() - 1)
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
					if (ptNewCursorPos.y == _buffer.LineCount() - 1)
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
				UpdateCaret();
				SetSelection(TextSelection(ptNewCursorPos, ptEnd));
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
		UpdateCaret();
		SetSelection(TextSelection(ptStart, ptEnd));
	}

	if (m_bPreparingToDrag)
	{
		m_bPreparingToDrag = false;
		HGLOBAL hData = PrepareDragData();
		if (hData != nullptr)
		{
			UndoGroup ug(_buffer);


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

void TextView::OnLButtonUp(const CPoint &point, UINT nFlags)
{
	if (m_bDragSelection)
	{
		TextLocation ptNewCursorPos = ClientToText(point);

		if (m_bLineSelection)
		{
			TextLocation ptStart, ptEnd;

			if (ptNewCursorPos.y < m_ptAnchor.y ||
				ptNewCursorPos.y == m_ptAnchor.y && ptNewCursorPos.x < m_ptAnchor.x)
			{
				ptEnd = m_ptAnchor;
				if (ptEnd.y == _buffer.LineCount() - 1)
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
				if (ptNewCursorPos.y == _buffer.LineCount() - 1)
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
			UpdateCaret();
			SetSelection(TextSelection(ptNewCursorPos, ptEnd));
		}
		else
		{
			auto selection = m_bWordSelection ? WordSelection() : TextSelection(m_ptAnchor, m_ptCursorPos);
			m_ptCursorPos = selection._end;
			EnsureVisible(m_ptCursorPos);
			UpdateCaret();
			SetSelection(selection);
		}

		ReleaseCapture();
		KillTimer(m_nDragSelTimer);
		m_bDragSelection = false;
	}

	if (m_bPreparingToDrag)
	{
		m_bPreparingToDrag = false;
		m_ptCursorPos = ClientToText(point);
		EnsureVisible(m_ptCursorPos);
		UpdateCaret();
		SetSelection(TextSelection(m_ptCursorPos, m_ptCursorPos));
	}
}

void TextView::OnTimer(UINT nIDEvent)
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
		int nNewTopLine = m_nTopLine;
		int nLineCount = _buffer.LineCount();
		if (pt.y < rcClient.top)
		{
			nNewTopLine--;
			if (pt.y < rcClient.top - GetLineHeight())
				nNewTopLine -= 2;
		}
		else
			if (pt.y >= rcClient.bottom)
			{
			nNewTopLine++;
			if (pt.y >= rcClient.bottom + GetLineHeight())
				nNewTopLine += 2;
			}

		if (nNewTopLine < 0)
			nNewTopLine = 0;
		if (nNewTopLine >= nLineCount)
			nNewTopLine = nLineCount - 1;

		if (m_nTopLine != nNewTopLine)
		{
			ScrollToLine(nNewTopLine);
			bChanged = true;
		}

		//	Scroll horizontally, if necessary
		int nNewOffsetChar = m_nOffsetChar;
		int nMaxLineLength = GetMaxLineLength();
		if (pt.x < rcClient.left)
			nNewOffsetChar--;
		else
			if (pt.x >= rcClient.right)
				nNewOffsetChar++;

		if (nNewOffsetChar >= nMaxLineLength)
			nNewOffsetChar = nMaxLineLength - 1;
		if (nNewOffsetChar < 0)
			nNewOffsetChar = 0;

		if (m_nOffsetChar != nNewOffsetChar)
		{
			ScrollToChar(nNewOffsetChar);
			UpdateCaret();
			bChanged = true;
		}

		//	Fix changes
		if (bChanged)
		{
			TextLocation ptNewCursorPos = ClientToText(pt);

			if (ptNewCursorPos != m_ptCursorPos)
			{
				m_ptCursorPos = ptNewCursorPos;
				UpdateCaret();
			}
			SetSelection(TextSelection(m_ptAnchor, m_ptCursorPos));
		}
	}
}

void TextView::OnLButtonDblClk(const CPoint &point, UINT nFlags)
{
	if (!m_bDragSelection)
	{
		m_ptAnchor = m_ptCursorPos = ClientToText(point);

		auto selection = WordSelection();

		m_ptCursorPos = selection._end;
		UpdateCaret();
		EnsureVisible(m_ptCursorPos);
		SetSelection(selection);

		SetCapture();
		m_nDragSelTimer = SetTimer(RETHINKIFY_TIMER_DRAGSEL, 100, nullptr);
		assert(m_nDragSelTimer != 0);
		m_bWordSelection = true;
		m_bLineSelection = false;
		m_bDragSelection = true;
	}
}


void TextView::OnRButtonDown(const CPoint &point, UINT nFlags)
{
	auto pt = ClientToText(point);

	if (!IsInsideSelBlock(pt))
	{
		m_ptAnchor = m_ptCursorPos = pt;
		SetSelection(TextSelection(m_ptCursorPos, m_ptCursorPos));
		EnsureVisible(m_ptCursorPos);
		UpdateCaret();
	}
}

void TextView::Copy()
{
	if (_selection._start == _selection._end)
		return;

	PrepareSelBounds();
	PutToClipboard(Combine(Text(m_ptDrawSel)));
}

bool TextView::TextInClipboard()
{
	return IsClipboardFormatAvailable(CF_UNICODETEXT) != 0;
}

bool TextView::PutToClipboard(const std::wstring &text)
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

std::wstring TextView::GetFromClipboard() const
{
	std::wstring result;
	auto pThis = const_cast<TextView*>(this);

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

bool TextView::QueryEditable()
{
	return true;
}

void TextView::Paste()
{
	if (QueryEditable())
	{
		auto text = GetFromClipboard();

		if (!text.empty())
		{
			UndoGroup ug(_buffer);
			auto pos = _buffer.DeleteText(ug, GetSelection());
			Locate(_buffer.InsertText(ug, pos, text));
		}
	}
}

void TextView::Cut()
{
	if (QueryEditable() && HasSelection())
	{
		auto sel = GetSelection();
		PutToClipboard(Combine(Text(sel)));

		UndoGroup ug(_buffer);
		Locate(_buffer.DeleteText(ug, sel));
	}
}

void TextView::OnEditDelete()
{
	if (QueryEditable())
	{
		auto sel = GetSelection();

		if (sel.empty())
		{
			if (sel._end.x == _buffer[sel._end.y].size())
			{
				if (sel._end.y == _buffer.LineCount() - 1)
					return;

				sel._end.y++;
				sel._end.x = 0;
			}
			else
			{
				sel._end.x++;
			}
		}

		UndoGroup ug(_buffer);
		Locate(_buffer.DeleteText(ug, sel));
	}
}

void TextView::OnChar(UINT nChar, UINT nRepCnt, UINT nFlags)
{
	if ((::GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0 ||
		(::GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0)
		return;

	bool bTranslated = false;

	if (nChar == VK_RETURN)
	{
		if (QueryEditable())
		{
			UndoGroup ug(_buffer);
			auto pos = _buffer.DeleteText(ug, GetSelection());
			Locate(_buffer.InsertText(ug, pos, L'\n'));
		}

		return;
	}

	if (nChar > 31)
	{
		if (QueryEditable())
		{
			UndoGroup ug(_buffer);
			auto pos = _buffer.DeleteText(ug, GetSelection());
			Locate(_buffer.InsertText(ug, pos, nChar));
		}
	}
}

void TextView::OnEditDeleteBack()
{
	if (QueryEditable())
	{
		if (HasSelection())
		{
			OnEditDelete();
		}
		else
		{
			UndoGroup ug(_buffer);
			Locate(_buffer.DeleteText(ug, GetCursorPos()));
		}
	}
}

void TextView::OnEditTab()
{
	if (QueryEditable())
	{
		auto sel = GetSelection();

		if (sel._end.y > sel._start.y)
		{
			UndoGroup ug(_buffer);

			int nStartLine = sel._start.y;
			int nEndLine = sel._end.y;
			sel._start.x = 0;

			if (sel._end.x > 0)
			{
				if (sel._end.y == _buffer.LineCount() - 1)
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

			SetSelection(sel);
			SetCursorPos(sel._end);
			EnsureVisible(sel._end);

			static const TCHAR pszText [] = _T("\t");

			for (int i = nStartLine; i <= nEndLine; i++)
			{
				_buffer.InsertText(ug, TextLocation(0, i), pszText);
			}

			RecalcHorzScrollBar();
		}
		else
		{
			UndoGroup ug(_buffer);
			auto pos = _buffer.DeleteText(ug, GetSelection());
			Locate(_buffer.InsertText(ug, pos, L'\t'));
		}
	}
}

void TextView::OnEditUntab()
{
	if (QueryEditable())
	{
		auto sel = GetSelection();

		if (sel._end.y > sel._start.y)
		{
			UndoGroup ug(_buffer);

			int nStartLine = sel._start.y;
			int nEndLine = sel._end.y;
			sel._start.x = 0;
			if (sel._end.x > 0)
			{
				if (sel._end.y == _buffer.LineCount() - 1)
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
			SetSelection(sel);
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
							if (++nOffset >= GetTabSize())
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
						_buffer.DeleteText(ug, TextSelection(0, i, nPos, i));
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
				int tabSize = GetTabSize();
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

void TextView::DoDragScroll(const CPoint &point)
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
	if (point.x < rcClientRect.left + GetMarginWidth() + DRAG_BORDER_X)
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

bool TextView::DoDropText(COleDataObject *pDataObject, const CPoint &ptClient)
{
	//HGLOBAL hData = pDataObject->GetGlobalData(CF_UNICODETEXT);
	//if (hData == nullptr)
	//	return false;

	//TextLocation ptDropPos = ClientToText(ptClient);
	//if (IsDraggingText() && IsInsideSelection(ptDropPos))
	//{
	//	SetAnchor(ptDropPos);
	//	SetSelection(ptDropPos, ptDropPos);
	//	SetCursorPos(ptDropPos);
	//	EnsureVisible(ptDropPos);
	//	return false;
	//}

	//auto pszText = (const char *) ::GlobalLock(hData);
	//if (pszText == nullptr)
	//	return false;

	//int x, y;
	//_buffer.InsertText(this, ptDropPos.y, ptDropPos.x, A2T(pszText), y, x, CE_ACTION_DRAGDROP);
	//TextLocation ptCurPos(x, y);
	//SetAnchor(ptDropPos);
	//SetSelection(ptDropPos, ptCurPos);
	//SetCursorPos(ptCurPos);
	//EnsureVisible(ptCurPos);

	//::GlobalUnlock(hData);
	return true;
}


void TextView::ShowDropIndicator(const CPoint &point)
{
	if (!m_bDropPosVisible)
	{
		HideCursor();
		m_ptSavedCaretPos = GetCursorPos();
		m_bDropPosVisible = true;
		::CreateCaret(m_hWnd, (HBITMAP) 1, 2, GetLineHeight());
	}
	m_ptDropPos = ClientToText(point);
	if (m_ptDropPos.x >= m_nOffsetChar)
	{
		auto pt = TextToClient(m_ptDropPos);
		SetCaretPos(pt.x, pt.y);
		ShowCaret();
	}
	else
	{
		HideCaret();
	}
}

void TextView::HideDropIndicator()
{
	if (m_bDropPosVisible)
	{
		SetCursorPos(m_ptSavedCaretPos);
		ShowCursor();
		m_bDropPosVisible = false;
	}
}

DROPEFFECT TextView::GetDropEffect()
{
	return DROPEFFECT_COPY | DROPEFFECT_MOVE;
}

void TextView::OnDropSource(DROPEFFECT de)
{
	if (m_bDraggingText && de == DROPEFFECT_MOVE)
	{
		UndoGroup ug(_buffer);
		_buffer.DeleteText(ug, m_ptDraggedText);
	}
}


void TextView::OnEditReplace()
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
	//	SetSelection(m_ptSavedSelStart, m_ptSavedSelEnd);
	//	m_bSelectionPushed = false;
	//}

	////	Save search parameters to registry
	//pApp->WriteProfileInt(REG_REPLACE_SUBKEY, REG_MATCH_CASE, dlg.m_bMatchCase);
	//pApp->WriteProfileInt(REG_REPLACE_SUBKEY, REG_WHOLE_WORD, dlg.m_bWholeWord);
	//pApp->WriteProfileString(REG_REPLACE_SUBKEY, REG_FIND_WHAT, dlg.m_sText);
	//pApp->WriteProfileString(REG_REPLACE_SUBKEY, REG_REPLACE_WITH, dlg.m_sNewText);
}

bool TextView::ReplaceSelection(const wchar_t * pszNewText)
{
	//assert(pszNewText != nullptr);
	//if (! HasSelection())
	//	return false;

	//DeleteCurrentSelection();

	//TextLocation ptCursorPos = GetCursorPos();
	//int x, y;
	//_buffer.InsertText(this, ptCursorPos.y, ptCursorPos.x, pszNewText, y, x, CE_ACTION_REPLACE);
	//TextLocation ptEndOfBlock = TextLocation(x, y);
	//SetAnchor(ptEndOfBlock);
	//SetSelection(ptCursorPos, ptEndOfBlock);
	//SetCursorPos(ptEndOfBlock);
	//EnsureVisible(ptEndOfBlock);
	return true;
}



void TextView::OnEditUndo()
{
	if (_buffer.CanUndo())
	{
		Locate(_buffer.Undo());
	}
}


void TextView::OnEditRedo()
{
	if (_buffer.CanRedo())
	{
		Locate(_buffer.Redo());
	}
}



//void TextView::OnEditOperation(int nAction, const std::wstring &text)
//{
//	if (m_bAutoIndent)
//	{
//		//	Analyse last action...
//		if (nAction == CE_ACTION_TYPING && _tcscmp(text.c_str(), _T("\r\n")) == 0 && !_overtype)
//		{
//			//	Enter stroke!
//			TextLocation ptCursorPos = GetCursorPos();
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
//				//	Insert part of the previos line
//				TCHAR *pszInsertStr = (TCHAR *) _alloca(sizeof(TCHAR) * (len + 1));
//				_tcsncpy_s(pszInsertStr, (len + 1), line.c_str(), nPos);
//				pszInsertStr[nPos] = 0;
//
//				int x, y;
//				_buffer.InsertText(ug, ptCursorPos.y, ptCursorPos.x, pszInsertStr, y, x, CE_ACTION_AUTOINDENT);
//
//				TextLocation pt(x, y);
//				SetCursorPos(pt);
//				SetSelection(pt, pt);
//				SetAnchor(pt);
//				EnsureVisible(pt);
//			}
//		}
//	}
//}

bool TextView::GetOverwriteMode() const
{
	return _overtype;
}

void TextView::SetOverwriteMode(bool bOvrMode /*= true*/)
{
	_overtype = bOvrMode;
}

bool TextView::GetAutoIndent() const
{
	return m_bAutoIndent;
}

void TextView::SetAutoIndent(bool bAutoIndent)
{
	m_bAutoIndent = bAutoIndent;
}

void TextView::EnableMenuItems(HMENU hMenu)
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
		case ID_EDIT_REDO: enable = _buffer.CanRedo(); break;
		case ID_EDIT_REPEAT: enable = m_bLastSearch; break;
		case ID_EDIT_SELECT_ALL: enable = true; break;
		case ID_EDIT_UNDO: enable = _buffer.CanUndo(); break;
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

void TextView::HighlightFromExtension(const wchar_t *ext)
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