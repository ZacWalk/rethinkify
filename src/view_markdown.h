#pragma once

// view_markdown.h — Markdown view: read-only rendered markdown display

#include "view_text.h"

class markdown_view final : public text_view
{
	isize _heading_font_extent[3] = {}; // cached char sizes for h1, h2, h3

public:
	markdown_view(app_events& events) : text_view(events)
	{
		_sel_margin = false;
		_word_wrap = true;
	}

	~markdown_view() override = default;

	void on_size(pf::window_frame_ptr& window, const isize extent,
	             pf::measure_context& measure) override
	{
		text_view::on_size(window, extent, measure);

		for (int level = 1; level <= 3; ++level)
			_heading_font_extent[level - 1] = measure.measure_char(font_for_heading(level));
	}

protected:
	void update_focus(pf::window_frame_ptr& window) override
	{
		const bool was_focused = _is_focused;
		text_view_base::update_focus(window);

		if (_is_focused != was_focused)
		{
			invalidate_selection(window);

			if (!_is_focused && _drag_selection)
			{
				window->release_capture();
				window->kill_timer(_drag_sel_timer);
				_drag_selection = false;
			}
		}
	}

	void draw_view(pf::window_frame_ptr& window, pf::draw_context& draw) const override
	{
		const auto rcClient = client_rect();
		const auto line_count = static_cast<int>(_doc->size());
		const auto pad_top = text_top();

		draw.fill_solid_rect(rcClient, style_to_color(style::normal_bkgnd));

		const auto left_pad = _font_extent.cx * 2;
		const auto avail_width = rcClient.right - left_pad;
		auto y = pad_top;
		auto nCurrentLine = _char_offset.cy;

		while (y < rcClient.bottom && nCurrentLine < line_count)
		{
			const auto& line = (*_doc)[nCurrentLine];
			const auto line_text = line.view();

			const auto info = parse_line(line_text);
			const auto font = font_for_heading(info.heading_level);
			auto font_cx = _font_extent.cx;
			auto font_cy = _font_extent.cy;

			if (info.heading_level > 0)
			{
				const auto& sz = _heading_font_extent[info.heading_level - 1];
				font_cx = sz.cx;
				font_cy = sz.cy;
			}

			const auto chars_per_row = font_cx > 0 ? std::max(1, avail_width / font_cx) : 1;
			const auto line_len = static_cast<int>(line_text.size());

			const auto breaks = calc_word_breaks(line_text, chars_per_row,
			                                     [](int, int) { return 1; });

			const auto num_rows = static_cast<int>(breaks.size()) + 1;

			for (int row = 0; row < num_rows && y < rcClient.bottom; row++)
			{
				const auto row_start = row == 0 ? 0 : breaks[row - 1];
				const auto row_end = row < static_cast<int>(breaks.size()) ? breaks[row] : line_len;

				draw_md_line(draw, irect(left_pad, y, rcClient.right, y + font_cy),
				             line_text, info, font, font_cx, nCurrentLine, row_start, row_end);
				y += font_cy;
			}

			nCurrentLine++;
		}

		_vscroll.draw(draw, rcClient);
		draw_message_bar(draw);
	}

private:
	enum class span_type
	{
		plain,
		marker,
		bold,
		italic,
		bold_italic,
		link_text,
		link_url,
		bullet
	};

	struct md_span
	{
		int start;
		int length;
		span_type type;
	};

	struct line_info
	{
		int heading_level = 0; // 0=none, 1-3
		bool is_list = false;
		int content_start = 0;
		std::vector<md_span> spans;
	};

	static color_t color_for_span(const span_type type, const int heading_level)
	{
		switch (type)
		{
		case span_type::marker:
			return style_to_color(style::md_marker);
		case span_type::bold:
			return style_to_color(heading_level > 0
				                      ? static_cast<style>(static_cast<int>(style::md_heading1) + heading_level - 1)
				                      : style::md_bold);
		case span_type::italic:
			return style_to_color(style::md_italic);
		case span_type::bold_italic:
			return style_to_color(style::md_bold);
		case span_type::link_text:
			return style_to_color(style::md_link_text);
		case span_type::link_url:
			return style_to_color(style::md_link_url);
		case span_type::bullet:
			return style_to_color(style::md_bullet);
		case span_type::plain:
		default:
			if (heading_level == 1) return style_to_color(style::md_heading1);
			if (heading_level == 2) return style_to_color(style::md_heading2);
			if (heading_level == 3) return style_to_color(style::md_heading3);
			return style_to_color(style::normal_text);
		}
	}

