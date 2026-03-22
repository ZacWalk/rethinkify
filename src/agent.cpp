// agent.cpp — Agent integration: prompt execution, tool dispatch, and model response handling

#include "pch.h"

#include <array>
#include <future>

#include "app_state.h"
#include "commands.h"
#include "document.h"
#include "agent.h"

namespace
{
	constexpr auto gemini_model = u8"gemini-2.5-flash";
	constexpr auto max_tool_round_trips = 12;
	constexpr auto max_list_entries = 200;
	constexpr auto max_search_matches = 100;
	constexpr auto max_response_chars = 24000;
	constexpr uint32_t max_inline_pdf_bytes = 50u * 1024u * 1024u;

	struct gemini_function_call
	{
		std::u8string name;
		std::u8string args_json;
		std::u8string raw_part_json;
	};

	struct gemini_message
	{
		std::u8string role;
		std::u8string part_json;
	};

	struct agent_context
	{
		app_state& app;
		pf::file_path root;
	};

	struct gemini_summary_source
	{
		std::u8string part_json;
		std::u8string source_label;
	};

	std::u8string json_escape(const std::u8string_view text)
	{
		std::u8string result;
		result.reserve(text.size() + 16);

		for (size_t i = 0; i < text.size(); ++i)
		{
			const auto ch = text[i];
			switch (ch)
			{
			case u8'\\': result += u8"\\\\";
				break;
			case u8'"': result += u8"\\\"";
				break;
			case u8'\b': result += u8"\\b";
				break;
			case u8'\f': result += u8"\\f";
				break;
			case u8'\n': result += u8"\\n";
				break;
			case u8'\r': result += u8"\\r";
				break;
			case u8'\t': result += u8"\\t";
				break;
			default:
				if (static_cast<unsigned char>(ch) < 0x20)
				{
					result += pf::format(u8"\\u{:04x}", static_cast<int>(ch));
				}
				else
				{
					result += ch;
				}
				break;
			}
		}

		return result;
	}

	std::u8string json_unescape(const std::u8string_view text)
	{
		std::u8string result;
		result.reserve(text.size());

		for (size_t i = 0; i < text.size(); ++i)
		{
			const auto ch = text[i];
			if (ch != u8'\\' || i + 1 >= text.size())
			{
				result += ch;
				continue;
			}

			const auto esc = text[++i];
			switch (esc)
			{
			case u8'"': result += u8'"';
				break;
			case u8'\\': result += u8'\\';
				break;
			case u8'/': result += u8'/';
				break;
			case u8'b': result += u8'\b';
				break;
			case u8'f': result += u8'\f';
				break;
			case u8'n': result += u8'\n';
				break;
			case u8'r': result += u8'\r';
				break;
			case u8't': result += u8'\t';
				break;
			case u8'u':
				if (i + 4 < text.size())
				{
					uint32_t cp = 0;
					for (int k = 0; k < 4; ++k)
					{
						cp = (cp << 4) | char_to_hex(text[i + 1 + k]);
					}
					i += 4;
					pf::char32_to_utf8(std::back_inserter(result), cp);
				}
				break;
			default:
				result += esc;
				break;
			}
		}

		return result;
	}

	size_t find_matching_brace(const std::u8string_view text, const size_t open_pos)
	{
		if (open_pos >= text.size()) return std::u8string::npos;

		const auto open_ch = text[open_pos];
		const auto close_ch = open_ch == u8'{' ? u8'}' : u8']';
		int depth = 1;

		for (size_t i = open_pos + 1; i < text.size(); ++i)
		{
			if (text[i] == u8'"')
			{
				for (++i; i < text.size(); ++i)
				{
					if (text[i] == u8'\\')
					{
						++i;
						continue;
					}
					if (text[i] == u8'"')
						break;
				}
				continue;
			}

			if (text[i] == open_ch) ++depth;
			else if (text[i] == close_ch) --depth;

			if (depth == 0)
				return i;
		}

		return std::u8string::npos;
	}

	size_t skip_ws(const std::u8string_view text, size_t pos)
	{
		while (pos < text.size() && (text[pos] == u8' ' || text[pos] == u8'\t' || text[pos] == u8'\r' || text[pos] ==
			u8'\n'))
			++pos;
		return pos;
	}

