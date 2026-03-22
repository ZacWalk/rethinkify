// view_console.h — Console view: command input, scrollable history, output display

#pragma once

#include "commands.h"
#include "view_text.h"

class console_view final : public text_view
{
	commands& _commands;

	edit_box_widget _input;
	int _history_index = -1;
	std::vector<uint16_t> _wrap_breaks;
	std::vector<int> _wrap_offsets;
	std::vector<int> _wrap_line_y;
	std::vector<int> _input_wrap_breaks;
	int _total_visual_rows = 0;

	static constexpr std::u8string_view prompt_text = u8"> ";

	[[nodiscard]] int prompt_columns() const
	{
		return static_cast<int>(prompt_text.size());
	}

	[[nodiscard]] int input_visual_rows() const
	{
		return std::max(1, static_cast<int>(_input_wrap_breaks.size()) + 1);
	}

	[[nodiscard]] int line_gap() const
	{
		return std::max(2, _font_extent.cy / 5);
	}

	[[nodiscard]] int row_pitch() const
	{
		return _font_extent.cy + line_gap();
	}

	[[nodiscard]] int prompt_bottom_padding() const
	{
		return std::max(4, _font_extent.cy / 3);
	}

	[[nodiscard]] int content_top_padding() const
	{
		return _font_extent.cy;
	}

	[[nodiscard]] int line_content_offset(const int line) const
	{
		return content_top_padding() + line * row_pitch();
	}

	[[nodiscard]] int edit_box_height() const
	{
		return _font_extent.cy * input_visual_rows() + line_gap() * std::max(0, input_visual_rows() - 1) +
			prompt_bottom_padding();
	}

	[[nodiscard]] int input_top() const
	{
		return line_content_offset(_total_visual_rows) - _scroll_offset.y + text_top();
	}

	[[nodiscard]] pf::irect edit_box_rect() const
	{
		const auto top = input_top();
		return full_width_band_rect(top, edit_box_height());
	}

	[[nodiscard]] bool is_in_edit_box(const pf::ipoint& pt) const
	{
		return edit_box_rect().contains(pt);
	}

	[[nodiscard]] int output_pad_left() const
	{
		return _font_extent.cx / 2;
	}

	[[nodiscard]] int wrap_width() const
	{
		const auto scrollbar_width = static_cast<int>(custom_scrollbar::base_hover_track_width * _events.styles().
			dpi_scale);
		const auto available_width = _view_extent.cx - output_pad_left() - scrollbar_width - _font_extent.cx;
		if (available_width <= 0 || _font_extent.cx <= 0)
			return 1;
		return std::max(1, available_width / _font_extent.cx);
	}

	void rebuild_input_wrap_state()
	{
		_input_wrap_breaks.clear();
		const auto& text = _input.edit.text;
		if (text.empty())
			return;

		const int first_row_cols = std::max(1, wrap_width() - prompt_columns());
		const int other_row_cols = std::max(1, wrap_width());
		int row_cols = first_row_cols;
		int col = 0;

		for (int i = 0; i < static_cast<int>(text.size()); ++i)
		{
			const int advance = pf::is_utf8_continuation(text[i]) ? 0 : 1;
			if (col > 0 && col + advance > row_cols)
			{
				_input_wrap_breaks.push_back(i);
				col = advance;
				row_cols = other_row_cols;
			}
			else
			{
				col += advance;
			}
		}
	}

	[[nodiscard]] int input_row_start(const int row) const
	{
		if (row <= 0 || _input_wrap_breaks.empty())
			return 0;
		return _input_wrap_breaks[std::min(row - 1, static_cast<int>(_input_wrap_breaks.size()) - 1)];
	}

	[[nodiscard]] int input_row_end(const int row) const
	{
		if (row < 0)
			return 0;
		if (row >= static_cast<int>(_input_wrap_breaks.size()))
			return static_cast<int>(_input.edit.text.size());
		return _input_wrap_breaks[row];
	}

