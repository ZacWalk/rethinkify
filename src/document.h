#pragma once


#include "spell_check.h"
#include "Util.h"

class undo_group;
class text_view;
class document_line;
class text_location;

using DROPEFFECT = uint32_t;
using POSITION = int;

const auto TIMER_DRAGSEL = 1001;
const auto invalid_length = -1;

const auto TAB_CHARACTER = 0xBB;
const auto SPACE_CHARACTER = 0x95;
const auto DEFAULT_PRINT_MARGIN = 1000; //	10 millimeters
const auto DRAG_BORDER_X = 5;
const auto DRAG_BORDER_Y = 5;

namespace invalid
{
	const auto title = 1 << 0;
	const auto view = 1 << 1;
	const auto layout = 1 << 2;
	const auto caret = 1 << 3;
	const auto horz_scrollbar = 1 << 4;
	const auto vert_scrollbar = 1 << 5;
}

enum class style
{
	main_wnd_clr,
	white_space,
	normal_bkgnd,
	normal_text,

	sel_margin,
	sel_bkgnd,
	sel_text,

	error_bkgnd,
	error_text,

	code_keyword,
	code_comment,
	code_number,
	code_operator,
	code_string,
	code_preprocessor,
};

class highlighter
{
public:
	virtual ~highlighter() = default;


	struct text_block
	{
		size_t _char_pos;
		style _color;
	};

	virtual uint32_t parse_line(uint32_t dwCookie, const document_line& line, text_block* pBuf,
	                            int& nActualItems) const = 0;
	virtual std::vector<std::wstring> suggest(std::wstring_view wword) const = 0;

	virtual bool can_add(std::wstring_view word) const
	{
		return false;
	}

	virtual void add_word(std::wstring_view word) const
	{
	}
};

class cpp_highlight : public highlighter
{
	uint32_t parse_line(uint32_t dwCookie, const document_line& line, text_block* pBuf,
	                    int& nActualItems) const override;

	std::vector<std::wstring> suggest(std::wstring_view wword) const override
	{
		return {std::vector<std::wstring>()};
	}
};

class text_highight : public highlighter
{
	mutable spell_check _check;

	uint32_t parse_line(uint32_t dwCookie, const document_line& line, text_block* pBuf,
	                    int& nActualItems) const override;
	std::vector<std::wstring> suggest(std::wstring_view wword) const override;

	bool can_add(std::wstring_view word) const override
	{
		return !_check.is_word_valid(word);
	}

	void add_word(std::wstring_view word) const override
	{
		_check.add_word(word);
	}
};

class IView
{
public:
	virtual ~IView() = default;

	virtual std::wstring text_from_clipboard() const = 0;
	virtual bool text_to_clipboard(std::wstring_view text) = 0;
	virtual void ensure_visible(const text_location& pt) = 0;
	virtual void invalidate_lines(int start, int end) = 0;
};

class IEvents
{
public:
	virtual ~IEvents() = default;

	virtual void path_selected(const file_path& path) = 0;
};

enum class line_endings
{
	crlf_style_automatic = -1,
	crlf_style_dos = 0,
	crlf_style_unix = 1,
	crlf_style_mac = 2
};

const auto find_match_case = 1 << 0;
const auto find_whole_word = 1 << 1;
const auto find_direction_up = 1 << 2;
const auto find_start_selection = 1 << 3;
const auto replace_selection = 1 << 4;

class text_location
{
public:
	int x = 0;
	int y = 0;

	text_location(const int xx = 0, const int yy = 0) : x(xx), y(yy)
	{
	}

	bool operator==(const text_location& other) const
	{
		return x == other.x && y == other.y;
	}

	bool operator!=(const text_location& other) const
	{
		return x != other.x || y != other.y;
	}

	bool operator<(const text_location& other) const
	{
		return y < other.y || (y == other.y && x < other.x);
	}
};

class text_selection
{
public:
	text_location _start;
	text_location _end;

	text_selection() = default;
	text_selection(const text_selection&) = default;
	text_selection(text_selection&&) = default;
	text_selection& operator=(const text_selection&) = default;
	text_selection& operator=(text_selection&&) = default;

	text_selection(const text_location& start, const text_location& end) : _start(start), _end(end)
	{
	}

	text_selection(const text_location& loc) : _start(loc), _end(loc)
	{
	}

	text_selection(int x1, int y1, int x2, int y2) : _start(x1, y1), _end(x2, y2)
	{
	}

	bool operator==(const text_selection& other) const
	{
		return _start == other._start && _end == other._end;
	}

