// app.cpp — Application logic: main window, menus, splitter, file I/O commands

#include "pch.h"

#include "ui.h"
#include "app.h"
#include "document.h"
#include "app_state.h"

#include "view_list_files.h"
#include "view_list_search.h"
#include "view_text_base.h"
#include "view_text_edit.h"
#include "view_markdown.h"
#include "view_hex.h"

#include "app_frame.h"

auto g_app_name = L"Rethinkify";

constexpr auto splitter_bar_width = 5;

extern std::wstring run_all_tests();

const file_path app_state::about_path{L"::about"};
const file_path app_state::test_results_path{L"::test"};


enum class command_id : int
{
	none = 0,

	// File
	file_new = 1001,
	file_open,
	file_save,
	file_save_as,
	file_save_all,
	file_print,

	// Edit (menu)
	edit_undo,
	edit_redo,
	edit_cut,
	edit_copy,
	edit_paste,
	edit_delete,
	edit_select_all,
	edit_replace,
	edit_reformat,
	edit_sort_remove_duplicates,

	// App
	app_about,
	app_exit,
	help_run_tests,

	edit_spell_check,
	edit_search_files,

	// View
	view_word_wrap,
	view_toggle_markdown,
	view_refresh_folder,
	view_next_result,
	view_prev_result,
};

color_t style_to_color(const style style_index)
{
	switch (style_index)
	{
	case style::white_space:
		return ui::main_wnd_clr;
	case style::main_wnd_clr:
		return ui::main_wnd_clr;
	case style::tool_wnd_clr:
		return ui::tool_wnd_clr;
	case style::normal_bkgnd:
		return color_t(30, 30, 30);
	case style::normal_text:
		return color_t(222, 222, 222);
	case style::sel_margin:
		return color_t(44, 44, 44);
	case style::code_preprocessor:
		return color_t(133, 133, 211);
	case style::code_comment:
		return color_t(128, 222, 128);
	case style::code_number:
		return color_t(244, 244, 144);
	case style::code_string:
		return color_t(244, 244, 144);
	case style::code_operator:
		return color_t(128, 255, 128);
	case style::code_keyword:
		return color_t(128, 128, 255);
	case style::sel_bkgnd:
		return color_t(88, 88, 88);
	case style::sel_text:
		return color_t(255, 255, 255);
	case style::error_bkgnd:
		return color_t(128, 0, 0);
	case style::error_text:
		return color_t(255, 100, 100);
	case style::md_heading1:
		return color_t(100, 200, 255);
	case style::md_heading2:
		return color_t(140, 180, 255);
	case style::md_heading3:
		return color_t(180, 160, 255);
	case style::md_bold:
		return color_t(255, 255, 255);
	case style::md_italic:
		return color_t(180, 220, 180);
	case style::md_link_text:
		return color_t(100, 180, 255);
	case style::md_link_url:
		return color_t(120, 120, 120);
	case style::md_marker:
		return color_t(80, 80, 80);
	case style::md_bullet:
		return color_t(200, 200, 100);
	}
	return color_t(222, 222, 222);
}

static std::wstring make_about_text()
{
	return L"# Rethinkify\n"
		L"\n"
		L"*A lightweight text editor written in C++ by Zac Walker*\n"
		L"\n"
		L"## Keyboard Shortcuts\n"
		L"\n"
		L"### File\n"
		L"- **Ctrl+N** New document\n"
		L"- **Ctrl+O** Open file\n"
		L"- **Ctrl+S** Save file\n"
		L"- **Ctrl+Shift+S** Save all modified files\n"
		L"\n"
		L"### Edit\n"
		L"- **Ctrl+Z** Undo\n"
		L"- **Ctrl+Y** Redo\n"
		L"- **Ctrl+X** Cut\n"
		L"- **Ctrl+C** Copy\n"
		L"- **Ctrl+V** Paste\n"
		L"- **Ctrl+A** Select all\n"
		L"- **Ctrl+Shift+F** Search in files\n"
		L"- **Ctrl+R** Reformat\n"
		L"- **Tab** Indent\n"
		L"- **Shift+Tab** Unindent\n"
		L"- **Del** Delete\n"
		L"- **Backspace** Delete back\n"
		L"\n"
		L"### Navigation\n"
		L"- **Ctrl+Home** Go to beginning\n"
		L"- **Ctrl+End** Go to end\n"
		L"- **Home** Go to line start\n"
		L"- **End** Go to line end\n"
		L"- **Ctrl+Left** Word left\n"
		L"- **Ctrl+Right** Word right\n"
		L"- **Page Up / Page Down** Page navigation\n"
		L"- **Ctrl+Up / Ctrl+Down** Scroll\n"
		L"\n"
		L"### View\n"
		L"- **Ctrl++** Zoom in\n"
		L"- **Ctrl+-** Zoom out\n"
		L"- **Ctrl+0** Reset zoom\n"
		L"- **Alt+Z** Toggle word wrap\n"
		L"- **Ctrl+M** Toggle markdown preview\n"
		L"- **Ctrl+Shift+P** Toggle spell check\n"
		L"- **F5** Refresh\n"
		L"- **F8** Next result\n"
		L"- **Shift+F8** Previous result\n"
		L"- **Ctrl+T** Run tests\n"
		L"- **F1** About / Help\n"
		L"- **Escape** Close about / Return to document\n"
		L"\n"
		L"*Hold Shift with navigation keys to extend selection.*\n";
}

