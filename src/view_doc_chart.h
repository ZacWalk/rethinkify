// view_doc_chart.h — Chart document view: candlestick rendering for CSV trade data

#pragma once

#include "view_text.h"

struct ohlc_candle
{
	double open = 0, high = 0, low = 0, close = 0;
	double volume = 0;
	int64_t time_ms = 0;
};

class chart_doc_view final : public doc_view
{
	std::vector<ohlc_candle> _candles;

	// Horizontal scroll state (in candle units)
	int _h_offset = 0; // first visible candle index
	int _visible_candles = 0; // candles visible in viewport

	// Minimap interaction
	bool _minimap_tracking = false;
	int _tracking_start_x = 0;
	int _tracking_start_offset = 0;

	// Chart layout constants
	static constexpr int minimap_height = 64;
	static constexpr int candle_width = 7; // pixels per candle (including gap)
	static constexpr int candle_body = 5; // body width
	static constexpr int top_margin = 4;
	static constexpr int bottom_margin = 4;
	static constexpr int label_pad = 4; // padding for superimposed labels

public:
	chart_doc_view(app_events& events) : doc_view(events)
	{
		_sel_margin = false;
		_word_wrap = false;
	}

	~chart_doc_view() override = default;

	void set_document(const document_ptr& d) override
	{
		doc_view::set_document(d);
		rebuild_candles();
		_h_offset = 0;
	}

	void handle_size(pf::window_frame_ptr& window, const pf::isize extent,
	                 pf::measure_context& measure) override
	{
		doc_view::handle_size(window, extent, measure);
		_visible_candles = chart_width() / candle_width;
		clamp_h_offset();
	}

	void on_mouse_wheel(pf::window_frame_ptr& window, const int zDelta) override
	{
		const int step = std::max(1, _visible_candles / 10);
		_h_offset += (zDelta < 0) ? step : -step;
		clamp_h_offset();
		_events.invalidate(invalid::windows);
	}

	bool on_key_down(pf::window_frame_ptr& window, const unsigned int vk) override
	{
		namespace pk = pf::platform_key;
		const bool ctrl = window->is_key_down(pk::Control);

		if (vk == pk::Left)
		{
			_h_offset -= ctrl ? _visible_candles : 1;
			clamp_h_offset();
			_events.invalidate(invalid::windows);
			return true;
		}
		if (vk == pk::Right)
		{
			_h_offset += ctrl ? _visible_candles : 1;
			clamp_h_offset();
			_events.invalidate(invalid::windows);
			return true;
		}
		if (vk == pk::Home)
		{
			_h_offset = 0;
			_events.invalidate(invalid::windows);
			return true;
		}
		if (vk == pk::End)
		{
			_h_offset = max_h_offset();
			_events.invalidate(invalid::windows);
			return true;
		}

		return doc_view::on_key_down(window, vk);
	}

	uint32_t handle_mouse(const pf::window_frame_ptr window, const pf::mouse_message_type msg,
	                      const pf::mouse_params& params) override
	{
		using mt = pf::mouse_message_type;

		const auto rc = client_rect();
		const auto minimap_top = rc.bottom - minimap_height;

		if (msg == mt::left_button_down && params.point.y >= minimap_top)
		{
			_minimap_tracking = true;
			_tracking_start_x = params.point.x;
			_tracking_start_offset = _h_offset;
			window->set_capture();
			return 0;
		}

		if (msg == mt::mouse_move && _minimap_tracking)
		{
			const auto dx = params.point.x - _tracking_start_x;
			const auto total = static_cast<int>(_candles.size());
			const auto cw = chart_width();
			if (cw > 0 && total > 0)
			{
				_h_offset = _tracking_start_offset + dx * total / cw;
				clamp_h_offset();
				_events.invalidate(invalid::windows);
			}
			return 0;
		}

		if (msg == mt::left_button_up && _minimap_tracking)
		{
			_minimap_tracking = false;
			window->release_capture();
			return 0;
		}

		return doc_view::handle_mouse(window, msg, params);
	}

	void recalc_vert_scrollbar() override
	{
		// No vertical scrollbar for chart view
	}

