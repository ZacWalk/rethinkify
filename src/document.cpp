#include "pch.h"
#include "document.h"


const TCHAR crlf[] = _T("\r\n");

static auto s_textHighlighter = std::make_shared<text_highight>();
static auto s_cppHighlighter = std::make_shared<cpp_highlight>();

document::document(IView& view, const std::wstring& text, line_endings nCrlfStyle) : _view(view),
	_highlight(s_textHighlighter)
{
	_modified = false;
	_create_backup_file = false;
	_undo_pos = 0;
	_line_ending = nCrlfStyle;

	if (text.empty())
	{
		append_line(L"");
	}
	else
	{
		std::wstringstream lines(text);
		std::wstring line;

		while (std::getline(lines, line))
		{
			append_line(line);
		}
	}
}

document::~document() = default;

uint32_t document::highlight_cookie(int lineIndex) const
{
	const auto line_count = _lines.size();

	if (lineIndex < 0 || lineIndex >= line_count)
		return 0;

	auto i = lineIndex;
	while (i >= 0 && _lines[i]._parse_cookie == invalid_length)
		i--;
	i++;

	int nBlocks;
	while (i <= lineIndex && _lines[i]._parse_cookie == invalid_length)
	{
		auto dwCookie = 0;

		if (i > 0)
		{
			dwCookie = _lines[i - 1]._parse_cookie;
		}

		const auto& line = _lines[i];
		line._parse_cookie = _highlight->parse_line(dwCookie, line, nullptr, nBlocks);

		assert(line._parse_cookie != invalid_length);
		i++;
	}

	return _lines[lineIndex]._parse_cookie;
}

uint32_t document::highlight_line(uint32_t cookie, const document_line& line, highlighter::text_block* pBuf,
                                  int& nBlocks) const
{
	return _highlight->parse_line(cookie, line, pBuf, nBlocks);
}

bool document::is_inside_selection(const text_location& ptTextPos) const
{
	const auto sel = _selection.normalize();

	if (ptTextPos.y < sel._start.y)
		return false;
	if (ptTextPos.y > sel._end.y)
		return false;
	if (ptTextPos.y < sel._end.y && ptTextPos.y > sel._start.y)
		return true;
	if (sel._start.y < sel._end.y)
	{
		if (ptTextPos.y == sel._end.y)
			return ptTextPos.x < sel._end.x;
		assert(ptTextPos.y == sel._start.y);
		return ptTextPos.x >= sel._start.x;
	}
	assert(sel._start.y == sel._end.y);
	return ptTextPos.x >= sel._start.x && ptTextPos.x < sel._end.x;
}

void document::reset()
{
	_auto_indent = true;
	_tab_size = 4;
	_max_line_len = -1;
	_ideal_char_pos = -1;
	_anchor_loc.x = 0;
	_anchor_loc.y = 0;

	for (const auto& line : _lines)
	{
		line._parse_cookie = invalid_length;
		line._expanded_length = invalid_length;
	}

	_cursor_loc.x = 0;
	_cursor_loc.y = 0;
	_selection._start = _selection._end = _cursor_loc;
}

void document::tab_size(int tabSize)
{
	assert(tabSize >= 0 && tabSize <= 64);
	if (_tab_size != tabSize)
	{
		_tab_size = tabSize;

		for (const auto& line : _lines)
		{
			line._expanded_length = invalid_length;
		}

		_max_line_len = -1;
		invalidate(invalid::view);
	}
}

int document::max_line_length() const
{
	if (_max_line_len == -1)
	{
		_max_line_len = 0;
		const auto line_count = _lines.size();

		for (int i = 0; i < line_count; i++)
		{
			update_max_line_length(i);
		}
	}

	return _max_line_len;
}

void document::update_max_line_length(int i) const
{
	const int len = expanded_line_length(i);

	if (_max_line_len < len)
		_max_line_len = len;
}

int document::expanded_line_length(int line_index) const
{
	const auto& line = _lines[line_index];

	if (line._expanded_length == -1)
	{
		auto expanded_len = 0;

		if (!line.empty())
		{
			const auto tab_len = tab_size();
			auto line_view = line.view();

			for (;;)
			{
				const auto tab_pos = line_view.find(L'\t');

				if (tab_pos == std::wstring_view::npos)
				{
					expanded_len += line_view.size();
					break;
				}

				expanded_len += tab_pos;
				expanded_len += tab_len - (expanded_len % tab_len);
				line_view = line_view.substr(tab_pos + 1);
			}
		}

		line._expanded_length = expanded_len;
	}

	return line._expanded_length;
}

