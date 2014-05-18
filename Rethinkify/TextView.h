#pragma once

#include "resource.h"
#include "TextBuffer.h"
#include "SpellCheck.h"

struct CPrintInfo
{
	CRect m_rectDraw;
};

class COleDataObject;
class CEditDropTargetImpl;

typedef DWORD DROPEFFECT;

const int RETHINKIFY_TIMER_DRAGSEL = 1001;

class TextBuffer;

class IHighlight
{
public:

	enum
	{
		//	Base colors
		COLORINDEX_WHITESPACE,
		COLORINDEX_BKGND,
		COLORINDEX_NORMALTEXT,
		COLORINDEX_SELMARGIN,
		COLORINDEX_SELBKGND,
		COLORINDEX_SELTEXT,
		//	Syntax colors
		COLORINDEX_KEYWORD,
		COLORINDEX_COMMENT,
		COLORINDEX_NUMBER,
		COLORINDEX_OPERATOR,
		COLORINDEX_STRING,
		COLORINDEX_PREPROCESSOR,
		//	Compiler/debugger colors
		COLORINDEX_ERRORBKGND,
		COLORINDEX_ERRORTEXT,
		COLORINDEX_EXECUTIONBKGND,
		COLORINDEX_EXECUTIONTEXT,
		COLORINDEX_BREAKPOINTBKGND,
		COLORINDEX_BREAKPOINTTEXT
		//	...
		//	Expandable: custom elements are allowed.
	};

	struct TEXTBLOCK
	{
		size_t	m_nCharPos;
		int m_nColorIndex;
	};

	virtual DWORD ParseLine(DWORD dwCookie, const TextBuffer::Line &line, TEXTBLOCK *pBuf, int &nActualItems) const = 0;
	virtual std::vector<std::wstring> Suggest(const std::wstring &wword) const = 0;
	virtual bool CanAdd(const std::wstring &word) const { return false; };
	virtual void AddWord(const std::wstring &word) const { };	
};

class CppSyntax : public IHighlight
{
	DWORD ParseLine(DWORD dwCookie, const TextBuffer::Line &line, TEXTBLOCK *pBuf, int &nActualItems) const;
	std::vector<std::wstring> Suggest(const std::wstring &wword) const { return std::vector<std::wstring>();  };
};

class TextHighight : public IHighlight
{
	mutable SpellCheck _check;

	DWORD ParseLine(DWORD dwCookie, const TextBuffer::Line &line, TEXTBLOCK *pBuf, int &nActualItems) const;
	std::vector<std::wstring> Suggest(const std::wstring &wword) const;
	bool CanAdd(const std::wstring &word) const { return !_check.WordValid(word.c_str(), word.size()); };
	void AddWord(const std::wstring &word) const { _check.AddWord(word); };
};

class TextView : public CWindowImpl<TextView>, public IView
{


private:

	CEditDropTargetImpl *_dropTarget;
	CRect m_ptPageArea, m_rcPrintArea;
	DWORD m_dwLastSearchFlags;
	HBITMAP _backBuffer;
	HFONT m_pPrintFont;
	LOGFONT _font;
	TextBuffer &_buffer;
	TextLocation m_ptAnchor;
	TextLocation m_ptCursorPos;
	TextLocation m_ptDropPos;
	TextLocation m_ptSavedCaretPos;
	TextSelection _selection;
	TextSelection m_ptDraggedText;
	TextSelection m_ptSavedSel;
	UINT m_nDragSelTimer;
	bool _overtype;
	bool m_bAutoIndent;
	bool m_bCursorHidden;
	bool m_bDisableDragAndDrop;
	bool m_bDragSelection, m_bWordSelection, m_bLineSelection;
	bool m_bDraggingText;
	bool m_bDropPosVisible;
	bool m_bFocused;
	bool m_bLastSearch;
	bool m_bMultipleSearch;
	bool m_bPreparingToDrag;
	bool m_bPrintHeader, m_bPrintFooter;
	bool m_bSelMargin;
	bool m_bSelectionPushed;
	bool m_bShowInactiveSelection;
	bool m_bViewTabs;
	int m_nIdealCharPos;
	int m_nPrintLineHeight;
	int m_nTopLine, m_nOffsetChar;
	int m_tabSize;
	mutable std::vector<int> _actualLineLengths;
	mutable HFONT m_apFonts[4];
	mutable TextSelection m_ptDrawSel;
	mutable int m_nLineHeight, m_nCharWidth;
	mutable int m_nMaxLineLength;
	mutable int m_nScreenLines, m_nScreenChars;
	mutable std::vector<DWORD> _parseCookies;
	std::vector<int> _pages;
	std::wstring _lastFindWhat;
	std::shared_ptr<IHighlight> _highlight;

