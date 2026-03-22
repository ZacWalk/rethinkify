#pragma once

// ui.h — UI types: color constants, edit_box, caret_blinker, splitter, custom_scrollbar

#include "platform.h"

namespace ui
{
	constexpr color_t handle_color{0x44, 0x44, 0x44};
	constexpr color_t focus_handle_color{40, 100, 220};
	constexpr color_t main_wnd_clr{0x22, 0x22, 0x22};
	constexpr color_t tool_wnd_clr{0x33, 0x33, 0x33};
	constexpr color_t handle_hover_color{0x66, 0x66, 0x66};
	constexpr color_t handle_tracking_color{0x11, 0x66, 0xCC};
	constexpr color_t text_color{0xFF, 0xFF, 0xFF};
	constexpr color_t folder_text_color{0xEE, 0xCC, 0x22};
	constexpr color_t darker_text_color{0xCC, 0xCC, 0xCC};
	constexpr color_t line_color{0x22, 0x55, 0xAA};
	constexpr color_t window_background{33, 33, 33};
}


struct edit_box
{
	std::wstring text;
	int cursor_pos = 0;
	int sel_anchor = 0;

	[[nodiscard]] int sel_start() const { return std::min(cursor_pos, sel_anchor); }
	[[nodiscard]] int sel_end() const { return std::max(cursor_pos, sel_anchor); }
	[[nodiscard]] bool has_selection() const { return cursor_pos != sel_anchor; }

	void delete_selection()
	{
		if (!has_selection()) return;
		const auto s = sel_start();
		const auto e = sel_end();
		text.erase(s, e - s);
		cursor_pos = s;
		sel_anchor = s;
	}

	void insert_at_cursor(const std::wstring_view t)
	{
		if (has_selection()) delete_selection();
		text.insert(cursor_pos, t);
		cursor_pos += static_cast<int>(t.length());
		sel_anchor = cursor_pos;
	}

	// Returns true if text was modified
	bool on_char(const pf::window_frame_ptr& w, const wchar_t ch)
	{
		if (ch == 0x01) // Ctrl+A
		{
			sel_anchor = 0;
			cursor_pos = static_cast<int>(text.length());
			return false;
		}

		if (ch == 0x03) // Ctrl+C
		{
			if (has_selection())
				w->text_to_clipboard(text.substr(sel_start(), sel_end() - sel_start()));
			else if (!text.empty())
				w->text_to_clipboard(text);
			return false;
		}

		if (ch == 0x16) // Ctrl+V
		{
			const auto clip = w->text_from_clipboard();
			if (!clip.empty())
			{
				std::wstring clean;
				clean.reserve(clip.size());
				for (const auto c : clip)
					if (c != L'\r' && c != L'\n')
						clean += c;
				insert_at_cursor(clean);
				return true;
			}
			return false;
		}

		if (ch == 0x18) // Ctrl+X
		{
			if (has_selection())
			{
				w->text_to_clipboard(text.substr(sel_start(), sel_end() - sel_start()));
				delete_selection();
			}
			else if (!text.empty())
			{
				w->text_to_clipboard(text);
				text.clear();
				cursor_pos = 0;
				sel_anchor = 0;
			}
			return true;
		}

		if (ch == 0x08) // Backspace
		{
			if (has_selection())
			{
				delete_selection();
			}
			else if (cursor_pos > 0)
			{
				text.erase(cursor_pos - 1, 1);
				cursor_pos--;
				sel_anchor = cursor_pos;
			}
			return true;
		}

		if (ch >= L' ')
		{
			insert_at_cursor(std::wstring_view(&ch, 1));
			return true;
		}

		return false;
	}