	std::u8string extract_json_string_value(const std::u8string_view text, const std::u8string_view key)
	{
		const auto search = u8"\"" + std::u8string(key) + u8"\"";
		auto pos = text.find(search);
		if (pos == std::u8string::npos) return {};

		pos = skip_ws(text, pos + search.size());
		if (pos >= text.size() || text[pos] != u8':') return {};
		pos = skip_ws(text, pos + 1);
		if (pos >= text.size() || text[pos] != u8'"') return {};

		const auto start = ++pos;
		while (pos < text.size())
		{
			if (text[pos] == u8'\\')
			{
				pos += 2;
				continue;
			}
			if (text[pos] == u8'"')
				return json_unescape(text.substr(start, pos - start));
			++pos;
		}

		return {};
	}

	std::u8string extract_json_object_value(const std::u8string_view text, const std::u8string_view key)
	{
		const auto search = u8"\"" + std::u8string(key) + u8"\"";
		auto pos = text.find(search);
		if (pos == std::u8string::npos) return {};

		pos = skip_ws(text, pos + search.size());
		if (pos >= text.size() || text[pos] != u8':') return {};
		pos = skip_ws(text, pos + 1);
		if (pos >= text.size() || (text[pos] != u8'{' && text[pos] != u8'[')) return {};

		const auto end = find_matching_brace(text, pos);
		if (end == std::u8string::npos) return {};
		return std::u8string(text.substr(pos, end - pos + 1));
	}

	int extract_json_int_value(const std::u8string_view text, const std::u8string_view key, const int default_value)
	{
		const auto search = u8"\"" + std::u8string(key) + u8"\"";
		auto pos = text.find(search);
		if (pos == std::u8string::npos) return default_value;

		pos = skip_ws(text, pos + search.size());
		if (pos >= text.size() || text[pos] != u8':') return default_value;
		pos = skip_ws(text, pos + 1);
		if (pos >= text.size()) return default_value;

		const auto start = pos;
		if (text[pos] == u8'+' || text[pos] == u8'-') ++pos;
		while (pos < text.size() && text[pos] >= u8'0' && text[pos] <= u8'9') ++pos;
		if (pos == start) return default_value;

		try
		{
			return pf::stoi(text.substr(start, pos - start));
		}
		catch (...)
		{
			return default_value;
		}
	}

	std::u8string truncate_text(const std::u8string_view text, const size_t max_chars = max_response_chars)
	{
		if (text.size() <= max_chars)
			return std::u8string(text);

		const auto kept = pf::utf8_truncate(text, static_cast<int>(max_chars));
		return std::u8string(text.substr(0, kept)) + u8"\n\n[truncated]";
	}

	std::u8string join_lines(const std::vector<std::u8string>& lines)
	{
		return combine(lines, u8"\n");
	}