	int GetMarginWidth() const;
	COLORREF GetColor(int nColorIndex) const;
	TextLocation ClientToText(const CPoint &point) const;
	const TextLocation &GetCursorPos() const { return m_ptCursorPos; };
	CPoint TextToClient(const TextLocation &point) const;
	TextLocation WordToLeft(TextLocation pt) const;
	TextLocation WordToRight(TextLocation pt) const;
	DROPEFFECT GetDropEffect();
	DWORD GetParseCookie(int lineIndex) const;	
	HFONT GetFont(bool bItalic = false, bool bBold = false) const;
	HGLOBAL PrepareDragData();
	HINSTANCE GetResourceHandle();
	bool HasSelection() const { return !_selection.empty(); };
	bool GetBold(int nColorIndex) const;
	bool GetDisableDragAndDrop() const;
	std::wstring GetFromClipboard() const;
	bool GetItalic(int nColorIndex) const;
	bool GetSelectionMargin() const;
	bool GetViewTabs() const;
	bool HighlightText(const TextLocation &ptStartPos, int nLength);
	bool IsInsideSelBlock(TextLocation ptTextPos) const;
	bool IsInsideSelection(const TextLocation &ptTextPos) const;
	bool OnPreparePrinting(CPrintInfo* pInfo);
	bool PutToClipboard(const std::wstring &text);
	bool TextInClipboard();
	int ApproxActualOffset(int lineIndex, int nOffset);
	int CalculateActualOffset(int lineIndex, int nCharIndex);
	int GetCharWidth() const;
	int GetLineActualLength(int lineIndex) const;
	int GetLineHeight() const;
	int GetMaxLineLength() const;
	int GetScreenChars() const;
	int GetScreenLines() const;
	int GetTabSize() const;
	int PrintLineHeight(HDC pdc, int nLine);
	std::wstring ExpandChars(const std::wstring &text, int nOffset, int nCount) const;
	std::vector<std::wstring> Text(const TextSelection &selection) const;
	void CalcLineCharDim() const;
	void Copy();
	void DrawLineHelper(HDC pdc, TextLocation &ptOrigin, const CRect &rcClip, int nColorIndex, const wchar_t * pszChars, int nOffset, int nCount, TextLocation ptTextPos) const;
	void DrawLineHelperImpl(HDC pdc, TextLocation &ptOrigin, const CRect &rcClip, const wchar_t * pszChars, int nOffset, int nCount) const;
	void DrawMargin(HDC pdc, const CRect &rect, int lineIndex) const;
	void DrawSingleLine(HDC pdc, const CRect &rect, int lineIndex) const;
	void EnsureVisible(TextLocation pt);
	void GetLineColors(int lineIndex, COLORREF &crBkgnd, COLORREF &crText, bool &bDrawWhitespace) const;
	std::wstring GetPrintFooterText(int nPageNum) const;
	std::wstring GetPrintHeaderText(int nPageNum) const;
	const TextSelection &GetSelection() const;
	void HideCursor();
	void InvalidateLine(int index);
	void InvalidateLines(int nLine1, int nLine2, bool bInvalidateMargin = false);	
	void MoveCtrlEnd(bool select);
	void MoveCtrlHome(bool select);
	void MoveDown(bool select);
	void MoveEnd(bool select);
	void MoveHome(bool select);
	void MoveLeft(bool select);
	void MovePgDn(bool select);
	void MovePgUp(bool select);
	void MoveRight(bool select);
	void MoveUp(bool select);
	void MoveWordLeft(bool select);
	void MoveWordRight(bool select);
	void OnBeginPrinting(HDC pDC, CPrintInfo* pInfo);
	void OnDraw(HDC pDC);  // overridden to draw this view
	void OnDropSource(DROPEFFECT de);
	void OnEndPrinting(HDC pDC, CPrintInfo* pInfo);
	void OnPrepareDC(HDC pDC, CPrintInfo* pInfo = nullptr);
	void OnPrint(HDC pDC, CPrintInfo* pInfo);
	void PrepareSelBounds() const;
	void PrintFooter(HDC pdc, int nPageNum) const;
	void PrintHeader(HDC pdc, int nPageNum) const;
	void RecalcHorzScrollBar(bool bPositionOnly = false);
	void RecalcPageLayouts(HDC pdc, CPrintInfo *pInfo);
	void RecalcVertScrollBar(bool bPositionOnly = false);
	void ResetView();
	void ScrollToChar(int nNewOffsetChar, bool bTrackScrollBar = true);
	void ScrollToLine(int nNewTopLine, bool bTrackScrollBar = true);
	void SelectAll();
	void SetAnchor(const TextLocation &ptNewAnchor);
	void SetCursorPos(const TextLocation &ptCursorPos);
	void SetDisableDragAndDrop(bool bDDAD);
	void SetSelection(const TextSelection &sel);
	void SetSelectionMargin(bool bSelMargin);
	void SetTabSize(int nTabSize);
	void SetViewTabs(bool bViewTabs);
	void ShowCursor();
	void UpdateCaret();
	
