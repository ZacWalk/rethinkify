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


	TextLocation m_ptDropPos;
	TextLocation m_ptSavedCaretPos;	
	TextLocation m_ptAnchor;
	TextLocation m_ptCursorPos;

	TextSelection m_ptSavedSel;
	TextSelection m_ptDraggedText;
	mutable TextSelection m_ptDrawSel;
	TextSelection _selection;

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
	CEditDropTargetImpl *m_pDropTarget;


	COLORREF GetColor(int nColorIndex) const;
	TextLocation ClientToText(const CPoint &point);
	const TextLocation &GetCursorPos() const { return m_ptCursorPos; };
	CPoint TextToClient(const TextLocation &point);
	TextLocation WordToLeft(TextLocation pt);
	TextLocation WordToRight(TextLocation pt);
	DROPEFFECT GetDropEffect();
	DWORD GetParseCookie(int nLineIndex);
	DWORD ParseLine(DWORD dwCookie, int nLineIndex, TEXTBLOCK *pBuf, int &nActualItems);
	HFONT GetFont(bool bItalic = FALSE, bool bBold = FALSE);
	HGLOBAL PrepareDragData();
	HINSTANCE GetResourceHandle();
	bool HasSelection() const { return !_selection.empty(); };
	bool DeleteCurrentSelection(UndoGroup &ug);
	bool GetBold(int nColorIndex);
	bool GetDisableDragAndDrop() const;
	bool GetFromClipboard(std::wstring &text) const;
	bool GetItalic(int nColorIndex);
	bool GetSelectionMargin() const;
	bool GetViewTabs() const;
	bool HighlightText(const TextLocation &ptStartPos, int nLength);
	bool IsInsideSelBlock(TextLocation ptTextPos);
	bool IsInsideSelection(const TextLocation &ptTextPos);
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
	std::vector<std::wstring> Text(const TextSelection &selection) const;
	void CalcLineCharDim() const;
	void Copy();
	void DrawLineHelper(HDC pdc, TextLocation &ptOrigin, const CRect &rcClip, int nColorIndex, LPCTSTR pszChars, int nOffset, int nCount, TextLocation ptTextPos);
	void DrawLineHelperImpl(HDC pdc, TextLocation &ptOrigin, const CRect &rcClip, LPCTSTR pszChars, int nOffset, int nCount);
	void DrawMargin(HDC pdc, const CRect &rect, int nLineIndex);
	void DrawSingleLine(HDC pdc, const CRect &rect, int nLineIndex);
	void EnsureVisible(TextLocation pt);
	void GetFont(LOGFONT &lf);
	void GetLineColors(int nLineIndex, COLORREF &crBkgnd, COLORREF &crText, bool &bDrawWhitespace);
	void GetPrintFooterText(int nPageNum, std::wstring &text);
	void GetPrintHeaderText(int nPageNum, std::wstring &text);
	const TextSelection &GetSelection() const;
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
	void PrepareSelBounds() const;
	void PrintFooter(HDC pdc, int nPageNum);
	void PrintHeader(HDC pdc, int nPageNum);
	void RecalcHorzScrollBar(bool bPositionOnly = FALSE);
	void RecalcPageLayouts(HDC pdc, CPrintInfo *pInfo);
	void RecalcVertScrollBar(bool bPositionOnly = FALSE);
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
	
	bool DoDropText(COleDataObject *pDataObject, const CPoint &ptClient);
	bool GetAutoIndent() const;
	bool GetOverwriteMode() const;
	bool OnEraseBkgnd(HDC pDC);
	bool OnSetCursor(CWindow* pWnd, UINT nHitTest, UINT message);
	bool QueryEditable();
	bool ReplaceSelection(LPCTSTR pszNewText);
	int OnCreate();
	void Cut();
	void DoDragScroll(const CPoint &point);
	void EnableMenuItems(HMENU h);
	void HideDropIndicator();
	void OnChar(UINT nChar, UINT nRepCnt, UINT nFlags);
	void OnDestroy();
	void OnEditCopy();
	void OnEditCut();
	void OnEditDelete();
	void OnEditDeleteBack();
	void OnEditFind();
	void OnEditFindPrevious();
	void OnEditPaste();
	void OnEditRedo();
	void OnEditRepeat();
	void OnEditReplace();
	void OnEditSelectAll();
	void OnEditTab();
	void OnEditUndo();
	void OnEditUntab();
	void OnFilePageSetup();
	void OnHScroll(UINT nSBCode, UINT nPos, HWND pScrollBar);
	void OnKillFocus(CWindow* pNewWnd);
	void OnLButtonDblClk(const CPoint &point, UINT nFlags);
	void OnLButtonDown(const CPoint &point, UINT nFlags);
	void OnLButtonUp(const CPoint &point, UINT nFlags);
	void OnMouseMove(const CPoint &point, UINT nFlags);
	void OnMouseWheel(const CPoint &point, int zDelta);
	void OnRButtonDown(const CPoint &point, UINT nFlags);
	void OnSetFocus(CWindow* pOldWnd);
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
	void SetOverwriteMode(bool bOvrMode = TRUE);
	void ShowDropIndicator(const CPoint &point);
};