	std::vector<std::u8string> tokenize_command(const std::u8string_view input)
	{
		std::vector<std::u8string> tokens;
		std::u8string current;
		bool in_quotes = false;

		for (const auto ch : input)
		{
			if (ch == u8'"')
			{
				in_quotes = !in_quotes;
			}
			else if (!in_quotes && ch == u8'>')
			{
				if (!current.empty())
				{
					tokens.push_back(std::move(current));
					current.clear();
				}

				if (!tokens.empty() && tokens.back() == u8">")
					tokens.back() = u8">>";
				else
					tokens.push_back(u8">");
			}
			else if (ch == u8' ' && !in_quotes)
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

	std::u8string read_text_file(const pf::file_path& path)
	{
		const auto handle = pf::open_for_read(path);
		if (!handle)
			return {};

		const auto size = handle->size();
		if (size == 0)
			return {};

		std::vector<uint8_t> data(size);
		uint32_t total = 0;
		while (total < size)
		{
			uint32_t chunk = 0;
			if (!handle->read(data.data() + total, size - total, &chunk) || chunk == 0)
				break;
			total += chunk;
		}

		return std::u8string(reinterpret_cast<const char8_t*>(data.data()), total);
	}

	std::vector<uint8_t> read_binary_file(const pf::file_path& path)
	{
		const auto handle = pf::open_for_read(path);
		if (!handle)
			return {};

		const auto size = handle->size();
		std::vector<uint8_t> data(size);
		uint32_t total = 0;
		while (total < size)
		{
			uint32_t chunk = 0;
			if (!handle->read(data.data() + total, size - total, &chunk) || chunk == 0)
				break;
			total += chunk;
		}

		data.resize(total);
		return data;
	}

	std::vector<index_item_ptr> collect_items(const index_item_ptr& root)
	{
		std::vector<index_item_ptr> items;
		if (!root)
			return items;

		std::vector<index_item_ptr> stack{root};
		while (!stack.empty())
		{
			auto current = stack.back();
			stack.pop_back();
			items.push_back(current);
			for (auto it = current->children.rbegin(); it != current->children.rend(); ++it)
				stack.push_back(*it);
		}

		return items;
	}

	index_item_ptr find_item(const index_item_ptr& root, const pf::file_path& path)
	{
		for (const auto& item : collect_items(root))
		{
			if (item && item->path == path)
				return item;
		}
		return nullptr;
	}

	template <typename Fn>
	auto run_on_ui(app_state& app, Fn&& fn) -> decltype(fn())
	{
		using result_t = decltype(fn());
		std::promise<result_t> promise;
		auto future = promise.get_future();

		app._scheduler->run_ui([&promise, task = std::forward<Fn>(fn)]() mutable
		{
			if constexpr (std::is_void_v<result_t>)
			{
				task();
				promise.set_value();
			}
			else
			{
				promise.set_value(task());
			}
		});

		return future.get();
	}

	pf::file_path resolve_agent_path(const pf::file_path& root, const std::u8string_view raw_path)
	{
		auto requested = std::u8string(pf::unquote(raw_path));
		if (requested.empty() || requested == u8".")
			return root;

		for (auto& ch : requested)
		{
			if (ch == u8'/') ch = u8'\\';
		}

		const bool absolute = requested.size() >= 2 && requested[1] == u8':' ||
			(requested.size() >= 2 && requested[0] == u8'\\' && requested[1] == u8'\\');

		const auto combined = absolute ? requested : std::u8string(root.combine(requested).view());
		const auto normalized = arent_normalize_path(combined);
		if (!agent_is_within_root(root.view(), normalized))
			return {};
		return pf::file_path{normalized};
	}

	std::u8string relative_agent_path(const pf::file_path& root, const pf::file_path& path)
	{
		const auto root_text = arent_normalize_path(root.view());
		const auto path_text = arent_normalize_path(path.view());
		if (pf::icmp(root_text, path_text) == 0)
			return u8".";

		if (path_text.size() > root_text.size() && pf::icmp(path_text.substr(0, root_text.size()), root_text) == 0)
		{
			auto rel = path_text.substr(root_text.size());
			if (!rel.empty() && rel.front() == u8'\\')
				rel = rel.substr(1);
			return std::u8string(rel);
		}

		return path.name();
	}

	std::u8string render_doc_lines(const document_ptr& doc, const int start_line, const int line_count)
	{
		if (!doc)
			return {};

		const auto first = std::max(1, start_line);
		const auto last = std::min(first + std::max(1, line_count) - 1, static_cast<int>(doc->size()));

		std::u8string out;
		std::u8string line_text;
		for (int line = first; line <= last; ++line)
		{
			doc->operator[](line - 1).render(line_text);
			out += pf::format(u8"{:>6} | {}\n", line, line_text);
		}
		return out;
	}

	std::u8string tool_list_files(const agent_context& ctx, const std::u8string_view args_json)
	{
		const auto path = resolve_agent_path(ctx.root, extract_json_string_value(args_json, u8"path"));
		if (path.empty())
			return u8"Error: path must stay within the current root folder.";

		const auto max_entries = pf::clamp(extract_json_int_value(args_json, u8"max_entries", 80), 1, max_list_entries);

		return run_on_ui(ctx.app, [&]()
		{
			std::u8string out = pf::format(u8"Root: {}\n", ctx.root.view());
			int count = 0;
			for (const auto& item : collect_items(ctx.app.root_item()))
			{
				if (!item || item->path.empty() || item == ctx.app.root_item())
					continue;
				if (!agent_is_within_root(path.view(), item->path.view()))
					continue;
				out += relative_agent_path(ctx.root, item->path);
				if (item->is_folder)
					out += u8"/";
				out += u8"\n";
				if (++count >= max_entries)
				{
					out += u8"[truncated]\n";
					break;
				}
			}
			if (count == 0)
				out += u8"[no entries]\n";
			return out;
		});
	}

	std::u8string tool_read_file(const agent_context& ctx, const std::u8string_view args_json)
	{
		const auto path = resolve_agent_path(ctx.root, extract_json_string_value(args_json, u8"path"));
		if (path.empty())
			return u8"Error: path must stay within the current root folder.";

		const auto start_line = std::max(1, extract_json_int_value(args_json, u8"start_line", 1));
		const auto line_count = pf::clamp(extract_json_int_value(args_json, u8"line_count", 200), 1, 400);

		return run_on_ui(ctx.app, [&]()
		{
			const auto item = find_item(ctx.app.root_item(), path);
			if (item && item->doc)
			{
				return truncate_text(render_doc_lines(item->doc, start_line, line_count));
			}

			if (!path.exists())
				return std::u8string(u8"Error: file not found.");

			auto loaded = load_lines(path);
			const auto doc = std::make_shared<document>(ctx.app, path, loaded.disk_modified_time, loaded.encoding);
			doc->apply_loaded_data(path, std::move(loaded));
			return truncate_text(render_doc_lines(doc, start_line, line_count));
		});
	}

	std::u8string tool_write_file(const agent_context& ctx, const std::u8string_view args_json)
	{
		const auto path = resolve_agent_path(ctx.root, extract_json_string_value(args_json, u8"path"));
		if (path.empty())
			return u8"Error: path must stay within the current root folder.";

		const auto content = extract_json_string_value(args_json, u8"content");

		return run_on_ui(ctx.app, [&]()
		{
			auto item = find_item(ctx.app.root_item(), path);
			pf::file_path save_path = path;
			if (!item)
			{
				const auto created = ctx.app.create_new_file(path, content);
				if (!created)
					return std::u8string(u8"Error: failed to create file.");

				save_path = created.path;
				item = find_item(ctx.app.root_item(), created.path);
				if (!item)
					item = ctx.app.active_item();
			}
			else
			{
				if (!item->doc)
				{
					auto loaded = load_lines(path);
					item->doc = std::make_shared<document>(ctx.app, path, loaded.disk_modified_time, loaded.encoding);
					item->doc->apply_loaded_data(path, std::move(loaded));
				}

				undo_group ug(item->doc);
				item->doc->select(item->doc->all());
				item->doc->select(item->doc->replace_text(ug, item->doc->all(), content));
				ctx.app.set_active_item(item);
			}

			if (!item || !item->doc || !item->doc->save_to_file(save_path))
				return std::u8string(u8"Error: failed to save file.");

			item->path = save_path;
			item->name = save_path.name();
			item->doc->path(save_path);
			ctx.app.invalidate(invalid::files_populate | invalid::app_title | invalid::doc);
			if (save_path == path)
				return pf::format(u8"Saved {}.", relative_agent_path(ctx.root, save_path));

			return pf::format(u8"Saved {} because {} was already in use.",
			                  relative_agent_path(ctx.root, save_path),
			                  relative_agent_path(ctx.root, path));
		});
	}

	std::u8string tool_search_files(const agent_context& ctx, const std::u8string_view args_json)
	{
		const auto query = extract_json_string_value(args_json, u8"query");
		if (query.empty())
			return u8"Error: query is required.";

		const auto folder = resolve_agent_path(ctx.root, extract_json_string_value(args_json, u8"path"));
		if (folder.empty())
			return u8"Error: path must stay within the current root folder.";

		const auto limit = pf::clamp(extract_json_int_value(args_json, u8"max_matches", 40), 1, max_search_matches);

		return run_on_ui(ctx.app, [&]()
		{
			std::u8string out;
			int total = 0;
			std::u8string line_text;

			for (const auto& item : collect_items(ctx.app.root_item()))
			{
				if (!item || item->is_folder || !agent_is_within_root(folder.view(), item->path.view()))
					continue;

				document_ptr doc = item->doc;
				loaded_file_data loaded;
				if (!doc)
				{
					loaded = load_lines(item->path);
					doc = std::make_shared<document>(ctx.app, item->path, loaded.disk_modified_time, loaded.encoding);
					doc->apply_loaded_data(item->path, std::move(loaded));
				}

				for (int line_number = 0; line_number < static_cast<int>(doc->size()); ++line_number)
				{
					doc->operator[](line_number).render(line_text);
					const auto pos = find_in_text(line_text, query);
					if (pos == std::u8string::npos)
						continue;

					out += pf::format(u8"{}:{}: {}\n", relative_agent_path(ctx.root, item->path), line_number + 1,
					                  line_text);
					if (++total >= limit)
					{
						out += u8"[truncated]\n";
						return out;
					}
				}
			}

			if (total == 0)
				return std::u8string(u8"No matches.");
			return out;
		});
	}

	std::u8string tool_run_command(const agent_context& ctx, const std::u8string_view args_json)
	{
		const auto command = extract_json_string_value(args_json, u8"command");
		if (command.empty())
			return u8"Error: command is required.";

		return run_on_ui(ctx.app, [&]()
		{
			auto tokens = tokenize_command(command);
			if (tokens.empty())
				return std::u8string(u8"Error: command is empty.");

			const std::vector<std::u8string> command_args(tokens.begin() + 1, tokens.end());
			const auto result = ctx.app.get_commands().invoke(tokens[0], command_args, command_availability::agent);
			if (result.output.empty())
				return std::u8string(result.success ? u8"Command completed." : u8"Command failed.");
			return truncate_text(result.output);
		});
	}

	std::u8string tool_descriptions_json()
	{
		return u8R"json([
			{
				"name": "list_files",
				"description": "List files and folders under the current root or a subfolder.",
				"parameters": {
					"type": "OBJECT",
					"properties": {
						"path": {"type": "STRING", "description": "Relative path under the root folder, or '.' for the root."},
						"max_entries": {"type": "INTEGER", "description": "Maximum number of entries to return."}
					}
				}
			},
			{
				"name": "read_file",
				"description": "Read a text file with line numbers. Use this before making changes.",
				"parameters": {
					"type": "OBJECT",
					"properties": {
						"path": {"type": "STRING", "description": "Relative path under the root folder."},
						"start_line": {"type": "INTEGER", "description": "1-based starting line."},
						"line_count": {"type": "INTEGER", "description": "Number of lines to read."}
					},
					"required": ["path"]
				}
			},
			{
				"name": "write_file",
				"description": "Replace an entire text file with new content and save it to disk.",
				"parameters": {
					"type": "OBJECT",
					"properties": {
						"path": {"type": "STRING", "description": "Relative path under the root folder."},
						"content": {"type": "STRING", "description": "Full replacement file contents."}
					},
					"required": ["path", "content"]
				}
			},
			{
				"name": "search_files",
				"description": "Search for text across files in the current root or a subfolder.",
				"parameters": {
					"type": "OBJECT",
					"properties": {
						"query": {"type": "STRING", "description": "Search text."},
						"path": {"type": "STRING", "description": "Optional relative folder path under the root."},
						"max_matches": {"type": "INTEGER", "description": "Maximum number of matches to return."}
					},
					"required": ["query"]
				}
			},
			{
				"name": "run_command",
				"description": "Run an existing Rethinkify console command without echoing it to the console.",
				"parameters": {
					"type": "OBJECT",
					"properties": {
						"command": {"type": "STRING", "description": "A full console command such as 'rf' or 'md'."}
					},
					"required": ["command"]
				}
			}
		])json";
	}

