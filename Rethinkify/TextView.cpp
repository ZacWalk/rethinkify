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

	DROPEFFECT OnDragEnter(CWindow* pWnd, COleDataObject* pDataObject, DWORD dwKeyState, CPoint point);
	void OnDragLeave(CWindow* pWnd);
	DROPEFFECT OnDragOver(CWindow* pWnd, COleDataObject* pDataObject, DWORD dwKeyState, CPoint point);
	bool OnDrop(CWindow* pWnd, COleDataObject* pDataObject, DROPEFFECT dropEffect, CPoint point);
	DROPEFFECT OnDragScroll(CWindow* pWnd, DWORD dwKeyState, CPoint point);
};


TextView::TextView(TextBuffer &buffer) : _buffer(buffer)
{
	m_hAccel = nullptr;
	//m_pDropTarget = nullptr;
	m_pCacheBitmap = nullptr;

	memset(m_apFonts, 0, sizeof(HFONT) * 4);

	m_bSelMargin = TRUE;
	_buffer.AddView(this);

	ResetView();
}

TextView::~TextView()
{
	assert(m_hAccel == nullptr);
	assert(m_pCacheBitmap == nullptr);
}

void TextView::GetSelection(CPoint &ptStart, CPoint &ptEnd)
{
	PrepareSelBounds();
	ptStart = m_ptDrawSelStart;
	ptEnd = m_ptDrawSelEnd;
}

int TextView::GetLineActualLength(int nLineIndex) const
{
	if (_actualLineLengths.size() != _buffer.LineCount())
	{
		_actualLineLengths.clear();
		_actualLineLengths.insert(_actualLineLengths.begin(), _buffer.LineCount(), 0);
	}

	if (_actualLineLengths[nLineIndex] == 0)
	{
		int nActualLength = 0;
		const auto &line = _buffer[nLineIndex];

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

		_actualLineLengths[nLineIndex] = nActualLength;
	}

	return _actualLineLengths[nLineIndex];
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
		ScrollWindow(nScrollChars * GetCharWidth(), 0, &rcScroll, &rcScroll);
		UpdateWindow();
		if (bTrackScrollBar)
			RecalcHorzScrollBar(TRUE);
	}
}

void TextView::ScrollToLine(int nNewTopLine, bool bTrackScrollBar)
{
	if (m_nTopLine != nNewTopLine)
	{
		int nScrollLines = m_nTopLine - nNewTopLine;
		m_nTopLine = nNewTopLine;
		ScrollWindow(0, nScrollLines * GetLineHeight());
		UpdateWindow();
		if (bTrackScrollBar)
			RecalcVertScrollBar(TRUE);
	}
}

