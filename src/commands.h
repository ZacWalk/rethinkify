// commands.h — Unified command system: command definitions, parsing, registration, help text

#pragma once

#include "app.h"
#include "platform.h"

class app_state;

struct command_result
{
	std::u8string output;
	bool success = true;
};

namespace command_availability
{
	constexpr uint8_t none = 0;
	constexpr uint8_t ui = 1 << 0;
	constexpr uint8_t console = 1 << 1;
	constexpr uint8_t agent = 1 << 2;
	constexpr uint8_t all = ui | console | agent;
}

struct command_def
{
	// Console identification
	std::vector<std::u8string> names; // e.g. {"s", "save"} or {"?", "h", "help"}
	std::u8string description;
	uint8_t availability = command_availability::ui | command_availability::console;

	// Menu identification (empty menu_text = console-only command)
	std::u8string menu_text;
	int menu_id = 0;
	pf::key_binding accel;
	std::function<bool()> is_enabled;
	std::function<bool()> is_checked;

	// Execution
	std::function<command_result(const std::vector<std::u8string>&)> execute;

	command_def() = default;

	command_def(std::vector<std::u8string> in_names,
	            std::u8string in_description,
	            std::u8string in_menu_text,
	            const int in_menu_id,
	            const pf::key_binding in_accel,
	            std::function<bool()> in_is_enabled,
	            std::function<bool()> in_is_checked,
	            std::function<command_result(const std::vector<std::u8string>&)> in_execute,
	            const uint8_t in_availability = command_availability::ui | command_availability::console)
		: names(std::move(in_names)),
		  description(std::move(in_description)),
		  availability(in_availability),
		  menu_text(std::move(in_menu_text)),
		  menu_id(in_menu_id),
		  accel(in_accel),
		  is_enabled(std::move(in_is_enabled)),
		  is_checked(std::move(in_is_checked)),
		  execute(std::move(in_execute))
	{
	}
};

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
	view_toggle_chart,
	view_refresh_folder,
	view_next_result,
	view_prev_result,
};

enum class app_command_handler : uint8_t
{
	file_new,
	file_open,
	file_save,
	file_save_as,
	file_save_all,
	app_exit,
	edit_undo,
	edit_redo,
	edit_cut,
	edit_copy,
	edit_paste,
	edit_delete,
	edit_search_files,
	edit_select_all,
	agent,
	summarize,
	edit_reformat,
	edit_sort_remove_duplicates,
	edit_spell_check,
	view_word_wrap,
	view_toggle_markdown,
	view_toggle_chart,
	view_refresh_folder,
	view_next_result,
	view_prev_result,
	help_run_tests,
	app_about,
	help,
	list_tree,
	remove_path,
	copy_path,
	move_path,
	rename_path,
	quote,
	echo,
	calc,
};

class commands
{
public:
	struct output_line
	{
		std::u8string text;
		bool is_command = false;
	};

	void set_commands(std::vector<command_def> cmds)
	{
		_defs = std::move(cmds);
		_lookup.clear();
		for (auto& def : _defs)
			for (const auto& name : def.names)
				_lookup[name] = &def;
	}

	command_result execute(const std::u8string& input, uint8_t availability = command_availability::console);
	command_result invoke(const std::u8string& name, const std::vector<std::u8string>& args,
	                      uint8_t availability = command_availability::console) const;
	void append_output(std::u8string_view text, bool is_command = false);

	[[nodiscard]] std::u8string help_text(uint8_t availability = command_availability::console) const;
	[[nodiscard]] const std::vector<command_def>& defs() const { return _defs; }
	[[nodiscard]] const command_def* find_by_menu_id(int id) const;
	[[nodiscard]] const std::vector<std::u8string>& history() const { return _history; }
	[[nodiscard]] const std::vector<output_line>& output() const { return _output; }
	[[nodiscard]] uint8_t execution_context() const { return _execution_context; }

private:
	static std::vector<std::u8string> tokenize(const std::u8string& input);
	[[nodiscard]] const command_def* find_command(const std::u8string& name) const;

	std::vector<command_def> _defs;
	std::unordered_map<std::u8string, const command_def*, pf::ihash, pf::ieq> _lookup;
	std::vector<std::u8string> _history;
	std::vector<output_line> _output;
	mutable uint8_t _execution_context = command_availability::console;
};

struct redirected_command_args
{
	std::vector<std::u8string> values;
	std::u8string output_path;
	bool append = false;
	std::u8string error;
};

pf::file_path resolve_sandbox_path(const app_state& app, std::u8string_view raw_path);
redirected_command_args parse_redirected_args(const std::vector<std::u8string>& args);
bool save_and_open_text_file(app_state& app, const pf::file_path& path, std::u8string_view content,
                             bool append = false);
std::u8string relative_sandbox_path(const app_state& app, const pf::file_path& path);
void build_tree(std::u8string& out, const index_item_ptr& item,
                const std::u8string& prefix, bool is_last);
pf::file_path
destination_path_for_copy_or_move(const pf::file_path& source, const pf::file_path& requested_destination);
bool is_path_within_root(const pf::file_path& root, const pf::file_path& path);
bool copy_path_recursive(const pf::file_path& source, const pf::file_path& destination);
bool is_same_or_child_path(const pf::file_path& base_path, const pf::file_path& candidate_path);
bool ensure_directory_exists(const pf::file_path& dir);
index_item_ptr find_item_recursively(const index_item_ptr& item, const pf::file_path& path);
void rebase_item_paths(const index_item_ptr& item, const pf::file_path& new_path);
std::function<command_result(const std::vector<std::u8string>&)> bind_command_handler(
	app_state& app, app_command_handler handler);
