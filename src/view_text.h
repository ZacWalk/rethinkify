#pragma once

// view_text.h — Text view with selection, caret, word wrap, and syntax-highlighted rendering

#include "view_text_base.h"

constexpr auto TIMER_DRAGSEL = 1001;

class text_view : public text_view_base
{
protected:
	bool _drag_selection = false;
	bool _word_selection = false;
	bool _line_selection = false;
	bool _preparing_to_drag = false;
	bool _cursor_hidden = false;
	bool _sel_margin = true;
	uint32_t _drag_sel_timer = 0;

	int _screen_chars = 0;
	custom_scrollbar _hscroll{custom_scrollbar::orientation::horizontal};
	bool _hover_tracking = false;

	// Caret blink state
	static constexpr uint32_t TIMER_CARET_BLINK = 1002;
	static constexpr uint32_t CARET_BLINK_MS = 530;
	bool _caret_visible = false;
	bool _caret_blink_active = false;

	// Word wrap state
	bool _word_wrap = false;

	struct line_wrap_info
	{
		std::vector<int> breaks; // char indices where each subsequent visual row begins
		[[nodiscard]] int visual_rows() const { return static_cast<int>(breaks.size()) + 1; }
	};

	std::vector<line_wrap_info> _wrap_cache;
	std::vector<int> _wrap_line_y; // prefix sum: _wrap_line_y[i] = first visual row of doc line i
	int _total_visual_rows = 0;

	mutable std::vector<text_block> _block_buf;
	mutable std::wstring _expand_buf;

	// Syntax highlighting — owned by the view, not the document
	highlight_fn _highlight;
	mutable std::vector<int> _parse_cookies;

public:
	text_view(app_events& events) : text_view_base(events),
	                                _highlight(select_highlighter(doc_type::overlay, {}))
	{
		build_accelerators();
	}

	~text_view() override = default;

	void start_caret_blink(const pf::window_frame_ptr& window)
	{
		if (window && !_caret_blink_active)
		{
			_caret_visible = true;
			_caret_blink_active = true;
			window->set_timer(TIMER_CARET_BLINK, CARET_BLINK_MS);
			_events.invalidate(invalid::invalidate);
		}
	}

	void stop_caret_blink(const pf::window_frame_ptr& window) override
	{
		if (window && _caret_blink_active)
		{
			_caret_blink_active = false;
			_caret_visible = false;
			window->kill_timer(TIMER_CARET_BLINK);
			_events.invalidate(invalid::invalidate);
		}
	}

	void reset_caret_blink(const pf::window_frame_ptr& window)
	{
		if (window && _is_focused)
		{
			_caret_visible = true;
			if (_caret_blink_active)
				window->kill_timer(TIMER_CARET_BLINK);
			_caret_blink_active = true;
			window->set_timer(TIMER_CARET_BLINK, CARET_BLINK_MS);
			_events.invalidate(invalid::invalidate);
		}
	}

	void on_caret_timer(const pf::window_frame_ptr& window)
	{
		if (_caret_blink_active)
		{
			_caret_visible = !_caret_visible;
			const auto r = caret_bounds();
			if (r.Width() > 0)
				window->invalidate_rect(r);
			else
				window->invalidate();
		}
	}

	void set_document(const document_ptr& d) override
	{
		text_view_base::set_document(d);
		_highlight = d ? select_highlighter(d->get_doc_type(), d->path()) : select_highlighter(doc_type::overlay, {});
		_parse_cookies.assign(d ? d->size() : 0, invalid_length);
	}

	// --- Syntax highlighting (view-owned) ---

	uint32_t highlight_cookie(const int lineIndex) const
	{
		if (lineIndex < 0 || lineIndex >= static_cast<int>(_doc->size()))
			return 0;

		if (_parse_cookies.size() != _doc->size())
			_parse_cookies.assign(_doc->size(), invalid_length);

		auto i = lineIndex;
		const auto scan_limit = std::max(0, i - 1000);
		while (i >= scan_limit && _parse_cookies[i] == invalid_length)
			i--;
		if (i < scan_limit) i = scan_limit;
		else i++;

		int nBlocks;
		while (i <= lineIndex && _parse_cookies[i] == invalid_length)
		{
			auto dwCookie = 0;

			if (i > 0)
				dwCookie = _parse_cookies[i - 1];

			const auto& line = (*_doc)[i];
			_parse_cookies[i] = _highlight(dwCookie, line, nullptr, nBlocks);
			assert(_parse_cookies[i] != invalid_length);
			i++;
		}

		return _parse_cookies[lineIndex];
	}

	uint32_t highlight_line(const uint32_t cookie, const document_line& line,
	                        text_block* pBuf, int& nBlocks) const
	{
		const auto result = _highlight(cookie, line, pBuf, nBlocks);

		if (_doc->spell_check() && pBuf)
		{
			const auto text = line.view();
			const auto len = static_cast<int>(text.size());

			for (int i = 0; i < nBlocks; ++i)
			{
				if (pBuf[i]._color != style::normal_text)
					continue;

				const int start = pBuf[i]._char_pos;
				const int end = i + 1 < nBlocks ? pBuf[i + 1]._char_pos : len;
				const auto word = text.substr(start, end - start);

				if (word.empty())
					continue;

				const auto is_word = std::ranges::all_of(word, [](const wchar_t ch) { return iswalnum(ch) != 0; });

				if (is_word && !spell_check_word(word))
					pBuf[i]._color = style::error_text;
			}
		}

		return result;
	}

	// --- frame_reactor overrides ---