	[[nodiscard]] int input_row_for_pos(const int pos) const
	{
		for (int row = 0; row < static_cast<int>(_input_wrap_breaks.size()); ++row)
		{
			if (pos < _input_wrap_breaks[row])
				return row;
		}
		return static_cast<int>(_input_wrap_breaks.size());
	}

	[[nodiscard]] int input_row_x(const int row) const
	{
		const auto pad = _events.styles().edit_box_margin;
		if (row == 0)
			return pad + prompt_columns() * _font_extent.cx;
		return pad;
	}

	void rebuild_wrap_state()
	{
		const auto& output = _commands.output();
		const auto line_count = static_cast<int>(output.size());

		_wrap_breaks.clear();
		_wrap_offsets.assign(line_count + 1, 0);
		_wrap_line_y.assign(line_count + 1, 0);

		const auto cols = wrap_width();
		for (int i = 0; i < line_count; ++i)
		{
			_wrap_offsets[i] = static_cast<int>(_wrap_breaks.size());
			const auto& text = output[i].text;
			auto char_advance = [&](const int j, const int) -> int
			{
				return pf::is_utf8_continuation(text[j]) ? 0 : 1;
			};
			const auto breaks = calc_word_breaks(text, cols, char_advance);
			for (const auto brk : breaks)
				_wrap_breaks.push_back(static_cast<uint16_t>(brk));
			_wrap_line_y[i + 1] = _wrap_line_y[i] + static_cast<int>(breaks.size()) + 1;
		}

		_wrap_offsets[line_count] = static_cast<int>(_wrap_breaks.size());
		_total_visual_rows = _wrap_line_y[line_count];
		rebuild_input_wrap_state();
	}

	[[nodiscard]] std::span<const uint16_t> line_breaks(const int line_index) const
	{
		if (_wrap_offsets.empty() || line_index < 0 || line_index >= static_cast<int>(_wrap_offsets.size()) - 1)
			return {};
		return {
			_wrap_breaks.data() + _wrap_offsets[line_index],
			static_cast<size_t>(_wrap_offsets[line_index + 1] - _wrap_offsets[line_index])
		};
	}

	[[nodiscard]] int visual_row_to_line_index(const int visual_row) const
	{
		if (_wrap_line_y.empty())
			return 0;
		int low = 0;
		int high = static_cast<int>(_wrap_line_y.size()) - 2;
		while (low <= high)
		{
			const int mid = (low + high) / 2;
			if (_wrap_line_y[mid] <= visual_row)
				low = mid + 1;
			else
				high = mid - 1;
		}
		return std::clamp(high, 0, static_cast<int>(_wrap_line_y.size()) - 2);
	}

	[[nodiscard]] int line_sub_row(const int line_index, const int visual_row) const
	{
		if (_wrap_line_y.empty() || line_index < 0 || line_index >= static_cast<int>(_wrap_line_y.size()) - 1)
			return 0;
		return std::max(0, visual_row - _wrap_line_y[line_index]);
	}

	[[nodiscard]] int row_start_col(const int line_index, const int sub_row) const
	{
		const auto breaks = line_breaks(line_index);
		if (sub_row <= 0 || breaks.empty())
			return 0;
		const auto row_index = std::min(sub_row - 1, static_cast<int>(breaks.size()) - 1);
		return breaks[row_index];
	}

	[[nodiscard]] int row_end_col(const int line_index, const int sub_row) const
	{
		const auto& output = _commands.output();
		if (line_index < 0 || line_index >= static_cast<int>(output.size()))
			return 0;
		const auto breaks = line_breaks(line_index);
		if (sub_row < 0 || sub_row >= static_cast<int>(breaks.size()))
			return static_cast<int>(output[line_index].text.size());
		return breaks[sub_row];
	}

