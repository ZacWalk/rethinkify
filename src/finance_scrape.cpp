// finance_scrape.cpp — Stock quote fetching: Yahoo Finance API, URL parsing, markdown generation

#include "pch.h"
#include "platform.h"
#include "finance.h"

// ── URL Parsing ────────────────────────────────────────────────────────

static constexpr auto chrome_user_agent =
	u8"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36";

struct parsed_url
{
	std::u8string host;
	std::u8string path;
	bool secure = true;
	int port = 0;
};

static parsed_url parse_url(const std::u8string_view url)
{
	parsed_url result;
	const auto s = url;

	// Strip scheme
	std::u8string_view sv(s);
	if (sv.starts_with(u8"https://"))
	{
		result.secure = true;
		sv.remove_prefix(8);
	}
	else if (sv.starts_with(u8"http://"))
	{
		result.secure = false;
		sv.remove_prefix(7);
	}

	// Split host and path
	const auto slash = sv.find('/');
	const std::u8string_view host_part = slash != std::u8string_view::npos ? sv.substr(0, slash) : sv;
	const std::u8string_view path_part = slash != std::u8string_view::npos ? sv.substr(slash) : u8"/";

	// Check for port in host
	const auto colon = host_part.find(':');
	if (colon != std::u8string_view::npos)
	{
		result.host = std::u8string(host_part.data(), colon);
		try { result.port = pf::stoi(host_part.substr(colon + 1)); }
		catch (...)
		{
		}
	}
	else
	{
		result.host = std::u8string(host_part.data(), host_part.size());
	}

	result.path = std::u8string(path_part.data(), path_part.size());
	return result;
}

// ── HTTP Fetching ──────────────────────────────────────────────────────

pf::web_response fetch_page(const std::u8string_view url)
{
	const auto parsed = parse_url(url);
	const auto host = pf::connect_to_host(parsed.host, parsed.secure, parsed.port, chrome_user_agent);
	if (!host) return {};

	pf::web_request req;
	req.path = parsed.path;
	req.verb = pf::web_request_verb::GET;
	req.headers = {
		{u8"Accept", u8"text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8"},
		{u8"Accept-Language", u8"en-US,en;q=0.5"},
	};

	return pf::send_request(host, req);
}

std::u8string fetch_yahoo_crumb()
{
	const auto response = fetch_page(u8"https://query2.finance.yahoo.com/v1/test/getcrumb");
	if (response.status_code == 200 && !response.body.empty())
		return response.body;
	return {};
}

std::u8string fetch_quote_summary(const std::u8string& ticker, const std::u8string& crumb)
{
	if (crumb.empty()) return {};
	const auto url = pf::format(
		u8"https://query1.finance.yahoo.com/v10/finance/quoteSummary/{}?modules=assetProfile,financialData,earningsTrend,earningsHistory,recommendationTrend&crumb={}",
		ticker, crumb);
	const auto response = fetch_page(url);
	if (response.status_code == 200)
		return response.body;
	return {};
}

// ── JSON Value Extraction ──────────────────────────────────────────────