std::wstring document::expanded_chars(std::wstring_view text, const int offset_in, const int count_in) const
{
	std::wstring result;
	result.reserve(count_in);

	if (count_in > 0)
	{
		auto i_text = text.begin();
		const auto tab_len = tab_size();
		auto actual_offset = 0;

		for (auto i = 0; i < offset_in; i++)
		{
			if (i_text[i] == _T('\t'))
			{
				actual_offset += tab_len - (actual_offset % tab_len);
			}
			else
			{
				actual_offset++;
			}
		}

		i_text += offset_in;

		auto tab_count = 0;

		for (auto i = 0; i < count_in; i++)
		{
			if (i_text[i] == _T('\t'))
			{
				tab_count++;
			}
		}

		if (tab_count > 0 || _view_tabs)
		{
			auto cur_pos = 0;

			for (auto i = 0; i < count_in; i++)
			{
				if (i_text[i] == _T('\t'))
				{
					auto space_count = tab_len - (actual_offset + cur_pos) % tab_len;

					if (_view_tabs)
					{
						result += TAB_CHARACTER;
						cur_pos++;
						space_count--;
					}
					while (space_count > 0)
					{
						result += _T(' ');
						cur_pos++;
						space_count--;
					}
				}
				else
				{
					result += (i_text[i] == _T(' ') && _view_tabs) ? SPACE_CHARACTER : i_text[i];
					cur_pos++;
				}
			}
		}
		else
		{
			result.append(i_text, i_text + count_in);
		}
	}

	return result;
}

int document::calc_offset(int lineIndex, int nCharIndex) const
{
	auto result = 0;

	if (lineIndex >= 0 && lineIndex < _lines.size())
	{
		const auto& line = _lines[lineIndex];
		const auto tabSize = tab_size();

		for (auto i = 0; i < nCharIndex; i++)
		{
			if (line[i] == _T('\t'))
			{
				result += (tabSize - result % tabSize);
			}
			else
			{
				result++;
			}
		}
	}

	return result;
}

int document::calc_offset_approx(int lineIndex, int nOffset) const
{
	if (nOffset == 0)
		return 0;

	const auto& line = _lines[lineIndex];
	const auto nLength = line.size();

	int nCurrentOffset = 0;
	const int tabSize = tab_size();

	for (int i = 0; i < nLength; i++)
	{
		if (line[i] == _T('\t'))
		{
			nCurrentOffset += (tabSize - nCurrentOffset % tabSize);
		}
		else
		{
			nCurrentOffset++;
		}

		if (nCurrentOffset >= nOffset)
		{
			if (nOffset <= nCurrentOffset - tabSize / 2)
				return i;
			return i + 1;
		}
	}
	return nLength;
}


void document::anchor_pos(const text_location& ptNewAnchor)
{
	_anchor_loc = ptNewAnchor;
}

void document::cursor_pos(const text_location& pos)
{
	if (_cursor_loc != pos)
	{
		_cursor_loc = pos;
		_ideal_char_pos = calc_offset(_cursor_loc.y, _cursor_loc.x);
		invalidate(invalid::caret);
	}
}

bool document::highlight_text(const text_location& ptStartPos, int nLength)
{
	_cursor_loc = ptStartPos;
	_cursor_loc.x += nLength;
	_anchor_loc = _cursor_loc;
	select(text_selection(ptStartPos, _cursor_loc));
	_view.ensure_visible(_cursor_loc);
	return true;
}


static size_t find_in_line(std::wstring_view line, std::wstring_view what, const bool whole_word)
{
	size_t cur = 0;

	for (;;)
	{
		const auto found_pos = str::find_in_text(line.substr(cur), what);

		if (found_pos == std::wstring_view::npos)
		{
			return std::wstring_view::npos;
		}

		if (whole_word)
		{
			const auto prev_char = found_pos > 0 ? line[found_pos - 1] : 0;
			const auto next_char = found_pos > 0 ? line[found_pos + what.size() + 1] : 0;
			const auto is_word = !iswalnum(prev_char) && prev_char != _T('_') &&
				!iswalnum(next_char) && next_char != _T('_');

			if (is_word)
			{
				return cur + found_pos;
			}

			cur += found_pos + 1;
		}
		else
		{
			return cur + found_pos;
		}
	}
}


void document::find(const std::wstring_view what, const uint32_t flags)
{
	text_location found_loc;

	const auto start_pos = (flags & find_start_selection) ? _selection._start : _cursor_loc;

	if (find(what, start_pos, all(), flags, true, found_loc))
	{
		_find_text = what;

		auto found_end = found_loc;
		found_end.x += what.size();
		select(text_selection(found_loc, found_end));
	}
}