	pf::font font_for_heading(const int level) const
	{
		const auto styles = _events.styles();
		switch (level)
		{
		case 1: return {styles.text_font_height + 12, pf::font_name::consolas};
		case 2: return {styles.text_font_height + 8, pf::font_name::consolas};
		case 3: return {styles.text_font_height + 4, pf::font_name::consolas};
		default: return styles.text_font;
		}
	}

	static line_info parse_line(const std::wstring_view text)
	{
		line_info info;
		if (text.empty())
			return info;

		int pos = 0;
		const auto len = static_cast<int>(text.size());

		// Check for heading
		if (text[0] == L'#')
		{
			int level = 0;
			while (pos < len && text[pos] == L'#' && level < 3)
			{
				level++;
				pos++;
			}
			if (pos < len && text[pos] == L' ')
			{
				info.heading_level = level;
				info.spans.push_back({0, pos + 1, span_type::marker});
				info.content_start = pos + 1;
				parse_inline(text, pos + 1, info);
				return info;
			}
			pos = 0; // not a valid heading
		}

		// Check for unordered list
		if (len >= 2 && (text[0] == L'-' || text[0] == L'*') && text[1] == L' ')
		{
			info.is_list = true;
			info.spans.push_back({0, 1, span_type::bullet});
			info.spans.push_back({1, 1, span_type::plain});
			info.content_start = 2;
			parse_inline(text, 2, info);
			return info;
		}

		// Check for ordered list (digit(s) followed by ". ")
		if (len >= 3 && text[0] >= L'0' && text[0] <= L'9')
		{
			int p = 0;
			while (p < len && text[p] >= L'0' && text[p] <= L'9') p++;
			if (p < len - 1 && text[p] == L'.' && text[p + 1] == L' ')
			{
				info.is_list = true;
				info.spans.push_back({0, p + 1, span_type::bullet});
				info.spans.push_back({p + 1, 1, span_type::plain});
				info.content_start = p + 2;
				parse_inline(text, p + 2, info);
				return info;
			}
		}

		// Plain line with inline formatting
		parse_inline(text, 0, info);
		return info;
	}

	static void parse_inline(const std::wstring_view text, const int start, line_info& info)
	{
		const auto len = static_cast<int>(text.size());
		int pos = start;
		int plain_start = pos;

		auto flush_plain = [&](const int end)
		{
			if (end > plain_start)
				info.spans.push_back({plain_start, end - plain_start, span_type::plain});
		};

		while (pos < len)
		{
			// Bold+italic: ***text*** or ___text___
			if (pos + 2 < len && ((text[pos] == L'*' && text[pos + 1] == L'*' && text[pos + 2] == L'*') ||
				(text[pos] == L'_' && text[pos + 1] == L'_' && text[pos + 2] == L'_')))
			{
				const auto marker = text[pos];
				const auto end = find_marker(text, pos + 3, marker, 3);
				if (end >= 0)
				{
					flush_plain(pos);
					info.spans.push_back({pos, 3, span_type::marker});
					info.spans.push_back({pos + 3, end - pos - 3, span_type::bold_italic});
					info.spans.push_back({end, 3, span_type::marker});
					pos = end + 3;
					plain_start = pos;
					continue;
				}
			}

			// Bold: **text** or __text__
			if (pos + 1 < len && ((text[pos] == L'*' && text[pos + 1] == L'*') ||
				(text[pos] == L'_' && text[pos + 1] == L'_')))
			{
				const auto marker = text[pos];
				const auto end = find_marker(text, pos + 2, marker, 2);
				if (end >= 0)
				{
					flush_plain(pos);
					info.spans.push_back({pos, 2, span_type::marker});
					info.spans.push_back({pos + 2, end - pos - 2, span_type::bold});
					info.spans.push_back({end, 2, span_type::marker});
					pos = end + 2;
					plain_start = pos;
					continue;
				}
			}

			// Italic: *text* or _text_
			if (text[pos] == L'*' || text[pos] == L'_')
			{
				const auto marker = text[pos];
				const auto end = find_marker(text, pos + 1, marker, 1);
				if (end >= 0)
				{
					flush_plain(pos);
					info.spans.push_back({pos, 1, span_type::marker});
					info.spans.push_back({pos + 1, end - pos - 1, span_type::italic});
					info.spans.push_back({end, 1, span_type::marker});
					pos = end + 1;
					plain_start = pos;
					continue;
				}
			}

			// Link: [text](url)
			if (text[pos] == L'[')
			{
				const auto close_bracket = find_char(text, pos + 1, L']');
				if (close_bracket >= 0 && close_bracket + 1 < len && text[close_bracket + 1] == L'(')
				{
					const auto close_paren = find_char(text, close_bracket + 2, L')');
					if (close_paren >= 0)
					{
						flush_plain(pos);
						info.spans.push_back({pos, 1, span_type::marker}); // [
						info.spans.push_back({pos + 1, close_bracket - pos - 1, span_type::link_text});
						info.spans.push_back({close_bracket, 2, span_type::marker}); // ](
						info.spans.push_back({close_bracket + 2, close_paren - close_bracket - 2, span_type::link_url});
						info.spans.push_back({close_paren, 1, span_type::marker}); // )
						pos = close_paren + 1;
						plain_start = pos;
						continue;
					}
				}
			}

			pos++;
		}

		flush_plain(len);
	}

