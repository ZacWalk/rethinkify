#pragma once

// view_console.h — Console view: command input, scrollable history, output display

#include "ui.h"

class console_view final : public pf::frame_reactor
{
	app_events& _events;
	commands& _commands;

	edit_box _edit;
	custom_scrollbar _vscroll{custom_scrollbar::orientation::vertical};

	isize _extent = {1, 1};
	isize _font_char_size = {10, 10};
	int _history_index = -1;
	bool _focused = false;
	bool _hover_tracking = false;

	int _scroll_offset = 0;
	int _content_height = 0;

	caret_blinker _caret;

	[[nodiscard]] int edit_box_height() const
	{
		const auto styles = _events.styles();
		return _font_char_size.cy + styles.edit_box_inner_pad * 2;
	}

	[[nodiscard]] irect edit_box_rect() const
	{
		const auto styles = _events.styles();
		const auto m = styles.edit_box_margin;
		const auto h = edit_box_height();
		return irect(m, _extent.cy - h - m, _extent.cx - m, _extent.cy - m);
	}

	[[nodiscard]] irect output_rect() const
	{
		const auto styles = _events.styles();
		const auto eb = edit_box_rect();
		return irect(0, 1, _extent.cx, eb.top - styles.edit_box_margin);
	}

	void update_caret(const pf::window_frame_ptr& window)
	{
		if (!window || !window->has_focus()) return;
		_caret.reset(window);
		window->invalidate();
	}

	uint32_t on_char(const pf::window_frame_ptr& window, const wchar_t ch)
	{
		if (ch == L'\r' || ch == L'\n')
		{
			if (!_edit.text.empty())
			{
				_commands.execute(_edit.text);
				_edit.text.clear();
				_edit.cursor_pos = 0;
				_edit.sel_anchor = 0;
				_history_index = -1;
				scroll_to_bottom(window);
			}
			window->invalidate();
			return 0;
		}

		if (_edit.on_char(window, ch))
			_history_index = -1;

		update_caret(window);
		window->invalidate();
		return 0;
	}

	uint32_t on_key_down(const pf::window_frame_ptr& window, const unsigned int vk)
	{
		namespace pk = pf::platform_key;

		if (vk == pk::Up)
		{
			navigate_history(window, -1);
			return 0;
		}
		if (vk == pk::Down)
		{
			navigate_history(window, 1);
			return 0;
		}
		if (vk == pk::Escape)
		{
			_events.set_focus(view_focus::text);
			return 0;
		}

		bool text_modified = false;
		if (_edit.on_key_down(window, vk, text_modified))
		{
			update_caret(window);
			window->invalidate();
			return 0;
		}

		return 0;
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
					_edit.text.clear();
					_edit.cursor_pos = 0;
					_edit.sel_anchor = 0;
					update_caret(window);
					window->invalidate();
					return;
				}
			}
		}

		if (_history_index >= 0 && _history_index < static_cast<int>(history.size()))
		{
			_edit.text = history[_history_index];
			_edit.cursor_pos = static_cast<int>(_edit.text.length());
			_edit.sel_anchor = _edit.cursor_pos;
		}

		update_caret(window);
		window->invalidate();
	}

	void scroll_by(const pf::window_frame_ptr& window, const int lines)
	{
		const auto line_h = _font_char_size.cy;
		const auto out_rect = output_rect();
		const auto max_scroll = std::max(0, _content_height - out_rect.Height());
		_scroll_offset = clamp(_scroll_offset + lines * line_h, 0, max_scroll);
		window->invalidate();
	}

	void scroll_to_bottom(const pf::window_frame_ptr& window)
	{
		const auto line_h = _font_char_size.cy;
		const auto out_rect = output_rect();
		const auto total = static_cast<int>(_commands.output().size()) * line_h;
		const auto max_scroll = std::max(0, total - out_rect.Height());
		_scroll_offset = max_scroll;
		window->invalidate();
	}

	void update_focus(const pf::window_frame_ptr& window)
	{
		const bool focused = window->has_focus();
		if (_focused != focused)
		{
			_focused = focused;
			if (_focused)
				_caret.start(window);
			else
				_caret.stop(window);
			window->invalidate();
		}
	}