bool document::find(std::wstring_view what, 
	const text_location& start_pos, 
	const text_selection& selection,
	const uint32_t flags, 
	bool wrap_search, 
	text_location& found_pos) const
{
	auto current_pos = start_pos;

	if (selection.empty() || what.empty())
		return false;

	if (current_pos < selection._start)
	{
		current_pos = selection._start;
	}

	const auto match_case = (flags & find_match_case) != 0;
	const auto whole_word = (flags & find_whole_word) != 0;

	if (flags & find_direction_up)
	{
		for (;;)
		{
			if (current_pos.x == 0)
			{
				current_pos.y--;
			}

			while (current_pos.y >= 0)
			{
				const auto& line = _lines[current_pos.y];

				if (current_pos.x < line._text.size())
				{
					const auto found_x = find_in_line(line._text.substr(current_pos.x), what, whole_word);

					if (found_x != std::wstring_view::npos) //	Found text!
					{
						current_pos.x += found_x;
						found_pos = current_pos;
						return true;
					}
				}

				current_pos.x = 0;
				current_pos.y--;
			}

			//	Beginning of text reached
			if (!wrap_search)
				return false;

			//	Start again from the end of text
			wrap_search = false;
			current_pos = text_location(0, _lines.size() - 1);
		}
	}
	for (;;)
	{
		while (current_pos.y <= selection._end.y)
		{
			const auto& line = _lines[current_pos.y];

			if (current_pos.x < line._text.size())
			{
				const auto found_x = find_in_line(line._text.substr(current_pos.x), what, whole_word);

				if (found_x != std::wstring_view::npos)
				{
					current_pos.x += found_x;

					if (selection._end < current_pos)
						break;

					found_pos = current_pos;
					return true;
				}
			}

			current_pos.x = 0;
			current_pos.y++;
		}

		if (!wrap_search)
			return false;

		wrap_search = false;
		current_pos = selection._start;
	}

	return false;
}

bool document::view_tabs() const
{
	return _view_tabs;
}

void document::view_tabs(const bool bViewTabs)
{
	if (bViewTabs != _view_tabs)
	{
		_view_tabs = bViewTabs;
		invalidate(invalid::view);
	}
}


void document::move_char_left(const bool selecting)
{
	const auto sel = _selection.normalize();

	if (!sel.empty() && !selecting)
	{
		_cursor_loc = sel._start;
	}
	else
	{
		if (_cursor_loc.x == 0)
		{
			if (_cursor_loc.y > 0)
			{
				_cursor_loc.y--;
				_cursor_loc.x = _lines[_cursor_loc.y].size();
			}
		}
		else
			_cursor_loc.x--;
	}
	_ideal_char_pos = calc_offset(_cursor_loc.y, _cursor_loc.x);
	_view.ensure_visible(_cursor_loc);
	if (!selecting)
		_anchor_loc = _cursor_loc;
	select(text_selection(_anchor_loc, _cursor_loc));
}

void document::move_char_right(const bool selecting)
{
	const auto sel = _selection.normalize();

	if (!sel.empty() && !selecting)
	{
		_cursor_loc = sel._end;
	}
	else
	{
		if (_cursor_loc.x == _lines[_cursor_loc.y].size())
		{
			if (_cursor_loc.y < _lines.size() - 1)
			{
				_cursor_loc.y++;
				_cursor_loc.x = 0;
			}
		}
		else
			_cursor_loc.x++;
	}
	_ideal_char_pos = calc_offset(_cursor_loc.y, _cursor_loc.x);
	_view.ensure_visible(_cursor_loc);
	if (!selecting)
		_anchor_loc = _cursor_loc;
	select(text_selection(_anchor_loc, _cursor_loc));
}

void document::move_word_left(const bool selecting)
{
	const auto sel = _selection.normalize();

	if (!sel.empty() && !selecting)
	{
		move_char_left(selecting);
		return;
	}

	if (_cursor_loc.x == 0)
	{
		if (_cursor_loc.y == 0)
			return;

		_cursor_loc.y--;
		_cursor_loc.x = _lines[_cursor_loc.y].size();
	}

	const auto& line = _lines[_cursor_loc.y];
	auto nPos = _cursor_loc.x;

	word_to_left(_cursor_loc);

	while (nPos > 0 && iswspace(line[nPos - 1]))
		nPos--;

	if (nPos > 0)
	{
		nPos--;
		if (iswalnum(line[nPos]) || line[nPos] == _T('_'))
		{
			while (nPos > 0 && (iswalnum(line[nPos - 1]) || line[nPos - 1] == _T('_')))
				nPos--;
		}
		else
		{
			while (nPos > 0 && !iswalnum(line[nPos - 1])
				&& line[nPos - 1] != _T('_') && !iswspace(line[nPos - 1]))
				nPos--;
		}
	}

	_cursor_loc.x = nPos;
	_ideal_char_pos = calc_offset(_cursor_loc.y, _cursor_loc.x);
	_view.ensure_visible(_cursor_loc);
	if (!selecting)
		_anchor_loc = _cursor_loc;
	select(text_selection(_anchor_loc, _cursor_loc));
}

void document::move_word_right(const bool selecting)
{
	const auto sel = _selection.normalize();

	if (!sel.empty() && !selecting)
	{
		move_char_right(selecting);
		return;
	}

	if (_cursor_loc.x == _lines[_cursor_loc.y].size())
	{
		if (_cursor_loc.y == _lines.size() - 1)
			return;
		_cursor_loc.y++;
		_cursor_loc.x = 0;
	}

	const auto nLength = _lines[_cursor_loc.y].size();

	if (_cursor_loc.x == nLength)
	{
		move_char_right(selecting);
		return;
	}

	const auto& line = _lines[_cursor_loc.y];
	int nPos = _cursor_loc.x;

	if (iswalnum(line[nPos]) || line[nPos] == _T('_'))
	{
		while (nPos < nLength && iswalnum(line[nPos]) || line[nPos] == _T('_'))
			nPos++;
	}
	else
	{
		while (nPos < nLength && !iswalnum(line[nPos])
			&& line[nPos] != _T('_') && !iswspace(line[nPos]))
			nPos++;
	}

	while (nPos < nLength && iswspace(line[nPos]))
		nPos++;

	_cursor_loc.x = nPos;
	_ideal_char_pos = calc_offset(_cursor_loc.y, _cursor_loc.x);
	_view.ensure_visible(_cursor_loc);
	if (!selecting)
		_anchor_loc = _cursor_loc;
	select(text_selection(_anchor_loc, _cursor_loc));
}