	[[nodiscard]] std::u8string input_selection_text() const
	{
		if (!_input.edit.has_selection())
			return {};
		const auto start = _input.edit.sel_start();
		const auto end = _input.edit.sel_end();
		return _input.edit.text.substr(start, end - start);
	}

	[[nodiscard]] text_location output_hit_test(const pf::ipoint& pt) const
	{
		const auto& output = _commands.output();
		if (output.empty() || _total_visual_rows <= 0)
			return {};

		const int content_y = pt.y - text_top() + _scroll_offset.y;
		int visual_row = row_pitch() > 0
			                 ? (content_y - content_top_padding()) / row_pitch()
			                 : 0;
		visual_row = std::clamp(visual_row, 0, std::max(0, _total_visual_rows - 1));
		const int line = visual_row_to_line_index(visual_row);
		const int sub_row = line_sub_row(line, visual_row);
		const int start_col = row_start_col(line, sub_row);
		const int end_col = row_end_col(line, sub_row);

		int col = _font_extent.cx > 0
			          ? std::max(0, (pt.x - output_pad_left()) / _font_extent.cx)
			          : 0;
		col = std::clamp(start_col + col, start_col, end_col);

		return {col, line};
	}

	void sync_output()
	{
		rebuild_wrap_state();
		_max_lines = _total_visual_rows + input_visual_rows();
		_content_extent.cy = content_top_padding() + _total_visual_rows * row_pitch() + edit_box_height();
	}

	void recalc_vert_scrollbar() override
	{
		const int visible_height = _view_extent.cy - text_top();
		const int max_y = std::max(0, _content_extent.cy - visible_height);
		if (_scroll_offset.y > max_y)
		{
			_scroll_offset.y = max_y;
			_events.invalidate(invalid::windows);
		}

		const int visible_rows = row_pitch() > 0
			                         ? std::max(1, (visible_height + row_pitch() - 1) / row_pitch())
			                         : 1;
		const int scroll_row = row_pitch() > 0 ? _scroll_offset.y / row_pitch() : 0;
		_vscroll.update(_max_lines + 2, visible_rows, scroll_row);
	}

	void scroll_output_to_bottom()
	{
		sync_output();
		const int max_y = std::max(0, _content_extent.cy - (_view_extent.cy - text_top()));
		if (_scroll_offset.y != max_y)
		{
			_scroll_offset.y = max_y;
			recalc_vert_scrollbar();
			_events.invalidate(invalid::console);
		}
	}

	std::vector<pf::menu_command> build_context_menu_items() const
	{
		std::vector<pf::menu_command> items;
		items.push_back(_events.command_menu_item(command_id::edit_copy, nullptr,
		                                          [this]
		                                          {
			                                          return _input.edit.has_selection() || has_current_selection();
		                                          }));
		items.push_back(_events.command_menu_item(command_id::edit_paste));
		return items;
	}

	void on_context_menu(const pf::window_frame_ptr& window, const pf::ipoint& screen_pt)
	{
		window->set_focus();
		const auto items = build_context_menu_items();
		if (!items.empty())
			window->show_popup_menu(items, screen_pt);
	}

	bool paste_into_input(const pf::window_frame_ptr& window, const std::u8string_view text)
	{
		if (text.empty())
			return false;

		std::u8string clean;
		clean.reserve(text.size());
		for (const auto ch : text)
		{
			if (ch != u8'\r' && ch != u8'\n')
				clean += ch;
		}

		if (clean.empty())
			return false;

		_input.edit.insert_at_cursor(clean);
		sync_output();
		_history_index = -1;
		if (window)
		{
			_input.reset_caret(window);
			window->invalidate_rect(edit_box_rect());
		}
		else
		{
			_events.invalidate(invalid::console);
		}
		return true;
	}