app_frame::app_frame() : _state(*this),
                         _view(std::make_shared<text_edit_view>(*this)),
                         _list(std::make_shared<file_list_view>(*this)),
                         _search(std::make_shared<search_list_view>(*this))
{
	_view->set_document(_state.active_item()->doc);
}

static index_item_ptr find_item_recursively(const index_item_ptr& item,
                                            const file_path& path)
{
	if (item->path == path)
		return item;

	for (const auto& child : item->children)
	{
		auto found = find_item_recursively(child, path);
		if (found)
			return found;
	}
	return nullptr;
}

void app_frame::load_doc(const index_item_ptr& item)
{
	auto d = item->doc;
	bool load_from_disk = true;

	if (d)
	{
		const auto current_time = pf::file_modified_time(item->path);
		const uint64_t disk_modified_time = d->disk_modified_time();
		if (disk_modified_time > 1 && current_time != disk_modified_time)
		{
			const auto id = _window->message_box(
				L"This file has been modified on disk. Do you want to reload it and lose your local changes?",
				g_app_name,
				pf::msg_box_style::yes_no | pf::msg_box_style::icon_question);

			load_from_disk = id == pf::msg_box_result::yes;
		}
		else
		{
			load_from_disk = false;
		}
	}
	else
	{
		auto encoding = is_binary_extension(item->path) ? file_encoding::binary : file_encoding::utf8;
		d = std::make_shared<document>(*this, item->path, 1, encoding);
		item->doc = d;
	}

	set_active_item(item);

	if (load_from_disk)
	{
		pf::run_async([this, item]
		{
			auto lines = load_lines(item->path);

			pf::run_ui([this, item, lines = std::move(lines)]()
			{
				item->doc->apply_loaded_data(item->path, lines);

				// Only switch view if this item is still the active one,
				// otherwise we'd override the user's current selection
				if (_state.active_item() == item)
					set_active_item(item);
			});
		});
	}
}

void app_frame::load_doc(const file_path& path)
{
	const auto item = find_item_recursively(_state.root_folder(), path);

	if (item)
	{
		load_doc(item);
	}
	else
	{
		_state.refresh_index(_state.root_folder()->path, [this, path]
		{
			const auto item = find_item_recursively(_state.root_folder(), path);

			if (item)
			{
				load_doc(item);
				return;
			}

			_window->message_box(
				std::format(L"The file \"{}\" was not found.", path.name()),
				g_app_name,
				pf::msg_box_style::ok | pf::msg_box_style::icon_warning);
		});
	}
}

void app_frame::set_active_item(const index_item_ptr& item)
{
	_state.set_active_item(item);

	const auto& d = item->doc;
	const auto is_search = ::is_search(_state.get_mode());

	// Auto-select view based on desired view type
	switch (d->get_doc_type())
	{
	case doc_type::overlay:
		set_mode(view_mode::overlay);
		break;
	case doc_type::hex:
		set_mode(is_search ? view_mode::hex_search : view_mode::hex_files);
		break;
	case doc_type::markdown:
		set_mode(is_search ? view_mode::markdown_search : view_mode::markdown_files);
		break;
	case doc_type::text:
		set_mode(is_search ? view_mode::edit_text_search : view_mode::edit_text_files);
		break;
	}

	const bool is_overlay = d->get_doc_type() == doc_type::overlay;

	if (!is_overlay)
	{
		pf::config_write(L"Recent", L"Document", item->path.view());
		_state.set_recent_item(item);
	}

	update_info_message();
}