	bool operator!=(const text_selection& other) const
	{
		return _start != other._start || _end != other._end;
	}

	bool empty() const
	{
		return _start == _end;
	}

	bool is_valid() const
	{
		return _start.x >= 0 && _start.y >= 0 && _end.x >= 0 && _end.y >= 0;
	}

	text_selection normalize() const
	{
		text_selection result;

		if (_start < _end)
		{
			result._start = _start;
			result._end = _end;
		}
		else
		{
			result._start = _end;
			result._end = _start;
		}

		return result;
	}

	int line_count() const
	{
		return 1 + _end.y - _start.y;
	}
};

class document_line
{
public:
	std::wstring _text;
	uint32_t _flags = 0;

	mutable int _expanded_length = invalid_length;
	mutable int _parse_cookie = invalid_length;

	document_line() = default;
	document_line(const document_line&) = default;
	document_line(document_line&&) = default;
	document_line& operator=(const document_line&) = default;
	document_line& operator=(document_line&&) = default;

	explicit document_line(std::wstring line) : _text(std::move(line))
	{
	}

	explicit document_line(std::wstring_view line) : _text(line)
	{
	}

	bool empty() const
	{
		return _text.empty();
	}

	size_t size() const
	{
		return _text.size();
	}

	std::wstring_view view() const
	{
		return _text;
	}

	const wchar_t& operator[](int n) const
	{
		return _text[n];
	}

	bool operator<(const document_line& other) const
	{
		return str::icmp(_text, other._text) < 0;
	}

	bool operator==(const document_line& other) const
	{
		return str::icmp(_text, other._text) == 0;
	}
};


class document
{
private:
	struct undo_item;

	IView& _view;

	text_location _anchor_loc;
	text_location _cursor_loc;
	text_selection _selection;
	text_selection _saved_sel;
	bool _auto_indent = false;
	bool _view_tabs = false;
	int _ideal_char_pos = 0;
	int _tab_size = 4;
	mutable int _max_line_len = -1;
	std::atomic<uint32_t> _invalid = 0;

	file_path _path;
	std::wstring _find_text;
	std::shared_ptr<highlighter> _highlight;

	mutable bool _modified = false;
	line_endings _line_ending = line_endings::crlf_style_automatic;
	bool _create_backup_file = false;

	std::vector<document_line> _lines;
	std::vector<undo_item> _undo;

	size_t _undo_pos = 0;

private:
	struct undo_step
	{
	public:
		text_selection _selection;
		bool _insert = false;
		std::wstring _text;

	public:
		undo_step() = default;
		undo_step(const undo_step&) = default;
		undo_step(undo_step&&) = default;
		undo_step& operator=(const undo_step&) = default;
		undo_step& operator=(undo_step&&) = default;

		undo_step(const text_location& location, const wchar_t& c, bool insert) : _selection(location, location),
			_insert(insert), _text(1, c)
		{
		}

		undo_step(const text_location& location, std::wstring text, bool insert) :
			_selection(location, location), _insert(insert), _text(std::move(text))
		{
		}

		undo_step(const text_selection& selection, std::wstring text, bool insert) : _selection(selection),
			_insert(insert), _text(std::move(text))
		{
		}

		undo_step(const text_selection& selection, std::wstring_view text, bool insert) : _selection(selection),
			_insert(insert), _text(text)
		{
		}

		text_location undo(document& buffer) const
		{
			if (_insert)
			{
				if (_text == L"\n")
				{
					return buffer.delete_text(text_location(0, _selection._start.y + 1));
				}
				if (_text.size() == 1)
				{
					return buffer.delete_text(text_location(_selection._start.x + 1, _selection._start.y));
				}
				return buffer.delete_text(_selection);
			}
			return buffer.insert_text(_selection._start, _text);
		}

		text_location redo(document& buffer) const
		{
			if (_insert)
			{
				return buffer.insert_text(_selection._start, _text);
			}
			if (_text == L"\n")
			{
				return buffer.delete_text(text_location(0, _selection._start.y + 1));
			}
			if (_text.size() == 1)
			{
				return buffer.delete_text(text_location(_selection._start.x + 1, _selection._start.y));
			}
			return buffer.delete_text(_selection);
		}
	};

	struct undo_item
	{
	public:
		std::vector<undo_step> _steps;

	public:
		undo_item() = default;
		undo_item(const undo_item&) = default;
		undo_item(undo_item&&) = default;
		undo_item& operator=(const undo_item&) = default;
		undo_item& operator=(undo_item&&) = default;