	std::u8string build_system_instruction(const app_state& app, const pf::file_path& root)
	{
		auto text = pf::format(
			u8"You are an editor agent running inside Rethinkify. Operate only within the current root folder '{}'. Never request or mention paths outside that root. Read files before editing them. Prefer the provided tools over guessing. When you have enough information, return a direct final answer to the user with no markdown code fences unless necessary. Available agent commands are:\n{}",
			root.view(), app.get_commands().help_text(command_availability::agent));
		return text;
	}

	std::u8string build_request_body(const app_state& app, const pf::file_path& root,
	                                 const std::vector<gemini_message>& messages)
	{
		std::u8string contents = u8"[";
		for (size_t i = 0; i < messages.size(); ++i)
		{
			if (i > 0)
				contents += u8",";
			contents += pf::format(u8"{{\"role\":\"{}\",\"parts\":[{}]}}", messages[i].role, messages[i].part_json);
		}
		contents += u8"]";

		return pf::format(
			u8"{{\"systemInstruction\":{{\"parts\":[{{\"text\":\"{}\"}}]}},\"tools\":[{{\"functionDeclarations\":{}}}],\"contents\":{},\"generationConfig\":{{\"temperature\":0.2}}}}",
			json_escape(build_system_instruction(app, root)), tool_descriptions_json(), contents);
	}

