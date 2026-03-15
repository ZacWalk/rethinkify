#pragma once

// app_state.h — Application state: document collection, file operations, testable app logic

#include "document.h"


struct search_result;

class app_state
{
public:
	static const file_path about_path;
	static const file_path test_results_path;

	static constexpr uint64_t max_search_file_size = 10 * 1024 * 1024;

	app_state(document_events& events) : _events(events)
	{
		_active_item = std::make_shared<index_item>();
		_active_item->doc = std::make_shared<document>(events);
		_root_folder = std::make_shared<index_item>();
	}

	[[nodiscard]] const index_item_ptr& active_item() const { return _active_item; }
	[[nodiscard]] const index_item_ptr& recent_item() const { return _recent_item; }
	[[nodiscard]] const index_item_ptr& root_folder() const { return _root_folder; }
	[[nodiscard]] view_styles styles() const { return _styles; }
	[[nodiscard]] view_mode get_mode() const { return _mode; }

	void invalidate(const uint32_t i)
	{
		_invalid |= i;
	}

	[[nodiscard]] uint32_t validate()
	{
		return _invalid.exchange(0);
	}

	[[nodiscard]] document_ptr& doc() { return _active_item->doc; }
	[[nodiscard]] const document_ptr& doc() const { return _active_item->doc; }

	void set_active_item(const index_item_ptr& item)
	{
		_active_item = item;
	}

	void set_recent_item(const index_item_ptr& item)
	{
		_recent_item = item;
	}

	void set_mode(const view_mode m)
	{
		_mode = m;
	}

	bool save_active_doc(const file_path& path)
	{
		if (_active_item->doc->save_to_file(path))
		{
			_active_item->doc->path(path);
			_active_item->path = path;
			_active_item->name = path.name();
			return true;
		}
		return false;
	}

	index_item_ptr create_overlay(std::wstring_view text, const file_path& path)
	{
		const auto d = std::make_shared<document>(_events, text, true);
		d->path(path);
		d->read_only(true);
		return std::make_shared<index_item>(path, std::wstring(path.name()), false, d);
	}

	[[nodiscard]] bool has_any_modified() const
	{
		return any_doc_modified(_root_folder->children);
	}

	[[nodiscard]] bool is_item_modified(const index_item_ptr& item) const
	{
		if (!item->path.is_save_path())
			return true;
		if (item->doc)
			return item->doc->is_modified();
		return false;
	}

	void update_styles();

	void on_scale(const double scale_factor)
	{
		_styles.dpi_scale = scale_factor;
		update_styles();
	}

	void on_zoom(const int delta, const bool is_text_view)
	{
		if (is_text_view)
		{
			_styles.text_font_height = clamp(_styles.text_font_height + delta, 8, 72);
		}
		else
		{
			_styles.list_font_height = clamp(_styles.list_font_height + delta, 8, 72);
		}

		update_styles();
	}

	void initialize_styles(const int lh, const int th)
	{
		_styles.list_font_height = clamp(lh, 8, 72);
		_styles.text_font_height = clamp(th, 8, 72);
		update_styles();
	}

	void new_doc()
	{
		const auto name = std::format(L"new-{}.md", _next_new_doc_number++);
		const auto d = std::make_shared<document>(_events);
		d->path(file_path{name});
		const auto item = std::make_shared<index_item>(d->path(), std::wstring(d->path().name()), false, d);

		if (_root_folder)
			_root_folder->children.push_back(item);

		set_active_item(item);
	}

	static void map_index_items_recursive(std::unordered_map<file_path, index_item_ptr, ihash>& items_by_path,
	                                      const index_item_ptr& item)
	{
		items_by_path[item->path] = item;

		for (const auto& i : item->children)
		{
			map_index_items_recursive(items_by_path, i);
		}
	}

	static index_item_ptr make_item(const std::unordered_map<file_path, index_item_ptr, ihash>& existing,
	                                file_path path, bool is_folder)
	{
		auto item = std::make_shared<index_item>(path, std::wstring(path.name()), is_folder);

		const auto found = existing.find(path);
		if (found != existing.end())
			item->doc = found->second->doc;

		return item;
	}

	static index_item_ptr load_index(const file_path& root_path,
	                                 const std::unordered_map<file_path, index_item_ptr, ihash>& existing)
	{
		index_item_ptr root = make_item(existing, root_path, true);
		std::vector<index_item_ptr> folders_to_load{root};

		while (!folders_to_load.empty())
		{
			const auto current = folders_to_load.back();
			folders_to_load.pop_back();

			const auto contents = pf::iterate_file_items(current->path, false);
			current->children.clear();

			for (const auto& f : contents.folders)
			{
				auto item = make_item(existing, f.path, true);
				item->is_folder = true;
				current->children.push_back(item);
				folders_to_load.push_back(item);
			}

			for (const auto& f : contents.files)
			{
				auto item = make_item(existing, f.path, false);
				item->is_folder = false;
				current->children.push_back(item);
			}

			std::ranges::sort(current->children, [](const index_item_ptr& l, const index_item_ptr& r)
			{
				if (l->is_folder != r->is_folder) return l->is_folder > r->is_folder;
				return str::icmp(l->name, r->name) < 0;
			});
		}

		return root;
	}