void app_frame::update_info_message()
{
	if (is_overlay(_state.get_mode()))
	{
		_message_bar_text = L"Press Escape to exit.";
	}
	else if (is_markdown(_state.get_mode()))
	{
		_message_bar_text = L"Preview mode. Press Escape to edit.";
	}
	else
	{
		_message_bar_text.clear();
	}

	_state.invalidate(invalid::view);
}

void app_frame::set_focus(const view_focus v)
{
	if (v == view_focus::list)
		_list_window->set_focus();
	else
		_view_window->set_focus();
}

void app_state::update_styles()
{
	_styles.list_font = {_styles.list_font_height, pf::font_name::calibri};
	_styles.edit_font = {(_styles.list_font_height * 3) / 2, pf::font_name::calibri};
	_styles.text_font = {_styles.text_font_height, pf::font_name::consolas};

	_styles.padding_x = static_cast<int>(5 * _styles.dpi_scale);
	_styles.padding_y = static_cast<int>(5 * _styles.dpi_scale);
	_styles.indent = static_cast<int>(16 * _styles.dpi_scale);
}


void app_frame::set_mode(const view_mode m)
{
	text_view_base_ptr new_view;

	if (_state.get_mode() != m)
	{
		if (!is_markdown(_state.get_mode()) && is_markdown(m))
		{
			new_view = std::make_shared<markdown_view>(*this);
		}
		else if (!is_hex(_state.get_mode()) && is_hex(m))
		{
			new_view = std::make_shared<hex_view>(*this);
		}
		else if (!is_edit_text(_state.get_mode()) && is_edit_text(m))
		{
			new_view = std::make_shared<text_edit_view>(*this);
		}
	}

	_state.set_mode(m);

	if (new_view)
	{
		if (_view)
			_view->stop_caret_blink(_view_window);

		new_view->set_document(_state.active_item()->doc);

		_view = new_view;
		_view_window->set_reactor(new_view);
		_view_window->notify_size();
		_view->scroll_to_top();
		_view->update_focus(_view_window);
		_state.invalidate(invalid::view);
	}
	else
	{
		_view->set_document(_state.active_item()->doc);
		_state.invalidate(invalid::view);
	}

	_list_window->show(!is_overlay(m));
	_list_window->set_reactor(is_search(m) ? std::static_pointer_cast<frame_reactor>(_search) : _list);
	_list_window->notify_size();
	_list_window->invalidate();
	_list->select_index_item(_list_window, _state.active_item());
	_state.invalidate(invalid::view);
	layout_views();
}

void app_frame::toggle_search_mode()
{
	switch (_state.get_mode())
	{
	case view_mode::edit_text_files:
		set_mode(view_mode::edit_text_search);
		_list_window->set_focus();
		break;
	case view_mode::markdown_files:
		set_mode(view_mode::markdown_search);
		_list_window->set_focus();
		break;
	case view_mode::hex_files:
		set_mode(view_mode::hex_search);
		_list_window->set_focus();
		break;
	case view_mode::edit_text_search:
		set_mode(view_mode::edit_text_files);
		break;
	case view_mode::markdown_search:
		set_mode(view_mode::markdown_files);
		break;
	case view_mode::hex_search:
		set_mode(view_mode::hex_files);
		break;
	}
}

static void find_matches_in_line(std::vector<search_result>& results, const std::wstring_view line,
                                 const int line_number, const std::wstring_view text)
{
	if (line.empty()) return;

	size_t trim = 0;
	while (trim < line.length() && (line[trim] == L' ' || line[trim] == L'\t')) trim++;

	auto pos = str::find_in_text(line, text);
	while (pos != std::wstring_view::npos)
	{
		search_result item;
		item.line_text = std::wstring(line.substr(trim));
		item.line_number = line_number;
		item.line_match_pos = static_cast<int>(pos);
		item.text_match_start = pos >= trim ? static_cast<int>(pos - trim) : 0;
		item.text_match_length = static_cast<int>(text.length());
		results.push_back(std::move(item));

		const auto next_start = pos + text.length();
		if (next_start >= line.length()) break;
		const auto next_pos = str::find_in_text(line.substr(next_start), text);
		if (next_pos == std::wstring_view::npos) break;
		pos = next_start + next_pos;
	}
}