void document::move_lines(const int lines_to_move, const bool selecting)
{
	const auto sel = _selection.normalize();

	if (!sel.empty() && !selecting)
		_cursor_loc = lines_to_move > 0 ? sel._end : sel._start;

	int yy = _cursor_loc.y + lines_to_move;
	if (yy < 0) yy = 0;
	if (yy >= _lines.size()) yy = _lines.size() - 1;

	if (yy != _cursor_loc.y)
	{
		if (_ideal_char_pos == -1)
			_ideal_char_pos = calc_offset(_cursor_loc.y, _cursor_loc.x);

		_cursor_loc.y = yy;
		_cursor_loc.x = calc_offset_approx(_cursor_loc.y, _ideal_char_pos);

		const auto size = _lines[_cursor_loc.y].size();

		if (_cursor_loc.x > size)
			_cursor_loc.x = size;
	}
	_view.ensure_visible(_cursor_loc);
	if (!selecting)
		_anchor_loc = _cursor_loc;
	select(text_selection(_anchor_loc, _cursor_loc));
}

void document::move_line_home(const bool selecting)
{
	const auto& line = _lines[_cursor_loc.y];

	int nHomePos = 0;
	while (nHomePos < line.size() && iswspace(line[nHomePos]))
		nHomePos++;
	if (nHomePos == line.size() || _cursor_loc.x == nHomePos)
		_cursor_loc.x = 0;
	else
		_cursor_loc.x = nHomePos;
	_ideal_char_pos = calc_offset(_cursor_loc.y, _cursor_loc.x);
	_view.ensure_visible(_cursor_loc);
	if (!selecting)
		_anchor_loc = _cursor_loc;
	select(text_selection(_anchor_loc, _cursor_loc));
}

void document::move_line_end(const bool selecting)
{
	_cursor_loc.x = _lines[_cursor_loc.y].size();
	_ideal_char_pos = calc_offset(_cursor_loc.y, _cursor_loc.x);
	_view.ensure_visible(_cursor_loc);
	if (!selecting)
		_anchor_loc = _cursor_loc;
	select(text_selection(_anchor_loc, _cursor_loc));
}

void document::move_doc_home(const bool selecting)
{
	_cursor_loc.x = 0;
	_cursor_loc.y = 0;
	_ideal_char_pos = calc_offset(_cursor_loc.y, _cursor_loc.x);
	_view.ensure_visible(_cursor_loc);
	if (!selecting)
		_anchor_loc = _cursor_loc;
	select(text_selection(_anchor_loc, _cursor_loc));
}

void document::move_doc_end(const bool selecting)
{
	_cursor_loc.y = _lines.size() - 1;
	_cursor_loc.x = _lines[_cursor_loc.y].size();
	_ideal_char_pos = calc_offset(_cursor_loc.y, _cursor_loc.x);
	_view.ensure_visible(_cursor_loc);
	if (!selecting)
		_anchor_loc = _cursor_loc;
	select(text_selection(_anchor_loc, _cursor_loc));
}

text_location document::word_to_right(text_location pt) const
{
	const auto& line = _lines[pt.y];

	while (pt.x < line.size())
	{
		if (!iswalnum(line[pt.x]) && line[pt.x] != _T('_'))
			break;
		pt.x++;
	}
	return pt;
}

text_location document::word_to_left(text_location pt) const
{
	const auto& line = _lines[pt.y];

	while (pt.x > 0)
	{
		if (!iswalnum(line[pt.x - 1]) && line[pt.x - 1] != _T('_'))
			break;
		pt.x--;
	}
	return pt;
}

void document::Copy() const
{
	if (_selection._start == _selection._end)
		return;

	const auto sel = _selection.normalize();
	_view.text_to_clipboard(str::combine(text(sel), L"\r\n"));
}

bool document::can_paste()
{
	return IsClipboardFormatAvailable(CF_UNICODETEXT) != 0;
}

bool document::QueryEditable()
{
	return true;
}

void document::edit_paste()
{
	if (QueryEditable())
	{
		const auto text = _view.text_from_clipboard();

		if (!text.empty())
		{
			undo_group ug(*this);
			const auto pos = delete_text(ug, selection());
			select(insert_text(ug, pos, text));
		}
	}
}

void document::edit_cut()
{
	if (QueryEditable() && has_selection())
	{
		const auto sel = selection();
		_view.text_to_clipboard(str::combine(text(sel)));

		undo_group ug(*this);
		select(delete_text(ug, sel));
	}
}

