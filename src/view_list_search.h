#pragma once

// view_list_search.h — Search panel: text input, result display, navigation

#include "view_list.h"


class search_list_view final : public list_view
{
	edit_box _edit;
	isize _edit_font_char_size = {10, 10};

	std::wstring _last_searched;
	int _result_count = 0;

	caret_blinker _caret;

	irect edit_box_rect() const
	{
		const auto styles = _events.styles();
		const auto m = styles.edit_box_margin;
		const auto ef = styles.edit_font;
		const auto h = ef.size + styles.edit_box_inner_pad * 2;
		return irect(m, m, _extent.cx - m, m + h);
	}

	int edit_inner_pad() const
	{
		return _events.styles().edit_box_inner_pad;
	}

	void update_caret(const pf::window_frame_ptr& window)
	{
		if (!window || !window->has_focus()) return;
		_caret.reset(window);
		_events.invalidate(invalid::search_list);
	}

	uint32_t on_timer(pf::window_frame_ptr& window, const uint32_t id) override
	{
		if (_caret.on_timer(id))
			window->invalidate_rect(edit_box_rect());
		return 0;
	}

protected:
	int calc_header_height() const
	{
		const auto styles = _events.styles();
		const auto m = styles.edit_box_margin;
		const auto ef = styles.edit_font;
		const auto h = ef.size + styles.edit_box_inner_pad * 2;
		return m + h + m;
	}

	void draw_header(pf::window_frame_ptr& window, pf::draw_context& dc, const irect& header_rect) override
	{
		dc.fill_solid_rect(header_rect, ui::tool_wnd_clr);

		const auto eb = edit_box_rect();

		// Edit box background
		dc.fill_solid_rect(eb, ui::tool_wnd_clr.darken(16));

		const auto has_focus = window->has_focus();
		edit_box::draw_border(dc, eb, has_focus);

		const auto styles = _events.styles();
		const auto pad = styles.edit_box_inner_pad;
		const auto text_rect = eb.Inflate(-pad, -pad);

		const auto ef = styles.edit_font;

		const auto char_sz = dc.measure_text(L"X", ef);
		const auto text_y = eb.top + (eb.Height() - char_sz.cy) / 2;
		constexpr auto bg_color = ui::tool_wnd_clr.darken(16);

		if (_edit.text.empty())
		{
			// Placeholder text — centered in edit box
			constexpr std::wstring_view placeholder = L"Search...";
			const auto ph_sz = dc.measure_text(placeholder, ef);
			const auto ph_x = eb.left + (eb.Width() - ph_sz.cx) / 2;
			dc.draw_text(ph_x, text_y, text_rect, placeholder, ef, ui::handle_hover_color, bg_color);
		}
		else
		{
			_edit.draw_selection(dc, eb.left + pad, text_y, char_sz.cy, ef);
			dc.draw_text(eb.left + pad, text_y, text_rect, _edit.text, ef, ui::text_color, bg_color);
		}

		// Draw result count below edit box
		if (!_last_searched.empty())
		{
			const auto count_text = std::format(L"{} results", _result_count);
			auto count_rect = header_rect;
			count_rect.top = eb.bottom + 2;
			count_rect.left = eb.left + pad;
			if (count_rect.top < count_rect.bottom)
				dc.draw_text(count_rect.left, count_rect.top, count_rect, count_text, styles.list_font,
				             ui::darker_text_color, ui::tool_wnd_clr);
		}

		if (has_focus && _caret.visible)
			_edit.draw_caret(dc, eb.left + edit_inner_pad(), text_y, char_sz.cy, ef);
	}

	void on_item_selected(const list_view_item_ptr& item, const bool activated) override
	{
		if (item->is_group && activated)
		{
			toggle_header_collapse(item);
			return;
		}

		if (!item->is_group)
		{
			_events.open_path_and_select(item->source, item->line_number, item->line_match_pos,
			                             item->text_match_length);
		}
	}

	void on_size(pf::window_frame_ptr& window, const isize extent,
	             pf::measure_context& measure) override
	{
		const auto styles = _events.styles();
		_edit_font_char_size = measure.measure_char(styles.edit_font);
		_header_height = calc_header_height();
		list_view::on_size(window, extent, measure);
	}

	void update_focus(pf::window_frame_ptr& window) override
	{
		if (window->has_focus())
			_caret.start(window);
		else
			_caret.stop(window);
		list_view::update_focus(window);
	}

	uint32_t on_char(pf::window_frame_ptr& window, const wchar_t ch) override
	{
		if (ch == L'\r' || ch == L'\n')
		{
			if (_selected_item && _selected_item->is_group)
			{
				toggle_header_collapse(_selected_item);
			}
			else
			{
				_events.set_focus(view_focus::text);
			}
			return 0;
		}

		const bool modified = _edit.on_char(window, ch);
		update_caret(window);
		_events.invalidate(invalid::search_list);
		if (modified) trigger_search();
		return 0;
	}

	uint32_t on_key_down(pf::window_frame_ptr& window, const unsigned int vk) override
	{
		namespace pk = pf::platform_key;

		bool text_modified = false;
		if (_edit.on_key_down(window, vk, text_modified))
		{
			update_caret(window);
			_events.invalidate(invalid::search_list);
			if (text_modified) trigger_search();
			return 0;
		}

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

		if (vk == pk::Escape)
		{
			_events.on_escape();
			return 0;
		}

		if (vk == pk::F5)
		{
			if (!_edit.text.empty())
			{
				_last_searched = _edit.text;
				_events.on_search(_edit.text);
			}
			return 0;
		}

		return 0;
	}

public:
	search_list_view(app_events& events) : list_view(events)
	{
	}

