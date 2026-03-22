// view_doc_hex.h — Hex document view: offset, hex bytes, and ASCII columns

#pragma once

#include "view_text.h"

class hex_doc_view final : public doc_view
{
	static constexpr int bytes_per_line = 16;

	// Column layout (in characters):
	//   offset: 8 hex digits + 2 spaces = 10 chars
	//   hex:    16 * 3 + 1 (gap at byte 8) = 49 chars  + 1 trailing space = 50
	//   ascii:  "|" + 16 chars + "|" = 18 chars
	// Total: ~78 chars

public:
	hex_doc_view(app_events& events) : doc_view(events)
	{
	}

	~hex_doc_view() override = default;

protected:
	uint32_t handle_mouse(const pf::window_frame_ptr window, const pf::mouse_message_type msg,
	                      const pf::mouse_params& params) override
	{
		using mt = pf::mouse_message_type;

		if (msg == mt::left_button_down)
			window->set_focus();

		const auto rc = scrollbar_rect();

		if (_vscroll.handle_mouse(msg, params.point, rc, window, [this](const int pos)
		{
			set_scroll_pixel(pos * _font_extent.cy);
		}))
			return 0;

		return doc_view::handle_mouse(window, msg, params);
	}

	void draw_view(pf::window_frame_ptr& window, pf::draw_context& draw) const override
	{
		const auto styles = _events.styles();
		const auto rcClient = client_rect();
		const auto line_count = static_cast<int>(_doc->size());
		const auto pad_top = text_top();
		const auto cx = _font_extent.cx;
		const auto cy = _font_extent.cy;

		const auto bg = style_to_color(style::normal_bkgnd);
		const auto offset_color = style_to_color(style::code_number);
		const auto hex_color = style_to_color(style::code_keyword);
		const auto ascii_color = style_to_color(style::code_string);
		const auto separator_color = style_to_color(style::code_comment);

		std::u8string line_text;

		draw.fill_solid_rect(rcClient, bg);

		// Total content width: offset(10) + hex(50) + separator(1) + ascii(16) + separator(1) = 78 chars
		constexpr int content_chars = 10 + 50 + 1 + bytes_per_line + 1;
		const auto content_width = content_chars * cx;
		const auto left_margin = std::max(cx, (rcClient.right - content_width) / 2);

		auto nCurrentLine = std::max(0, _font_extent.cy > 0
			                                ? (_scroll_offset.y - _font_extent.cy) / _font_extent.cy
			                                : 0);
		auto y = line_content_offset(nCurrentLine) - _scroll_offset.y + pad_top;

		while (y < rcClient.bottom && nCurrentLine < line_count)
		{
			const auto& line = (*_doc)[nCurrentLine];
			line.render(line_text);

			const auto num_bytes = static_cast<int>(line_text.size());
			const auto file_offset = nCurrentLine * bytes_per_line;

			int x = left_margin;

			// --- Offset column ---
			const auto offset_str = pf::format(u8"{:08X}", file_offset);
			{
				const pf::irect clip(x, y, x + 8 * cx, y + cy);
				draw.draw_text(x, y, clip, offset_str, styles.text_font, offset_color, bg);
			}
			x += 10 * cx; // 8 digits + 2 space gap

			// --- Hex bytes column ---
			const auto hex_start_x = x;
			for (int i = 0; i < bytes_per_line; i++)
			{
				if (i == 8)
					x += cx; // extra gap between byte 7 and 8

				if (i < num_bytes)
				{
					const auto byte_val = static_cast<uint8_t>(line_text[i]);
					const auto hex = pf::format(u8"{:02X} ", byte_val);
					const pf::irect clip(x, y, x + 3 * cx, y + cy);
					draw.draw_text(x, y, clip, hex, styles.text_font, hex_color, bg);
				}
				x += 3 * cx;
			}
			x += cx; // trailing space

			// --- Separator ---
			{
				const pf::irect clip(x, y, x + cx, y + cy);
				draw.draw_text(x, y, clip, u8"|", styles.text_font, separator_color, bg);
			}
			x += cx;

			// --- ASCII column ---
			std::u8string ascii_str;
			ascii_str.reserve(bytes_per_line);
			for (int i = 0; i < num_bytes; i++)
			{
				const auto c = static_cast<uint8_t>(line_text[i]);
				ascii_str += c >= 32 && c < 127 ? static_cast<char8_t>(c) : L'.';
			}
			// Pad to full width
			for (int i = num_bytes; i < bytes_per_line; i++)
				ascii_str += L' ';

			{
				const pf::irect clip(x, y, x + bytes_per_line * cx, y + cy);
				draw.draw_text(x, y, clip, ascii_str, styles.text_font, ascii_color, bg);
			}
			x += bytes_per_line * cx;

			// --- Closing separator ---
			{
				const pf::irect clip(x, y, x + cx, y + cy);
				draw.draw_text(x, y, clip, u8"|", styles.text_font, separator_color, bg);
			}

			nCurrentLine++;
			y += cy;
		}

		_vscroll.draw(draw, scrollbar_rect());
	}
};
