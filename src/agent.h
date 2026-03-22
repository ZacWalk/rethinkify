// agent.h — Agent integration interfaces: environment parsing and prompt execution

#pragma once

#include "platform.h"

class app_state;

struct agent_operation_result
{
	std::u8string output;
	bool success = true;
};

std::u8string agent_parse_env_value(std::u8string_view text, std::u8string_view key);
std::u8string arent_normalize_path(std::u8string_view path);
bool agent_is_within_root(std::u8string_view root, std::u8string_view candidate);
std::u8string run_agent(app_state& app, std::u8string_view prompt);
agent_operation_result summarize_document_as_markdown(app_state& app, const pf::file_path& source_path);
