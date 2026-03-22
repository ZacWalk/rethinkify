// commands.cpp — Console command system implementation

#include "pch.h"
#include "commands.h"

#include "agent.h"
#include "app_state.h"
#include "command_calc.h"
#include "document.h"
#include "finance.h"
#include "view_doc.h"
#include "view_text.h"

namespace
{
	bool path_exists(const pf::file_path& path)
	{
		return path.exists() || pf::is_directory(path);
	}

	pf::file_path default_summary_output_path(const pf::file_path& source_path)
	{
		auto base_name = pf::file_path{source_path.name()}.without_extension();
		if (base_name.empty())
			base_name = source_path.name();
		return source_path.folder().combine(base_name + u8"-summary", u8"md");
	}

	std::u8string join_args(const std::vector<std::u8string>& args)
	{
		std::u8string text;
		for (size_t i = 0; i < args.size(); ++i)
		{
			if (i > 0)
				text += u8' ';
			text += args[i];
		}
		return text;
	}

	command_result execute_file_new(app_state& app, const std::vector<std::u8string>&)
	{
		app.on_new();
		return {u8"New document created.", true};
	}

	command_result execute_file_open(app_state& app, const std::vector<std::u8string>&)
	{
		app.on_open();
		return {u8"Open dialog shown.", true};
	}

	command_result execute_file_save(app_state& app, const std::vector<std::u8string>&)
	{
		app.on_save();
		return {u8"File saved.", true};
	}

	command_result execute_file_save_as(app_state& app, const std::vector<std::u8string>&)
	{
		app.on_save_as();
		return {u8"Save As dialog shown.", true};
	}

	command_result execute_file_save_all(app_state& app, const std::vector<std::u8string>&)
	{
		app.save_all();
		return {u8"All files saved.", true};
	}

	command_result execute_app_exit(app_state& app, const std::vector<std::u8string>&)
	{
		app.on_close();
		return {u8"", true};
	}

	command_result execute_edit_undo(app_state& app, const std::vector<std::u8string>&)
	{
		if (!app.doc()->can_undo())
			return {u8"Nothing to undo.", false};
		app.doc()->edit_undo();
		return {u8"Undone.", true};
	}

	command_result execute_edit_redo(app_state& app, const std::vector<std::u8string>&)
	{
		if (!app.doc()->can_redo())
			return {u8"Nothing to redo.", false};
		app.doc()->edit_redo();
		return {u8"Redone.", true};
	}

	command_result execute_edit_cut(app_state& app, const std::vector<std::u8string>&)
	{
		const auto view = app.focused_text_view();
		if (!view || !view->cut_text_to_clipboard())
			return {u8"No selection to cut.", false};
		app.invalidate(invalid::doc | invalid::console);
		return {u8"Selection cut.", true};
	}

	command_result execute_edit_copy(app_state& app, const std::vector<std::u8string>&)
	{
		if (!app.copy_current_focus_to_clipboard())
			return {u8"Nothing to copy.", false};
		return {u8"Copied.", true};
	}

	command_result execute_edit_paste(app_state& app, const std::vector<std::u8string>&)
	{
		const auto view = app.focused_text_view();
		if (!view || !view->paste_text_from_clipboard())
			return {u8"Nothing to paste.", false};
		app.invalidate(invalid::doc | invalid::console);
		return {u8"Pasted.", true};
	}

	command_result execute_edit_delete(app_state& app, const std::vector<std::u8string>&)
	{
		if (!app.delete_current_focus())
			return {u8"Nothing to delete.", false};
		app.invalidate(invalid::doc | invalid::console);
		return {u8"Deleted.", true};
	}

	command_result execute_edit_search_files(app_state& app, const std::vector<std::u8string>& args)
	{
		if (args.empty())
		{
			app.toggle_search_mode();
			return {u8"Search mode toggled.", true};
		}

		const auto text = join_args(args);
		if (!is_search(app.get_mode()))
			app.toggle_search_mode();
		app.on_search(text);
		return {pf::format(u8"Searching for '{}'...", text), true};
	}

	command_result execute_edit_select_all(app_state& app, const std::vector<std::u8string>&)
	{
		if (const auto view = app.focused_text_view())
			view->select_all_text();
		app.invalidate(invalid::doc | invalid::console);
		return {u8"All text selected.", true};
	}

