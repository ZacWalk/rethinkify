#pragma once

#include "resource.h"
#include "TextBuffer.h"

class CPrintInfo;
class COleDataObject;
class CEditDropTargetImpl;

typedef DWORD DROPEFFECT;

const int RETHINKIFY_TIMER_DRAGSEL = 1001;

class TextBuffer;

class TextView : public CWindowImpl<TextView>, public IView
{

private:



	//	Syntax coloring overrides
	struct TEXTBLOCK
	{
		size_t	m_nCharPos;
		int m_nColorIndex;
	};



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


	CPoint	m_ptDropPos;
	CPoint	m_ptSavedCaretPos;
	CPoint	m_ptSavedSelStart, m_ptSavedSelEnd;
	CPoint m_ptAnchor;
	CPoint m_ptCursorPos;
	CPoint m_ptDraggedTextBegin, m_ptDraggedTextEnd;
	CPoint m_ptDrawSelStart, m_ptDrawSelEnd;
	CPoint m_ptSelStart, m_ptSelEnd;
	CRect m_ptPageArea, m_rcPrintArea;
	DWORD m_dwLastSearchFlags;
	HACCEL m_hAccel;
	HBITMAP m_pCacheBitmap;
	HFONT m_apFonts[4];
	HFONT m_pPrintFont;
	LOGFONT m_lfBaseFont;
	TextBuffer &_buffer;
	UINT m_nDragSelTimer;
	bool m_bAutoIndent;
	bool m_bDropPosVisible;
	bool _overtype;
	bool m_bSelectionPushed;
	bool m_bCursorHidden;
	bool m_bDisableDragAndDrop;
	bool m_bDragSelection, m_bWordSelection, m_bLineSelection;
	bool m_bDraggingText;
	bool m_bFocused;
	bool m_bLastSearch;
	bool m_bMultipleSearch;         // More search  
	bool m_bPreparingToDrag;
	bool m_bPrintHeader, m_bPrintFooter;
	bool m_bSelMargin;
	bool m_bShowInactiveSelection;
	bool m_bVertScrollBarLocked, m_bHorzScrollBarLocked;
	bool m_bViewTabs;
	int *m_pnPages;
	int GetMarginWidth() const;
	int m_nIdealCharPos;
	int m_nPrintLineHeight;
	int m_nPrintPages;
	int m_nTopLine, m_nOffsetChar;
	int m_tabSize;
	mutable  std::vector<int> _actualLineLengths;
	mutable int m_nLineHeight, m_nCharWidth;
	mutable int m_nMaxLineLength;
	mutable int m_nScreenLines, m_nScreenChars;
	mutable std::vector<DWORD> _parseCookies;
	std::wstring _lastFindWhat;


