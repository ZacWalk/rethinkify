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
                                              const std::function<void()> action_override,
                                              std::function<bool()> is_enabled_override,
                                              std::function<bool()> is_checked_override,
                                              std::string text_override) const
{
	const auto* def = get_commands().find_by_menu_id(static_cast<int>(id));
	if (!def)
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
				menu_def->execute();
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
			return true; // consume keystroke even when disabled
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
			"Create a new document",
			"&New", static_cast<int>(command_id::file_new), {'N', pf::key_mod::ctrl},
			nullptr, nullptr,
			bind_command_handler(*this, app_command_handler::file_new)
		},
		{
			"Open a file",
			"&Open...", static_cast<int>(command_id::file_open), {'O', pf::key_mod::ctrl},
			nullptr, nullptr,
			bind_command_handler(*this, app_command_handler::file_open)
		},
		{
			"Save the current file",
			"&Save", static_cast<int>(command_id::file_save), {'S', pf::key_mod::ctrl},
			nullptr, nullptr,
			bind_command_handler(*this, app_command_handler::file_save)
		},
		{
			"Save the current file as...",
			"Save &As...", static_cast<int>(command_id::file_save_as), {},
			nullptr, nullptr,
			bind_command_handler(*this, app_command_handler::file_save_as)
		},
		{
			"Save all modified files",
			"Save A&ll", static_cast<int>(command_id::file_save_all),
			{'S', pf::key_mod::ctrl | pf::key_mod::shift},
			nullptr, nullptr,
			bind_command_handler(*this, app_command_handler::file_save_all)
		},
		{
			"Exit the application",
			"E&xit", static_cast<int>(command_id::app_exit), {},
			nullptr, nullptr,
			bind_command_handler(*this, app_command_handler::app_exit)
		},

		// ── Edit ───────────────────────────────────────────────────────
		{
			"Undo the last edit",
			"&Undo", static_cast<int>(command_id::edit_undo), {'Z', pf::key_mod::ctrl},
			[this] { return doc()->can_undo(); }, nullptr,
			bind_command_handler(*this, app_command_handler::edit_undo)
		},
		{
			"Redo the last undone edit",
			"&Redo", static_cast<int>(command_id::edit_redo), {'Y', pf::key_mod::ctrl},
			[this] { return doc()->can_redo(); }, nullptr,
			bind_command_handler(*this, app_command_handler::edit_redo)
		},
		{
			"Cut selection to clipboard",
			"Cu&t", static_cast<int>(command_id::edit_cut), {'X', pf::key_mod::ctrl},
			[this]
			{
				const auto view = focused_text_view();
				return view && view->can_cut_text();
			},
			nullptr,
			bind_command_handler(*this, app_command_handler::edit_cut)
		},
		{
			"Copy selection or selected path to clipboard",
			"&Copy", static_cast<int>(command_id::edit_copy), {'C', pf::key_mod::ctrl},
			[this]
			{
				return can_copy_current_focus();
			},
			nullptr,
			bind_command_handler(*this, app_command_handler::edit_copy)
		},
		{
			"Paste from clipboard",
			"&Paste", static_cast<int>(command_id::edit_paste), {'V', pf::key_mod::ctrl},
			[this]
			{
				const auto view = focused_text_view();
				return view && view->can_paste_text();
			},
			nullptr,
			bind_command_handler(*this, app_command_handler::edit_paste)
		},
		{
			"Delete selection or selected file",
			"&Delete", static_cast<int>(command_id::edit_delete), {pf::platform_key::Delete, pf::key_mod::none},
			[this]
			{
				return can_delete_current_focus();
			},
			nullptr,
			bind_command_handler(*this, app_command_handler::edit_delete)
		},
		{
			"Search in files",
			"Search in &Files", static_cast<int>(command_id::edit_search_files),
			{'F', pf::key_mod::ctrl | pf::key_mod::shift},
			nullptr, nullptr,
			bind_command_handler(*this, app_command_handler::edit_search_files)
		},
		{
			"Select all text",
			"Select &All", static_cast<int>(command_id::edit_select_all), {'A', pf::key_mod::ctrl},
			nullptr, nullptr,
			bind_command_handler(*this, app_command_handler::edit_select_all)
		},
		{
			"Reformat JSON document",
			"&Reformat", static_cast<int>(command_id::edit_reformat), {'R', pf::key_mod::ctrl},
			nullptr, nullptr,
			bind_command_handler(*this, app_command_handler::edit_reformat)
		},
		{
			"Sort lines and remove duplicates",
			"Sort && Remove Duplicates", static_cast<int>(command_id::edit_sort_remove_duplicates), {},
			nullptr, nullptr,
			bind_command_handler(*this, app_command_handler::edit_sort_remove_duplicates)
		},
		{
			"Calculate selected expression",
			"&Calculate Selection", static_cast<int>(command_id::edit_calc_selection),
			{'E', pf::key_mod::ctrl},
			[this]
			{
				return doc()->has_selection();
			},
			nullptr,
			bind_command_handler(*this, app_command_handler::edit_calc_selection)
		},
		{
			"Toggle spell check",
			"&Spell Check", static_cast<int>(command_id::edit_spell_check),
			{'P', pf::key_mod::ctrl | pf::key_mod::shift},
			nullptr, [this] { return doc()->spell_check(); },
			bind_command_handler(*this, app_command_handler::edit_spell_check)
		},

		// ── View ───────────────────────────────────────────────────────
		{
			"Toggle word wrap",
			"&Word Wrap", static_cast<int>(command_id::view_word_wrap), {'Z', pf::key_mod::alt},
			nullptr, [this] { return _doc_view->word_wrap(); },
			bind_command_handler(*this, app_command_handler::view_word_wrap)
		},
		{
			"Toggle markdown preview",
			"&Markdown Preview", static_cast<int>(command_id::view_toggle_markdown), {'M', pf::key_mod::ctrl},
			nullptr, [this] { return is_markdown(get_mode()); },
			bind_command_handler(*this, app_command_handler::view_toggle_markdown)
		},
		{
			"Refresh folder index",
			"&Refresh", static_cast<int>(command_id::view_refresh_folder), {pf::platform_key::F5, pf::key_mod::none},
			nullptr, nullptr,
			bind_command_handler(*this, app_command_handler::view_refresh_folder)
		},
		{
			"Navigate to next search result",
			"&Next Result", static_cast<int>(command_id::view_next_result), {pf::platform_key::F8, pf::key_mod::none},
			nullptr, nullptr,
			bind_command_handler(*this, app_command_handler::view_next_result)
		},
		{
			"Navigate to previous search result",
			"&Previous Result", static_cast<int>(command_id::view_prev_result),
			{pf::platform_key::F8, pf::key_mod::shift},
			nullptr, nullptr,
			bind_command_handler(*this, app_command_handler::view_prev_result)
		},

		// ── Help ───────────────────────────────────────────────────────
		{
			"Run all tests",
			"Run &Tests", static_cast<int>(command_id::help_run_tests), {'T', pf::key_mod::ctrl},
			nullptr, nullptr,
			bind_command_handler(*this, app_command_handler::help_run_tests)
		},
		{
			"Show about / help",
			"&About", static_cast<int>(command_id::app_about), {pf::platform_key::F1, pf::key_mod::none},
			nullptr, nullptr,
			bind_command_handler(*this, app_command_handler::app_about)
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
			"&File", 0, nullptr, nullptr, nullptr, {
				command_menu_item(cid::file_new),
				command_menu_item(cid::file_open),
				{"Open Recent &Root Folder", 0, nullptr, nullptr, nullptr, std::move(recent_roots)},
				sep(),
				command_menu_item(cid::file_save),
				command_menu_item(cid::file_save_as),
				command_menu_item(cid::file_save_all),
				sep(),
				command_menu_item(cid::app_exit),
			}
		},
		{
			"&Edit", 0, nullptr, nullptr, nullptr, {
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
				command_menu_item(cid::edit_calc_selection),
				sep(),
				command_menu_item(cid::edit_spell_check),
			}
		},
		{
			"&View", 0, nullptr, nullptr, nullptr, {
				command_menu_item(cid::view_word_wrap),
				command_menu_item(cid::view_toggle_markdown),
				sep(),
				command_menu_item(cid::view_refresh_folder),
				sep(),
				command_menu_item(cid::view_next_result),
				command_menu_item(cid::view_prev_result),
			}
		},
		{
			"&Help", 0, nullptr, nullptr, nullptr, {
				command_menu_item(cid::help_run_tests),
				command_menu_item(cid::app_about),
			}
		},
	};
}
