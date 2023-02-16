#pragma once

#include "document.h"
#include "resource.h"
#include "ui.h"

COLORREF style_to_color(style style_index);

class find_wnd : public ui::win_impl
{
public:
	const int editId = 101;
	const int nextId = 103;
	const int lastId = 104;

	HFONT _font = nullptr;
	document& _doc;
	win _find_edit;
	win _find_button;

	find_wnd(document& d) : _doc(d)
	{
	}

	LRESULT handle_message(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) override
	{
		if (uMsg == WM_CREATE) return on_create(uMsg, wParam, lParam);
		if (uMsg == WM_SIZE) return on_size(uMsg, wParam, lParam);
		if (uMsg == WM_ERASEBKGND) return on_erase_background(uMsg, wParam, lParam);
		if (uMsg == WM_COMMAND) return on_command(uMsg, wParam, lParam);
		return DefWindowProc(hWnd, uMsg, wParam, lParam);
	}

	LRESULT on_create(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam)
	{
		_find_edit.create_control(L"EDIT", m_hWnd, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0,
		                          editId);

		constexpr auto button_style = WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_FLAT;

		_find_button.create_control(L"BUTTON", m_hWnd, button_style, 0, nextId);
		SetWindowText(_find_button.m_hWnd, L"Find");

		return 0;
	}

	void update_font(const double scale_factor)
	{
		_font = CreateFont(20 * scale_factor, 0, 0, 0, FW_DONTCARE, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
		                   OUT_OUTLINE_PRECIS,
		                   CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, TEXT("Calibri"));

		ui::set_font(_find_edit.m_hWnd, _font);
		ui::set_font(_find_button.m_hWnd, _font);
		ui::set_font(m_hWnd, _font);
	}

	LRESULT on_size(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/) const
	{
		auto r = get_client_rect();

		r.left += 4;
		r.right -= 54;
		r.top += 4;
		r.bottom -= 4;

		_find_edit.move_window(r);

		r.left = r.right + 4;
		r.right += 50;

		_find_button.move_window(r);

		return 0;
	}

	LRESULT on_erase_background(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam) const
	{
		auto r = get_client_rect();
		ui::fill_solid_rect(reinterpret_cast<HDC>(wParam), r, style_to_color(style::main_wnd_clr));
		return 1;
	}

	LRESULT OnPaint(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam) const
	{
		auto r = get_client_rect();

		PAINTSTRUCT ps = {nullptr};
		const auto hdc = BeginPaint(m_hWnd, &ps);
		ui::fill_solid_rect(hdc, r, style_to_color(style::main_wnd_clr));
		EndPaint(m_hWnd, &ps);
		return 0;
	}

	std::wstring find_text() const
	{
		return ui::window_text(_find_edit.m_hWnd);
	}

	void Text(std::wstring s) const
	{
		SetWindowText(_find_edit.m_hWnd, s.c_str());
	}

	LRESULT on_command(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam) const
	{
		const auto id = LOWORD(wParam);
		const auto code = HIWORD(wParam);
		if (id == nextId) _doc.find(find_text(), 0);
		if (id == editId && code == EN_CHANGE) _doc.find(find_text(), find_start_selection);
		return DefWindowProc(m_hWnd, WM_COMMAND, wParam, lParam);
	}
};

