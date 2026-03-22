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
#include "view_console.h"

#include "app_frame.h"

auto g_app_name = L"Rethinkify";

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

static std::wstring make_about_text(const commands& cmds)
{
	std::wstring text =
		L"# Rethinkify\n"
		L"\n"
		L"*A lightweight text editor written in C++ by Zac Walker*\n"
		L"\n"
		L"## Keyboard Shortcuts\n"
		L"\n";

	for (const auto& cmd : cmds.defs())
	{
		if (cmd.accel.empty())
			continue;

		const auto key_text = pf::format_key_binding(cmd.accel);
		text += std::format(L"- **{}** {}\n", key_text, cmd.description);
	}

	text +=
		L"\n"
		L"*Hold Shift with navigation keys to extend selection.*\n";

	return text;
}

static void build_tree(std::wstring& out, const index_item_ptr& item,
                       const std::wstring& prefix, const bool is_last)
{
	out += prefix;
	out += is_last ? L"\x2514\x2500\x2500 " : L"\x251C\x2500\x2500 ";
	out += item->name;
	out += L'\n';

	const auto child_prefix = prefix + (is_last ? L"    " : L"\x2502   ");

	for (size_t i = 0; i < item->children.size(); ++i)
		build_tree(out, item->children[i], child_prefix, i == item->children.size() - 1);
}