	void navigate_history(const pf::window_frame_ptr& window, const int direction)
	{
		const auto& history = _commands.history();
		if (history.empty()) return;

		if (direction < 0)
		{
			if (_history_index < 0)
				_history_index = static_cast<int>(history.size()) - 1;
			else if (_history_index > 0)
				_history_index--;
		}
		else
		{
			if (_history_index >= 0)
			{
				_history_index++;
				if (_history_index >= static_cast<int>(history.size()))
				{
					_history_index = -1;
					_input.edit.text.clear();
					_input.edit.cursor_pos = 0;
					_input.edit.sel_anchor = 0;
					sync_output();
					_input.reset_caret(window);
					window->invalidate();
					return;
				}
			}
		}

		if (_history_index >= 0 && _history_index < static_cast<int>(history.size()))
		{
			_input.edit.text = history[_history_index];
			_input.edit.cursor_pos = static_cast<int>(_input.edit.text.length());
			_input.edit.sel_anchor = _input.edit.cursor_pos;
		}

		sync_output();
		scroll_output_to_bottom();
		_input.reset_caret(window);
		window->invalidate();
	}

	void draw_edit_box_background(pf::draw_context& dc) const
	{
		const auto eb = edit_box_rect();
		if (eb.bottom <= text_top() || eb.top >= _view_extent.cy)
			return;

		dc.fill_solid_rect(eb, focus_band_color());
	}

	void draw_scrollbar_lane(pf::draw_context& dc, const pf::irect& bounds) const
	{
		if (!_vscroll.can_scroll())
			return;

		const auto lane_width = _vscroll.hover_track_width();
		const auto lane_left = std::max(bounds.left, bounds.right - lane_width);
		dc.fill_solid_rect(pf::irect(lane_left, bounds.top, bounds.right, bounds.bottom),
		                   style_to_color(style::normal_bkgnd));
	}

	void draw_edit_box_foreground(pf::draw_context& dc) const
	{
		const auto styles = _events.styles();
		const auto font = styles.console_font;
		const auto pad = styles.edit_box_margin;
		const auto eb = edit_box_rect();
		if (eb.bottom <= text_top() || eb.top >= _view_extent.cy)
			return;

		const auto eb_bg = focus_band_color();
		const auto prompt_y = centered_text_top(eb.top, row_pitch());
		dc.draw_text(pad, prompt_y, eb, prompt_text, font,
		             ui::handle_hover_color, eb_bg);

		for (int row = 0; row < input_visual_rows(); ++row)
		{
			const int row_band_top = eb.top + row * row_pitch();
			const int row_y = centered_text_top(row_band_top, row_pitch());
			const int row_start = input_row_start(row);
			const int row_end = input_row_end(row);
			const int row_x = eb.left + input_row_x(row);
			const auto row_text = _input.edit.text.substr(row_start, row_end - row_start);

			if (_input.edit.has_selection())
			{
				const int sel_start = std::clamp(_input.edit.sel_start(), row_start, row_end);
				const int sel_end = std::clamp(_input.edit.sel_end(), row_start, row_end);
				auto x = row_x;

				if (sel_start > row_start)
				{
					const auto pre = _input.edit.text.substr(row_start, sel_start - row_start);
					const auto w = dc.measure_text(pre, font).cx;
					dc.draw_text(x, row_y, pf::irect(x, row_y, x + w, row_y + _font_extent.cy), pre, font,
					             ui::text_color, eb_bg);
					x += w;
				}
				if (sel_end > sel_start)
				{
					const auto mid = _input.edit.text.substr(sel_start, sel_end - sel_start);
					const auto w = dc.measure_text(mid, font).cx;
					dc.draw_text(x, row_y, pf::irect(x, row_y, x + w, row_y + _font_extent.cy), mid, font,
					             style_to_color(style::sel_text), style_to_color(style::sel_bkgnd));
					x += w;
				}
				if (sel_end < row_end)
				{
					const auto post = _input.edit.text.substr(sel_end, row_end - sel_end);
					dc.draw_text(x, row_y, pf::irect(x, row_y, eb.right, row_y + _font_extent.cy), post, font,
					             ui::text_color, eb_bg);
				}
			}
			else if (!row_text.empty())
			{
				dc.draw_text(row_x, row_y, pf::irect(row_x, row_y, eb.right, row_y + _font_extent.cy), row_text, font,
				             ui::text_color, eb_bg);
			}
		}

		if (_focused && _input.caret.visible)
		{
			const int caret_row = input_row_for_pos(_input.edit.cursor_pos);
			const int row_start = input_row_start(caret_row);
			const auto before = _input.edit.text.substr(row_start, _input.edit.cursor_pos - row_start);
			const int caret_x = eb.left + input_row_x(caret_row) + dc.measure_text(before, font).cx;
			const int caret_y = centered_text_top(eb.top + caret_row * row_pitch(), row_pitch());
			const int caret_w = std::max(1, static_cast<int>(2 * styles.dpi_scale));
			dc.fill_solid_rect(caret_x, caret_y, caret_w, _font_extent.cy, ui::text_color);
		}
	}

public:
	console_view(app_events& events, commands& cmds)
		: text_view(events), _commands(cmds)
	{
	}