	// Returns true if the key was handled. Sets text_modified if text content changed.
	bool on_key_down(const pf::window_frame_ptr& w, const unsigned int vk, bool& text_modified)
	{
		namespace pk = pf::platform_key;
		const bool shift = w->is_key_down(pk::Shift);
		const bool ctrl = w->is_key_down(pk::Control);
		text_modified = false;

		if (vk == pk::Left)
		{
			if (ctrl) // word-skip left
			{
				auto pos = cursor_pos;
				while (pos > 0 && text[pos - 1] == L' ') pos--;
				while (pos > 0 && text[pos - 1] != L' ') pos--;
				cursor_pos = pos;
			}
			else if (cursor_pos > 0)
			{
				cursor_pos--;
			}
			if (!shift) sel_anchor = cursor_pos;
			return true;
		}

		if (vk == pk::Right)
		{
			const auto len = static_cast<int>(text.length());
			if (ctrl)
			{
				auto pos = cursor_pos;
				while (pos < len && text[pos] != L' ') pos++;
				while (pos < len && text[pos] == L' ') pos++;
				cursor_pos = pos;
			}
			else if (cursor_pos < len)
			{
				cursor_pos++;
			}
			if (!shift) sel_anchor = cursor_pos;
			return true;
		}

		if (vk == pk::Home)
		{
			cursor_pos = 0;
			if (!shift) sel_anchor = cursor_pos;
			return true;
		}

		if (vk == pk::End)
		{
			cursor_pos = static_cast<int>(text.length());
			if (!shift) sel_anchor = cursor_pos;
			return true;
		}

		if (vk == pk::Delete)
		{
			if (has_selection())
			{
				delete_selection();
				text_modified = true;
			}
			else if (cursor_pos < static_cast<int>(text.length()))
			{
				text.erase(cursor_pos, 1);
				text_modified = true;
			}
			return true;
		}

		return false;
	}

	ipoint caret_position(const irect& box_rect, const int inner_pad,
	                      const isize& char_sz, const pf::font& font,
	                      const pf::window_frame_ptr& w) const
	{
		const auto text_y = box_rect.top + (box_rect.Height() - char_sz.cy) / 2;

		if (text.empty() || cursor_pos == 0)
		{
			return {box_rect.left + inner_pad, text_y};
		}
		const auto mc = w->create_measure_context();
		const auto before = text.substr(0, cursor_pos);
		const auto sz = mc->measure_text(before, font);
		return {box_rect.left + inner_pad + sz.cx, text_y};
	}

	// --- Common drawing helpers for edit-box-based views ---

	static void draw_border(pf::draw_context& dc, const irect& box, const bool focused)
	{
		constexpr int thickness = 2;
		const auto color = focused ? ui::focus_handle_color : ui::handle_color;
		dc.fill_solid_rect(box.left, box.top, box.Width(), thickness, color);
		dc.fill_solid_rect(box.left, box.bottom - thickness, box.Width(), thickness, color);
		dc.fill_solid_rect(box.left, box.top, thickness, box.Height(), color);
		dc.fill_solid_rect(box.right - thickness, box.top, thickness, box.Height(), color);
	}

	void draw_selection(pf::draw_context& dc, const int text_x, const int text_y,
	                    const int char_cy, const pf::font& font) const
	{
		if (!has_selection()) return;
		const auto before_sz = dc.measure_text(text.substr(0, sel_start()), font);
		const auto sel_sz = dc.measure_text(text.substr(sel_start(), sel_end() - sel_start()), font);
		dc.fill_solid_rect(irect(text_x + before_sz.cx, text_y,
		                         text_x + before_sz.cx + sel_sz.cx, text_y + char_cy),
		                   color_t(88, 88, 88));
	}

	void draw_caret(pf::draw_context& dc, const int text_x, const int text_y,
	                const int char_cy, const pf::font& font) const
	{
		int caret_x = text_x;
		if (!text.empty() && cursor_pos > 0)
			caret_x += dc.measure_text(text.substr(0, cursor_pos), font).cx;
		dc.fill_solid_rect(caret_x, text_y, 2, char_cy, ui::text_color);
	}
};


// caret_blinker — Shared timer-based caret blink logic used by text views and edit-box views
struct caret_blinker
{
	bool visible = false;
	bool active = false;

	static constexpr uint32_t timer_id = 1002;
	static constexpr uint32_t blink_ms = 530;

	void start(const pf::window_frame_ptr& window)
	{
		if (!active)
		{
			visible = true;
			active = true;
			window->set_timer(timer_id, blink_ms);
		}
	}