static FORMATETC ascii_text_format = {CF_TEXT, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
static FORMATETC utf16_text_format = {CF_UNICODETEXT, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
static FORMATETC file_drop_format = {CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};

class text_view : public ui::win_impl, public IDropTarget, public IView
{
private:
	document& _doc;
	find_wnd& _find;
	IEvents& _events;

	volatile uint32_t _ref_count = 0;

	text_location _pt_drop_pos;
	bool _drag_selection = false;
	bool _word_selection = false;
	bool _line_selection = false;
	bool _preparing_to_drag = false;
	bool _drop_pos_visible = false;
	bool _cursor_hidden = false;
	bool _is_focused = false;
	bool _sel_margin = true;
	bool _dragging_text = false;
	UINT _drag_sel_timer = 0;
	text_location _saved_caret_pos;
	text_selection _dragged_text_selection;
	HFONT _font = nullptr;

	isize _char_offset;
	isize _extent;
	isize _font_extent;

	int _screen_lines = 0;
	int _screen_chars = 0;
	

public:
	text_view(document& d, find_wnd& f, IEvents &events) : _doc(d), _find(f), _events(events)
	{
	}

	~text_view() override = default;


	LRESULT handle_message(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) override
	{
		if (uMsg == WM_CREATE) return on_create(uMsg, wParam, lParam);
		if (uMsg == WM_DESTROY) return on_destroy(uMsg, wParam, lParam);
		if (uMsg == WM_SIZE) return on_size(uMsg, wParam, lParam);
		if (uMsg == WM_SETFOCUS) return on_set_focus(uMsg, wParam, lParam);
		if (uMsg == WM_KILLFOCUS) return on_kill_focus(uMsg, wParam, lParam);
		if (uMsg == WM_PAINT) return on_paint(uMsg, wParam, lParam);
		if (uMsg == WM_ERASEBKGND) return on_erase_background(uMsg, wParam, lParam);
		if (uMsg == WM_VSCROLL) return on_v_scroll(uMsg, wParam, lParam);
		if (uMsg == WM_HSCROLL) return on_h_scroll(uMsg, wParam, lParam);
		if (uMsg == WM_TIMER) return on_timer(uMsg, wParam, lParam);
		if (uMsg == WM_SYSCOLORCHANGE) return on_sys_color_change(uMsg, wParam, lParam);
		if (uMsg == WM_LBUTTONDBLCLK) return on_left_button_dbl_clk(uMsg, wParam, lParam);
		if (uMsg == WM_LBUTTONDOWN) return on_left_button_down(uMsg, wParam, lParam);
		if (uMsg == WM_LBUTTONUP) return on_left_button_up(uMsg, wParam, lParam);
		if (uMsg == WM_MOUSEMOVE) return on_mouse_move(uMsg, wParam, lParam);
		if (uMsg == WM_MOUSEWHEEL) return on_mouse_wheel(uMsg, wParam, lParam);
		if (uMsg == WM_CHAR) return on_char(uMsg, wParam, lParam);
		if (uMsg == WM_CONTEXTMENU) return on_context_menu(uMsg, wParam, lParam);
		return DefWindowProc(hWnd, uMsg, wParam, lParam);
	}

	void update_font(const double scale_factor)
	{
		LOGFONT lf;
		memset(&lf, 0, sizeof(lf));
		lf.lfHeight = 24 * scale_factor;
		lf.lfWeight = FW_NORMAL;
		lf.lfCharSet = ANSI_CHARSET;
		lf.lfOutPrecision = OUT_DEFAULT_PRECIS;
		lf.lfClipPrecision = CLIP_DEFAULT_PRECIS;
		lf.lfQuality = CLEARTYPE_NATURAL_QUALITY;
		lf.lfPitchAndFamily = DEFAULT_PITCH | FF_DONTCARE;
		wcscpy_s(lf.lfFaceName, L"Consolas");

		if (_font) DeleteObject(_font);
		_font = ::CreateFontIndirect(&lf);

		_doc.invalidate(invalid::layout);
	}

	LRESULT on_create(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/)
	{
		RegisterDragDrop(m_hWnd, this);
		return 0;
	}

	LRESULT on_destroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/) const
	{
		RevokeDragDrop(m_hWnd);
		return 0;
	}

	LRESULT on_size(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam)
	{
		const ui::win_dc hdc(m_hWnd);
		const auto old_font = hdc.SelectFont(_font);

		isize font_extent;
		GetTextExtentExPoint(hdc, _T("X"), 1, 1, nullptr, nullptr, &font_extent);
		if (font_extent.cy < 1) font_extent.cy = 1;

		/*
		TEXTMETRIC tm;
		if (hdc->GetTextMetrics(&tm))
		m_nCharWidth -= tm.tmOverhang;
		*/

		const auto xPos = GET_X_LPARAM(lParam);
		const auto yPos = GET_Y_LPARAM(lParam);
		_extent = isize(xPos, yPos);
		_font_extent = font_extent;

		layout();

		hdc.SelectFont(old_font);


		return 0;
	}

	LRESULT on_paint(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam) const
	{
		PAINTSTRUCT ps = {nullptr};
		const auto hdc = BeginPaint(m_hWnd, &ps);
		draw(hdc);
		EndPaint(m_hWnd, &ps);
		return 0;
	}

	static LRESULT on_erase_background(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam)
	{
		return 1;
	}

	LRESULT on_timer(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/)
	{
		on_timer(wParam);
		return 0;
	}

	LRESULT on_sys_color_change(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/) const
	{
		invalidate();
		return 0;
	}

	LRESULT on_set_focus(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam)
	{
		OnSetFocus(reinterpret_cast<HWND>(wParam));
		return 0;
	}

	LRESULT on_kill_focus(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam)
	{
		on_kill_focus(reinterpret_cast<HWND>(wParam));
		return 0;
	}

	LRESULT on_v_scroll(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam)
	{
		const auto nScrollCode = static_cast<int>(LOWORD(wParam));
		const auto y = static_cast<short int>(HIWORD(wParam));
		const auto hwndScrollBar = reinterpret_cast<HWND>(lParam);
		on_v_scroll(nScrollCode, y, hwndScrollBar);
		return 0;
	}

	LRESULT on_h_scroll(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam)
	{
		const auto nScrollCode = static_cast<int>(LOWORD(wParam));
		const auto nPos = static_cast<short int>(HIWORD(wParam));
		const auto hwndScrollBar = reinterpret_cast<HWND>(lParam);
		on_h_scroll(nScrollCode, nPos, hwndScrollBar);
		return 0;
	}

	LRESULT on_left_button_dbl_clk(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam)
	{
		on_left_button_dbl_clk(ipoint(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)), wParam);
		return 0;
	}

	LRESULT on_left_button_down(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam)
	{
		on_left_button_down(ipoint(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)), wParam);
		return 0;
	}

	LRESULT on_left_button_up(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam)
	{
		on_left_button_up(ipoint(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)), wParam);
		return 0;
	}

	LRESULT on_mouse_move(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam)
	{
		on_mouse_move(ipoint(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)), wParam);
		return 0;
	}

	LRESULT on_mouse_wheel(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam)
	{
		const auto delta = static_cast<short>(HIWORD(wParam)) > 0 ? -2 : 2;
		on_mouse_wheel(ipoint(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)), delta);
		return 0;
	}

	LRESULT on_char(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam) const
	{
		const auto c = wParam;
		auto flags = lParam;

		if ((GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0 ||
			(GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0)
			return 0;

		if (c == VK_RETURN)
		{
			if (_doc.QueryEditable())
			{
				undo_group ug(_doc);
				const auto pos = _doc.delete_text(ug, _doc.selection());
				_doc.select(_doc.insert_text(ug, pos, L'\n'));
			}
		}
		else if (c > 31)
		{
			if (_doc.QueryEditable())
			{
				undo_group ug(_doc);
				const auto pos = _doc.delete_text(ug, _doc.selection());
				_doc.select(_doc.insert_text(ug, pos, c));
			}
		}

		return 0;
	}

	LRESULT on_context_menu(UINT uMsg, WPARAM wParam, LPARAM lParam)
	{
		const ipoint location(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
		ipoint clientLocation(location);
		ScreenToClient(m_hWnd, &clientLocation);

		const auto menu = CreatePopupMenu();

		if (menu)
		{
			const auto loc = client_to_text(clientLocation);
			const auto selection = _doc.is_inside_selection(loc) ? _doc.selection() : _doc.word_selection(loc, false);

			_doc.select(selection);

			const auto word = str::combine(_doc.text(selection));

			std::map<UINT, std::wstring> replacements;

			if (!selection.empty())
			{
				auto id = 1000U;


				for (auto option : _doc.suggest(word))
				{
					auto title = str::replace(option, L"&", L"&&");
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
					AppendMenu(menu, MF_ENABLED, ID_FILE_NEW, std::format(L"Add '{}'", word).c_str());
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

			const auto result = TrackPopupMenu(
				menu, TPM_TOPALIGN | TPM_LEFTALIGN | TPM_NONOTIFY | TPM_RETURNCMD | TPM_RIGHTBUTTON |
				TPM_VERNEGANIMATION, location.x, location.y, 0, m_hWnd, nullptr);
			DestroyMenu(menu);

			switch (result)
			{
			case ID_EDIT_UNDO:
			case ID_EDIT_CUT:
			case ID_EDIT_COPY:
			case ID_EDIT_PASTE:
			case ID_EDIT_DELETE:
			case ID_EDIT_SELECT_ALL:
				on_command(result);
				break;

			case ID_FILE_NEW:
				_doc.add_word(word);
				break;

			default:

				if (replacements.contains(result))
				{
					undo_group ug(_doc);
					_doc.select(_doc.replace_text(ug, selection, replacements[result]));
				}
				break;
			}
		}

		return 0;
	}

	void on_command(int id)
	{
		switch (id)
		{
		case ID_EDIT_COPY: _doc.Copy();
			break;
		case ID_EDIT_SELECT_ALL: _doc.select(_doc.all());
			break;
		//case ID_EDIT_FIND: _doc.find(); break;
		case ID_EDIT_REPEAT: _doc.find(_find.find_text(), 0);
			break;
		case ID_EDIT_FIND_PREVIOUS: _doc.find(_find.find_text(), find_direction_up);
			break;
		case ID_EDIT_CHAR_LEFT: _doc.move_char_left(false);
			break;
		case ID_EDIT_EXT_CHAR_LEFT: _doc.move_char_left(true);
			break;
		case ID_EDIT_CHAR_RIGHT: _doc.move_char_right(false);
			break;
		case ID_EDIT_EXT_CHAR_RIGHT: _doc.move_char_right(true);
			break;
		case ID_EDIT_WORD_LEFT: _doc.move_word_left(false);
			break;
		case ID_EDIT_EXT_WORD_LEFT: _doc.move_word_left(true);
			break;
		case ID_EDIT_WORD_RIGHT: _doc.move_word_right(false);
			break;
		case ID_EDIT_EXT_WORD_RIGHT: _doc.move_word_right(true);
			break;
		case ID_EDIT_LINE_UP: _doc.move_lines(-1, false);
			break;
		case ID_EDIT_EXT_LINE_UP: _doc.move_lines(-1, true);
			break;
		case ID_EDIT_LINE_DOWN: _doc.move_lines(1, false);
			break;
		case ID_EDIT_EXT_LINE_DOWN: _doc.move_lines(1, true);
			break;
		case ID_EDIT_SCROLL_UP: scroll_up();
			break;
		case ID_EDIT_SCROLL_DOWN: scroll_down();
			break;
		case ID_EDIT_PAGE_UP: _doc.move_lines(-_screen_lines, false);
			break;
		case ID_EDIT_EXT_PAGE_UP: _doc.move_lines(-_screen_lines, false);
			break;
		case ID_EDIT_PAGE_DOWN: _doc.move_lines(_screen_lines, false);
			break;
		case ID_EDIT_EXT_PAGE_DOWN: _doc.move_lines(_screen_lines, true);
			break;
		case ID_EDIT_LINE_END: _doc.move_line_end(false);
			break;
		case ID_EDIT_EXT_LINE_END: _doc.move_line_end(true);
			break;
		case ID_EDIT_HOME: _doc.move_line_home(false);
			break;
		case ID_EDIT_EXT_HOME: _doc.move_line_home(true);
			break;
		case ID_EDIT_TEXT_BEGIN: _doc.move_doc_home(false);
			break;
		case ID_EDIT_EXT_TEXT_BEGIN: _doc.move_doc_home(true);
			break;
		case ID_EDIT_TEXT_END: _doc.move_doc_end(false);
			break;
		case ID_EDIT_EXT_TEXT_END: _doc.move_doc_end(true);
			break;
		case ID_EDIT_PASTE: _doc.edit_paste();
			break;
		case ID_EDIT_CUT: _doc.edit_cut();
			break;
		case ID_EDIT_DELETE: _doc.edit_delete();
			break;
		case ID_EDIT_DELETE_BACK: _doc.edit_delete_back();
			break;
		case ID_EDIT_UNTAB: _doc.edit_untab();
			break;
		case ID_EDIT_TAB: _doc.edit_tab();
			break;
		//case ID_EDIT_REPLACE: _doc.OnEditReplace(); break;
		case ID_EDIT_UNDO: _doc.edit_undo();
			break;
		case ID_EDIT_REDO: _doc.edit_redo();
			break;
		}
	}

	void invalidate_selection()
	{
		const auto sel = _doc.selection();

		if (!sel.empty())
			invalidate_lines(sel._start.y, sel._end.y);
	}

	void OnSetFocus(HWND oldWnd)
	{
		_is_focused = true;
		invalidate_selection();
		update_caret();
	}

	void on_kill_focus(HWND newWnd)
	{
		_is_focused = false;

		update_caret();
		invalidate_selection();

		if (_drag_selection)
		{
			ReleaseCapture();
			KillTimer(m_hWnd, _drag_sel_timer);
			_drag_selection = false;
		}
	}

	void on_timer(UINT nIDEvent)
	{
		if (nIDEvent == TIMER_DRAGSEL)
		{
			assert(m_bDragSelection);
			ipoint pt;
			GetCursorPos(&pt);
			ScreenToClient(m_hWnd, &pt);

			const auto rcClient = client_rect();
			auto bChanged = false;
			auto y = _char_offset.cy;
			const auto line_count = _doc.size();

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

			y = clamp(y, 0, line_count - 1);

			if (_char_offset.cy != y)
			{
				ScrollToLine(y);
				bChanged = true;
			}

			//	Scroll horizontally, if necessary
			auto x = _char_offset.cx;
			const auto nMaxLineLength = _doc.max_line_length();

			if (pt.x < rcClient.left)
			{
				x--;
			}
			else if (pt.x >= rcClient.right)
			{
				x++;
			}

			x = clamp(x, 0, nMaxLineLength - 1);

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

	void on_left_button_down(const ipoint& point, UINT nFlags)
	{
		const bool bShift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
		const bool bControl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;

		SetFocus(m_hWnd);

		if (point.x < margin_width())
		{
			if (bControl)
			{
				_doc.select(_doc.all());
			}
			else
			{
				const auto sel = _doc.line_selection(client_to_text(point), bShift);
				_doc.select(sel);

				SetCapture(m_hWnd);
				_drag_sel_timer = SetTimer(m_hWnd, TIMER_DRAGSEL, 100, nullptr);
				assert(m_nDragSelTimer != 0);
				_word_selection = false;
				_line_selection = true;
				_drag_selection = true;
			}
		}
		else
		{
			const auto ptText = client_to_text(point);

			if (_doc.is_inside_selection(ptText))
			{
				_preparing_to_drag = true;
			}
			else
			{
				const auto pos = client_to_text(point);
				const auto sel = bControl ? _doc.word_selection(pos, bShift) : _doc.pos_selection(pos, bShift);
				_doc.select(sel);

				SetCapture(m_hWnd);
				_drag_sel_timer = SetTimer(m_hWnd, TIMER_DRAGSEL, 100, nullptr);
				assert(m_nDragSelTimer != 0);
				_word_selection = bControl;
				_line_selection = false;
				_drag_selection = true;
			}
		}
	}

	bool can_scroll() const
	{
		const auto line_count = _doc.size() - 1;
		return _screen_chars < line_count;
	}

	void on_mouse_wheel(const ipoint& point, int zDelta)
	{
		if (can_scroll())
		{
			ScrollToLine(clamp(_char_offset.cy + zDelta, 0, _doc.size()));
			update_caret();
		}
	}

	void on_mouse_move(const ipoint& point, UINT nFlags)
	{
		if (_drag_selection)
		{
			const auto bOnMargin = point.x < margin_width();
			const auto pos = client_to_text(point);

			if (_line_selection)
			{
				if (bOnMargin)
				{
					const auto sel = _doc.line_selection(pos, true);
					_doc.select(sel);
					return;
				}

				_line_selection = _word_selection = false;
				update_cursor();
			}

			const auto sel = _word_selection ? _doc.word_selection(pos, true) : _doc.pos_selection(pos, true);
			_doc.select(sel);
		}

		if (_preparing_to_drag)
		{
			_preparing_to_drag = false;
			const auto hData = prepare_drag_data();

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

	static DROPEFFECT GetDropEffect()
	{
		return DROPEFFECT_COPY | DROPEFFECT_MOVE;
	}

	void on_left_button_up(const ipoint& point, UINT nFlags)
	{
		if (_drag_selection)
		{
			const auto pos = client_to_text(point);

			if (_line_selection)
			{
				const auto sel = _doc.line_selection(pos, true);
				_doc.select(sel);
			}
			else
			{
				const auto sel = _word_selection ? _doc.word_selection(pos, true) : _doc.pos_selection(pos, true);
				_doc.select(sel);
			}

			ReleaseCapture();
			KillTimer(m_hWnd, _drag_sel_timer);
			_drag_selection = false;
		}

		if (_preparing_to_drag)
		{
			_preparing_to_drag = false;
			_doc.select(client_to_text(point));
		}
	}

	void on_left_button_dbl_clk(const ipoint& point, UINT nFlags)
	{
		if (!_drag_selection)
		{
			const bool bShift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
			_doc.select(_doc.word_selection(client_to_text(point), bShift));

			SetCapture(m_hWnd);
			_drag_sel_timer = SetTimer(m_hWnd, TIMER_DRAGSEL, 100, nullptr);
			assert(m_nDragSelTimer != 0);
			_word_selection = true;
			_line_selection = false;
			_drag_selection = true;
		}
	}

	void OnRButtonDown(const ipoint& point, UINT nFlags)
	{
		const auto pt = client_to_text(point);

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
			const int nScrollChars = _char_offset.cx - x;
			_char_offset.cx = x;
			auto rcScroll = client_rect();
			rcScroll.left += margin_width();
			ScrollWindowEx(m_hWnd, nScrollChars * _font_extent.cx, 0, rcScroll, rcScroll, nullptr, nullptr,
			               SW_INVALIDATE);
			recalc_horz_scrollbar();
		}
	}

	void ScrollToLine(int y)
	{
		if (_char_offset.cy != y)
		{
			const int nScrollLines = _char_offset.cy - y;
			_char_offset.cy = y;
			ScrollWindowEx(m_hWnd, 0, nScrollLines * _font_extent.cy, nullptr, nullptr, nullptr, nullptr,
			               SW_INVALIDATE);
			recalc_vert_scrollbar();
		}
	}

	void recalc_vert_scrollbar()
	{
		if (_screen_lines >= _doc.size() && _char_offset.cy > 0)
		{
			_char_offset.cy = 0;
			invalidate();
			update_caret();
		}

		SCROLLINFO si;
		si.cbSize = sizeof(si);
		si.fMask = SIF_PAGE | SIF_POS | SIF_RANGE;
		si.nMin = 0;
		si.nMax = _doc.size() - 1;
		si.nPage = _screen_lines;
		si.nPos = _char_offset.cy;
		SetScrollInfo(m_hWnd, SB_VERT, &si, TRUE);
	}

	void on_v_scroll(UINT nSBCode, UINT nPos, HWND pScrollBar)
	{
		SCROLLINFO si;
		si.cbSize = sizeof(si);
		si.fMask = SIF_ALL;
		GetScrollInfo(m_hWnd, SB_VERT, &si);

		const int nPageLines = _screen_lines;
		const int line_count = _doc.size();

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

		ScrollToLine(clamp(y, 0, line_count - 1));
		update_caret();
	}

	void recalc_horz_scrollbar()
	{
		if (_screen_chars >= _doc.max_line_length() && _char_offset.cx > 0)
		{
			_char_offset.cx = 0;
			invalidate();
			update_caret();
		}

		const auto margin_width = _sel_margin ? 3 : 0;

		SCROLLINFO si;
		si.cbSize = sizeof(si);
		si.fMask = SIF_PAGE | SIF_POS | SIF_RANGE;
		si.nMin = 0;
		si.nMax = _doc.max_line_length() + margin_width;
		si.nPage = _screen_chars;
		si.nPos = _char_offset.cx;

		SetScrollInfo(m_hWnd, SB_HORZ, &si, TRUE);
	}

	void on_h_scroll(UINT nSBCode, UINT nPos, HWND pScrollBar)
	{
		SCROLLINFO si;
		si.cbSize = sizeof(si);
		si.fMask = SIF_ALL;
		GetScrollInfo(m_hWnd, SB_HORZ, &si);

		const int nPageChars = _screen_chars;
		const int nMaxLineLength = _doc.max_line_length();

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

		ScrollToChar(clamp(nNewOffset, 0, nMaxLineLength - 1));
		update_caret();
	}

	bool OnSetCursor(HWND wnd, UINT nHitTest, UINT message) const
	{
		if (nHitTest == HTCLIENT)
		{
			update_cursor();
			return true;
		}
		return false;
	}

	void update_cursor() const
	{
		static auto arrow = ::LoadCursor(nullptr, MAKEINTRESOURCE(IDC_ARROW));
		static auto beam = ::LoadCursor(nullptr, MAKEINTRESOURCE(IDC_IBEAM));

		ipoint pt;
		GetCursorPos(&pt);
		ScreenToClient(m_hWnd, &pt);

		if (pt.x < margin_width())
		{
			SetCursor(arrow);
		}
		else if (_doc.is_inside_selection(client_to_text(pt)))
		{
			SetCursor(arrow);
		}
		else
		{
			SetCursor(beam);
		}
	}

	void update_caret() const
	{
		const auto pos = _doc.cursor_pos();

		if (_is_focused && !_cursor_hidden &&
			_doc.calc_offset(pos.y, pos.x) >= _char_offset.cx)
		{
			CreateCaret(m_hWnd, nullptr, 2, _font_extent.cy);

			const auto pt = text_to_client(pos);
			SetCaretPos(pt.x, pt.y);
			ShowCaret(m_hWnd);
		}
		else
		{
			HideCaret(m_hWnd);
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
		InterlockedIncrement(&_ref_count);
		return _ref_count;
	}

	ULONG STDMETHODCALLTYPE Release() override
	{
		return InterlockedDecrement(&_ref_count);
	}

	static bool can_drop(IDataObject* pDataObj)
	{
		return pDataObj->QueryGetData(&ascii_text_format) == S_OK ||
			pDataObj->QueryGetData(&utf16_text_format) == S_OK ||
			pDataObj->QueryGetData(&file_drop_format) == S_OK;
	}


	DWORD drop_effect(IDataObject* pDataObj, const ipoint& loc)
	{
		if (can_drop(pDataObj))
		{
			show_drop_indicator(loc);
			return (GetKeyState(VK_CONTROL) < 0) ? DROPEFFECT_COPY : DROPEFFECT_MOVE;
		}
		hide_drop_indicator();
		return DROPEFFECT_NONE;
	}

	HRESULT STDMETHODCALLTYPE DragEnter(IDataObject* pDataObj, DWORD grfKeyState, POINTL pt, DWORD* pdwEffect) override
	{
		*pdwEffect = drop_effect(pDataObj, pt);
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE DragLeave(void) override
	{
		hide_drop_indicator();
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

	DROPEFFECT OnDragScroll(HWND wnd, DWORD dwKeyState, ipoint point)
	{
		assert(m_hWnd == wnd);

		const auto rcClientRect = client_rect();

		if (point.y < rcClientRect.top + DRAG_BORDER_Y)
		{
			hide_drop_indicator();
			scroll_up();
			show_drop_indicator(point);
		}
		else if (point.y >= rcClientRect.bottom - DRAG_BORDER_Y)
		{
			hide_drop_indicator();
			scroll_down();
			show_drop_indicator(point);
		}
		else if (point.x < rcClientRect.left + margin_width() + DRAG_BORDER_X)
		{
			hide_drop_indicator();
			ScrollLeft();
			show_drop_indicator(point);
		}
		else if (point.x >= rcClientRect.right - DRAG_BORDER_X)
		{
			hide_drop_indicator();
			ScrollRight();
			show_drop_indicator(point);
		}

		if (dwKeyState & MK_CONTROL)
			return DROPEFFECT_COPY;
		return DROPEFFECT_MOVE;
	}

	void scroll_up()
	{
		if (_char_offset.cy > 0)
		{
			ScrollToLine(_char_offset.cy - 1);
			update_caret();
		}
	}

	void scroll_down()
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

	bool DropData(IDataObject* pDataObject, const ipoint& ptClient) const
	{
		STGMEDIUM stgmed;
		std::wstring text;

		if (SUCCEEDED(pDataObject->GetData(&utf16_text_format, &stgmed)))
		{
			//unicode text
			const auto data = static_cast<const wchar_t*>(GlobalLock(stgmed.hGlobal));
			text = data;
			GlobalUnlock(stgmed.hGlobal);
			ReleaseStgMedium(&stgmed);
		}
		else if (SUCCEEDED(pDataObject->GetData(&ascii_text_format, &stgmed)))
		{
			//ascii text
			const auto data = static_cast<const char*>(GlobalLock(stgmed.hGlobal));
			text = str::ascii_to_utf16(data);
			GlobalUnlock(stgmed.hGlobal);
			ReleaseStgMedium(&stgmed);
		}
		else if (SUCCEEDED(pDataObject->GetData(&file_drop_format, &stgmed)))
		{
			const auto data = static_cast<const char*>(GlobalLock(stgmed.hGlobal));
			const auto files = reinterpret_cast<const DROPFILES*>(data);
			text = reinterpret_cast<const wchar_t*>(data + files->pFiles);
			GlobalUnlock(stgmed.hGlobal);
			ReleaseStgMedium(&stgmed);
			
			_events.path_selected({ text });
			return true;
		}

		const auto drop_loc = client_to_text(ptClient);

		if (_dragging_text && _doc.is_inside_selection(drop_loc))
		{
			return false;
		}
		undo_group ug(_doc);
		_doc.select(_doc.insert_text(ug, drop_loc, text));

		return true;
	}

	void show_drop_indicator(const ipoint& point)
	{
		if (!_drop_pos_visible)
		{
			//HideCursor();
			_saved_caret_pos = _doc.cursor_pos();
			_drop_pos_visible = true;
			CreateCaret(m_hWnd, reinterpret_cast<HBITMAP>(1), 2, _font_extent.cy);
		}
		_pt_drop_pos = client_to_text(point);
		if (_pt_drop_pos.x >= _char_offset.cx)
		{
			const auto pt = text_to_client(_pt_drop_pos);
			SetCaretPos(pt.x, pt.y);
			ShowCaret(m_hWnd);
		}
		else
		{
			HideCaret(m_hWnd);
		}
	}

	void hide_drop_indicator()
	{
		if (_drop_pos_visible)
		{
			_doc.cursor_pos(_saved_caret_pos);
			//ShowCursor();
			_drop_pos_visible = false;
		}
	}

	void show_cursor()
	{
		_cursor_hidden = false;
		update_caret();
	}

	void hide_cursor()
	{
		_cursor_hidden = true;
		update_caret();
	}

	std::wstring text_from_clipboard() const override
	{
		std::wstring result;
		const auto pThis = const_cast<text_view*>(this);

		if (OpenClipboard(pThis->m_hWnd))
		{
			const auto hData = GetClipboardData(CF_UNICODETEXT);

			if (hData != nullptr)
			{
				const auto pszData = static_cast<const wchar_t*>(GlobalLock(hData));

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

	bool text_to_clipboard(std::wstring_view text) override
	{
		// TODO CWaitCursor wc;
		auto success = false;

		if (OpenClipboard(m_hWnd))
		{
			EmptyClipboard();

			const auto len = text.size() + 1;
			const auto hData = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, len * sizeof(wchar_t));

			if (hData != nullptr)
			{
				const auto pszData = static_cast<wchar_t*>(GlobalLock(hData));
				wcsncpy_s(pszData, len, text.data(), text.size());
				GlobalUnlock(hData);
				success = SetClipboardData(CF_UNICODETEXT, hData) != nullptr;
			}
			CloseClipboard();
		}

		return success;
	}

	void invalidate(irect r = {}) const
	{
		InvalidateRect(m_hWnd, r.Width() > 0 ? static_cast<LPCRECT>(r) : nullptr, FALSE);
	}

	HGLOBAL prepare_drag_data()
	{
		const auto sel = _doc.selection();

		if (sel.empty())
			return nullptr;

		const auto text = str::combine(_doc.text(sel));
		const auto len = text.size() + 1;
		const auto hData = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, len * sizeof(wchar_t));

		if (hData == nullptr)
			return nullptr;

		const auto pszData = static_cast<wchar_t*>(GlobalLock(hData));
		wcscpy_s(pszData, len, text.c_str());
		GlobalUnlock(hData);

		_dragged_text_selection = sel;

		return hData;
	}


	int client_to_line(const ipoint& point) const
	{
		const auto line_count = _doc.size();

		const auto result = (point.y / _font_extent.cy) + _char_offset.cy;
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

		return clamp(result, 0, line_count - 1);
	}

	text_location client_to_text(const ipoint& point) const
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

			pt.x = clamp(i, 0, lineSize);
		}

		return pt;
	}

	ipoint text_to_client(const text_location& point) const
	{
		ipoint pt;

		if (point.y >= 0 && point.y < _doc.size())
		{
			pt.y = line_offset(point.y) - top_offset();
			pt.x = 0;

			const auto tabSize = _doc.tab_size();
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
		const auto result = _char_offset.cy * _font_extent.cy;

		/*if (!_doc.empty() || _char_offset.cy <= 0)
		{
		result = _doc[_char_offset.cy]._y;
		}*/

		return result;
	}

	void invalidate_lines(int start, int end) override
	{
		auto rcInvalid = client_rect();
		const auto top = top_offset();

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

	void invalidate_view()
	{
		_screen_chars = -1;

		layout();

		update_caret();
		recalc_vert_scrollbar();
		recalc_horz_scrollbar();
		invalidate();
	}

	void layout()
	{
		const auto rect = client_rect();
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

		_screen_lines = rect.Height() / _font_extent.cy;
		_screen_chars = rect.Width() / _font_extent.cx;

		recalc_vert_scrollbar();
		recalc_horz_scrollbar();
	}

	int line_offset(int lineIndex) const
	{
		/*auto max = _doc.size();
		auto line = clamp(lineIndex, 0, max - 1);
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
		const int line_count = _doc.size();
		int y = _char_offset.cy;

		if (pt.y >= y + _screen_lines)
		{
			y = pt.y - _screen_lines + 1;
		}
		else if (pt.y < y)
		{
			y = pt.y;
		}

		y = clamp(y, 0, line_count - 1);

		if (_char_offset.cy != y)
		{
			ScrollToLine(y);
		}

		//	Scroll horizontally
		const auto actual_pos = _doc.calc_offset(pt.y, pt.x);
		auto new_offset = _char_offset.cx;

		if (actual_pos > new_offset + _screen_chars)
		{
			new_offset = actual_pos - _screen_chars;
		}
		if (actual_pos < new_offset)
		{
			new_offset = actual_pos;
		}

		if (new_offset >= _doc.max_line_length())
			new_offset = _doc.max_line_length() - 1;
		if (new_offset < 0)
			new_offset = 0;

		if (_char_offset.cx != new_offset)
		{
			ScrollToChar(new_offset);
		}

		update_caret();
	}

	irect client_rect() const
	{
		return irect(0, 0, _extent.cx, _extent.cy);
	}

	int margin_width() const
	{
		return _sel_margin ? 20 : 1;
	}

	void draw_line(HDC pdc, text_location& ptOrigin, const irect& rcClip,
	               std::wstring_view pszChars, int nOffset, int nCount) const
	{
		if (nCount > 0)
		{
			const auto line = _doc.expanded_chars(pszChars, nOffset, nCount);
			const auto nWidth = rcClip.right - ptOrigin.x;

			if (nWidth > 0)
			{
				const auto nCharWidth = _font_extent.cx;
				auto nCount = line.size();
				const auto nCountFit = nWidth / nCharWidth + 1;

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

	void draw_line(HDC pdc, text_location& ptOrigin, const irect& rcClip, style nColorIndex,
	               std::wstring_view pszChars, int nOffset, int nCount, const text_location& ptTextPos) const
	{
		if (nCount > 0)
		{
			const auto sel = _doc.selection();
			auto nSelBegin = 0, nSelEnd = 0;

			if (sel._start.y > ptTextPos.y)
			{
				nSelBegin = nCount;
			}
			else if (sel._start.y == ptTextPos.y)
			{
				nSelBegin = clamp(sel._start.x - ptTextPos.x, 0, nCount);
			}
			if (sel._end.y > ptTextPos.y)
			{
				nSelEnd = nCount;
			}
			else if (sel._end.y == ptTextPos.y)
			{
				nSelEnd = clamp(sel._end.x - ptTextPos.x, 0, nCount);
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
				const auto crOldBk = SetBkColor(pdc, style_to_color(style::sel_bkgnd));
				const auto crOldText = SetTextColor(pdc, style_to_color(style::sel_text));
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

	void draw_line(HDC hdc, const irect& rc, int lineIndex) const
	{
		if (lineIndex == -1)
		{
			//	Draw line beyond the text
			ui::fill_solid_rect(hdc, rc, style_to_color(style::white_space));
		}
		else
		{
			//	Acquire the background color for the current line
			const auto draw_whitespace = true;
			const auto bg_color = style_to_color(style::normal_bkgnd);

			const auto& line = _doc[lineIndex];

			if (line.empty())
			{
				//	Draw the empty line
				auto rect = rc;

				if (_doc.is_inside_selection(text_location(0, lineIndex)))
				{
					ui::fill_solid_rect(hdc, rect.left, rect.top, _font_extent.cx, rect.Height(),
					                    style_to_color(style::sel_bkgnd));
					rect.left += _font_extent.cx;
				}

				ui::fill_solid_rect(hdc, rect, draw_whitespace ? bg_color : style_to_color(style::white_space));
			}
			else
			{
				//	Parse the line
				const auto nLength = line.size();
				const auto pBuf = static_cast<highlighter::text_block*>(_malloca(
					sizeof(highlighter::text_block) * nLength * 3));
				auto nBlocks = 0;
				const auto cookie = _doc.highlight_cookie(lineIndex - 1);

				line._parse_cookie = _doc.highlight_line(cookie, line, pBuf, nBlocks);

				//	Draw the line text
				text_location origin(rc.left - _char_offset.cx * _font_extent.cx, rc.top);
				SetBkColor(hdc, bg_color);

				const auto line_view = line.view();

				if (nBlocks > 0)
				{
					assert(pBuf[0]._char_pos >= 0 && pBuf[0]._char_pos <= nLength);

					SetTextColor(hdc, style_to_color(style::normal_text));
					draw_line(hdc, origin, rc, style::normal_text, line_view, 0, pBuf[0]._char_pos,
					          text_location(0, lineIndex));

					for (auto i = 0; i < nBlocks - 1; i++)
					{
						assert(pBuf[i]._char_pos >= 0 && pBuf[i]._char_pos <= nLength);

						SetTextColor(hdc, style_to_color(pBuf[i]._color));

						draw_line(hdc, origin, rc, pBuf[i]._color, line_view,
						          pBuf[i]._char_pos, pBuf[i + 1]._char_pos - pBuf[i]._char_pos,
						          text_location(pBuf[i]._char_pos, lineIndex));
					}

					assert(pBuf[nBlocks - 1]._char_pos >= 0 && pBuf[nBlocks - 1]._char_pos <= nLength);

					SetTextColor(hdc, style_to_color(pBuf[nBlocks - 1]._color));

					draw_line(hdc, origin, rc, pBuf[nBlocks - 1]._color, line_view,
					          pBuf[nBlocks - 1]._char_pos, nLength - pBuf[nBlocks - 1]._char_pos,
					          text_location(pBuf[nBlocks - 1]._char_pos, lineIndex));
				}
				else
				{
					SetTextColor(hdc, style_to_color(style::normal_text));
					draw_line(hdc, origin, rc, style::normal_text, line_view, 0, nLength,
					          text_location(0, lineIndex));
				}

				//	Draw whitespaces to the left of the text
				auto frect = rc;

				if (origin.x > frect.left)
					frect.left = origin.x;

				if (frect.right > frect.left)
				{
					if (_doc.is_inside_selection(text_location(nLength, lineIndex)))
					{
						ui::fill_solid_rect(hdc, frect.left, frect.top, _font_extent.cx, frect.Height(),
						                    style_to_color(style::sel_bkgnd));
						frect.left += _font_extent.cx;
					}
					if (frect.right > frect.left)
					{
						ui::fill_solid_rect(hdc, frect, draw_whitespace
							                                ? bg_color
							                                : style_to_color(style::white_space));
					}
				}

				_freea(pBuf);
			}
		}
	}

	
	void draw_margin(HDC hdc, const irect& rect, int lineIndex) const
	{
		ui::fill_solid_rect(
			hdc, rect, style_to_color(_sel_margin ? style::sel_margin : style::normal_bkgnd));
	}

public:
	void draw(HDC hdc) const
	{
		const auto oldFont = SelectObject(hdc, _font);
		const auto rcClient = client_rect();
		const auto line_count = _doc.size();
		auto y = 0;
		auto nCurrentLine = _char_offset.cy;

		while (y < rcClient.bottom)
		{
			const auto nLineHeight = line_height(nCurrentLine);
			auto rcLine = rcClient;
			rcLine.bottom = rcLine.top + nLineHeight;

			irect rcCacheMargin(0, y, margin_width(), y + nLineHeight);
			irect rcCacheLine(margin_width(), y, rcLine.Width(), y + nLineHeight);

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

};