	command_result execute_agent(app_state& app, const std::vector<std::u8string>& args)
	{
		if (args.empty())
			return {u8"Usage: a \"question\"", false};

		const auto prompt = join_args(args);
		app._scheduler->run_async([t = app.shared_from_this(), prompt]
		{
			const auto reply = run_agent(*t, prompt);
			t->_scheduler->run_ui([t, reply]()
			{
				t->get_commands().append_output(reply, false);
				t->invalidate(invalid::console);
			});
		});

		return {u8"Gemini is working...", true};
	}

	command_result execute_summarize(app_state& app, const std::vector<std::u8string>& args)
	{
		const auto parsed = parse_redirected_args(args);
		if (!parsed.error.empty())
			return {parsed.error, false};
		if (parsed.values.size() > 1)
			return {u8"Usage: summarize [path] [> file.md]", false};

		pf::file_path source_path;
		if (parsed.values.empty())
		{
			if (!app.active_item() || app.active_item()->path.empty())
				return {u8"No active document to summarize.", false};
			source_path = app.active_item()->path;
		}
		else
		{
			source_path = resolve_sandbox_path(app, parsed.values[0]);
			if (source_path.empty())
				return {u8"Source path must stay within the current root folder.", false};
		}

		const auto source_item = find_item_recursively(app.root_item(), source_path);
		if ((!source_item || !source_item->doc) && !source_path.exists())
			return {u8"Source document not found.", false};

		pf::file_path output_path;
		if (parsed.output_path.empty())
		{
			output_path = default_summary_output_path(source_path);
		}
		else
		{
			output_path = resolve_sandbox_path(app, parsed.output_path);
			if (output_path.empty())
				return {u8"Output path must stay within the current root folder.", false};
		}

		if (output_path == source_path)
			return {u8"Output path must be different from the source document.", false};

		const auto append = parsed.append;
		app._scheduler->run_async([t = app.shared_from_this(), source_path, output_path, append]
		{
			try
			{
				const auto result = summarize_document_as_markdown(*t, source_path);

				t->_scheduler->run_ui([t, source_path, output_path, append, result]()
				{
					if (!result.success)
					{
						t->get_commands().append_output(result.output, false);
					}
					else if (save_and_open_text_file(*t, output_path, result.output, append))
					{
						t->get_commands().append_output(
							pf::format(u8"Saved summary of {} to {}.",
							           relative_sandbox_path(*t, source_path),
							           relative_sandbox_path(*t, output_path)),
							false);
					}
					else
					{
						t->get_commands().append_output(u8"Failed to save summary output.", false);
					}

					t->invalidate(invalid::console);
				});
			}
			catch (const std::exception& e)
			{
				t->_scheduler->run_ui([t, message = std::u8string(reinterpret_cast<const char8_t*>(e.what()))]()
				{
					t->get_commands().append_output(u8"Summarize failed: " + message, false);
					t->invalidate(invalid::console);
				});
			}
		});

		return {
			pf::format(u8"Summarizing {} to {}...",
			           relative_sandbox_path(app, source_path),
			           relative_sandbox_path(app, output_path)),
			true
		};
	}

	command_result execute_edit_reformat(app_state& app, const std::vector<std::u8string>&)
	{
		app.on_edit_reformat();
		return {u8"Document reformatted.", true};
	}

	command_result execute_edit_sort_remove_duplicates(app_state& app, const std::vector<std::u8string>&)
	{
		app.on_edit_remove_duplicates();
		return {u8"Sorted and removed duplicates.", true};
	}

	command_result execute_edit_spell_check(app_state& app, const std::vector<std::u8string>&)
	{
		app.doc()->toggle_spell_check();
		const auto on = app.doc()->spell_check();
		app.set_spell_check_mode(on ? spell_check_mode::enabled : spell_check_mode::disabled);
		return {on ? u8"Spell check enabled." : u8"Spell check disabled.", true};
	}

	command_result execute_view_word_wrap(app_state& app, const std::vector<std::u8string>&)
	{
		app._doc_view->toggle_word_wrap();
		const auto on = app._doc_view->word_wrap();
		return {on ? u8"Word wrap enabled." : u8"Word wrap disabled.", true};
	}

	command_result execute_view_toggle_markdown(app_state& app, const std::vector<std::u8string>&)
	{
		app.toggle_markdown_view();
		const auto on = is_markdown(app.get_mode());
		return {on ? u8"Markdown preview on." : u8"Markdown preview off.", true};
	}