	void stop(const pf::window_frame_ptr& window)
	{
		if (active)
		{
			active = false;
			visible = false;
			window->kill_timer(timer_id);
		}
	}

	void reset(const pf::window_frame_ptr& window)
	{
		visible = true;
		if (active)
			window->kill_timer(timer_id);
		active = true;
		window->set_timer(timer_id, blink_ms);
	}

	// Returns true if this timer event was handled (caller should invalidate)
	bool on_timer(const uint32_t id)
	{
		if (id == timer_id && active)
		{
			visible = !visible;
			return true;
		}
		return false;
	}
};


struct splitter
{
	enum class orientation { vertical, horizontal };

	orientation orient;
	double ratio = 0.5;
	bool is_tracking = false;
	bool is_hover = false;

	static constexpr int bar_width = 5;
	static constexpr double min_ratio = 0.05;
	static constexpr double max_ratio = 0.95;

	splitter(const orientation o, const double initial_ratio)
		: orient(o), ratio(initial_ratio)
	{
	}

	[[nodiscard]] int split_pos(const irect& bounds) const
	{
		if (orient == orientation::vertical)
			return static_cast<int>(bounds.left + (bounds.right - bounds.left) * ratio);
		return static_cast<int>(bounds.top + (bounds.bottom - bounds.top) * ratio);
	}

	[[nodiscard]] irect bar_rect(const irect& bounds) const
	{
		const auto pos = split_pos(bounds);
		if (orient == orientation::vertical)
			return {pos - bar_width, bounds.top, pos + bar_width, bounds.bottom};
		return {bounds.left, pos - bar_width, bounds.right, pos + bar_width};
	}

	[[nodiscard]] bool hit_test(const irect& bounds, const ipoint& pt) const
	{
		return bar_rect(bounds).Contains(pt);
	}

	void update_ratio(const irect& bounds, const ipoint& pt)
	{
		if (orient == orientation::vertical)
		{
			const auto width = bounds.right - bounds.left;
			if (width > 0)
				ratio = (pt.x - bounds.left) / static_cast<double>(width);
		}
		else
		{
			const auto height = bounds.bottom - bounds.top;
			if (height > 0)
				ratio = (pt.y - bounds.top) / static_cast<double>(height);
		}

		if (ratio < min_ratio) ratio = min_ratio;
		if (ratio > max_ratio) ratio = max_ratio;
	}

	[[nodiscard]] color_t color() const
	{
		if (is_tracking) return ui::handle_tracking_color;
		if (is_hover) return ui::handle_hover_color;
		return ui::handle_color;
	}

	[[nodiscard]] pf::cursor_shape cursor() const
	{
		return orient == orientation::vertical
			       ? pf::cursor_shape::size_we
			       : pf::cursor_shape::size_ns;
	}

	void draw(pf::draw_context& dc, const irect& bounds) const
	{
		dc.fill_solid_rect(bar_rect(bounds), color());
	}

	bool begin_tracking(const irect& bounds, const ipoint& pt, const pf::window_frame_ptr& window)
	{
		if (!hit_test(bounds, pt)) return false;
		is_tracking = true;
		window->set_capture();
		window->set_cursor_shape(cursor());
		return true;
	}

	bool track_to(const irect& bounds, const ipoint& pt, const pf::window_frame_ptr& window)
	{
		if (!is_tracking) return false;
		update_ratio(bounds, pt);
		window->invalidate();
		return true;
	}

	void end_tracking(const pf::window_frame_ptr& window)
	{
		if (is_tracking)
		{
			is_tracking = false;
			window->release_capture();
			window->invalidate();
		}
	}

	bool update_hover(const irect& bounds, const ipoint& pt, const pf::window_frame_ptr& window)
	{
		const auto new_hover = hit_test(bounds, pt);
		if (new_hover == is_hover) return false;
		is_hover = new_hover;
		if (is_hover)
		{
			window->track_mouse_leave();
			window->set_cursor_shape(cursor());
		}
		window->invalidate();
		return true;
	}