// Extract a formatted value from Yahoo Finance JSON data.
// Looks for "key":{"raw":N,"fmt":"VALUE"} and returns VALUE.
// Also handles "key":N (returns the number as string).
static std::u8string extract_fmt_value(const std::u8string& text, const std::u8string_view key)
{
	const auto search = u8"\"" + std::u8string(key) + u8"\"";
	auto pos = text.find(search);
	if (pos == std::u8string::npos) return {};

	pos += search.size();

	// Skip whitespace and colon
	while (pos < text.size() && (text[pos] == L' ' || text[pos] == L'\t')) pos++;
	if (pos >= text.size() || text[pos] != L':') return {};
	pos++;
	while (pos < text.size() && (text[pos] == L' ' || text[pos] == L'\t')) pos++;

	if (pos >= text.size()) return {};

	if (text[pos] == L'{')
	{
		// Nested object — look for "fmt":"VALUE" within the next ~500 chars
		const auto end = std::min(pos + 500, text.size());
		const auto fmt_key = std::u8string(u8"\"fmt\":");
		auto fmt_pos = text.find(fmt_key, pos);
		if (fmt_pos == std::u8string::npos || fmt_pos > end) return {};

		fmt_pos += fmt_key.size();
		while (fmt_pos < end && (text[fmt_pos] == L' ' || text[fmt_pos] == L'\t')) fmt_pos++;
		if (fmt_pos >= end) return {};

		if (text[fmt_pos] == L'"')
		{
			fmt_pos++;
			const auto close = text.find(L'"', fmt_pos);
			if (close == std::u8string::npos || close > end) return {};
			return text.substr(fmt_pos, close - fmt_pos);
		}
	}

	// Direct numeric value
	if (text[pos] == L'-' || text[pos] == L'+' || iswdigit(text[pos]))
	{
		const auto start = pos;
		while (pos < text.size() && (iswdigit(text[pos]) || text[pos] == L'.' || text[pos] == L'-'
			|| text[pos] == L'+' || text[pos] == L'e' || text[pos] == L'E'))
			pos++;
		return text.substr(start, pos - start);
	}

	return {};
}

// Extract a direct string value: "key":"VALUE"
static std::u8string extract_string_value(const std::u8string& text, const std::u8string_view key)
{
	const auto search = u8"\"" + std::u8string(key) + u8"\"";
	auto pos = text.find(search);
	if (pos == std::u8string::npos) return {};

	pos += search.size();

	while (pos < text.size() && (text[pos] == L' ' || text[pos] == L'\t')) pos++;
	if (pos >= text.size() || text[pos] != L':') return {};
	pos++;
	while (pos < text.size() && (text[pos] == L' ' || text[pos] == L'\t')) pos++;

	if (pos >= text.size() || text[pos] != L'"') return {};
	pos++;

	const auto close = text.find(L'"', pos);
	if (close == std::u8string::npos) return {};
	return text.substr(pos, close - pos);
}

// ── Markdown Generation ────────────────────────────────────────────────

// Find matching brace/bracket in JSON text starting at opening brace/bracket position
static size_t find_matching_brace(const std::u8string& text, const size_t open_pos)
{
	if (open_pos >= text.size()) return std::u8string::npos;
	const char8_t open_ch = text[open_pos];
	const char8_t close_ch = (open_ch == L'{') ? L'}' : L']';
	int depth = 1;
	for (size_t i = open_pos + 1; i < text.size() && depth > 0; ++i)
	{
		if (text[i] == L'"')
		{
			// Skip string contents
			for (++i; i < text.size() && text[i] != L'"'; ++i)
				if (text[i] == L'\\') ++i;
		}
		else if (text[i] == open_ch) ++depth;
		else if (text[i] == close_ch) --depth;
		if (depth == 0) return i;
	}
	return std::u8string::npos;
}

// Find an object within an array by searching for a key-value match
static std::u8string find_array_object(const std::u8string& array_text, const std::u8string_view key,
                                       const std::u8string_view value)
{
	const auto search = u8"\"" + std::u8string(key) + u8"\":\"" + std::u8string(value) + u8"\"";
	size_t pos = 0;
	while (pos < array_text.size())
	{
		const auto obj_start = array_text.find(L'{', pos);
		if (obj_start == std::u8string::npos) break;
		const auto obj_end = find_matching_brace(array_text, obj_start);
		if (obj_end == std::u8string::npos) break;

		const auto obj = array_text.substr(obj_start, obj_end - obj_start + 1);
		if (obj.find(search) != std::u8string::npos)
			return obj;
		pos = obj_end + 1;
	}
	return {};
}

