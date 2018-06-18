#pragma once

#include "document.h"
#include "resource.h"
#include "ui.h"


class find_wnd : public CWindowImpl<find_wnd>
{
public:

	const int editId = 101;
	const int tbId = 102;
	const int nextId = 103;
	const int lastId = 104;

	document& _doc;
	CWindow _findText;
	CWindow _findNext;

	find_wnd(document& d) : _doc(d) { }

	BEGIN_MSG_MAP(find_wnd)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBackground)
		//MESSAGE_HANDLER(WM_PAINT, OnPaint)
		COMMAND_ID_HANDLER(nextId, OnNext)
		COMMAND_ID_HANDLER(lastId, OnLast)
		COMMAND_HANDLER(editId, EN_CHANGE, OnEditChange)
	END_MSG_MAP()

	LRESULT OnCreate(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/)
	{
		auto font = CreateFont(20, 0, 0, 0, FW_DONTCARE, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, TEXT("Calibri"));

		_findText.Create(L"EDIT", m_hWnd, nullptr, nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, editId);
		_findText.SetFont(font);

		auto tbStyle = WS_CHILD | WS_VISIBLE | TBSTYLE_FLAT | TBSTYLE_TRANSPARENT | CCS_NOPARENTALIGN | CCS_NODIVIDER | CCS_ADJUSTABLE;

		_findNext.Create(TOOLBARCLASSNAME, m_hWnd, nullptr, nullptr, tbStyle, 0, nextId);
		_findNext.SetFont(font);

		//HWND hToolbar = CreateWindowEx(0, TOOLBARCLASSNAME, 0,
		//	CCS_ADJUSTABLE | CCS_NODIVIDER | WS_CHILD | WS_VISIBLE | TBSTYLE_FLAT | TBSTYLE_TOOLTIPS,
		//	0, 0, 0, 0, m_hwnd, (HMENU) IDR_TOOLBAR1, GetModuleHandle(NULL), 0);

		_findNext.SendMessage(TB_BUTTONSTRUCTSIZE, static_cast<WPARAM>(sizeof(TBBUTTON)), 0);

		const auto numButtons = 2;

		auto hImageList = ImageList_Create(16, 16, ILC_COLOR32 | ILC_MASK, numButtons, 0);
		ImageList_AddIcon(hImageList, LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_LAST)));
		ImageList_AddIcon(hImageList, LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_NEXT)));
		_findNext.SendMessage(TB_SETIMAGELIST, static_cast<WPARAM>(0), reinterpret_cast<LPARAM>(hImageList));

		TBBUTTON tbButtons[numButtons] =
		{
			{ 0, lastId, TBSTATE_ENABLED, BTNS_AUTOSIZE ,{ 0 }, 0, 0 },
			{ 1, nextId, TBSTATE_ENABLED, BTNS_AUTOSIZE ,{ 0 }, 0, 0 },
		};
		_findNext.SendMessage(TB_ADDBUTTONS, numButtons, reinterpret_cast<LPARAM>(tbButtons));
		_findNext.SendMessage(TB_AUTOSIZE, 0, 0);

		//auto nextIcon = LoadImage(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_RETHINKIFY), IMAGE_ICON, 32, 32, NULL);
		//_findNext.SendMessage(BM_SETIMAGE, (WPARAM) IMAGE_ICON, (LPARAM) nextIcon);

		return 0;
	}

	LRESULT OnSize(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
	{
		CRect r;
		GetClientRect(r);

		r.left += 4;
		r.right -= 54;
		r.top += 4;
		r.bottom -= 4;

		_findText.MoveWindow(r);

		r.left = r.right + 4;
		r.right += 50;

		_findNext.MoveWindow(r);

		return 0;
	}

	LRESULT OnEraseBackground(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/) const
	{
		CRect r;
		GetClientRect(r);
		FillSolidRect(reinterpret_cast<HDC>(wParam), r, RGB(100, 100, 100));
		return 1;
	}

	LRESULT OnPaint(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/)
	{
		CRect r;
		GetClientRect(r);

		PAINTSTRUCT ps = { 0 };
		auto hdc = BeginPaint(&ps);
		FillSolidRect(hdc, r, RGB(100, 100, 100));
		EndPaint(&ps);
		return 0;
	}

	std::wstring Text() const
	{
		const auto bufferSize = 200;
		wchar_t text[bufferSize];
		_findText.GetWindowText(text, bufferSize);
		return text;
	}

	void Text(const std::wstring &s)
	{
		_findText.SetWindowText(s.c_str());
	}

	LRESULT OnEditChange(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled)
	{
		_doc.find(Text(), 0);
		return 0;
	}

	LRESULT OnLast(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
	{
		_doc.find(Text(), FIND_DIRECTION_UP);
		return 0;
	}

	LRESULT OnNext(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
	{
		_doc.find(Text(), 0);
		return 0;
	}
};

static FORMATETC plainTextFormat = {CF_TEXT, 0, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
static FORMATETC plainTextWFormat = {CF_UNICODETEXT, 0, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
static FORMATETC file_drop_format = {CF_HDROP, 0, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};

class text_view : public CWindowImpl<text_view>, public IDropTarget, public IView
{
private:

	document& _doc;
	find_wnd& _find;

	volatile unsigned long m_cRef = 0;
	text_location m_ptDropPos;
	bool m_bDragSelection = false;
	bool m_bWordSelection = false;
	bool m_bLineSelection = false;
	bool m_bPreparingToDrag = false;
	bool m_bDropPosVisible = false;
	bool m_bCursorHidden = false;
	bool m_bFocused = false;
	bool m_bSelMargin = true;
	bool m_bDraggingText = false;
	UINT m_nDragSelTimer = 0;
	text_location m_ptSavedCaretPos;
	text_selection m_ptDraggedText;
	HFONT _font = nullptr;

	CSize _char_offset;
	CSize _extent;
	CSize _font_extent;

	int m_nScreenLines = 0;
	int m_nScreenChars = 0;

public:

	text_view(document& d, find_wnd &f) : _doc(d), _find(f) { }
	~text_view() { }


	BEGIN_MSG_MAP(text_view)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		MESSAGE_HANDLER(WM_SETFOCUS, OnSetFocus)
		MESSAGE_HANDLER(WM_KILLFOCUS, OnKillFocus)
		MESSAGE_HANDLER(WM_PAINT, OnPaint)
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBackground)
		MESSAGE_HANDLER(WM_VSCROLL, OnVScroll)
		MESSAGE_HANDLER(WM_HSCROLL, OnHScroll)
		MESSAGE_HANDLER(WM_TIMER, OnTimer)
		MESSAGE_HANDLER(WM_SYSCOLORCHANGE, OnSysColorChange)
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
		RegisterDragDrop(m_hWnd, this);

		LOGFONT lf;
		memset(&lf, 0, sizeof(lf));
		lf.lfWeight = FW_NORMAL;
		lf.lfCharSet = ANSI_CHARSET;
		lf.lfOutPrecision = OUT_DEFAULT_PRECIS;
		lf.lfClipPrecision = CLIP_DEFAULT_PRECIS;
		lf.lfQuality = CLEARTYPE_NATURAL_QUALITY;
		lf.lfPitchAndFamily = DEFAULT_PITCH | FF_DONTCARE ;
		wcscpy_s(lf.lfFaceName, L"Consolas");

		_font = ::CreateFontIndirect(&lf);

		return 0;
	}

	LRESULT OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
	{
		RevokeDragDrop(m_hWnd);
		return 0;
	}

	LRESULT OnSize(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/)
	{
		win_dc hdc(m_hWnd);
		auto pOldFont = hdc.SelectFont(_font);

		CSize font_extent;
		GetTextExtentExPoint(hdc, _T("X"), 1, 1, nullptr, nullptr, &font_extent);
		if (font_extent.cy < 1) font_extent.cy = 1;

		/*
        TEXTMETRIC tm;
        if (hdc->GetTextMetrics(&tm))
        m_nCharWidth -= tm.tmOverhang;
        */

		_extent = CSize(LOWORD(lParam), HIWORD(lParam));
		_font_extent = font_extent;

		layout();

		hdc.SelectFont(pOldFont);


		return 0;
	}

	LRESULT OnPaint(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/)
	{
		PAINTSTRUCT ps = {0};
		auto hdc = BeginPaint(&ps);
		draw(hdc);
		EndPaint(&ps);
		return 0;
	}

	LRESULT OnEraseBackground(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/)
	{
		return 1;
	}

	LRESULT OnTimer(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& /*bHandled*/)
	{
		OnTimer(wParam);
		return 0;
	}

	LRESULT OnSysColorChange(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& /*bHandled*/)
	{
		Invalidate(FALSE);
		return 0;
	}

	LRESULT OnSetFocus(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/)
	{
		OnSetFocus(reinterpret_cast<HWND>(wParam));
		return 0;
	}

	LRESULT OnKillFocus(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/)
	{
		OnKillFocus(reinterpret_cast<HWND>(wParam));
		return 0;
	}

	LRESULT OnVScroll(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/)
	{
		auto nScrollCode = static_cast<int>(LOWORD(wParam));
		auto y = static_cast<short int>(HIWORD(wParam));
		auto hwndScrollBar = reinterpret_cast<HWND>(lParam);
		OnVScroll(nScrollCode, y, hwndScrollBar);
		return 0;
	}

	LRESULT OnHScroll(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/)
	{
		auto nScrollCode = static_cast<int>(LOWORD(wParam));
		auto nPos = static_cast<short int>(HIWORD(wParam));
		auto hwndScrollBar = reinterpret_cast<HWND>(lParam);
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
		auto delta = static_cast<short>(HIWORD(wParam)) > 0 ? -2 : 2;
		OnMouseWheel(CPoint(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)), delta);
		return 0;
	}

	LRESULT OnChar(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/)
	{
		auto c = wParam;
		auto flags = lParam;

		if ((::GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0 ||
			(::GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0)
			return 0;

		if (c == VK_RETURN)
		{
			if (_doc.QueryEditable())
			{
				undo_group ug(_doc);
				auto pos = _doc.delete_text(ug, _doc.selection());
				_doc.select(_doc.insert_text(ug, pos, L'\n'));
			}
		}
		else if (c > 31)
		{
			if (_doc.QueryEditable())
			{
				undo_group ug(_doc);
				auto pos = _doc.delete_text(ug, _doc.selection());
				_doc.select(_doc.insert_text(ug, pos, c));
			}
		}

		return 0;
	}

	LRESULT OnCommand(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/)
	{
		OnCommand(LOWORD(wParam));
		return 0;
	}

	LRESULT OnContextMenu(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
	{
		CPoint location(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
		CPoint clientLocation(location);
		ScreenToClient(&clientLocation);

		auto menu = CreatePopupMenu();

		if (menu)
		{
			auto loc = client_to_text(clientLocation);
			auto selection = _doc.is_inside_selection(loc) ? _doc.selection() : _doc.word_selection(loc, false);

			_doc.select(selection);

			auto word = Combine(_doc.text(selection));

			std::map<UINT, std::wstring> replacements;

			if (!selection.empty())
			{
				auto id = 1000U;


				for (auto option : _doc.suggest(word))
				{
					auto title = Replace(option, L"&", L"&&");
					AppendMenu(menu, MF_ENABLED, id, title.c_str());
					replacements[id] = option;
					id++;
				}

				if (replacements.size() > 0)
				{
					AppendMenu(menu, MF_SEPARATOR, 0, nullptr);
				}

				if (_doc.can_add(word))
				{
					AppendMenu(menu, MF_ENABLED, ID_FILE_NEW, String::Format(L"Add '%s'", word.c_str()).c_str());
					AppendMenu(menu, MF_SEPARATOR, 0, nullptr);
				}
			}

			if (_doc.can_undo())
			{
				AppendMenu(menu, MF_ENABLED, ID_EDIT_UNDO, L"Undo");
				AppendMenu(menu, MF_SEPARATOR, 0, nullptr);
			}

			AppendMenu(menu, MF_ENABLED, ID_EDIT_CUT, L"Cut");
			AppendMenu(menu, MF_ENABLED, ID_EDIT_COPY, L"Copy");
			AppendMenu(menu, MF_ENABLED, ID_EDIT_PASTE, L"Paste");
			AppendMenu(menu, MF_ENABLED, ID_EDIT_DELETE, L"Erase");
			AppendMenu(menu, MF_SEPARATOR, 0, nullptr);
			AppendMenu(menu, MF_ENABLED, ID_EDIT_SELECT_ALL, L"Select All");

			auto result = TrackPopupMenu(menu, TPM_TOPALIGN | TPM_LEFTALIGN | TPM_NONOTIFY | TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_VERNEGANIMATION, location.x, location.y, 0, m_hWnd, nullptr);
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
				_doc.add_word(word);
				break;

			default:

				if (replacements.find(result) != replacements.end())
				{
					undo_group ug(_doc);
					_doc.select(_doc.replace_text(ug, selection, replacements[result]));
				}
				break;
			}
		}

		return 0;
	}

	void OnCommand(int id)
	{
		switch (id)
		{
		case ID_EDIT_COPY: _doc.Copy();
			break;
		case ID_EDIT_SELECT_ALL: _doc.select(_doc.all());
			break;
			//case ID_EDIT_FIND: _doc.find(); break;
		case ID_EDIT_REPEAT: _doc.find(_find.Text(), 0);
			break;
		case ID_EDIT_FIND_PREVIOUS: _doc.find(_find.Text(), FIND_DIRECTION_UP);
			break;
		case ID_EDIT_CHAR_LEFT: _doc.MoveLeft(false);
			break;
		case ID_EDIT_EXT_CHAR_LEFT: _doc.MoveLeft(true);
			break;
		case ID_EDIT_CHAR_RIGHT: _doc.MoveRight(false);
			break;
		case ID_EDIT_EXT_CHAR_RIGHT: _doc.MoveRight(true);
			break;
		case ID_EDIT_WORD_LEFT: _doc.MoveWordLeft(false);
			break;
		case ID_EDIT_EXT_WORD_LEFT: _doc.MoveWordLeft(true);
			break;
		case ID_EDIT_WORD_RIGHT: _doc.MoveWordRight(false);
			break;
		case ID_EDIT_EXT_WORD_RIGHT: _doc.MoveWordRight(true);
			break;
		case ID_EDIT_LINE_UP: _doc.MoveUp(false);
			break;
		case ID_EDIT_EXT_LINE_UP: _doc.MoveUp(true);
			break;
		case ID_EDIT_LINE_DOWN: _doc.MoveDown(false);
			break;
		case ID_EDIT_EXT_LINE_DOWN: _doc.MoveDown(true);
			break;
		case ID_EDIT_SCROLL_UP: ScrollUp();
			break;
		case ID_EDIT_SCROLL_DOWN: ScrollDown();
			break;
		case ID_EDIT_PAGE_UP: move_page(false, false);
			break;
		case ID_EDIT_EXT_PAGE_UP: move_page(true, false);
			break;
		case ID_EDIT_PAGE_DOWN: move_page(false, true);
			break;
		case ID_EDIT_EXT_PAGE_DOWN: move_page(true, true);
			break;
		case ID_EDIT_LINE_END: _doc.MoveEnd(false);
			break;
		case ID_EDIT_EXT_LINE_END: _doc.MoveEnd(true);
			break;
		case ID_EDIT_HOME: _doc.MoveHome(false);
			break;
		case ID_EDIT_EXT_HOME: _doc.MoveHome(true);
			break;
		case ID_EDIT_TEXT_BEGIN: _doc.MoveCtrlHome(false);
			break;
		case ID_EDIT_EXT_TEXT_BEGIN: _doc.MoveCtrlHome(true);
			break;
		case ID_EDIT_TEXT_END: _doc.MoveCtrlEnd(false);
			break;
		case ID_EDIT_EXT_TEXT_END: _doc.MoveCtrlEnd(true);
			break;
		case ID_EDIT_PASTE: _doc.Paste();
			break;
		case ID_EDIT_CUT: _doc.Cut();
			break;
		case ID_EDIT_DELETE: _doc.OnEditDelete();
			break;
		case ID_EDIT_DELETE_BACK: _doc.OnEditDeleteBack();
			break;
		case ID_EDIT_UNTAB: _doc.OnEditUntab();
			break;
		case ID_EDIT_TAB: _doc.OnEditTab();
			break;
			//case ID_EDIT_REPLACE: _doc.OnEditReplace(); break;
		case ID_EDIT_UNDO: _doc.OnEditUndo();
			break;
		case ID_EDIT_REDO: _doc.OnEditRedo();
			break;
		}
	}

	void invalidate_selection()
	{
		auto sel = _doc.selection();

		if (!sel.empty())
			invalidate_lines(sel._start.y, sel._end.y);
	}

	void OnSetFocus(CWindow oldWnd)
	{
		m_bFocused = true;
		invalidate_selection();
		update_caret();
	}

	void OnKillFocus(CWindow newWnd)
	{
		m_bFocused = false;

		update_caret();
		invalidate_selection();

		if (m_bDragSelection)
		{
			ReleaseCapture();
			KillTimer(m_nDragSelTimer);
			m_bDragSelection = false;
		}
	}

	void OnTimer(UINT nIDEvent)
	{
		if (nIDEvent == RETHINKIFY_TIMER_DRAGSEL)
		{
			assert(m_bDragSelection);
			CPoint pt;
			::GetCursorPos(&pt);
			ScreenToClient(&pt);

			auto rcClient = client_rect();
			auto bChanged = false;
			auto y = _char_offset.cy;
			auto line_count = _doc.size();

			if (pt.y < rcClient.top)
			{
				y--;

				if (pt.y < rcClient.top - _font_extent.cy)
					y -= 2;
			}
			else if (pt.y >= rcClient.bottom)
			{
				y++;

				if (pt.y >= rcClient.bottom + _font_extent.cy)
					y += 2;
			}

			y = Clamp(y, 0, line_count - 1);

			if (_char_offset.cy != y)
			{
				ScrollToLine(y);
				bChanged = true;
			}

			//	Scroll horizontally, if necessary
			auto x = _char_offset.cx;
			auto nMaxLineLength = _doc.max_line_length();

			if (pt.x < rcClient.left)
			{
				x--;
			}
			else if (pt.x >= rcClient.right)
			{
				x++;
			}

			x = Clamp(x, 0, nMaxLineLength - 1);

			if (_char_offset.cx != x)
			{
				ScrollToChar(x);
				bChanged = true;
			}

			//	Fix changes
			if (bChanged)
			{
				_doc.cursor_pos(client_to_text(pt));
				_doc.select(text_selection(_doc.cursor_pos()));
			}
		}
	}

	void OnLButtonDown(const CPoint& point, UINT nFlags)
	{
		bool bShift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
		bool bControl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;

		SetFocus();

		if (point.x < margin_width())
		{
			if (bControl)
			{
				_doc.select(_doc.all());
			}
			else
			{
				auto sel = _doc.line_selection(client_to_text(point), bShift);
				_doc.select(sel);

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
			auto ptText = client_to_text(point);

			if (_doc.is_inside_selection(ptText))
			{
				m_bPreparingToDrag = true;
			}
			else
			{
				auto pos = client_to_text(point);
				auto sel = bControl ? _doc.word_selection(pos, bShift) : _doc.pos_selection(pos, bShift);
				_doc.select(sel);

				SetCapture();
				m_nDragSelTimer = SetTimer(RETHINKIFY_TIMER_DRAGSEL, 100, nullptr);
				assert(m_nDragSelTimer != 0);
				m_bWordSelection = bControl;
				m_bLineSelection = false;
				m_bDragSelection = true;
			}
		}
	}

	void OnMouseWheel(const CPoint& point, int zDelta)
	{
		ScrollToLine(Clamp(_char_offset.cy + zDelta, 0, _doc.size()));
		update_caret();
	}

	void OnMouseMove(const CPoint& point, UINT nFlags)
	{
		if (m_bDragSelection)
		{
			auto bOnMargin = point.x < margin_width();
			auto pos = client_to_text(point);

			if (m_bLineSelection)
			{
				if (bOnMargin)
				{
					auto sel = _doc.line_selection(pos, true);
					_doc.select(sel);
					return;
				}

				m_bLineSelection = m_bWordSelection = false;
				update_cursor();
			}

			auto sel = m_bWordSelection ? _doc.word_selection(pos, true) : _doc.pos_selection(pos, true);
			_doc.select(sel);
		}

		if (m_bPreparingToDrag)
		{
			m_bPreparingToDrag = false;
			auto hData = PrepareDragData();

			if (hData != nullptr)
			{
				//undo_group ug(*this);


				/*COleDataSource ds;
                ds.CacheGlobalData(CF_UNICODETEXT, hData);
                m_bDraggingText = true;
                DROPEFFECT de = ds.DoDragDrop(GetDropEffect());
                if (de != DROPEFFECT_NONE)
                if (m_bDraggingText && de == DROPEFFECT_MOVE)
                {
                undo_group ug(_doc.);
                _doc.delete_text(ug, m_ptDraggedText);
                }
                m_bDraggingText = false;

                if (_doc != nullptr)
                _doc.FlushUndoGroup(this);*/
			}
		}
	}

	DROPEFFECT GetDropEffect()
	{
		return DROPEFFECT_COPY | DROPEFFECT_MOVE;
	}

	void OnLButtonUp(const CPoint& point, UINT nFlags)
	{
		if (m_bDragSelection)
		{
			auto pos = client_to_text(point);

			if (m_bLineSelection)
			{
				auto sel = _doc.line_selection(pos, true);
				_doc.select(sel);
			}
			else
			{
				auto sel = m_bWordSelection ? _doc.word_selection(pos, true) : _doc.pos_selection(pos, true);
				_doc.select(sel);
			}

			ReleaseCapture();
			KillTimer(m_nDragSelTimer);
			m_bDragSelection = false;
		}

		if (m_bPreparingToDrag)
		{
			m_bPreparingToDrag = false;
			_doc.select(client_to_text(point));
		}
	}

	void OnLButtonDblClk(const CPoint& point, UINT nFlags)
	{
		if (!m_bDragSelection)
		{
			bool bShift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
			_doc.select(_doc.word_selection(client_to_text(point), bShift));

			SetCapture();
			m_nDragSelTimer = SetTimer(RETHINKIFY_TIMER_DRAGSEL, 100, nullptr);
			assert(m_nDragSelTimer != 0);
			m_bWordSelection = true;
			m_bLineSelection = false;
			m_bDragSelection = true;
		}
	}

	void OnRButtonDown(const CPoint& point, UINT nFlags)
	{
		auto pt = client_to_text(point);

		if (!_doc.is_inside_selection(pt))
		{
			//m_ptAnchor = m_ptCursorPos = pt;
			_doc.select(text_selection(pt, pt));
			ensure_visible(pt);
		}
	}

	void ScrollToChar(int x)
	{
		if (_char_offset.cx != x)
		{
			int nScrollChars = _char_offset.cx - x;
			_char_offset.cx = x;
			auto rcScroll = client_rect();
			rcScroll.left += margin_width();
			ScrollWindowEx(nScrollChars * _font_extent.cx, 0, rcScroll, rcScroll, nullptr, nullptr, SW_INVALIDATE);
			recalc_horz_scrollbar();
		}
	}

	void ScrollToLine(int y)
	{
		if (_char_offset.cy != y)
		{
			int nScrollLines = _char_offset.cy - y;
			_char_offset.cy = y;
			ScrollWindowEx(0, nScrollLines * _font_extent.cy, nullptr, nullptr, nullptr, nullptr, SW_INVALIDATE);
			recalc_vert_scrollbar();
		}
	}

	void recalc_vert_scrollbar() override
	{
		if (m_nScreenLines >= _doc.size() && _char_offset.cy > 0)
		{
			_char_offset.cy = 0;
			Invalidate();
			update_caret();
		}

		SCROLLINFO si;
		si.cbSize = sizeof(si);
		si.fMask = SIF_PAGE | SIF_POS | SIF_RANGE;
		si.nMin = 0;
		si.nMax = _doc.size() - 1;
		si.nPage = m_nScreenLines;
		si.nPos = _char_offset.cy;
		SetScrollInfo(SB_VERT, &si);
	}

	void OnVScroll(UINT nSBCode, UINT nPos, HWND pScrollBar)
	{
		SCROLLINFO si;
		si.cbSize = sizeof(si);
		si.fMask = SIF_ALL;
		GetScrollInfo(SB_VERT, &si);

		int nPageLines = m_nScreenLines;
		int line_count = _doc.size();

		int y;
		switch (nSBCode)
		{
		case SB_TOP:
			y = 0;
			break;
		case SB_BOTTOM:
			y = line_count - nPageLines + 1;
			break;
		case SB_LINEUP:
			y = _char_offset.cy - 1;
			break;
		case SB_LINEDOWN:
			y = _char_offset.cy + 1;
			break;
		case SB_PAGEUP:
			y = _char_offset.cy - si.nPage + 1;
			break;
		case SB_PAGEDOWN:
			y = _char_offset.cy + si.nPage - 1;
			break;
		case SB_THUMBPOSITION:
		case SB_THUMBTRACK:
			y = si.nTrackPos;
			break;
		default:
			return;
		}

		ScrollToLine(Clamp(y, 0, line_count - 1));
		update_caret();
	}

	void recalc_horz_scrollbar() override
	{
		if (m_nScreenChars >= _doc.max_line_length() && _char_offset.cx > 0)
		{
			_char_offset.cx = 0;
			Invalidate();
			update_caret();
		}

		auto margin_width = m_bSelMargin ? 3 : 0;

		SCROLLINFO si;
		si.cbSize = sizeof(si);
		si.fMask = SIF_PAGE | SIF_POS | SIF_RANGE;
		si.nMin = 0;
		si.nMax = _doc.max_line_length() + margin_width;
		si.nPage = m_nScreenChars;
		si.nPos = _char_offset.cx;
				
		SetScrollInfo(SB_HORZ, &si);
	}

	void OnHScroll(UINT nSBCode, UINT nPos, HWND pScrollBar)
	{
		SCROLLINFO si;
		si.cbSize = sizeof(si);
		si.fMask = SIF_ALL;
		GetScrollInfo(SB_HORZ, &si);

		int nPageChars = m_nScreenChars;
		int nMaxLineLength = _doc.max_line_length();

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
			nNewOffset = _char_offset.cx - 1;
			break;
		case SB_LINEDOWN:
			nNewOffset = _char_offset.cx + 1;
			break;
		case SB_PAGEUP:
			nNewOffset = _char_offset.cx - si.nPage + 1;
			break;
		case SB_PAGEDOWN:
			nNewOffset = _char_offset.cx + si.nPage - 1;
			break;
		case SB_THUMBPOSITION:
		case SB_THUMBTRACK:
			nNewOffset = si.nTrackPos;
			break;
		default:
			return;
		}

		ScrollToChar(Clamp(nNewOffset, 0, nMaxLineLength - 1));
		update_caret();
	}

	bool OnSetCursor(CWindow wnd, UINT nHitTest, UINT message)
	{
		if (nHitTest == HTCLIENT)
		{
			update_cursor();
			return true;
		}
		return false;
	}

	void update_cursor()
	{
		static auto arrow = ::LoadCursor(nullptr, MAKEINTRESOURCE(IDC_ARROW));
		static auto beam = ::LoadCursor(nullptr, MAKEINTRESOURCE(IDC_IBEAM));

		CPoint pt;
		GetCursorPos(&pt);
		ScreenToClient(&pt);

		if (pt.x < margin_width())
		{
			::SetCursor(arrow);
		}
		else if (_doc.is_inside_selection(client_to_text(pt)))
		{
			::SetCursor(arrow);
		}
		else
		{
			::SetCursor(beam);
		}
	}

	void update_caret() override
	{
		auto pos = _doc.cursor_pos();

		if (m_bFocused && !m_bCursorHidden &&
			_doc.calc_offset(pos.y, pos.x) >= _char_offset.cx)
		{
			CreateSolidCaret(2, _font_extent.cy);

			auto pt = text_to_client(pos);
			SetCaretPos(pt.x, pt.y);
			ShowCaret();
		}
		else
		{
			HideCaret();
		}
	}

	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, LPVOID* ppvObj) override
	{
		if (!ppvObj)
			return E_INVALIDARG;

		*ppvObj = nullptr;

		if (riid == IID_IUnknown || riid == IID_IDropTarget)
		{
			*ppvObj = static_cast<LPVOID>(this);
			AddRef();
			return NOERROR;
		}
		return E_NOINTERFACE;
	}

	ULONG STDMETHODCALLTYPE AddRef() override
	{
		InterlockedIncrement(&m_cRef);
		return m_cRef;
	}

	ULONG STDMETHODCALLTYPE Release() override
	{
		return InterlockedDecrement(&m_cRef);
	}

	bool CanDrop(IDataObject* pDataObj) const
	{
		return pDataObj->QueryGetData(&plainTextFormat) == S_OK ||
			pDataObj->QueryGetData(&plainTextWFormat) == S_OK ||
			pDataObj->QueryGetData(&file_drop_format) == S_OK;
	}


	DWORD DropEffect(IDataObject* pDataObj, const CPoint& loc)
	{
		if (CanDrop(pDataObj))
		{
			ShowDropIndicator(loc);
			return (GetKeyState(VK_CONTROL) < 0) ? DROPEFFECT_COPY : DROPEFFECT_MOVE;
		}
		else
		{
			HideDropIndicator();
			return DROPEFFECT_NONE;
		}
	}

	HRESULT STDMETHODCALLTYPE DragEnter(IDataObject* pDataObj, DWORD grfKeyState, POINTL pt, DWORD* pdwEffect) override
	{
		*pdwEffect = DropEffect(pDataObj, pt);
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE DragLeave(void) override
	{
		HideDropIndicator();
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE DragOver(DWORD grfKeyState, POINTL pt, DWORD* pdwEffect) override
	{
		//*pdwEffect = DropEffect(pDataObj, pt);
		//return S_OK;
		*pdwEffect = DROPEFFECT_MOVE;
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE Drop(IDataObject* pDataObj, DWORD grfKeyState, POINTL pt, DWORD* pdwEffect) override
	{
		return DropData(pDataObj, pt) ? S_OK : S_FALSE;
	}

	DROPEFFECT OnDragScroll(CWindow wnd, DWORD dwKeyState, CPoint point)
	{
		assert(m_hWnd == wnd.m_hWnd);

		auto rcClientRect = client_rect();

		if (point.y < rcClientRect.top + DRAG_BORDER_Y)
		{
			HideDropIndicator();
			ScrollUp();
			ShowDropIndicator(point);
		}
		else if (point.y >= rcClientRect.bottom - DRAG_BORDER_Y)
		{
			HideDropIndicator();
			ScrollDown();
			ShowDropIndicator(point);
		}
		else if (point.x < rcClientRect.left + margin_width() + DRAG_BORDER_X)
		{
			HideDropIndicator();
			ScrollLeft();
			ShowDropIndicator(point);
		}
		else if (point.x >= rcClientRect.right - DRAG_BORDER_X)
		{
			HideDropIndicator();
			ScrollRight();
			ShowDropIndicator(point);
		}

		if (dwKeyState & MK_CONTROL)
			return DROPEFFECT_COPY;
		return DROPEFFECT_MOVE;
	}

	void ScrollUp()
	{
		if (_char_offset.cy > 0)
		{
			ScrollToLine(_char_offset.cy - 1);
			update_caret();
		}
	}

	void ScrollDown()
	{
		if (_char_offset.cy < _doc.size() - 1)
		{
			ScrollToLine(_char_offset.cy + 1);
			update_caret();
		}
	}

	void ScrollLeft()
	{
		if (_char_offset.cx > 0)
		{
			ScrollToChar(_char_offset.cx - 1);
			update_caret();
		}
	}

	void ScrollRight()
	{
		if (_char_offset.cx < _doc.max_line_length() - 1)
		{
			ScrollToChar(_char_offset.cx + 1);
			update_caret();
		}
	}

	bool DropData(IDataObject* pDataObject, const CPoint& ptClient)
	{
		STGMEDIUM stgmed;
		std::wstring text;

		if (SUCCEEDED(pDataObject->GetData(&plainTextWFormat, &stgmed)))
		{
			//unicode text
			auto data = static_cast<const wchar_t*>(GlobalLock(stgmed.hGlobal));
			text = data;
			GlobalUnlock(stgmed.hGlobal);
			ReleaseStgMedium(&stgmed);
		}
		else if (SUCCEEDED(pDataObject->GetData(&plainTextFormat, &stgmed)))
		{
			//ascii text
			auto data = static_cast<const char*>(GlobalLock(stgmed.hGlobal));
			text = AsciiToUtf16(data);
			GlobalUnlock(stgmed.hGlobal);
			ReleaseStgMedium(&stgmed);
		}
		else if (SUCCEEDED(pDataObject->GetData(&file_drop_format, &stgmed)))
		{
			auto data = static_cast<const char*>(GlobalLock(stgmed.hGlobal));
			auto files = reinterpret_cast<const DROPFILES*>(data);
			text = reinterpret_cast<const wchar_t *>(data + files->pFiles);

			if (_doc.load_from_file(text))
			{
				GetParent().SetWindowText(PathFindFileName(text.c_str()));
			}

			GlobalUnlock(stgmed.hGlobal);
			ReleaseStgMedium(&stgmed);

			return true;
		}

		auto drop_loc = client_to_text(ptClient);

		if (m_bDraggingText && _doc.is_inside_selection(drop_loc))
		{
			return false;
		}
		else
		{
			undo_group ug(_doc);
			_doc.select(_doc.insert_text(ug, drop_loc, text));
		}

		return true;
	}

	void ShowDropIndicator(const CPoint& point)
	{
		if (!m_bDropPosVisible)
		{
			//HideCursor();
			m_ptSavedCaretPos = _doc.cursor_pos();
			m_bDropPosVisible = true;
			::CreateCaret(m_hWnd, reinterpret_cast<HBITMAP>(1), 2, _font_extent.cy);
		}
		m_ptDropPos = client_to_text(point);
		if (m_ptDropPos.x >= _char_offset.cx)
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

	void HideDropIndicator()
	{
		if (m_bDropPosVisible)
		{
			_doc.cursor_pos(m_ptSavedCaretPos);
			//ShowCursor();
			m_bDropPosVisible = false;
		}
	}

	void ShowCursor()
	{
		m_bCursorHidden = false;
		update_caret();
	}

	void HideCursor()
	{
		m_bCursorHidden = true;
		update_caret();
	}

	std::wstring text_from_clipboard() const override
	{
		std::wstring result;
		auto pThis = const_cast<text_view*>(this);

		if (pThis->OpenClipboard())
		{
			auto hData = GetClipboardData(CF_UNICODETEXT);

			if (hData != nullptr)
			{
				auto pszData = static_cast<const wchar_t *>(GlobalLock(hData));

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

	bool text_to_clipboard(const std::wstring& text) override
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
				auto pszData = static_cast<wchar_t*>(::GlobalLock(hData));
				wcscpy_s(pszData, len, text.c_str());
				GlobalUnlock(hData);
				success = SetClipboardData(CF_UNICODETEXT, hData) != nullptr;
			}
			CloseClipboard();
		}

		return success;
	}

	void invalidate(LPCRECT r = nullptr)
	{
		InvalidateRect(r, FALSE);
	}

	HGLOBAL PrepareDragData()
	{
		auto sel = _doc.selection();

		if (sel.empty())
			return nullptr;

		auto text = Combine(_doc.text(sel));
		auto len = text.size() + 1;
		auto hData = ::GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, len * sizeof(wchar_t));

		if (hData == nullptr)
			return nullptr;

		auto pszData = static_cast<wchar_t*>(::GlobalLock(hData));
		wcscpy_s(pszData, len, text.c_str());
		::GlobalUnlock(hData);

		m_ptDraggedText = sel;

		return hData;
	}


	int client_to_line(const CPoint& point) const
	{
		auto line_count = _doc.size();

		auto result = (point.y / _font_extent.cy) + _char_offset.cy;
		/*auto y = _char_offset.cy;

        while (y < line_count)
        {
        auto const &line = _doc[y];

        if (point.y >= line._y && point.y < (line._y + line._cy))
        {
        return y;
        }

        y += 1;
        }*/

		return Clamp(result, 0, line_count - 1);
	}

	text_location client_to_text(const CPoint& point) const
	{
		const auto line_count = _doc.size();

		text_location pt;
		pt.y = client_to_line(point);

		if (pt.y >= 0 && pt.y < line_count)
		{
			const auto tabSize = _doc.tab_size();
			const auto& line = _doc[pt.y];
			const auto lineSize = line.size();
			auto x = _char_offset.cx + ((point.x - margin_width()) / _font_extent.cx);

			if (x < 0)
				x = 0;

			auto i = 0;
			auto xx = 0;

			while (i < lineSize)
			{
				if (line[i] == _T('\t'))
				{
					xx += (tabSize - xx % tabSize);
				}
				else
				{
					xx++;
				}

				if (xx > x)
					break;

				i++;
			}

			pt.x = Clamp(i, 0, lineSize);
		}

		return pt;
	}

	CPoint text_to_client(const text_location& point) const
	{
		CPoint pt;

		if (point.y >= 0 && point.y < _doc.size())
		{
			pt.y = line_offset(point.y) - top_offset();
			pt.x = 0;

			auto tabSize = _doc.tab_size();
			const auto& line = _doc[point.y];

			for (auto i = 0; i < point.x; i++)
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

	int top_offset() const
	{
		auto result = _char_offset.cy * _font_extent.cy;

		/*if (!_doc.empty() || _char_offset.cy <= 0)
        {
        result = _doc[_char_offset.cy]._y;
        }*/

		return result;
	}

	void invalidate_lines(int start, int end) override
	{
		auto rcInvalid = client_rect();
		auto top = top_offset();

		if (end == -1)
		{			
			rcInvalid.top = line_offset(start) - top;
		}
		else
		{
			if (end < start)
			{
				std::swap(start, end);
			}

			rcInvalid.top = line_offset(start) - top;
			rcInvalid.bottom = line_offset(end) + line_height(end) - top;
		}

		invalidate(rcInvalid);
	}

	void invalidate_line(int index) override
	{
		auto& line = _doc[index];
		line._expanded_length = -1;
		line._parse_cookie = -1;

		invalidate_lines(index, index + 1);

		_doc.update_max_line_length(index);

		recalc_horz_scrollbar();
	}

	void invalidate_view() override
	{
		m_nScreenChars = -1;

		layout();

		update_caret();
		recalc_vert_scrollbar();
		recalc_horz_scrollbar();
		invalidate();
	}

	void layout() override
	{
		auto rect = client_rect();
		auto line_count = _doc.size();
		auto y = 0;
		auto cy = _font_extent.cy;

		/*for (int i = 0; i < line_count; i++)
        {
        auto &line = _doc[i];

        line._y = y;
        line._cy = cy;

        y += cy;
        }*/

		m_nScreenLines = rect.Height() / _font_extent.cy;
		m_nScreenChars = rect.Width() / _font_extent.cx;

		recalc_vert_scrollbar();
		recalc_horz_scrollbar();
	}

	int line_offset(int lineIndex) const
	{
		/*auto max = _doc.size();
        auto line = Clamp(lineIndex, 0, max - 1);
        return _doc[line]._y;*/

		return lineIndex * _font_extent.cy;
	}

	int line_height(int lineIndex) const
	{
		return _font_extent.cy;
	}

	void ensure_visible(const text_location& pt) override
	{
		//	Scroll vertically
		int line_count = _doc.size();
		int y = _char_offset.cy;

		if (pt.y >= y + m_nScreenLines)
		{
			y = pt.y - m_nScreenLines + 1;
		}
		else if (pt.y < y)
		{
			y = pt.y;
		}

		y = Clamp(y, 0, line_count - 1);

		if (_char_offset.cy != y)
		{
			ScrollToLine(y);
		}

		//	Scroll horizontally
		auto nActualPos = _doc.calc_offset(pt.y, pt.x);
		auto nNewOffset = _char_offset.cx;

		if (nActualPos > nNewOffset + m_nScreenChars)
		{
			nNewOffset = nActualPos - m_nScreenChars;
		}
		if (nActualPos < nNewOffset)
		{
			nNewOffset = nActualPos;
		}

		if (nNewOffset >= _doc.max_line_length())
			nNewOffset = _doc.max_line_length() - 1;
		if (nNewOffset < 0)
			nNewOffset = 0;

		if (_char_offset.cx != nNewOffset)
		{
			ScrollToChar(nNewOffset);
		}

		update_caret();
	}

	CRect client_rect() const
	{
		return CRect(0, 0, _extent.cx, _extent.cy);
	}

	int margin_width() const
	{
		return m_bSelMargin ? 20 : 1;
	}

	void draw_line(HDC pdc, text_location& ptOrigin, const CRect& rcClip, const wchar_t* pszChars, int nOffset, int nCount) const
	{
		if (nCount > 0)
		{
			auto line = _doc.expanded_chars(pszChars, nOffset, nCount);
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

	void draw_line(HDC pdc, text_location& ptOrigin, const CRect& rcClip, color_index nColorIndex, const wchar_t* pszChars, int nOffset, int nCount, const text_location& ptTextPos) const
	{
		if (nCount > 0)
		{
			auto sel = _doc.selection();
			auto nSelBegin = 0, nSelEnd = 0;

			if (sel._start.y > ptTextPos.y)
			{
				nSelBegin = nCount;
			}
			else if (sel._start.y == ptTextPos.y)
			{
				nSelBegin = Clamp(sel._start.x - ptTextPos.x, 0, nCount);
			}
			if (sel._end.y > ptTextPos.y)
			{
				nSelEnd = nCount;
			}
			else if (sel._end.y == ptTextPos.y)
			{
				nSelEnd = Clamp(sel._end.x - ptTextPos.x, 0, nCount);
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
				auto crOldBk = SetBkColor(pdc, GetColor(color_index::COLORINDEX_SELBKGND));
				auto crOldText = SetTextColor(pdc, GetColor(color_index::COLORINDEX_SELTEXT));
				draw_line(pdc, ptOrigin, rcClip, pszChars, nOffset + nSelBegin, nSelEnd - nSelBegin);
				SetBkColor(pdc, crOldBk);
				SetTextColor(pdc, crOldText);
			}
			if (nSelEnd < nCount)
			{
				draw_line(pdc, ptOrigin, rcClip, pszChars, nOffset + nSelEnd, nCount - nSelEnd);
			}
		}
	}

	void draw_line(HDC hdc, const CRect& rc, int lineIndex) const
	{
		if (lineIndex == -1)
		{
			//	Draw line beyond the text
			FillSolidRect(hdc, rc, GetColor(color_index::COLORINDEX_WHITESPACE));
		}
		else
		{
			//	Acquire the background color for the current line
			auto bDrawWhitespace = true;
			auto crBkgnd = GetColor(color_index::COLORINDEX_BKGND);

			const auto& line = _doc[lineIndex];

			if (line.empty())
			{
				//	Draw the empty line
				auto rect = rc;

				if (_doc.is_inside_selection(text_location(0, lineIndex)))
				{
					FillSolidRect(hdc, rect.left, rect.top, _font_extent.cx, rect.Height(), GetColor(color_index::COLORINDEX_SELBKGND));
					rect.left += _font_extent.cx;
				}

				FillSolidRect(hdc, rect, bDrawWhitespace ? crBkgnd : GetColor(color_index::COLORINDEX_WHITESPACE));
			}
			else
			{
				//	Parse the line
				auto nLength = line.size();
				auto pBuf = static_cast<highlighter::text_block*>(_malloca(sizeof(highlighter::text_block) * nLength * 3));
				auto nBlocks = 0;
				auto cookie = _doc.highlight_cookie(lineIndex - 1);

				line._parse_cookie = _doc.highlight_line(cookie, line, pBuf, nBlocks);

				//	Draw the line text
				text_location origin(rc.left - _char_offset.cx * _font_extent.cx, rc.top);
				SetBkColor(hdc, crBkgnd);

				auto bColorSet = false;
				auto pszChars = line.c_str();

				if (nBlocks > 0)
				{
					assert(pBuf[0]._char_pos >= 0 && pBuf[0]._char_pos <= nLength);

					SetTextColor(hdc, GetColor(color_index::COLORINDEX_NORMALTEXT));
					draw_line(hdc, origin, rc, color_index::COLORINDEX_NORMALTEXT, pszChars, 0, pBuf[0]._char_pos, text_location(0, lineIndex));

					for (auto i = 0; i < nBlocks - 1; i++)
					{
						assert(pBuf[i]._char_pos >= 0 && pBuf[i]._char_pos <= nLength);

						SetTextColor(hdc, GetColor(pBuf[i]._color));

						draw_line(hdc, origin, rc, pBuf[i]._color, pszChars,
						          pBuf[i]._char_pos, pBuf[i + 1]._char_pos - pBuf[i]._char_pos,
						          text_location(pBuf[i]._char_pos, lineIndex));
					}

					assert(pBuf[nBlocks - 1]._char_pos >= 0 && pBuf[nBlocks - 1]._char_pos <= nLength);

					SetTextColor(hdc, GetColor(pBuf[nBlocks - 1]._color));

					draw_line(hdc, origin, rc, pBuf[nBlocks - 1]._color, pszChars,
					          pBuf[nBlocks - 1]._char_pos, nLength - pBuf[nBlocks - 1]._char_pos,
					          text_location(pBuf[nBlocks - 1]._char_pos, lineIndex));
				}
				else
				{
					SetTextColor(hdc, GetColor(color_index::COLORINDEX_NORMALTEXT));
					draw_line(hdc, origin, rc, color_index::COLORINDEX_NORMALTEXT, pszChars, 0, nLength, text_location(0, lineIndex));
				}

				//	Draw whitespaces to the left of the text
				auto frect = rc;

				if (origin.x > frect.left)
					frect.left = origin.x;

				if (frect.right > frect.left)
				{
					if (_doc.is_inside_selection(text_location(nLength, lineIndex)))
					{
						FillSolidRect(hdc, frect.left, frect.top, _font_extent.cx, frect.Height(), GetColor(color_index::COLORINDEX_SELBKGND));
						frect.left += _font_extent.cx;
					}
					if (frect.right > frect.left)
						FillSolidRect(hdc, frect, bDrawWhitespace ? crBkgnd : GetColor(color_index::COLORINDEX_WHITESPACE));
				}

				_freea(pBuf);
			}
		}
	}

	COLORREF GetColor(color_index nColorIndex) const
	{
		switch (nColorIndex)
		{
		case color_index::COLORINDEX_WHITESPACE:
		case color_index::COLORINDEX_BKGND:
			return RGB(30, 30, 30);
		case color_index::COLORINDEX_NORMALTEXT:
			return RGB(240, 240, 240);
		case color_index::COLORINDEX_SELMARGIN:
			return RGB(44, 44, 44);
		case color_index::COLORINDEX_PREPROCESSOR:
			return RGB(128, 128, 192);
		case color_index::COLORINDEX_COMMENT:
			return RGB(128, 128, 128);
		case color_index::COLORINDEX_NUMBER:
			return RGB(123, 234, 123);
		case color_index::COLORINDEX_OPERATOR:
			return RGB(128, 255, 128);
		case color_index::COLORINDEX_KEYWORD:
			return RGB(128, 128, 255);
		case color_index::COLORINDEX_SELBKGND:
			return RGB(88, 88, 88);
		case color_index::COLORINDEX_SELTEXT:
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


	void draw_margin(HDC hdc, const CRect& rect, int lineIndex) const
	{
		FillSolidRect(hdc, rect, GetColor(m_bSelMargin ? color_index::COLORINDEX_SELMARGIN : color_index::COLORINDEX_BKGND));
	}

public:

	void draw(HDC hdc)
	{
		auto oldFont = SelectObject(hdc, _font);
		auto rcClient = client_rect();
		auto line_count = _doc.size();
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
				draw_margin(hdc, rcCacheMargin, nCurrentLine);
				draw_line(hdc, rcCacheLine, nCurrentLine);
			}
			else
			{
				draw_margin(hdc, rcCacheMargin, -1);
				draw_line(hdc, rcCacheLine, -1);
			}

			nCurrentLine++;
			y += nLineHeight;
		}

		SelectObject(hdc, oldFont);
	}

	void move_page(bool down, bool selecting)
	{
		int y = Clamp(down ? _char_offset.cy + m_nScreenLines - 1 : _char_offset.cy - m_nScreenLines + 1, 0, _doc.size() - 1);

		if (_char_offset.cy != y)
		{
			ScrollToLine(y);
			update_caret();
		}

		auto pos = _doc.cursor_pos();
		pos.y = Clamp(down ? pos.y + (m_nScreenLines - 1) : pos.y - (m_nScreenLines - 1), 0, _doc.size() - 1);
		_doc.move_to(pos, selecting);
	}
};
