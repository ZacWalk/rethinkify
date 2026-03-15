#pragma once

// view_list.h — Base class for panel views (folder browser, search results)

#include "ui.h"

struct list_view_item
{
	irect bounds;

	std::wstring name;
	index_item_ptr source;

	int depth = 0;
	bool is_group = false;
	bool expanded = false;

	int line_number = -1;
	int line_match_pos = -1;
	int text_match_start = 0;
	int text_match_length = 0;
};

using list_view_item_ptr = std::shared_ptr<list_view_item>;

class list_view : public pf::frame_reactor
{
protected:
	app_events& _events;

	std::vector<list_view_item_ptr> _items;
	list_view_item_ptr _selected_item;
	list_view_item_ptr _hover_item;
	bool _list_focused = false;

	isize _extent;
	isize _font_char_size = {10, 10};
	ipoint _offset;
	int _y_max = 0;
	int _header_height = 0;
	bool _hover = false;

	custom_scrollbar _scrollbar{custom_scrollbar::orientation::vertical};


	// Virtual hooks for derived classes

	virtual void draw_header(pf::window_frame_ptr& window, pf::draw_context& dc, const irect& header_rect)
	{
	}

	void draw_expand_icon(pf::draw_context& dc, const int cx, const int cy, const bool expanded) const
	{
		constexpr auto size = 4;

		if (expanded)
		{
			const ipoint pts[] = {{cx - size, cy - size / 2}, {cx, cy + size / 2}, {cx + size, cy - size / 2}};
			dc.draw_lines(pts, ui::line_color);
		}
		else
		{
			const ipoint pts[] = {{cx - size / 2, cy - size}, {cx + size / 2, cy}, {cx - size / 2, cy + size}};
			dc.draw_lines(pts, ui::line_color);
		}
	}