	std::u8string send_gemini_generate_content(const pf::web_host_ptr& host,
	                                           const std::u8string_view api_key,
	                                           const std::u8string_view request_body)
	{
		pf::web_request req;
		req.path = u8"/v1beta/models/";
		req.path += std::u8string_view(gemini_model);
		req.path += u8":generateContent";
		req.query = {{u8"key", std::u8string(api_key)}};
		req.verb = pf::web_request_verb::POST;
		req.headers = {{u8"Content-Type", u8"application/json"}};
		req.body = std::u8string(request_body);

		const auto response = pf::send_request(host, req);
		if (response.status_code == 0)
		{
			auto detail = pf::platform_last_error_message();
			if (detail.empty())
				detail =
					u8"No HTTP response was received. This usually means the transport failed before Gemini replied, often because the inline request body was too large.";
			return pf::format(u8"Gemini request failed with HTTP 0.\n{}", detail);
		}

		if (response.status_code != 200)
		{
			const auto body = response.body.empty() ? u8"" : truncate_text(response.body, 8000);
			return pf::format(u8"Gemini request failed with HTTP {}.\n{}", response.status_code, body);
		}

		return response.body;
	}

	std::u8string build_summary_system_instruction(const pf::file_path& root)
	{
		return pf::format(
			u8"You summarize documents for Rethinkify. Operate only on the provided document from the current root folder '{}'. Return markdown only. Preserve important facts, headings, numbers, dates, and caveats when present. If content is unclear or partially unreadable, say so instead of guessing.",
			root.view());
	}