	command_result execute_view_toggle_chart(app_state& app, const std::vector<std::u8string>&)
	{
		app.toggle_chart_view();
		const auto on = is_chart(app.get_mode());
		return {on ? u8"Chart view on." : u8"Chart view off.", true};
	}

	command_result execute_view_refresh_folder(app_state& app, const std::vector<std::u8string>&)
	{
		app.on_refresh();
		return {u8"Folder refreshed.", true};
	}

	command_result execute_view_next_result(app_state& app, const std::vector<std::u8string>&)
	{
		app.on_navigate_next(true);
		return {u8"", true};
	}

	command_result execute_view_prev_result(app_state& app, const std::vector<std::u8string>&)
	{
		app.on_navigate_next(false);
		return {u8"", true};
	}

	command_result execute_help_run_tests(app_state& app, const std::vector<std::u8string>&)
	{
		app.on_run_tests();
		return {u8"Tests complete.", true};
	}

	command_result execute_app_about(app_state& app, const std::vector<std::u8string>&)
	{
		app.on_about();
		return {u8"", true};
	}

	command_result execute_help(app_state& app, const std::vector<std::u8string>& args)
	{
		const auto parsed = parse_redirected_args(args);
		if (!parsed.error.empty())
			return {parsed.error, false};

		const auto output = app.get_commands().help_text(app.get_commands().execution_context());
		if (parsed.output_path.empty())
			return {output, true};

		const auto output_path = resolve_sandbox_path(app, parsed.output_path);
		if (output_path.empty())
			return {u8"Output path must stay within the current root folder.", false};

		if (!save_and_open_text_file(app, output_path, output, parsed.append))
			return {u8"Failed to save help output.", false};

		return {pf::format(u8"Saved {}.", relative_sandbox_path(app, output_path)), true};
	}

	command_result execute_list_tree(app_state& app, const std::vector<std::u8string>& args)
	{
		const auto parsed = parse_redirected_args(args);
		if (!parsed.error.empty())
			return {parsed.error, false};
		if (!parsed.values.empty())
			return {u8"Usage: ls [> file.txt]", false};

		const auto root = app.root_item();
		if (!root || root->children.empty())
			return {u8"No folder open.", false};

		std::u8string out = root->name + u8"\n";
		for (size_t i = 0; i < root->children.size(); ++i)
			build_tree(out, root->children[i], u8"", i == root->children.size() - 1);

		if (parsed.output_path.empty())
			return {out, true};

		const auto output_path = resolve_sandbox_path(app, parsed.output_path);
		if (output_path.empty())
			return {u8"Output path must stay within the current root folder.", false};

		if (!save_and_open_text_file(app, output_path, out, parsed.append))
			return {u8"Failed to save directory listing.", false};

		return {pf::format(u8"Saved {}.", relative_sandbox_path(app, output_path)), true};
	}

	command_result execute_remove_path(app_state& app, const std::vector<std::u8string>& args)
	{
		if (args.size() != 1)
			return {u8"Usage: rm <path>", false};

		const auto target = resolve_sandbox_path(app, args[0]);
		if (target.empty())
			return {u8"Path must stay within the current root folder.", false};
		if (!path_exists(target))
			return {u8"Path not found.", false};

		const auto current_active = app.active_item() ? app.active_item()->path : pf::file_path{};
		if (!pf::platform_recycle_file(target))
			return {pf::format(u8"Failed to delete {}.", relative_sandbox_path(app, target)), false};

		app.refresh_index(app.root_item()->path, [&app, target, current_active]
		{
			app.invalidate(invalid::files_populate | invalid::search_populate | invalid::app_title | invalid::doc);
			if (!current_active.empty() && is_same_or_child_path(target, current_active))
				app.select_alternative();
		});

		return {pf::format(u8"Deleted {}.", relative_sandbox_path(app, target)), true};
	}

