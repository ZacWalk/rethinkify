#pragma once

// view_list_files.h — Folder browser panel: tree navigation, expand/collapse, item rendering

#include "view_list.h"


class file_list_view final : public list_view
{
protected:
	uint32_t on_key_down(pf::window_frame_ptr& window, const unsigned int vk) override
	{
		namespace pk = pf::platform_key;

		if (vk == pk::Return)
		{
			if (_selected_item)
			{
				on_item_selected(_selected_item, true);
				if (!_selected_item->is_group)
					_events.set_focus(view_focus::text);
			}
			return 0;
		}

		return list_view::on_key_down(window, vk);
	}

	void on_item_selected(const list_view_item_ptr& item, const bool activated) override
	{
		if (item->is_group)
		{
			if (activated)
			{
				item->expanded = !item->expanded;
				populate();
			}
		}
		else
		{
			_events.path_selected(item->source);
		}
	}

public:
	file_list_view(app_events& events) : list_view(events)
	{
	}

	static bool compare_items(const list_view_item_ptr& l, const list_view_item_ptr& r)
	{
		const auto lf = l;
		const auto rf = r;
		if (lf->is_group != rf->is_group) return lf->is_group > rf->is_group;
		return str::icmp(l->name, r->name) < 0;
	}

	list_view_item_ptr make_list_item(const index_item_ptr& src)
	{
		const auto found = _path_to_item.find(src->path);

		if (found != _path_to_item.end())
		{
			return found->second;
		}

		auto i = std::make_shared<list_view_item>();
		i->name = src->name;
		i->source = src;
		i->depth = 0;
		i->is_group = src->is_folder;
		return i;
	}

	void map_index_items_recursive(std::unordered_map<file_path, list_view_item_ptr, ihash>& items_by_path,
	                               const index_item_ptr& item)
	{
		items_by_path[item->path] = make_list_item(item);

		for (const auto& i : item->children)
		{
			map_index_items_recursive(items_by_path, i);
		}
	}

	void build_folder_items(std::vector<list_view_item_ptr>& items,
	                        const std::vector<index_item_ptr>& children, const int depth)
	{
		for (const auto& child : children)
		{
			auto found = _path_to_item.find(child->path);

			if (found != _path_to_item.end())
			{
				found->second->depth = depth;
				items.push_back(found->second);

				if (found->second->expanded)
					build_folder_items(items, child->children, depth + 1);
			}
		}
	}

	std::unordered_map<file_path, list_view_item_ptr, ihash> _path_to_item;

	void populate()
	{
		std::unordered_map<file_path, list_view_item_ptr, ihash> existing;
		const auto root = _events.root_item();

		if (root)
		{
			map_index_items_recursive(existing, root);
		}

		_path_to_item = existing;

		std::vector<list_view_item_ptr> items;
		if (root)
			build_folder_items(items, root->children, 0);
		_items = std::move(items);
		_events.invalidate(invalid::folder_list);
		layout_list();
	}

	bool expand_path_to(const index_item_ptr& node, const index_item_ptr& target)
	{
		for (const auto& child : node->children)
		{
			if (child == target)
				return true;

			if (child->is_folder && expand_path_to(child, target))
			{
				const auto found = _path_to_item.find(child->path);
				if (found != _path_to_item.end())
					found->second->expanded = true;
				return true;
			}
		}
		return false;
	}

	void select_index_item(const pf::window_frame_ptr& window, const index_item_ptr& item)
	{
		for (const auto& i : _items)
		{
			if (i->source == item)
			{
				if (_selected_item != i)
				{
					_selected_item = i;
					ensure_visible(window, i);
					window->invalidate();
					on_item_selected(i, false);
				}
				return;
			}
		}

		// Item not visible — expand ancestor folders to reveal it
		const auto root = _events.root_item();
		if (root && expand_path_to(root, item))
		{
			populate();

			for (const auto& i : _items)
			{
				if (i->source == item)
				{
					_selected_item = i;
					ensure_visible(window, i);
					window->invalidate();
					on_item_selected(i, false);
					return;
				}
			}
		}
	}
};

using folder_view_ptr = std::shared_ptr<file_list_view>;