	COLORREF GetColor(int nColorIndex) const;
	CPoint AdjustTextPoint(const CPoint &point) const;
	CPoint ClientToText(const CPoint &point);
	const CPoint &GetCursorPos() const { return m_ptCursorPos; };
	CPoint TextToClient(const CPoint &point);
	CPoint WordToLeft(CPoint pt);
	CPoint WordToRight(CPoint pt);
	DROPEFFECT GetDropEffect();
	DWORD GetParseCookie(int nLineIndex);
	DWORD ParseLine(DWORD dwCookie, int nLineIndex, TEXTBLOCK *pBuf, int &nActualItems);
	HFONT GetFont(bool bItalic = FALSE, bool bBold = FALSE);
	HGLOBAL PrepareDragData();
	HINSTANCE GetResourceHandle();
	bool HasSelection() const { return m_ptSelStart != m_ptSelEnd; };
	bool DeleteCurrentSelection(UndoGroup &ug);
	bool GetBold(int nColorIndex);
	bool GetDisableDragAndDrop() const;
	bool GetFromClipboard(std::wstring &text) const;
	bool GetItalic(int nColorIndex);
	bool GetSelectionMargin() const;
	bool GetViewTabs() const;
	bool HighlightText(const CPoint &ptStartPos, int nLength);
	bool IsInsideSelBlock(CPoint ptTextPos);
	bool IsInsideSelection(const CPoint &ptTextPos);
	bool IsSelection();
	bool OnPreparePrinting(CPrintInfo* pInfo);
	bool PreTranslateMessage(MSG* pMsg);
	bool PutToClipboard(const std::wstring &text);
	bool TextInClipboard();
	int ApproxActualOffset(int nLineIndex, int nOffset);
	int CalculateActualOffset(int nLineIndex, int nCharIndex);
	int GetCharWidth() const;
	int GetLineActualLength(int nLineIndex) const;
	int GetLineHeight() const;
	int GetMaxLineLength() const;
	int GetScreenChars() const;
	int GetScreenLines() const;
	int GetTabSize() const;
	int PrintLineHeight(HDC pdc, int nLine);
	std::wstring ExpandChars(const std::wstring &text, int nOffset, int nCount);
	std::vector<std::wstring> Text(const CPoint &ptStart, const CPoint &ptEnd) const;
	void CalcLineCharDim() const;
	void Copy();
	void DrawLineHelper(HDC pdc, CPoint &ptOrigin, const CRect &rcClip, int nColorIndex, LPCTSTR pszChars, int nOffset, int nCount, CPoint ptTextPos);
	void DrawLineHelperImpl(HDC pdc, CPoint &ptOrigin, const CRect &rcClip, LPCTSTR pszChars, int nOffset, int nCount);
	void DrawMargin(HDC pdc, const CRect &rect, int nLineIndex);
	void DrawSingleLine(HDC pdc, const CRect &rect, int nLineIndex);
	void EnsureVisible(CPoint pt);
	void GetFont(LOGFONT &lf);
	void GetLineColors(int nLineIndex, COLORREF &crBkgnd, COLORREF &crText, bool &bDrawWhitespace);
	void GetPrintFooterText(int nPageNum, std::wstring &text);
	void GetPrintHeaderText(int nPageNum, std::wstring &text);
	void GetSelection(CPoint &ptStart, CPoint &ptEnd);
	void HideCursor();
	void InvalidateLine(int index);
	void InvalidateLines(int nLine1, int nLine2, bool bInvalidateMargin = FALSE);	
	void MoveCtrlEnd(bool bSelect);
	void MoveCtrlHome(bool bSelect);
	void MoveDown(bool bSelect);
	void MoveEnd(bool bSelect);
	void MoveHome(bool bSelect);
	void MoveLeft(bool bSelect);
	void MovePgDn(bool bSelect);
	void MovePgUp(bool bSelect);
	void MoveRight(bool bSelect);
	void MoveUp(bool bSelect);
	void MoveWordLeft(bool bSelect);
	void MoveWordRight(bool bSelect);
	void OnBeginPrinting(HDC pDC, CPrintInfo* pInfo);
	void OnDraw(HDC pDC);  // overridden to draw this view
	void OnDropSource(DROPEFFECT de);
	void OnEndPrinting(HDC pDC, CPrintInfo* pInfo);
	void OnPrepareDC(HDC pDC, CPrintInfo* pInfo = nullptr);
	void OnPrint(HDC pDC, CPrintInfo* pInfo);
	void PrepareSelBounds();
	void PrintFooter(HDC pdc, int nPageNum);
	void PrintHeader(HDC pdc, int nPageNum);
	void RecalcHorzScrollBar(bool bPositionOnly = FALSE);
	void RecalcPageLayouts(HDC pdc, CPrintInfo *pInfo);
	void RecalcVertScrollBar(bool bPositionOnly = FALSE);
	void ResetView();
	void ScrollToChar(int nNewOffsetChar, bool bTrackScrollBar = true);
	void ScrollToLine(int nNewTopLine, bool bTrackScrollBar = true);
	void SelectAll();
	void SetAnchor(const CPoint &ptNewAnchor);
	void SetCursorPos(const CPoint &ptCursorPos);
	void SetDisableDragAndDrop(bool bDDAD);
	void SetSelection(const CPoint &ptStart, const CPoint &ptEnd);
	void SetSelectionMargin(bool bSelMargin);
	void SetTabSize(int nTabSize);
	void SetViewTabs(bool bViewTabs);
	void ShowCursor();
	void UpdateCaret();

public:

	TextView(TextBuffer &buffer);
	~TextView();

	void SetFont(const LOGFONT &lf);
	void InvalidateView();


	BEGIN_MSG_MAP(TextView)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		MESSAGE_HANDLER(WM_SETFOCUS, OnSetFocus)
		MESSAGE_HANDLER(WM_KILLFOCUS, OnKillFocus)
		MESSAGE_HANDLER(WM_PAINT, OnPaint)
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
		MESSAGE_HANDLER(WM_VSCROLL, OnVScroll)
		MESSAGE_HANDLER(WM_VSCROLL, OnHScroll)
		MESSAGE_HANDLER(WM_TIMER, OnTimer)
		MESSAGE_HANDLER(WM_LBUTTONDOWN, OnLButtonDown)
		MESSAGE_HANDLER(WM_LBUTTONUP, OnLButtonUp)
		MESSAGE_HANDLER(WM_MOUSEMOVE, OnMouseMove)
		MESSAGE_HANDLER(WM_MOUSEWHEEL, OnMouseWheel)
		MESSAGE_HANDLER(WM_CHAR, OnChar)
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
		CWindow w = (HWND) wParam;
		OnSetFocus(&w);
		return 0;
	}

	LRESULT OnKillFocus(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/)
	{
		CWindow w = (HWND) wParam;
		OnKillFocus(&w);
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

	void OnCommand(int id)
	{
		switch (id)
		{
		case ID_EDIT_COPY: OnEditCopy(); break;
		case ID_EDIT_SELECT_ALL: OnEditSelectAll(); break;
		case ID_EDIT_FIND: OnEditFind(); break;
		case ID_EDIT_REPEAT: OnEditRepeat(); break;
		case ID_EDIT_FIND_PREVIOUS: OnEditFindPrevious(); break;
		case ID_EDIT_CHAR_LEFT: OnCharLeft(); break;
		case ID_EDIT_EXT_CHAR_LEFT: OnExtCharLeft(); break;
		case ID_EDIT_CHAR_RIGHT: OnCharRight(); break;
		case ID_EDIT_EXT_CHAR_RIGHT: OnExtCharRight(); break;
		case ID_EDIT_WORD_LEFT: OnWordLeft(); break;
		case ID_EDIT_EXT_WORD_LEFT: OnExtWordLeft(); break;
		case ID_EDIT_WORD_RIGHT: OnWordRight(); break;
		case ID_EDIT_EXT_WORD_RIGHT: OnExtWordRight(); break;
		case ID_EDIT_LINE_UP: OnLineUp(); break;
		case ID_EDIT_EXT_LINE_UP: OnExtLineUp(); break;
		case ID_EDIT_LINE_DOWN: OnLineDown(); break;
		case ID_EDIT_EXT_LINE_DOWN: OnExtLineDown(); break;
		case ID_EDIT_SCROLL_UP: ScrollUp(); break;
		case ID_EDIT_SCROLL_DOWN: ScrollDown(); break;
		case ID_EDIT_PAGE_UP: OnPageUp(); break;
		case ID_EDIT_EXT_PAGE_UP: OnExtPageUp(); break;
		case ID_EDIT_PAGE_DOWN: OnPageDown(); break;
		case ID_EDIT_EXT_PAGE_DOWN: OnExtPageDown(); break;
		case ID_EDIT_LINE_END: OnLineEnd(); break;
		case ID_EDIT_EXT_LINE_END: OnExtLineEnd(); break;
		case ID_EDIT_HOME: OnHome(); break;
		case ID_EDIT_EXT_HOME: OnExtHome(); break;
		case ID_EDIT_TEXT_BEGIN: OnTextBegin(); break;
		case ID_EDIT_EXT_TEXT_BEGIN: OnExtTextBegin(); break;
		case ID_EDIT_TEXT_END: OnTextEnd(); break;
		case ID_EDIT_EXT_TEXT_END: OnExtTextEnd(); break;
		case ID_FILE_PAGE_SETUP: OnFilePageSetup(); break;
			//case ID_FILE_PRINT: OnFilePrint(); break;
			//case ID_FILE_PRINT_DIRECT: OnFilePrint(); break;
			//case ID_FILE_PRINT_PREVIEW: OnFilePrintPreview(); break;
		case ID_EDIT_PASTE: OnEditPaste(); break;
		case ID_EDIT_CUT: OnEditCut(); break;
		case ID_EDIT_DELETE: OnEditDelete(); break;
		case ID_EDIT_DELETE_BACK: OnEditDeleteBack(); break;
		case ID_EDIT_UNTAB: OnEditUntab(); break;
		case ID_EDIT_TAB: OnEditTab(); break;
		case ID_EDIT_REPLACE: OnEditReplace(); break;
		case ID_EDIT_UNDO: OnEditUndo(); break;
		case ID_EDIT_REDO: OnEditRedo(); break;
		}
	}

	void OnDestroy();
	bool OnEraseBkgnd(HDC pDC);
	void OnSize(UINT nType, int cx, int cy);
	void OnVScroll(UINT nSBCode, UINT nPos, HWND pScrollBar);
	bool OnSetCursor(CWindow* pWnd, UINT nHitTest, UINT message);
	void OnLButtonDown(const CPoint &point, UINT nFlags);
	void OnSetFocus(CWindow* pOldWnd);
	void OnHScroll(UINT nSBCode, UINT nPos, HWND pScrollBar);
	void OnLButtonUp(const CPoint &point, UINT nFlags);
	void OnMouseMove(const CPoint &point, UINT nFlags);
	void OnMouseWheel(const CPoint &point, int zDelta);
	void OnTimer(UINT nIDEvent);
	void OnKillFocus(CWindow* pNewWnd);
	void OnLButtonDblClk(const CPoint &point, UINT nFlags);
	void OnEditCopy();
	void OnEditSelectAll();
	void OnChar(UINT nChar, UINT nRepCnt, UINT nFlags);
	void OnRButtonDown(const CPoint &point, UINT nFlags);
	void OnSysColorChange();
	int OnCreate();

	void OnEditFind();
	void OnEditRepeat();
	void OnEditFindPrevious();
	void OnFilePageSetup();
	void OnCharLeft();
	void OnExtCharLeft();
	void OnCharRight();
	void OnExtCharRight();
	void OnWordLeft();
	void OnExtWordLeft();
	void OnWordRight();
	void OnExtWordRight();
	void OnLineUp();
	void OnExtLineUp();
	void OnLineDown();
	void OnExtLineDown();
	void OnPageUp();
	void OnExtPageUp();
	void OnPageDown();
	void OnExtPageDown();
	void OnLineEnd();
	void OnExtLineEnd();
	void OnHome();
	void OnExtHome();
	void OnTextBegin();
	void OnExtTextBegin();
	void OnTextEnd();
	void OnExtTextEnd();

	void ScrollUp();
	void ScrollDown();
	void ScrollLeft();
	void ScrollRight();

protected:
	CEditDropTargetImpl *m_pDropTarget;
	void Paste();
	void Cut();

public:

	bool GetAutoIndent() const;
	void SetAutoIndent(bool bAutoIndent);

	bool GetOverwriteMode() const;
	void SetOverwriteMode(bool bOvrMode = TRUE);

	void ShowDropIndicator(const CPoint &point);
	void HideDropIndicator();

	bool DoDropText(COleDataObject *pDataObject, const CPoint &ptClient);
	void DoDragScroll(const CPoint &point);

	bool QueryEditable();

	bool ReplaceSelection(LPCTSTR pszNewText);
	void EnableMenuItems(HMENU h);


protected:

	void OnEditPaste();
	void OnEditCut();
	void OnEditDelete();
	void OnEditDeleteBack();
	void OnEditUntab();
	void OnEditTab();
	void OnEditReplace();
	void OnEditUndo();
	void OnEditRedo();

};

