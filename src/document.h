#pragma once


#include "spell_check.h"
#include "Util.h"

class undo_group;
class text_view;
class document_line;
class text_location;

typedef uint32_t DROPEFFECT;
typedef int POSITION;

const auto RETHINKIFY_TIMER_DRAGSEL = 1001;
const auto invalid = -1;

const auto TAB_CHARACTER = 0xBB;
const auto SPACE_CHARACTER = 0x95;
const auto DEFAULT_PRINT_MARGIN = 1000; //	10 millimeters
const auto DRAG_BORDER_X = 5;
const auto DRAG_BORDER_Y = 5;

enum class color_index
{
	//	Base colors
	COLORINDEX_WHITESPACE,
	COLORINDEX_BKGND,
	COLORINDEX_NORMALTEXT,
	COLORINDEX_SELMARGIN,
	COLORINDEX_SELBKGND,
	COLORINDEX_SELTEXT,
	//	Syntax colors
	COLORINDEX_KEYWORD,
	COLORINDEX_COMMENT,
	COLORINDEX_NUMBER,
	COLORINDEX_OPERATOR,
	COLORINDEX_STRING,
	COLORINDEX_PREPROCESSOR,
	//	Compiler/debugger colors
	COLORINDEX_ERRORBKGND,
	COLORINDEX_ERRORTEXT,
	COLORINDEX_EXECUTIONBKGND,
	COLORINDEX_EXECUTIONTEXT,
	COLORINDEX_BREAKPOINTBKGND,
	COLORINDEX_BREAKPOINTTEXT
	//	...
	//	Expandable: custom elements are allowed.
};

class highlighter
{
public:
	virtual ~highlighter() {}
	

	struct text_block
	{
		size_t _char_pos;
		color_index _color;
	};

	virtual uint32_t parse_line(uint32_t dwCookie, const document_line& line, text_block* pBuf, int& nActualItems) const = 0;
	virtual std::vector<std::wstring> suggest(const std::wstring& wword) const = 0;

	virtual bool can_add(const std::wstring& word) const
	{
		return false;
	};

	virtual void add_word(const std::wstring& word) const { };
};

class cpp_highlight : public highlighter
{
	uint32_t parse_line(uint32_t dwCookie, const document_line& line, text_block* pBuf, int& nActualItems) const override;

	std::vector<std::wstring> suggest(const std::wstring& wword) const override
	{
		return std::vector<std::wstring>();
	};
};

class text_highight : public highlighter
{
	mutable spell_check _check;

	uint32_t parse_line(uint32_t dwCookie, const document_line& line, text_block* pBuf, int& nActualItems) const override;
	std::vector<std::wstring> suggest(const std::wstring& wword) const override;

	bool can_add(const std::wstring& word) const override
	{
		return !_check.is_word_valid(word.c_str(), word.size());
	};

	void add_word(const std::wstring& word) const override
	{
		_check.add_word(word);
	};
};

class IView
{
public:
	virtual ~IView() {}

	virtual std::wstring text_from_clipboard() const = 0;
	virtual bool text_to_clipboard(const std::wstring& text) = 0;
	virtual void update_caret() = 0;
	virtual void recalc_horz_scrollbar() = 0;
	virtual void recalc_vert_scrollbar() = 0;
	virtual void invalidate_lines(int start, int end) = 0;
	virtual void invalidate_line(int index) = 0;
	virtual void invalidate_view() = 0;
	virtual void layout() = 0;
	virtual void ensure_visible(const text_location& pt) = 0;
};

enum class line_endings
{
	CRLF_STYLE_AUTOMATIC = -1,
	CRLF_STYLE_DOS = 0,
	CRLF_STYLE_UNIX = 1,
	CRLF_STYLE_MAC = 2
};

const auto FIND_MATCH_CASE = 0x0001;
const auto FIND_WHOLE_WORD = 0x0002;
const auto FIND_DIRECTION_UP = 0x0010;
const auto REPLACE_SELECTION = 0x0100;

class text_location
{
public:
	int x = 0;
	int y = 0;

	text_location(int xx = 0, int yy = 0)
	{
		x = xx;
		y = yy;
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

	text_selection() { }

	text_selection(const text_location& start, const text_location& end) : _start(start), _end(end) { }

	text_selection(const text_location& loc) : _start(loc), _end(loc) { }

	text_selection(int x1, int y1, int x2, int y2) : _start(x1, y1), _end(x2, y2) { }

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
	};

	bool is_valid() const
	{
		return _start.x >= 0 && _start.y >= 0 && _end.x >= 0 && _end.y >= 0;
	};

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
		return 1+  _end.y - _start.y;
	}
};