		text_location undo(document& buffer)
		{
			text_location location;

			for (auto i = _steps.rbegin(); i != _steps.rend(); ++i)
			{
				location = i->undo(buffer);
			}

			return location;
		}

		text_location redo(document& buffer)
		{
			text_location location;

			for (auto i = _steps.begin(); i != _steps.end(); ++i)
			{
				location = i->redo(buffer);
			}

			return location;
		}
	};


	text_location insert_text(const text_location& location, std::wstring_view text);
	text_location insert_text(const text_location& location, const wchar_t& c);
	text_location delete_text(const text_selection& selection);
	text_location delete_text(const text_location& location);

public:
	document(IView& view, const std::wstring& text = std::wstring(),
	         line_endings nCrlfStyle = line_endings::crlf_style_dos);
	~document();

	bool load_from_file(const file_path &path);
	bool save_to_file(const file_path &path, line_endings nCrlfStyle = line_endings::crlf_style_automatic,
	                  bool bClearModifiedFlag = true) const;
	void clear();

	bool is_modified() const
	{
		return _modified;
	}

	bool empty() const
	{
		return _lines.empty();
	}

	size_t size() const
	{
		return _lines.size();
	}

	const document_line& operator[](int n) const
	{
		return _lines[n];
	}

	document_line& operator[](int n)
	{
		return _lines[n];
	}

	std::vector<std::wstring> text(const text_selection& selection) const;
	
	static std::wstring combine_line_text(const std::vector<document_line>& lines)
	{
		std::wostringstream result;
		auto first = true;

		for (const auto& line : lines)
		{
			if (first)
			{
				result << line._text;
				first = false;
			}
			else
			{
				result << std::endl << line._text;
			}
		}

		return result.str();
	}

	std::wstring str() const
	{
		return combine_line_text(_lines);
	}

	void path(file_path path_in)
	{
		_path = path_in;
		invalidate(invalid::title);
	}

	file_path path() const
	{
		return _path;
	}

	void append_line(std::wstring_view text);

	text_selection replace_text(undo_group& ug, const text_selection& selection, std::wstring_view text);

	text_location insert_text(undo_group& ug, const text_location& location, std::wstring_view text);
	text_location insert_text(undo_group& ug, const text_location& location, const wchar_t& c);
	text_location delete_text(undo_group& ug, const text_selection& selection);
	text_location delete_text(undo_group& ug, const text_location& location);


	bool can_undo() const;
	bool can_redo() const;
	text_location undo();
	text_location redo();
	void record_undo(const undo_item& ui);

	bool find(std::wstring_view what, const text_location& start_pos, const text_selection& selection,
	          uint32_t flags, bool wrap_search, text_location& found_pos) const;
	void find(std::wstring_view what, uint32_t flags);

	bool can_find_next() const
	{
		return !_find_text.empty();
	}

	void highlight_from_extension(std::wstring_view ext);

	const std::vector<document_line>& lines() const
	{
		return _lines;
	}

	static bool can_paste();

	bool has_selection() const
	{
		return !_selection.empty();
	}

	bool GetAutoIndent() const;
	static bool QueryEditable();

	void edit_cut();
	void Copy() const;
	void edit_delete();
	void edit_delete_back();
	void edit_redo();
	void edit_tab();
	void edit_undo();
	void edit_untab();
	void edit_paste();
	void SetAutoIndent(bool bAutoIndent);

	text_selection selection() const
	{
		return _selection.normalize();
	}

	void move_doc_end(bool selecting);
	void move_doc_home(bool selecting);
	void move_line_end(bool selecting);
	void move_line_home(bool selecting);
	void move_char_left(bool selecting);
	void move_char_right(bool selecting);
	void move_word_left(bool selecting);
	void move_word_right(bool selecting);
	void move_lines(int lines_to_move, bool selecting);

	bool is_inside_selection(const text_location& loc) const;
	void reset();

	text_location end() const
	{
		const auto last_line = _lines.size() - 1;
		return text_location(_lines[last_line].size(), last_line);
	}

	text_selection all() const
	{
		return text_selection(text_location(0, 0), end());
	}

	int tab_size() const
	{
		return _tab_size;
	}

	void tab_size(int nTabSize);
	void view_tabs(bool bViewTabs);
	int max_line_length() const;
	text_location word_to_left(text_location pt) const;
	text_location word_to_right(text_location pt) const;

	uint32_t highlight_cookie(int lineIndex) const;
	uint32_t highlight_line(uint32_t dwCookie, const document_line& line, highlighter::text_block* pBuf,
	                        int& nActualItems) const;