	TextSelection WordSelection() const
	{
		TextLocation ptStart, ptEnd;

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

		return TextSelection(ptStart, ptEnd);
	}

	void Select(const TextSelection &selection)
	{
		SetSelection(selection);
		SetAnchor(selection._start);
		SetCursorPos(selection._end);
		EnsureVisible(selection._end);
		UpdateCaret();
	}

	void Locate(const TextLocation &pos)
	{
		SetSelection(TextSelection(pos, pos));
		SetAnchor(pos);
		SetCursorPos(pos);
		EnsureVisible(pos);
	}

public:

	TextView(TextBuffer &buffer);
	~TextView();

	void InvalidateView();
	void HighlightFromExtension(const wchar_t *ext);

	BEGIN_MSG_MAP(TextView)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		MESSAGE_HANDLER(WM_SETFOCUS, OnSetFocus)
		MESSAGE_HANDLER(WM_KILLFOCUS, OnKillFocus)
		MESSAGE_HANDLER(WM_PAINT, OnPaint)
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
		MESSAGE_HANDLER(WM_VSCROLL, OnVScroll)
		MESSAGE_HANDLER(WM_HSCROLL, OnHScroll)
		MESSAGE_HANDLER(WM_TIMER, OnTimer)
		MESSAGE_HANDLER(WM_LBUTTONDBLCLK, OnLButtonDblClk)
		MESSAGE_HANDLER(WM_LBUTTONDOWN, OnLButtonDown)
		MESSAGE_HANDLER(WM_LBUTTONUP, OnLButtonUp)
		MESSAGE_HANDLER(WM_MOUSEMOVE, OnMouseMove)
		MESSAGE_HANDLER(WM_MOUSEWHEEL, OnMouseWheel)
		MESSAGE_HANDLER(WM_CHAR, OnChar)
		MESSAGE_HANDLER(WM_CONTEXTMENU, OnContextMenu)
	END_MSG_MAP()

	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
	{
		OnCreate();
		return 0;
	}

	LRESULT OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
	{
		OnDestroy();
		return 0;
	}

	LRESULT OnSize(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/)
	{
		OnSize(wParam, LOWORD(lParam), HIWORD(lParam));
		return 0;
	}

	LRESULT OnPaint(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/)
	{
		PAINTSTRUCT ps = { 0 };
		auto hdc = BeginPaint(&ps);
		OnDraw(hdc);
		EndPaint(&ps);
		return 0;
	}