// Extract each object from a JSON array
static std::vector<std::u8string> extract_array_objects(const std::u8string& text, const std::u8string_view key)
{
	std::vector<std::u8string> results;
	const auto search = u8"\"" + std::u8string(key) + u8"\"";
	auto pos = text.find(search);
	if (pos == std::u8string::npos) return results;

	pos += search.size();
	while (pos < text.size() && text[pos] != L'[') pos++;
	if (pos >= text.size()) return results;

	const auto arr_end = find_matching_brace(text, pos);
	if (arr_end == std::u8string::npos) return results;

	size_t cur = pos + 1;
	while (cur < arr_end)
	{
		const auto obj_start = text.find(L'{', cur);
		if (obj_start == std::u8string::npos || obj_start >= arr_end) break;
		const auto obj_end = find_matching_brace(text, obj_start);
		if (obj_end == std::u8string::npos || obj_end > arr_end) break;
		results.push_back(text.substr(obj_start, obj_end - obj_start + 1));
		cur = obj_end + 1;
	}
	return results;
}

// Extract a JSON subsection by key: "key":{...} → returns the braced content
static std::u8string extract_section(const std::u8string& text, const std::u8string_view key)
{
	const auto search = u8"\"" + std::u8string(key) + u8"\"";
	auto pos = text.find(search);
	if (pos == std::u8string::npos) return {};

	pos += search.size();
	while (pos < text.size() && text[pos] != L'{' && text[pos] != L'[') pos++;
	if (pos >= text.size()) return {};

	const auto end = find_matching_brace(text, pos);
	if (end == std::u8string::npos) return {};
	return text.substr(pos, end - pos + 1);
}

