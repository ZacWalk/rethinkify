#pragma once

// app_frame.h — Main window frame: child window creation, layout, splitter, menu command routing

class app_frame final : public pf::frame_reactor, public app_events
{
public:
	app_state _state;

	pf::window_frame_ptr _window;
	pf::window_frame_ptr _view_window;
	pf::window_frame_ptr _list_window;

	text_view_base_ptr _view;
	folder_view_ptr _list;
	search_view_ptr _search;

	// Config-based startup state
	std::wstring _startup_folder;
	std::wstring _startup_document;
	pf::window_frame::placement _startup_placement{};
	bool _has_startup_placement = false;

	double _split_ratio = 0.2;
	bool _is_tracking_splitter = false;
	bool _is_hover_splitter = false;

	app_frame();

	[[nodiscard]] document_ptr& doc() { return _state.active_item()->doc; }
	[[nodiscard]] const document_ptr& doc() const { return _state.active_item()->doc; }

	std::wstring clipboard_text() const
	{
		return pf::platform_text_from_clipboard();
	}

	bool set_clipboard(const std::wstring_view text) const
	{
		return pf::platform_text_to_clipboard(text);
	}

	void ensure_visible(const text_location& pt) override
	{
		_view->ensure_visible(_view_window, pt);
	}

	void invalidate_lines(const int start, const int end) override
	{
		_view->invalidate_lines(_view_window, start, end);
	}

	void path_selected(const index_item_ptr& item) override
	{
		if (pf::is_directory(item->path))
			return;
		load_doc(item);
	}

	void open_path_and_select(const index_item_ptr& item, const int line, const int col,
	                          const int length) override
	{
		load_doc(item);
		const text_selection sel(col, line, col + length, line);
		_state.active_item()->doc->select(sel);
		_state.invalidate(invalid::view | invalid::caret);
	}

	void set_focus(view_focus v) override;

	void set_mode(view_mode m) override;

	void on_search(const std::wstring& text) override
	{
		_state.execute_search_async(text, [this]()
		{
			_search->populate();
		});
	}

	void on_zoom(const int delta, const bool is_text_view) override
	{
		_state.on_zoom(delta, is_text_view);
		_view_window->notify_size();
		_list_window->notify_size();
	}

	void toggle_search_mode();

	view_styles styles() const override { return _state.styles(); }

	uint32_t handle_message(pf::window_frame_ptr window, pf::message_type msg,
	                        uintptr_t wParam, intptr_t lParam) override;

	uint32_t on_create(const pf::window_frame_ptr& window);

	uint32_t on_window_dpi_changed(const uintptr_t wParam, const intptr_t lParam)
	{
		const auto scale_factor = (wParam & 0xFFFF) / static_cast<double>(96);
		_state.on_scale(scale_factor);

		const auto new_bounds = reinterpret_cast<irect*>(lParam);

		if (new_bounds)
		{
			_window->move_window(*new_bounds);
		}

		_view_window->notify_size();
		_list_window->notify_size();

		return 0;
	}

	void on_paint(pf::window_frame_ptr& window, pf::draw_context& dc) override;

	void layout_views() const;

	void on_size(pf::window_frame_ptr& window, isize extent,
	             pf::measure_context& measure) override
	{
		_window = window;
		layout_views();
	}

	void save_config() const
	{
		// Save window position
		if (_window)
		{
			const auto p = _window->get_placement();
			pf::config_write(L"Window", L"Left", std::to_wstring(p.normal_bounds.left));
			pf::config_write(L"Window", L"Top", std::to_wstring(p.normal_bounds.top));
			pf::config_write(L"Window", L"Right", std::to_wstring(p.normal_bounds.right));
			pf::config_write(L"Window", L"Bottom", std::to_wstring(p.normal_bounds.bottom));
			pf::config_write(L"Window", L"Maximized", p.maximized ? L"1" : L"0");
		}

		// Save font sizes
		const auto styles = _state.styles();
		pf::config_write(L"Font", L"TextSize", std::to_wstring(styles.text_font_height));
		pf::config_write(L"Font", L"ListSize", std::to_wstring(styles.list_font_height));

		// Save current root folder and document
		if (_state.root_folder() && !_state.root_folder()->path.empty())
			pf::config_write(L"Recent", L"Folder", _state.root_folder()->path.view());
		if (_state.recent_item() && !_state.recent_item()->path.empty())
			pf::config_write(L"Recent", L"Document", _state.recent_item()->path.view());
	}

	uint32_t on_close()
	{
		if (!prompt_save_all_modified())
			return 0; // user cancelled

		save_config();
		_window->close();
		return 0;
	}

	uint32_t on_about();

	void on_escape() override
	{
		if (is_overlay(_state.get_mode()))
		{
			close_overlay();
		}
		else if (is_markdown(_state.get_mode()))
		{
			close_markdown();
		}
		else if (is_search(_state.get_mode()))
		{
			toggle_search_mode();
		}
	}