	command_result execute_copy_path(app_state& app, const std::vector<std::u8string>& args)
	{
		if (args.size() != 2)
			return {u8"Usage: cp <source> <dest>", false};

		const auto source = resolve_sandbox_path(app, args[0]);
		const auto requested_destination = resolve_sandbox_path(app, args[1]);
		if (source.empty() || requested_destination.empty())
			return {u8"Paths must stay within the current root folder.", false};
		if (!path_exists(source))
			return {u8"Source path not found.", false};

		const auto destination = destination_path_for_copy_or_move(source, requested_destination);
		if (!is_path_within_root(app.save_folder(), destination))
			return {u8"Destination must stay within the current root folder.", false};
		if (path_exists(destination))
			return {u8"Destination already exists.", false};

		if (!copy_path_recursive(source, destination))
			return {pf::format(u8"Failed to copy {}.", relative_sandbox_path(app, source)), false};

		app.refresh_index(app.root_item()->path, [&app]
		{
			app.invalidate(invalid::files_populate | invalid::search_populate | invalid::app_title);
		});

		return {
			pf::format(u8"Copied {} to {}.",
			           relative_sandbox_path(app, source),
			           relative_sandbox_path(app, destination)),
			true
		};
	}

	command_result execute_move_path(app_state& app, const std::vector<std::u8string>& args)
	{
		if (args.size() != 2)
			return {u8"Usage: mv <source> <dest>", false};

		const auto source = resolve_sandbox_path(app, args[0]);
		const auto requested_destination = resolve_sandbox_path(app, args[1]);
		if (source.empty() || requested_destination.empty())
			return {u8"Paths must stay within the current root folder.", false};
		if (!path_exists(source))
			return {u8"Source path not found.", false};

		const auto destination = destination_path_for_copy_or_move(source, requested_destination);
		if (!is_path_within_root(app.save_folder(), destination))
			return {u8"Destination must stay within the current root folder.", false};
		if (path_exists(destination))
			return {u8"Destination already exists.", false};
		if (is_same_or_child_path(source, destination))
			return {u8"Destination cannot be inside the source path.", false};

		if (!ensure_directory_exists(destination.folder()))
			return {u8"Destination folder does not exist and could not be created.", false};
		if (!pf::platform_rename_file(source, destination))
			return {pf::format(u8"Failed to move {}.", relative_sandbox_path(app, source)), false};

		if (const auto item = find_item_recursively(app.root_item(), source))
			rebase_item_paths(item, destination);

		app.refresh_index(app.root_item()->path, [&app]
		{
			app.invalidate(invalid::files_populate | invalid::search_populate | invalid::app_title | invalid::doc);
		});

		return {
			pf::format(u8"Moved {} to {}.",
			           relative_sandbox_path(app, source),
			           relative_sandbox_path(app, destination)),
			true
		};
	}

	command_result execute_rename_path(app_state& app, const std::vector<std::u8string>& args)
	{
		if (args.size() != 2)
			return {u8"Usage: rename <source> <new-name>", false};

		const auto source = resolve_sandbox_path(app, args[0]);
		if (source.empty())
			return {u8"Path must stay within the current root folder.", false};
		if (!path_exists(source))
			return {u8"Source path not found.", false};

		const auto new_name = std::u8string(pf::unquote(args[1]));
		if (new_name.empty() || new_name.find_first_of(u8"/\\") != std::u8string::npos)
			return {u8"New name must not contain path separators.", false};

		const pf::file_path destination{source.folder().combine(new_name)};
		if (!is_path_within_root(app.save_folder(), destination))
			return {u8"Destination must stay within the current root folder.", false};
		if (path_exists(destination))
			return {u8"Destination already exists.", false};
		if (!pf::platform_rename_file(source, destination))
			return {pf::format(u8"Failed to rename {}.", relative_sandbox_path(app, source)), false};

		if (const auto item = find_item_recursively(app.root_item(), source))
			rebase_item_paths(item, destination);

		app.refresh_index(app.root_item()->path, [&app]
		{
			app.invalidate(invalid::files_populate | invalid::search_populate | invalid::app_title | invalid::doc);
		});

		return {
			pf::format(u8"Renamed {} to {}.",
			           relative_sandbox_path(app, source),
			           relative_sandbox_path(app, destination)),
			true
		};
	}