std::vector<command_def> app_frame::make_commands()
{
	std::vector<command_def> defs = {
		// ── File ───────────────────────────────────────────────────────
		{
			{L"n", L"new"}, L"Create a new document",
			L"&New", static_cast<int>(command_id::file_new), {'N', pf::key_mod::ctrl},
			nullptr, nullptr,
			[this](const std::vector<std::wstring>&)
			{
				on_new();
				return command_result{L"New document created.", true};
			}
		},
		{
			{L"o", L"open"}, L"Open a file",
			L"&Open...", static_cast<int>(command_id::file_open), {'O', pf::key_mod::ctrl},
			nullptr, nullptr,
			[this](const std::vector<std::wstring>&)
			{
				on_open();
				return command_result{L"Open dialog shown.", true};
			}
		},
		{
			{L"s", L"save"}, L"Save the current file",
			L"&Save", static_cast<int>(command_id::file_save), {'S', pf::key_mod::ctrl},
			nullptr, nullptr,
			[this](const std::vector<std::wstring>&)
			{
				on_save();
				return command_result{L"File saved.", true};
			}
		},
		{
			{L"sa", L"saveas"}, L"Save the current file as...",
			L"Save &As...", static_cast<int>(command_id::file_save_as), {},
			nullptr, nullptr,
			[this](const std::vector<std::wstring>&)
			{
				on_save_as();
				return command_result{L"Save As dialog shown.", true};
			}
		},
		{
			{L"ss", L"saveall"}, L"Save all modified files",
			L"Save A&ll", static_cast<int>(command_id::file_save_all),
			{'S', pf::key_mod::ctrl | pf::key_mod::shift},
			nullptr, nullptr,
			[this](const std::vector<std::wstring>&)
			{
				save_all();
				return command_result{L"All files saved.", true};
			}
		},
		{
			{L"q", L"exit"}, L"Exit the application",
			L"E&xit", static_cast<int>(command_id::app_exit), {},
			nullptr, nullptr,
			[this](const std::vector<std::wstring>&)
			{
				on_exit();
				return command_result{L"", true};
			}
		},

		// ── Edit ───────────────────────────────────────────────────────
		{
			{L"u", L"undo"}, L"Undo the last edit",
			L"&Undo", static_cast<int>(command_id::edit_undo), {'Z', pf::key_mod::ctrl},
			[this] { return doc()->can_undo(); }, nullptr,
			[this](const std::vector<std::wstring>&)
			{
				if (!doc()->can_undo())
					return command_result{L"Nothing to undo.", false};
				doc()->edit_undo();
				return command_result{L"Undone.", true};
			}
		},
		{
			{L"y", L"redo"}, L"Redo the last undone edit",
			L"&Redo", static_cast<int>(command_id::edit_redo), {'Y', pf::key_mod::ctrl},
			[this] { return doc()->can_redo(); }, nullptr,
			[this](const std::vector<std::wstring>&)
			{
				if (!doc()->can_redo())
					return command_result{L"Nothing to redo.", false};
				doc()->edit_redo();
				return command_result{L"Redone.", true};
			}
		},
		{
			{L"x", L"cut"}, L"Cut selection to clipboard",
			L"Cu&t", static_cast<int>(command_id::edit_cut), {'X', pf::key_mod::ctrl},
			[this] { return doc()->has_selection(); }, nullptr,
			[this](const std::vector<std::wstring>&)
			{
				if (!doc()->has_selection())
					return command_result{L"No selection to cut.", false};
				pf::platform_text_to_clipboard(doc()->edit_cut());
				return command_result{L"Selection cut.", true};
			}
		},
		{
			{L"c", L"copy"}, L"Copy selection to clipboard",
			L"&Copy", static_cast<int>(command_id::edit_copy), {'C', pf::key_mod::ctrl},
			[this] { return doc()->has_selection(); }, nullptr,
			[this](const std::vector<std::wstring>&)
			{
				if (!doc()->has_selection())
					return command_result{L"No selection to copy.", false};
				pf::platform_text_to_clipboard(doc()->copy());
				return command_result{L"Selection copied.", true};
			}
		},
		{
			{L"v", L"paste"}, L"Paste from clipboard",
			L"&Paste", static_cast<int>(command_id::edit_paste), {'V', pf::key_mod::ctrl},
			[] { return document::can_paste(); }, nullptr,
			[this](const std::vector<std::wstring>&)
			{
				doc()->edit_paste(pf::platform_text_from_clipboard());
				return command_result{L"Pasted.", true};
			}
		},
		{
			{L"d", L"delete"}, L"Delete selection",
			L"&Delete", static_cast<int>(command_id::edit_delete), {pf::platform_key::Delete, pf::key_mod::none},
			[this] { return doc()->has_selection(); }, nullptr,
			[this](const std::vector<std::wstring>&)
			{
				if (!doc()->has_selection())
					return command_result{L"No selection to delete.", false};
				doc()->edit_delete();
				return command_result{L"Selection deleted.", true};
			}
		},
		{
			{L"f", L"find"}, L"Search in files: find <text>",
			L"Search in &Files", static_cast<int>(command_id::edit_search_files),
			{'F', pf::key_mod::ctrl | pf::key_mod::shift},
			nullptr, nullptr,
			[this](const std::vector<std::wstring>& args)
			{
				if (args.empty())
				{
					toggle_search_mode();
					return command_result{L"Search mode toggled.", true};
				}
				std::wstring text;
				for (size_t i = 0; i < args.size(); ++i)
				{
					if (i > 0) text += L' ';
					text += args[i];
				}
				if (!is_search(_state.get_mode()))
					toggle_search_mode();
				_state.execute_search_async(text, [this]() { _search->populate(); });
				return command_result{std::format(L"Searching for '{}'...", text), true};
			}
		},
		{
			{L"a", L"selectall"}, L"Select all text",
			L"Select &All", static_cast<int>(command_id::edit_select_all), {'A', pf::key_mod::ctrl},
			nullptr, nullptr,
			[this](const std::vector<std::wstring>&)
			{
				doc()->select(doc()->all());
				return command_result{L"All text selected.", true};
			}
		},
		{
			{L"rf", L"reformat"}, L"Reformat JSON document",
			L"&Reformat", static_cast<int>(command_id::edit_reformat), {'R', pf::key_mod::ctrl},
			nullptr, nullptr,
			[this](const std::vector<std::wstring>&)
			{
				on_edit_reformat();
				return command_result{L"Document reformatted.", true};
			}
		},
		{
			{L"sd", L"sort"}, L"Sort lines and remove duplicates",
			L"Sort && Remove Duplicates", static_cast<int>(command_id::edit_sort_remove_duplicates), {},
			nullptr, nullptr,
			[this](const std::vector<std::wstring>&)
			{
				on_edit_remove_duplicates();
				return command_result{L"Sorted and removed duplicates.", true};
			}
		},
		{
			{L"sp", L"spellcheck"}, L"Toggle spell check",
			L"&Spell Check", static_cast<int>(command_id::edit_spell_check),
			{'P', pf::key_mod::ctrl | pf::key_mod::shift},
			nullptr, [this] { return doc()->spell_check(); },
			[this](const std::vector<std::wstring>&)
			{
				doc()->toggle_spell_check();
				const auto on = doc()->spell_check();
				return command_result{on ? L"Spell check enabled." : L"Spell check disabled.", true};
			}
		},

		// ── View ───────────────────────────────────────────────────────
		{
			{L"ww", L"wordwrap"}, L"Toggle word wrap",
			L"&Word Wrap", static_cast<int>(command_id::view_word_wrap), {'Z', pf::key_mod::alt},
			nullptr, [this] { return _view->word_wrap(); },
			[this](const std::vector<std::wstring>&)
			{
				_view->toggle_word_wrap();
				const auto on = _view->word_wrap();
				return command_result{on ? L"Word wrap enabled." : L"Word wrap disabled.", true};
			}
		},
		{
			{L"md", L"markdown"}, L"Toggle markdown preview",
			L"&Markdown Preview", static_cast<int>(command_id::view_toggle_markdown), {'M', pf::key_mod::ctrl},
			nullptr, [this] { return is_markdown(_state.get_mode()); },
			[this](const std::vector<std::wstring>&)
			{
				toggle_markdown_view();
				const auto on = is_markdown(_state.get_mode());
				return command_result{on ? L"Markdown preview on." : L"Markdown preview off.", true};
			}
		},
		{
			{L"r", L"refresh"}, L"Refresh folder index",
			L"&Refresh", static_cast<int>(command_id::view_refresh_folder), {pf::platform_key::F5, pf::key_mod::none},
			nullptr, nullptr,
			[this](const std::vector<std::wstring>&)
			{
				on_refresh();
				return command_result{L"Folder refreshed.", true};
			}
		},
		{
			{L"fn", L"nextresult"}, L"Navigate to next search result",
			L"&Next Result", static_cast<int>(command_id::view_next_result), {pf::platform_key::F8, pf::key_mod::none},
			nullptr, nullptr,
			[this](const std::vector<std::wstring>&)
			{
				on_navigate_next(true);
				return command_result{L"", true};
			}
		},
		{
			{L"fp", L"prevresult"}, L"Navigate to previous search result",
			L"&Previous Result", static_cast<int>(command_id::view_prev_result),
			{pf::platform_key::F8, pf::key_mod::shift},
			nullptr, nullptr,
			[this](const std::vector<std::wstring>&)
			{
				on_navigate_next(false);
				return command_result{L"", true};
			}
		},

		// ── Help ───────────────────────────────────────────────────────
		{
			{L"t", L"test"}, L"Run all tests",
			L"Run &Tests", static_cast<int>(command_id::help_run_tests), {'T', pf::key_mod::ctrl},
			nullptr, nullptr,
			[this](const std::vector<std::wstring>&)
			{
				on_run_tests();
				return command_result{L"Tests complete.", true};
			}
		},
		{
			{L"ab", L"about"}, L"Show about / help overlay",
			L"&About", static_cast<int>(command_id::app_about), {pf::platform_key::F1, pf::key_mod::none},
			nullptr, nullptr,
			[this](const std::vector<std::wstring>&)
			{
				on_about();
				return command_result{L"", true};
			}
		},

		// ── Console-only commands ──────────────────────────────────────
		{
			{L"?", L"h", L"help"}, L"List available commands",
			{}, 0, {},
			nullptr, nullptr,
			[this](const std::vector<std::wstring>&)
			{
				return command_result{_state.get_commands().help_text(), true};
			}
		},
		{
			{L"ls", L"dir", L"tree"}, L"List folder contents as a tree",
			{}, 0, {},
			nullptr, nullptr,
			[this](const std::vector<std::wstring>&)
			{
				const auto root = _state.root_folder();
				if (!root || root->children.empty())
					return command_result{L"No folder open.", false};

				std::wstring out = root->name + L"\n";
				for (size_t i = 0; i < root->children.size(); ++i)
					build_tree(out, root->children[i], L"", i == root->children.size() - 1);
				return command_result{out, true};
			}
		},
	};
	return defs;
}