static std::vector<search_result> search_file_results(const file_path& path, const document_ptr& doc,
                                                      const std::wstring& text)
{
	if (text.empty()) return {};
	if (is_binary_file(path)) return {};

	if (doc)
	{
		std::vector<search_result> results;
		for (int line_number = 0; line_number < static_cast<int>(doc->size()); line_number++)
			find_matches_in_line(results, (*doc)[line_number]._text, line_number, text);
		return results;
	}

	const auto handle = pf::open_for_read(path);
	if (!handle) return {};

	const auto size = handle->size();
	if (size > app_state::max_search_file_size || size == 0) return {};

	std::vector<search_result> results;

	iterate_file_lines(handle, [&](const std::wstring& line, const int line_number)
	{
		find_matches_in_line(results, line, line_number, text);
	});

	return results;
}

app_state::search_results_map app_state::perform_search(const std::vector<search_input>& inputs,
                                                        const std::wstring& text)
{
	search_results_map results;
	int total = 0;

	for (const auto& input : inputs)
	{
		if (total >= max_search_results) break;

		auto file_results = search_file_results(input.path, input.doc, text);
		total += static_cast<int>(file_results.size());

		if (!file_results.empty())
			results[input.path] = std::move(file_results);
	}

	return results;
}

uint32_t app_frame::handle_message(const pf::window_frame_ptr window,
                                   const pf::message_type msg, const uintptr_t wParam, const intptr_t lParam)
{
	_window = window;
	using mt = pf::message_type;

	if (msg == mt::create)
		return on_create(window);
	if (msg == mt::erase_background)
		return 1;
	if (msg == mt::set_focus)
	{
		_view_window->set_focus();
		return 0;
	}
	if (msg == mt::close)
		return on_close();
	if (msg == mt::command)
		return 0;
	if (msg == mt::dpi_changed)
		return on_window_dpi_changed(wParam, lParam);

	if (msg == mt::left_button_down)
	{
		const auto x_pos = pf::point_from_lparam(lParam).x;
		const auto rect = window->get_client_rect();
		const auto split_pos = static_cast<int>(rect.left + (rect.right - rect.left) * _split_ratio);

		_is_tracking_splitter = x_pos > split_pos - splitter_bar_width &&
			x_pos < split_pos + splitter_bar_width;

		if (_is_tracking_splitter)
		{
			_window->set_capture();
			_window->set_cursor_shape(pf::cursor_shape::size_we);
		}
	}

	if (msg == mt::mouse_leave)
	{
		if (_is_hover_splitter)
		{
			_is_hover_splitter = false;
			_window->invalidate();
		}
	}

	if (msg == mt::mouse_move)
	{
		const auto x_pos = pf::point_from_lparam(lParam).x;
		const auto rect = window->get_client_rect();

		if (wParam == 0x0001 /*MK_LBUTTON*/)
		{
			if (_is_tracking_splitter)
			{
				const auto width = rect.right - rect.left;
				if (width > 0)
				{
					_split_ratio = (x_pos - rect.left) / static_cast<double>(width);
					if (_split_ratio < 0.05)
						_split_ratio = 0.05;
					if (_split_ratio > 0.95)
						_split_ratio = 0.95;
				}
				_window->invalidate();
				layout_views();
			}
		}

		const auto split_pos = static_cast<int>(rect.left + (rect.right - rect.left) * _split_ratio);
		const auto new_hover_splitter = x_pos > split_pos - splitter_bar_width &&
			x_pos < split_pos + splitter_bar_width;

		if (new_hover_splitter != _is_hover_splitter)
		{
			_is_hover_splitter = new_hover_splitter;
			_window->invalidate();

			if (_is_hover_splitter)
			{
				_window->track_mouse_leave();
			}
		}

		if (_is_hover_splitter)
		{
			_window->set_cursor_shape(pf::cursor_shape::size_we);
		}
	}

	if (msg == mt::left_button_up)
	{
		if (_is_tracking_splitter)
		{
			_window->release_capture();
			_window->invalidate();
			_is_tracking_splitter = false;
		}
	}

	return 0;
}