	void layout() override
	{
		// No word-wrap layout needed
	}

protected:
	void update_focus(pf::window_frame_ptr& window) override
	{
		doc_view::update_focus(window);
		stop_caret_blink(window);
	}

	void draw_view(pf::window_frame_ptr& window, pf::draw_context& draw) const override
	{
		const auto rc = client_rect();
		const auto bg = style_to_color(style::normal_bkgnd);
		const auto text_clr = style_to_color(style::normal_text);
		const auto& font = _events.styles().text_font;

		draw.fill_solid_rect(rc, bg);

		if (_candles.empty())
		{
			draw.draw_text(8, 8, rc, u8"No trade data to chart", font, text_clr, bg);
			draw_message_bar(draw);
			return;
		}

		constexpr auto chart_x = 0;
		const auto cw = chart_width();
		const auto chart_top = text_top() + top_margin;
		const auto chart_bottom = rc.bottom - minimap_height - bottom_margin;
		const auto chart_h = chart_bottom - chart_top;

		if (chart_h < 20 || cw < 10) return;

		// Find price range for visible candles
		double min_price = std::numeric_limits<double>::max();
		double max_price = std::numeric_limits<double>::lowest();

		const auto first = _h_offset;
		const auto last = std::min(first + _visible_candles, static_cast<int>(_candles.size()));

		for (int i = first; i < last; i++)
		{
			if (_candles[i].low < min_price) min_price = _candles[i].low;
			if (_candles[i].high > max_price) max_price = _candles[i].high;
		}

		if (max_price <= min_price) max_price = min_price + 1.0;

		// Add 5% padding to price range
		const auto range = max_price - min_price;
		min_price -= range * 0.05;
		max_price += range * 0.05;
		const auto price_range = max_price - min_price;

		// Draw price grid lines and labels
		draw_price_grid(draw, rc, chart_x, chart_top, chart_h, min_price, max_price, price_range,
		                font, text_clr, bg);

		// Draw candles
		constexpr auto green = pf::color_t(0x44, 0xBB, 0x44);
		constexpr auto red = pf::color_t(0xCC, 0x44, 0x44);
		constexpr auto wick_clr = pf::color_t(0xAA, 0xAA, 0xAA);

		for (int i = first; i < last; i++)
		{
			const auto& c = _candles[i];
			const auto x = chart_x + (i - first) * candle_width + 1;

			const auto y_high = chart_top + static_cast<int>((max_price - c.high) / price_range * chart_h);
			const auto y_low = chart_top + static_cast<int>((max_price - c.low) / price_range * chart_h);
			const auto y_open = chart_top + static_cast<int>((max_price - c.open) / price_range * chart_h);
			const auto y_close = chart_top + static_cast<int>((max_price - c.close) / price_range * chart_h);

			const bool bullish = c.close >= c.open;
			const auto body_color = bullish ? green : red;

			// Draw wick (high-low line)
			const auto wick_x = x + candle_body / 2;
			draw.fill_solid_rect(wick_x, y_high, 1, y_low - y_high + 1, wick_clr);

			// Draw body (open-close rectangle)
			const auto body_top = std::min(y_open, y_close);
			const auto body_bottom = std::max(y_open, y_close);
			const auto body_h = std::max(1, body_bottom - body_top);
			draw.fill_solid_rect(x, body_top, candle_body, body_h, body_color);
		}

		// Draw minimap
		draw_minimap(draw, rc, chart_x, font, text_clr, bg);

		draw_message_bar(draw);
	}

private:
	int chart_width() const
	{
		return std::max(0, _view_extent.cx);
	}

	int max_h_offset() const
	{
		return std::max(0, static_cast<int>(_candles.size()) - _visible_candles);
	}

	void clamp_h_offset()
	{
		_h_offset = std::clamp(_h_offset, 0, max_h_offset());
	}