	uint32_t handle_message(pf::window_frame_ptr window, const pf::message_type msg,
	                        const uintptr_t wParam, const intptr_t lParam) override
	{
		using mt = pf::message_type;

		if (msg == mt::timer)
		{
			on_timer(window, static_cast<uint32_t>(wParam));
			return 0;
		}
		if (msg == mt::set_cursor_msg)
		{
			if (on_set_cursor(window, static_cast<uint32_t>(lParam & 0xFFFF))) return 1;
			return 0;
		}
		if (msg == mt::mouse_leave) return on_mouse_leave();
		if (msg == mt::sys_color_change)
		{
			_events.invalidate(invalid::invalidate);
			return 0;
		}
		if (msg == mt::left_button_dbl_clk)
		{
			on_left_button_dbl_clk(window, pf::point_from_lparam(lParam),
			                       static_cast<uint32_t>(wParam));
			return 0;
		}
		if (msg == mt::left_button_down)
		{
			on_left_button_down(window, pf::point_from_lparam(lParam),
			                    static_cast<uint32_t>(wParam));
			return 0;
		}
		if (msg == mt::right_button_down)
		{
			on_right_button_down(window, pf::point_from_lparam(lParam),
			                     static_cast<uint32_t>(wParam));
			return 0;
		}
		if (msg == mt::left_button_up)
		{
			on_left_button_up(window, pf::point_from_lparam(lParam),
			                  static_cast<uint32_t>(wParam));
			return 0;
		}
		if (msg == mt::mouse_move)
		{
			on_mouse_move(window, pf::point_from_lparam(lParam),
			              static_cast<uint32_t>(wParam));
			return 0;
		}
		if (msg == mt::context_menu)
		{
			on_context_menu(window, pf::point_from_lparam(lParam));
			return 0;
		}

		return text_view_base::handle_message(window, msg, wParam, lParam);
	}

	void on_size(pf::window_frame_ptr& window, const isize extent,
	             pf::measure_context& measure) override
	{
		text_view_base::on_size(window, extent, measure);
		_screen_chars = _font_extent.cx > 0 ? extent.cx / _font_extent.cx : 0;

		if (_word_wrap)
			layout();

		_events.invalidate(invalid::horz_scrollbar | invalid::vert_scrollbar | invalid::layout);
	}

	// --- App-level interface overrides ---

	[[nodiscard]] bool word_wrap() const override { return _word_wrap; }

	void toggle_word_wrap() override
	{
		_word_wrap = !_word_wrap;
		_char_offset.cx = 0;
		_events.invalidate(invalid::view);
	}

	[[nodiscard]] int wrap_width() const
	{
		if (_extent.cx <= 0 || _font_extent.cx <= 0) return 80;
		return std::max(1, (_extent.cx - text_left()) / _font_extent.cx);
	}

	[[nodiscard]] line_wrap_info calc_line_wrap(const document_line& line) const
	{
		line_wrap_info info;
		const auto ww = wrap_width();
		if (ww <= 0) return info;

		const auto tab_sz = _doc->tab_size();
		const auto line_len = static_cast<int>(line.size());

		auto char_advance = [&](const int i, const int col) -> int
		{
			return line[i] == L'\t' ? tab_sz - col % tab_sz : 1;
		};

		info.breaks = calc_word_breaks(line.view(), ww, char_advance);
		return info;
	}

	// Compute word-boundary break points for a line of text.
	// char_advance(i, col) returns the column advance for character i at column col.
	template <typename AdvanceFn>
	static std::vector<int> calc_word_breaks(const std::wstring_view text, const int max_cols, AdvanceFn&& char_advance)
	{
		std::vector<int> breaks;
		const auto len = static_cast<int>(text.size());
		int col = 0;
		int last_break_opportunity = -1;
		int col_at_break = 0;

		for (int i = 0; i < len; i++)
		{
			const auto advance = char_advance(i, col);

			if (text[i] == L' ' || text[i] == L'\t')
			{
				last_break_opportunity = i + 1;
				col_at_break = col + advance;
			}

			if (col > 0 && col + advance > max_cols)
			{
				if (last_break_opportunity > (breaks.empty() ? 0 : breaks.back()))
				{
					breaks.push_back(last_break_opportunity);
					col = col - col_at_break + advance;
				}
				else
				{
					breaks.push_back(i);
					col = advance;
				}
				last_break_opportunity = -1;
			}
			else
			{
				col += advance;
			}
		}
		return breaks;
	}

	int char_to_sub_row(const int lineIndex, const int charIndex) const
	{
		if (!_word_wrap || _wrap_cache.empty() || lineIndex < 0
			|| lineIndex >= static_cast<int>(_wrap_cache.size()))
			return 0;
		const auto& wrap = _wrap_cache[lineIndex];
		for (int b = 0; b < static_cast<int>(wrap.breaks.size()); b++)
		{
			if (charIndex < wrap.breaks[b])
				return b;
		}
		return static_cast<int>(wrap.breaks.size());
	}

	int visual_row_to_line_index(const int visual_row) const
	{
		if (!_word_wrap || _wrap_line_y.empty()) return visual_row;
		const int line_count = static_cast<int>(_doc->size());
		int lo = 0, hi = line_count - 1;
		while (lo < hi)
		{
			const int mid = (lo + hi + 1) / 2;
			if (_wrap_line_y[mid] <= visual_row)
				lo = mid;
			else
				hi = mid - 1;
		}
		return lo;
	}

	int line_visual_rows(const int lineIndex) const
	{
		if (!_word_wrap || _wrap_cache.empty()
			|| lineIndex < 0 || lineIndex >= static_cast<int>(_wrap_cache.size()))
			return 1;
		return _wrap_cache[lineIndex].visual_rows();
	}

protected:
	void update_focus(pf::window_frame_ptr& window) override
	{
		const bool was_focused = _is_focused;
		text_view_base::update_focus(window);

		if (_is_focused != was_focused)
		{
			if (_is_focused)
				start_caret_blink(window);
			else
				stop_caret_blink(window);

			invalidate_selection(window);
			update_caret(window);

			if (!_is_focused && _drag_selection)
			{
				window->release_capture();
				window->kill_timer(_drag_sel_timer);
				_drag_selection = false;
			}
		}
	}