uint32_t app_frame::on_create(const pf::window_frame_ptr& window)
{
	pf::debug_trace(L"app_frame::on_create ENTERED\n");
	_view_window = window->create_child(L"TEXT_FRAME",
	                                    pf::window_style::child | pf::window_style::visible |
	                                    pf::window_style::clip_children,
	                                    ui::window_background);
	_view_window->set_reactor(_view);

	_list_window = window->create_child(L"LIST_FRAME",
	                                    pf::window_style::child | pf::window_style::visible |
	                                    pf::window_style::clip_children,
	                                    ui::window_background);
	_list_window->set_reactor(_list);


	// TODO: _find_window creation

	// Restore font sizes from config
	const auto text_size = pf::config_read(L"Font", L"TextSize");
	const auto list_size = pf::config_read(L"Font", L"ListSize");

	if (!text_size.empty() && !list_size.empty())
	{
		try
		{
			const auto lh = std::stoi(list_size);
			const auto th = std::stoi(text_size);

			_state.initialize_styles(lh, th);
		}
		catch (...)
		{
		}
	}

	_state.invalidate(invalid::view);
	update_title();

	// Restore window placement from config
	if (_has_startup_placement)
	{
		_window->set_placement(_startup_placement);
	}

	// Determine root folder: startup folder from config or cwd
	auto root = _startup_folder;
	auto doc_path = _startup_document;

	if (root.empty())
		root = pf::current_directory();

	pf::debug_trace(L"on_create: root='" + root + L"'\n");
	if (!root.empty())
	{
		_state.refresh_index(file_path{root}, [this, doc_path]
		{
			invalidate(invalid::populate_folder_list);

			if (!doc_path.empty())
				load_doc(file_path{doc_path});
		});
	}

	return 0;
}

void app_frame::on_paint(pf::window_frame_ptr& window, pf::draw_context& dc)
{
	const auto bounds = window->get_client_rect();
	if (is_overlay(_state.get_mode()))
	{
		dc.fill_solid_rect(bounds, ui::main_wnd_clr);
		return;
	}

	auto c = ui::handle_color;
	if (_is_hover_splitter)
		c = ui::handle_hover_color;
	if (_is_tracking_splitter)
		c = ui::handle_tracking_color;

	const auto split_pos = static_cast<int>(bounds.left + (bounds.right - bounds.left) * _split_ratio);

	const irect splitter_rect = {
		split_pos - splitter_bar_width, bounds.top,
		split_pos + splitter_bar_width, bounds.bottom
	};

	dc.fill_solid_rect(splitter_rect, c);
}

void app_frame::layout_views() const
{
	if (!_window)
		return;

	const auto is_list_visible = _list_window && _list_window->is_visible();
	const auto is_panel_visible = !is_overlay(_state.get_mode());
	const auto bounds = _window->get_client_rect();
	const auto split_pos = is_panel_visible
		                       ? static_cast<int>(bounds.left + (bounds.right - bounds.left) * _split_ratio)
		                       : bounds.left;

	auto text_bounds = bounds;
	text_bounds.left = is_panel_visible ? split_pos + splitter_bar_width : bounds.left;
	_view_window->move_window(text_bounds);

	auto panel_bounds = bounds;
	panel_bounds.right = split_pos - splitter_bar_width;

	if (is_list_visible)
	{
		_list_window->move_window(panel_bounds);
	}
}

uint32_t app_frame::on_about()
{
	const auto item = _state.create_overlay(make_about_text(), app_state::about_path);
	set_active_item(item);
	return 0;
}

uint32_t app_frame::on_run_tests()
{
	const auto item = _state.create_overlay(run_all_tests(), app_state::test_results_path);
	set_active_item(item);
	return 0;
}

void app_frame::on_idle()
{
	const auto invalids = _state.validate();

	if (invalids & invalid::title)
	{
		update_title();
	}

	if (invalids & invalid::layout)
	{
		_view->layout();
	}

	if (invalids & invalid::caret)
	{
		_view->update_caret(_view_window);
	}

	if (invalids & invalid::horz_scrollbar)
	{
		_view->recalc_horz_scrollbar();
	}

	if (invalids & invalid::vert_scrollbar)
	{
		_view->recalc_vert_scrollbar();
	}

	if (invalids & invalid::populate_folder_list)
	{
		_list->populate();
		_list_window->invalidate();
	}

	if (invalids & invalid::folder_list)
	{
		_list->layout_list();
		_list_window->invalidate();
	}

	if (invalids & invalid::search_list)
	{
		_search->layout_list();
		_list_window->invalidate();
	}

	if (invalids & invalid::invalidate)
	{
		_view->invalidate(_view_window);
	}
}

static std::shared_ptr<app_frame> g_main_app;