	command_result execute_quote(app_state& app, const std::vector<std::u8string>& args)
	{
		const auto parsed = parse_redirected_args(args);
		if (!parsed.error.empty())
			return {parsed.error, false};
		if (parsed.values.empty())
			return {u8"Usage: q <ticker>", false};

		auto ticker = parsed.values[0];
		const auto output_path = resolve_sandbox_path(app, parsed.output_path);
		if (!parsed.output_path.empty() && output_path.empty())
			return {u8"Output path must stay within the current root folder.", false};

		if (app.get_commands().execution_context() == command_availability::agent)
		{
			auto md = fetch_stock_markdown(ticker);
			if (output_path.empty())
				return {std::move(md), true};

			if (!save_and_open_text_file(app, output_path, md, parsed.append))
				return {u8"Failed to save quote output.", false};

			return {pf::format(u8"Saved quote to {}.", relative_sandbox_path(app, output_path)), true};
		}

		const auto append = parsed.append;
		app._scheduler->run_async([t = app.shared_from_this(), ticker, output_path, append]
		{
			auto md = fetch_stock_markdown(ticker);

			t->_scheduler->run_ui([t, output_path, append, md = std::move(md)]()
			{
				if (output_path.empty())
				{
					t->get_commands().append_output(md, false);
				}
				else if (save_and_open_text_file(*t, output_path, md, append))
				{
					t->get_commands().append_output(
						pf::format(u8"Saved quote to {}.", relative_sandbox_path(*t, output_path)), false);
				}
				else
				{
					t->get_commands().append_output(u8"Failed to save quote output.", false);
				}

				t->invalidate(invalid::console);
			});
		});

		if (output_path.empty())
			return {pf::format(u8"Fetching quote for {}...", ticker), true};

		return {
			pf::format(u8"Fetching quote for {} and saving to {}...",
			           ticker,
			           relative_sandbox_path(app, output_path)),
			true
		};
	}

	command_result execute_echo(app_state& app, const std::vector<std::u8string>& args)
	{
		const auto parsed = parse_redirected_args(args);
		if (!parsed.error.empty())
			return {parsed.error, false};

		const auto text = join_args(parsed.values);
		if (parsed.output_path.empty())
			return {text, true};

		const auto output_path = resolve_sandbox_path(app, parsed.output_path);
		if (output_path.empty())
			return {u8"Output path must stay within the current root folder.", false};

		if (!save_and_open_text_file(app, output_path, text, parsed.append))
			return {u8"Failed to save echoed text.", false};

		return {pf::format(u8"Saved {}.", relative_sandbox_path(app, output_path)), true};
	}

	command_result execute_calc(app_state& app, const std::vector<std::u8string>& args)
	{
		const auto parsed = parse_redirected_args(args);
		if (!parsed.error.empty())
			return {parsed.error, false};
		if (parsed.values.empty())
			return {u8"Usage: calc <expression>", false};

		const auto expression = join_args(parsed.values);
		calc_parser parser(expression);
		const auto value = parser.parse();
		if (!value.has_value())
			return {pf::format(u8"Invalid expression: {}", parser.error()), false};

		const auto output = pf::format(u8"{:.10g}", *value);
		if (parsed.output_path.empty())
			return {output, true};

		const auto output_path = resolve_sandbox_path(app, parsed.output_path);
		if (output_path.empty())
			return {u8"Output path must stay within the current root folder.", false};

		if (!save_and_open_text_file(app, output_path, output, parsed.append))
			return {u8"Failed to save calc output.", false};

		return {pf::format(u8"Saved {}.", relative_sandbox_path(app, output_path)), true};
	}