	void trigger_search()
	{
		if (_edit.text != _last_searched)
		{
			_last_searched = _edit.text;
			_events.on_search(_edit.text);
		}
	}

	struct search_key
	{
		file_path path;
		int line_number = -1;
		int match_pos = -1;

		bool operator==(const search_key& other) const
		{
			return path == other.path && line_number == other.line_number && match_pos == other.match_pos;
		}
	};

	struct search_key_hash
	{
		size_t operator()(const search_key& key) const
		{
			size_t h = ihash{}(key.path);
			h ^= std::hash<int>{}(key.line_number) + 0x9e3779b9 + (h << 6) + (h >> 2);
			h ^= std::hash<int>{}(key.match_pos) + 0x9e3779b9 + (h << 6) + (h >> 2);
			return h;
		}
	};

	std::unordered_map<search_key, list_view_item_ptr, search_key_hash> _key_to_item;

	search_key make_key(const index_item_ptr& i, const search_result& s)
	{
		return {i->path, s.line_number, s.line_match_pos};
	}

	search_key make_key(const index_item_ptr& i)
	{
		return {i->path, -1, -1};
	}

	list_view_item_ptr make_list_item(const index_item_ptr& item, const search_result& s)
	{
		const auto found = _key_to_item.find(make_key(item, s));

		if (found != _key_to_item.end())
		{
			found->second->text_match_start = s.text_match_start;
			found->second->text_match_length = s.text_match_length;
			return found->second;
		}

		auto i = std::make_shared<list_view_item>();
		i->name = s.line_text;
		i->source = item;
		i->depth = 0;
		i->is_group = false;
		i->line_number = s.line_number;
		i->line_match_pos = s.line_match_pos;
		i->text_match_start = s.text_match_start;
		i->text_match_length = s.text_match_length;
		return i;
	}

	std::wstring relative_path(const index_item_ptr& item) const
	{
		const auto root = _events.root_item();
		if (!root) return item->name;
		const auto root_view = root->path.view();
		const auto item_view = item->path.view();
		if (item_view.length() > root_view.length() &&
			str::icmp(item_view.substr(0, root_view.length()), root_view) == 0)
		{
			auto rel = std::wstring(item_view.substr(root_view.length()));
			if (!rel.empty() && (rel[0] == L'\\' || rel[0] == L'/'))
				rel = rel.substr(1);
			return rel;
		}
		return item->name;
	}

	list_view_item_ptr make_list_item(const index_item_ptr& item)
	{
		const auto found = _key_to_item.find(make_key(item));

		if (found != _key_to_item.end())
		{
			found->second->name = std::format(L"{} ({})", relative_path(item),
			                                  static_cast<int>(item->search_results.size()));
			return found->second;
		}

		auto i = std::make_shared<list_view_item>();
		i->name = std::format(L"{} ({})", relative_path(item),
		                      static_cast<int>(item->search_results.size()));
		i->source = item;
		i->depth = 0;
		i->is_group = true;
		i->expanded = true;
		return i;
	}

	void map_index_items_recursive(std::unordered_map<search_key, list_view_item_ptr, search_key_hash>& items_by_path,
	                               const index_item_ptr& item)
	{
		items_by_path[make_key(item)] = make_list_item(item);

		for (const auto& s : item->search_results)
		{
			items_by_path[make_key(item, s)] = make_list_item(item, s);
		}

		for (const auto& i : item->children)
		{
			map_index_items_recursive(items_by_path, i);
		}
	}

	void build_folder_items(std::vector<list_view_item_ptr>& items,
	                        const std::vector<index_item_ptr>& children)
	{
		for (const auto& child : children)
		{
			auto found = _key_to_item.find(make_key(child));

			if (found != _key_to_item.end())
			{
				if (!child->is_folder && !child->search_results.empty())
				{
					found->second->depth = 0;
					items.push_back(found->second);

					if (found->second->expanded)
					{
						for (const auto& s : child->search_results)
						{
							auto val = make_list_item(child, s);
							val->depth = 1;
							items.push_back(val);
						}
					}
				}

				build_folder_items(items, child->children);
			}
		}
	}

	void populate()
	{
		std::unordered_map<search_key, list_view_item_ptr, search_key_hash> existing;
		const auto root = _events.root_item();

		if (root)
		{
			map_index_items_recursive(existing, root);
		}

		_key_to_item = existing;

		std::vector<list_view_item_ptr> items;
		if (root)
			build_folder_items(items, root->children);
		_items = std::move(items);

		_result_count = 0;
		for (const auto& i : _items)
			if (!i->is_group) _result_count++;

		_events.invalidate(invalid::search_list);
		layout_list();
	}


	void refresh_search()
	{
		if (!_edit.text.empty())
		{
			_last_searched = _edit.text;
			_events.on_search(_edit.text);
		}
	}

private:
	void toggle_header_collapse(const list_view_item_ptr& header_item)
	{
		const auto si = header_item;
		si->expanded = !si->expanded;
		populate();
	}
};

using search_view_ptr = std::shared_ptr<search_list_view>;
