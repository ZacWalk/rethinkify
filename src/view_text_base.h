#pragma once

// view_text_base.h — Lightweight base for text-document-based views:
// window/document/font management, vertical scrolling, basic message routing

#include "ui.h"

color_t style_to_color(style style_index);

class text_view_base : public pf::frame_reactor
{
protected:
	document_ptr _doc;
	app_events& _events;

	bool _is_focused = false;

	isize _char_offset = {};
	isize _extent = {1, 1};
	isize _font_extent = {10, 10};
	int _screen_lines = 0;

	custom_scrollbar _vscroll{custom_scrollbar::orientation::vertical};

	pf::accelerator_table _accel;

public:
	[[nodiscard]] int message_bar_height() const
	{
		return _events.message_bar_text().empty() ? 0 : _font_extent.cy + _font_extent.cy / 2;
	}

	text_view_base(app_events& events) : _doc(nullptr), _events(events)
	{
		build_base_accelerators();
	}

	virtual void set_document(const document_ptr& d)
	{
		_doc = d;
		_events.invalidate(invalid::view);
	}

	virtual void update_focus(pf::window_frame_ptr& window)
	{
		const bool focused = window->has_focus();
		if (_is_focused != focused)
		{
			_is_focused = focused;
			if (!_events.message_bar_text().empty()) _events.invalidate(invalid::invalidate);
		}
	}

	~text_view_base() override = default;

	// --- App-level virtual interface ---
	// Provide no-op defaults so the app can call these uniformly on any view.
	[[nodiscard]] virtual bool word_wrap() const { return false; }

	virtual void toggle_word_wrap()
	{
	}

	virtual void layout()
	{
	}

	virtual void update_caret(pf::window_frame_ptr& window)
	{
	}

	virtual void stop_caret_blink(const pf::window_frame_ptr& window)
	{
	}

	virtual void recalc_horz_scrollbar()
	{
	}

	virtual void recalc_vert_scrollbar()
	{
		if (_screen_lines >= static_cast<int>(_doc->size()) && _char_offset.cy > 0)
		{
			_char_offset.cy = 0;
			_events.invalidate(invalid::invalidate);
		}

		_vscroll.update(static_cast<int>(_doc->size()) + 2, _screen_lines, _char_offset.cy);
	}

	void invalidate(const pf::window_frame_ptr& window, const irect r = {}) const
	{
		if (r.Width() > 0)
			window->invalidate_rect(r);
		else
			window->invalidate();
	}

	// --- frame_reactor interface ---

	uint32_t handle_message(pf::window_frame_ptr window, const pf::message_type msg,
	                        const uintptr_t wParam, const intptr_t lParam) override
	{
		using mt = pf::message_type;

		if (msg == mt::create) return on_create();
		if (msg == mt::destroy) return on_destroy();
		if (msg == mt::set_focus || msg == mt::kill_focus)
		{
			update_focus(window);
			return 0;
		}
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
		if (msg == mt::mouse_wheel)
		{
			const auto keys = static_cast<uint32_t>(wParam & 0xFFFF);
			const auto delta = static_cast<short>(wParam >> 16 & 0xFFFF);
			if (keys & 0x0008 /*MK_CONTROL*/)
			{
				zoom(window, delta > 0 ? 2 : -2);
				return 0;
			}
			on_mouse_wheel(window, delta > 0 ? -2 : 2);
			return 0;
		}
		if (msg == mt::key_down)
		{
			uint8_t mods = pf::key_mod::none;
			if (window->is_key_down(pf::platform_key::Control)) mods |= pf::key_mod::ctrl;
			if (window->is_key_down(pf::platform_key::Shift)) mods |= pf::key_mod::shift;
			if (window->is_key_down(pf::platform_key::Alt)) mods |= pf::key_mod::alt;

			if (_accel.dispatch(static_cast<unsigned int>(wParam), mods))
				return 0;

			if (mods & pf::key_mod::ctrl)
			{
				if (wParam == 0xBB /*VK_OEM_PLUS*/ || wParam == 0x6B /*VK_ADD*/)
				{
					zoom(window, 2);
					return 0;
				}
				if (wParam == 0xBD /*VK_OEM_MINUS*/ || wParam == 0x6D /*VK_SUBTRACT*/)
				{
					zoom(window, -2);
					return 0;
				}
			}

			return 0;
		}
		if (msg == mt::char_input)
		{
			on_char(window, static_cast<wchar_t>(wParam));
			return 0;
		}

		return 0;
	}

	void on_paint(pf::window_frame_ptr& window, pf::draw_context& draw) override
	{
		draw_view(window, draw);
	}

	void on_size(pf::window_frame_ptr& window, const isize extent,
	             pf::measure_context& measure) override
	{
		const auto styles = _events.styles();

		_extent = extent;
		_font_extent = measure.measure_char(styles.text_font);
		_screen_lines = _font_extent.cy > 0 ? extent.cy / _font_extent.cy : 0;
		_events.invalidate(invalid::vert_scrollbar);
		window->invalidate();
	}