	std::u8string build_summary_request_body(const pf::file_path& root, const gemini_summary_source& source)
	{
		const auto prompt = pf::format(
			u8"Summarize this document into a standalone markdown document. Start with a level-1 title based on the document. Then use concise sections and bullet points where useful. Include key takeaways, important details, and uncertainties or missing context when relevant. Source path: {}",
			source.source_label);

		return pf::format(
			u8"{{\"systemInstruction\":{{\"parts\":[{{\"text\":\"{}\"}}]}},\"contents\":[{{\"role\":\"user\",\"parts\":[{},{{\"text\":\"{}\"}}]}}],\"generationConfig\":{{\"temperature\":0.2}}}}",
			json_escape(build_summary_system_instruction(root)), source.part_json, json_escape(prompt));
	}

	std::u8string summary_mime_type(const pf::file_path& path)
	{
		const auto ext = pf::to_lower(path.extension());
		if (ext == u8".pdf")
			return u8"application/pdf";
		if (ext == u8".md" || ext == u8".markdown")
			return u8"text/markdown";
		if (ext == u8".txt" || ext == u8".text" || ext == u8".log")
			return u8"text/plain";
		return {};
	}

	agent_operation_result build_summary_source(const agent_context& ctx, const pf::file_path& source_path,
	                                            gemini_summary_source& out_source)
	{
		const auto mime_type = summary_mime_type(source_path);
		if (mime_type.empty())
			return {u8"Only .md, .markdown, .txt, .text, .log, and .pdf documents can be summarized.", false};

		out_source.source_label = relative_agent_path(ctx.root, source_path);
		if (mime_type == u8"application/pdf")
		{
			if (!source_path.exists())
				return {u8"PDF source file was not found on disk.", false};

			const auto bytes = read_binary_file(source_path);
			if (bytes.empty())
				return {u8"Failed to read PDF source file.", false};
			if (bytes.size() > max_inline_pdf_bytes)
			{
				return {
					u8"PDF is larger than 50 MB. Gemini supports larger PDFs via the Files API, but this command currently sends PDFs inline.",
					false
				};
			}

			out_source.part_json = pf::format(
				u8"{{\"inlineData\":{{\"mimeType\":\"{}\",\"data\":\"{}\"}}}}",
				mime_type, to_base64(std::span<const uint8_t>(bytes.data(), bytes.size())));
			return {};
		}

		auto text = run_on_ui(ctx.app, [&]()
		{
			if (const auto item = find_item(ctx.app.root_item(), source_path); item && item->doc)
				return item->doc->str();
			if (source_path.exists())
				return read_text_file(source_path);
			return std::u8string{};
		});

		if (text.empty() && !source_path.exists())
			return {u8"Source document was not found.", false};

		out_source.part_json = pf::format(u8"{{\"text\":\"{}\"}}", json_escape(text));
		return {};
	}

	std::u8string find_candidate_content(const std::u8string_view response_json)
	{
		const auto candidates = extract_json_object_value(response_json, u8"candidates");
		if (candidates.empty())
			return {};
		const auto content_pos = candidates.find(u8"\"content\"");
		if (content_pos == std::u8string::npos)
			return {};
		const auto brace = candidates.find(u8'{', content_pos);
		if (brace == std::u8string::npos)
			return {};
		const auto end = find_matching_brace(candidates, brace);
		if (end == std::u8string::npos)
			return {};
		return std::u8string(candidates.substr(brace, end - brace + 1));
	}