std::u8string generate_stock_markdown(const std::u8string& ticker, const std::u8string& chart_json,
                                      const std::u8string& summary_json)
{
	// Uppercase ticker for display
	std::u8string sym;
	for (const auto c : ticker)
		sym += static_cast<char8_t>(towupper(c));

	const auto name = extract_string_value(chart_json, u8"longName");
	const auto exchange = extract_string_value(chart_json, u8"fullExchangeName");
	const auto currency = extract_string_value(chart_json, u8"currency");

	const auto price = extract_fmt_value(chart_json, u8"regularMarketPrice");
	const auto prev_close = extract_fmt_value(chart_json, u8"chartPreviousClose");
	const auto week52_high = extract_fmt_value(chart_json, u8"fiftyTwoWeekHigh");
	const auto week52_low = extract_fmt_value(chart_json, u8"fiftyTwoWeekLow");
	const auto volume = extract_fmt_value(chart_json, u8"regularMarketVolume");
	const auto day_high = extract_fmt_value(chart_json, u8"regularMarketDayHigh");
	const auto day_low = extract_fmt_value(chart_json, u8"regularMarketDayLow");

	if (price.empty())
		return pf::format(u8"# {} — Quote Unavailable\n\nCould not retrieve stock data for {}.\n", sym, sym);

	// Calculate change from price and previous close
	std::u8string change_str;
	std::u8string change_pct_str;
	if (!prev_close.empty())
	{
		try
		{
			const auto p = pf::stod(price);
			const auto pc = pf::stod(prev_close);
			if (pc > 0.0)
			{
				const auto chg = p - pc;
				const auto pct = (chg / pc) * 100.0;
				change_str = pf::format(u8"{:+.2f}", chg);
				change_pct_str = pf::format(u8"{:+.2f}%", pct);
			}
		}
		catch (...)
		{
		}
	}

	std::u8string md;
	md += pf::format(u8"# {}", sym);
	if (!name.empty()) md += pf::format(u8" — {}", name);
	md += u8"\n\n";

	if (!exchange.empty() || !currency.empty())
	{
		if (!exchange.empty()) md += pf::format(u8"**Exchange:** {}", exchange);
		if (!exchange.empty() && !currency.empty()) md += u8" | ";
		if (!currency.empty()) md += pf::format(u8"**Currency:** {}", currency);
		md += u8"\n\n";
	}

	md += u8"## Current Price\n\n";
	md += pf::format(u8"**{}**", price);
	if (!change_str.empty() || !change_pct_str.empty())
	{
		md += u8" (";
		if (!change_str.empty()) md += change_str;
		if (!change_str.empty() && !change_pct_str.empty()) md += u8", ";
		if (!change_pct_str.empty()) md += change_pct_str;
		md += u8")";
	}
	md += u8"\n\n";

	// ── Company Profile (from quoteSummary assetProfile) ──
	if (!summary_json.empty())
	{
		const auto profile_section = extract_section(summary_json, u8"assetProfile");
		if (!profile_section.empty())
		{
			const auto sector = extract_string_value(profile_section, u8"sector");
			const auto industry = extract_string_value(profile_section, u8"industry");
			const auto employees = extract_fmt_value(profile_section, u8"fullTimeEmployees");
			const auto website = extract_string_value(profile_section, u8"website");
			const auto summary = extract_string_value(profile_section, u8"longBusinessSummary");

			if (!sector.empty() || !industry.empty() || !summary.empty())
			{
				md += u8"## Company Profile\n\n";
				md += u8"| Detail | Value |\n";
				md += u8"|--------|-------|\n";
				if (!sector.empty()) md += pf::format(u8"| Sector | {} |\n", sector);
				if (!industry.empty()) md += pf::format(u8"| Industry | {} |\n", industry);
				if (!employees.empty()) md += pf::format(u8"| Employees | {} |\n", employees);
				if (!website.empty()) md += pf::format(u8"| Website | {} |\n", website);
				if (!summary.empty()) md += pf::format(u8"\n\n{}\n", summary);
				md += u8"\n";
			}
		}
	}

	md += u8"## Trading Data\n\n";
	md += u8"| Metric | Value |\n";
	md += u8"|--------|-------|\n";
	if (!prev_close.empty()) md += pf::format(u8"| Previous Close | {} |\n", prev_close);
	if (!day_low.empty() && !day_high.empty())
		md += pf::format(u8"| Day Range | {} - {} |\n", day_low, day_high);
	if (!week52_low.empty() && !week52_high.empty())
		md += pf::format(u8"| 52-Week Range | {} - {} |\n", week52_low, week52_high);
	if (!volume.empty()) md += pf::format(u8"| Volume | {} |\n", volume);

	// ── Analyst Targets (from quoteSummary financialData) ──
	if (!summary_json.empty())
	{
		const auto target_mean = extract_fmt_value(summary_json, u8"targetMeanPrice");
		const auto target_high = extract_fmt_value(summary_json, u8"targetHighPrice");
		const auto target_low = extract_fmt_value(summary_json, u8"targetLowPrice");
		const auto target_median = extract_fmt_value(summary_json, u8"targetMedianPrice");
		const auto rec_key = extract_string_value(summary_json, u8"recommendationKey");
		const auto num_analysts = extract_fmt_value(summary_json, u8"numberOfAnalystOpinions");

		if (!target_mean.empty())
		{
			md += u8"\n## Analyst Price Targets\n\n";
			md += u8"| Metric | Value |\n";
			md += u8"|--------|-------|\n";
			if (!target_mean.empty()) md += pf::format(u8"| Mean Target | {} |\n", target_mean);
			if (!target_median.empty()) md += pf::format(u8"| Median Target | {} |\n", target_median);
			if (!target_high.empty()) md += pf::format(u8"| High Target | {} |\n", target_high);
			if (!target_low.empty()) md += pf::format(u8"| Low Target | {} |\n", target_low);
			if (!rec_key.empty()) md += pf::format(u8"| Recommendation | {} |\n", rec_key);
			if (!num_analysts.empty()) md += pf::format(u8"| Analysts | {} |\n", num_analysts);
		}

		// ── Recommendation Trend ──
		const auto rec_trend_section = extract_section(summary_json, u8"recommendationTrend");
		const auto rec_trend_objects = extract_array_objects(rec_trend_section, u8"trend");
		if (!rec_trend_objects.empty())
		{
			// Find the current month trend (period "0m")
			bool has_rec = false;
			for (const auto& obj : rec_trend_objects)
			{
				if (obj.find(u8"\"strongBuy\"") != std::u8string::npos)
				{
					if (!has_rec)
					{
						md += u8"\n## Analyst Recommendations\n\n";
						md += u8"| Period | Strong Buy | Buy | Hold | Sell | Strong Sell |\n";
						md += u8"|--------|-----------|-----|------|------|------------|\n";
						has_rec = true;
					}
					const auto period = extract_string_value(obj, u8"period");
					const auto strong_buy = extract_fmt_value(obj, u8"strongBuy");
					const auto buy = extract_fmt_value(obj, u8"buy");
					const auto hold = extract_fmt_value(obj, u8"hold");
					const auto sell = extract_fmt_value(obj, u8"selu8");
					const auto strong_sell = extract_fmt_value(obj, u8"strongSelu8");
					md += pf::format(u8"| {} | {} | {} | {} | {} | {} |\n",
					                 period, strong_buy, buy, hold, sell, strong_sell);
				}
			}
		}

		// ── Quarterly Earnings ──
		const auto earnings_hist_section = extract_section(summary_json, u8"earningsHistory");
		const auto history_objects = extract_array_objects(earnings_hist_section, u8"history");
		if (!history_objects.empty())
		{
			bool has_earnings = false;
			for (const auto& obj : history_objects)
			{
				if (obj.find(u8"\"epsActual\"") != std::u8string::npos)
				{
					if (!has_earnings)
					{
						md += u8"\n## Quarterly Earnings\n\n";
						md += u8"| Quarter | EPS Actual | EPS Estimate | Surprise |\n";
						md += u8"|---------|-----------|-------------|----------|\n";
						has_earnings = true;
					}
					const auto quarter = extract_fmt_value(obj, u8"quarter");
					const auto eps_actual = extract_fmt_value(obj, u8"epsActuau8");
					const auto eps_estimate = extract_fmt_value(obj, u8"epsEstimate");
					const auto surprise = extract_fmt_value(obj, u8"surprisePercent");
					md += pf::format(u8"| {} | {} | {} | {} |\n",
					                 quarter, eps_actual, eps_estimate, surprise);
				}
			}
		}

		// ── Earnings Estimates ──
		const auto earnings_trend_section = extract_section(summary_json, u8"earningsTrend");
		const auto earnings_trend_objects = extract_array_objects(earnings_trend_section, u8"trend");
		if (!earnings_trend_objects.empty())
		{
			bool has_estimates = false;
			for (const auto& obj : earnings_trend_objects)
			{
				if (obj.find(u8"\"earningsEstimate\"") != std::u8string::npos)
				{
					if (!has_estimates)
					{
						md += u8"\n## Earnings Estimates\n\n";
						md += u8"| Period | EPS Est. | Revenue Est. | Growth |\n";
						md += u8"|--------|---------|-------------|--------|\n";
						has_estimates = true;
					}
					const auto period = extract_string_value(obj, u8"period");
					const auto end_date = extract_string_value(obj, u8"endDate");

					// Extract earnings estimate avg from nested object
					auto eps_pos = obj.find(u8"\"earningsEstimate\"");
					std::u8string eps_avg;
					if (eps_pos != std::u8string::npos)
					{
						const auto ee_brace = obj.find(L'{', eps_pos);
						if (ee_brace != std::u8string::npos)
						{
							const auto ee_end = find_matching_brace(obj, ee_brace);
							if (ee_end != std::u8string::npos)
							{
								const auto ee_section = obj.substr(ee_brace, ee_end - ee_brace + 1);
								eps_avg = extract_fmt_value(ee_section, u8"avg");
							}
						}
					}

					// Extract revenue estimate avg
					auto rev_pos = obj.find(u8"\"revenueEstimate\"");
					std::u8string rev_avg;
					if (rev_pos != std::u8string::npos)
					{
						const auto re_brace = obj.find(L'{', rev_pos);
						if (re_brace != std::u8string::npos)
						{
							const auto re_end = find_matching_brace(obj, re_brace);
							if (re_end != std::u8string::npos)
							{
								const auto re_section = obj.substr(re_brace, re_end - re_brace + 1);
								rev_avg = extract_fmt_value(re_section, u8"avg");
							}
						}
					}

					const auto growth = extract_fmt_value(obj, u8"growth");
					const auto label = end_date.empty() ? period : end_date;
					md += pf::format(u8"| {} | {} | {} | {} |\n",
					                 label, eps_avg, rev_avg, growth);
				}
			}
		}
	}

	return md;
}