	void clear_hover(const pf::window_frame_ptr& window)
	{
		if (is_hover)
		{
			is_hover = false;
			window->invalidate();
		}
	}
};


struct custom_scrollbar
{
	enum class orientation { vertical, horizontal };

	orientation _orient;
	int _content_size = 0;
	int _page_size = 0;
	int _position = 0;

	bool _hover = false;
	bool _tracking = false;
	int _tracking_start_mouse = 0;
	int _tracking_start_pos = 0;

	static constexpr int thumb_thickness = 8;
	static constexpr int edge_margin = 4;
	static constexpr int hover_track_width = 26;
	static constexpr int hit_margin = 32;

	custom_scrollbar(const orientation o) : _orient(o)
	{
	}

	[[nodiscard]] bool can_scroll() const
	{
		return _content_size > _page_size && _page_size > 0;
	}

	void update(const int content_size, const int page_size, const int position)
	{
		_content_size = content_size;
		_page_size = page_size;
		_position = position;
	}

	bool hit_test(const ipoint& pt, const irect& client) const
	{
		if (!can_scroll()) return false;
		if (_orient == orientation::vertical)
			return pt.x >= client.right - hit_margin && pt.y >= client.top && pt.y < client.bottom;
		return pt.y >= client.bottom - hit_margin && pt.x >= client.left && pt.x < client.right;
	}

	void draw(pf::draw_context& dc, const irect& client) const
	{
		if (!can_scroll()) return;

		if (_orient == orientation::vertical)
		{
			const auto extent = client.Height();
			const auto y = pf::mul_div(_position, extent, _content_size) + client.top;
			const auto cy = std::max(pf::mul_div(_page_size, extent, _content_size), thumb_thickness);
			const auto right = client.right;
			auto x_pad = 0;

			if (_hover || _tracking)
			{
				dc.fill_solid_rect(irect(right - hover_track_width, client.top, right, client.bottom),
				                   ui::handle_color);
				x_pad = 10;
			}

			const auto c = _tracking ? ui::handle_tracking_color : ui::handle_hover_color;
			dc.fill_solid_rect(irect(right - thumb_thickness - edge_margin - x_pad, y,
			                         right - edge_margin, y + cy), c);
		}
		else
		{
			const auto extent = client.Width();
			const auto x = pf::mul_div(_position, extent, _content_size) + client.left;
			const auto cx = std::max(pf::mul_div(_page_size, extent, _content_size), thumb_thickness);
			const auto bottom = client.bottom;
			auto y_pad = 0;

			if (_hover || _tracking)
			{
				dc.fill_solid_rect(irect(client.left, bottom - hover_track_width, client.right, bottom),
				                   ui::handle_color);
				y_pad = 10;
			}

			const auto c = _tracking ? ui::handle_tracking_color : ui::handle_hover_color;
			dc.fill_solid_rect(irect(x, bottom - thumb_thickness - edge_margin - y_pad,
			                         x + cx, bottom - edge_margin), c);
		}
	}

	bool begin_tracking(const ipoint& pt, const irect& client, const pf::window_frame_ptr& window)
	{
		if (!hit_test(pt, client)) return false;
		_tracking = true;
		_tracking_start_mouse = _orient == orientation::vertical ? pt.y : pt.x;
		_tracking_start_pos = _position;
		window->set_capture();
		return true;
	}

	int track_to(const ipoint& pt, const irect& client) const
	{
		if (!_tracking) return _position;
		const int delta_mouse = _orient == orientation::vertical
			                        ? pt.y - _tracking_start_mouse
			                        : pt.x - _tracking_start_mouse;
		const int extent = _orient == orientation::vertical ? client.Height() : client.Width();
		if (extent <= 0) return _position;
		const auto delta_content = pf::mul_div(delta_mouse, _content_size, extent);
		return clamp(_tracking_start_pos + delta_content, 0, _content_size - _page_size);
	}

	void end_tracking(const pf::window_frame_ptr& window)
	{
		if (_tracking)
		{
			_tracking = false;
			window->release_capture();
		}
	}

	bool set_hover(const bool hover)
	{
		if (_hover == hover) return false;
		_hover = hover;
		return true;
	}
};