	void draw_price_grid(pf::draw_context& draw, const pf::irect& rc,
	                     const int chart_x, const int chart_top, const int chart_h,
	                     const double min_price, const double max_price, const double price_range,
	                     const pf::font& font, const pf::color_t text_clr, const pf::color_t bg) const
	{
		constexpr auto grid_clr = pf::color_t(0x33, 0x33, 0x33);
		constexpr auto label_clr = pf::color_t(0x99, 0x99, 0x99);
		constexpr auto label_bg = pf::color_t(0x22, 0x22, 0x22);

		// Draw ~5 horizontal grid lines with superimposed price labels
		constexpr int grid_lines = 5;
		for (int i = 0; i <= grid_lines; i++)
		{
			const auto frac = static_cast<double>(i) / grid_lines;
			const auto y = chart_top + static_cast<int>(frac * chart_h);
			const auto price = max_price - frac * price_range;

			// Grid line
			draw.fill_solid_rect(chart_x, y, _view_extent.cx, 1, grid_clr);

			// Price label superimposed at right edge
			auto label = format_price(price);
			const auto label_sz = draw.measure_text(label, font);
			const auto lx = _view_extent.cx - label_sz.cx - label_pad;
			const auto ly = y - _font_extent.cy / 2;
			draw.fill_solid_rect(lx - 2, ly, label_sz.cx + 4, _font_extent.cy, label_bg);
			draw.draw_text(lx, ly, rc, label, font, label_clr, label_bg);
		}
	}

	void draw_minimap(pf::draw_context& draw, const pf::irect& rc,
	                  const int chart_x, const pf::font& font,
	                  const pf::color_t text_clr, const pf::color_t bg) const
	{
		const auto total = static_cast<int>(_candles.size());
		if (total == 0) return;

		const auto minimap_top = rc.bottom - minimap_height;
		const auto minimap_w = chart_width();
		constexpr auto minimap_bg = pf::color_t(0x1A, 0x1A, 0x1A);

		// Minimap background
		draw.fill_solid_rect(chart_x, minimap_top, minimap_w, minimap_height, minimap_bg);

		// Separator line
		draw.fill_solid_rect(chart_x, minimap_top, minimap_w, 1, pf::color_t(0x55, 0x55, 0x55));

		// Find global price range
		double min_price = std::numeric_limits<double>::max();
		double max_price = std::numeric_limits<double>::lowest();
		for (const auto& c : _candles)
		{
			if (c.low < min_price) min_price = c.low;
			if (c.high > max_price) max_price = c.high;
		}
		if (max_price <= min_price) max_price = min_price + 1.0;
		const auto price_range = max_price - min_price;

		// Draw minimap candles as single-pixel columns
		constexpr auto map_h = minimap_height - 2; // leave border
		const auto map_top = minimap_top + 1;

		constexpr auto mini_green = pf::color_t(0x33, 0x88, 0x33);
		constexpr auto mini_red = pf::color_t(0x88, 0x33, 0x33);

		for (int px = 0; px < minimap_w; px++)
		{
			// Map pixel to candle range
			const auto c_start = px * total / minimap_w;
			const auto c_end = std::min((px + 1) * total / minimap_w, total);
			if (c_start >= c_end) continue;

			double seg_high = std::numeric_limits<double>::lowest();
			double seg_low = std::numeric_limits<double>::max();
			const double seg_open = _candles[c_start].open;
			const double seg_close = _candles[c_end - 1].close;

			for (int j = c_start; j < c_end; j++)
			{
				if (_candles[j].high > seg_high) seg_high = _candles[j].high;
				if (_candles[j].low < seg_low) seg_low = _candles[j].low;
			}

			const auto y1 = map_top + static_cast<int>((max_price - seg_high) / price_range * map_h);
			const auto y2 = map_top + static_cast<int>((max_price - seg_low) / price_range * map_h);
			const auto h = std::max(1, y2 - y1);
			const auto clr = (seg_close >= seg_open) ? mini_green : mini_red;
			draw.fill_solid_rect(chart_x + px, y1, 1, h, clr);
		}

		// Draw viewport indicator
		const auto vp_x = _h_offset * minimap_w / total;
		const auto vp_w = std::max(4, _visible_candles * minimap_w / total);
		const auto indicator_clr = _minimap_tracking
			                           ? pf::color_t(0x22, 0x88, 0xEE)
			                           : pf::color_t(0x44, 0x66, 0xAA);

		// Top and bottom edges of viewport indicator
		draw.fill_solid_rect(chart_x + vp_x, minimap_top, vp_w, 2, indicator_clr);
		draw.fill_solid_rect(chart_x + vp_x, minimap_top + minimap_height - 2, vp_w, 2, indicator_clr);
		// Left and right edges
		draw.fill_solid_rect(chart_x + vp_x, minimap_top, 2, minimap_height, indicator_clr);
		draw.fill_solid_rect(chart_x + vp_x + vp_w - 2, minimap_top, 2, minimap_height, indicator_clr);
	}