	void scroll_to_top()
	{
		_char_offset = {};
		_events.invalidate(invalid::invalidate);
	}

	void scroll_to_end()
	{
		const auto max_line = std::max(0, static_cast<int>(_doc->size()) - _screen_lines);
		_char_offset.cy = max_line;
		_events.invalidate(invalid::invalidate);
	}

	// --- view_base interface ---

	virtual std::wstring text_from_clipboard(pf::window_frame_ptr& window)
	{
		return window->text_from_clipboard();
	}

	virtual bool text_to_clipboard(pf::window_frame_ptr& window, const std::wstring_view text)
	{
		return window->text_to_clipboard(text);
	}

	std::wstring clipboard_text() const
	{
		return pf::platform_text_from_clipboard();
	}

	bool set_clipboard(const std::wstring_view text) const
	{
		return pf::platform_text_to_clipboard(text);
	}

	virtual void ensure_visible(pf::window_frame_ptr& window, const text_location& pt)
	{
		const int line_count = static_cast<int>(_doc->size());
		int y = _char_offset.cy;

		if (pt.y >= y + _screen_lines)
			y = pt.y - _screen_lines + 1;
		else if (pt.y < y)
			y = pt.y;

		y = clamp(y, 0, line_count - 1);

		if (_char_offset.cy != y)
			scroll_to_line(y);
	}

	virtual void invalidate_lines(pf::window_frame_ptr& window, int start, int end)
	{
		invalidate(window);
	}

protected:
	virtual void draw_view(pf::window_frame_ptr& window,
	                       pf::draw_context& draw) const = 0;

	virtual void on_char(pf::window_frame_ptr& window, const wchar_t c)
	{
	}

	virtual uint32_t on_create() { return 0; }
	virtual uint32_t on_destroy() { return 0; }


	virtual void on_mouse_wheel(pf::window_frame_ptr& window, const int zDelta)
	{
		if (_screen_lines < static_cast<int>(_doc->size()))
		{
			scroll_to_line(clamp(_char_offset.cy + zDelta, 0, static_cast<int>(_doc->size()) - 1));
		}
	}

	void zoom(const pf::window_frame_ptr& window, const int delta)
	{
		_events.on_zoom(delta, true);
	}

	void scroll_to_line(const int y)
	{
		if (_char_offset.cy != y)
		{
			_char_offset.cy = y;
			recalc_vert_scrollbar();
			_events.invalidate(invalid::invalidate);
		}
	}

	irect client_rect() const
	{
		return irect(0, 0, _extent.cx, _extent.cy);
	}

	int text_top() const
	{
		return message_bar_height() + (_char_offset.cy == 0 ? _font_extent.cy : 0);
	}

	void draw_message_bar(pf::draw_context& draw) const
	{
		const auto text = _events.message_bar_text();
		if (text.empty()) return;

		const auto styles = _events.styles();
		const auto rcClient = client_rect();
		const auto bar_h = message_bar_height();
		const auto pad_y = _font_extent.cy / 4;
		const auto bg = _is_focused ? ui::focus_handle_color : ui::handle_color;
		const irect bar_rc(0, 0, rcClient.right, bar_h);
		const auto text_len = static_cast<int>(text.size());
		const auto text_w = text_len * _font_extent.cx;
		const auto text_x = (rcClient.right - text_w) / 2;

		draw.fill_solid_rect(bar_rc, bg);
		draw.draw_text(text_x, pad_y, bar_rc, text,
		               styles.text_font, ui::text_color, bg);
	}

	void scroll_by(const int delta)
	{
		const auto max_line = std::max(0, static_cast<int>(_doc->size()) - 1);
		scroll_to_line(clamp(_char_offset.cy + delta, 0, max_line));
	}

private:
	void build_base_accelerators()
	{
		namespace pk = pf::platform_key;
		namespace km = pf::key_mod;

		_accel.add({pk::Escape}, [this] { _events.on_escape(); });
		_accel.add({'C', km::ctrl}, [this] { set_clipboard(_doc->copy()); });
		_accel.add({pk::Insert, km::ctrl}, [this] { set_clipboard(_doc->copy()); });
		_accel.add({'A', km::ctrl}, [this] { _doc->select(_doc->all()); });

		// Basic scroll navigation (overridden by text_view with cursor-based navigation)
		_accel.add({pk::Up}, [this] { scroll_by(-1); });
		_accel.add({pk::Down}, [this] { scroll_by(1); });
		_accel.add({pk::Prior}, [this] { scroll_by(-_screen_lines); });
		_accel.add({pk::Next}, [this] { scroll_by(_screen_lines); });
		_accel.add({pk::Home, km::ctrl}, [this] { scroll_to_top(); });
		_accel.add({pk::End, km::ctrl}, [this] { scroll_to_end(); });
	}
};

using text_view_base_ptr = std::shared_ptr<text_view_base>;
