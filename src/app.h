#pragma once

// app.h — Application types: event interfaces, search results, index items, app_events

#include "platform.h"

class text_location;
class document;
using document_ptr = std::shared_ptr<document>;

struct index_item;
using index_item_ptr = std::shared_ptr<index_item>;

struct search_result
{
	std::wstring line_text;
	int line_number = -1;
	int line_match_pos = -1;
	int text_match_start = -1;
	int text_match_length = 0;
};

struct index_item
{
	file_path path;
	std::wstring name;
	bool is_folder = false;
	document_ptr doc;
	std::vector<index_item_ptr> children;
	std::vector<search_result> search_results;

	index_item() = default;

	index_item(file_path p, std::wstring n, const bool folder,
	           document_ptr d = nullptr)
		: path(std::move(p)), name(std::move(n)), is_folder(folder), doc(std::move(d))
	{
	}
};

namespace invalid
{
	constexpr auto title = 1 << 0;
	constexpr auto layout = 1 << 2;
	constexpr auto caret = 1 << 3;
	constexpr auto horz_scrollbar = 1 << 4;
	constexpr auto vert_scrollbar = 1 << 5;
	constexpr auto invalidate = 1 << 6;
	constexpr auto view = horz_scrollbar | vert_scrollbar | layout | caret | invalidate;

	constexpr auto folder_list = 1 << 7;
	constexpr auto search_list = 1 << 8;
	constexpr auto populate_folder_list = 1 << 9;
	constexpr auto console = 1 << 10;
}

enum class view_mode
{
	overlay,
	edit_text_files,
	edit_text_search,
	markdown_files,
	markdown_search,
	hex_files,
	hex_search,
};

inline bool is_overlay(const view_mode m) { return m == view_mode::overlay; }

inline bool is_search(const view_mode m)
{
	return m == view_mode::edit_text_search || m == view_mode::markdown_search || m == view_mode::hex_search;
}

inline bool is_markdown(const view_mode m) { return m == view_mode::markdown_files || m == view_mode::markdown_search; }
inline bool is_hex(const view_mode m) { return m == view_mode::hex_files || m == view_mode::hex_search; }

inline bool is_edit_text(const view_mode m)
{
	return m == view_mode::edit_text_files || m == view_mode::edit_text_search;
}

enum class view_focus
{
	text,
	list,
	console,
};

// document_events — Narrow interface for document-to-host notifications.
// The document model depends only on this, not the full app_events.
class document_events
{
public:
	virtual ~document_events() = default;

	virtual void invalidate(uint32_t i) = 0;
	virtual void invalidate_lines(int start, int end) = 0;
	virtual void ensure_visible(const text_location& pt) = 0;
};

struct view_styles
{
	double dpi_scale = 1.0;

	int list_font_height = 20;
	int text_font_height = 24;
	int console_font_height = 20;

	pf::font list_font = {20, pf::font_name::calibri};
	pf::font edit_font = {30, pf::font_name::calibri};
	pf::font text_font = {24, pf::font_name::consolas};
	pf::font console_font = {20, pf::font_name::consolas};

	int padding_x = 5;
	int padding_y = 5;
	int indent = 16;

	// Edit box / input field layout (DPI-scaled)
	int edit_box_margin = 6;
	int edit_box_inner_pad = 4;

	// List view layout
	int list_top_pad = 4;
	int list_scroll_pad = 64;
};

// app_events — Full application event interface used by views and panels.
class app_events : public document_events
{
public:
	virtual std::wstring_view message_bar_text() const = 0;
	virtual index_item_ptr root_item() const = 0;
	virtual index_item_ptr active_item() const = 0;
	virtual view_styles styles() const = 0;

	virtual void set_focus(view_focus v) = 0;
	virtual void set_mode(view_mode m) = 0;

	virtual void path_selected(const index_item_ptr& item) = 0;
	virtual bool is_path_modified(const index_item_ptr& item) const = 0;
	virtual void on_escape() = 0;
	virtual void open_path_and_select(const index_item_ptr& item, int line, int col, int length) = 0;

	virtual void on_search(const std::wstring& text) = 0;
	virtual void on_zoom(int delta, bool is_text_view) = 0;
};
