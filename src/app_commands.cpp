// app_commands.cpp — Command definitions and menu construction

#include "pch.h"

#include "app.h"
#include "document.h"
#include "commands.h"
#include "view_doc.h"
#include "app_state.h"

namespace
{
	bool key_binding_matches(const pf::window_frame_ptr& window, const unsigned int vk, const pf::key_binding& binding)
	{
		if (!window || binding.empty() || binding.key != vk)
			return false;

		const bool ctrl = window->is_key_down(pf::platform_key::Control);
		const bool shift = window->is_key_down(pf::platform_key::Shift);
		const bool alt = window->is_key_down(pf::platform_key::Alt);

		return ctrl == ((binding.modifiers & pf::key_mod::ctrl) != 0) &&
			shift == ((binding.modifiers & pf::key_mod::shift) != 0) &&
			alt == ((binding.modifiers & pf::key_mod::alt) != 0);
	}
}

pf::menu_command app_state::command_menu_item(const command_id id,
                                              std::function<void()> action_override,
                                              std::function<bool()> is_enabled_override,
                                              std::function<bool()> is_checked_override,
                                              std::u8string text_override) const
{
	const auto* def = get_commands().find_by_menu_id(static_cast<int>(id));
	if (!def || (def->availability & command_availability::ui) == 0)
		return {};

	auto is_enabled = def->is_enabled;
	if (is_enabled_override)
	{
		if (is_enabled)
		{
			auto base_enabled = std::move(is_enabled);
			is_enabled = [base_enabled = std::move(base_enabled), is_enabled_override]()
			{
				return base_enabled() && is_enabled_override();
			};
		}
		else
		{
			is_enabled = std::move(is_enabled_override);
		}
	}

	auto is_checked = is_checked_override ? std::move(is_checked_override) : def->is_checked;
	auto action = action_override;
	if (!action)
	{
		action = [this, menu_id = def->menu_id]()
		{
			if (const auto* menu_def = get_commands().find_by_menu_id(menu_id))
				menu_def->execute({});
		};
	}

	return {
		text_override.empty() ? def->menu_text : std::move(text_override),
		def->menu_id,
		std::move(action),
		std::move(is_enabled),
		std::move(is_checked),
		def->accel
	};
}

bool app_state::invoke_menu_accelerator(const pf::window_frame_ptr& window,
                                        const std::vector<pf::menu_command>& items,
                                        const unsigned int vk) const
{
	for (const auto& item : items)
	{
		if (!item.children.empty() && invoke_menu_accelerator(window, item.children, vk))
			return true;

		if (!item.children.empty() || !key_binding_matches(window, vk, item.accel))
			continue;
		if (item.is_enabled && !item.is_enabled())
			return false;
		if (item.action)
			item.action();
		return true;
	}

	return false;
}


