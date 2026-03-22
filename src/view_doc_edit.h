// view_doc_edit.h — Editable document view: character input, undo/redo, editing commands

#pragma once

#include "view_doc.h"

class edit_doc_view final : public doc_view
{
public:
	edit_doc_view(app_events& events) : doc_view(events)
	{
	}

	~edit_doc_view() override = default;

	std::vector<pf::menu_command> on_popup_menu(const pf::ipoint& client_pt) override
	{
		std::vector<pf::menu_command> items;

		// Spelling suggestions for the word under the cursor
		if (_doc->spell_check())
		{
			const auto text_pos = client_to_text(client_pt);
			const auto word_start = _doc->word_to_left(text_pos);
			const auto word_end = _doc->word_to_right(text_pos);

			if (word_start != word_end && word_start.y == word_end.y)
			{
				std::u8string line_text;
				(*_doc)[word_start.y].render(line_text);
				const auto word = line_text.substr(word_start.x, word_end.x - word_start.x);

				if (!word.empty() && _doc->spell_check() && !spell_check_word(word))
				{
					const text_selection word_sel(word_start, word_end);
					const auto suggestions = spell_suggest(word);

					for (const auto& suggestion : suggestions)
					{
						items.emplace_back(suggestion, 0,
						                   [this, word_sel, s = suggestion]
						                   {
							                   _doc->select(word_sel);
							                   undo_group ug(_doc);
							                   _doc->replace_text(ug, word_sel, s);
						                   });
					}

					if (!suggestions.empty())
						items.emplace_back(); // separator

					items.emplace_back(u8"Add to Dictionary", 0,
					                   [this, w = word]
					                   {
						                   spell_add_word(w);
						                   _events.invalidate(invalid::doc);
					                   });
					items.emplace_back(); // separator
				}
			}
		}

		// Edit commands
		items.push_back(_events.command_menu_item(command_id::edit_undo));
		items.push_back(_events.command_menu_item(command_id::edit_redo));
		items.emplace_back(); // separator
		items.push_back(_events.command_menu_item(command_id::edit_cut));
		items.push_back(_events.command_menu_item(command_id::edit_copy));
		items.push_back(_events.command_menu_item(command_id::edit_paste));
		items.push_back(_events.command_menu_item(command_id::edit_delete));
		items.emplace_back(); // separator
		items.push_back(_events.command_menu_item(command_id::edit_select_all));

		return items;
	}

	void drop_text(const std::u8string& text, const pf::ipoint& client_pt)
	{
		const auto drop_loc = client_to_text(client_pt);

		if (_dragging_text && _doc->is_inside_selection(drop_loc))
			return;

		undo_group ug(_doc);
		_doc->select(_doc->insert_text(ug, drop_loc, text));
	}

	void show_drop_indicator(const pf::ipoint& point)
	{
		if (!_drop_pos_visible)
		{
			_saved_caret_pos = _doc->cursor_pos();
			_drop_pos_visible = true;
		}
		_pt_drop_pos = client_to_text(point);
		_events.invalidate(invalid::windows);
	}

	void hide_drop_indicator()
	{
		if (_drop_pos_visible)
		{
			_doc->cursor_pos(_saved_caret_pos);
			_drop_pos_visible = false;
			_events.invalidate(invalid::windows);
		}
	}

	std::u8string prepare_drag_text()
	{
		const auto sel = _doc->selection();

		if (sel.empty())
			return {};

		_dragged_text_selection = sel;
		return combine(_doc->text(sel));
	}

protected:
	[[nodiscard]] bool can_cut_text() const override
	{
		return _doc->has_selection();
	}

	[[nodiscard]] bool can_paste_text() const override
	{
		return document::can_paste();
	}

	[[nodiscard]] bool can_delete_text() const override
	{
		return _doc->has_selection() || _doc->query_editable();
	}

	bool cut_text_to_clipboard() override
	{
		if (!_doc->has_selection())
			return false;
		return set_clipboard(_doc->edit_cut());
	}