	void draw_item(pf::draw_context& dc, const list_view_item_ptr& item,
	               const irect& bounds, const bool selected, const bool hovered)
	{
		auto styles = _events.styles();
		const auto indent = styles.padding_x + item->depth * styles.indent;

		const auto bg = selected
			                ? (_list_focused ? ui::focus_handle_color : ui::handle_color)
			                : (hovered ? ui::handle_hover_color : ui::tool_wnd_clr);

		auto font_spec = styles.list_font;

		if (item->is_group)
		{
			const auto icon_x = bounds.left + indent + 4;
			const auto icon_y = (bounds.top + bounds.bottom) / 2;
			draw_expand_icon(dc, icon_x, icon_y, item->expanded);

			auto text_bounds = bounds;
			text_bounds.left += indent + 14;
			text_bounds = text_bounds.Inflate(-styles.padding_x, -styles.padding_y);
			dc.draw_text(text_bounds.left, text_bounds.top, text_bounds, item->name,
			             font_spec, ui::folder_text_color, bg);
		}
		else if (item->line_number >= 0)
		{
			auto text_bounds = bounds;
			text_bounds.left += indent + 4;
			text_bounds = text_bounds.Inflate(-styles.padding_x, -styles.padding_y);

			const auto line_str = std::format(L"{}: ", item->line_number + 1);
			const auto line_sz = dc.measure_text(line_str, font_spec);

			auto line_rect = text_bounds;
			line_rect.right = line_rect.left + line_sz.cx;
			dc.draw_text(line_rect.left, line_rect.top, line_rect, line_str, font_spec,
			             ui::darker_text_color, bg);

			auto context_rect = text_bounds;
			context_rect.left += line_sz.cx;

			const auto avail_width = context_rect.Width();
			const auto& name = item->name;
			const auto name_len = static_cast<int>(name.size());
			const auto full_width = dc.measure_text(name, font_spec).cx;

			std::wstring display_text;
			int display_match_start = item->text_match_start;

			if (full_width <= avail_width || item->text_match_length <= 0)
			{
				display_text = name;
			}
			else
			{
				const auto ellipsis_width = dc.measure_text(L"...", font_spec).cx;
				const int match_start = item->text_match_start;
				const int match_end = match_start + item->text_match_length;
				int vis_start = match_start;
				int vis_end = std::min(match_end, name_len);

				auto calc_width = [&]()
				{
					auto w = dc.measure_text(name.substr(vis_start, vis_end - vis_start), font_spec).cx;
					if (vis_start > 0) w += ellipsis_width;
					if (vis_end < name_len) w += ellipsis_width;
					return w;
				};

				bool expanded = true;
				while (expanded)
				{
					expanded = false;
					if (vis_start > 0)
					{
						--vis_start;
						if (calc_width() > avail_width) ++vis_start;
						else expanded = true;
					}
					if (vis_end < name_len)
					{
						++vis_end;
						if (calc_width() > avail_width) --vis_end;
						else expanded = true;
					}
				}

				const bool prefix = vis_start > 0;
				const bool suffix = vis_end < name_len;

				display_text.clear();
				if (prefix) display_text += L"...";
				display_match_start = static_cast<int>(display_text.size()) + (match_start - vis_start);
				display_text += name.substr(vis_start, vis_end - vis_start);
				if (suffix) display_text += L"...";
			}

			if (item->line_match_pos >= 0 && item->text_match_length > 0 &&
				display_match_start >= 0 && display_match_start < static_cast<int>(display_text.size()))
			{
				const auto match_end = std::min(display_match_start + item->text_match_length,
				                                static_cast<int>(display_text.size()));
				const auto before_text = display_text.substr(0, display_match_start);
				const auto match_text = display_text.substr(display_match_start, match_end - display_match_start);
				const auto after_text = display_text.substr(match_end);

				const auto before_sz = dc.measure_text(before_text, font_spec);
				const auto match_sz = dc.measure_text(match_text, font_spec);

				constexpr auto highlight_color = color_t(220, 140, 0);

				auto seg_rect = context_rect;
				seg_rect.right = seg_rect.left + before_sz.cx;
				dc.draw_text(seg_rect.left, seg_rect.top, seg_rect, before_text, font_spec, ui::text_color, bg);

				seg_rect.left = seg_rect.right;
				seg_rect.right = seg_rect.left + match_sz.cx;
				dc.draw_text(seg_rect.left, seg_rect.top, seg_rect, match_text, font_spec, ui::text_color,
				             highlight_color);

				seg_rect.left = seg_rect.right;
				seg_rect.right = context_rect.right;
				dc.draw_text(seg_rect.left, seg_rect.top, seg_rect, after_text, font_spec, ui::text_color, bg);
			}
			else
			{
				dc.draw_text(context_rect.left, context_rect.top, context_rect, display_text, font_spec,
				             ui::text_color, bg);
			}
		}
		else
		{
			auto text_bounds = bounds;
			text_bounds.left += indent + 4;
			text_bounds = text_bounds.Inflate(-styles.padding_x, -styles.padding_y);

			auto text_color = ui::text_color;
			if (_events.is_path_modified(item->source))
				text_color = color_t{255, 80, 80};

			const auto text_sz = dc.measure_text(item->name, font_spec);
			auto display_text = std::wstring_view(item->name);
			std::wstring ellipsized;
			if (text_sz.cx > text_bounds.Width())
			{
				const auto ellipsis_sz = dc.measure_text(L"...", font_spec);
				const auto avail = text_bounds.Width() - ellipsis_sz.cx;
				size_t fit = display_text.size();
				while (fit > 0 && dc.measure_text(display_text.substr(0, fit), font_spec).cx > avail)
					--fit;
				ellipsized = std::wstring(display_text.substr(0, fit)) + L"...";
				display_text = ellipsized;
			}
			dc.draw_text(text_bounds.left, text_bounds.top, text_bounds, display_text, font_spec, text_color, bg);
		}
	}

	virtual void on_item_selected(const list_view_item_ptr& item, bool activated)
	{
	}

	virtual uint32_t on_timer(pf::window_frame_ptr& window, uint32_t id) { return 0; }

	virtual uint32_t on_char(pf::window_frame_ptr& window, wchar_t ch) { return 0; }

	virtual uint32_t on_key_down(pf::window_frame_ptr& window, const unsigned int vk)
	{
		namespace pk = pf::platform_key;

		if (vk == pk::Down)
		{
			navigate_next(window, true);
			return 0;
		}

		if (vk == pk::Up)
		{
			navigate_next(window, false);
			return 0;
		}

		if (vk == pk::Return)
		{
			_events.set_focus(view_focus::text);
			return 0;
		}

		if (vk == pk::Escape)
		{
			_events.on_escape();
			return 0;
		}

		return 0;
	}

	virtual void update_focus(pf::window_frame_ptr& window)
	{
		const bool focused = window && window->has_focus();
		if (_list_focused != focused)
		{
			_list_focused = focused;
			window->invalidate();
		}
	}

public:
	list_view(app_events& events) : _events(events)
	{
	}