	void invalidate(const uint32_t i) override
	{
		_state.invalidate(i);
	}

	std::wstring _message_bar_text;

	std::wstring_view message_bar_text() const override { return _message_bar_text; }
	index_item_ptr root_item() const override { return _state.root_folder(); }
	index_item_ptr active_item() const override { return _state.active_item(); }

	void close_overlay()
	{
		_message_bar_text.clear();

		if (_state.recent_item())
		{
			load_doc(_state.recent_item());
		}
		else
		{
			new_doc();
		}
	}

	void close_markdown()
	{
		_message_bar_text.clear();

		if (_state.get_mode() == view_mode::markdown_files)
			set_mode(view_mode::edit_text_files);
		else if (_state.get_mode() == view_mode::markdown_search)
			set_mode(view_mode::edit_text_search);
	}

	void toggle_markdown_view()
	{
		if (_state.get_mode() == view_mode::markdown_files)
			set_mode(view_mode::edit_text_files);
		else if (_state.get_mode() == view_mode::markdown_search)
			set_mode(view_mode::edit_text_search);
		else if (_state.get_mode() == view_mode::edit_text_files)
			set_mode(view_mode::markdown_files);
		else if (_state.get_mode() == view_mode::edit_text_search)
			set_mode(view_mode::markdown_search);

		update_info_message();
	}

	uint32_t on_exit()
	{
		on_close();
		return 0;
	}

	uint32_t on_run_tests();

	uint32_t on_open()
	{
		const auto path = pf::open_file_path(L"Open File", L"");

		if (!path.empty())
		{
			load_doc(path);
		}
		return 0;
	}

	uint32_t on_save()
	{
		const auto& path = _state.active_item()->path;
		if (path.is_save_path())
		{
			if (save_doc(path))
				_state.invalidate(invalid::folder_list | invalid::title);
		}
		else
		{
			on_save_as();
		}
		return 0;
	}

	uint32_t on_save_as()
	{
		if (save_doc())
		{
			_state.refresh_index(_state.root_folder()->path, [this]
			{
				_state.invalidate(invalid::populate_folder_list);
				_state.invalidate(invalid::title);
			});
		}
		return 0;
	}

	uint32_t on_new()
	{
		new_doc();
		return 0;
	}

	uint32_t on_edit_reformat()
	{
		doc()->reformat_json();
		return 0;
	}

	uint32_t on_edit_remove_duplicates()
	{
		doc()->sort_remove_duplicates();
		return 0;
	}

	void update_title() const
	{
		auto name = _state.active_item()->path.name();
		if (name.empty()) name = _state.active_item()->path.view();
		const auto title = name.empty() ? std::wstring(g_app_name) : std::format(L"{} - {}", name, g_app_name);
		_window->set_text(title);
	}

	void new_doc()
	{
		_state.new_doc();
		set_active_item(_state.active_item());
		_state.refresh_index(_state.root_folder()->path, [this] { _list->populate(); });
	}

	void set_active_item(const index_item_ptr& item);
	void update_info_message();


	uint32_t on_refresh()
	{
		_state.refresh_index(_state.root_folder()->path, [this]
		{
			_list->populate();

			if (is_search(_state.get_mode()))
				_search->refresh_search();
		});
		return 0;
	}

	void on_navigate_next(const bool forward)
	{
		const bool is_search_mode = is_search(_state.get_mode());

		if (is_search_mode)
			_search->navigate_next(_list_window, forward, true);
		else
			_list->navigate_next(_list_window, forward);
	}

	void load_doc(const index_item_ptr& item);
	void load_doc(const file_path& path);

	bool is_path_modified(const index_item_ptr& item) const override
	{
		return _state.is_item_modified(item);
	}

	bool prompt_save_all_modified()
	{
		if (!_state.has_any_modified())
			return true;

		const auto id = _window->message_box(L"You have unsaved changes. Do you want to save all modified files?",
		                                     g_app_name,
		                                     pf::msg_box_style::yes_no_cancel |
		                                     pf::msg_box_style::icon_question);

		if (id == pf::msg_box_result::yes)
		{
			save_all();
			return true;
		}
		if (id == pf::msg_box_result::cancel)
		{
			return false;
		}
		// No — discard changes
		return true;
	}

	uint32_t on_save_all()
	{
		save_all();
		return 0;
	}

	void save_all()
	{
		_state.save_all();
		_state.invalidate(invalid::folder_list | invalid::title);
	}

	bool save_doc(const file_path& path)
	{
		return _state.save_active_doc(path);
	}

	bool save_doc()
	{
		const auto path = pf::save_file_path(L"Save File", _state.active_item()->path, L"");

		return !path.empty() && save_doc(path);
	}

	void on_idle();
};