app_frame::app_frame() : _state(*this),
                         _view(std::make_shared<text_edit_view>(*this)),
                         _list(std::make_shared<file_list_view>(*this)),
                         _search(std::make_shared<search_list_view>(*this)),
                         _console(std::make_shared<console_view>(*this, _state.get_commands()))
{
	_view->set_document(_state.active_item()->doc);
	_state.get_commands().set_commands(make_commands());
}

std::vector<pf::menu_command> app_frame::build_menu()
{
	using cid = command_id;

	auto& cmds = _state.get_commands();

	auto item = [&](const command_id id) -> pf::menu_command
	{
		const auto* def = cmds.find_by_menu_id(static_cast<int>(id));
		if (!def) return {};

		auto text = def->menu_text;
		if (!def->accel.empty())
			text += L'\t' + pf::format_key_binding(def->accel);

		auto fn = def->execute;
		return {
			std::move(text), def->menu_id,
			[fn]() { fn({}); },
			def->is_enabled, def->is_checked,
			def->accel
		};
	};

	auto sep = []() { return pf::menu_command{}; };

	return {
		{L"&File", 0, nullptr, nullptr, nullptr, {
			item(cid::file_new),
			item(cid::file_open),
			item(cid::file_save),
			item(cid::file_save_as),
			item(cid::file_save_all),
			sep(),
			item(cid::app_exit),
		}},
		{L"&Edit", 0, nullptr, nullptr, nullptr, {
			item(cid::edit_undo),
			item(cid::edit_redo),
			sep(),
			item(cid::edit_cut),
			item(cid::edit_copy),
			item(cid::edit_paste),
			item(cid::edit_delete),
			sep(),
			item(cid::edit_search_files),
			item(cid::edit_select_all),
			sep(),
			item(cid::edit_reformat),
			item(cid::edit_sort_remove_duplicates),
			sep(),
			item(cid::edit_spell_check),
		}},
		{L"&View", 0, nullptr, nullptr, nullptr, {
			item(cid::view_word_wrap),
			item(cid::view_toggle_markdown),
			sep(),
			item(cid::view_refresh_folder),
			sep(),
			item(cid::view_next_result),
			item(cid::view_prev_result),
		}},
		{L"&Help", 0, nullptr, nullptr, nullptr, {
			item(cid::help_run_tests),
			item(cid::app_about),
		}},
	};
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
	else if (v == view_focus::console)
		_console_window->set_focus();
	else
		_view_window->set_focus();
}