	~console_view() override = default;

	void zoom(const pf::window_frame_ptr& window, const int delta) override
	{
		_events.on_zoom(delta, zoom_target::console);
	}

	void on_mouse_wheel(pf::window_frame_ptr& window, const int zDelta) override
	{
		sync_output();
		set_scroll_pixel(_scroll_offset.y + zDelta * row_pitch());
	}

	void handle_size(pf::window_frame_ptr& window, const pf::isize extent,
	                 pf::measure_context& measure) override
	{
		_view_extent = extent;
		const auto styles = _events.styles();
		_font_extent = measure.measure_char(styles.console_font);
		_screen_lines = row_pitch() > 0 ? extent.cy / row_pitch() : 0;

		sync_output();
		_vscroll.set_dpi_scale(styles.dpi_scale);

		_events.invalidate(invalid::doc_scrollbar);
		window->invalidate();
	}

	uint32_t handle_message(const pf::window_frame_ptr window, const pf::message_type msg,
	                        const uintptr_t wParam, const intptr_t lParam) override
	{
		using mt = pf::message_type;

		if (msg == mt::timer)
		{
			if (_input.on_timer(static_cast<uint32_t>(wParam)))
				window->invalidate_rect(edit_box_rect());
			return 0;
		}

		return text_view::handle_message(window, msg, wParam, lParam);
	}

	uint32_t handle_mouse(const pf::window_frame_ptr window, const pf::mouse_message_type msg,
	                      const pf::mouse_params& params) override
	{
		using mt = pf::mouse_message_type;

		{
			const auto rc = client_rect();

			if (_vscroll.handle_mouse(msg, params.point, rc, window, [this](const int pos)
			{
				sync_output();
				set_scroll_pixel(line_content_offset(std::clamp(pos, 0, std::max(0, _max_lines - 1))));
			}))
				return 0;
		}

		if (msg == mt::set_cursor)
		{
			if (params.hit_test == 1)
			{
				auto pt = pf::platform_cursor_pos();
				pt = window->screen_to_client(pt);
				window->set_cursor_shape(is_in_edit_box(pt) ? pf::cursor_shape::ibeam : pf::cursor_shape::ibeam);
				return 1;
			}
			return 0;
		}

		if (msg == mt::left_button_down || msg == mt::left_button_dbl_clk)
		{
			if (is_in_edit_box(params.point))
			{
				window->set_focus();
				scroll_output_to_bottom();
				window->invalidate();
				return 0;
			}
		}

		if (msg == mt::left_button_down)
		{
			window->set_focus();
			if (_commands.output().empty())
				return 0;
			const auto pos = output_hit_test(params.point);
			const bool shift = window->is_key_down(pf::platform_key::Shift);

			if (shift && has_current_selection())
				_output_sel = text_selection(_sel_anchor, pos).normalize();
			else
			{
				_sel_anchor = pos;
				_output_sel = text_selection(pos, pos);
			}

			_selecting = true;
			window->set_capture();
			window->invalidate();
			return 0;
		}

		if (msg == mt::left_button_dbl_clk)
		{
			if (_commands.output().empty())
				return 0;
			const auto pos = output_hit_test(params.point);
			const auto& output = _commands.output();

			if (pos.y >= 0 && pos.y < static_cast<int>(output.size()))
			{
				const auto& text = output[pos.y].text;
				const auto len = static_cast<int>(text.size());
				int start = pos.x;
				int end = pos.x;

				while (start > 0 && start <= len && text[start - 1] != u8' ' && text[start - 1] != u8'\t')
					start--;
				while (end < len && text[end] != u8' ' && text[end] != u8'\t')
					end++;

				_sel_anchor = text_location(start, pos.y);
				_output_sel = text_selection(start, pos.y, end, pos.y);
				_selecting = true;
				window->set_capture();
				window->invalidate();
			}
			return 0;
		}

		if (msg == mt::mouse_move && _selecting)
		{
			if (_commands.output().empty())
				return 0;
			const auto pos = output_hit_test(params.point);
			_output_sel = text_selection(_sel_anchor, pos).normalize();
			window->invalidate();
			return 0;
		}

		if (msg == mt::left_button_up && _selecting)
		{
			_selecting = false;
			window->release_capture();
			return 0;
		}

		if (msg == mt::context_menu)
		{
			on_context_menu(window, params.point);
			return 0;
		}

		return text_view::handle_mouse(window, msg, params);
	}