std::vector<command_def> app_state::make_commands()
{
	std::vector<command_def> defs = {
		// ── File ───────────────────────────────────────────────────────
		{
			{u8"n", u8"new"}, u8"Create a new document",
			u8"&New", static_cast<int>(command_id::file_new), {'N', pf::key_mod::ctrl},
			nullptr, nullptr,
			bind_command_handler(*this, app_command_handler::file_new)
		},
		{
			{u8"o", u8"open"}, u8"Open a file",
			u8"&Open...", static_cast<int>(command_id::file_open), {'O', pf::key_mod::ctrl},
			nullptr, nullptr,
			bind_command_handler(*this, app_command_handler::file_open)
		},
		{
			{u8"s", u8"save"}, u8"Save the current file",
			u8"&Save", static_cast<int>(command_id::file_save), {'S', pf::key_mod::ctrl},
			nullptr, nullptr,
			bind_command_handler(*this, app_command_handler::file_save)
		},
		{
			{u8"sa", u8"saveas"}, u8"Save the current file as...",
			u8"Save &As...", static_cast<int>(command_id::file_save_as), {},
			nullptr, nullptr,
			bind_command_handler(*this, app_command_handler::file_save_as)
		},
		{
			{u8"ss", u8"saveall"}, u8"Save all modified files",
			u8"Save A&ll", static_cast<int>(command_id::file_save_all),
			{'S', pf::key_mod::ctrl | pf::key_mod::shift},
			nullptr, nullptr,
			bind_command_handler(*this, app_command_handler::file_save_all)
		},
		{
			{u8"ex", u8"exit"}, u8"Exit the application",
			u8"E&xit", static_cast<int>(command_id::app_exit), {},
			nullptr, nullptr,
			bind_command_handler(*this, app_command_handler::app_exit)
		},

		// ── Edit ───────────────────────────────────────────────────────
		{
			{u8"u", u8"undo"}, u8"Undo the last edit",
			u8"&Undo", static_cast<int>(command_id::edit_undo), {'Z', pf::key_mod::ctrl},
			[this] { return doc()->can_undo(); }, nullptr,
			bind_command_handler(*this, app_command_handler::edit_undo)
		},
		{
			{u8"y", u8"redo"}, u8"Redo the last undone edit",
			u8"&Redo", static_cast<int>(command_id::edit_redo), {'Y', pf::key_mod::ctrl},
			[this] { return doc()->can_redo(); }, nullptr,
			bind_command_handler(*this, app_command_handler::edit_redo)
		},
		{
			{u8"x", u8"cut"}, u8"Cut selection to clipboard",
			u8"Cu&t", static_cast<int>(command_id::edit_cut), {'X', pf::key_mod::ctrl},
			[this]
			{
				const auto view = focused_text_view();
				return view && view->can_cut_text();
			},
			nullptr,
			bind_command_handler(*this, app_command_handler::edit_cut)
		},
		{
			{u8"c", u8"copy"}, u8"Copy selection or selected path to clipboard",
			u8"&Copy", static_cast<int>(command_id::edit_copy), {'C', pf::key_mod::ctrl},
			[this]
			{
				return can_copy_current_focus();
			},
			nullptr,
			bind_command_handler(*this, app_command_handler::edit_copy)
		},
		{
			{u8"v", u8"paste"}, u8"Paste from clipboard",
			u8"&Paste", static_cast<int>(command_id::edit_paste), {'V', pf::key_mod::ctrl},
			[this]
			{
				const auto view = focused_text_view();
				return view && view->can_paste_text();
			},
			nullptr,
			bind_command_handler(*this, app_command_handler::edit_paste)
		},
		{
			{u8"d", u8"delete"}, u8"Delete selection or selected file",
			u8"&Delete", static_cast<int>(command_id::edit_delete), {pf::platform_key::Delete, pf::key_mod::none},
			[this]
			{
				return can_delete_current_focus();
			},
			nullptr,
			bind_command_handler(*this, app_command_handler::edit_delete)
		},
		{
			{u8"f", u8"find"}, u8"Search in files: find <text>",
			u8"Search in &Files", static_cast<int>(command_id::edit_search_files),
			{'F', pf::key_mod::ctrl | pf::key_mod::shift},
			nullptr, nullptr,
			bind_command_handler(*this, app_command_handler::edit_search_files)
		},
		{
			{u8"selectall"}, u8"Select all text",
			u8"Select &All", static_cast<int>(command_id::edit_select_all), {'A', pf::key_mod::ctrl},
			nullptr, nullptr,
			bind_command_handler(*this, app_command_handler::edit_select_all)
		},
		{
			{u8"a", u8"ai", u8"agent"}, u8"Ask Gemini to inspect or edit files in the current root: a \"question\"",
			{}, 0, {},
			nullptr, nullptr,
			bind_command_handler(*this, app_command_handler::agent),
			command_availability::console
		},
		{
			{u8"sum", u8"summarize"},
			u8"Summarize a markdown, text, or PDF document into markdown: summarize [path] [> file.md]",
			{}, 0, {},
			nullptr, nullptr,
			bind_command_handler(*this, app_command_handler::summarize),
			command_availability::console
		},
		{
			{u8"rf", u8"reformat"}, u8"Reformat JSON document",
			u8"&Reformat", static_cast<int>(command_id::edit_reformat), {'R', pf::key_mod::ctrl},
			nullptr, nullptr,
			bind_command_handler(*this, app_command_handler::edit_reformat)
		},
		{
			{u8"sd", u8"sort"}, u8"Sort lines and remove duplicates",
			u8"Sort && Remove Duplicates", static_cast<int>(command_id::edit_sort_remove_duplicates), {},
			nullptr, nullptr,
			bind_command_handler(*this, app_command_handler::edit_sort_remove_duplicates)
		},
		{
			{u8"sp", u8"spellcheck"}, u8"Toggle spell check",
			u8"&Spell Check", static_cast<int>(command_id::edit_spell_check),
			{'P', pf::key_mod::ctrl | pf::key_mod::shift},
			nullptr, [this] { return doc()->spell_check(); },
			bind_command_handler(*this, app_command_handler::edit_spell_check)
		},

		// ── View ───────────────────────────────────────────────────────
		{
			{u8"ww", u8"wordwrap"}, u8"Toggle word wrap",
			u8"&Word Wrap", static_cast<int>(command_id::view_word_wrap), {'Z', pf::key_mod::alt},
			nullptr, [this] { return _doc_view->word_wrap(); },
			bind_command_handler(*this, app_command_handler::view_word_wrap)
		},
		{
			{u8"md", u8"markdown"}, u8"Toggle markdown preview",
			u8"&Markdown Preview", static_cast<int>(command_id::view_toggle_markdown), {'M', pf::key_mod::ctrl},
			nullptr, [this] { return is_markdown(get_mode()); },
			bind_command_handler(*this, app_command_handler::view_toggle_markdown)
		},
		{
			{u8"chart"}, u8"Toggle chart view for CSV files",
			u8"&Chart View", static_cast<int>(command_id::view_toggle_chart), {'K', pf::key_mod::ctrl},
			[this] { return is_csv(get_mode()) || is_chart(get_mode()); },
			[this] { return is_chart(get_mode()); },
			bind_command_handler(*this, app_command_handler::view_toggle_chart)
		},
		{
			{u8"r", u8"refresh"}, u8"Refresh folder index",
			u8"&Refresh", static_cast<int>(command_id::view_refresh_folder), {pf::platform_key::F5, pf::key_mod::none},
			nullptr, nullptr,
			bind_command_handler(*this, app_command_handler::view_refresh_folder)
		},
		{
			{u8"fn", u8"nextresult"}, u8"Navigate to next search result",
			u8"&Next Result", static_cast<int>(command_id::view_next_result), {pf::platform_key::F8, pf::key_mod::none},
			nullptr, nullptr,
			bind_command_handler(*this, app_command_handler::view_next_result)
		},
		{
			{u8"fp", u8"prevresult"}, u8"Navigate to previous search result",
			u8"&Previous Result", static_cast<int>(command_id::view_prev_result),
			{pf::platform_key::F8, pf::key_mod::shift},
			nullptr, nullptr,
			bind_command_handler(*this, app_command_handler::view_prev_result)
		},

		// ── Help ───────────────────────────────────────────────────────
		{
			{u8"t", u8"test"}, u8"Run all tests",
			u8"Run &Tests", static_cast<int>(command_id::help_run_tests), {'T', pf::key_mod::ctrl},
			nullptr, nullptr,
			bind_command_handler(*this, app_command_handler::help_run_tests)
		},
		{
			{u8"ab", u8"about"}, u8"Show about / help",
			u8"&About", static_cast<int>(command_id::app_about), {pf::platform_key::F1, pf::key_mod::none},
			nullptr, nullptr,
			bind_command_handler(*this, app_command_handler::app_about)
		},

		// ── Console-only commands ──────────────────────────────────────
		{
			{u8"h", u8"help"}, u8"List available commands",
			{}, 0, {},
			nullptr, nullptr,
			bind_command_handler(*this, app_command_handler::help),
			command_availability::console | command_availability::agent
		},
		{
			{u8"ls", u8"dir", u8"tree"}, u8"List folder contents as a tree",
			{}, 0, {},
			nullptr, nullptr,
			bind_command_handler(*this, app_command_handler::list_tree),
			command_availability::console | command_availability::agent
		},
		{
			{u8"del", u8"rm", u8"delete"}, u8"Delete a file or folder in the current root: rm <path>",
			{}, 0, {},
			nullptr, nullptr,
			bind_command_handler(*this, app_command_handler::remove_path),
			command_availability::console | command_availability::agent
		},
		{
			{u8"cp", u8"copy"}, u8"Copy a file or folder within the current root: cp <source> <dest>",
			{}, 0, {},
			nullptr, nullptr,
			bind_command_handler(*this, app_command_handler::copy_path),
			command_availability::console | command_availability::agent
		},
		{
			{u8"mv", u8"move"}, u8"Move a file or folder within the current root: mv <source> <dest>",
			{}, 0, {},
			nullptr, nullptr,
			bind_command_handler(*this, app_command_handler::move_path),
			command_availability::console | command_availability::agent
		},
		{
			{u8"rename"}, u8"Rename within the same folder: rename <source> <new-name>",
			{}, 0, {},
			nullptr, nullptr,
			bind_command_handler(*this, app_command_handler::rename_path),
			command_availability::console | command_availability::agent
		},
		{
			{u8"q", u8"quote"}, u8"Stock quote: q <ticker>",
			{}, 0, {},
			nullptr, nullptr,
			bind_command_handler(*this, app_command_handler::quote),
			command_availability::console | command_availability::agent
		},
		{
			{u8"echo"}, u8"Echo text or write it to a file: echo \"text\" > file.md",
			{}, 0, {},
			nullptr, nullptr,
			bind_command_handler(*this, app_command_handler::echo),
			command_availability::console | command_availability::agent
		},
		{
			{u8"?", u8"calc"}, u8"Evaluate a simple math expression: calc 1 + 2 * (3 + 4)",
			{}, 0, {},
			nullptr, nullptr,
			bind_command_handler(*this, app_command_handler::calc),
			command_availability::console | command_availability::agent
		},
	};
	return defs;
}