std::wstring TextView::ExpandChars(const std::wstring &text, int nOffset, int nCount)
{
	std::wstring result;

	if (nCount > 0)
	{
		auto pszChars = text.c_str();
		int tabSize = GetTabSize();
		int nActualOffset = 0;
		int I = 0;

		for (I = 0; I < nOffset; I++)
		{
			if (pszChars[I] == _T('\t'))
				nActualOffset += (tabSize - nActualOffset % tabSize);
			else
				nActualOffset++;
		}

		pszChars += nOffset;
		int nLength = nCount;

		int nTabCount = 0;
		for (I = 0; I < nLength; I++)
		{
			if (pszChars[I] == _T('\t'))
				nTabCount++;
		}

		int nCurPos = 0;

		if (nTabCount > 0 || m_bViewTabs)
		{
			for (I = 0; I < nLength; I++)
			{
				if (pszChars[I] == _T('\t'))
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
					result += (pszChars[I] == _T(' ') && m_bViewTabs) ? SPACE_CHARACTER : pszChars[I];
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

void TextView::DrawLineHelperImpl(HDC pdc, CPoint &ptOrigin, const CRect &rcClip, LPCTSTR pszChars, int nOffset, int nCount)
{
	assert(nCount >= 0);
	if (nCount > 0)
	{
		auto line = ExpandChars(pszChars, nOffset, nCount);
		int nWidth = rcClip.right - ptOrigin.x;

		if (nWidth > 0)
		{
			int nCharWidth = GetCharWidth();
			int nCount = line.size();
			int nCountFit = nWidth / nCharWidth + 1;
			if (nCount > nCountFit)
				nCount = nCountFit;
#ifdef _DEBUG
			//CSize sz = pdc->GetTextExtent(line, nCount);
			//assert(sz.cx == m_nCharWidth * nCount);
#endif
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

void TextView::DrawLineHelper(HDC pdc, CPoint &ptOrigin, const CRect &rcClip, int nColorIndex, LPCTSTR pszChars, int nOffset, int nCount, CPoint ptTextPos)
{
	if (nCount > 0)
	{
		if (m_bFocused || m_bShowInactiveSelection)
		{
			int nSelBegin = 0, nSelEnd = 0;
			if (m_ptDrawSelStart.y > ptTextPos.y)
			{
				nSelBegin = nCount;
			}
			else
				if (m_ptDrawSelStart.y == ptTextPos.y)
				{
				nSelBegin = m_ptDrawSelStart.x - ptTextPos.x;
				if (nSelBegin < 0)
					nSelBegin = 0;
				if (nSelBegin > nCount)
					nSelBegin = nCount;
				}
			if (m_ptDrawSelEnd.y > ptTextPos.y)
			{
				nSelEnd = nCount;
			}
			else
				if (m_ptDrawSelEnd.y == ptTextPos.y)
				{
				nSelEnd = m_ptDrawSelEnd.x - ptTextPos.x;
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
				COLORREF crOldBk = SetBkColor(pdc, GetColor(COLORINDEX_SELBKGND));
				COLORREF crOldText = SetTextColor(pdc, GetColor(COLORINDEX_SELTEXT));
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

void TextView::GetLineColors(int nLineIndex, COLORREF &crBkgnd, COLORREF &crText, bool &bDrawWhitespace)
{
	bDrawWhitespace = TRUE;
	crText = RGB(255, 255, 255);
	crBkgnd = CLR_NONE;
	crText = CLR_NONE;
	bDrawWhitespace = FALSE;
}

DWORD TextView::GetParseCookie(int nLineIndex)
{
	const auto invalid = (DWORD) -1;
	int nLineCount = _buffer.LineCount();

	if (_parseCookies.empty())
	{
		_parseCookies.insert(_parseCookies.begin(), nLineCount, invalid);
	}

	if (nLineIndex < 0)
		return 0;

	if (_parseCookies[nLineIndex] != invalid)
		return _parseCookies[nLineIndex];

	int L = nLineIndex;
	while (L >= 0 && _parseCookies[L] == invalid)
		L--;
	L++;

	int nBlocks;
	while (L <= nLineIndex)
	{
		DWORD dwCookie = 0;
		if (L > 0)
			dwCookie = _parseCookies[L - 1];
		assert(dwCookie != (DWORD) -1);
		_parseCookies[L] = ParseLine(dwCookie, L, nullptr, nBlocks);
		assert(_parseCookies[L] != (DWORD) -1);
		L++;
	}

	return _parseCookies[nLineIndex];
}

static void FillSolidRect(HDC hdc, const CRect &rc, COLORREF clr)
{
	auto r = rc;
	COLORREF clrOld = ::SetBkColor(hdc, clr);
	ATLASSERT(clrOld != CLR_INVALID);
	if (clrOld != CLR_INVALID)
	{
		::ExtTextOut(hdc, 0, 0, ETO_OPAQUE, r, nullptr, 0, nullptr);
		::SetBkColor(hdc, clrOld);
	}
}

static void FillSolidRect(HDC hdc, int l, int t, int w, int h, COLORREF clr)
{
	CRect rc(l, t, l + w, t + h);
	FillSolidRect(hdc, rc, clr);
}

void TextView::DrawSingleLine(HDC pdc, const CRect &rc, int nLineIndex)
{
	if (nLineIndex == -1)
	{
		//	Draw line beyond the text
		FillSolidRect(pdc, rc, GetColor(COLORINDEX_WHITESPACE));
	}
	else
	{
		//	Acquire the background color for the current line
		bool bDrawWhitespace = FALSE;
		COLORREF crBkgnd, crText;
		GetLineColors(nLineIndex, crBkgnd, crText, bDrawWhitespace);
		if (crBkgnd == CLR_NONE)
			crBkgnd = GetColor(COLORINDEX_BKGND);

		const auto &line = _buffer[nLineIndex];

		if (line.empty())
		{
			//	Draw the empty line
			CRect rect = rc;
			if ((m_bFocused || m_bShowInactiveSelection) && IsInsideSelBlock(CPoint(0, nLineIndex)))
			{
				FillSolidRect(pdc, rect.left, rect.top, GetCharWidth(), rect.Height(), GetColor(COLORINDEX_SELBKGND));
				rect.left += GetCharWidth();
			}
			FillSolidRect(pdc, rect, bDrawWhitespace ? crBkgnd : GetColor(COLORINDEX_WHITESPACE));
			return;
		}

		//	Parse the line
		auto nLength = line.size();
		DWORD dwCookie = GetParseCookie(nLineIndex - 1);
		TEXTBLOCK *pBuf = (TEXTBLOCK *) _alloca(sizeof(TEXTBLOCK) * nLength * 3);
		int nBlocks = 0;
		_parseCookies[nLineIndex] = ParseLine(dwCookie, nLineIndex, pBuf, nBlocks);
		assert(_parseCookies[nLineIndex] != (DWORD) -1);

		//	Draw the line text
		CPoint origin(rc.left - m_nOffsetChar * GetCharWidth(), rc.top);
		SetBkColor(pdc, crBkgnd);
		if (crText != CLR_NONE)
			SetTextColor(pdc, crText);
		bool bColorSet = FALSE;
		auto pszChars = line.c_str();

		if (nBlocks > 0)
		{
			assert(pBuf[0].m_nCharPos >= 0 && pBuf[0].m_nCharPos <= nLength);
			if (crText == CLR_NONE)
				SetTextColor(pdc, GetColor(COLORINDEX_NORMALTEXT));
			SelectObject(pdc, GetFont(GetItalic(COLORINDEX_NORMALTEXT), GetBold(COLORINDEX_NORMALTEXT)));
			DrawLineHelper(pdc, origin, rc, COLORINDEX_NORMALTEXT, pszChars, 0, pBuf[0].m_nCharPos, CPoint(0, nLineIndex));
			for (int I = 0; I < nBlocks - 1; I++)
			{
				assert(pBuf[I].m_nCharPos >= 0 && pBuf[I].m_nCharPos <= nLength);
				if (crText == CLR_NONE)
					SetTextColor(pdc, GetColor(pBuf[I].m_nColorIndex));
				SelectObject(pdc, GetFont(GetItalic(pBuf[I].m_nColorIndex), GetBold(pBuf[I].m_nColorIndex)));
				DrawLineHelper(pdc, origin, rc, pBuf[I].m_nColorIndex, pszChars,
					pBuf[I].m_nCharPos, pBuf[I + 1].m_nCharPos - pBuf[I].m_nCharPos,
					CPoint(pBuf[I].m_nCharPos, nLineIndex));
			}
			assert(pBuf[nBlocks - 1].m_nCharPos >= 0 && pBuf[nBlocks - 1].m_nCharPos <= nLength);
			if (crText == CLR_NONE)
				SetTextColor(pdc, GetColor(pBuf[nBlocks - 1].m_nColorIndex));
			SelectObject(pdc, GetFont(GetItalic(pBuf[nBlocks - 1].m_nColorIndex),
				GetBold(pBuf[nBlocks - 1].m_nColorIndex)));
			DrawLineHelper(pdc, origin, rc, pBuf[nBlocks - 1].m_nColorIndex, pszChars,
				pBuf[nBlocks - 1].m_nCharPos, nLength - pBuf[nBlocks - 1].m_nCharPos,
				CPoint(pBuf[nBlocks - 1].m_nCharPos, nLineIndex));
		}
		else
		{
			if (crText == CLR_NONE)
				SetTextColor(pdc, GetColor(COLORINDEX_NORMALTEXT));
			SelectObject(pdc, GetFont(GetItalic(COLORINDEX_NORMALTEXT), GetBold(COLORINDEX_NORMALTEXT)));
			DrawLineHelper(pdc, origin, rc, COLORINDEX_NORMALTEXT, pszChars, 0, nLength, CPoint(0, nLineIndex));
		}

		//	Draw whitespaces to the left of the text
		CRect frect = rc;
		if (origin.x > frect.left)
			frect.left = origin.x;
		if (frect.right > frect.left)
		{
			if ((m_bFocused || m_bShowInactiveSelection) && IsInsideSelBlock(CPoint(nLength, nLineIndex)))
			{
				FillSolidRect(pdc, frect.left, frect.top, GetCharWidth(), frect.Height(),
					GetColor(COLORINDEX_SELBKGND));
				frect.left += GetCharWidth();
			}
			if (frect.right > frect.left)
				FillSolidRect(pdc, frect, bDrawWhitespace ? crBkgnd : GetColor(COLORINDEX_WHITESPACE));
		}
	}
}

COLORREF TextView::GetColor(int nColorIndex) const
{
	switch (nColorIndex)
	{
	case COLORINDEX_WHITESPACE:
	case COLORINDEX_BKGND:
		return RGB(30, 30, 30);
	case COLORINDEX_NORMALTEXT:
		return RGB(240, 240, 240);
	case COLORINDEX_SELMARGIN:
		return RGB(44, 44, 44);
	case COLORINDEX_PREPROCESSOR:
		return RGB(128, 128, 192);
	case COLORINDEX_COMMENT:
		return RGB(128, 128, 128);
	case COLORINDEX_NUMBER:
		return RGB(255, 128, 128);
	case COLORINDEX_OPERATOR:
		return RGB(128, 255, 128);
	case COLORINDEX_KEYWORD:
		return RGB(128, 128, 255);
	case COLORINDEX_SELBKGND:
		return RGB(88, 88, 88);
	case COLORINDEX_SELTEXT:
		return RGB(255, 255, 255);
	}
	return RGB(255, 0, 0);
}

//COLORREF TextView::GetColor(int nColorIndex)
//{
//	switch (nColorIndex)
//	{
//	case COLORINDEX_WHITESPACE:
//	case COLORINDEX_BKGND:
//		return ::GetSysColor(COLOR_WINDOW);
//	case COLORINDEX_NORMALTEXT:
//		return ::GetSysColor(COLOR_WINDOWTEXT);
//	case COLORINDEX_SELMARGIN:
//		return ::GetSysColor(COLOR_SCROLLBAR);
//	case COLORINDEX_PREPROCESSOR:
//		return RGB(0, 128, 192);
//	case COLORINDEX_COMMENT:
//		return RGB(128, 128, 128);
//		//	[JRT]: Enabled Support For Numbers...
//	case COLORINDEX_NUMBER:
//		return RGB(0x80, 0x00, 0x00);
//	case COLORINDEX_OPERATOR:
//		return RGB(0x00, 0x00, 0x00);
//	case COLORINDEX_KEYWORD:
//		return RGB(0, 0, 255);
//	case COLORINDEX_SELBKGND:
//		return RGB(0, 0, 0);
//	case COLORINDEX_SELTEXT:
//		return RGB(255, 255, 255);
//	}
//	return RGB(255, 0, 0);
//}


void TextView::DrawMargin(HDC pdc, const CRect &rect, int nLineIndex)
{
	if (!m_bSelMargin)
	{
		FillSolidRect(pdc, rect, GetColor(COLORINDEX_BKGND));
	}
	else
	{
		FillSolidRect(pdc, rect, GetColor(COLORINDEX_SELMARGIN));
	}
}

bool TextView::IsInsideSelBlock(CPoint ptTextPos)
{
	if (ptTextPos.y < m_ptDrawSelStart.y)
		return FALSE;
	if (ptTextPos.y > m_ptDrawSelEnd.y)
		return FALSE;
	if (ptTextPos.y < m_ptDrawSelEnd.y && ptTextPos.y > m_ptDrawSelStart.y)
		return TRUE;
	if (m_ptDrawSelStart.y < m_ptDrawSelEnd.y)
	{
		if (ptTextPos.y == m_ptDrawSelEnd.y)
			return ptTextPos.x < m_ptDrawSelEnd.x;
		assert(ptTextPos.y == m_ptDrawSelStart.y);
		return ptTextPos.x >= m_ptDrawSelStart.x;
	}
	assert(m_ptDrawSelStart.y == m_ptDrawSelEnd.y);
	return ptTextPos.x >= m_ptDrawSelStart.x && ptTextPos.x < m_ptDrawSelEnd.x;
}

bool TextView::IsInsideSelection(const CPoint &ptTextPos)
{
	PrepareSelBounds();
	return IsInsideSelBlock(ptTextPos);
}

void TextView::PrepareSelBounds()
{
	if (m_ptSelStart.y < m_ptSelEnd.y ||
		(m_ptSelStart.y == m_ptSelEnd.y && m_ptSelStart.x < m_ptSelEnd.x))
	{
		m_ptDrawSelStart = m_ptSelStart;
		m_ptDrawSelEnd = m_ptSelEnd;
	}
	else
	{
		m_ptDrawSelStart = m_ptSelEnd;
		m_ptDrawSelEnd = m_ptSelStart;
	}
}

void TextView::OnDraw(HDC pdc)
{
	CRect rcClient;
	GetClientRect(rcClient);

	int nLineCount = _buffer.LineCount();
	int nLineHeight = GetLineHeight();
	PrepareSelBounds();

	HDC cacheDC;
	cacheDC = CreateCompatibleDC(pdc);

	if (m_pCacheBitmap == nullptr)
	{
		m_pCacheBitmap = CreateCompatibleBitmap(pdc, rcClient.Width(), nLineHeight);
	}

	auto pOldBitmap = SelectObject(cacheDC, m_pCacheBitmap);

	CRect rcLine;
	rcLine = rcClient;
	rcLine.bottom = rcLine.top + nLineHeight;
	CRect rcCacheMargin(0, 0, GetMarginWidth(), nLineHeight);
	CRect rcCacheLine(GetMarginWidth(), 0, rcLine.Width(), nLineHeight);

	int nCurrentLine = m_nTopLine;
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
	m_bAutoIndent = TRUE;
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

	for (int I = 0; I < 4; I++)
	{
		if (m_apFonts[I] != nullptr)
		{
			DeleteObject(m_apFonts[I]);
			m_apFonts[I] = nullptr;
		}
	}

	_parseCookies.clear();
	_actualLineLengths.clear();

	m_ptCursorPos.x = 0;
	m_ptCursorPos.y = 0;
	m_ptSelStart = m_ptSelEnd = m_ptCursorPos;
	m_bDragSelection = FALSE;
	if (::IsWindow(m_hWnd))
		UpdateCaret();
	m_bLastSearch = FALSE;
	m_bShowInactiveSelection = FALSE;
	m_bPrintHeader = FALSE;
	m_bPrintFooter = TRUE;

	m_bMultipleSearch = FALSE;	// More search
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

HFONT TextView::GetFont(bool bItalic /*= FALSE*/, bool bBold /*= FALSE*/)
{
	int nIndex = 0;
	if (bBold)
		nIndex |= 1;
	if (bItalic)
		nIndex |= 2;

	if (m_apFonts[nIndex] == nullptr)
	{
		m_apFonts[nIndex] = nullptr;
		m_lfBaseFont.lfWeight = bBold ? FW_BOLD : FW_NORMAL;
		m_lfBaseFont.lfItalic = (BYTE) bItalic;
		m_apFonts[nIndex] = ::CreateFontIndirect(&m_lfBaseFont);
	}
	return m_apFonts[nIndex];
}

void TextView::CalcLineCharDim() const
{
	auto pThis = const_cast<TextView*>(this);
	HDC pdc = pThis->GetDC();

	auto pOldFont = SelectObject(pdc, pThis->GetFont());
	CSize szCharExt;
	GetTextExtentExPoint(pdc, _T("X"), 1, 1, nullptr, nullptr, &szCharExt);
	m_nLineHeight = szCharExt.cy;
	if (m_nLineHeight < 1)
		m_nLineHeight = 1;
	m_nCharWidth = szCharExt.cx;
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

		for (int I = 0; I < nLineCount; I++)
		{
			int nActualLength = GetLineActualLength(I);

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
	pInfo->m_bContinuePrinting = TRUE;
	if (m_pnPages != nullptr && (int) pInfo->m_nCurPage > m_nPrintPages)
	pInfo->m_bContinuePrinting = FALSE;
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

	CRect rcPrintArea = m_rcPrintArea;
	DrawText(pdc, expanded.c_str(), -1, rcPrintArea, DT_LEFT | DT_NOPREFIX | DT_TOP | DT_WORDBREAK | DT_CALCRECT);
	return rcPrintArea.Height();
}

void TextView::GetPrintHeaderText(int nPageNum, std::wstring &text)
{
	assert(m_bPrintHeader);
	text = _T("");
}

void TextView::GetPrintFooterText(int nPageNum, std::wstring &text)
{
	assert(m_bPrintFooter);
	//TODO text.Format(_T("Page %d/%d"), nPageNum, m_nPrintPages);
}

void TextView::PrintHeader(HDC pdc, int nPageNum)
{
	CRect rcHeader = m_rcPrintArea;
	rcHeader.bottom = rcHeader.top;
	rcHeader.top -= (m_nPrintLineHeight + m_nPrintLineHeight / 2);

	std::wstring text;
	GetPrintHeaderText(nPageNum, text);
	if (!text.empty())
		DrawText(pdc, text.c_str(), -1, rcHeader, DT_CENTER | DT_NOPREFIX | DT_TOP | DT_SINGLELINE);
}

void TextView::PrintFooter(HDC pdc, int nPageNum)
{
	CRect rcFooter = m_rcPrintArea;
	rcFooter.top = rcFooter.bottom;
	rcFooter.bottom += (m_nPrintLineHeight + m_nPrintLineHeight / 2);

	std::wstring text;
	GetPrintFooterText(nPageNum, text);
	if (!text.empty())
		DrawText(pdc, text.c_str(), -1, &rcFooter, DT_CENTER | DT_NOPREFIX | DT_BOTTOM | DT_SINGLELINE);
}

void TextView::RecalcPageLayouts(HDC pdc, CPrintInfo *pInfo)
{
	/*m_ptPageArea = pInfo->m_rectDraw;
	m_ptPageArea.NormalizeRect();

	m_nPrintLineHeight = pdc->GetTextExtent(_T("X")).cy;

	m_rcPrintArea = m_ptPageArea;
	CSize szTopLeft, szBottomRight;
	CWinApp *pApp = AfxGetApp();
	assert(pApp != nullptr);
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

	int nLimit = 32;
	m_nPrintPages = 1;
	//m_pnPages = new int[nLimit];
	m_pnPages[0] = 0;

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
	assert(nLimit >= m_nPrintPages);
	if (nLimit <= m_nPrintPages)
	{
	nLimit += 32;
	//int *pnNewPages = new int[nLimit];
	memcpy(pnNewPages, m_pnPages, sizeof(int) * m_nPrintPages);
	delete m_pnPages;
	m_pnPages = pnNewPages;
	}
	assert(nLimit > m_nPrintPages);
	m_pnPages[m_nPrintPages ++] = nLine;
	y = m_rcPrintArea.top + nHeight;
	}
	nLine ++;
	}*/
}

void TextView::OnBeginPrinting(HDC pdc, CPrintInfo *pInfo)
{
	/*assert(m_pnPages == nullptr);
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
	if (m_pnPages != nullptr)
	{
	delete m_pnPages;
	m_pnPages = nullptr;
	}
	m_nPrintPages = 0;
	m_nPrintLineHeight = 0;*/
}

void TextView::OnPrint(HDC pdc, CPrintInfo* pInfo)
{
	/*if (m_pnPages == nullptr)
	{
	RecalcPageLayouts(pdc, pInfo);
	assert(m_pnPages != nullptr);
	}

	assert(pInfo->m_nCurPage >= 1 && (int) pInfo->m_nCurPage <= m_nPrintPages);
	int nLine = m_pnPages[pInfo->m_nCurPage - 1];
	int nEndLine = _buffer.LineCount();
	if ((int) pInfo->m_nCurPage < m_nPrintPages)
	nEndLine = m_pnPages[pInfo->m_nCurPage];
	TRACE(_T("Printing page %d of %d, lines %d - %d\n"), pInfo->m_nCurPage, m_nPrintPages,
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

bool TextView::GetItalic(int nColorIndex)
{
	return FALSE;
}

bool TextView::GetBold(int nColorIndex)
{
	return FALSE;
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
	/*if (m_pDropTarget != nullptr)
	{
	m_pDropTarget->Revoke();
	delete m_pDropTarget;
	m_pDropTarget = nullptr;
	}*/

	m_hAccel = nullptr;


	for (int I = 0; I < 4; I++)
	{
		if (m_apFonts[I] != nullptr)
		{
			DeleteObject(m_apFonts[I]);
			m_apFonts[I] = nullptr;
		}
	}
	if (m_pCacheBitmap != nullptr)
	{
		DeleteObject(m_pCacheBitmap);
		m_pCacheBitmap = nullptr;
	}
}

bool TextView::OnEraseBkgnd(HDC pdc)
{
	return TRUE;
}

void TextView::OnSize(UINT nType, int cx, int cy)
{
	if (m_pCacheBitmap != nullptr)
	{
		DeleteObject(m_pCacheBitmap);
		m_pCacheBitmap = nullptr;
	}

	m_nScreenLines = -1;
	m_nScreenChars = -1;
	RecalcVertScrollBar();
	RecalcHorzScrollBar();
}


void TextView::RecalcVertScrollBar(bool bPositionOnly /*= FALSE*/)
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

void TextView::RecalcHorzScrollBar(bool bPositionOnly /*= FALSE*/)
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
	ScrollToChar(nNewOffset, TRUE);
	UpdateCaret();
}

bool TextView::OnSetCursor(CWindow* pWnd, UINT nHitTest, UINT message)
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
			CPoint ptText = ClientToText(pt);
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
		return TRUE;
	}
	return FALSE;
}

CPoint TextView::ClientToText(const CPoint &point)
{
	int nLineCount = _buffer.LineCount();

	CPoint pt;
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


CPoint TextView::TextToClient(const CPoint &point)
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

void TextView::InvalidateLines(int nLine1, int nLine2, bool bInvalidateMargin /*= FALSE*/)
{
	bInvalidateMargin = TRUE;

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

	InvalidateRect(rcInvalid, FALSE);
}

void TextView::SetSelection(const CPoint &ptStart, const CPoint &ptEnd)
{
	InvalidateLines(ptStart.y, ptEnd.y);
	InvalidateLines(m_ptSelStart.y, m_ptSelEnd.y);

	m_ptSelStart = ptStart;
	m_ptSelEnd = ptEnd;
}

CPoint TextView::AdjustTextPoint(const CPoint &point) const
{
	CPoint result(point);
	result.x += GetCharWidth() / 2;	//todo
	return result;
}

void TextView::OnSetFocus(CWindow* pOldWnd)
{
	m_bFocused = TRUE;
	if (m_ptSelStart != m_ptSelEnd)
		InvalidateLines(m_ptSelStart.y, m_ptSelEnd.y);
	UpdateCaret();
}


int TextView::CalculateActualOffset(int nLineIndex, int nCharIndex)
{
	const auto &line = _buffer[nLineIndex];

	int nOffset = 0;
	int tabSize = GetTabSize();

	for (int I = 0; I < nCharIndex; I++)
	{
		if (line[I] == _T('\t'))
			nOffset += (tabSize - nOffset % tabSize);
		else
			nOffset++;
	}
	return nOffset;
}

int TextView::ApproxActualOffset(int nLineIndex, int nOffset)
{
	if (nOffset == 0)
		return 0;

	const auto &line = _buffer[nLineIndex];
	const auto nLength = line.size();

	int nCurrentOffset = 0;
	int tabSize = GetTabSize();

	for (int I = 0; I < nLength; I++)
	{
		if (line[I] == _T('\t'))
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
				return I;
			return I + 1;
		}
	}
	return nLength;
}

void TextView::EnsureVisible(CPoint pt)
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

void TextView::OnKillFocus(CWindow* pNewWnd)
{
	m_bFocused = FALSE;
	UpdateCaret();
	if (m_ptSelStart != m_ptSelEnd)
		InvalidateLines(m_ptSelStart.y, m_ptSelEnd.y);
	if (m_bDragSelection)
	{
		ReleaseCapture();
		KillTimer(m_nDragSelTimer);
		m_bDragSelection = FALSE;
	}
}

void TextView::OnSysColorChange()
{
	Invalidate();
}

std::vector<std::wstring> TextView::Text(const CPoint &ptStart, const CPoint &ptEnd) const
{
	return _buffer.Text(ptStart, ptEnd);
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
	Invalidate(FALSE);
}

HINSTANCE TextView::GetResourceHandle()
{
	return _AtlBaseModule.GetResourceInstance();
}

int TextView::OnCreate()
{
	memset(&m_lfBaseFont, 0, sizeof(m_lfBaseFont));
	lstrcpy(m_lfBaseFont.lfFaceName, _T("FixedSys"));
	m_lfBaseFont.lfHeight = 0;
	m_lfBaseFont.lfWeight = FW_NORMAL;
	m_lfBaseFont.lfItalic = FALSE;
	m_lfBaseFont.lfCharSet = DEFAULT_CHARSET;
	m_lfBaseFont.lfOutPrecision = OUT_DEFAULT_PRECIS;
	m_lfBaseFont.lfClipPrecision = CLIP_DEFAULT_PRECIS;
	m_lfBaseFont.lfQuality = DEFAULT_QUALITY;
	m_lfBaseFont.lfPitchAndFamily = DEFAULT_PITCH;

	assert(m_hAccel == nullptr);
	m_hAccel = ::LoadAccelerators(GetResourceHandle(), MAKEINTRESOURCE(IDC_RETHINKIFY));
	assert(m_hAccel != nullptr);

	assert(m_pDropTarget == nullptr);
	/*m_pDropTarget = new CEditDropTargetImpl(this);
	if (! m_pDropTarget->Register(this))
	{
	TRACE0("Warning: Unable to register drop target for TextView.\n");
	delete m_pDropTarget;
	m_pDropTarget = nullptr;
	}*/

	return 0;
}

void TextView::SetAnchor(const CPoint &ptNewAnchor)
{
	m_ptAnchor = ptNewAnchor;
}

bool TextView::PreTranslateMessage(MSG *pMsg)
{
	if (pMsg->message >= WM_KEYFIRST && pMsg->message <= WM_KEYLAST)
	{
		if (m_hAccel != nullptr)
		{
			if (::TranslateAccelerator(m_hWnd, m_hAccel, pMsg))
				return TRUE;
		}
	}

	return FALSE;
}

void TextView::SetCursorPos(const CPoint &ptCursorPos)
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

void TextView::GetFont(LOGFONT &lf)
{
	lf = m_lfBaseFont;
}

void TextView::SetFont(const LOGFONT &lf)
{
	m_lfBaseFont = lf;
	m_nScreenLines = -1;
	m_nScreenChars = -1;
	m_nCharWidth = -1;
	m_nLineHeight = -1;
	if (m_pCacheBitmap != nullptr)
	{
		DeleteObject(m_pCacheBitmap);
		m_pCacheBitmap = nullptr;
	}
	for (int I = 0; I < 4; I++)
	{
		if (m_apFonts[I] != nullptr)
		{
			DeleteObject(m_apFonts[I]);
			m_apFonts[I] = nullptr;
		}
	}
	if (::IsWindow(m_hWnd))
	{
		RecalcVertScrollBar();
		RecalcHorzScrollBar();
		UpdateCaret();
		Invalidate();
	}
}

void TextView::ShowCursor()
{
	m_bCursorHidden = FALSE;
	UpdateCaret();
}

void TextView::HideCursor()
{
	m_bCursorHidden = TRUE;
	UpdateCaret();
}

HGLOBAL TextView::PrepareDragData()
{
	PrepareSelBounds();
	if (m_ptDrawSelStart == m_ptDrawSelEnd)
		return nullptr;

	auto text = ToUtf8(Combine(Text(m_ptDrawSelStart, m_ptDrawSelEnd)));
	auto len = text.size() + 1;

	HGLOBAL hData = ::GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, len * sizeof(wchar_t));
	if (hData == nullptr)
		return nullptr;

	char* pszData = (char*) ::GlobalLock(hData);
	strcpy_s(pszData, len, text.c_str());
	::GlobalUnlock(hData);

	m_ptDraggedTextBegin = m_ptDrawSelStart;
	m_ptDraggedTextEnd = m_ptDrawSelEnd;
	return hData;
}



bool TextView::HighlightText(const CPoint &ptStartPos, int nLength)
{
	m_ptCursorPos = ptStartPos;
	m_ptCursorPos.x += nLength;
	m_ptAnchor = m_ptCursorPos;
	SetSelection(ptStartPos, m_ptCursorPos);
	UpdateCaret();
	EnsureVisible(m_ptCursorPos);
	return TRUE;
}


void TextView::OnEditFind()
{
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
	//		dlg.m_bMatchCase = pApp->GetProfileInt(REG_FIND_SUBKEY, REG_MATCH_CASE, FALSE);
	//		dlg.m_bWholeWord = pApp->GetProfileInt(REG_FIND_SUBKEY, REG_WHOLE_WORD, FALSE);
	//		dlg.m_nDirection = 1;		//	Search down
	//		dlg.m_sText = pApp->GetProfileString(REG_FIND_SUBKEY, REG_FIND_WHAT, _T(""));
	//	}
	//
	//	//	Take the current selection, if any
	//	if (HasSelection())
	//	{
	//		CPoint ptSelStart, ptSelEnd;
	//		GetSelection(ptSelStart, ptSelEnd);		if (ptSelStart.y == ptSelEnd.y)
	//		{
	//			LPCTSTR pszChars = GetLineChars(ptSelStart.y);
	//			int nChars = ptSelEnd.x - ptSelStart.x;
	//			lstrcpyn(dlg.m_sText.GetBuffer(nChars + 1), pszChars + ptSelStart.x, nChars + 1);
	//			dlg.m_sText.ReleaseBuffer();
	//		}
	//	}
	//
	//	//	Execute Find dialog
	//	dlg.m_ptCurrentPos = m_ptCursorPos;		//	Search from cursor position
	//	m_bShowInactiveSelection = TRUE;
	//	dlg.DoModal();
	//	m_bShowInactiveSelection = FALSE;
	//
	//	//	Save search parameters for 'F3' command
	//	m_bLastSearch = TRUE;
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
	//	CPoint ptFoundPos;
	//	if (! FindText(_lastFindWhat, m_ptCursorPos, m_dwLastSearchFlags, TRUE, &ptFoundPos))
	//	{
	//		std::wstring prompt;
	//		prompt.Format(IDS_EDIT_TEXT_NOT_FOUND, _lastFindWhat);
	//		AfxMessageBox(prompt);
	//		return;
	//	}
	//	HighlightText(ptFoundPos, lstrlen(_lastFindWhat));
	//	m_bMultipleSearch = TRUE;       // More search       
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

void TextView::MoveLeft(bool bSelect)
{
	PrepareSelBounds();

	if (m_ptDrawSelStart != m_ptDrawSelEnd && !bSelect)
	{
		m_ptCursorPos = m_ptDrawSelStart;
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
	if (!bSelect)
		m_ptAnchor = m_ptCursorPos;
	SetSelection(m_ptAnchor, m_ptCursorPos);
}

void TextView::MoveRight(bool bSelect)
{
	PrepareSelBounds();
	if (m_ptDrawSelStart != m_ptDrawSelEnd && !bSelect)
	{
		m_ptCursorPos = m_ptDrawSelEnd;
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
	if (!bSelect)
		m_ptAnchor = m_ptCursorPos;
	SetSelection(m_ptAnchor, m_ptCursorPos);
}

void TextView::MoveWordLeft(bool bSelect)
{
	PrepareSelBounds();
	if (m_ptDrawSelStart != m_ptDrawSelEnd && !bSelect)
	{
		MoveLeft(bSelect);
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

	while (nPos > 0 && isspace(line[nPos - 1]))
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
				&& line[nPos - 1] != _T('_') && !isspace(line[nPos - 1]))
				nPos--;
		}
	}

	m_ptCursorPos.x = nPos;
	m_nIdealCharPos = CalculateActualOffset(m_ptCursorPos.y, m_ptCursorPos.x);
	EnsureVisible(m_ptCursorPos);
	UpdateCaret();
	if (!bSelect)
		m_ptAnchor = m_ptCursorPos;
	SetSelection(m_ptAnchor, m_ptCursorPos);
}

void TextView::MoveWordRight(bool bSelect)
{
	PrepareSelBounds();
	if (m_ptDrawSelStart != m_ptDrawSelEnd && !bSelect)
	{
		MoveRight(bSelect);
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
		MoveRight(bSelect);
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
			&& line[nPos] != _T('_') && !isspace(line[nPos]))
			nPos++;
	}

	while (nPos < nLength && isspace(line[nPos]))
		nPos++;

	m_ptCursorPos.x = nPos;
	m_nIdealCharPos = CalculateActualOffset(m_ptCursorPos.y, m_ptCursorPos.x);
	EnsureVisible(m_ptCursorPos);
	UpdateCaret();
	if (!bSelect)
		m_ptAnchor = m_ptCursorPos;
	SetSelection(m_ptAnchor, m_ptCursorPos);
}

void TextView::MoveUp(bool bSelect)
{
	PrepareSelBounds();
	if (m_ptDrawSelStart != m_ptDrawSelEnd && !bSelect)
		m_ptCursorPos = m_ptDrawSelStart;

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
	if (!bSelect)
		m_ptAnchor = m_ptCursorPos;
	SetSelection(m_ptAnchor, m_ptCursorPos);
}

void TextView::MoveDown(bool bSelect)
{
	PrepareSelBounds();
	if (m_ptDrawSelStart != m_ptDrawSelEnd && !bSelect)
		m_ptCursorPos = m_ptDrawSelEnd;

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
	if (!bSelect)
		m_ptAnchor = m_ptCursorPos;
	SetSelection(m_ptAnchor, m_ptCursorPos);
}

void TextView::MoveHome(bool bSelect)
{
	const auto &line = _buffer[m_ptCursorPos.y];

	int nHomePos = 0;
	while (nHomePos < line.size() && isspace(line[nHomePos]))
		nHomePos++;
	if (nHomePos == line.size() || m_ptCursorPos.x == nHomePos)
		m_ptCursorPos.x = 0;
	else
		m_ptCursorPos.x = nHomePos;
	m_nIdealCharPos = CalculateActualOffset(m_ptCursorPos.y, m_ptCursorPos.x);
	EnsureVisible(m_ptCursorPos);
	UpdateCaret();
	if (!bSelect)
		m_ptAnchor = m_ptCursorPos;
	SetSelection(m_ptAnchor, m_ptCursorPos);
}

void TextView::MoveEnd(bool bSelect)
{
	m_ptCursorPos.x = _buffer[m_ptCursorPos.y].size();
	m_nIdealCharPos = CalculateActualOffset(m_ptCursorPos.y, m_ptCursorPos.x);
	EnsureVisible(m_ptCursorPos);
	UpdateCaret();
	if (!bSelect)
		m_ptAnchor = m_ptCursorPos;
	SetSelection(m_ptAnchor, m_ptCursorPos);
}

void TextView::MovePgUp(bool bSelect)
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
	if (!bSelect)
		m_ptAnchor = m_ptCursorPos;
	SetSelection(m_ptAnchor, m_ptCursorPos);
}

void TextView::MovePgDn(bool bSelect)
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
	if (!bSelect)
		m_ptAnchor = m_ptCursorPos;
	SetSelection(m_ptAnchor, m_ptCursorPos);
}

void TextView::MoveCtrlHome(bool bSelect)
{
	m_ptCursorPos.x = 0;
	m_ptCursorPos.y = 0;
	m_nIdealCharPos = CalculateActualOffset(m_ptCursorPos.y, m_ptCursorPos.x);
	EnsureVisible(m_ptCursorPos);
	UpdateCaret();
	if (!bSelect)
		m_ptAnchor = m_ptCursorPos;
	SetSelection(m_ptAnchor, m_ptCursorPos);
}

void TextView::MoveCtrlEnd(bool bSelect)
{
	m_ptCursorPos.y = _buffer.LineCount() - 1;
	m_ptCursorPos.x = _buffer[m_ptCursorPos.y].size();
	m_nIdealCharPos = CalculateActualOffset(m_ptCursorPos.y, m_ptCursorPos.x);
	EnsureVisible(m_ptCursorPos);
	UpdateCaret();
	if (!bSelect)
		m_ptAnchor = m_ptCursorPos;
	SetSelection(m_ptAnchor, m_ptCursorPos);
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

CPoint TextView::WordToRight(CPoint pt)
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

CPoint TextView::WordToLeft(CPoint pt)
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
	SetSelection(CPoint(0, 0), m_ptCursorPos);
	UpdateCaret();
}

void TextView::OnLButtonDown(const CPoint &point, UINT nFlags)
{
	bool bShift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
	bool bControl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;

	if (point.x < GetMarginWidth())
	{
		AdjustTextPoint(point);
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

			CPoint ptStart, ptEnd;
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
			SetSelection(ptStart, ptEnd);

			SetCapture();
			m_nDragSelTimer = SetTimer(RETHINKIFY_TIMER_DRAGSEL, 100, nullptr);
			assert(m_nDragSelTimer != 0);
			m_bWordSelection = FALSE;
			m_bLineSelection = TRUE;
			m_bDragSelection = TRUE;
		}
	}
	else
	{
		CPoint ptText = ClientToText(point);
		PrepareSelBounds();

		if ((IsInsideSelBlock(ptText)) &&				// If Inside Selection Area
			(!m_bDisableDragAndDrop))				// And D&D Not Disabled
		{
			m_bPreparingToDrag = TRUE;
		}
		else
		{
			AdjustTextPoint(point);
			m_ptCursorPos = ClientToText(point);
			if (!bShift)
				m_ptAnchor = m_ptCursorPos;

			CPoint ptStart, ptEnd;
			if (bControl)
			{
				if (m_ptCursorPos.y < m_ptAnchor.y ||
					m_ptCursorPos.y == m_ptAnchor.y && m_ptCursorPos.x < m_ptAnchor.x)
				{
					ptStart = WordToLeft(m_ptCursorPos);
					ptEnd = WordToRight(m_ptAnchor);
				}
				else
				{
					ptStart = WordToLeft(m_ptAnchor);
					ptEnd = WordToRight(m_ptCursorPos);
				}
			}
			else
			{
				ptStart = m_ptAnchor;
				ptEnd = m_ptCursorPos;
			}

			m_ptCursorPos = ptEnd;
			UpdateCaret();
			EnsureVisible(m_ptCursorPos);
			SetSelection(ptStart, ptEnd);

			SetCapture();
			m_nDragSelTimer = SetTimer(RETHINKIFY_TIMER_DRAGSEL, 100, nullptr);
			assert(m_nDragSelTimer != 0);
			m_bWordSelection = bControl;
			m_bLineSelection = FALSE;
			m_bDragSelection = TRUE;
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

		AdjustTextPoint(point);
		CPoint ptNewCursorPos = ClientToText(point);

		CPoint ptStart, ptEnd;
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
				SetSelection(ptNewCursorPos, ptEnd);
				return;
			}

			//	Moving to normal selection mode
			::SetCursor(::LoadCursor(nullptr, MAKEINTRESOURCE(IDC_IBEAM)));
			m_bLineSelection = m_bWordSelection = FALSE;
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
		SetSelection(ptStart, ptEnd);
	}

	if (m_bPreparingToDrag)
	{
		m_bPreparingToDrag = FALSE;
		HGLOBAL hData = PrepareDragData();
		if (hData != nullptr)
		{
			UndoGroup ug(_buffer);


			/*COleDataSource ds;
			ds.CacheGlobalData(CF_TEXT, hData);
			m_bDraggingText = TRUE;
			DROPEFFECT de = ds.DoDragDrop(GetDropEffect());
			if (de != DROPEFFECT_NONE)
			OnDropSource(de);
			m_bDraggingText = FALSE;

			if (_buffer != nullptr)
			_buffer.FlushUndoGroup(this);*/
		}
	}
}

void TextView::OnLButtonUp(const CPoint &point, UINT nFlags)
{
	if (m_bDragSelection)
	{
		AdjustTextPoint(point);
		CPoint ptNewCursorPos = ClientToText(point);

		CPoint ptStart, ptEnd;
		if (m_bLineSelection)
		{
			CPoint ptEnd;
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
			SetSelection(ptNewCursorPos, ptEnd);
		}
		else
		{
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
				ptEnd = m_ptCursorPos;
			}

			m_ptCursorPos = ptEnd;
			EnsureVisible(m_ptCursorPos);
			UpdateCaret();
			SetSelection(ptStart, ptEnd);
		}

		ReleaseCapture();
		KillTimer(m_nDragSelTimer);
		m_bDragSelection = FALSE;
	}

	if (m_bPreparingToDrag)
	{
		m_bPreparingToDrag = FALSE;

		AdjustTextPoint(point);
		m_ptCursorPos = ClientToText(point);
		EnsureVisible(m_ptCursorPos);
		UpdateCaret();
		SetSelection(m_ptCursorPos, m_ptCursorPos);
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

		bool bChanged = FALSE;

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
			bChanged = TRUE;
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
			bChanged = TRUE;
		}

		//	Fix changes
		if (bChanged)
		{
			AdjustTextPoint(pt);
			CPoint ptNewCursorPos = ClientToText(pt);
			if (ptNewCursorPos != m_ptCursorPos)
			{
				m_ptCursorPos = ptNewCursorPos;
				UpdateCaret();
			}
			SetSelection(m_ptAnchor, m_ptCursorPos);
		}
	}
}

void TextView::OnLButtonDblClk(const CPoint &point, UINT nFlags)
{
	if (!m_bDragSelection)
	{
		AdjustTextPoint(point);

		m_ptCursorPos = ClientToText(point);
		m_ptAnchor = m_ptCursorPos;

		CPoint ptStart, ptEnd;
		if (m_ptCursorPos.y < m_ptAnchor.y ||
			m_ptCursorPos.y == m_ptAnchor.y && m_ptCursorPos.x < m_ptAnchor.x)
		{
			ptStart = WordToLeft(m_ptCursorPos);
			ptEnd = WordToRight(m_ptAnchor);
		}
		else
		{
			ptStart = WordToLeft(m_ptAnchor);
			ptEnd = WordToRight(m_ptCursorPos);
		}

		m_ptCursorPos = ptEnd;
		UpdateCaret();
		EnsureVisible(m_ptCursorPos);
		SetSelection(ptStart, ptEnd);

		SetCapture();
		m_nDragSelTimer = SetTimer(RETHINKIFY_TIMER_DRAGSEL, 100, nullptr);
		assert(m_nDragSelTimer != 0);
		m_bWordSelection = TRUE;
		m_bLineSelection = FALSE;
		m_bDragSelection = TRUE;
	}
}

void TextView::OnEditCopy()
{
	Copy();
}

void TextView::OnEditSelectAll()
{
	SelectAll();
}

void TextView::OnRButtonDown(const CPoint &point, UINT nFlags)
{
	CPoint pt = point;
	AdjustTextPoint(pt);
	pt = ClientToText(pt);
	if (!IsInsideSelBlock(pt))
	{
		m_ptAnchor = m_ptCursorPos = pt;
		SetSelection(m_ptCursorPos, m_ptCursorPos);
		EnsureVisible(m_ptCursorPos);
		UpdateCaret();
	}
}

void TextView::Copy()
{
	if (m_ptSelStart == m_ptSelEnd)
		return;

	PrepareSelBounds();
	PutToClipboard(Combine(Text(m_ptDrawSelStart, m_ptDrawSelEnd)));
}

bool TextView::TextInClipboard()
{
	return IsClipboardFormatAvailable(CF_TEXT) != 0;
}

bool TextView::PutToClipboard(const std::wstring &textUtf16)
{
	// TODO CWaitCursor wc;
	bool success = false;

	if (OpenClipboard())
	{
		EmptyClipboard();

		auto text = ToUtf8(textUtf16);
		auto len = text.size() + 1;
		HGLOBAL hData = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, len);

		if (hData != nullptr)
		{
			LPSTR pszData = (LPSTR) ::GlobalLock(hData);
			strcpy_s(pszData, len, text.c_str());
			GlobalUnlock(hData);
			success = SetClipboardData(CF_TEXT, hData) != nullptr;
		}
		CloseClipboard();
	}

	return success;
}

bool TextView::GetFromClipboard(std::wstring &text) const
{
	// TODO
	bool bSuccess = FALSE;
	/*if (OpenClipboard())
	{
	HGLOBAL hData = GetClipboardData(CF_TEXT);
	if (hData != nullptr)
	{
	LPSTR pszData = (LPSTR) GlobalLock(hData);

	if (pszData != nullptr)
	{
	text = pszData;
	GlobalUnlock(hData);
	bSuccess = TRUE;
	}
	}
	CloseClipboard();
	}*/
	return bSuccess;
}

bool TextView::QueryEditable()
{
	return true;
}

void TextView::OnEditPaste()
{
	Paste();
}

void TextView::OnEditCut()
{
	Cut();
}

bool TextView::DeleteCurrentSelection(UndoGroup &ug)
{
	if (HasSelection())
	{
		CPoint ptSelStart, ptSelEnd;
		GetSelection(ptSelStart, ptSelEnd);

		CPoint ptCursorPos = ptSelStart;
		SetAnchor(ptCursorPos);
		SetSelection(ptCursorPos, ptCursorPos);
		SetCursorPos(ptCursorPos);
		EnsureVisible(ptCursorPos);

		_buffer.DeleteText(ug, ptSelStart, ptSelEnd);
		return TRUE;
	}
	return FALSE;
}

void TextView::Paste()
{
	if (QueryEditable())
	{
		UndoGroup ug(_buffer);
		DeleteCurrentSelection(ug);

		std::wstring text;

		if (GetFromClipboard(text))
		{
			auto location = _buffer.InsertText(ug, GetCursorPos(), text);
			SetAnchor(location);
			SetSelection(location, location);
			SetCursorPos(location);
			EnsureVisible(location);
		}
	}
}

void TextView::Cut()
{
	if (QueryEditable() && HasSelection())
	{
		CPoint ptSelStart, ptSelEnd;
		GetSelection(ptSelStart, ptSelEnd);
		PutToClipboard(Combine(Text(ptSelStart, ptSelEnd)));

		CPoint ptCursorPos = ptSelStart;
		SetAnchor(ptCursorPos);
		SetSelection(ptCursorPos, ptCursorPos);
		SetCursorPos(ptCursorPos);
		EnsureVisible(ptCursorPos);

		UndoGroup ug(_buffer);
		_buffer.DeleteText(ug, ptSelStart, ptSelEnd);
	}
}

void TextView::OnEditDelete()
{
	if (QueryEditable())
	{
		CPoint ptSelStart, ptSelEnd;
		GetSelection(ptSelStart, ptSelEnd);
		if (ptSelStart == ptSelEnd)
		{
			if (ptSelEnd.x == _buffer[ptSelEnd.y].size())
			{
				if (ptSelEnd.y == _buffer.LineCount() - 1)
					return;
				ptSelEnd.y++;
				ptSelEnd.x = 0;
			}
			else
				ptSelEnd.x++;
		}

		CPoint ptCursorPos = ptSelStart;
		SetAnchor(ptCursorPos);
		SetSelection(ptCursorPos, ptCursorPos);
		SetCursorPos(ptCursorPos);
		EnsureVisible(ptCursorPos);

		UndoGroup ug(_buffer);
		_buffer.DeleteText(ug, ptSelStart, ptSelEnd);
	}
}

void TextView::OnChar(UINT nChar, UINT nRepCnt, UINT nFlags)
{
	if ((::GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0 ||
		(::GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0)
		return;

	bool bTranslated = FALSE;
	if (nChar == VK_RETURN)
	{
		if (QueryEditable())
		{
			UndoGroup ug(_buffer);
			DeleteCurrentSelection(ug);

			auto location = _buffer.InsertText(ug, GetCursorPos(), L'\n');
			SetSelection(location, location);
			SetAnchor(location);
			SetCursorPos(location);
			EnsureVisible(location);
		}

		return;
	}

	if (nChar > 31)
	{
		if (QueryEditable())
		{
			UndoGroup ug(_buffer);
			auto location = GetCursorPos();

			if (HasSelection())
			{
				CPoint ptSelStart, ptSelEnd;
				GetSelection(ptSelStart, ptSelEnd);

				location = ptSelStart;
				DeleteCurrentSelection(ug);
			}

			location = _buffer.InsertText(ug, location, nChar);

			SetSelection(location, location);
			SetAnchor(location);
			SetCursorPos(location);
			EnsureVisible(location);
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
			auto ptCursorPos = GetCursorPos();

			UndoGroup ug(_buffer);
			auto newPosition = _buffer.DeleteText(ug, ptCursorPos);

			SetAnchor(newPosition);
			SetSelection(newPosition, newPosition);
			SetCursorPos(newPosition);
			EnsureVisible(newPosition);
		}
	}
}

void TextView::OnEditTab()
{
	if (QueryEditable())
	{
		bool bTabify = FALSE;
		CPoint ptSelStart, ptSelEnd;

		if (HasSelection())
		{
			GetSelection(ptSelStart, ptSelEnd);
			bTabify = ptSelStart.y != ptSelEnd.y;
		}

		if (bTabify)
		{
			UndoGroup ug(_buffer);

			int nStartLine = ptSelStart.y;
			int nEndLine = ptSelEnd.y;
			ptSelStart.x = 0;

			if (ptSelEnd.x > 0)
			{
				if (ptSelEnd.y == _buffer.LineCount() - 1)
				{
					ptSelEnd.x = _buffer[ptSelEnd.y].size();
				}
				else
				{
					ptSelEnd.x = 0;
					ptSelEnd.y++;
				}
			}
			else
			{
				nEndLine--;
			}

			SetSelection(ptSelStart, ptSelEnd);
			SetCursorPos(ptSelEnd);
			EnsureVisible(ptSelEnd);

			static const TCHAR pszText [] = _T("\t");

			for (int L = nStartLine; L <= nEndLine; L++)
			{
				_buffer.InsertText(ug, CPoint(0, L), pszText);
			}

			RecalcHorzScrollBar();
		}
		else
		{
			UndoGroup ug(_buffer);
			DeleteCurrentSelection(ug);

			auto location = _buffer.InsertText(ug, GetCursorPos(), L'\t');

			SetSelection(location, location);
			SetAnchor(location);
			SetCursorPos(location);
			EnsureVisible(location);
		}
	}
}

void TextView::OnEditUntab()
{
	if (QueryEditable())
	{
		bool bTabify = FALSE;
		CPoint ptSelStart, ptSelEnd;
		if (HasSelection())
		{
			GetSelection(ptSelStart, ptSelEnd);
			bTabify = ptSelStart.y != ptSelEnd.y;
		}

		if (bTabify)
		{
			UndoGroup ug(_buffer);

			CPoint ptSelStart, ptSelEnd;
			GetSelection(ptSelStart, ptSelEnd);
			int nStartLine = ptSelStart.y;
			int nEndLine = ptSelEnd.y;
			ptSelStart.x = 0;
			if (ptSelEnd.x > 0)
			{
				if (ptSelEnd.y == _buffer.LineCount() - 1)
				{
					ptSelEnd.x = _buffer[ptSelEnd.y].size();
				}
				else
				{
					ptSelEnd.x = 0;
					ptSelEnd.y++;
				}
			}
			else
				nEndLine--;
			SetSelection(ptSelStart, ptSelEnd);
			SetCursorPos(ptSelEnd);
			EnsureVisible(ptSelEnd);

			for (int L = nStartLine; L <= nEndLine; L++)
			{
				const auto &line = _buffer[L];

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
						_buffer.DeleteText(ug, CPoint(0, L), CPoint(nPos, L));
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
				int I = 0;

				while (nCurrentOffset < nNewOffset)
				{
					if (line[I] == _T('\t'))
					{
						nCurrentOffset = nCurrentOffset / tabSize * tabSize + tabSize;
					}
					else
					{
						nCurrentOffset++;
					}

					I++;
				}

				assert(nCurrentOffset == nNewOffset);

				ptCursorPos.x = I;
				SetSelection(ptCursorPos, ptCursorPos);
				SetAnchor(ptCursorPos);
				SetCursorPos(ptCursorPos);
				EnsureVisible(ptCursorPos);
			}
		}
	}
}

DROPEFFECT CEditDropTargetImpl::OnDragEnter(CWindow* pWnd, COleDataObject* pDataObject, DWORD dwKeyState, CPoint point)
{
	/*if (! pDataObject->IsDataAvailable(CF_TEXT))
	{
	m_pOwner->HideDropIndicator();
	return DROPEFFECT_NONE;
	}
	m_pOwner->ShowDropIndicator(point);
	if (dwKeyState & MK_CONTROL)
	return DROPEFFECT_COPY;*/
	return DROPEFFECT_MOVE;
}

void CEditDropTargetImpl::OnDragLeave(CWindow* pWnd)
{
	m_pOwner->HideDropIndicator();
}

DROPEFFECT CEditDropTargetImpl::OnDragOver(CWindow* pWnd, COleDataObject* pDataObject, DWORD dwKeyState, CPoint point)
{
	///*
	//	if (! pDataObject->IsDataAvailable(CF_TEXT))
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
	////	if ((pDataObject->IsDataAvailable( CF_TEXT ) ) ||	    // If Text Available
	////			( pDataObject -> IsDataAvailable( xxx ) ) ||	// Or xxx Available
	////			( pDataObject -> IsDataAvailable( yyy ) ) )		// Or yyy Available
	//	if (pDataObject->IsDataAvailable(CF_TEXT))		  	    // If Text Available
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

bool CEditDropTargetImpl::OnDrop(CWindow* pWnd, COleDataObject* pDataObject, DROPEFFECT dropEffect, CPoint point)
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
	////	if( ( pDataObject -> IsDataAvailable( CF_TEXT ) ) ||	// If Text Available
	////			( pDataObject -> IsDataAvailable( xxx ) ) ||	// Or xxx Available
	////			( pDataObject -> IsDataAvailable( yyy ) ) )		// Or yyy Available
	//	if (pDataObject->IsDataAvailable(CF_TEXT))			    // If Text Available
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

DROPEFFECT CEditDropTargetImpl::OnDragScroll(CWindow* pWnd, DWORD dwKeyState, CPoint point)
{
	assert(m_pOwner == pWnd);
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
	//HGLOBAL hData = pDataObject->GetGlobalData(CF_TEXT);
	//if (hData == nullptr)
	//	return FALSE;

	//CPoint ptDropPos = ClientToText(ptClient);
	//if (IsDraggingText() && IsInsideSelection(ptDropPos))
	//{
	//	SetAnchor(ptDropPos);
	//	SetSelection(ptDropPos, ptDropPos);
	//	SetCursorPos(ptDropPos);
	//	EnsureVisible(ptDropPos);
	//	return FALSE;
	//}

	//LPSTR pszText = (LPSTR) ::GlobalLock(hData);
	//if (pszText == nullptr)
	//	return FALSE;

	//int x, y;
	//_buffer.InsertText(this, ptDropPos.y, ptDropPos.x, A2T(pszText), y, x, CE_ACTION_DRAGDROP);
	//CPoint ptCurPos(x, y);
	//SetAnchor(ptDropPos);
	//SetSelection(ptDropPos, ptCurPos);
	//SetCursorPos(ptCurPos);
	//EnsureVisible(ptCurPos);

	//::GlobalUnlock(hData);
	return TRUE;
}


void TextView::ShowDropIndicator(const CPoint &point)
{
	if (!m_bDropPosVisible)
	{
		HideCursor();
		m_ptSavedCaretPos = GetCursorPos();
		m_bDropPosVisible = TRUE;
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
		m_bDropPosVisible = FALSE;
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
		_buffer.DeleteText(ug, m_ptDraggedTextBegin, m_ptDraggedTextEnd);
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
	//dlg.m_bMatchCase = pApp->GetProfileInt(REG_REPLACE_SUBKEY, REG_MATCH_CASE, FALSE);
	//dlg.m_bWholeWord = pApp->GetProfileInt(REG_REPLACE_SUBKEY, REG_WHOLE_WORD, FALSE);
	//dlg.m_sText = pApp->GetProfileString(REG_REPLACE_SUBKEY, REG_FIND_WHAT, _T(""));
	//dlg.m_sNewText = pApp->GetProfileString(REG_REPLACE_SUBKEY, REG_REPLACE_WITH, _T(""));

	//if (HasSelection())
	//{
	//	GetSelection(m_ptSavedSelStart, m_ptSavedSelEnd);
	//	m_bSelectionPushed = TRUE;

	//	dlg.m_nScope = 0;	//	Replace in current selection
	//	dlg.m_ptCurrentPos = m_ptSavedSelStart;
	//	dlg.m_bEnableScopeSelection = TRUE;
	//	dlg.m_ptBlockBegin = m_ptSavedSelStart;
	//	dlg.m_ptBlockEnd = m_ptSavedSelEnd;
	//}
	//else
	//{
	//	dlg.m_nScope = 1;	//	Replace in whole text
	//	dlg.m_ptCurrentPos = GetCursorPos();
	//	dlg.m_bEnableScopeSelection = FALSE;
	//}

	////	Execute Replace dialog
	//m_bShowInactiveSelection = TRUE;
	//dlg.DoModal();
	//m_bShowInactiveSelection = FALSE;

	////	Restore selection
	//if (m_bSelectionPushed)
	//{
	//	SetSelection(m_ptSavedSelStart, m_ptSavedSelEnd);
	//	m_bSelectionPushed = FALSE;
	//}

	////	Save search parameters to registry
	//pApp->WriteProfileInt(REG_REPLACE_SUBKEY, REG_MATCH_CASE, dlg.m_bMatchCase);
	//pApp->WriteProfileInt(REG_REPLACE_SUBKEY, REG_WHOLE_WORD, dlg.m_bWholeWord);
	//pApp->WriteProfileString(REG_REPLACE_SUBKEY, REG_FIND_WHAT, dlg.m_sText);
	//pApp->WriteProfileString(REG_REPLACE_SUBKEY, REG_REPLACE_WITH, dlg.m_sNewText);
}

bool TextView::ReplaceSelection(LPCTSTR pszNewText)
{
	//assert(pszNewText != nullptr);
	//if (! HasSelection())
	//	return FALSE;

	//DeleteCurrentSelection();

	//CPoint ptCursorPos = GetCursorPos();
	//int x, y;
	//_buffer.InsertText(this, ptCursorPos.y, ptCursorPos.x, pszNewText, y, x, CE_ACTION_REPLACE);
	//CPoint ptEndOfBlock = CPoint(x, y);
	//SetAnchor(ptEndOfBlock);
	//SetSelection(ptCursorPos, ptEndOfBlock);
	//SetCursorPos(ptEndOfBlock);
	//EnsureVisible(ptEndOfBlock);
	return TRUE;
}



void TextView::OnEditUndo()
{
	if (_buffer.CanUndo())
	{
		auto location = _buffer.Undo();
		SetAnchor(location);
		SetSelection(location, location);
		SetCursorPos(location);
		EnsureVisible(location);
	}
}


void TextView::OnEditRedo()
{
	if (_buffer.CanRedo())
	{
		auto location = _buffer.Redo();
		SetAnchor(location);
		SetSelection(location, location);
		SetCursorPos(location);
		EnsureVisible(location);
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
//			CPoint ptCursorPos = GetCursorPos();
//			assert(ptCursorPos.y > 0);
//
//			//	Take indentation from the previos line
//			const auto &line = _buffer.GetLineChars(ptCursorPos.y - 1);
//			const auto len = line.size();
//
//			int nPos = 0;
//			while (nPos < len && isspace(line[nPos]))
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
//				CPoint pt(x, y);
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

void TextView::SetOverwriteMode(bool bOvrMode /*= TRUE*/)
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

//void TextView::OnUpdateEditUndo(CCmdUI* pCmdUI)
//{
//	bool bCanUndo = _buffer.CanUndo();
//	pCmdUI->Enable(bCanUndo);
//
//	//	Since we need text only for menus...
//	//if (pCmdUI->m_pMenu != nullptr)
//	{
//		//	Tune up 'resource handle'
//		/*HINSTANCE hOldResHandle = AfxGetResourceHandle();
//		AfxSetResourceHandle(GetResourceHandle());*/
//
//		std::wstring menu;
//		if (bCanUndo)
//		{
//			//	Format menu item text using the provided item description
//			std::wstring desc;
//			_buffer.GetUndoDescription(desc);
//			menu.Format(IDS_MENU_UNDO_FORMAT, desc);
//		}
//		else
//		{
//			//	Just load default menu item text
//			menu.LoadString(IDS_MENU_UNDO_DEFAULT);
//		}
//
//		//	Restore original handle
//		//AfxSetResourceHandle(hOldResHandle);
//
//		//	Set menu item text
//		pCmdUI->SetText(menu);
//	}
//}

//void TextView::OnUpdateEditRedo(CCmdUI* pCmdUI)
//{
//	bool bCanRedo = _buffer.CanRedo();
//	pCmdUI->Enable(bCanRedo);
//
//	//	Since we need text only for menus...
//	//if (pCmdUI->m_pMenu != nullptr)
//	{
//		//	Tune up 'resource handle'
//		//HINSTANCE hOldResHandle = AfxGetResourceHandle();
//		//AfxSetResourceHandle(GetResourceHandle());
//
//		std::wstring menu;
//		if (bCanRedo)
//		{
//			//	Format menu item text using the provided item description
//			std::wstring desc;
//			_buffer.GetRedoDescription(desc);
//			menu.Format(IDS_MENU_REDO_FORMAT, desc);
//		}
//		else
//		{
//			//	Just load default menu item text
//			menu.LoadString(IDS_MENU_REDO_DEFAULT);
//		}
//
//		//	Restore original handle
//		//AfxSetResourceHandle(hOldResHandle);
//
//		//	Set menu item text
//		pCmdUI->SetText(menu);
//	}
//}

void TextView::EnableMenuItems(HMENU hMenu)
{
	auto count = GetMenuItemCount(hMenu);

	for (int i = 0; i < count; i++)
	{
		auto id = GetMenuItemID(hMenu, i);
		auto enable = true;

		switch (id)
		{
		case ID_EDIT_COPY: enable = m_ptSelStart != m_ptSelEnd; break;
		case ID_EDIT_SELECT_ALL: enable = true; break;
		case ID_EDIT_REPEAT: enable = m_bLastSearch; break;
		case ID_EDIT_FIND_PREVIOUS: enable = m_bLastSearch; break;
		case ID_EDIT_CUT: enable = HasSelection(); break;
		case ID_EDIT_PASTE: enable = TextInClipboard(); break;
		case ID_EDIT_UNDO: enable = _buffer.CanUndo(); break;
		case ID_EDIT_REDO: enable = _buffer.CanRedo(); break;
		}

		EnableMenuItem(hMenu, i, MF_BYPOSITION | (enable ? MF_ENABLED : MF_DISABLED));
	}
}
