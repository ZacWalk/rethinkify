#pragma once

// view_hex.h — Binary hex view: read-only hex display derived from text_view_base
// Each document line stores raw bytes (one byte per wchar_t, values 0x00-0xFF).
// This view renders offset | hex bytes | ASCII columns.

#include "view_text_base.h"

class hex_view final : public text_view_base
{
	static constexpr int bytes_per_line = 16;

	// Column layout (in characters):
	//   offset: 8 hex digits + 2 spaces = 10 chars
	//   hex:    16 * 3 + 1 (gap at byte 8) = 49 chars  + 1 trailing space = 50
	//   ascii:  "|" + 16 chars + "|" = 18 chars
	// Total: ~78 chars

public:
	hex_view(app_events& events) : text_view_base(events)
	{
	}

	~hex_view() override = default;

protected:
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

		draw.fill_solid_rect(rcClient, bg);

		auto y = pad_top;
		auto nCurrentLine = _char_offset.cy;

		while (y < rcClient.bottom && nCurrentLine < line_count)
		{
			const auto& line = (*_doc)[nCurrentLine];
			const auto num_bytes = static_cast<int>(line.size());
			const auto file_offset = nCurrentLine * bytes_per_line;

			int x = cx; // left padding

			// --- Offset column ---
			const auto offset_str = std::format(L"{:08X}", file_offset);
			{
				const irect clip(x, y, x + 8 * cx, y + cy);
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
					const auto byte_val = static_cast<uint8_t>(line[i]);
					const auto hex = std::format(L"{:02X} ", byte_val);
					const irect clip(x, y, x + 3 * cx, y + cy);
					draw.draw_text(x, y, clip, hex, styles.text_font, hex_color, bg);
				}
				x += 3 * cx;
			}
			x += cx; // trailing space

			// --- Separator ---
			{
				const irect clip(x, y, x + cx, y + cy);
				draw.draw_text(x, y, clip, L"|", styles.text_font, separator_color, bg);
			}
			x += cx;

			// --- ASCII column ---
			std::wstring ascii_str;
			ascii_str.reserve(bytes_per_line);
			for (int i = 0; i < num_bytes; i++)
			{
				const auto c = static_cast<uint8_t>(line[i]);
				ascii_str += c >= 32 && c < 127 ? static_cast<wchar_t>(c) : L'.';
			}
			// Pad to full width
			for (int i = num_bytes; i < bytes_per_line; i++)
				ascii_str += L' ';

			{
				const irect clip(x, y, x + bytes_per_line * cx, y + cy);
				draw.draw_text(x, y, clip, ascii_str, styles.text_font, ascii_color, bg);
			}
			x += bytes_per_line * cx;

			// --- Closing separator ---
			{
				const irect clip(x, y, x + cx, y + cy);
				draw.draw_text(x, y, clip, L"|", styles.text_font, separator_color, bg);
			}

			nCurrentLine++;
			y += cy;
		}

		_vscroll.draw(draw, rcClient);
	}
};