	void on_char(pf::window_frame_ptr& window, const char8_t ch) override
	{
		if (ch == u8'\r' || ch == u8'\n')
		{
			if (!_input.edit.text.empty())
			{
				_commands.execute(_input.edit.text);
				_input.edit.text.clear();
				_input.edit.cursor_pos = 0;
				_input.edit.sel_anchor = 0;
				_history_index = -1;
				_output_sel = {};
				scroll_output_to_bottom();
			}
			window->invalidate();
			return;
		}

		if (_input.on_char(window, ch))
		{
			_history_index = -1;
			scroll_output_to_bottom();
		}

		window->invalidate();
	}

	bool on_key_down(pf::window_frame_ptr& window, const unsigned int vk) override
	{
		namespace pk = pf::platform_key;
		const bool ctrl = window->is_key_down(pk::Control);
		const bool shift = window->is_key_down(pk::Shift);

		if (vk == pk::Escape)
		{
			_events.set_focus(view_focus::text);
			return true;
		}

		if (ctrl && !shift && vk == 'A')
		{
			select_all();
			window->invalidate();
			return true;
		}

		if (ctrl && !shift && vk == 'V')
		{
			if (paste_into_input(window, clipboard_text()))
				scroll_output_to_bottom();
			return true;
		}

		if (shift && !ctrl && vk == pk::Insert)
		{
			if (paste_into_input(window, clipboard_text()))
				scroll_output_to_bottom();
			return true;
		}

		if (!ctrl && !shift)
		{
			if (vk == pk::Up)
			{
				navigate_history(window, -1);
				return true;
			}
			if (vk == pk::Down)
			{
				navigate_history(window, 1);
				return true;
			}
		}

		if (ctrl && vk == pk::Up)
		{
			scroll_by(-1);
			return true;
		}
		if (ctrl && vk == pk::Down)
		{
			scroll_by(1);
			return true;
		}

		if (ctrl && !shift && vk == pk::Home)
		{
			scroll_to_top();
			return true;
		}
		if (ctrl && !shift && vk == pk::End)
		{
			scroll_output_to_bottom();
			return true;
		}

		if (text_view::on_key_down(window, vk))
			return true;

		bool text_modified = false;
		if (_input.on_key_down(window, vk, text_modified))
		{
			if (text_modified)
			{
				_history_index = -1;
				scroll_output_to_bottom();
			}
			window->invalidate();
			return true;
		}

		return false;
	}