	void navigate_next(const pf::window_frame_ptr& window, const bool forward, const bool skip_groups = false)
	{
		if (_items.empty()) return;

		if (!_selected_item)
		{
			auto pick = forward ? _items.front() : _items.back();
			if (skip_groups && pick->is_group)
				pick = nullptr;
			if (!skip_groups || pick)
			{
				_selected_item = pick;
				ensure_visible(window, _selected_item);
				window->invalidate();
				on_item_selected(_selected_item, false);
			}
			return;
		}

		auto find_next = [&](auto&& range) -> list_view_item_ptr
		{
			bool found = false;
			for (const auto& i : range)
			{
				if (found && (!skip_groups || !i->is_group))
					return i;
				if (i == _selected_item)
					found = true;
			}
			return nullptr;
		};

		const auto next = forward
			                  ? find_next(_items)
			                  : find_next(_items | std::views::reverse);

		if (next)
		{
			_selected_item = next;
			ensure_visible(window, _selected_item);
			window->invalidate();
			on_item_selected(next, false);
		}
	}

	[[nodiscard]] int scroll_y() const { return _offset.y; }

	~list_view() override = default;

	uint32_t handle_message(pf::window_frame_ptr window, const pf::message_type msg,
	                        const uintptr_t wParam, const intptr_t lParam) override
	{
		using mt = pf::message_type;

		if (msg == mt::create) return 0;
		if (msg == mt::erase_background) return 1;
		if (msg == mt::timer) return on_timer(window, static_cast<uint32_t>(wParam));
		if (msg == mt::set_focus || msg == mt::kill_focus)
		{
			update_focus(window);
			return 0;
		}
		if (msg == mt::char_input) return on_char(window, static_cast<wchar_t>(wParam));
		if (msg == mt::key_down) return on_key_down(window, static_cast<unsigned int>(wParam));
		if (msg == mt::set_cursor_msg)
		{
			if ((lParam & 0xFFFF) == 1 /*HTCLIENT*/)
			{
				window->set_cursor_shape(pf::cursor_shape::arrow);
				return 1;
			}
			return 0;
		}
		if (msg == mt::left_button_down)
			return on_left_button_down(window, pf::point_from_lparam(lParam));
		if (msg == mt::left_button_up) return on_left_button_up(window);
		if (msg == mt::left_button_dbl_clk)
			return 0; // ignore double-click; single click already handled it
		if (msg == mt::mouse_move)
			return on_mouse_move(window, pf::point_from_lparam(lParam),
			                     static_cast<uint32_t>(wParam));
		if (msg == mt::mouse_leave) return on_mouse_leave(window);
		if (msg == mt::mouse_wheel)
		{
			const auto keys = static_cast<uint32_t>(wParam & 0xFFFF);
			const auto delta = static_cast<short>(wParam >> 16 & 0xFFFF);
			if (keys & 0x0008 /*MK_CONTROL*/)
			{
				zoom(window, delta > 0 ? 1 : -1);
				return 0;
			}
			scroll_to(window, _offset.y - delta / 2);
			return 0;
		}
		return 0;
	}

	void on_size(pf::window_frame_ptr& window, const isize extent,
	             pf::measure_context& measure) override
	{
		_extent = extent;
		layout_list(measure);
		window->invalidate();
	}

	void layout_list(const pf::measure_context& measure)
	{
		const auto styles = _events.styles();
		const auto font_spec = styles.list_font;
		_font_char_size = measure.measure_char(font_spec);
		layout_list(styles);
	}

	void layout_list()
	{
		layout_list(_events.styles());
	}

	void layout_list(const view_styles& styles)
	{
		int y = _header_height + 4;
		const auto single_line_height = _font_char_size.cy;
		const auto item_height = single_line_height + styles.padding_y * 2;

		for (const auto& i : _items)
		{
			irect bounds;
			bounds.left = 0;
			bounds.top = y;
			bounds.right = _extent.cx;
			bounds.bottom = y + item_height;

			i->bounds = bounds;
			y += item_height;
		}

		_y_max = y + 64;
	}

	void on_paint(pf::window_frame_ptr& window, pf::draw_context& dc) override
	{
		const auto r = window->get_client_rect();
		dc.fill_solid_rect(r, ui::tool_wnd_clr);

		const auto hh = _header_height;
		if (hh > 0)
		{
			const irect header_rect = {r.left, r.top, r.right, r.top + hh};
			draw_header(window, dc, header_rect);
		}

		const auto items_top = r.top + hh;

		int first_visible = 0;
		{
			int lo = 0, hi = static_cast<int>(_items.size()) - 1;
			while (lo <= hi)
			{
				const int mid = lo + (hi - lo) / 2;
				if (_items[mid]->bounds.bottom <= _offset.y)
					lo = mid + 1;
				else
					hi = mid - 1;
			}
			first_visible = lo;
		}

		for (int idx = first_visible; idx < static_cast<int>(_items.size()); idx++)
		{
			const auto& i = _items[idx];
			auto bounds = i->bounds.Offset(0, -_offset.y);

			if (bounds.top >= r.bottom)
				break;
			if (bounds.bottom <= items_top)
				continue;
			if (bounds.top < items_top)
				bounds.top = items_top;

			const bool selected = i == _selected_item;
			const bool hovered = i == _hover_item;

			if (hovered)
				dc.fill_solid_rect(bounds, ui::handle_hover_color);
			if (selected)
				dc.fill_solid_rect(bounds, _list_focused ? ui::focus_handle_color : ui::handle_color);

			draw_item(dc, i, bounds, selected, hovered);
		}

		auto scrollbar_rect = r;
		scrollbar_rect.top = items_top;
		_scrollbar.update(_y_max, _extent.cy - hh, _offset.y);
		_scrollbar.draw(dc, scrollbar_rect);
	}