bool app_init(const pf::window_frame_ptr& main_frame,
              const std::span<const std::wstring_view> params)
{
	std::wstring_view file_to_open;

	for (const auto& param : params)
	{
		if (str::icmp(param, L"/test") == 0 || str::icmp(param, L"--test") == 0)
		{
			const auto results = run_all_tests();
			const auto utf8 = str::utf16_to_utf8(results);
			fwrite(utf8.c_str(), 1, utf8.size(), stdout);
			return false;
		}
		if (!param.starts_with(L'/') && !param.starts_with(L'-'))
		{
			file_to_open = param;
		}
	}

	g_main_app = std::make_shared<app_frame>();

	// Create the main window via platform
	main_frame->set_reactor(g_main_app);

	using cid = command_id;
	namespace pk = pf::platform_key;
	namespace km = pf::key_mod;

	// Set up the menu with keyboard accelerators
	std::vector<pf::menu_command> menu = {
		{
			L"&File", 0, nullptr, nullptr, nullptr, {
				{
					L"&New\tCtrl+N", static_cast<int>(cid::file_new), [&]
					{
						g_main_app->on_new();
					},
					nullptr,
					nullptr,
					{'N', km::ctrl}
				},
				{
					L"&Open...\tCtrl+O", static_cast<int>(cid::file_open), [&]
					{
						g_main_app->on_open();
					},
					nullptr,
					nullptr,
					{'O', km::ctrl}
				},
				{
					L"&Save\tCtrl+S", static_cast<int>(cid::file_save), [&]
					{
						g_main_app->on_save();
					},
					nullptr,
					nullptr,
					{'S', km::ctrl}
				},
				{
					L"Save &As...", static_cast<int>(cid::file_save_as), [&]
					{
						g_main_app->on_save_as();
					}
				},
				{
					L"Save A&ll\tCtrl+Shift+S", static_cast<int>(cid::file_save_all), [&]
					{
						g_main_app->on_save_all();
					},
					nullptr,
					nullptr,
					{'S', km::ctrl | km::shift}
				},
				{},
				{
					L"E&xit", static_cast<int>(cid::app_exit), [&]
					{
						g_main_app->on_exit();
					}
				},
			}
		},
		{
			L"&Edit", 0, nullptr, nullptr, nullptr, {
				{
					L"&Undo\tCtrl+Z", static_cast<int>(cid::edit_undo), [&]
					{
						g_main_app->doc()->edit_undo();
					},
					[&]
					{
						return g_main_app->doc()->can_undo();
					},
					nullptr,
					{'Z', km::ctrl}
				},
				{
					L"&Redo\tCtrl+Y", static_cast<int>(cid::edit_redo), [&]
					{
						g_main_app->doc()->edit_redo();
					},
					[&]
					{
						return g_main_app->doc()->can_redo();
					},
					nullptr,
					{'Y', km::ctrl}
				},
				{},
				{
					L"Cu&t\tCtrl+X", static_cast<int>(cid::edit_cut), [&]
					{
						g_main_app->set_clipboard(g_main_app->doc()->edit_cut());
					},
					[&]
					{
						return g_main_app->doc()->has_selection();
					},
					nullptr,
					{'X', km::ctrl}
				},
				{
					L"&Copy\tCtrl+C", static_cast<int>(cid::edit_copy), [&]
					{
						g_main_app->set_clipboard(g_main_app->doc()->copy());
					},
					[&]
					{
						return g_main_app->doc()->has_selection();
					},
					nullptr,
					{'C', km::ctrl}
				},
				{
					L"&Paste\tCtrl+V", static_cast<int>(cid::edit_paste), [&]
					{
						g_main_app->doc()->edit_paste(g_main_app->clipboard_text());
					},
					[&]
					{
						return document::can_paste();
					},
					nullptr,
					{'V', km::ctrl}
				},
				{
					L"&Delete\tDel", static_cast<int>(cid::edit_delete), [&]
					{
						g_main_app->doc()->edit_delete();
					},
					[&]
					{
						return g_main_app->doc()->has_selection();
					},
					nullptr,
					{pk::Delete, km::none}
				},
				{},
				{
					L"Search in &Files\tCtrl+Shift+F", static_cast<int>(cid::edit_search_files), [&]
					{
						g_main_app->toggle_search_mode();
					},
					nullptr,
					nullptr,
					{'F', km::ctrl | km::shift}
				},
				{
					L"Select &All\tCtrl+A", static_cast<int>(cid::edit_select_all), [&]
					{
						g_main_app->doc()->select(g_main_app->doc()->all());
					},
					nullptr,
					nullptr,
					{'A', km::ctrl}
				},
				{},
				{
					L"&Reformat\tCtrl+R", static_cast<int>(cid::edit_reformat), [&]
					{
						g_main_app->on_edit_reformat();
					},
					nullptr,
					nullptr,
					{'R', km::ctrl}
				},
				{
					L"Sort && Remove Duplicates", static_cast<int>(cid::edit_sort_remove_duplicates), [&]
					{
						g_main_app->on_edit_remove_duplicates();
					}
				},
				{},
				{
					L"&Spell Check\tCtrl+Shift+P", static_cast<int>(cid::edit_spell_check), [&]
					{
						g_main_app->doc()->toggle_spell_check();
					},
					nullptr,
					[&]
					{
						return g_main_app->doc()->spell_check();
					},
					{'P', km::ctrl | km::shift}
				},
			}
		},
		{
			L"&View", 0, nullptr, nullptr, nullptr, {
				{
					L"&Word Wrap\tAlt+Z", static_cast<int>(cid::view_word_wrap), [&]
					{
						g_main_app->_view->toggle_word_wrap();
					},
					nullptr,
					[&]
					{
						return g_main_app->_view->word_wrap();
					},
					{'Z', km::alt}
				},
				{
					L"&Markdown Preview\tCtrl+M", static_cast<int>(cid::view_toggle_markdown), [&]
					{
						g_main_app->toggle_markdown_view();
					},
					nullptr,
					[&]
					{
						return is_markdown(g_main_app->_state.get_mode());
					},
					{'M', km::ctrl}
				},
				{},
				{
					L"&Refresh\tF5", static_cast<int>(cid::view_refresh_folder), [&]
					{
						g_main_app->on_refresh();
					},
					nullptr,
					nullptr,
					{pk::F5, km::none}
				},
				{},
				{
					L"&Next Result\tF8", static_cast<int>(cid::view_next_result), [&]
					{
						g_main_app->on_navigate_next(true);
					},
					nullptr,
					nullptr,
					{pk::F8, km::none}
				},
				{
					L"&Previous Result\tShift+F8", static_cast<int>(cid::view_prev_result), [&]
					{
						g_main_app->on_navigate_next(false);
					},
					nullptr,
					nullptr,
					{pk::F8, km::shift}
				},
			}
		},
		{
			L"&Help", 0, nullptr, nullptr, nullptr, {
				{
					L"Run &Tests\tCtrl+T", static_cast<int>(cid::help_run_tests), [&]
					{
						g_main_app->on_run_tests();
					},
					nullptr,
					nullptr,
					{'T', km::ctrl}
				},
				{
					L"&About\tF1", static_cast<int>(cid::app_about), [&]
					{
						g_main_app->on_about();
					},
					nullptr,
					nullptr,
					{pk::F1, km::none}
				},
			}
		},
	};

	main_frame->set_menu(std::move(menu));

	if (!file_to_open.empty())
	{
		g_main_app->_startup_document = file_to_open;
	}
	else
	{
		// Restore last session from config
		const auto folder = pf::config_read(L"Recent", L"Folder");
		const auto document = pf::config_read(L"Recent", L"Document");

		if (!folder.empty())
			g_main_app->_startup_folder = folder;
		if (!document.empty())
			g_main_app->_startup_document = document;
	}

	// Restore window placement from config
	const auto wl = pf::config_read(L"Window", L"Left");
	const auto wt = pf::config_read(L"Window", L"Top");
	const auto wr = pf::config_read(L"Window", L"Right");
	const auto wb = pf::config_read(L"Window", L"Bottom");

	if (!wl.empty() && !wt.empty() && !wr.empty() && !wb.empty())
	{
		try
		{
			g_main_app->_startup_placement.normal_bounds = {std::stoi(wl), std::stoi(wt), std::stoi(wr), std::stoi(wb)};
			g_main_app->_startup_placement.maximized = pf::config_read(L"Window", L"Maximized") == L"1";
			g_main_app->_has_startup_placement = true;
		}
		catch (const std::exception&)
		{
			// Ignore corrupted config values
		}
	}

	return true;
}

void app_idle()
{
	if (g_main_app)
		g_main_app->on_idle();
}

void app_destroy()
{
	g_main_app.reset();
}