	void draw_view(pf::window_frame_ptr& window, pf::draw_context& draw) const override
	{
		const auto styles = _events.styles();
		const auto font = styles.console_font;
		const auto rcClient = client_rect();
		const auto bg = style_to_color(style::normal_bkgnd);

		draw.fill_solid_rect(rcClient, bg);
		draw.fill_solid_rect(0, 0, _view_extent.cx, 1, ui::handle_color);

		const auto& output = _commands.output();
		const auto pad_left = output_pad_left();
		const auto sel = _output_sel.normalize();
		const auto has_sel = has_current_selection();
		const auto sel_fg = style_to_color(style::sel_text);
		const auto sel_bg = style_to_color(style::sel_bkgnd);
		int visual_row = row_pitch() > 0 ? std::max(0, (_scroll_offset.y - content_top_padding()) / row_pitch()) : 0;
		auto y = line_content_offset(visual_row) - _scroll_offset.y + text_top();

		while (y < rcClient.bottom && visual_row < _total_visual_rows)
		{
			const int line_idx = visual_row_to_line_index(visual_row);
			const int sub_row = line_sub_row(line_idx, visual_row);
			const int row_start = row_start_col(line_idx, sub_row);
			const int row_end = row_end_col(line_idx, sub_row);
			const auto& full_text = output[line_idx].text;
			const auto row_text = full_text.substr(row_start, std::max(0, row_end - row_start));

			if (y + _font_extent.cy > 0 && y < rcClient.bottom)
			{
				const auto text_color = output[line_idx].is_command ? ui::folder_text_color : ui::darker_text_color;
				const auto line_rect = pf::irect(pad_left, y, rcClient.right, y + _font_extent.cy);

				if (has_sel && line_idx >= sel._start.y && line_idx <= sel._end.y && !row_text.empty())
				{
					const int sb = (line_idx == sel._start.y)
						               ? std::clamp(sel._start.x, row_start, row_end)
						               : row_start;
					const int se = (line_idx == sel._end.y) ? std::clamp(sel._end.x, row_start, row_end) : row_end;
					auto x = pad_left;

					if (sb > row_start)
					{
						const auto pre = full_text.substr(row_start, sb - row_start);
						const auto w = static_cast<int>(pre.size()) * _font_extent.cx;
						draw.draw_text(x, y, pf::irect(x, y, x + w, y + _font_extent.cy), pre, font, text_color, bg);
						x += w;
					}
					if (sb < se)
					{
						const auto mid = full_text.substr(sb, se - sb);
						const auto w = static_cast<int>(mid.size()) * _font_extent.cx;
						draw.draw_text(x, y, pf::irect(x, y, x + w, y + _font_extent.cy), mid, font, sel_fg, sel_bg);
						x += w;
					}
					if (se < row_end)
					{
						const auto post = full_text.substr(se, row_end - se);
						draw.draw_text(x, y, pf::irect(x, y, rcClient.right, y + _font_extent.cy), post, font,
						               text_color,
						               bg);
					}
					if (sub_row == static_cast<int>(line_breaks(line_idx).size()) && line_idx < sel._end.y)
					{
						const auto eol_x = pad_left + static_cast<int>(row_text.size()) * _font_extent.cx;
						draw.fill_solid_rect(eol_x, y, _font_extent.cx, _font_extent.cy, sel_bg);
					}
				}
				else if (!row_text.empty())
				{
					draw.draw_text(pad_left, y, line_rect, row_text, font, text_color, bg);
				}
			}

			visual_row++;
			y += row_pitch();
		}

		draw_edit_box_background(draw);
		draw_scrollbar_lane(draw, rcClient);
		_vscroll.draw(draw, rcClient);
		draw_edit_box_foreground(draw);
	}

	void render_line(const int line_num, std::u8string& out) const override
	{
		const auto& output = _commands.output();
		if (line_num >= 0 && line_num < static_cast<int>(output.size()))
			out = output[line_num].text;
		else
			out.clear();
	}

