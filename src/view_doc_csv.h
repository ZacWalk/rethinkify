// view_doc_csv.h — CSV document view: read-only table display for comma-separated value files

#pragma once

#include "view_text.h"

class csv_doc_view final : public doc_view
{
	table_layout::table_block _table; // cached column layout for entire document

public:
	csv_doc_view(app_events& events) : doc_view(events)
	{
		_sel_margin = false;
		_word_wrap = true;
	}

	~csv_doc_view() override = default;

	void set_document(const document_ptr& d) override
	{
		doc_view::set_document(d);
		rebuild_table();
	}

	void handle_size(pf::window_frame_ptr& window, const pf::isize extent,
	                 pf::measure_context& measure) override
	{
		doc_view::handle_size(window, extent, measure);
	}

	void on_mouse_wheel(pf::window_frame_ptr& window, const int zDelta) override
	{
		if (!can_scroll()) return;
		set_scroll_pixel(_scroll_offset.y + zDelta * _font_extent.cy);
	}

	bool on_key_down(pf::window_frame_ptr& window, const unsigned int vk) override
	{
		namespace pk = pf::platform_key;
		const bool ctrl = window->is_key_down(pk::Control);

		if (vk == pk::Up && ctrl)
		{
			wrap_scroll_by(-1);
			return true;
		}
		if (vk == pk::Down && ctrl)
		{
			wrap_scroll_by(1);
			return true;
		}
		if (vk == pk::Prior)
		{
			wrap_scroll_by(-_screen_lines);
			return true;
		}
		if (vk == pk::Next)
		{
			wrap_scroll_by(_screen_lines);
			return true;
		}

		if (vk == pk::Home && ctrl)
		{
			_scroll_offset = {};
			recalc_vert_scrollbar();
			_events.invalidate(invalid::windows);
			return true;
		}
		if (vk == pk::End && ctrl)
		{
			const int max_y = std::max(0, _content_extent.cy - (_view_extent.cy - text_top()));
			set_scroll_pixel(max_y);
			return true;
		}

		return doc_view::on_key_down(window, vk);
	}

	uint32_t handle_mouse(const pf::window_frame_ptr window, const pf::mouse_message_type msg,
	                      const pf::mouse_params& params) override
	{
		using mt = pf::mouse_message_type;

		if (msg == mt::mouse_move && _vscroll._tracking)
		{
			const auto new_pos = _vscroll.track_to(params.point, scrollbar_rect());
			set_scroll_pixel(new_pos * _font_extent.cy);
			return 0;
		}

		return doc_view::handle_mouse(window, msg, params);
	}

	void recalc_vert_scrollbar() override
	{
		_content_extent.cy = _font_extent.cy + _total_visual_rows * _font_extent.cy;

		const int max_y = std::max(0, _content_extent.cy - (_view_extent.cy - text_top()));
		if (_scroll_offset.y > max_y)
		{
			_scroll_offset.y = max_y;
			_events.invalidate(invalid::windows);
		}

		const int pos = _font_extent.cy > 0 ? _scroll_offset.y / _font_extent.cy : 0;
		_vscroll.update(_total_visual_rows + 1 + 2, _screen_lines, pos);
	}

	void layout() override
	{
		if (_doc)
			_parse_cookies.assign(_doc->size(), invalid_cookie);
		else
			_parse_cookies.clear();

		rebuild_table();

		if (!_doc || _table.col_widths.empty())
		{
			_wrap_breaks.clear();
			_wrap_offsets.clear();
			_wrap_line_y.clear();
			_total_visual_rows = 0;
			return;
		}

		const auto line_count = static_cast<int>(_doc->size());
		_wrap_breaks.clear();
		_wrap_offsets.clear();
		_wrap_line_y.resize(line_count + 1);
		_wrap_line_y[0] = 0;

		auto break_fn = [](const std::string_view text, const int col_w)
		{
			return calc_word_breaks(text, col_w, [](int, int) { return 1; });
		};

		int cumulative = 0;
		std::string line_text;

		for (int i = 0; i < line_count; i++)
		{
			_wrap_line_y[i] = cumulative;

			(*_doc)[i].render(line_text);
			const auto cells = table_layout::split_csv_cells(line_text);

			int max_rows = 1;
			for (size_t c = 0; c < _table.col_widths.size() && c < cells.size(); c++)
			{
				const auto vr = table_layout::cell_visual_rows(
					table_layout::trim_cell(cells[c]), _table.col_widths[c], break_fn);
				if (vr > max_rows) max_rows = vr;
			}
			cumulative += max_rows;

			// Add one visual row for the separator after the header
			if (i == 0 && line_count > 1)
				cumulative += 1;
		}

		_wrap_line_y[line_count] = cumulative;
		_total_visual_rows = cumulative;
	}

protected:
	void update_focus(pf::window_frame_ptr& window) override
	{
		doc_view::update_focus(window);
		stop_caret_blink(window);
	}