void document::edit_delete()
{
	if (QueryEditable())
	{
		auto sel = selection();

		if (sel.empty())
		{
			if (sel._end.x == _lines[sel._end.y].size())
			{
				if (sel._end.y == _lines.size() - 1)
					return;

				sel._end.y++;
				sel._end.x = 0;
			}
			else
			{
				sel._end.x++;
			}
		}

		undo_group ug(*this);
		select(delete_text(ug, sel));
	}
}

void document::edit_delete_back()
{
	if (QueryEditable())
	{
		if (has_selection())
		{
			edit_delete();
		}
		else
		{
			undo_group ug(*this);
			select(delete_text(ug, cursor_pos()));
		}
	}
}

void document::edit_tab()
{
	if (QueryEditable())
	{
		auto sel = selection();

		if (sel._end.y > sel._start.y)
		{
			undo_group ug(*this);

			const int nStartLine = sel._start.y;
			int nEndLine = sel._end.y;
			sel._start.x = 0;

			if (sel._end.x > 0)
			{
				if (sel._end.y == _lines.size() - 1)
				{
					sel._end.x = _lines[sel._end.y].size();
				}
				else
				{
					sel._end.x = 0;
					sel._end.y++;
				}
			}
			else
			{
				nEndLine--;
			}

			select(sel);
			cursor_pos(sel._end);
			_view.ensure_visible(sel._end);

			static const TCHAR pszText[] = _T("\t");

			for (int i = nStartLine; i <= nEndLine; i++)
			{
				insert_text(ug, text_location(0, i), pszText);
			}

			invalidate(invalid::horz_scrollbar);
		}
		else
		{
			undo_group ug(*this);
			const auto pos = delete_text(ug, selection());
			select(insert_text(ug, pos, L'\t'));
		}
	}
}

void document::edit_untab()
{
	if (QueryEditable())
	{
		auto sel = selection();

		if (sel._end.y > sel._start.y)
		{
			undo_group ug(*this);

			const int nStartLine = sel._start.y;
			int nEndLine = sel._end.y;
			sel._start.x = 0;
			if (sel._end.x > 0)
			{
				if (sel._end.y == _lines.size() - 1)
				{
					sel._end.x = _lines[sel._end.y].size();
				}
				else
				{
					sel._end.x = 0;
					sel._end.y++;
				}
			}
			else
				nEndLine--;
			select(sel);
			cursor_pos(sel._end);
			_view.ensure_visible(sel._end);

			for (int i = nStartLine; i <= nEndLine; i++)
			{
				const auto& line = _lines[i];

				if (!line.empty())
				{
					int nPos = 0, nOffset = 0;

					while (nPos < line.size())
					{
						if (line[nPos] == _T(' '))
						{
							nPos++;
							if (++nOffset >= tab_size())
								break;
						}
						else
						{
							if (line[nPos] == _T('\t'))
								nPos++;
							break;
						}
					}

					if (nPos > 0)
					{
						delete_text(ug, text_selection(0, i, nPos, i));
					}
				}
			}

			invalidate(invalid::horz_scrollbar);
		}
		else
		{
			auto ptCursorPos = cursor_pos();

			if (ptCursorPos.x > 0)
			{
				const int tabSize = tab_size();
				const int nOffset = calc_offset(ptCursorPos.y, ptCursorPos.x);
				int nNewOffset = nOffset / tabSize * tabSize;
				if (nOffset == nNewOffset && nNewOffset > 0)
					nNewOffset -= tabSize;
				assert(nNewOffset >= 0);

				const auto& line = _lines[ptCursorPos.y];
				int nCurrentOffset = 0;
				int i = 0;

				while (nCurrentOffset < nNewOffset)
				{
					if (line[i] == _T('\t'))
					{
						nCurrentOffset = nCurrentOffset / tabSize * tabSize + tabSize;
					}
					else
					{
						nCurrentOffset++;
					}

					i++;
				}

				assert(nCurrentOffset == nNewOffset);

				ptCursorPos.x = i;
				select(ptCursorPos);
			}
		}
	}
}

void document::edit_undo()
{
	if (can_undo())
	{
		select(undo());
	}
}

void document::edit_redo()
{
	if (can_redo())
	{
		select(redo());
	}
}

bool document::GetAutoIndent() const
{
	return _auto_indent;
}

void document::SetAutoIndent(const bool bAutoIndent)
{
	_auto_indent = bAutoIndent;
}

static bool is_cpp_extension(std::wstring_view ext)
{
	static auto comp = [](std::wstring_view l, std::wstring_view r)
	{
		return str::icmp(l, r) < 0;
	};
	static std::set<std::wstring_view, std::function<bool(std::wstring_view, std::wstring_view)>> extensions(comp);

	if (extensions.empty())
	{
		extensions.insert(L"c");
		extensions.insert(L"cpp");
		extensions.insert(L"cxx");
		extensions.insert(L"cc");
		extensions.insert(L"h");
		extensions.insert(L"hh");
		extensions.insert(L"hpp");
		extensions.insert(L"hxx");
		extensions.insert(L"inl");
	}

	return extensions.contains(ext);
};