class document_line
{
public:

	std::wstring _text;
	uint32_t _flags = 0;

	mutable int _expanded_length = invalid;
	mutable int _parse_cookie = invalid;

	document_line() { };

	explicit document_line(const std::wstring& line) : _text(line) { };

	bool empty() const
	{
		return _text.empty();
	};

	size_t size() const
	{
		return _text.size();
	};

	const wchar_t* c_str() const
	{
		return _text.c_str();
	};

	const wchar_t& operator[](int n) const
	{
		return _text[n];
	};
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

	std::wstring _path;
	uint32_t _find_flags = 0;
	std::wstring _find_text;
	std::shared_ptr<highlighter> _highlight;

	mutable bool _modified = false;
	line_endings _line_ending = line_endings::CRLF_STYLE_AUTOMATIC;
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

		undo_step() : _insert(false) { };

		undo_step(const text_location& location, const wchar_t& c, bool insert) : _selection(location, location), _insert(insert), _text(1, c) { };

		undo_step(const text_location& location, const std::wstring& text, bool insert) : _selection(location, location), _insert(insert), _text(text) { };

		undo_step(const text_selection& selection, const std::wstring& text, bool insert) : _selection(selection), _insert(insert), _text(text) { };

		text_location undo(document& buffer) const
		{
			if (_insert)
			{
				if (_text == L"\n")
				{
					return buffer.delete_text(text_location(0, _selection._start.y + 1));
				}
				else if (_text.size() == 1)
				{
					return buffer.delete_text(text_location(_selection._start.x + 1, _selection._start.y));
				}
				else
				{
					return buffer.delete_text(_selection);
				}
			}
			else
			{
				return buffer.insert_text(_selection._start, _text);
			}
		}