	static int find_marker(const std::wstring_view text, const int start, const wchar_t ch, const int count)
	{
		const auto len = static_cast<int>(text.size());
		if (start > len - count) return -1;

		auto pos = static_cast<size_t>(start);
		while (pos != std::wstring_view::npos && static_cast<int>(pos) <= len - count)
		{
			pos = text.find(ch, pos);
			if (pos == std::wstring_view::npos || static_cast<int>(pos) > len - count)
				return -1;

			bool match = true;
			for (int j = 1; j < count; j++)
			{
				if (text[pos + j] != ch)
				{
					match = false;
					break;
				}
			}
			if (match)
				return static_cast<int>(pos);
			pos++;
		}
		return -1;
	}

	static int find_char(const std::wstring_view text, const int start, const wchar_t ch)
	{
		const auto pos = text.find(ch, start);
		return pos == std::wstring_view::npos ? -1 : static_cast<int>(pos);
	}

	void draw_md_line(pf::draw_context& draw, const irect& rc,
	                  const std::wstring_view line_text, const line_info& info,
	                  const pf::font& font, const int font_cx, const int line_index,
	                  const int row_start = 0, int row_end = -1) const
	{
		if (row_end < 0) row_end = static_cast<int>(line_text.size());

		const auto bg_color = style_to_color(style::normal_bkgnd);
		ipoint origin(rc.left, rc.top);

		if (info.spans.empty())
			return;

		const auto sel = _doc->selection();
		int sel_begin = 0, sel_end = 0;

		if (!sel.empty())
		{
			const auto line_len = static_cast<int>(line_text.size());
			sel_begin = sel._start.y > line_index
				            ? line_len
				            : sel._start.y == line_index
				            ? clamp(sel._start.x, 0, line_len)
				            : 0;
			sel_end = sel._end.y > line_index
				          ? line_len
				          : sel._end.y == line_index
				          ? clamp(sel._end.x, 0, line_len)
				          : 0;
		}

		const auto sel_text_color = style_to_color(style::sel_text);
		const auto sel_bg_color = style_to_color(style::sel_bkgnd);

		for (const auto& span : info.spans)
		{
			const auto span_end = span.start + span.length;

			// Skip spans entirely outside this visual row
			if (span_end <= row_start || span.start >= row_end)
				continue;
			if (span.length <= 0 || span.start >= static_cast<int>(line_text.size()))
				continue;

			// Clip span to this visual row
			const auto vis_start = std::max(span.start, row_start);
			const auto vis_end = std::min(span_end, row_end);
			const auto vis_len = vis_end - vis_start;

			const auto color = color_for_span(span.type, info.heading_level);
			const auto text = line_text.substr(vis_start, vis_len);

			// Selection boundaries relative to this visible portion
			const auto s0 = clamp(sel_begin - vis_start, 0, vis_len);
			const auto s1 = clamp(sel_end - vis_start, 0, vis_len);

			auto clip = rc;
			clip.left = origin.x;

			if (s0 > 0 && origin.x < rc.right)
			{
				clip.left = origin.x;
				draw.draw_text(origin.x, origin.y, clip, text.substr(0, s0),
				               font, color, bg_color);
				origin.x += font_cx * s0;
			}
			if (s1 > s0 && origin.x < rc.right)
			{
				clip.left = origin.x;
				draw.draw_text(origin.x, origin.y, clip, text.substr(s0, s1 - s0),
				               font, sel_text_color, sel_bg_color);
				origin.x += font_cx * (s1 - s0);
			}
			if (s1 < vis_len && origin.x < rc.right)
			{
				clip.left = origin.x;
				draw.draw_text(origin.x, origin.y, clip, text.substr(s1),
				               font, color, bg_color);
				origin.x += font_cx * (vis_len - s1);
			}
		}
	}
};