	static std::u8string format_price(const double price)
	{
		// Format price to 2 decimal places
		char buf[32];
		snprintf(buf, sizeof(buf), "%.2f", price);
		std::u8string result;
		for (const char c : std::string_view(buf))
			result.push_back(static_cast<char8_t>(c));
		return result;
	}

	// Check if a CSV cell looks numeric (starts with digit, minus, or dot)
	static bool is_numeric_cell(const std::u8string_view sv)
	{
		const auto t = table_layout::trim_cell(sv);
		if (t.empty()) return false;
		const auto ch = t[0];
		return (ch >= u8'0' && ch <= u8'9') || ch == u8'-' || ch == u8'.';
	}

	void rebuild_candles()
	{
		_candles.clear();
		if (!_doc || _doc->empty()) return;

		// Inspect first line to detect format:
		// - If first cell is non-numeric, assume header row with named columns (trade tick data)
		// - If all cells are numeric, assume headerless pre-aggregated OHLC candle data
		std::u8string first_line;
		(*_doc)[0].render(first_line);
		const auto first_cells = table_layout::split_csv_cells(first_line);

		const bool has_header = !first_cells.empty() && !is_numeric_cell(first_cells[0]);

		if (has_header)
			rebuild_from_trades(first_cells);
		else
			rebuild_from_ohlc_rows();

		_visible_candles = chart_width() / candle_width;
		clamp_h_offset();
	}

	// Format 1: Trade tick CSV with header (e.g. id,price,qty,quote_qty,time,is_buyer_maker)
	void rebuild_from_trades(const std::vector<std::u8string_view>& headers)
	{
		int col_price = -1, col_qty = -1, col_time = -1;
		for (size_t i = 0; i < headers.size(); i++)
		{
			const auto h = table_layout::trim_cell(headers[i]);
			if (h == u8"price") col_price = static_cast<int>(i);
			else if (h == u8"qty") col_qty = static_cast<int>(i);
			else if (h == u8"time") col_time = static_cast<int>(i);
		}

		if (col_price < 0) return;

		struct trade
		{
			double price = 0;
			double qty = 0;
			int64_t time_ms = 0;
		};

		std::vector<trade> trades;
		trades.reserve(_doc->size());

		std::u8string line_text;
		for (int i = 1; i < static_cast<int>(_doc->size()); i++)
		{
			(*_doc)[i].render(line_text);
			const auto cells = table_layout::split_csv_cells(line_text);

			trade t;
			if (col_price >= 0 && col_price < static_cast<int>(cells.size()))
				t.price = parse_double(cells[col_price]);
			if (col_qty >= 0 && col_qty < static_cast<int>(cells.size()))
				t.qty = parse_double(cells[col_qty]);
			if (col_time >= 0 && col_time < static_cast<int>(cells.size()))
				t.time_ms = parse_int64(cells[col_time]);

			if (t.price > 0)
				trades.push_back(t);
		}

		if (trades.empty()) return;

		// Determine candle interval based on data span
		const auto time_span = trades.back().time_ms - trades.front().time_ms;
		int64_t interval_ms;
		if (time_span <= 0)
			interval_ms = 60000;
		else if (time_span < 3600000LL)
			interval_ms = 10000;
		else if (time_span < 86400000LL)
			interval_ms = 60000;
		else
			interval_ms = 300000;

		auto candle_start = trades[0].time_ms;
		ohlc_candle current;
		current.open = trades[0].price;
		current.high = trades[0].price;
		current.low = trades[0].price;
		current.close = trades[0].price;
		current.volume = trades[0].qty;
		current.time_ms = candle_start;

		for (size_t i = 1; i < trades.size(); i++)
		{
			const auto& t = trades[i];

			if (t.time_ms >= candle_start + interval_ms)
			{
				_candles.push_back(current);

				candle_start += interval_ms;
				while (t.time_ms >= candle_start + interval_ms)
					candle_start += interval_ms;

				current = {};
				current.open = t.price;
				current.high = t.price;
				current.low = t.price;
				current.close = t.price;
				current.volume = t.qty;
				current.time_ms = candle_start;
			}
			else
			{
				if (t.price > current.high) current.high = t.price;
				if (t.price < current.low) current.low = t.price;
				current.close = t.price;
				current.volume += t.qty;
			}
		}

		_candles.push_back(current);
	}

