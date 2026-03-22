// commands.cpp — Console command system implementation

#include "pch.h"
#include "commands.h"

std::vector<std::wstring> commands::tokenize(const std::wstring& input)
{
	std::vector<std::wstring> tokens;
	std::wstring current;
	bool in_quotes = false;

	for (const auto ch : input)
	{
		if (ch == L'"')
		{
			in_quotes = !in_quotes;
		}
		else if (ch == L' ' && !in_quotes)
		{
			if (!current.empty())
			{
				tokens.push_back(std::move(current));
				current.clear();
			}
		}
		else
		{
			current += ch;
		}
	}

	if (!current.empty())
		tokens.push_back(std::move(current));

	return tokens;
}

const command_def* commands::find_command(const std::wstring& name) const
{
	const auto it = _lookup.find(name);
	return it != _lookup.end() ? it->second : nullptr;
}

const command_def* commands::find_by_menu_id(const int id) const
{
	for (const auto& def : _defs)
		if (def.menu_id == id)
			return &def;
	return nullptr;
}

std::wstring commands::help_text() const
{
	std::wstring text;

	for (const auto& cmd : _defs)
	{
		std::wstring aliases;
		for (size_t i = 0; i < cmd.names.size(); ++i)
		{
			if (i > 0) aliases += L", ";
			aliases += cmd.names[i];
		}

		text += std::format(L"  {:<20s} {}\n", aliases, cmd.description);
	}

	return text;
}

command_result commands::execute(const std::wstring& input)
{
	if (input.empty())
		return {L"", true};

	_history.push_back(input);
	_output.push_back({L"> " + input, true});

	auto tokens = tokenize(input);

	if (tokens.empty())
		return {L"", true};

	const auto& cmd_name = tokens[0];
	const std::vector<std::wstring> args(tokens.begin() + 1, tokens.end());

	const auto* cmd = find_command(cmd_name);

	command_result result;
	if (cmd)
	{
		result = cmd->execute(args);
	}
	else
	{
		result = {std::format(L"Unknown command: '{}'. Type 'help' for a list of commands.", cmd_name), false};
	}

	if (!result.output.empty())
	{
		// Split multi-line output into separate output entries
		std::wstring_view remaining = result.output;
		while (!remaining.empty())
		{
			const auto nl = remaining.find(L'\n');
			if (nl == std::wstring_view::npos)
			{
				_output.push_back({std::wstring(remaining), false});
				break;
			}
			_output.push_back({std::wstring(remaining.substr(0, nl)), false});
			remaining = remaining.substr(nl + 1);
		}
	}

	return result;
}