void document::highlight_from_extension(std::wstring_view ext)
{
	if (ext[0] == L'.') ext = ext.substr(1);

	if (is_cpp_extension(ext))
	{
		_highlight = s_cppHighlighter;
	}
	else
	{
		_highlight = s_textHighlighter;
	}
}

void document::append_line(std::wstring_view text)
{
	_lines.emplace_back(text);
}

void document::clear()
{
	_lines.clear();
	_undo.clear();
	_undo_pos = 0;
	append_line(L"");
	reset();
}

static const char* crlfs[] =
{
	"\x0d\x0a", //	DOS/Windows style
	"\x0a\x0d", //	UNIX style
	"\x0a" //	Macintosh style
};

static std::istream& safe_get_line(std::istream& is, std::string& t)
{
	t.clear();

	// The characters in the stream are read one-by-one using a std::streambuf.
	// That is faster than reading them one-by-one using the std::istream.
	// Code that uses streambuf this way must be guarded by a sentry object.
	// The sentry object performs various tasks,
	// such as thread synchronization and updating the stream state.

	std::istream::sentry se(is, true);
	const auto sb = is.rdbuf();

	for (;;)
	{
		const auto c = sb->sbumpc();
		switch (c)
		{
		case '\n':
			return is;
		case '\r':
			if (sb->sgetc() == '\n')
				sb->sbumpc();
			return is;
		case EOF:
			// Also handle the case when the last line has no line ending
			if (t.empty())
				is.setstate(std::ios::eofbit);
			return is;
		default:
			t += static_cast<char>(c);
		}
	}
}

enum class Encoding
{
	UTF32,
	UTF32BE,
	UTF8,
	UTF16BE,
	UTF16,
	ASCII,
};

struct _BOM_LOOKUP
{
	uint32_t bom;
	ULONG len;
	Encoding type;
};

struct _BOM_LOOKUP BOMLOOK[] =
{
	// define longest headers first
	{0x0000FEFF, 4, Encoding::UTF32},
	{0xFFFE0000, 4, Encoding::UTF32BE},
	{0xBFBBEF, 3, Encoding::UTF8},
	{0xFFFE, 2, Encoding::UTF16BE},
	{0xFEFF, 2, Encoding::UTF16},
	{0, 0, Encoding::ASCII},
};

//
//	00 00 FE FF			UTF-32, big-endian 
//	FF FE 00 00			UTF-32, little-endian 
//	FE FF				UTF-16, big-endian 
//	FF FE				UTF-16, little-endian 
//	EF BB BF			UTF-8 
//
static Encoding detect_encoding(const uint8_t* header, size_t filesize, int& headerLen)
{
	for (auto i = 0; BOMLOOK[i].len; i++)
	{
		if (filesize >= BOMLOOK[i].len &&
			memcmp(header, &BOMLOOK[i].bom, BOMLOOK[i].len) == 0)
		{
			headerLen = BOMLOOK[i].len;
			return BOMLOOK[i].type;
		}
	}

	headerLen = 0;

	if (header[0] == 0 && header[1] != 0 && header[2] == 0 && header[3] != 0)
	{
		return Encoding::UTF16;
	}

	if (header[0] != 0 && header[1] == 0 && header[2] != 0 && header[3] == 0)
	{
		return Encoding::UTF16BE;
	}

	return Encoding::ASCII;
}

static line_endings detect_line_endings(const uint8_t* buffer, const int len)
{
	auto i = 0;

	for (i = 0; i < len; i++)
	{
		if (buffer[i] == 0x0a)
			break;
	}
	if (i < len)
	{
		if (i > 0 && buffer[i - 1] == 0x0d)
		{
			return line_endings::crlf_style_dos;
		}

		return (i < len - 1 && buffer[i + 1] == 0x0d) ? line_endings::crlf_style_unix : line_endings::crlf_style_mac;
	}

	return line_endings::crlf_style_dos; // guess
}