	LRESULT OnEraseBkgnd(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/)
	{
		return OnEraseBkgnd((HDC) wParam);
	}

	LRESULT OnTimer(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& /*bHandled*/)
	{
		OnTimer(wParam);
		return 0;
	}

	LRESULT OnSetFocus(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/)
	{
		OnSetFocus((HWND) wParam);
		return 0;
	}

	LRESULT OnKillFocus(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/)
	{
		OnKillFocus((HWND) wParam);
		return 0;
	}

	LRESULT OnVScroll(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/)
	{
		auto nScrollCode = (int) LOWORD(wParam);
		auto nPos = (short int) HIWORD(wParam);
		auto hwndScrollBar = (HWND) lParam;
		OnVScroll(nScrollCode, nPos, hwndScrollBar);
		return 0;
	}

	LRESULT OnHScroll(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/)
	{
		auto nScrollCode = (int) LOWORD(wParam);
		auto nPos = (short int) HIWORD(wParam);
		auto hwndScrollBar = (HWND) lParam;
		OnHScroll(nScrollCode, nPos, hwndScrollBar);
		return 0;
	}

	LRESULT OnLButtonDblClk(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/)
	{
		OnLButtonDblClk(CPoint(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)), wParam);
		return 0;
	}

	LRESULT OnLButtonDown(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/)
	{
		OnLButtonDown(CPoint(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)), wParam);
		return 0;
	}

	LRESULT OnLButtonUp(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/)
	{
		OnLButtonUp(CPoint(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)), wParam);
		return 0;
	}

	LRESULT OnMouseMove(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/)
	{
		OnMouseMove(CPoint(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)), wParam);
		return 0;
	}

	LRESULT OnMouseWheel(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/)
	{
		auto delta = ((short)HIWORD(wParam)) > 0 ? -2 : 2;
		OnMouseWheel(CPoint(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)), delta);
		return 0;
	}

	LRESULT OnChar(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/)
	{
		OnChar(wParam, 0xFF & lParam, lParam);
		return 0;
	}

	LRESULT OnCommand(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/)
	{		
		OnCommand(LOWORD(wParam));
		return 0;
	}

	LRESULT OnContextMenu(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
	{
		OnContextMenu(CPoint(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)), wParam);
		return 0;
	}

	void OnCommand(int id)
	{
		switch (id)
		{
		case ID_EDIT_COPY: Copy(); break;
		case ID_EDIT_SELECT_ALL: SelectAll(); break;
		//case ID_EDIT_FIND: OnEditFind(); break;
		case ID_EDIT_REPEAT: OnEditRepeat(); break;
		case ID_EDIT_FIND_PREVIOUS: OnEditFindPrevious(); break;
		case ID_EDIT_CHAR_LEFT: MoveLeft(false); break;
		case ID_EDIT_EXT_CHAR_LEFT: MoveLeft(true); break;
		case ID_EDIT_CHAR_RIGHT: MoveRight(false); break;
		case ID_EDIT_EXT_CHAR_RIGHT: MoveRight(true); break;
		case ID_EDIT_WORD_LEFT: MoveWordLeft(false); break;
		case ID_EDIT_EXT_WORD_LEFT: MoveWordLeft(true); break;
		case ID_EDIT_WORD_RIGHT: MoveWordRight(false); break;
		case ID_EDIT_EXT_WORD_RIGHT: MoveWordRight(true); break;
		case ID_EDIT_LINE_UP: MoveUp(false); break;
		case ID_EDIT_EXT_LINE_UP: MoveUp(true); break;
		case ID_EDIT_LINE_DOWN: MoveDown(false); break;
		case ID_EDIT_EXT_LINE_DOWN: MoveDown(true); break;
		case ID_EDIT_SCROLL_UP: ScrollUp(); break;
		case ID_EDIT_SCROLL_DOWN: ScrollDown(); break;
		case ID_EDIT_PAGE_UP: MovePgUp(false); break;
		case ID_EDIT_EXT_PAGE_UP: MovePgDn(true); break;
		case ID_EDIT_PAGE_DOWN: MovePgDn(false); break;
		case ID_EDIT_EXT_PAGE_DOWN: MovePgDn(true); break;
		case ID_EDIT_LINE_END: MoveEnd(false); break;
		case ID_EDIT_EXT_LINE_END: MoveEnd(true); break;
		case ID_EDIT_HOME: MoveHome(false); break;
		case ID_EDIT_EXT_HOME: MoveHome(true); break;
		case ID_EDIT_TEXT_BEGIN: MoveCtrlHome(false); break;
		case ID_EDIT_EXT_TEXT_BEGIN: MoveCtrlHome(true); break;
		case ID_EDIT_TEXT_END: MoveCtrlEnd(false); break;
		case ID_EDIT_EXT_TEXT_END: MoveCtrlEnd(true); break;
		case ID_FILE_PAGE_SETUP: OnFilePageSetup(); break;
			//case ID_FILE_PRINT: OnFilePrint(); break;
			//case ID_FILE_PRINT_DIRECT: OnFilePrint(); break;
			//case ID_FILE_PRINT_PREVIEW: OnFilePrintPreview(); break;
		case ID_EDIT_PASTE: Paste(); break;
		case ID_EDIT_CUT: Cut(); break;
		case ID_EDIT_DELETE: OnEditDelete(); break;
		case ID_EDIT_DELETE_BACK: OnEditDeleteBack(); break;
		case ID_EDIT_UNTAB: OnEditUntab(); break;
		case ID_EDIT_TAB: OnEditTab(); break;
		case ID_EDIT_REPLACE: OnEditReplace(); break;
		case ID_EDIT_UNDO: OnEditUndo(); break;
		case ID_EDIT_REDO: OnEditRedo(); break;
		}
	}
	
	bool DoDropText(COleDataObject *pDataObject, const CPoint &ptClient);
	bool GetAutoIndent() const;
	bool GetOverwriteMode() const;
	bool OnEraseBkgnd(HDC pDC);
	bool OnSetCursor(CWindow wnd, UINT nHitTest, UINT message);
	bool QueryEditable();
	bool ReplaceSelection(const wchar_t * pszNewText);
	int OnCreate();
	void Cut();
	void DoDragScroll(const CPoint &point);
	void EnableMenuItems(HMENU h);
	void HideDropIndicator();
	void OnChar(UINT nChar, UINT nRepCnt, UINT nFlags);
	void OnDestroy();
	void OnEditDelete();
	void OnEditDeleteBack();
	//void OnEditFind() { Find(_lastFindWhat); };
	void Find(const std::wstring &text, DWORD flags);
	void OnEditFindPrevious();
	void OnEditRedo();
	void OnEditRepeat();
	void OnEditReplace();
	void OnEditTab();
	void OnEditUndo();
	void OnEditUntab();
	void OnFilePageSetup();
	void OnHScroll(UINT nSBCode, UINT nPos, HWND pScrollBar);
	void OnKillFocus(CWindow newWnd);
	void OnContextMenu(const CPoint &point, UINT nFlags);
	void OnLButtonDblClk(const CPoint &point, UINT nFlags);
	void OnLButtonDown(const CPoint &point, UINT nFlags);
	void OnLButtonUp(const CPoint &point, UINT nFlags);
	void OnMouseMove(const CPoint &point, UINT nFlags);
	void OnMouseWheel(const CPoint &point, int zDelta);
	void OnRButtonDown(const CPoint &point, UINT nFlags);
	void OnSetFocus(CWindow oldWnd);
	void OnSize(UINT nType, int cx, int cy);
	void OnSysColorChange();
	void OnTimer(UINT nIDEvent);
	void OnVScroll(UINT nSBCode, UINT nPos, HWND pScrollBar);
	void Paste();
	void ScrollDown();
	void ScrollLeft();
	void ScrollRight();
	void ScrollUp();
	void SetAutoIndent(bool bAutoIndent);
	void SetOverwriteMode(bool bOvrMode = true);
	void ShowDropIndicator(const CPoint &point);
};