void app_state::update_styles()
{
	_styles.list_font = {_styles.list_font_height, pf::font_name::calibri};
	_styles.edit_font = {(_styles.list_font_height * 3) / 2, pf::font_name::calibri};
	_styles.text_font = {_styles.text_font_height, pf::font_name::consolas};
	_styles.console_font = {_styles.console_font_height, pf::font_name::consolas};

	_styles.padding_x = static_cast<int>(5 * _styles.dpi_scale);
	_styles.padding_y = static_cast<int>(5 * _styles.dpi_scale);
	_styles.indent = static_cast<int>(16 * _styles.dpi_scale);
	_styles.edit_box_margin = static_cast<int>(6 * _styles.dpi_scale);
	_styles.edit_box_inner_pad = static_cast<int>(4 * _styles.dpi_scale);
	_styles.list_top_pad = static_cast<int>(4 * _styles.dpi_scale);
	_styles.list_scroll_pad = static_cast<int>(64 * _styles.dpi_scale);
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
		const auto pt = pf::point_from_lparam(lParam);
		const auto rect = window->get_client_rect();

		if (!is_overlay(_state.get_mode()) && _panel_splitter.begin_tracking(rect, pt, window))
		{
			layout_views();
		}
		else
		{
			const auto right_bounds = console_split_bounds();
			_console_splitter.begin_tracking(right_bounds, pt, window);
		}
	}

	if (msg == mt::mouse_leave)
	{
		_panel_splitter.clear_hover(window);
		_console_splitter.clear_hover(window);
	}

	if (msg == mt::mouse_move)
	{
		const auto pt = pf::point_from_lparam(lParam);
		const auto rect = window->get_client_rect();

		if (wParam == 0x0001 /*MK_LBUTTON*/)
		{
			if (_panel_splitter.track_to(rect, pt, window))
				layout_views();
			else
			{
				const auto right_bounds = console_split_bounds();
				if (_console_splitter.track_to(right_bounds, pt, window))
					layout_views();
			}
		}

		if (!is_overlay(_state.get_mode()))
			_panel_splitter.update_hover(rect, pt, window);

		const auto right_bounds = console_split_bounds();
		_console_splitter.update_hover(right_bounds, pt, window);
	}

	if (msg == mt::left_button_up)
	{
		_panel_splitter.end_tracking(window);
		_console_splitter.end_tracking(window);
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

	_console_window = window->create_child(L"CONSOLE_FRAME",
	                                       pf::window_style::child | pf::window_style::visible |
	                                       pf::window_style::clip_children,
	                                       ui::window_background);
	_console_window->set_reactor(_console);

	// Restore font sizes from config
	const auto text_size = pf::config_read(L"Font", L"TextSize");
	const auto list_size = pf::config_read(L"Font", L"ListSize");
	const auto console_size = pf::config_read(L"Font", L"ConsoleSize");

	if (!text_size.empty() && !list_size.empty())
	{
		try
		{
			const auto lh = std::stoi(list_size);
			const auto th = std::stoi(text_size);
			const auto ch = console_size.empty() ? 20 : std::stoi(console_size);

			_state.initialize_styles(lh, th, ch);
		}
		catch (...)
		{
		}
	}

	_state.invalidate(invalid::view);
	update_title();

	// Restore splitter positions from config
	const auto panel_ratio = pf::config_read(L"Splitter", L"PanelRatio");
	const auto console_ratio = pf::config_read(L"Splitter", L"ConsoleRatio");

	try
	{
		if (!panel_ratio.empty())
			_panel_splitter.ratio = std::clamp(std::stod(panel_ratio), splitter::min_ratio, splitter::max_ratio);
		if (!console_ratio.empty())
			_console_splitter.ratio = std::clamp(std::stod(console_ratio), splitter::min_ratio, splitter::max_ratio);
	}
	catch (...)
	{
	}

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

	_panel_splitter.draw(dc, bounds);

	const auto right_bounds = console_split_bounds();
	_console_splitter.draw(dc, right_bounds);
}