		text_location redo(document& buffer) const
		{
			if (_insert)
			{
				return buffer.insert_text(_selection._start, _text);
			}
			else
			{
				if (_text == L"\n")
				{
					return buffer.delete_text(text_location(0, _selection._start.y + 1));
				}
				else if (_text.size() == 1)
				{
					return buffer.delete_text(text_location(_selection._start.x + 1, _selection._start.y));
				}
				else
				{
					return buffer.delete_text(_selection);
				}
			}
		}
	};

	struct undo_item
	{
	public:

		std::vector<undo_step> _steps;

	public:

		undo_item() { };

		undo_item(const undo_item& other) : _steps(other._steps) { };

		const undo_item& operator=(const undo_item& other)
		{
			_steps = other._steps;
			return *this;
		};

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


	text_location insert_text(const text_location& location, const std::wstring& text);
	text_location insert_text(const text_location& location, const wchar_t& c);
	text_location delete_text(const text_selection& selection);
	text_location delete_text(const text_location& location);

public:

	document(IView& view, const std::wstring& text = std::wstring(), line_endings nCrlfStyle = line_endings::CRLF_STYLE_DOS);
	~document();

	bool load_from_file(const std::wstring& path);
	bool save_to_file(const std::wstring& path, line_endings nCrlfStyle = line_endings::CRLF_STYLE_AUTOMATIC, bool bClearModifiedFlag = true) const;
	void clear();

	bool IsModified() const
	{
		return _modified;
	}

	bool empty() const
	{
		return _lines.empty();
	};

	size_t size() const
	{
		return _lines.size();
	};

	const document_line& operator[](int n) const
	{
		return _lines[n];
	};

	document_line& operator[](int n)
	{
		return _lines[n];
	};

	std::vector<std::wstring> text(const text_selection& selection) const;
	std::vector<std::wstring> text() const;

	std::wstring str() const
	{
		return Combine(text());
	}

	void Path(const std::wstring& path)
	{
		_path = path;
	}

	std::wstring Path() const
	{
		return _path;
	}

	void append_line(const std::wstring& text);

	text_selection replace_text(undo_group& ug, const text_selection& selection, const std::wstring& text);

	text_location insert_text(undo_group& ug, const text_location& location, const std::wstring& text);
	text_location insert_text(undo_group& ug, const text_location& location, const wchar_t& c);
	text_location delete_text(undo_group& ug, const text_selection& selection);
	text_location delete_text(undo_group& ug, const text_location& location);

	

	bool can_undo() const;
	bool can_redo() const;
	text_location undo();
	text_location redo();
	void record_undo(const undo_item& ui);

	bool find(const std::wstring& text, const text_location& ptStartPos, const text_selection& selection, uint32_t dwFlags, bool bWrapSearch, text_location* pptFoundPos);
	void find(const std::wstring& text, uint32_t flags);

	bool can_find_next() const
	{
		return !_find_text.empty();
	};

	void HighlightFromExtension(const wchar_t* ext);

	bool CanPaste();

	bool has_selection() const
	{
		return !_selection.empty();
	};

	bool GetAutoIndent() const;
	bool QueryEditable();

	void Cut();
	void Copy();
	void OnEditDelete();
	void OnEditDeleteBack();
	void OnEditRedo();
	void OnEditTab();
	void OnEditUndo();
	void OnEditUntab();
	void Paste();
	void SetAutoIndent(bool bAutoIndent);

	text_selection selection() const
	{
		return _selection.normalize();
	};

	void MoveCtrlEnd(bool selecting);
	void MoveCtrlHome(bool selecting);
	void MoveDown(bool selecting);
	void MoveEnd(bool selecting);
	void MoveHome(bool selecting);
	void MoveLeft(bool selecting);
	void MoveRight(bool selecting);
	void MoveUp(bool selecting);
	void MoveWordLeft(bool selecting);
	void MoveWordRight(bool selecting);

	bool is_inside_selection(const text_location& loc) const;
	void reset();

	text_location end() const
	{
		auto last_line = _lines.size() - 1;
		return text_location(_lines[last_line].size(), last_line);
	}

	text_selection all() const
	{
		return text_selection(text_location(0, 0), end());
	}

	int tab_size() const
	{
		return _tab_size;
	};

	void tab_size(int nTabSize);
	void view_tabs(bool bViewTabs);
	int max_line_length() const;
	text_location WordToLeft(text_location pt) const;
	text_location WordToRight(text_location pt) const;

	uint32_t highlight_cookie(int lineIndex) const;
	uint32_t highlight_line(uint32_t dwCookie, const document_line& line, highlighter::text_block* pBuf, int& nActualItems) const;

	bool view_tabs() const;
	bool HighlightText(const text_location& ptStartPos, int nLength);

	int calc_offset(int lineIndex, int nCharIndex) const;
	int calc_offset_approx(int lineIndex, int nOffset) const;
	int expanded_line_length(int lineIndex) const;
	std::wstring expanded_chars(const std::wstring& text, int nOffset, int nCount) const;

	const text_location& cursor_pos() const
	{
		return _cursor_loc;
	};

	const text_location& anchor_pos() const
	{
		return _anchor_loc;
	};

	void anchor_pos(const text_location& ptNewAnchor);
	void cursor_pos(const text_location& ptCursorPos);

	void update_max_line_length(int lineIndex) const;

	void move_to(text_location pos, bool selecting)
	{
		auto limit = _lines[pos.y].size();

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
		auto ptStart = from_anchor ? _anchor_loc : pos;
		auto ptEnd = pos;

		if (ptStart < ptEnd || ptStart == ptEnd)
		{
			return text_selection(WordToLeft(ptStart), WordToRight(ptEnd));
		}
		else
		{
			return text_selection(WordToRight(ptStart), WordToLeft(ptEnd));
		}
	}

	text_selection word_selection() const
	{
		if (_cursor_loc < _anchor_loc)
		{
			return text_selection(WordToLeft(_cursor_loc), WordToRight(_anchor_loc));
		}
		else
		{
			return text_selection(WordToLeft(_anchor_loc), WordToRight(_cursor_loc));
		}
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
			_view.update_caret();

			_view.invalidate_lines(selection._start.y, selection._end.y);
			_view.invalidate_lines(_selection._start.y, _selection._end.y);
			_selection = selection;
		}
	}

	void add_word(const std::wstring& word)
	{
		_highlight->add_word(word);
		_view.invalidate_view();
	}

	std::vector<std::wstring> suggest(const std::wstring& word) const
	{
		return _highlight->suggest(word);
	}

	bool can_add(const std::wstring& word) const
	{
		return _highlight->can_add(word);
	}

	friend class undo_group;
};


class undo_group
{
	document& _doc;
	document::undo_item _undo;

public:

	undo_group(document& d) : _doc(d) { }

	~undo_group()
	{
		_doc.record_undo(_undo);
	}

	void insert(const text_location& location, const wchar_t& c)
	{
		_undo._steps.emplace_back(document::undo_step(location, c, true));
	}

	void insert(const text_selection& selection, const std::wstring& text)
	{
		_undo._steps.emplace_back(document::undo_step(selection, text, true));
	}

	void erase(const text_location& location, const wchar_t& c)
	{
		_undo._steps.emplace_back(document::undo_step(location, c, false));
	}

	void erase(const text_selection& selection, const std::wstring& text)
	{
		_undo._steps.emplace_back(document::undo_step(selection, text, false));
	}
};
