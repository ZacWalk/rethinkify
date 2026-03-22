// finance.h — Stock quote helpers and Yahoo Finance fetch interfaces

#pragma once

#include "platform.h"

// Build the Yahoo Finance chart API URL for a given ticker.
inline std::u8string yahoo_chart_url(const std::u8string_view ticker)
{
	return pf::format(u8"https://query1.finance.yahoo.com/v8/finance/chart/{}?range=1d&interval=1d", ticker);
}

// Fetch a URL using a Chrome-compatible user agent, returning the HTTP response.
pf::web_response fetch_page(std::u8string_view url);

// Fetch a Yahoo Finance API crumb (needed for quoteSummary requests).
std::u8string fetch_yahoo_crumb();

// Fetch Yahoo Finance quoteSummary JSON for a ticker using a crumb.
std::u8string fetch_quote_summary(const std::u8string& ticker, const std::u8string& crumb);

// Parse Yahoo Finance chart API JSON and quoteSummary JSON, generate a markdown stock summary.
std::u8string generate_stock_markdown(const std::u8string& ticker, const std::u8string& chart_json,
                                      const std::u8string& summary_json = {});

// Fetch chart + summary and generate markdown for a ticker (combines all fetch steps).
inline std::u8string fetch_stock_markdown(const std::u8string& ticker)
{
	const auto chart_json = fetch_page(yahoo_chart_url(ticker)).body;
	const auto crumb = fetch_yahoo_crumb();
	const auto summary_json = fetch_quote_summary(ticker, crumb);
	return generate_stock_markdown(ticker, chart_json, summary_json);
}