irect app_frame::console_split_bounds() const
{
	if (!_window) return {};

	const auto is_panel_visible = !is_overlay(_state.get_mode());
	const auto bounds = _window->get_client_rect();
	const auto panel_split = is_panel_visible
		                         ? _panel_splitter.split_pos(bounds)
		                         : bounds.left;
	const auto right_left = is_panel_visible ? panel_split + splitter::bar_width : bounds.left;

	return {right_left, bounds.top, bounds.right, bounds.bottom};
}

void app_frame::layout_views() const
{
	if (!_window)
		return;

	const auto is_list_visible = _list_window && _list_window->is_visible();
	const auto is_panel_visible = !is_overlay(_state.get_mode());
	const auto bounds = _window->get_client_rect();

	const auto right_bounds = console_split_bounds();
	const auto console_split = _console_splitter.split_pos(right_bounds);

	auto text_bounds = right_bounds;
	text_bounds.bottom = console_split - splitter::bar_width;
	_view_window->move_window(text_bounds);

	auto console_bounds = right_bounds;
	console_bounds.top = console_split + splitter::bar_width;
	_console_window->move_window(console_bounds);

	const auto panel_split = is_panel_visible
		                         ? _panel_splitter.split_pos(bounds)
		                         : bounds.left;

	auto panel_bounds = bounds;
	panel_bounds.right = panel_split - splitter::bar_width;

	if (is_list_visible)
	{
		_list_window->move_window(panel_bounds);
	}
}

uint32_t app_frame::on_about()
{
	const auto item = _state.create_overlay(make_about_text(_state.get_commands()), app_state::about_path);
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

	if (invalids & invalid::console)
	{
		_console_window->invalidate();
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
	main_frame->set_menu(g_main_app->build_menu());

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