	bool paste_text_from_clipboard() override
	{
		if (!document::can_paste())
			return false;
		_doc->edit_paste(clipboard_text());
		return true;
	}

	bool delete_selected_text() override
	{
		if (!_doc->query_editable())
			return false;
		_doc->edit_delete();
		return true;
	}

	void on_char(pf::window_frame_ptr& window, const char8_t c) override
	{
		if (window->is_key_down_async(pf::platform_key::LButton) ||
			window->is_key_down_async(pf::platform_key::RButton))
			return;

		if (c == pf::platform_key::Return)
		{
			if (_doc->query_editable())
			{
				undo_group ug(_doc);
				const auto pos = _doc->delete_text(ug, _doc->selection());
				_doc->select(_doc->insert_text(ug, pos, u8'\n'));
			}
		}
		else if (c > 31)
		{
			if (_doc->query_editable())
			{
				undo_group ug(_doc);
				const auto pos = _doc->delete_text(ug, _doc->selection());
				_doc->select(_doc->insert_text(ug, pos, static_cast<char8_t>(c)));
			}
		}
	}

	void draw_caret(pf::draw_context& draw) const override
	{
		if (_drop_pos_visible)
		{
			if (_pt_drop_pos.x >= scroll_char())
			{
				const auto pt = text_to_client(_pt_drop_pos);
				const auto rc = client_rect();
				const auto caret_rect = pf::irect(pt.x, pt.y, pt.x + 2, pt.y + _font_extent.cy);
				if (caret_rect.top < rc.bottom && caret_rect.bottom > rc.top)
					draw.fill_solid_rect(caret_rect, pf::color_t(128, 128, 128));
			}
			return;
		}
		doc_view::draw_caret(draw);
	}

private:
	text_location _pt_drop_pos;
	bool _drop_pos_visible = false;
	bool _dragging_text = false;
	text_location _saved_caret_pos;
	text_selection _dragged_text_selection;

	bool on_key_down(pf::window_frame_ptr& window, const unsigned int vk) override
	{
		namespace pk = pf::platform_key;
		const bool ctrl = window->is_key_down(pk::Control);
		const bool shift = window->is_key_down(pk::Shift);
		const bool alt = window->is_key_down(pk::Alt);

		// Editing keys
		if (vk == pk::Back && !ctrl && !alt)
		{
			_doc->edit_delete_back();
			return true;
		}
		if (vk == pk::Back && ctrl)
		{
			_doc->edit_delete_back();
			return true;
		}
		if (vk == pk::Delete && !shift)
		{
			_doc->edit_delete();
			return true;
		}
		if (vk == pk::Tab && !shift)
		{
			_doc->edit_tab();
			return true;
		}
		if (vk == pk::Tab && shift)
		{
			_doc->edit_untab();
			return true;
		}

		// Clipboard editing (secondary bindings)
		if (vk == pk::Insert && shift)
		{
			_doc->edit_paste(clipboard_text());
			return true;
		}
		if (vk == pk::Delete && shift)
		{
			set_clipboard(_doc->edit_cut());
			return true;
		}
		if (vk == pk::Back && alt)
		{
			_doc->edit_undo();
			return true;
		}

		// Clipboard + edit (primary bindings)
		if (ctrl && !shift && !alt)
		{
			if (vk == 'X')
			{
				set_clipboard(_doc->edit_cut());
				return true;
			}
			if (vk == 'V')
			{
				_doc->edit_paste(clipboard_text());
				return true;
			}
			if (vk == 'Z')
			{
				_doc->edit_undo();
				return true;
			}
			if (vk == 'Y')
			{
				_doc->edit_redo();
				return true;
			}
		}

		// View toggles
		if (vk == 'Z' && alt && !ctrl)
		{
			toggle_word_wrap();
			return true;
		}

		return doc_view::on_key_down(window, vk);
	}
};