	command_result dispatch_command_handler(app_state& app, const app_command_handler handler,
	                                        const std::vector<std::u8string>& args)
	{
		switch (handler)
		{
		case app_command_handler::file_new:
			return execute_file_new(app, args);
		case app_command_handler::file_open:
			return execute_file_open(app, args);
		case app_command_handler::file_save:
			return execute_file_save(app, args);
		case app_command_handler::file_save_as:
			return execute_file_save_as(app, args);
		case app_command_handler::file_save_all:
			return execute_file_save_all(app, args);
		case app_command_handler::app_exit:
			return execute_app_exit(app, args);
		case app_command_handler::edit_undo:
			return execute_edit_undo(app, args);
		case app_command_handler::edit_redo:
			return execute_edit_redo(app, args);
		case app_command_handler::edit_cut:
			return execute_edit_cut(app, args);
		case app_command_handler::edit_copy:
			return execute_edit_copy(app, args);
		case app_command_handler::edit_paste:
			return execute_edit_paste(app, args);
		case app_command_handler::edit_delete:
			return execute_edit_delete(app, args);
		case app_command_handler::edit_search_files:
			return execute_edit_search_files(app, args);
		case app_command_handler::edit_select_all:
			return execute_edit_select_all(app, args);
		case app_command_handler::agent:
			return execute_agent(app, args);
		case app_command_handler::summarize:
			return execute_summarize(app, args);
		case app_command_handler::edit_reformat:
			return execute_edit_reformat(app, args);
		case app_command_handler::edit_sort_remove_duplicates:
			return execute_edit_sort_remove_duplicates(app, args);
		case app_command_handler::edit_spell_check:
			return execute_edit_spell_check(app, args);
		case app_command_handler::view_word_wrap:
			return execute_view_word_wrap(app, args);
		case app_command_handler::view_toggle_markdown:
			return execute_view_toggle_markdown(app, args);
		case app_command_handler::view_toggle_chart:
			return execute_view_toggle_chart(app, args);
		case app_command_handler::view_refresh_folder:
			return execute_view_refresh_folder(app, args);
		case app_command_handler::view_next_result:
			return execute_view_next_result(app, args);
		case app_command_handler::view_prev_result:
			return execute_view_prev_result(app, args);
		case app_command_handler::help_run_tests:
			return execute_help_run_tests(app, args);
		case app_command_handler::app_about:
			return execute_app_about(app, args);
		case app_command_handler::help:
			return execute_help(app, args);
		case app_command_handler::list_tree:
			return execute_list_tree(app, args);
		case app_command_handler::remove_path:
			return execute_remove_path(app, args);
		case app_command_handler::copy_path:
			return execute_copy_path(app, args);
		case app_command_handler::move_path:
			return execute_move_path(app, args);
		case app_command_handler::rename_path:
			return execute_rename_path(app, args);
		case app_command_handler::quote:
			return execute_quote(app, args);
		case app_command_handler::echo:
			return execute_echo(app, args);
		case app_command_handler::calc:
			return execute_calc(app, args);
		}

		return {u8"Unknown command handler.", false};
	}
}

std::function<command_result(const std::vector<std::u8string>&)> bind_command_handler(
	app_state& app, const app_command_handler handler)
{
	return [&app, handler](const std::vector<std::u8string>& args)
	{
		return dispatch_command_handler(app, handler, args);
	};
}