	void on_timer(const pf::window_frame_ptr& window, const uint32_t nIDEvent)
	{
		if (nIDEvent == TIMER_CARET_BLINK)
		{
			on_caret_timer(window);
			return;
		}

		if (nIDEvent == TIMER_DRAGSEL)
		{
			assert(_drag_selection);
			auto pt = pf::platform_cursor_pos();
			pt = window->screen_to_client(pt);

			const auto rcClient = client_rect();
			auto bChanged = false;
			auto y = _char_offset.cy;
			const auto line_count = static_cast<int>(_doc->size());

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
				scroll_to_line(y);
				bChanged = true;
			}

			//	Scroll horizontally, if necessary
			auto x = _char_offset.cx;
			const auto nMaxLineLength = _doc->max_line_length();

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
				scroll_to_char(x);
				bChanged = true;
			}

			//	Fix changes
			if (bChanged)
			{
				const auto pos = client_to_text(pt);

				if (_line_selection)
				{
					_doc->select(_doc->line_selection(pos, true));
				}
				else
				{
					const auto sel = _word_selection
						                 ? _doc->word_selection(pos, true)
						                 : _doc->pos_selection(pos, true);
					_doc->select(sel);
				}
			}
		}
	}

	void on_left_button_down(const pf::window_frame_ptr& window, const ipoint& point, uint32_t nFlags)
	{
		const bool bShift = window->is_key_down(pf::platform_key::Shift);
		const bool bControl = window->is_key_down(pf::platform_key::Control);

		window->set_focus();

		// Check scrollbar clicks first
		const auto rc = client_rect();
		if (_vscroll.begin_tracking(point, rc, window) || _hscroll.begin_tracking(point, rc, window))
		{
			_events.invalidate(invalid::invalidate);
			return;
		}

		if (point.x < margin_width())
		{
			if (bControl)
			{
				_doc->select(_doc->all());
			}
			else
			{
				const auto sel = _doc->line_selection(client_to_text(point), bShift);
				_doc->select(sel);

				window->set_capture();
				_drag_sel_timer = window->set_timer(TIMER_DRAGSEL, 100);
				if (_drag_sel_timer == 0) return;
				_word_selection = false;
				_line_selection = true;
				_drag_selection = true;
			}
		}
		else
		{
			const auto ptText = client_to_text(point);

			if (_doc->is_inside_selection(ptText))
			{
				_preparing_to_drag = true;
			}
			else
			{
				const auto pos = client_to_text(point);
				const auto sel = bControl ? _doc->word_selection(pos, bShift) : _doc->pos_selection(pos, bShift);
				_doc->select(sel);

				window->set_capture();
				_drag_sel_timer = window->set_timer(TIMER_DRAGSEL, 100);
				if (_drag_sel_timer == 0) return;
				_word_selection = bControl;
				_line_selection = false;
				_drag_selection = true;
			}
		}
	}

	bool can_scroll() const
	{
		if (_word_wrap && _total_visual_rows > 0)
			return _screen_lines < _total_visual_rows;
		const auto line_count = static_cast<int>(_doc->size());
		return line_count > 1 && _screen_lines < line_count - 1;
	}

	void on_mouse_wheel(pf::window_frame_ptr& window, const int zDelta) override
	{
		if (can_scroll())
		{
			int new_line;
			if (_word_wrap && !_wrap_line_y.empty())
			{
				const int cur_vrow = _wrap_line_y[clamp(_char_offset.cy, 0, static_cast<int>(_doc->size()))];
				new_line = visual_row_to_line_index(clamp(cur_vrow + zDelta, 0, _total_visual_rows));
			}
			else
			{
				new_line = _char_offset.cy + zDelta;
			}
			scroll_to_line(clamp(new_line, 0, static_cast<int>(_doc->size())));
			update_caret(window);
		}
	}

	void on_mouse_move(pf::window_frame_ptr& window, const ipoint& point, uint32_t nFlags)
	{
		// Handle scrollbar tracking
		if (_vscroll._tracking)
		{
			auto new_pos = _vscroll.track_to(point, client_rect());
			if (_word_wrap && !_wrap_line_y.empty())
				new_pos = visual_row_to_line_index(clamp(new_pos, 0, _total_visual_rows - 1));
			scroll_to_line(clamp(new_pos, 0, static_cast<int>(_doc->size()) - 1));
			update_caret(window);
			return;
		}
		if (_hscroll._tracking)
		{
			const auto new_pos = _hscroll.track_to(point, client_rect());
			scroll_to_char(clamp(new_pos, 0, _doc->max_line_length() - 1));
			update_caret(window);
			return;
		}

		if (_drag_selection)
		{
			const auto bOnMargin = point.x < margin_width();
			const auto pos = client_to_text(point);

			if (_line_selection)
			{
				if (bOnMargin)
				{
					const auto sel = _doc->line_selection(pos, true);
					_doc->select(sel);
					return;
				}

				_line_selection = _word_selection = false;
				update_cursor(window);
			}

			const auto sel = _word_selection ? _doc->word_selection(pos, true) : _doc->pos_selection(pos, true);
			_doc->select(sel);
		}

		if (_preparing_to_drag)
		{
			_preparing_to_drag = false;
			// TODO: OLE drag/drop needs platform abstraction
		}

		// Update scrollbar hover
		if (!_drag_selection && !_preparing_to_drag)
		{
			const auto rc = client_rect();
			bool need_redraw = _vscroll.set_hover(_vscroll.hit_test(point, rc));
			need_redraw |= _hscroll.set_hover(_hscroll.hit_test(point, rc));
			if (need_redraw) _events.invalidate(invalid::invalidate);

			if (!_hover_tracking)
			{
				window->track_mouse_leave();
				_hover_tracking = true;
			}
		}
	}

	void on_left_button_up(const pf::window_frame_ptr& window, const ipoint& point, uint32_t nFlags)
	{
		if (_vscroll._tracking || _hscroll._tracking)
		{
			_vscroll.end_tracking(window);
			_hscroll.end_tracking(window);
			_events.invalidate(invalid::invalidate);
			return;
		}

		if (_drag_selection)
		{
			const auto pos = client_to_text(point);

			if (_line_selection)
			{
				const auto sel = _doc->line_selection(pos, true);
				_doc->select(sel);
			}
			else
			{
				const auto sel = _word_selection ? _doc->word_selection(pos, true) : _doc->pos_selection(pos, true);
				_doc->select(sel);
			}

			window->release_capture();
			window->kill_timer(_drag_sel_timer);
			_drag_selection = false;
		}

		if (_preparing_to_drag)
		{
			_preparing_to_drag = false;
			_doc->select(client_to_text(point));
		}
	}

	void on_left_button_dbl_clk(const pf::window_frame_ptr& window, const ipoint& point, uint32_t nFlags)
	{
		if (!_drag_selection)
		{
			const bool bShift = window->is_key_down(pf::platform_key::Shift);
			_doc->select(_doc->word_selection(client_to_text(point), bShift));

			window->set_capture();
			_drag_sel_timer = window->set_timer(TIMER_DRAGSEL, 100);
			if (_drag_sel_timer == 0) return;
			_word_selection = true;
			_line_selection = false;
			_drag_selection = true;
		}
	}

	uint32_t on_right_button_down(pf::window_frame_ptr& window, const ipoint& point, uint32_t nFlags)
	{
		const auto pt = client_to_text(point);

		if (!_doc->is_inside_selection(pt))
		{
			//m_ptAnchor = m_ptCursorPos = pt;
			_doc->select(text_selection(pt, pt));
			ensure_visible(window, pt);
		}

		return 0;
	}

	void invalidate_selection(pf::window_frame_ptr& window)
	{
		const auto sel = _doc->selection();

		if (!sel.empty())
			invalidate_lines(window, sel._start.y, sel._end.y);
	}

	void build_accelerators()
	{
		namespace pk = pf::platform_key;
		namespace km = pf::key_mod;

		// Navigation
		_accel.add({pk::Left}, [this] { _doc->move_char_left(false); });
		_accel.add({pk::Left, km::shift}, [this] { _doc->move_char_left(true); });
		_accel.add({pk::Right}, [this] { _doc->move_char_right(false); });
		_accel.add({pk::Right, km::shift}, [this] { _doc->move_char_right(true); });
		_accel.add({pk::Left, km::ctrl}, [this] { _doc->move_word_left(false); });
		_accel.add({pk::Left, km::ctrl | km::shift}, [this] { _doc->move_word_left(true); });
		_accel.add({pk::Right, km::ctrl}, [this] { _doc->move_word_right(false); });
		_accel.add({pk::Right, km::ctrl | km::shift}, [this] { _doc->move_word_right(true); });
		_accel.add({pk::Up}, [this] { _doc->move_lines(-1, false); });
		_accel.add({pk::Up, km::shift}, [this] { _doc->move_lines(-1, true); });
		_accel.add({pk::Down}, [this] { _doc->move_lines(1, false); });
		_accel.add({pk::Down, km::shift}, [this] { _doc->move_lines(1, true); });
		_accel.add({pk::Up, km::ctrl}, [this] { scroll_by(-1); });
		_accel.add({pk::Down, km::ctrl}, [this] { scroll_by(1); });
		_accel.add({pk::Prior}, [this] { _doc->move_lines(-_screen_lines, false); });
		_accel.add({pk::Prior, km::shift}, [this] { _doc->move_lines(-_screen_lines, true); });
		_accel.add({pk::Next}, [this] { _doc->move_lines(_screen_lines, false); });
		_accel.add({pk::Next, km::shift}, [this] { _doc->move_lines(_screen_lines, true); });
		_accel.add({pk::End}, [this] { _doc->move_line_end(false); });
		_accel.add({pk::End, km::shift}, [this] { _doc->move_line_end(true); });
		_accel.add({pk::Home}, [this] { _doc->move_line_home(false); });
		_accel.add({pk::Home, km::shift}, [this] { _doc->move_line_home(true); });
		_accel.add({pk::Home, km::ctrl}, [this] { _doc->move_doc_home(false); });
		_accel.add({pk::Home, km::ctrl | km::shift}, [this] { _doc->move_doc_home(true); });
		_accel.add({pk::End, km::ctrl}, [this] { _doc->move_doc_end(false); });
		_accel.add({pk::End, km::ctrl | km::shift}, [this] { _doc->move_doc_end(true); });

		// Copy, Select All, Escape are inherited from text_view_base
	}

	void on_context_menu(const pf::window_frame_ptr& window, const ipoint& screen_pt)
	{
		const auto client_pt = window->screen_to_client(screen_pt);
		const auto items = on_popup_menu(client_pt);
		if (!items.empty())
			window->show_popup_menu(items, screen_pt);
	}

	virtual std::vector<pf::menu_command> on_popup_menu(const ipoint& client_pt)
	{
		std::vector<pf::menu_command> items;
		items.emplace_back(L"&Copy", 0, [this] { set_clipboard(_doc->copy()); },
		                   [this] { return _doc->has_selection(); });
		items.emplace_back(); // separator
		items.emplace_back(L"Select &All", 0, [this] { _doc->select(_doc->all()); });
		return items;
	}

	void scroll_to_char(const int x)
	{
		if (_char_offset.cx != x)
		{
			_char_offset.cx = x;
			recalc_horz_scrollbar();
			_events.invalidate(invalid::invalidate);
		}
	}

	void recalc_vert_scrollbar() override
	{
		if (_screen_lines >= static_cast<int>(_doc->size()) && _char_offset.cy > 0)
		{
			_char_offset.cy = 0;
			_events.invalidate(invalid::invalidate | invalid::caret);
		}

		if (_word_wrap && _total_visual_rows > 0)
			_vscroll.update(_total_visual_rows + 2, _screen_lines,
			                _wrap_line_y.empty()
				                ? 0
				                : _wrap_line_y[clamp(_char_offset.cy, 0, static_cast<int>(_doc->size()))]);
		else
			_vscroll.update(static_cast<int>(_doc->size()) + 2, _screen_lines, _char_offset.cy);
	}

	void recalc_horz_scrollbar() override
	{
		if (_word_wrap)
		{
			_char_offset.cx = 0;
			_hscroll.update(0, 0, 0);
			return;
		}

		if (_screen_chars >= _doc->max_line_length() && _char_offset.cx > 0)
		{
			_char_offset.cx = 0;
			_events.invalidate(invalid::invalidate | invalid::caret);
		}

		const auto margin_chars = _sel_margin ? 4 : 1; // +1 for text padding
		_hscroll.update(_doc->max_line_length() + margin_chars, _screen_chars, _char_offset.cx);
	}

	bool on_set_cursor(const pf::window_frame_ptr& window, const uint32_t nHitTest) const
	{
		if (nHitTest == 1 /*HTCLIENT*/)
		{
			update_cursor(window);
			return true;
		}
		return false;
	}

	void update_cursor(const pf::window_frame_ptr& window) const
	{
		auto pt = pf::platform_cursor_pos();
		pt = window->screen_to_client(pt);

		const auto rc = client_rect();

		if (_vscroll.hit_test(pt, rc) || _hscroll.hit_test(pt, rc))
		{
			window->set_cursor_shape(pf::cursor_shape::arrow);
		}
		else if (pt.x < margin_width())
		{
			window->set_cursor_shape(pf::cursor_shape::arrow);
		}
		else if (_doc->is_inside_selection(client_to_text(pt)))
		{
			window->set_cursor_shape(pf::cursor_shape::arrow);
		}
		else
		{
			window->set_cursor_shape(pf::cursor_shape::ibeam);
		}
	}

	void update_caret(pf::window_frame_ptr& window) override
	{
		reset_caret_blink(window);
	}

	irect caret_bounds() const
	{
		const auto pos = _doc->cursor_pos();
		if (_doc->calc_offset(pos.y, pos.x) < _char_offset.cx)
			return {};
		const auto pt = text_to_client(pos);
		return {pt.x, pt.y, pt.x + 2, pt.y + _font_extent.cy};
	}

	// TODO: Drag-drop needs platform abstraction
	// The following scroll methods support drag-based scrolling

	void scroll_up(pf::window_frame_ptr& window)
	{
		if (_char_offset.cy > 0)
		{
			scroll_to_line(_char_offset.cy - 1);
			update_caret(window);
		}
	}

	void scroll_down(pf::window_frame_ptr& window)
	{
		if (_char_offset.cy < static_cast<int>(_doc->size()) - 1)
		{
			scroll_to_line(_char_offset.cy + 1);
			update_caret(window);
		}
	}

	void scroll_left(pf::window_frame_ptr& window)
	{
		if (_char_offset.cx > 0)
		{
			scroll_to_char(_char_offset.cx - 1);
			update_caret(window);
		}
	}

	void scroll_right(pf::window_frame_ptr& window)
	{
		if (_char_offset.cx < _doc->max_line_length() - 1)
		{
			scroll_to_char(_char_offset.cx + 1);
			update_caret(window);
		}
	}

	void show_cursor(pf::window_frame_ptr& window)
	{
		_cursor_hidden = false;
		update_caret(window);
	}

	void hide_cursor(pf::window_frame_ptr& window)
	{
		_cursor_hidden = true;
		update_caret(window);
	}

	uint32_t on_mouse_leave()
	{
		_hover_tracking = false;
		bool need_redraw = _vscroll.set_hover(false);
		need_redraw |= _hscroll.set_hover(false);
		if (need_redraw) _events.invalidate(invalid::invalidate);
		return 0;
	}

	int client_to_line(const ipoint& point) const
	{
		const auto line_count = static_cast<int>(_doc->size());

		if (_font_extent.cy <= 0)
			return clamp(0, 0, line_count - 1);

		if (_word_wrap && !_wrap_line_y.empty())
		{
			const int pixel_y = point.y - text_top() + top_offset();
			const int visual_row = pixel_y / _font_extent.cy;
			return clamp(visual_row_to_line_index(visual_row), 0, line_count - 1);
		}

		const auto result = (point.y - text_top()) / _font_extent.cy + _char_offset.cy;

		return clamp(result, 0, line_count - 1);
	}

	text_location client_to_text(const ipoint& point) const
	{
		const auto line_count = static_cast<int>(_doc->size());

		text_location pt{0, client_to_line(point)};

		if (_font_extent.cx <= 0 || _font_extent.cy <= 0)
			return pt;

		if (pt.y >= 0 && pt.y < line_count)
		{
			const auto tabSize = _doc->tab_size();
			const auto& line = (*_doc)[pt.y];
			const auto lineSize = static_cast<int>(line.size());

			if (_word_wrap && !_wrap_cache.empty() && pt.y < static_cast<int>(_wrap_cache.size()) && pt.y < static_cast<
				int>(_wrap_line_y.size()))
			{
				// Determine which visual sub-row was clicked
				const int line_pixel_y = _wrap_line_y[pt.y] * _font_extent.cy;
				const int pixel_y = point.y - text_top() + top_offset();
				const auto& wrap = _wrap_cache[pt.y];
				const int sub_row = clamp((pixel_y - line_pixel_y) / _font_extent.cy, 0, wrap.visual_rows() - 1);

				const int row_start = sub_row == 0 ? 0 : wrap.breaks[sub_row - 1];
				const int row_end = sub_row < static_cast<int>(wrap.breaks.size())
					                    ? wrap.breaks[sub_row]
					                    : lineSize;

				auto x = (point.x - text_left()) / _font_extent.cx;
				if (x < 0) x = 0;

				int abs_col = _doc->calc_offset(pt.y, row_start);
				int rel_col = 0;
				int i = row_start;

				while (i < row_end)
				{
					const auto advance = line[i] == L'\t' ? tabSize - abs_col % tabSize : 1;
					abs_col += advance;
					rel_col += advance;
					if (rel_col > x) break;
					i++;
				}

				pt.x = clamp(i, row_start, row_end);
			}
			else
			{
				auto x = _char_offset.cx + (point.x - text_left()) / _font_extent.cx;

				if (x < 0)
					x = 0;

				auto i = 0;
				auto xx = 0;

				while (i < lineSize)
				{
					if (line[i] == L'\t')
					{
						xx += tabSize - xx % tabSize;
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
		}

		return pt;
	}

	ipoint text_to_client(const text_location& point) const
	{
		ipoint pt;

		if (point.y >= 0 && point.y < static_cast<int>(_doc->size()))
		{
			if (_word_wrap && !_wrap_cache.empty() && point.y < static_cast<int>(_wrap_cache.size()))
			{
				const auto& wrap = _wrap_cache[point.y];
				const int sub_row = char_to_sub_row(point.y, point.x);
				const int row_start = sub_row == 0 ? 0 : wrap.breaks[sub_row - 1];

				pt.y = (_wrap_line_y[point.y] + sub_row) * _font_extent.cy
					- top_offset() + text_top();

				const auto tabSize = _doc->tab_size();
				const auto& line = (*_doc)[point.y];
				int abs_col = _doc->calc_offset(point.y, row_start);
				int rel_col = 0;
				for (int i = row_start; i < point.x && i < static_cast<int>(line.size()); i++)
				{
					const auto advance = line[i] == L'\t' ? tabSize - abs_col % tabSize : 1;
					abs_col += advance;
					rel_col += advance;
				}
				pt.x = rel_col * _font_extent.cx + text_left();
			}
			else
			{
				pt.y = line_offset(point.y) - top_offset() + text_top();
				pt.x = 0;

				const auto tabSize = _doc->tab_size();
				const auto& line = (*_doc)[point.y];

				for (auto i = 0; i < point.x; i++)
				{
					if (line[i] == L'\t')
					{
						pt.x += tabSize - pt.x % tabSize;
					}
					else
					{
						pt.x++;
					}
				}

				pt.x = (pt.x - _char_offset.cx) * _font_extent.cx + text_left();
			}
		}

		return pt;
	}

	int top_offset() const
	{
		if (_word_wrap && !_wrap_line_y.empty())
		{
			const auto idx = clamp(_char_offset.cy, 0, static_cast<int>(_doc->size()));
			return _wrap_line_y[idx] * _font_extent.cy;
		}
		return _char_offset.cy * _font_extent.cy;
	}

	void invalidate_lines(pf::window_frame_ptr& window, int start, int end) override
	{
		if (_word_wrap)
			_events.invalidate(invalid::layout);

		// Invalidate parse cookies from 'start' to end of document (later lines depend on earlier cookies)
		if (start >= 0 && start < static_cast<int>(_parse_cookies.size()))
		{
			for (int i = start; i < static_cast<int>(_parse_cookies.size()); ++i)
				_parse_cookies[i] = invalid_length;
		}

		auto rcInvalid = client_rect();
		const auto top = top_offset() - text_top();

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

		invalidate(window, rcInvalid);
	}

	void layout() override
	{
		if (_doc)
			_parse_cookies.assign(_doc->size(), invalid_length);
		else
			_parse_cookies.clear();

		if (!_doc || !_word_wrap)
		{
			_wrap_cache.clear();
			_wrap_line_y.clear();
			_total_visual_rows = _doc ? static_cast<int>(_doc->size()) : 0;
			return;
		}

		const auto line_count = static_cast<int>(_doc->size());
		_wrap_cache.resize(line_count);
		_wrap_line_y.resize(line_count + 1);
		_wrap_line_y[0] = 0;

		for (int i = 0; i < line_count; i++)
		{
			_wrap_cache[i] = calc_line_wrap((*_doc)[i]);
			_wrap_line_y[i + 1] = _wrap_line_y[i] + _wrap_cache[i].visual_rows();
		}

		_total_visual_rows = _wrap_line_y[line_count];
	}

	int line_offset(const int lineIndex) const
	{
		if (_word_wrap && !_wrap_line_y.empty())
		{
			const auto idx = clamp(lineIndex, 0, static_cast<int>(_doc->size()));
			return _wrap_line_y[idx] * _font_extent.cy;
		}
		return lineIndex * _font_extent.cy;
	}

	int line_height(int lineIndex) const
	{
		if (_word_wrap && !_wrap_cache.empty())
		{
			lineIndex = clamp(lineIndex, 0, static_cast<int>(_wrap_cache.size()) - 1);
			return _wrap_cache[lineIndex].visual_rows() * _font_extent.cy;
		}
		return _font_extent.cy;
	}

	void ensure_visible(pf::window_frame_ptr& window, const text_location& pt) override
	{
		//	Scroll vertically
		const int line_count = static_cast<int>(_doc->size());
		int y = _char_offset.cy;

		if (_word_wrap && !_wrap_line_y.empty() && !_wrap_cache.empty())
		{
			// Find the visual row of the cursor
			const int sub_row = char_to_sub_row(pt.y, pt.x);
			const int cursor_vrow = _wrap_line_y[pt.y] + sub_row;
			const int top_vrow = _wrap_line_y[clamp(y, 0, line_count)];

			if (cursor_vrow >= top_vrow + _screen_lines)
			{
				const int target_top = cursor_vrow - _screen_lines + 1;
				y = visual_row_to_line_index(target_top);
			}
			else if (cursor_vrow < top_vrow)
			{
				y = visual_row_to_line_index(cursor_vrow);
			}

			y = clamp(y, 0, line_count - 1);

			if (_char_offset.cy != y)
				scroll_to_line(y);

			// No horizontal scrolling when wrapping
		}
		else
		{
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
				scroll_to_line(y);
			}

			//	Scroll horizontally
			const auto actual_pos = _doc->calc_offset(pt.y, pt.x);
			auto new_offset = _char_offset.cx;

			if (actual_pos > new_offset + _screen_chars)
			{
				new_offset = actual_pos - _screen_chars;
			}
			if (actual_pos < new_offset)
			{
				new_offset = actual_pos;
			}

			if (new_offset >= _doc->max_line_length())
				new_offset = _doc->max_line_length() - 1;
			if (new_offset < 0)
				new_offset = 0;

			if (_char_offset.cx != new_offset)
			{
				scroll_to_char(new_offset);
			}
		}

		update_caret(window);
	}

	int margin_width() const
	{
		if (!_sel_margin) return 1;
		const auto line_count = static_cast<int>(_doc->size());
		int digits = 1;
		auto n = line_count;
		while (n >= 10)
		{
			n /= 10;
			digits++;
		}
		return (digits + 1) * _font_extent.cx + 4;
	}

	int text_left() const
	{
		return margin_width() + _font_extent.cx;
	}

	void draw_line(pf::draw_context& draw, ipoint& ptOrigin, const irect& rcClip,
	               const std::wstring_view pszChars, const int nOffset, const int nCount,
	               const pf::font& f, const color_t text_color, const color_t bg_color) const
	{
		if (nCount > 0)
		{
			_doc->expanded_chars(pszChars, nOffset, nCount, _expand_buf);
			const auto nWidth = rcClip.right - ptOrigin.x;

			if (nWidth > 0)
			{
				const auto nCharWidth = _font_extent.cx;
				auto nDrawCount = static_cast<int>(_expand_buf.size());
				const auto nCountFit = nWidth / nCharWidth + 1;

				if (nDrawCount > nCountFit)
					nDrawCount = nCountFit;

				auto rcClipBlock = rcClip;
				rcClipBlock.left = ptOrigin.x;
				rcClipBlock.right = ptOrigin.x + nDrawCount * nCharWidth;
				rcClipBlock.bottom = ptOrigin.y + _font_extent.cy;

				draw.draw_text(ptOrigin.x, ptOrigin.y, rcClipBlock,
				               std::wstring_view(_expand_buf.c_str(), nDrawCount),
				               f, text_color, bg_color);
			}

			ptOrigin.x += _font_extent.cx * static_cast<int>(_expand_buf.size());
		}
	}

	void draw_line(pf::draw_context& draw, ipoint& ptOrigin, const irect& rcClip, style nColorIndex,
	               const std::wstring_view pszChars, const int nOffset, const int nCount,
	               const text_location& ptTextPos,
	               const pf::font& f, const color_t text_color, const color_t bg_color) const
	{
		if (nCount > 0)
		{
			const auto sel = _doc->selection();
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
				draw_line(draw, ptOrigin, rcClip, pszChars, nOffset, nSelBegin,
				          f, text_color, bg_color);
			}
			if (nSelBegin < nSelEnd)
			{
				draw_line(draw, ptOrigin, rcClip, pszChars, nOffset + nSelBegin, nSelEnd - nSelBegin,
				          f, style_to_color(style::sel_text), style_to_color(style::sel_bkgnd));
			}
			if (nSelEnd < nCount)
			{
				draw_line(draw, ptOrigin, rcClip, pszChars, nOffset + nSelEnd, nCount - nSelEnd,
				          f, text_color, bg_color);
			}
		}
	}

	void draw_wrapped_line(pf::draw_context& draw, const irect& rc, const int lineIndex,
	                       const pf::font& f) const
	{
		const auto bg_color = style_to_color(style::normal_bkgnd);
		const auto& line = (*_doc)[lineIndex];
		const auto& wrap = _wrap_cache[lineIndex];
		const auto nLength = static_cast<int>(line.size());
		const auto line_view = line.view();

		// Get syntax highlighting blocks for the full line
		const auto needed = static_cast<size_t>(nLength) * 2 + 128;
		if (_block_buf.size() < needed)
			_block_buf.resize(needed);
		auto* pBuf = _block_buf.data();
		auto nBlocks = 0;
		const auto cookie = highlight_cookie(lineIndex - 1);
		_parse_cookies[lineIndex] = highlight_line(cookie, line, pBuf, nBlocks);

		const int num_rows = wrap.visual_rows();

		for (int row = 0; row < num_rows; row++)
		{
			const int row_start = row == 0 ? 0 : wrap.breaks[row - 1];
			const int row_end = row < static_cast<int>(wrap.breaks.size()) ? wrap.breaks[row] : nLength;
			const int row_y = rc.top + row * _font_extent.cy;

			if (row_y >= rc.bottom) break;

			const irect row_rc(rc.left, row_y, rc.right, row_y + _font_extent.cy);
			ipoint origin(row_rc.left, row_y);

			// Lambda to draw a segment clipped to this visual row
			auto draw_clipped = [&](const int seg_start, const int seg_end, const style color)
			{
				const int cs = std::max(seg_start, row_start);
				const int ce = std::min(seg_end, row_end);
				if (cs < ce)
				{
					draw_line(draw, origin, row_rc, color, line_view, cs, ce - cs,
					          text_location(cs, lineIndex), f, style_to_color(color), bg_color);
				}
			};

			if (nBlocks > 0)
			{
				draw_clipped(0, pBuf[0]._char_pos, style::normal_text);
				for (int i = 0; i < nBlocks; i++)
				{
					const int block_end = i + 1 < nBlocks ? pBuf[i + 1]._char_pos : nLength;
					draw_clipped(pBuf[i]._char_pos, block_end, pBuf[i]._color);
				}
			}
			else
			{
				draw_clipped(0, nLength, style::normal_text);
			}

			// Selection indicator after text on last visual row
			if (row == num_rows - 1 && _doc->is_inside_selection(text_location(nLength, lineIndex)))
			{
				const auto sel_x = std::max(origin.x, row_rc.left);
				draw.fill_solid_rect(sel_x, row_rc.top, _font_extent.cx, row_rc.Height(),
				                     style_to_color(style::sel_bkgnd));
			}
		}
	}

	void draw_line(pf::draw_context& draw, const irect& rc, const int lineIndex, const pf::font& f) const
	{
		const auto bg_color = style_to_color(style::normal_bkgnd);
		const auto& line = (*_doc)[lineIndex];

		// Dispatch to wrapped drawing if this line wraps to multiple visual rows
		if (_word_wrap && !_wrap_cache.empty()
			&& lineIndex < static_cast<int>(_wrap_cache.size())
			&& _wrap_cache[lineIndex].visual_rows() > 1)
		{
			draw_wrapped_line(draw, rc, lineIndex, f);
			return;
		}

		if (line.empty())
		{
			if (_doc->is_inside_selection(text_location(0, lineIndex)))
			{
				draw.fill_solid_rect(rc.left, rc.top, _font_extent.cx, rc.Height(),
				                     style_to_color(style::sel_bkgnd));
			}
			return;
		}

		const auto nLength = static_cast<int>(line.size());
		const auto needed = static_cast<size_t>(nLength) * 2 + 128;
		if (_block_buf.size() < needed)
			_block_buf.resize(needed);
		auto* pBuf = _block_buf.data();
		auto nBlocks = 0;
		const auto cookie = highlight_cookie(lineIndex - 1);

		_parse_cookies[lineIndex] = highlight_line(cookie, line, pBuf, nBlocks);

		ipoint origin(rc.left - _char_offset.cx * _font_extent.cx, rc.top);
		const auto line_view = line.view();

		if (nBlocks > 0)
		{
			assert(pBuf[0]._char_pos >= 0 && pBuf[0]._char_pos <= nLength);

			draw_line(draw, origin, rc, style::normal_text, line_view, 0, pBuf[0]._char_pos,
			          text_location(0, lineIndex),
			          f, style_to_color(style::normal_text), bg_color);

			for (auto i = 0; i < nBlocks - 1; i++)
			{
				assert(pBuf[i]._char_pos >= 0 && pBuf[i]._char_pos <= nLength);

				draw_line(draw, origin, rc, pBuf[i]._color, line_view,
				          pBuf[i]._char_pos, pBuf[i + 1]._char_pos - pBuf[i]._char_pos,
				          text_location(pBuf[i]._char_pos, lineIndex),
				          f, style_to_color(pBuf[i]._color), bg_color);
			}

			assert(pBuf[nBlocks - 1]._char_pos >= 0 && pBuf[nBlocks - 1]._char_pos <= nLength);

			draw_line(draw, origin, rc, pBuf[nBlocks - 1]._color, line_view,
			          pBuf[nBlocks - 1]._char_pos, nLength - pBuf[nBlocks - 1]._char_pos,
			          text_location(pBuf[nBlocks - 1]._char_pos, lineIndex),
			          f, style_to_color(pBuf[nBlocks - 1]._color), bg_color);
		}
		else
		{
			draw_line(draw, origin, rc, style::normal_text, line_view, 0, nLength,
			          text_location(0, lineIndex),
			          f, style_to_color(style::normal_text), bg_color);
		}

		// Draw selection indicator after end of text
		if (_doc->is_inside_selection(text_location(nLength, lineIndex)))
		{
			const auto sel_x = std::max(origin.x, rc.left);
			draw.fill_solid_rect(sel_x, rc.top, _font_extent.cx, rc.Height(),
			                     style_to_color(style::sel_bkgnd));
		}
	}


	void draw_margin(pf::draw_context& draw, const irect& rect, const int lineIndex,
	                 const pf::font& f) const
	{
		if (_sel_margin)
		{
			auto text_rect = rect;
			text_rect.right -= 4;
			// When wrapping, show line number only in the first visual row
			if (_word_wrap && rect.Height() > _font_extent.cy)
				text_rect.bottom = text_rect.top + _font_extent.cy;
			const auto num = std::to_wstring(lineIndex + 1);
			const auto sz = draw.measure_text(num, f);
			const auto x = text_rect.right - sz.cx;
			const auto y = text_rect.top + (text_rect.Height() - sz.cy) / 2;
			draw.draw_text(x, y, text_rect, num, f, color_t{120, 120, 120},
			               style_to_color(style::sel_margin));
		}
	}

	void draw_view(pf::window_frame_ptr& window, pf::draw_context& draw) const override
	{
		const auto styles = _events.styles();
		const auto rcClient = client_rect();
		const auto clip = draw.clip_rect();
		const auto line_count = static_cast<int>(_doc->size());
		const auto pad_top = text_top();
		const auto pad_left = text_left();

		// Erase entire viewport
		draw.fill_solid_rect(rcClient, style_to_color(style::normal_bkgnd));

		// Paint margin background
		const irect rcMargin(0, 0, margin_width(), rcClient.bottom);
		draw.fill_solid_rect(rcMargin, style_to_color(_sel_margin ? style::sel_margin : style::normal_bkgnd));

		// Draw content lines — skip lines outside the clip region
		auto y = pad_top;
		auto nCurrentLine = _char_offset.cy;

		while (y < rcClient.bottom && nCurrentLine < line_count)
		{
			const auto nLineHeight = line_height(nCurrentLine);

			if (y + nLineHeight > clip.top && y < clip.bottom)
			{
				draw_margin(draw, irect(0, y, margin_width(), y + nLineHeight), nCurrentLine, styles.text_font);
				draw_line(draw, irect(pad_left, y, rcClient.right, y + nLineHeight), nCurrentLine, styles.text_font);
			}

			nCurrentLine++;
			y += nLineHeight;
		}

		_vscroll.draw(draw, rcClient);
		_hscroll.draw(draw, rcClient);
		draw_caret(draw);
		draw_message_bar(draw);
	}

	virtual void draw_caret(pf::draw_context& draw) const
	{
		if (!_is_focused || !_caret_visible || _cursor_hidden)
			return;

		const auto pos = _doc->cursor_pos();

		if (_doc->calc_offset(pos.y, pos.x) < _char_offset.cx)
			return;

		const auto pt = text_to_client(pos);
		const auto caret_rect = irect(pt.x, pt.y, pt.x + 2, pt.y + _font_extent.cy);
		const auto rc = client_rect();

		if (caret_rect.top < rc.bottom && caret_rect.bottom > rc.top)
			draw.fill_solid_rect(caret_rect, ui::text_color);
	}
};