public:
	console_view(app_events& events, commands& cmds)
		: _events(events), _commands(cmds)
	{
	}

	uint32_t handle_message(pf::window_frame_ptr window, const pf::message_type msg,
	                        const uintptr_t wParam, const intptr_t lParam) override
	{
		using mt = pf::message_type;

		if (msg == mt::create) return 0;
		if (msg == mt::erase_background) return 1;
		if (msg == mt::set_cursor_msg)
		{
			if ((lParam & 0xFFFF) == 1 /*HTCLIENT*/)
			{
				window->set_cursor_shape(pf::cursor_shape::arrow);
				return 1;
			}
			return 0;
		}
		if (msg == mt::timer)
		{
			if (_caret.on_timer(static_cast<uint32_t>(wParam)))
				window->invalidate_rect(edit_box_rect());
			return 0;
		}
		if (msg == mt::set_focus || msg == mt::kill_focus)
		{
			update_focus(window);
			return 0;
		}
		if (msg == mt::left_button_down)
		{
			window->set_focus();
			const auto point = pf::point_from_lparam(lParam);
			const auto out_rc = output_rect();
			if (_vscroll.begin_tracking(point, out_rc, window))
				window->invalidate();
			return 0;
		}
		if (msg == mt::left_button_up)
		{
			if (_vscroll._tracking)
			{
				_vscroll.end_tracking(window);
				window->invalidate();
			}
			return 0;
		}
		if (msg == mt::mouse_move)
		{
			if (!_hover_tracking)
			{
				window->track_mouse_leave();
				_hover_tracking = true;
			}
			const auto point = pf::point_from_lparam(lParam);
			if (_vscroll._tracking)
			{
				const auto rc = output_rect();
				const auto new_pos = _vscroll.track_to(point, rc);
				if (new_pos != _scroll_offset)
				{
					_scroll_offset = new_pos;
					window->invalidate();
				}
			}
			else
			{
				const auto rc = output_rect();
				if (_vscroll.set_hover(_vscroll.hit_test(point, rc)))
					window->invalidate();
			}
			return 0;
		}
		if (msg == mt::mouse_leave)
		{
			_hover_tracking = false;
			if (_vscroll.set_hover(false))
				window->invalidate();
			return 0;
		}
		if (msg == mt::mouse_wheel)
		{
			const auto keys = static_cast<uint32_t>(wParam & 0xFFFF);
			const auto delta = static_cast<short>(wParam >> 16 & 0xFFFF);
			if (keys & 0x0008 /*MK_CONTROL*/)
			{
				_events.on_zoom(delta > 0 ? 1 : -1, false);
				return 0;
			}
			scroll_by(window, delta > 0 ? -2 : 2);
			return 0;
		}
		if (msg == mt::char_input)
			return on_char(window, static_cast<wchar_t>(wParam));
		if (msg == mt::key_down)
			return on_key_down(window, static_cast<unsigned int>(wParam));

		return 0;
	}

	void on_paint(pf::window_frame_ptr& window, pf::draw_context& dc) override
	{
		const auto styles = _events.styles();
		const auto font = styles.console_font;
		const auto pad = styles.edit_box_inner_pad;
		const auto margin = styles.edit_box_margin;
		const auto r = irect(0, 0, _extent.cx, _extent.cy);
		constexpr auto bg = ui::tool_wnd_clr.darken(8);

		dc.fill_solid_rect(r, bg);

		// Separator line at top
		dc.fill_solid_rect(0, 0, _extent.cx, 1, ui::handle_color);

		// Output area
		const auto out_rect = output_rect();
		const auto& output = _commands.output();
		const auto line_h = _font_char_size.cy;

		_content_height = static_cast<int>(output.size()) * line_h;
		_vscroll.update(_content_height, out_rect.Height(), _scroll_offset);

		int y = out_rect.top - _scroll_offset;
		for (const auto& line : output)
		{
			if (y + line_h > out_rect.top && y < out_rect.bottom)
			{
				const auto text_color = line.is_command
					                        ? ui::folder_text_color
					                        : ui::darker_text_color;
				const auto text_rect = irect(margin, y,
				                             out_rect.right - margin, y + line_h);
				dc.draw_text(text_rect.left, text_rect.top, text_rect,
				             line.text, font, text_color, bg);
			}
			y += line_h;
		}

		_vscroll.draw(dc, out_rect);

		// Edit box
		const auto eb = edit_box_rect();
		constexpr auto eb_bg = ui::tool_wnd_clr.darken(16);
		dc.fill_solid_rect(eb, eb_bg);

		edit_box::draw_border(dc, eb, _focused);

		// Prompt
		constexpr std::wstring_view prompt = L"> ";
		const auto prompt_sz = dc.measure_text(prompt, font);
		const auto text_y = eb.top + (eb.Height() - _font_char_size.cy) / 2;
		dc.draw_text(eb.left + pad, text_y, eb, prompt, font,
		             ui::handle_hover_color, eb_bg);

		const auto text_x = eb.left + pad + prompt_sz.cx;

		// Edit text
		if (!_edit.text.empty())
		{
			_edit.draw_selection(dc, text_x, text_y, _font_char_size.cy, font);

			auto text_rect = eb;
			text_rect.left = text_x;
			dc.draw_text(text_x, text_y, text_rect, _edit.text, font,
			             ui::text_color, eb_bg);
		}

		if (_focused && _caret.visible)
			_edit.draw_caret(dc, text_x, text_y, _font_char_size.cy, font);
	}

	void on_size(pf::window_frame_ptr& window, const isize extent,
	             pf::measure_context& measure) override
	{
		_extent = extent;
		const auto styles = _events.styles();
		_font_char_size = measure.measure_char(styles.console_font);
		window->invalidate();
	}
};

using console_view_ptr = std::shared_ptr<console_view>;