std::vector<std::u8string> commands::tokenize(const std::u8string& input)
{
	std::vector<std::u8string> tokens;
	std::u8string current;
	bool in_quotes = false;

	for (const auto ch : input)
	{
		if (ch == L'"')
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
			{
				tokens.back() = u8">>";
			}
			else
			{
				tokens.push_back(u8">");
			}
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

const command_def* commands::find_command(const std::u8string& name) const
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

command_result commands::invoke(const std::u8string& name, const std::vector<std::u8string>& args,
                                const uint8_t availability) const
{
	const auto previous_context = _execution_context;
	_execution_context = availability;

	const auto* cmd = find_command(name);
	if (!cmd)
	{
		_execution_context = previous_context;
		return {pf::format(u8"Unknown command: '{}'. Type 'help' for a list of commands.", name), false};
	}

	if ((cmd->availability & _execution_context) == 0)
	{
		_execution_context = previous_context;
		return {pf::format(u8"Command '{}' is not available here.", name), false};
	}

	try
	{
		auto result = cmd->execute(args);
		_execution_context = previous_context;
		return result;
	}
	catch (...)
	{
		_execution_context = previous_context;
		throw;
	}
}

void commands::append_output(const std::u8string_view text, const bool is_command)
{
	if (text.empty())
		return;

	std::u8string_view remaining = text;
	while (!remaining.empty())
	{
		const auto nl = remaining.find(L'\n');
		if (nl == std::u8string_view::npos)
		{
			_output.push_back({std::u8string(remaining), is_command});
			break;
		}
		_output.push_back({std::u8string(remaining.substr(0, nl)), is_command});
		remaining = remaining.substr(nl + 1);
	}
}

std::u8string commands::help_text(const uint8_t availability) const
{
	std::u8string text;

	for (const auto& cmd : _defs)
	{
		if ((cmd.availability & availability) == 0)
			continue;

		std::u8string aliases;
		for (size_t i = 0; i < cmd.names.size(); ++i)
		{
			if (i > 0) aliases += u8", ";
			aliases += cmd.names[i];
		}

		text += pf::format(u8"  {:<20s} {}\n", aliases, cmd.description);
	}

	return text;
}

command_result commands::execute(const std::u8string& input, const uint8_t availability)
{
	if (input.empty())
		return {u8"", true};

	_history.push_back(input);
	_output.push_back({u8"> " + input, true});

	auto tokens = tokenize(input);

	if (tokens.empty())
		return {u8"", true};

	const auto& cmd_name = tokens[0];
	const std::vector<std::u8string> args(tokens.begin() + 1, tokens.end());

	command_result result;
	try
	{
		result = invoke(cmd_name, args, availability);
	}
	catch (...)
	{
		throw;
	}

	if (!result.output.empty())
	{
		append_output(result.output, false);
	}

	return result;
}

void build_tree(std::u8string& out, const index_item_ptr& item,
                const std::u8string& prefix, const bool is_last)
{
	out += prefix;
	out += is_last ? u8"`-- " : u8"|-- ";
	out += item->name;
	out += u8'\n';

	const auto child_prefix = prefix + (is_last ? u8"    " : u8"|   ");

	for (size_t i = 0; i < item->children.size(); ++i)
		build_tree(out, item->children[i], child_prefix, i == item->children.size() - 1);
}

bool is_path_within_root(const pf::file_path& root, const pf::file_path& path)
{
	return !path.empty() && agent_is_within_root(root.view(), arent_normalize_path(path.view()));
}

pf::file_path resolve_sandbox_path(const app_state& app, const std::u8string_view raw_path)
{
	auto requested = std::u8string(pf::unquote(raw_path));
	if (requested.empty() || requested == u8".")
		return app.save_folder();

	for (auto& ch : requested)
	{
		if (ch == u8'/')
			ch = u8'\\';
	}

	const bool absolute = (requested.size() >= 2 && requested[1] == u8':') ||
		(requested.size() >= 2 && requested[0] == u8'\\' && requested[1] == u8'\\');
	const auto combined = absolute ? requested : std::u8string(app.save_folder().combine(requested).view());
	const auto normalized = arent_normalize_path(combined);
	if (!agent_is_within_root(app.save_folder().view(), normalized))
		return {};
	return pf::file_path{normalized};
}

std::u8string relative_sandbox_path(const app_state& app, const pf::file_path& path)
{
	const auto root_text = arent_normalize_path(app.save_folder().view());
	const auto path_text = arent_normalize_path(path.view());
	if (pf::icmp(root_text, path_text) == 0)
		return u8".";

	if (path_text.size() > root_text.size() &&
		pf::icmp(path_text.substr(0, root_text.size()), root_text) == 0)
	{
		auto rel = path_text.substr(root_text.size());
		if (!rel.empty() && rel.front() == u8'\\')
			rel = rel.substr(1);
		return std::u8string(rel);
	}

	return std::u8string(path.view());
}

index_item_ptr find_item_recursively(const index_item_ptr& item, const pf::file_path& path)
{
	if (!item)
		return nullptr;
	if (item->path == path)
		return item;
	for (const auto& child : item->children)
	{
		if (const auto found = find_item_recursively(child, path))
			return found;
	}
	return nullptr;
}

bool is_same_or_child_path(const pf::file_path& base_path, const pf::file_path& candidate_path)
{
	const auto base = arent_normalize_path(base_path.view());
	const auto candidate = arent_normalize_path(candidate_path.view());
	if (pf::icmp(base, candidate) == 0)
		return true;
	if (candidate.size() <= base.size())
		return false;
	if (pf::icmp(candidate.substr(0, base.size()), base) != 0)
		return false;
	return candidate[base.size()] == u8'\\';
}

bool ensure_directory_exists(const pf::file_path& dir)
{
	if (dir.empty() || pf::is_directory(dir))
		return true;

	const auto parent_text = dir.folder();
	if (!parent_text.empty())
	{
		const pf::file_path parent{parent_text};
		if (!(parent == dir) && !pf::is_directory(parent) && !ensure_directory_exists(parent))
			return false;
	}

	return pf::platform_create_directory(dir) || pf::is_directory(dir);
}

static bool write_text_file(const pf::file_path& path, const std::u8string_view content)
{
	if (!ensure_directory_exists(path.folder()))
		return false;

	const auto handle = pf::open_file_for_write(path);
	if (!handle)
		return false;

	const auto bytes = reinterpret_cast<const uint8_t*>(content.data());
	const auto size = static_cast<uint32_t>(content.size());
	return handle->write(bytes, size) == size;
}

static bool copy_file_contents(const pf::file_path& source, const pf::file_path& destination)
{
	const auto input = pf::open_for_read(source);
	if (!input)
		return false;
	if (!ensure_directory_exists(destination.folder()))
		return false;

	const auto output = pf::open_file_for_write(destination);
	if (!output)
		return false;

	const auto buffer = std::make_unique<uint8_t[]>(64 * 1024);
	while (true)
	{
		uint32_t read = 0;
		if (!input->read(buffer.get(), 64 * 1024, &read))
			return false;
		if (read == 0)
			break;
		if (output->write(buffer.get(), read) != read)
			return false;
	}

	return true;
}

bool copy_path_recursive(const pf::file_path& source, const pf::file_path& destination)
{
	if (pf::is_directory(source))
	{
		if (!ensure_directory_exists(destination))
			return false;

		const auto contents = pf::iterate_file_items(source, true);
		for (const auto& folder : contents.folders)
		{
			if (!copy_path_recursive(folder.path, destination.combine(folder.path.name())))
				return false;
		}
		for (const auto& file : contents.files)
		{
			if (!copy_file_contents(file.path, destination.combine(file.path.name())))
				return false;
		}
		return true;
	}

	return copy_file_contents(source, destination);
}

void rebase_item_paths(const index_item_ptr& item, const pf::file_path& new_path)
{
	if (!item)
		return;

	item->path = new_path;
	item->name = new_path.name();
	if (item->doc)
		item->doc->path(new_path);

	if (item->is_folder)
	{
		for (const auto& child : item->children)
			rebase_item_paths(child, new_path.combine(child->name));
	}
}

pf::file_path destination_path_for_copy_or_move(const pf::file_path& source, const pf::file_path& requested_destination)
{
	if (pf::is_directory(requested_destination))
		return requested_destination.combine(source.name());
	return requested_destination;
}

redirected_command_args parse_redirected_args(const std::vector<std::u8string>& args)
{
	redirected_command_args parsed;

	for (size_t i = 0; i < args.size(); ++i)
	{
		if (args[i] != u8">" && args[i] != u8">>")
		{
			parsed.values.push_back(args[i]);
			continue;
		}

		if (!parsed.output_path.empty())
		{
			parsed.error = u8"Only one output redirection is supported.";
			return parsed;
		}

		if (i + 1 >= args.size())
		{
			parsed.error = u8"Missing output file path after '>'.";
			return parsed;
		}

		parsed.output_path = std::u8string(pf::unquote(args[i + 1]));
		parsed.append = args[i] == u8">>";
		if (i + 2 != args.size())
		{
			parsed.error = u8"Output redirection must appear at the end of the command.";
			return parsed;
		}

		return parsed;
	}

	return parsed;
}

bool save_and_open_text_file(app_state& app, const pf::file_path& path, const std::u8string_view content,
                             const bool append)
{
	if (path.empty())
		return false;

	std::u8string final_content;
	const auto existed_on_disk = path.exists();
	auto item = find_item_recursively(app.root_item(), path);

	if (append && path.exists())
	{
		if (item && item->doc)
		{
			final_content = item->doc->str();
		}
		else
		{
			auto loaded = load_lines(path);
			const auto doc = std::make_shared<document>(app, path, loaded.disk_modified_time, loaded.encoding);
			doc->apply_loaded_data(path, std::move(loaded));
			final_content = doc->str();
		}
	}
	if (append)
		final_content += content;
	const auto& content_to_save = append ? std::u8string_view(final_content) : content;

	// create_new_file is only safe before the file exists on disk because it resolves collisions
	// by generating a unique name. For brand new outputs we want the exact requested path.
	if (!item && !existed_on_disk)
	{
		app.create_new_file(path, std::u8string(content_to_save));
		item = app.active_item();
	}

	if (!write_text_file(path, content_to_save))
		return false;

	if (!item)
		return false;

	if (!item->doc)
	{
		auto loaded = load_lines(path);
		item->doc = std::make_shared<document>(app, path, loaded.disk_modified_time, loaded.encoding);
		item->doc->apply_loaded_data(path, std::move(loaded));
	}

	auto loaded = load_lines(path);
	item->doc->apply_loaded_data(path, std::move(loaded));
	item->path = path;
	item->name = path.name();
	item->doc->path(path);
	app.set_active_item(item);

	app.invalidate(invalid::index | invalid::files_populate | invalid::app_title | invalid::doc | invalid::console);
	return true;
}