	void select_all() override
	{
		const auto& output = _commands.output();
		if (output.empty()) return;

		const auto last = static_cast<int>(output.size()) - 1;
		const auto last_len = static_cast<int>(output[last].text.size());
		_sel_anchor = text_location(0, 0);
		_output_sel = text_selection(0, 0, last_len, last);
	}

	[[nodiscard]] bool can_copy_text() const override
	{
		return _input.edit.has_selection() || !_commands.output().empty();
	}

	[[nodiscard]] bool can_cut_text() const override
	{
		return _input.edit.has_selection() || !_input.edit.text.empty();
	}

	[[nodiscard]] bool can_paste_text() const override
	{
		return !clipboard_text().empty();
	}

	[[nodiscard]] bool can_delete_text() const override
	{
		return _input.edit.has_selection() || _input.edit.cursor_pos < static_cast<int>(_input.edit.text.size());
	}

	bool copy_text_to_clipboard() override
	{
		if (_input.edit.has_selection())
			return set_clipboard(input_selection_text());
		return text_view::copy_text_to_clipboard();
	}

	bool cut_text_to_clipboard() override
	{
		if (_input.edit.text.empty())
			return false;

		const std::u8string text = _input.edit.has_selection() ? input_selection_text() : _input.edit.text;
		if (!set_clipboard(text))
			return false;

		if (_input.edit.has_selection())
			_input.edit.delete_selection();
		else
		{
			_input.edit.text.clear();
			_input.edit.cursor_pos = 0;
			_input.edit.sel_anchor = 0;
		}

		sync_output();
		_events.invalidate(invalid::console);
		return true;
	}

	bool paste_text_from_clipboard() override
	{
		const auto ok = paste_into_input({}, clipboard_text());
		if (ok)
			scroll_output_to_bottom();
		return ok;
	}

	bool delete_selected_text() override
	{
		if (_input.edit.has_selection())
		{
			_input.edit.delete_selection();
			sync_output();
			_events.invalidate(invalid::console);
			return true;
		}
		if (_input.edit.cursor_pos >= static_cast<int>(_input.edit.text.size()))
			return false;
		_input.edit.text.erase(_input.edit.cursor_pos, 1);
		_input.edit.sel_anchor = _input.edit.cursor_pos;
		sync_output();
		_events.invalidate(invalid::console);
		return true;
	}

	[[nodiscard]] bool word_wrap() const override { return true; }

	void scroll_by(const int delta)
	{
		set_scroll_pixel(_scroll_offset.y + delta * row_pitch());
	}

	std::u8string select_text() const override
	{
		const auto& output = _commands.output();
		if (output.empty()) return {};

		if (!has_current_selection())
		{
			std::u8string result;
			for (const auto& line : output)
			{
				if (!result.empty()) result += u8'\n';
				result += line.text;
			}
			return result;
		}

		const auto sel = _output_sel.normalize();
		const auto line_count = static_cast<int>(output.size());
		std::u8string result;

		for (int i = sel._start.y; i <= sel._end.y && i < line_count; i++)
		{
			if (!result.empty()) result += u8'\n';
			const auto& text = output[i].text;
			const auto len = static_cast<int>(text.size());
			const int start_col = (i == sel._start.y) ? std::clamp(sel._start.x, 0, len) : 0;
			const int end_col = (i == sel._end.y) ? std::clamp(sel._end.x, 0, len) : len;

			if (start_col < end_col)
				result += text.substr(start_col, end_col - start_col);
		}

		return result;
	}

	void update_focus(pf::window_frame_ptr& window) override
	{
		const bool focused = window->has_focus();

		if (_focused != focused)
		{
			_focused = focused;
			_input.update_focus(window, _focused);
			window->invalidate();
		}
	}
};

using console_view_ptr = std::shared_ptr<console_view>;