	std::optional<gemini_function_call> parse_function_call(const std::u8string_view response_json)
	{
		const auto content = find_candidate_content(response_json);
		if (content.empty())
			return std::nullopt;

		const auto key_pos = content.find(u8"\"functionCall\"");
		if (key_pos == std::u8string::npos)
			return std::nullopt;

		const auto part_start = content.rfind(u8'{', key_pos);
		const auto call_start = content.find(u8'{', key_pos);
		if (part_start == std::u8string::npos || call_start == std::u8string::npos)
			return std::nullopt;

		const auto part_end = find_matching_brace(content, part_start);
		const auto call_end = find_matching_brace(content, call_start);
		if (part_end == std::u8string::npos || call_end == std::u8string::npos)
			return std::nullopt;

		gemini_function_call call;
		call.raw_part_json = std::u8string(content.substr(part_start, part_end - part_start + 1));
		call.name = extract_json_string_value(content.substr(call_start, call_end - call_start + 1), u8"name");
		call.args_json = extract_json_object_value(content.substr(call_start, call_end - call_start + 1), u8"args");
		if (call.name.empty())
			return std::nullopt;
		if (call.args_json.empty())
			call.args_json = u8"{}";
		return call;
	}

	std::u8string parse_text_response(const std::u8string_view response_json)
	{
		const auto content = find_candidate_content(response_json);
		if (content.empty())
			return {};

		std::u8string result;
		size_t pos = 0;
		while (true)
		{
			const auto text_pos = content.find(u8"\"text\"", pos);
			if (text_pos == std::u8string::npos)
				break;
			const auto value = extract_json_string_value(content.substr(text_pos), u8"text");
			if (!value.empty())
			{
				if (!result.empty())
					result += u8"\n";
				result += value;
			}
			pos = text_pos + 6;
		}
		return result;
	}

	std::u8string execute_tool(const agent_context& ctx, const gemini_function_call& call)
	{
		if (call.name == u8"list_files")
			return tool_list_files(ctx, call.args_json);
		if (call.name == u8"read_file")
			return tool_read_file(ctx, call.args_json);
		if (call.name == u8"write_file")
			return tool_write_file(ctx, call.args_json);
		if (call.name == u8"search_files")
			return tool_search_files(ctx, call.args_json);
		if (call.name == u8"run_command")
			return tool_run_command(ctx, call.args_json);
		return pf::format(u8"Error: unknown tool '{}'.", call.name);
	}

	std::u8string append_function_response_json(const gemini_function_call& call, const std::u8string_view result)
	{
		return pf::format(
			u8"{{\"functionResponse\":{{\"name\":\"{}\",\"response\":{{\"result\":\"{}\"}}}}}}",
			json_escape(call.name), json_escape(truncate_text(result)));
	}

	std::u8string load_api_key(const pf::file_path& root)
	{
		const std::array<pf::file_path, 2> candidates = {
			root.combine(u8".env"),
			pf::file_path::module_folder().combine(u8".env")
		};

		for (const auto& path : candidates)
		{
			if (!path.exists())
				continue;
			const auto text = read_text_file(path);
			const auto key = agent_parse_env_value(text, u8"GEMINI_API_KEY");
			if (!key.empty())
				return key;
		}

		return {};
	}
}

std::u8string agent_parse_env_value(const std::u8string_view text, const std::u8string_view key)
{
	std::u8string_view remaining = text;
	while (!remaining.empty())
	{
		const auto nl = remaining.find(u8'\n');
		auto line = nl == std::u8string_view::npos ? remaining : remaining.substr(0, nl);
		if (!line.empty() && line.back() == u8'\r')
			line = line.substr(0, line.size() - 1);

		auto trimmed = line;
		while (!trimmed.empty() && (trimmed.front() == u8' ' || trimmed.front() == u8'\t'))
			trimmed.remove_prefix(1);

		if (!trimmed.empty() && trimmed.front() != u8'#')
		{
			const auto eq = trimmed.find(u8'=');
			if (eq != std::u8string_view::npos)
			{
				auto name = trimmed.substr(0, eq);
				auto value = trimmed.substr(eq + 1);

				while (!name.empty() && (name.back() == u8' ' || name.back() == u8'\t')) name.remove_suffix(1);
				while (!value.empty() && (value.front() == u8' ' || value.front() == u8'\t')) value.remove_prefix(1);

				if (pf::icmp(name, key) == 0)
					return std::u8string(pf::unquote(value));
			}
		}

		if (nl == std::u8string_view::npos)
			break;
		remaining.remove_prefix(nl + 1);
	}

	return {};
}