bool document::load_from_file(const file_path &path)
{
	if (_path == path)
	{
		// skip if same
		return false;
	}

	clear();

	auto success = false;
	const auto hFile = ::CreateFile(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
	                                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);

	if (hFile != INVALID_HANDLE_VALUE)
	{
		const auto bufferLen = 1024 * 64;
		uint8_t buffer[bufferLen];

		DWORD readLen;
		if (ReadFile(hFile, buffer, bufferLen, &readLen, nullptr))
		{
			_lines.clear();

			auto headerLen = 0;
			const auto size = GetFileSize(hFile, nullptr);
			const auto encoding = detect_encoding(buffer, size, headerLen);

			_line_ending = detect_line_endings(buffer, readLen);
			auto crlf = crlfs[static_cast<int>(_line_ending)];

			auto bufferPos = headerLen;
			auto last_char = 0;

			if (encoding == Encoding::UTF8 || encoding == Encoding::ASCII)
			{
				std::string line;

				while (readLen > 0)
				{
					const int c = buffer[bufferPos];

					if ((last_char == 0x0A && c == 0x0D) || (last_char == 0x0A && c != 0x0D))
					{
						append_line((encoding == Encoding::ASCII) ? str::ascii_to_utf16(line) : str::utf8_to_utf16(line));
						line.clear();
					}

					if (c != 0x0A && c != 0x0D)
					{
						line += static_cast<char>(c);
					}

					bufferPos++;

					if (bufferPos == readLen)
					{
						if (ReadFile(hFile, buffer, bufferLen, &readLen, nullptr))
						{
							bufferPos = 0;
						}
						else
						{
							readLen = 0;
						}
					}

					last_char = c;
				}

				append_line((encoding == Encoding::ASCII) ? str::ascii_to_utf16(line) : str::utf8_to_utf16(line));
			}
			else if (encoding == Encoding::UTF16BE || encoding == Encoding::UTF16)
			{
				const auto buffer16 = reinterpret_cast<const wchar_t*>(buffer);
				readLen /= 2;

				std::wstring line;

				while (readLen > 0)
				{
					auto c = buffer16[bufferPos];

					if (encoding == Encoding::UTF16)
					{
						c = _byteswap_ushort(c);
					}

					if (c != 0x0A && c != 0x0D)
					{
						line += c;
					}

					if (c == 0x0D)
					{
						append_line(line);
						line.clear();
					}

					bufferPos++;

					if (bufferPos == readLen)
					{
						if (ReadFile(hFile, buffer, bufferLen, &readLen, nullptr))
						{
							bufferPos = 0;
							readLen /= 2;
						}
						else
						{
							readLen = 0;
						}
					}
				}

				append_line(line);
			}

			_modified = false;
			_undo_pos = 0;

			success = true;

			highlight_from_extension(path.extension());

			_path = path;
			invalidate(invalid::view | invalid::title);
		}

		if (hFile != nullptr)
			CloseHandle(hFile);
	}

	if (_lines.empty())
	{
		append_line(L"");
	}

	reset();

	return success;
}


static std::wstring temp_file_path()
{
	wchar_t result[MAX_PATH + 1] = {0};
	GetTempPath(MAX_PATH, result);
	GetTempFileName(result, L"rethinkify.", 0, result);
	return result;
}

// Create a string with last error message
static std::wstring last_error_message()
{
	std::wstring result;
	const auto error = GetLastError();

	if (error)
	{
		LPVOID lpMsgBuf;
		const auto bufLen = FormatMessageW(
			FORMAT_MESSAGE_ALLOCATE_BUFFER |
			FORMAT_MESSAGE_FROM_SYSTEM |
			FORMAT_MESSAGE_IGNORE_INSERTS,
			nullptr,
			error,
			MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
			reinterpret_cast<LPWSTR>(&lpMsgBuf),
			0, nullptr);

		if (bufLen)
		{
			result = static_cast<const wchar_t*>(lpMsgBuf);
			LocalFree(lpMsgBuf);
		}
	}
	return result;
}