	// Format 2: Pre-aggregated OHLC candle CSV without header
	// Columns: time_seconds, low, high, open, close, volume
	// Also supports header variants: time/timestamp, open, high, low, close, volume
	void rebuild_from_ohlc_rows()
	{
		const auto line_count = static_cast<int>(_doc->size());
		_candles.reserve(line_count);

		// Detect if there's a header by checking first line cell count and format
		// For headerless files the first row is data; for files with named OHLC headers
		// we check common column names
		std::u8string line_text;
		(*_doc)[0].render(line_text);
		const auto probe = table_layout::split_csv_cells(line_text);

		// Default column mapping for Coinbase-style headerless: time_s, low, high, open, close, volume
		int col_time = 0, col_low = 1, col_high = 2, col_open = 3, col_close = 4, col_vol = 5;
		int start_row = 0;
		constexpr bool time_in_seconds = true;

		// Check if first row might be a named header (e.g. "time,open,high,low,close,volume")
		if (!probe.empty() && !is_numeric_cell(probe[0]))
		{
			col_time = col_low = col_high = col_open = col_close = col_vol = -1;
			for (size_t i = 0; i < probe.size(); i++)
			{
				const auto h = table_layout::trim_cell(probe[i]);
				if (h == u8"time" || h == u8"timestamp" || h == u8"date") col_time = static_cast<int>(i);
				else if (h == u8"open") col_open = static_cast<int>(i);
				else if (h == u8"high") col_high = static_cast<int>(i);
				else if (h == u8"low") col_low = static_cast<int>(i);
				else if (h == u8"close") col_close = static_cast<int>(i);
				else if (h == u8"volume" || h == u8"vol") col_vol = static_cast<int>(i);
			}
			if (col_open < 0 || col_close < 0) return; // Need at least open+close
			start_row = 1;
		}

		for (int i = start_row; i < line_count; i++)
		{
			(*_doc)[i].render(line_text);
			if (line_text.empty()) continue;
			const auto cells = table_layout::split_csv_cells(line_text);

			const auto ncells = static_cast<int>(cells.size());
			ohlc_candle c;

			if (col_time >= 0 && col_time < ncells)
			{
				const auto t = parse_int64(cells[col_time]);
				c.time_ms = t * 1000;
			}
			if (col_low >= 0 && col_low < ncells) c.low = parse_double(cells[col_low]);
			if (col_high >= 0 && col_high < ncells) c.high = parse_double(cells[col_high]);
			if (col_open >= 0 && col_open < ncells) c.open = parse_double(cells[col_open]);
			if (col_close >= 0 && col_close < ncells) c.close = parse_double(cells[col_close]);
			if (col_vol >= 0 && col_vol < ncells) c.volume = parse_double(cells[col_vol]);

			// Fill in missing high/low from open/close if not provided
			if (c.high == 0 && c.low == 0)
			{
				c.high = std::max(c.open, c.close);
				c.low = std::min(c.open, c.close);
			}

			if (c.open > 0 || c.close > 0)
				_candles.push_back(c);
		}
	}

	static double parse_double(const std::u8string_view sv)
	{
		const std::string s(sv.begin(), sv.end());
		char* end = nullptr;
		const auto v = strtod(s.c_str(), &end);
		return (end != s.c_str()) ? v : 0.0;
	}

	static int64_t parse_int64(const std::u8string_view sv)
	{
		const std::string s(sv.begin(), sv.end());
		char* end = nullptr;
		const auto v = strtoll(s.c_str(), &end, 10);
		return (end != s.c_str()) ? v : 0;
	}
};
