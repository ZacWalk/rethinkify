#pragma once

// view_text_edit.h — Editable text view: character input, undo/redo, editing accelerators

#include "view_text.h"

class text_edit_view final : public text_view
{
public:
	text_edit_view(app_events& events) : text_view(events)
	{
		build_edit_accelerators();
	}

	~text_edit_view() override = default;

	std::vector<pf::menu_command> on_popup_menu(const ipoint& client_pt) override
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
				const auto line_text = (*_doc)[word_start.y].view();
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

					items.emplace_back(L"Add to Dictionary", 0,
					                   [this, w = std::wstring(word)]
					                   {
						                   spell_add_word(w);
						                   _events.invalidate(invalid::view);
					                   });
					items.emplace_back(); // separator
				}
			}
		}

		// Edit commands
		items.emplace_back(L"&Undo\tCtrl+Z", 0, [this] { _doc->edit_undo(); },
		                   [this] { return _doc->can_undo(); });
		items.emplace_back(L"&Redo\tCtrl+Y", 0, [this] { _doc->edit_redo(); },
		                   [this] { return _doc->can_redo(); });
		items.emplace_back(); // separator
		items.emplace_back(L"Cu&t\tCtrl+X", 0, [this] { set_clipboard(_doc->edit_cut()); },
		                   [this] { return _doc->has_selection(); });
		items.emplace_back(L"&Copy\tCtrl+C", 0, [this] { set_clipboard(_doc->copy()); },
		                   [this] { return _doc->has_selection(); });
		items.emplace_back(L"&Paste\tCtrl+V", 0, [this] { _doc->edit_paste(clipboard_text()); },
		                   [this] { return document::can_paste(); });
		items.emplace_back(L"&Delete", 0, [this] { _doc->edit_delete(); },
		                   [this] { return _doc->has_selection(); });
		items.emplace_back(); // separator
		items.emplace_back(L"Select &All\tCtrl+A", 0, [this] { _doc->select(_doc->all()); });

		return items;
	}

	void drop_text(const std::wstring& text, const ipoint& client_pt)
	{
		const auto drop_loc = client_to_text(client_pt);

		if (_dragging_text && _doc->is_inside_selection(drop_loc))
			return;

		undo_group ug(_doc);
		_doc->select(_doc->insert_text(ug, drop_loc, text));
	}

	void show_drop_indicator(const ipoint& point)
	{
		if (!_drop_pos_visible)
		{
			_saved_caret_pos = _doc->cursor_pos();
			_drop_pos_visible = true;
		}
		_pt_drop_pos = client_to_text(point);
		_events.invalidate(invalid::invalidate);
	}

	void hide_drop_indicator()
	{
		if (_drop_pos_visible)
		{
			_doc->cursor_pos(_saved_caret_pos);
			_drop_pos_visible = false;
			_events.invalidate(invalid::invalidate);
		}
	}

	std::wstring prepare_drag_text()
	{
		const auto sel = _doc->selection();

		if (sel.empty())
			return {};

		_dragged_text_selection = sel;
		return str::combine(_doc->text(sel));
	}

protected:
	void on_char(pf::window_frame_ptr& window, const wchar_t c) override
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
				_doc->select(_doc->insert_text(ug, pos, L'\n'));
			}
		}
		else if (c > 31)
		{
			if (_doc->query_editable())
			{
				undo_group ug(_doc);
				const auto pos = _doc->delete_text(ug, _doc->selection());
				_doc->select(_doc->insert_text(ug, pos, c));
			}
		}
	}

	void draw_caret(pf::draw_context& draw) const override
	{
		if (_drop_pos_visible)
		{
			if (_pt_drop_pos.x >= _char_offset.cx)
			{
				const auto pt = text_to_client(_pt_drop_pos);
				const auto rc = client_rect();
				const auto caret_rect = irect(pt.x, pt.y, pt.x + 2, pt.y + _font_extent.cy);
				if (caret_rect.top < rc.bottom && caret_rect.bottom > rc.top)
					draw.fill_solid_rect(caret_rect, color_t(128, 128, 128));
			}
			return;
		}
		text_view::draw_caret(draw);
	}

private:
	text_location _pt_drop_pos;
	bool _drop_pos_visible = false;
	bool _dragging_text = false;
	text_location _saved_caret_pos;
	text_selection _dragged_text_selection;

	void build_edit_accelerators()
	{
		namespace pk = pf::platform_key;
		namespace km = pf::key_mod;

		// Editing
		_accel.add({pk::Back}, [this] { _doc->edit_delete_back(); });
		_accel.add({pk::Back, km::ctrl}, [this] { _doc->edit_delete_back(); });
		_accel.add({pk::Delete}, [this] { _doc->edit_delete(); });
		_accel.add({pk::Tab}, [this] { _doc->edit_tab(); });
		_accel.add({pk::Tab, km::shift}, [this] { _doc->edit_untab(); });

		// Clipboard editing (secondary bindings)
		_accel.add({pk::Insert, km::shift}, [this] { _doc->edit_paste(clipboard_text()); });
		_accel.add({pk::Delete, km::shift}, [this] { set_clipboard(_doc->edit_cut()); });
		_accel.add({pk::Back, km::alt}, [this] { _doc->edit_undo(); });

		// Clipboard + edit (primary bindings)
		_accel.add({'X', km::ctrl}, [this] { set_clipboard(_doc->edit_cut()); });
		_accel.add({'V', km::ctrl}, [this] { _doc->edit_paste(clipboard_text()); });
		_accel.add({'Z', km::ctrl}, [this] { _doc->edit_undo(); });
		_accel.add({'Y', km::ctrl}, [this] { _doc->edit_redo(); });

		// View toggles
		_accel.add({'Z', km::alt}, [this] { toggle_word_wrap(); });
	}
};