std::vector<pf::menu_command> app_state::build_menu()
{
	using cid = command_id;

	auto sep = []() { return pf::menu_command{}; };
	auto recent_roots = build_recent_root_folder_menu();

	return {
		{
			u8"&File", 0, nullptr, nullptr, nullptr, {
				command_menu_item(cid::file_new),
				command_menu_item(cid::file_open),
				{u8"Open Recent &Root Folder", 0, nullptr, nullptr, nullptr, std::move(recent_roots)},
				sep(),
				command_menu_item(cid::file_save),
				command_menu_item(cid::file_save_as),
				command_menu_item(cid::file_save_all),
				sep(),
				command_menu_item(cid::app_exit),
			}
		},
		{
			u8"&Edit", 0, nullptr, nullptr, nullptr, {
				command_menu_item(cid::edit_undo),
				command_menu_item(cid::edit_redo),
				sep(),
				command_menu_item(cid::edit_cut),
				command_menu_item(cid::edit_copy),
				command_menu_item(cid::edit_paste),
				command_menu_item(cid::edit_delete),
				sep(),
				command_menu_item(cid::edit_search_files),
				command_menu_item(cid::edit_select_all),
				sep(),
				command_menu_item(cid::edit_reformat),
				command_menu_item(cid::edit_sort_remove_duplicates),
				sep(),
				command_menu_item(cid::edit_spell_check),
			}
		},
		{
			u8"&View", 0, nullptr, nullptr, nullptr, {
				command_menu_item(cid::view_word_wrap),
				command_menu_item(cid::view_toggle_markdown),
				command_menu_item(cid::view_toggle_chart),
				sep(),
				command_menu_item(cid::view_refresh_folder),
				sep(),
				command_menu_item(cid::view_next_result),
				command_menu_item(cid::view_prev_result),
			}
		},
		{
			u8"&Help", 0, nullptr, nullptr, nullptr, {
				command_menu_item(cid::help_run_tests),
				command_menu_item(cid::app_about),
			}
		},
	};
}