	bool view_tabs() const;
	bool highlight_text(const text_location& ptStartPos, int nLength);

	int calc_offset(int lineIndex, int nCharIndex) const;
	int calc_offset_approx(int lineIndex, int nOffset) const;
	int expanded_line_length(int line_index) const;
	std::wstring expanded_chars(std::wstring_view text, int offset_in, int count_in) const;

	const text_location& cursor_pos() const
	{
		return _cursor_loc;
	}

	const text_location& anchor_pos() const
	{
		return _anchor_loc;
	}

	void anchor_pos(const text_location& ptNewAnchor);
	void cursor_pos(const text_location& ptCursorPos);

	void update_max_line_length(int lineIndex) const;

	void move_to(text_location pos, bool selecting)
	{
		const auto limit = _lines[pos.y].size();

		if (pos.x > limit)
		{
			pos.x = limit;
		}

		if (!selecting)
		{
			_anchor_loc = _cursor_loc;
		}

		select(text_selection(_anchor_loc, _cursor_loc));
	}

	text_selection word_selection(const text_location& pos, bool from_anchor) const
	{
		const auto ptStart = from_anchor ? _anchor_loc : pos;
		const auto ptEnd = pos;

		if (ptStart < ptEnd || ptStart == ptEnd)
		{
			return text_selection(word_to_left(ptStart), word_to_right(ptEnd));
		}
		return text_selection(word_to_right(ptStart), word_to_left(ptEnd));
	}

	text_selection word_selection() const
	{
		if (_cursor_loc < _anchor_loc)
		{
			return text_selection(word_to_left(_cursor_loc), word_to_right(_anchor_loc));
		}
		return text_selection(word_to_left(_anchor_loc), word_to_right(_cursor_loc));
	}

	text_selection line_selection(const text_location& pos, bool from_anchor) const
	{
		auto ptStart = from_anchor ? _anchor_loc : pos;
		auto ptEnd = pos;

		ptEnd.x = 0; //	Force beginning of the line

		if (ptStart.y >= _lines.size())
		{
			ptStart.x = _lines[ptStart.y].size();
		}
		else
		{
			ptStart.y++;
			ptStart.x = 0;
		}

		return text_selection(ptStart, ptEnd);
	}

	text_selection pos_selection(const text_location& pos, bool from_anchor) const
	{
		return text_selection(from_anchor ? _anchor_loc : pos, pos);
	}

	void select(const text_selection& selection)
	{
		if (_selection != selection)
		{
			assert(selection.is_valid());

			anchor_pos(selection._start);
			cursor_pos(selection._end);

			_view.ensure_visible(selection._end);
			invalidate(invalid::caret);

			_view.invalidate_lines(selection._start.y, selection._end.y);
			_view.invalidate_lines(_selection._start.y, _selection._end.y);
			_selection = selection;
		}
	}

	void invalidate_line(int index)
	{
		const auto& line = _lines[index];
		line._expanded_length = -1;
		line._parse_cookie = -1;

		_view.invalidate_lines(index, index + 1);

		update_max_line_length(index);
		invalidate(invalid::horz_scrollbar);
	}

	void add_word(std::wstring_view word)
	{
		_highlight->add_word(word);
		invalidate(invalid::view);
	}

	std::vector<std::wstring> suggest(std::wstring_view word) const
	{
		return _highlight->suggest(word);
	}

	bool can_add(std::wstring_view word) const
	{
		return _highlight->can_add(word);
	}

	void invalidate(const uint32_t i)
	{
		_invalid |= i;
	}

	uint32_t validate()
	{
		return _invalid.exchange(0);
	}

	friend class undo_group;
};


class undo_group
{
	document& _doc;
	document::undo_item _undo;

public:
	undo_group(const undo_group&) = default;
	undo_group(undo_group&&) = default;
	undo_group& operator=(const undo_group&) = default;
	undo_group& operator=(undo_group&&) = default;

	undo_group(document& d) : _doc(d)
	{
	}

	~undo_group()
	{
		_doc.record_undo(_undo);
	}

	void insert(const text_location& location, const wchar_t& c)
	{
		_undo._steps.emplace_back(location, c, true);
	}

	void insert(const text_selection& selection, std::wstring_view text)
	{
		_undo._steps.emplace_back(selection, text, true);
	}

	void erase(const text_location& location, const wchar_t& c)
	{
		_undo._steps.emplace_back(location, c, false);
	}

	void erase(const text_selection& selection, std::wstring_view text)
	{
		_undo._steps.emplace_back(selection, text, false);
	}
};
