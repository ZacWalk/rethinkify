#pragma once

// commands.h — Console command system: tokenizer, command_def registration, case-insensitive map lookup, help text

#include "platform.h"

struct command_result
{
	std::wstring output;
	bool success = true;
};

struct command_def
{
	// Console identification
	std::vector<std::wstring> names; // e.g. {"s", "save"} or {"?", "h", "help"}
	std::wstring description;

	// Menu identification (empty menu_text = console-only command)
	std::wstring menu_text;
	int menu_id = 0;
	pf::key_binding accel;
	std::function<bool()> is_enabled;
	std::function<bool()> is_checked;

	// Execution
	std::function<command_result(const std::vector<std::wstring>&)> execute;
};

class commands
{
public:
	struct output_line
	{
		std::wstring text;
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

	command_result execute(const std::wstring& input);

	[[nodiscard]] std::wstring help_text() const;
	[[nodiscard]] const std::vector<command_def>& defs() const { return _defs; }
	[[nodiscard]] const command_def* find_by_menu_id(int id) const;
	[[nodiscard]] const std::vector<std::wstring>& history() const { return _history; }
	[[nodiscard]] const std::vector<output_line>& output() const { return _output; }

private:
	static std::vector<std::wstring> tokenize(const std::wstring& input);
	[[nodiscard]] const command_def* find_command(const std::wstring& name) const;

	std::vector<command_def> _defs;
	std::unordered_map<std::wstring, const command_def*, ihash, ieq> _lookup;
	std::vector<std::wstring> _history;
	std::vector<output_line> _output;
};