	bool can_scroll() const
	{
		return _y_max > _extent.cy;
	}

	list_view_item_ptr selection_from_point(const ipoint& pt) const
	{
		if (_items.empty()) return nullptr;

		int lo = 0;
		int hi = static_cast<int>(_items.size()) - 1;

		while (lo <= hi)
		{
			const int mid = lo + (hi - lo) / 2;
			const auto& b = _items[mid]->bounds;

			if (pt.y < b.top)
				hi = mid - 1;
			else if (pt.y >= b.bottom)
				lo = mid + 1;
			else
				return _items[mid];
		}

		return nullptr;
	}

	void set_hover(const pf::window_frame_ptr& window, const list_view_item_ptr& h)
	{
		if (_hover_item != h)
		{
			_hover_item = h;
			window->invalidate();
		}
	}

	void select_list_item(const pf::window_frame_ptr& window, const list_view_item_ptr& item)
	{
		for (const auto& i : _items)
		{
			if (i == item)
			{
				_selected_item = i;
				ensure_visible(window, i);
				window->invalidate();
				on_item_selected(i, true);
				return;
			}
		}
	}

	uint32_t on_left_button_down(const pf::window_frame_ptr& window, const ipoint& point)
	{
		window->set_focus();

		const auto hh = _header_height;
		if (point.y < hh)
			return 0;

		const auto rc = irect(0, hh, _extent.cx, _extent.cy);

		if (_scrollbar.begin_tracking(point, rc, window))
		{
			window->invalidate();
		}
		else
		{
			const auto scroll_pt = ipoint(point.x, point.y + _offset.y);
			const auto i = selection_from_point(scroll_pt);

			if (i)
			{
				set_hover(window, i);
				select_list_item(window, i);
			}
		}

		window->invalidate();
		return 0;
	}

	uint32_t on_mouse_move(const pf::window_frame_ptr& window, const ipoint& point, uint32_t keys)
	{
		if (!_hover)
		{
			window->track_mouse_leave();
			_hover = true;
		}

		if (_scrollbar._tracking)
		{
			const auto hh = _header_height;
			const auto rc = irect(0, hh, _extent.cx, _extent.cy);
			const auto new_pos = _scrollbar.track_to(point, rc);
			scroll_to(window, new_pos);
		}
		else
		{
			const auto rc = irect(0, 0, _extent.cx, _extent.cy);
			_scrollbar.set_hover(_scrollbar.hit_test(point, rc));
		}

		if (point.y >= _header_height)
		{
			const auto scroll_pt = ipoint(point.x, point.y + _offset.y);
			set_hover(window, selection_from_point(scroll_pt));
		}
		else
		{
			set_hover(window, nullptr);
		}
		return 0;
	}

	uint32_t on_left_button_up(const pf::window_frame_ptr& window)
	{
		if (_scrollbar._tracking)
		{
			_scrollbar.end_tracking(window);
			window->invalidate();
		}
		return 0;
	}

	uint32_t on_mouse_leave(const pf::window_frame_ptr& window)
	{
		_hover_item = nullptr;
		_hover = false;
		_scrollbar.set_hover(false);
		window->invalidate();
		return 0;
	}

	void scroll_to(const pf::window_frame_ptr& window, int offset)
	{
		offset = clamp(offset, 0, _y_max - _extent.cy);

		if (_offset.y != offset)
		{
			_offset.y = offset;
			window->invalidate();
		}
	}

	void zoom(const pf::window_frame_ptr& window, const int delta)
	{
		_events.on_zoom(delta, false);
	}

	void ensure_visible(const pf::window_frame_ptr& window, const list_view_item_ptr& item)
	{
		if (!item) return;

		const auto hh = _header_height;
		const auto visible_top = _offset.y + hh;
		const auto visible_bottom = _offset.y + _extent.cy;

		if (item->bounds.top < visible_top)
			scroll_to(window, item->bounds.top - hh);
		else if (item->bounds.bottom > visible_bottom)
			scroll_to(window, item->bounds.bottom - _extent.cy);
	}
};