	void refresh_index(const file_path& root_path, std::function<void()> on_complete = {})
	{
		std::unordered_map<file_path, index_item_ptr, ihash> existing;
		if (_root_folder)
			map_index_items_recursive(existing, _root_folder);

		pf::run_async([this, root_path, existing = std::move(existing), on_complete = std::move(on_complete)]() mutable
		{
			auto new_root = load_index(root_path, std::move(existing));

			pf::run_ui([this, new_root = std::move(new_root), on_complete = std::move(on_complete)]()
			{
				set_root(new_root);
				invalidate(invalid::folder_list);
				invalidate(invalid::populate_folder_list);

				if (on_complete)
					on_complete();
			});
		});
	}

	void set_root(const index_item_ptr& root)
	{
		_root_folder = root;
	}

	void save_all()
	{
		save_all_items(_root_folder->children);
	}

	// ── Search ─────────────────────────────────────────────────────────────

	struct search_input
	{
		file_path path;
		document_ptr doc;
	};

	using search_results_map = std::unordered_map<file_path, std::vector<search_result>, ihash>;

	void execute_search(const std::wstring& text)
	{
		std::vector<search_input> inputs;
		collect_search_inputs(_root_folder->children, inputs);
		const auto results = perform_search(inputs, text);
		apply_search_results(_root_folder->children, results);
	}

	void execute_search_async(const std::wstring& text, std::function<void()> on_complete = {})
	{
		std::vector<search_input> inputs;
		collect_search_inputs(_root_folder->children, inputs);

		pf::run_async([this, inputs = std::move(inputs), text, on_complete = std::move(on_complete)]() mutable
		{
			auto results = perform_search(inputs, text);

			pf::run_ui([this, results = std::move(results), on_complete = std::move(on_complete)]()
			{
				apply_search_results(_root_folder->children, results);
				if (on_complete)
					on_complete();
			});
		});
	}

private:
	[[nodiscard]] std::wstring relative_name(const file_path& path) const
	{
		auto rel_name = std::wstring(path.view());
		const auto root_view = _root_folder->path.view();
		if (rel_name.length() > root_view.length() && str::icmp(
			std::wstring_view(rel_name).substr(0, root_view.length()), root_view) == 0)
		{
			rel_name = rel_name.substr(root_view.length());
			if (!rel_name.empty() && (rel_name.starts_with(L'\\') || rel_name.starts_with(L'/')))
				rel_name = rel_name.substr(1);
		}
		return rel_name;
	}

	static bool any_doc_modified(const std::vector<index_item_ptr>& items)
	{
		for (const auto& item : items)
		{
			if (item->doc && item->doc->is_modified())
				return true;
			if (item->is_folder && !item->children.empty() && any_doc_modified(item->children))
				return true;
		}
		return false;
	}

	static void save_all_items(const std::vector<index_item_ptr>& items)
	{
		for (const auto& item : items)
		{
			if (item->doc && item->doc->is_modified() &&
				!item->path.empty() && item->path.is_save_path())
				item->doc->save_to_file(item->path);
			if (item->is_folder && !item->children.empty())
				save_all_items(item->children);
		}
	}

	static constexpr int max_search_results = 5000;

	static void collect_search_inputs(const std::vector<index_item_ptr>& items, std::vector<search_input>& inputs)
	{
		for (const auto& item : items)
		{
			if (!item->is_folder)
				inputs.push_back({item->path, item->doc});
			collect_search_inputs(item->children, inputs);
		}
	}

	static search_results_map perform_search(const std::vector<search_input>& inputs, const std::wstring& text);

	static void apply_search_results(const std::vector<index_item_ptr>& items, const search_results_map& results)
	{
		for (const auto& item : items)
		{
			if (!item->is_folder)
			{
				const auto it = results.find(item->path);
				item->search_results = (it != results.end()) ? it->second : std::vector<search_result>{};
			}
			apply_search_results(item->children, results);
		}
	}

	document_events& _events;
	index_item_ptr _active_item;
	index_item_ptr _recent_item;
	index_item_ptr _root_folder;
	view_styles _styles;
	view_mode _mode = view_mode::edit_text_files;

	int _next_new_doc_number = 1;
	std::atomic<uint32_t> _invalid = 0;
};