	void draw_view(pf::window_frame_ptr& window, pf::draw_context& draw) const override
	{
		const auto rcClient = client_rect();
		const auto line_count = static_cast<int>(_doc->size());
		const auto pad_top = text_top();
		const auto left_pad = _font_extent.cx * 2;
		const auto font_cx = _font_extent.cx;
		const auto font_cy = _font_extent.cy;
		const auto& font = _events.styles().text_font;

		const auto bg = style_to_color(style::normal_bkgnd);
		const auto pipe_clr = style_to_color(style::md_marker);
		const auto header_clr = style_to_color(style::md_bold);
		const auto text_clr = style_to_color(style::normal_text);

		draw.fill_solid_rect(rcClient, bg);

		if (_table.col_widths.empty())
		{
			_vscroll.draw(draw, rcClient);
			draw_message_bar(draw);
			return;
		}

		auto break_fn = [](const std::string_view text, const int col_w)
		{
			return calc_word_breaks(text, col_w, [](int, int) { return 1; });
		};

		// Find first visible line
		int nCurrentLine;
		if (!_wrap_line_y.empty())
		{
			const int first_vrow = font_cy > 0
				                       ? std::max(0, (_scroll_offset.y - font_cy) / font_cy)
				                       : 0;
			nCurrentLine = visual_row_to_line_index(first_vrow);
		}
		else
		{
			nCurrentLine = 0;
		}

		auto y = line_offset(nCurrentLine) - _scroll_offset.y + pad_top;
		std::string line_text;

		while (y < rcClient.bottom && nCurrentLine < line_count)
		{
			(*_doc)[nCurrentLine].render(line_text);

			const auto cells = table_layout::split_csv_cells(line_text);
			const bool is_header = (nCurrentLine == 0);
			const auto tx = is_header ? header_clr : text_clr;

			const auto vis_rows = table_layout::draw_table_row(
				draw, y, left_pad, rcClient.right, cells, _table,
				font, font_cx, font_cy, is_header, bg, pipe_clr, tx, break_fn);

			y += vis_rows * font_cy;

			// Draw a separator row after the header
			if (is_header && line_count > 1 && y < rcClient.bottom)
			{
				table_layout::draw_separator_row(draw, y, left_pad, rcClient.right, _table,
				                                 font, font_cx, font_cy, bg, pipe_clr);
				y += font_cy;
			}

			nCurrentLine++;
		}

		_vscroll.draw(draw, scrollbar_rect());
		draw_message_bar(draw);
	}

private:
	static int safe_cols(const int width, const int char_width)
	{
		return char_width > 0 ? std::max(1, width / char_width) : 1;
	}

	void rebuild_table()
	{
		_table = {};

		if (!_doc || _doc->empty()) return;

		const auto line_count = static_cast<int>(_doc->size());
		const auto left_pad = _font_extent.cx * 2;
		const auto avail_width = _view_extent.cx > left_pad ? _view_extent.cx - left_pad : 1;
		const auto avail_cols = safe_cols(avail_width, _font_extent.cx);

		_table.start_line = 0;
		_table.end_line = line_count;
		_table.separator_line = -1; // separator is drawn visually, not from a document line

		// Compute natural column widths from all rows
		std::string tmp;
		for (int i = 0; i < line_count; i++)
		{
			(*_doc)[i].render(tmp);
			const auto cells = table_layout::split_csv_cells(tmp);

			while (_table.col_widths.size() < cells.size())
				_table.col_widths.push_back(0);

			for (size_t c = 0; c < cells.size(); c++)
			{
				const auto w = pf::utf8_codepoint_count(table_layout::trim_cell(cells[c]));
				if (w > _table.col_widths[c]) _table.col_widths[c] = w;
			}
		}

		table_layout::cap_col_widths(_table.col_widths, avail_cols);
	}
};