bool document::save_to_file(const file_path &path, line_endings nCrlfStyle /*= CRLF_STYLE_AUTOMATIC*/,
                            bool bClearModifiedFlag /*= true*/) const
{
	auto success = false;
	const auto tempPath = temp_file_path();
	std::ofstream stream(tempPath);

	if (stream)
	{
		auto first = true;

		static uint8_t smarker[3] = {0xEF, 0xBB, 0xBF};
		stream.write(reinterpret_cast<const char*>(smarker), 3);

		for (const auto& line : _lines)
		{
			if (!first) stream << std::endl;
			stream << str::utf16_to_utf8(line._text);
			first = false;
		}

		stream.close();

		success = ::MoveFileEx(tempPath.c_str(), path.c_str(),
		                       MOVEFILE_COPY_ALLOWED | MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;

		if (success && bClearModifiedFlag)
		{
			_modified = false;
		}

		if (!success)
		{
			MessageBox(GetActiveWindow(), last_error_message().c_str(), L"Diffractor", MB_OK);
		}
	}

	return success;
}


std::vector<std::wstring> document::text(const text_selection& selection) const
{
	std::vector<std::wstring> result;

	if (!selection.empty())
	{
		if (selection._start.y == selection._end.y)
		{
			result.emplace_back(
				_lines[selection._start.y]._text.substr(selection._start.x, selection._end.x - selection._start.x));
		}
		else
		{
			for (int y = selection._start.y; y <= selection._end.y; y++)
			{
				const auto& text = _lines[y]._text;

				if (y == selection._start.y)
				{
					result.emplace_back(text.substr(selection._start.x, text.size() - selection._start.x));
				}
				else if (y == selection._end.y)
				{
					result.emplace_back(text.substr(0, selection._end.x));
				}
				else
				{
					result.emplace_back(text);
				}
			}
		}
	}

	return result;
}

bool document::can_undo() const
{
	assert(_undo_pos >= 0 && _undo_pos <= _undo.size());
	return _undo_pos > 0;
}

bool document::can_redo() const
{
	assert(_undo_pos >= 0 && _undo_pos <= _undo.size());
	return _undo_pos < _undo.size();
}

text_location document::undo()
{
	assert(can_undo());
	_undo_pos--;
	_modified = true;
	return _undo[_undo_pos].undo(*this);
}

text_location document::redo()
{
	assert(can_redo());
	_modified = true;
	const auto result = _undo[_undo_pos].redo(*this);
	_undo_pos++;
	return result;
}

void document::record_undo(const undo_item& ui)
{
	_undo.erase(_undo.begin() + _undo_pos, _undo.end());
	_undo.emplace_back(ui);
	_undo_pos = _undo.size();
}

text_selection document::replace_text(undo_group& ug, const text_selection& selection, std::wstring_view text)
{
	text_selection result;
	result._start = delete_text(ug, selection);
	result._end = insert_text(ug, selection._start, text);
	return result;
}

text_location document::insert_text(const text_location& location, std::wstring_view text)
{
	auto resultLocation = location;

	for (const auto& c : text)
	{
		resultLocation = insert_text(resultLocation, c);
	}

	_modified = true;

	return resultLocation;
}

text_location document::insert_text(undo_group& ug, const text_location& location, std::wstring_view text)
{
	auto result_location = location;

	for (const auto& c : text)
	{
		result_location = insert_text(result_location, c);
	}

	ug.insert(text_selection(location, result_location), text);
	_modified = true;

	return result_location;
}

text_location document::insert_text(const text_location& location, const wchar_t& c)
{
	text_location resultLocation = location;
	auto& li = _lines[location.y];

	if (c == L'\n')
	{
		// Split
		const document_line newLine(li._text.substr(location.x, li._text.size()));
		_lines.insert(_lines.begin() + location.y + 1, newLine);
		_lines[location.y]._text = _lines[location.y]._text.substr(0, location.x);

		resultLocation.y = location.y + 1;
		resultLocation.x = 0;

		invalidate(invalid::view);
	}
	else if (c != '\r')
	{
		auto before = li._text;
		auto after = li._text;

		after.insert(after.begin() + location.x, c);

		li._text = after;

		resultLocation.y = location.y;
		resultLocation.x = location.x + 1;

		invalidate_line(location.y);
	}

	return resultLocation;
}

text_location document::insert_text(undo_group& ug, const text_location& location, const wchar_t& c)
{
	ug.insert(location, c);
	_modified = true;
	return insert_text(location, c);
}

text_location document::delete_text(undo_group& ug, const text_location& location)
{
	if (location.x == 0)
	{
		if (location.y > 0)
		{
			ug.erase(text_location(_lines[location.y - 1].size(), location.y - 1),
			         location.x > 0 ? _lines[location.y][location.x - 1] : '\n');
		}
	}
	else
	{
		ug.erase(text_location(location.x - 1, location.y), _lines[location.y][location.x - 1]);
	}

	_modified = true;
	return delete_text(location);
}

text_location document::delete_text(const text_selection& selection)
{
	if (!selection.empty())
	{
		if (selection._start.y == selection._end.y)
		{
			auto& li = _lines[selection._start.y];
			auto before = li._text;
			auto after = li._text;

			after.erase(after.begin() + selection._start.x, after.begin() + selection._end.x);
			li._text = after;

			if (end() < _cursor_loc) _cursor_loc = end();

			invalidate_line(selection._start.y);
		}
		else
		{
			auto& line_start = _lines[selection._start.y];
			auto& line_end = _lines[selection._end.y];

			line_start._text.erase(line_start._text.begin() + selection._start.x, line_start._text.end());
			line_start._text.append(line_end._text.begin() + selection._end.x, line_end._text.end());

			if (selection._start.y + 1 < selection._end.y + 1)
			{
				_lines.erase(_lines.begin() + selection._start.y + 1, _lines.begin() + selection._end.y + 1);
			}

			if (end() < _cursor_loc) _cursor_loc = end();

			invalidate(invalid::view);
		}
	}


	return selection._start;
}

text_location document::delete_text(undo_group& ug, const text_selection& selection)
{
	ug.erase(selection, str::combine(text(selection)));
	_modified = true;
	return delete_text(selection);
}

text_location document::delete_text(const text_location& location)
{
	auto& line = _lines[location.y];
	auto resultPos = location;

	if (location.x == 0)
	{
		if (location.y > 0)
		{
			auto& previous = _lines[location.y - 1];

			resultPos.x = previous.size();
			resultPos.y = location.y - 1;

			previous._text.insert(previous._text.end(), line._text.begin(), line._text.end());
			_lines.erase(_lines.begin() + location.y);

			invalidate(invalid::view);
		}
	}
	else
	{
		auto& li = _lines[location.y];
		auto before = li._text;
		auto after = li._text;

		after.erase(after.begin() + location.x - 1, after.begin() + location.x);
		li._text = after;

		resultPos.x = location.x - 1;
		resultPos.y = location.y;

		invalidate_line(location.y);
	}

	return resultPos;
}