std::u8string arent_normalize_path(const std::u8string_view path)
{
	if (path.empty())
		return {};

	std::u8string normalized;
	normalized.reserve(path.size());
	for (const auto ch : path)
		normalized += ch == u8'/' ? u8'\\' : ch;

	std::u8string prefix;
	size_t start = 0;
	if (normalized.size() >= 2 && normalized[1] == u8':')
	{
		prefix = pf::to_lower(std::u8string_view(normalized).substr(0, 2));
		start = 2;
	}
	else if (normalized.size() >= 2 && normalized[0] == u8'\\' && normalized[1] == u8'\\')
	{
		prefix = u8"\\\\";
		start = 2;
	}

	std::vector<std::u8string> parts;
	while (start < normalized.size())
	{
		while (start < normalized.size() && normalized[start] == u8'\\') ++start;
		const auto end = normalized.find(u8'\\', start);
		auto part = std::u8string(end == std::u8string::npos
			                          ? std::u8string_view(normalized).substr(start)
			                          : std::u8string_view(normalized).substr(start, end - start));

		if (part == u8"." || part.empty())
		{
		}
		else if (part == u8"..")
		{
			if (!parts.empty())
				parts.pop_back();
		}
		else
		{
			parts.push_back(part);
		}

		if (end == std::u8string::npos)
			break;
		start = end + 1;
	}

	std::u8string result = prefix;
	if (!prefix.empty() && prefix != u8"\\\\")
		result += u8"\\";
	for (size_t i = 0; i < parts.size(); ++i)
	{
		if (!result.empty() && result.back() != u8'\\')
			result += u8"\\";
		result += parts[i];
	}

	return result;
}

bool agent_is_within_root(const std::u8string_view root, const std::u8string_view candidate)
{
	const auto normalized_root = arent_normalize_path(root);
	const auto normalized_candidate = arent_normalize_path(candidate);
	if (normalized_root.empty() || normalized_candidate.empty())
		return false;
	if (pf::icmp(normalized_root, normalized_candidate) == 0)
		return true;
	if (normalized_candidate.size() <= normalized_root.size())
		return false;
	if (pf::icmp(std::u8string_view(normalized_candidate).substr(0, normalized_root.size()), normalized_root) != 0)
		return false;
	return normalized_candidate[normalized_root.size()] == u8'\\';
}

std::u8string run_agent(app_state& app, const std::u8string_view prompt)
{
	const auto root = app.save_folder();
	if (root.empty())
		return u8"No root folder is open.";

	const auto api_key = load_api_key(root);
	if (api_key.empty())
		return u8"GEMINI_API_KEY was not found in .env.";

	const auto host = pf::connect_to_host(u8"generativelanguage.googleapis.com", true, 0, g_app_name);
	if (!host)
		return u8"Failed to connect to Gemini.";

	std::vector<gemini_message> messages;
	messages.push_back({u8"user", pf::format(u8"{{\"text\":\"{}\"}}", json_escape(prompt))});

	const agent_context ctx{app, root};

	for (int round = 0; round < max_tool_round_trips; ++round)
	{
		const auto body = send_gemini_generate_content(host, api_key, build_request_body(app, root, messages));
		if (body.find(u8"Gemini request failed with HTTP ") == 0)
			return body;

		if (const auto function_call = parse_function_call(body))
		{
			messages.push_back({u8"model", function_call->raw_part_json});
			const auto tool_result = execute_tool(ctx, *function_call);
			messages.push_back({u8"user", append_function_response_json(*function_call, tool_result)});
			continue;
		}

		const auto text = parse_text_response(body);
		if (!text.empty())
			return text;
		return u8"Gemini returned no text response.";
	}

	return u8"Gemini agent stopped after too many tool calls.";
}

agent_operation_result summarize_document_as_markdown(app_state& app, const pf::file_path& source_path)
{
	const auto root = app.save_folder();
	if (root.empty())
		return {u8"No root folder is open.", false};
	if (source_path.empty() || !agent_is_within_root(root.view(), arent_normalize_path(source_path.view())))
		return {u8"Source path must stay within the current root folder.", false};

	const auto api_key = load_api_key(root);
	if (api_key.empty())
		return {u8"GEMINI_API_KEY was not found in .env.", false};

	const auto host = pf::connect_to_host(u8"generativelanguage.googleapis.com", true, 0, g_app_name);
	if (!host)
		return {u8"Failed to connect to Gemini.", false};

	const agent_context ctx{app, root};
	gemini_summary_source source;
	if (const auto result = build_summary_source(ctx, source_path, source); !result.success)
		return result;

	const auto body = send_gemini_generate_content(host, api_key, build_summary_request_body(root, source));
	if (body.find(u8"Gemini request failed with HTTP ") == 0)
		return {body, false};

	const auto text = parse_text_response(body);
	if (text.empty())
		return {u8"Gemini returned no text response.", false};

	return {text, true};
}
