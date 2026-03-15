// document.cpp — Document model implementation: text manipulation, undo/redo, file I/O

#include "pch.h"
#include "platform.h"
#include "app.h"
#include "document.h"

// Standalone spell checker state — shared across all documents
static std::unique_ptr<pf::spell_checker> s_checker;
static std::unordered_map<uint64_t, bool> s_spell_cache;

static void ensure_spell_checker()
{
	if (!s_checker)
		s_checker = pf::create_spell_checker();
}

bool spell_check_word(const std::wstring_view word)
{
	ensure_spell_checker();
	if (!s_checker) return true;

	const auto key = fnv1a_i_64(word);
	const auto it = s_spell_cache.find(key);
	if (it != s_spell_cache.end()) return it->second;

	const auto valid = s_checker->is_word_valid(word);
	s_spell_cache[key] = valid;
	return valid;
}

std::vector<std::wstring> spell_suggest(const std::wstring_view word)
{
	ensure_spell_checker();
	if (s_checker) return s_checker->suggest(word);
	return {};
}

void spell_add_word(const std::wstring_view word)
{
	ensure_spell_checker();
	if (s_checker)
	{
		s_checker->add_word(word);
		s_spell_cache[fnv1a_i_64(word)] = true;
	}
}

loaded_file_data load_lines(const file_path& path)
{
	loaded_file_data data;
	const auto hFile = pf::open_for_read(path);

	if (hFile != nullptr)
	{
		if (is_binary_file(path))
		{
			uint8_t line[16];
			uint32_t readLen = 0;

			while (hFile->read(line, 16, &readLen) && readLen > 0)
			{
				std::wstring linew(readLen, L'\0');

				for (uint32_t i = 0; i < readLen; ++i)
				{
					linew[i] = line[i];
				}
				data.lines.emplace_back(std::move(linew));
			}

			data.endings = line_endings::binary;
			data.encoding = file_encoding::binary;
		}
		else
		{
			const auto info = iterate_file_lines(hFile, [&data](const std::wstring& line, int)
			{
				data.lines.emplace_back(line);
			});

			data.endings = info.endings;
			data.encoding = info.enc;
		}

		data.disk_modified_time = pf::file_modified_time(path);
	}

	return data;
}

document::document(document_events& events, const std::wstring_view text,
                   const bool is_overlay,
                   const line_endings nCrlfStyle) :
	_events(events),
	_is_overlay(is_overlay)
{
	_modified = false;
	_create_backup_file = false;
	_undo_pos = 0;
	_saved_undo_pos = 0;
	_line_ending = nCrlfStyle;

	if (text.empty())
	{
		append_line(L"");
	}
	else
	{
		size_t start = 0;
		for (size_t i = 0; i < text.size(); i++)
		{
			if (text[i] == L'\n')
			{
				auto end = i;
				if (end > start && text[end - 1] == L'\r')
					end--;
				append_line(text.substr(start, end - start));
				start = i + 1;
			}
		}
		auto end = text.size();
		if (end > start && text[end - 1] == L'\r')
			end--;
		append_line(text.substr(start, end - start));
	}
}

document::document(document_events& events, const file_path& path, const uint64_t disk_modified_time,
                   const file_encoding encoding) : _events(events), _path(path),
                                                   _disk_modified_time(disk_modified_time), _encoding(encoding)
{
}

document::~document() = default;

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
		line.invalidate_expanded_length();
	}

	_cursor_loc = {};
	_selection._start = _selection._end = _cursor_loc;
}

void document::tab_size(const int tabSize)
{
	assert(tabSize >= 0 && tabSize <= 64);
	if (_tab_size != tabSize)
	{
		_tab_size = tabSize;

		for (const auto& line : _lines)
		{
			line.invalidate_expanded_length();
		}

		_max_line_len = -1;
		_events.invalidate(invalid::view);
	}
}

int document::max_line_length() const
{
	if (_max_line_len == -1)
	{
		_max_line_len = 0;
		const auto line_count = std::ssize(_lines);

		for (int i = 0; i < line_count; i++)
		{
			update_max_line_length(i);
		}
	}

	return _max_line_len;
}

void document::update_max_line_length(const int i) const
{
	const int len = expanded_line_length(i);

	if (_max_line_len < len)
		_max_line_len = len;
}

int document::expanded_line_length(const int line_index) const
{
	const auto& line = _lines[line_index];

	if (line.expanded_length_cache() == invalid_length)
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
					expanded_len += static_cast<int>(line_view.size());
					break;
				}

				expanded_len += static_cast<int>(tab_pos);
				expanded_len += tab_len - expanded_len % tab_len;
				line_view = line_view.substr(tab_pos + 1);
			}
		}

		line.set_expanded_length(expanded_len);
	}

	return line.expanded_length_cache();
}

std::wstring document::expanded_chars(const std::wstring_view text, const int offset_in, const int count_in) const
{
	std::wstring result;
	expanded_chars(text, offset_in, count_in, result);
	return result;
}

void document::expanded_chars(std::wstring_view text, const int offset_in, const int count_in,
                              std::wstring& result) const
{
	result.clear();

	if (count_in > 0)
	{
		const auto text_len = static_cast<int>(text.size());
		if (offset_in < 0 || count_in < 0 || offset_in + count_in > text_len)
			return;

		auto i_text = text.begin();
		const auto tab_len = tab_size();
		auto actual_offset = 0;

		for (auto i = 0; i < offset_in; i++)
		{
			if (i_text[i] == L'\t')
			{
				actual_offset += tab_len - actual_offset % tab_len;
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
			if (i_text[i] == L'\t')
			{
				tab_count++;
			}
		}

		if (tab_count > 0 || _view_tabs)
		{
			auto cur_pos = 0;

			for (auto i = 0; i < count_in; i++)
			{
				if (i_text[i] == L'\t')
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
						result += L' ';
						cur_pos++;
						space_count--;
					}
				}
				else
				{
					result += i_text[i] == L' ' && _view_tabs ? SPACE_CHARACTER : i_text[i];
					cur_pos++;
				}
			}
		}
		else
		{
			result.append(i_text, i_text + count_in);
		}
	}
}

int document::calc_offset(const int lineIndex, const int nCharIndex) const
{
	auto result = 0;

	if (lineIndex >= 0 && lineIndex < std::ssize(_lines))
	{
		const auto& line = _lines[lineIndex];
		const auto tabSize = tab_size();
		const auto charLimit = std::min(nCharIndex, static_cast<int>(line.size()));

		for (auto i = 0; i < charLimit; i++)
		{
			if (line[i] == L'\t')
			{
				result += tabSize - result % tabSize;
			}
			else
			{
				result++;
			}
		}
	}

	return result;
}

int document::calc_offset_approx(const int lineIndex, const int nOffset) const
{
	if (nOffset == 0)
		return 0;

	if (lineIndex < 0 || lineIndex >= std::ssize(_lines))
		return 0;

	const auto& line = _lines[lineIndex];
	const auto nLength = static_cast<int>(line.size());

	int nCurrentOffset = 0;
	const int tabSize = tab_size();

	for (int i = 0; i < nLength; i++)
	{
		if (line[i] == L'\t')
		{
			nCurrentOffset += tabSize - nCurrentOffset % tabSize;
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
		_events.invalidate(invalid::caret);
	}
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
		_events.invalidate(invalid::view);
	}
}

void document::finalize_move(const bool selecting)
{
	_ideal_char_pos = calc_offset(_cursor_loc.y, _cursor_loc.x);
	_events.ensure_visible(_cursor_loc);
	if (!selecting)
		_anchor_loc = _cursor_loc;
	select(text_selection(_anchor_loc, _cursor_loc));
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
				_cursor_loc.x = static_cast<int>(_lines[_cursor_loc.y].size());
			}
		}
		else
			_cursor_loc.x--;
	}
	finalize_move(selecting);
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
		if (_cursor_loc.x == static_cast<int>(_lines[_cursor_loc.y].size()))
		{
			if (_cursor_loc.y < static_cast<int>(_lines.size()) - 1)
			{
				_cursor_loc.y++;
				_cursor_loc.x = 0;
			}
		}
		else
			_cursor_loc.x++;
	}
	finalize_move(selecting);
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
		_cursor_loc.x = static_cast<int>(_lines[_cursor_loc.y].size());
	}

	const auto& line = _lines[_cursor_loc.y];
	auto nPos = _cursor_loc.x;

	while (nPos > 0 && iswspace(line[nPos - 1]))
		nPos--;

	if (nPos > 0)
	{
		nPos--;
		if (iswalnum(line[nPos]) || line[nPos] == L'_')
		{
			while (nPos > 0 && (iswalnum(line[nPos - 1]) || line[nPos - 1] == L'_'))
				nPos--;
		}
		else
		{
			while (nPos > 0 && !iswalnum(line[nPos - 1])
				&& line[nPos - 1] != L'_' && !iswspace(line[nPos - 1]))
				nPos--;
		}
	}

	_cursor_loc.x = nPos;
	finalize_move(selecting);
}

void document::move_word_right(const bool selecting)
{
	const auto sel = _selection.normalize();

	if (!sel.empty() && !selecting)
	{
		move_char_right(selecting);
		return;
	}

	const auto line_len = static_cast<int>(_lines[_cursor_loc.y].size());

	if (_cursor_loc.x == line_len)
	{
		if (_cursor_loc.y == static_cast<int>(_lines.size()) - 1)
			return;
		_cursor_loc.y++;
		_cursor_loc.x = 0;
	}

	const auto nLength = static_cast<int>(_lines[_cursor_loc.y].size());

	if (_cursor_loc.x == nLength)
	{
		move_char_right(selecting);
		return;
	}

	const auto& line = _lines[_cursor_loc.y];
	int nPos = _cursor_loc.x;

	if (iswalnum(line[nPos]) || line[nPos] == L'_')
	{
		while (nPos < nLength && (iswalnum(line[nPos]) || line[nPos] == L'_'))
			nPos++;
	}
	else
	{
		while (nPos < nLength && !iswalnum(line[nPos])
			&& line[nPos] != L'_' && !iswspace(line[nPos]))
			nPos++;
	}

	while (nPos < nLength && iswspace(line[nPos]))
		nPos++;

	_cursor_loc.x = nPos;
	finalize_move(selecting);
}

void document::move_lines(const int lines_to_move, const bool selecting)
{
	const auto sel = _selection.normalize();
	const auto line_count = static_cast<int>(_lines.size());

	if (!sel.empty() && !selecting)
		_cursor_loc = lines_to_move > 0 ? sel._end : sel._start;

	int yy = _cursor_loc.y + lines_to_move;
	if (yy < 0) yy = 0;
	if (yy >= line_count) yy = line_count - 1;

	if (yy != _cursor_loc.y)
	{
		if (_ideal_char_pos == -1)
			_ideal_char_pos = calc_offset(_cursor_loc.y, _cursor_loc.x);

		_cursor_loc.y = yy;
		_cursor_loc.x = calc_offset_approx(_cursor_loc.y, _ideal_char_pos);

		const auto size = static_cast<int>(_lines[_cursor_loc.y].size());

		if (_cursor_loc.x > size)
			_cursor_loc.x = size;
	}
	_events.ensure_visible(_cursor_loc);
	if (!selecting)
		_anchor_loc = _cursor_loc;
	select(text_selection(_anchor_loc, _cursor_loc));
}

void document::move_line_home(const bool selecting)
{
	const auto& line = _lines[_cursor_loc.y];
	const auto line_len = static_cast<int>(line.size());

	int nHomePos = 0;
	while (nHomePos < line_len && iswspace(line[nHomePos]))
		nHomePos++;
	if (nHomePos == line_len || _cursor_loc.x == nHomePos)
		_cursor_loc.x = 0;
	else
		_cursor_loc.x = nHomePos;
	finalize_move(selecting);
}

void document::move_line_end(const bool selecting)
{
	_cursor_loc.x = static_cast<int>(_lines[_cursor_loc.y].size());
	finalize_move(selecting);
}

void document::move_doc_home(const bool selecting)
{
	_cursor_loc.x = 0;
	_cursor_loc.y = 0;
	finalize_move(selecting);
}

void document::move_doc_end(const bool selecting)
{
	_cursor_loc.y = static_cast<int>(_lines.size()) - 1;
	_cursor_loc.x = static_cast<int>(_lines[_cursor_loc.y].size());
	finalize_move(selecting);
}

text_location document::word_to_right(text_location pt) const
{
	const auto& line = _lines[pt.y];

	while (pt.x < line.size())
	{
		if (!iswalnum(line[pt.x]) && line[pt.x] != L'_')
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
		if (!iswalnum(line[pt.x - 1]) && line[pt.x - 1] != L'_')
			break;
		pt.x--;
	}
	return pt;
}

std::wstring document::copy() const
{
	if (_selection._start == _selection._end)
		return {};

	const auto sel = _selection.normalize();
	return str::combine(text(sel), L"\r\n");
}

bool document::can_paste()
{
	return pf::platform_clipboard_has_text();
}

bool document::query_editable() const
{
	return !_read_only;
}

void document::edit_paste(const std::wstring_view text)
{
	if (query_editable() && !text.empty())
	{
		undo_group ug(shared_from_this());
		const auto pos = delete_text(ug, selection());
		select(insert_text(ug, pos, text));
	}
}

std::wstring document::edit_cut()
{
	if (query_editable() && has_selection())
	{
		const auto sel = selection();
		auto result = str::combine(text(sel), L"\r\n");

		undo_group ug(shared_from_this());
		select(delete_text(ug, sel));
		return result;
	}
	return {};
}

void document::edit_delete()
{
	if (query_editable())
	{
		auto sel = selection();

		if (sel.empty())
		{
			if (sel._end.x == static_cast<int>(_lines[sel._end.y].size()))
			{
				if (sel._end.y == static_cast<int>(_lines.size()) - 1)
					return;

				sel._end.y++;
				sel._end.x = 0;
			}
			else
			{
				sel._end.x++;
			}
		}

		undo_group ug(shared_from_this());
		select(delete_text(ug, sel));
	}
}

void document::edit_delete_back()
{
	if (query_editable())
	{
		if (has_selection())
		{
			edit_delete();
		}
		else
		{
			undo_group ug(shared_from_this());
			select(delete_text(ug, cursor_pos()));
		}
	}
}

document::block_range document::prepare_block_selection(text_selection& sel)
{
	const int nStartLine = sel._start.y;
	int nEndLine = sel._end.y;
	sel._start.x = 0;

	if (sel._end.x > 0)
	{
		if (sel._end.y == static_cast<int>(_lines.size()) - 1)
		{
			sel._end.x = static_cast<int>(_lines[sel._end.y].size());
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
	_events.ensure_visible(sel._end);

	return {nStartLine, nEndLine};
}

void document::edit_tab()
{
	if (query_editable())
	{
		auto sel = selection();

		if (sel._end.y > sel._start.y)
		{
			undo_group ug(shared_from_this());
			const auto [nStartLine, nEndLine] = prepare_block_selection(sel);

			static constexpr wchar_t pszText[] = L"\t";

			for (int i = nStartLine; i <= nEndLine; i++)
			{
				insert_text(ug, text_location(0, i), pszText);
			}

			_events.invalidate(invalid::horz_scrollbar);
		}
		else
		{
			undo_group ug(shared_from_this());
			const auto pos = delete_text(ug, selection());
			select(insert_text(ug, pos, L'\t'));
		}
	}
}

void document::edit_untab()
{
	if (query_editable())
	{
		auto sel = selection();

		if (sel._end.y > sel._start.y)
		{
			undo_group ug(shared_from_this());
			const auto [nStartLine, nEndLine] = prepare_block_selection(sel);

			for (int i = nStartLine; i <= nEndLine; i++)
			{
				const auto& line = _lines[i];

				if (!line.empty())
				{
					int nPos = 0, nOffset = 0;

					while (nPos < line.size())
					{
						if (line[nPos] == L' ')
						{
							nPos++;
							if (++nOffset >= tab_size())
								break;
						}
						else
						{
							if (line[nPos] == L'\t')
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

			_events.invalidate(invalid::horz_scrollbar);
		}
		else
		{
			auto ptCursorPos = cursor_pos();

			if (ptCursorPos.x > 0)
			{
				const int tabSize = tab_size();
				if (tabSize == 0) return;
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
					if (line[i] == L'\t')
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

bool document::get_auto_indent() const
{
	return _auto_indent;
}

void document::set_auto_indent(const bool bAutoIndent)
{
	_auto_indent = bAutoIndent;
}


static bool is_spell_check_extension(const std::wstring_view ext)
{
	static const std::set<std::wstring_view, iless> extensions = {
		L"md", L"txt"
	};

	return extensions.contains(ext);
}


void document::select(const text_selection& selection)
{
	if (_selection != selection)
	{
		assert(selection.is_valid());

		anchor_pos(selection._start);
		cursor_pos(selection._end);

		_events.ensure_visible(selection._end);
		_events.invalidate(invalid::caret);

		_events.invalidate_lines(selection._start.y, selection._end.y);
		_events.invalidate_lines(_selection._start.y, _selection._end.y);
		_selection = selection;
	}
}

void document::invalidate_line(const int index)
{
	const auto& line = _lines[index];
	const auto old_len = line.expanded_length_cache();
	line.invalidate_expanded_length();

	_events.invalidate_lines(index, index + 1);

	const int new_len = expanded_line_length(index);

	if (new_len >= _max_line_len)
	{
		_max_line_len = new_len;
	}
	else if (old_len == _max_line_len)
	{
		// The previously longest line got shorter — must rescan
		_max_line_len = -1;
	}

	_events.invalidate(invalid::horz_scrollbar);
}

void document::toggle_spell_check()
{
	_spell_check = !_spell_check;
	_events.invalidate(invalid::view);
}

void document::path(const file_path& path_in)
{
	_path = path_in;
	_events.invalidate(invalid::title);
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
	_read_only = false;
	append_line(L"");
	reset();
}

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

// BOM byte signatures:
//   FF FE 00 00  UTF-32, little-endian
//   00 00 FE FF  UTF-32, big-endian
//   EF BB BF     UTF-8
//   FF FE        UTF-16, little-endian
//   FE FF        UTF-16, big-endian
//
file_encoding detect_encoding(const uint8_t* header, const size_t filesize, int& headerLen)
{
	if (filesize >= 4 && header[0] == 0xFF && header[1] == 0xFE && header[2] == 0x00 && header[3] == 0x00)
	{
		headerLen = 4;
		return file_encoding::utf32;
	}
	if (filesize >= 4 && header[0] == 0x00 && header[1] == 0x00 && header[2] == 0xFE && header[3] == 0xFF)
	{
		headerLen = 4;
		return file_encoding::utf32be;
	}
	if (filesize >= 3 && header[0] == 0xEF && header[1] == 0xBB && header[2] == 0xBF)
	{
		headerLen = 3;
		return file_encoding::utf8;
	}
	if (filesize >= 2 && header[0] == 0xFF && header[1] == 0xFE)
	{
		headerLen = 2;
		return file_encoding::utf16;
	}
	if (filesize >= 2 && header[0] == 0xFE && header[1] == 0xFF)
	{
		headerLen = 2;
		return file_encoding::utf16be;
	}

	headerLen = 0;

	if (filesize >= 4 && header[0] != 0 && header[1] == 0 && header[2] != 0 && header[3] == 0)
		return file_encoding::utf16; // little-endian: char,0,char,0
	if (filesize >= 4 && header[0] == 0 && header[1] != 0 && header[2] == 0 && header[3] != 0)
		return file_encoding::utf16be; // big-endian: 0,char,0,char

	return file_encoding::utf8;
}

line_endings detect_line_endings(const uint8_t* buffer, const int len)
{
	for (int i = 0; i < len; i++)
	{
		if (buffer[i] == 0x0d)
		{
			// \r\n = DOS, \r alone = Mac
			if (i + 1 < len && buffer[i + 1] == 0x0a)
				return line_endings::crlf_style_dos;
			return line_endings::crlf_style_mac;
		}
		if (buffer[i] == 0x0a)
		{
			// \n alone = Unix
			return line_endings::crlf_style_unix;
		}
	}

	return line_endings::crlf_style_dos; // guess
}

file_lines_info iterate_file_lines(const pf::file_handle_ptr& handle,
                                   const std::function<void(const std::wstring&, int)>& on_line)
{
	file_lines_info info;

	constexpr uint32_t buf_size = 64 * 1024;
	uint8_t buffer[buf_size];
	uint32_t read_len = 0;

	if (!handle->read(buffer, buf_size, &read_len) || read_len == 0)
		return info;

	int header_len = 0;
	info.enc = detect_encoding(buffer, handle->size(), header_len);
	info.endings = detect_line_endings(buffer, read_len);

	int line_number = 0;

	if (info.enc == file_encoding::utf8)
	{
		std::string utf8_line;
		auto pos = static_cast<uint32_t>(header_len);
		bool skip_next_lf = false;

		for (;;)
		{
			for (; pos < read_len; pos++)
			{
				const auto c = buffer[pos];

				if (skip_next_lf && c == '\n')
				{
					skip_next_lf = false;
					continue;
				}
				skip_next_lf = false;

				if (c == '\r' || c == '\n')
				{
					on_line(str::utf8_to_utf16(utf8_line), line_number);
					utf8_line.clear();
					line_number++;

					if (c == '\r')
						skip_next_lf = true;
				}
				else
				{
					utf8_line += static_cast<char>(c);
				}
			}

			if (!handle->read(buffer, buf_size, &read_len) || read_len == 0)
				break;
			pos = 0;
		}

		on_line(str::utf8_to_utf16(utf8_line), line_number);
	}
	else if (info.enc == file_encoding::utf16 || info.enc == file_encoding::utf16be)
	{
		const bool is_be = (info.enc == file_encoding::utf16be);
		const auto* buffer16 = reinterpret_cast<const wchar_t*>(buffer);
		read_len /= 2;
		auto pos = static_cast<uint32_t>(header_len / 2);
		bool skip_next_lf = false;

		std::wstring line;

		for (;;)
		{
			for (; pos < read_len; pos++)
			{
				auto c = buffer16[pos];
				if (is_be) c = _byteswap_ushort(c);

				if (skip_next_lf && c == L'\n')
				{
					skip_next_lf = false;
					continue;
				}
				skip_next_lf = false;

				if (c == L'\r' || c == L'\n')
				{
					on_line(line, line_number);
					line.clear();
					line_number++;

					if (c == L'\r')
						skip_next_lf = true;
				}
				else
				{
					line += c;
				}
			}

			if (!handle->read(buffer, buf_size, &read_len) || read_len == 0)
				break;
			buffer16 = reinterpret_cast<const wchar_t*>(buffer);
			read_len /= 2;
			pos = 0;
		}

		on_line(line, line_number);
	}

	return info;
}

static std::wstring temp_file_path()
{
	return pf::platform_temp_file_path(L"rethinkify.");
}

static std::wstring last_error_message()
{
	return pf::platform_last_error_message();
}

bool document::save_to_file(const file_path& path, const line_endings nCrlfStyle /*= CRLF_STYLE_AUTOMATIC*/,
                            const bool bClearModifiedFlag /*= true*/) const
{
	auto success = false;
	const auto tempPath = temp_file_path();
	std::ofstream stream(tempPath, std::ios::binary);

	if (stream)
	{
		auto effective_style = nCrlfStyle;
		if (effective_style == line_endings::crlf_style_automatic)
			effective_style = _line_ending == line_endings::crlf_style_automatic
				                  ? line_endings::crlf_style_dos
				                  : _line_ending;

		const char* eol;
		switch (effective_style)
		{
		case line_endings::crlf_style_unix: eol = "\n";
			break;
		case line_endings::crlf_style_mac: eol = "\r";
			break;
		default: eol = "\r\n";
			break;
		}

		auto first = true;

		static uint8_t smarker[3] = {0xEF, 0xBB, 0xBF};
		stream.write(reinterpret_cast<const char*>(smarker), 3);

		for (const auto& line : _lines)
		{
			if (!first) stream << eol;
			stream << str::utf16_to_utf8(line._text);
			first = false;
		}

		stream.close();

		if (stream.fail())
			return false;

		success = pf::platform_move_file_replace(tempPath.c_str(), path.c_str());

		if (success && bClearModifiedFlag)
		{
			_modified = false;
			_saved_undo_pos = _undo_pos;
			_disk_modified_time = pf::file_modified_time(path);
		}

		if (!success)
		{
			pf::platform_show_error(last_error_message(), g_app_name);
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

text_location document::apply_undo_step(const undo_step& step)
{
	if (step.is_insert())
	{
		if (step.is_single_char())
			return delete_text(step.char_end_location());
		return delete_text(step._selection);
	}
	return insert_text(step._selection._start, step._text);
}

text_location document::apply_redo_step(const undo_step& step)
{
	if (step.is_insert())
		return insert_text(step._selection._start, step._text);
	if (step.is_single_char())
		return delete_text(step.char_end_location());
	return delete_text(step._selection);
}

text_location document::apply_undo(const undo_item& item)
{
	text_location location;
	for (auto i = item.rbegin(); i != item.rend(); ++i)
		location = apply_undo_step(*i);
	return location;
}

text_location document::apply_redo(const undo_item& item)
{
	text_location location;
	for (const auto& step : item)
		location = apply_redo_step(step);
	return location;
}

text_location document::undo()
{
	assert(can_undo());
	_undo_pos--;
	_modified = (_undo_pos != _saved_undo_pos);
	return apply_undo(_undo[_undo_pos]);
}

text_location document::redo()
{
	assert(can_redo());
	_modified = true;
	const auto result = apply_redo(_undo[_undo_pos]);
	_undo_pos++;
	_modified = (_undo_pos != _saved_undo_pos);
	return result;
}

void document::record_undo(undo_item ui)
{
	_undo.erase(_undo.begin() + _undo_pos, _undo.end());
	_undo.push_back(std::move(ui));
	_undo_pos = _undo.size();
}

text_selection document::replace_text(undo_group& ug, const text_selection& selection, const std::wstring_view text)
{
	text_selection result;
	result._start = delete_text(ug, selection);
	result._end = insert_text(ug, selection._start, text);
	return result;
}

text_location document::insert_text(const text_location& location, const std::wstring_view text)
{
	if (text.empty())
		return location;

	// Split input into lines
	std::vector<std::wstring_view> input_lines;
	size_t start = 0;
	for (size_t i = 0; i < text.size(); i++)
	{
		if (text[i] == L'\n')
		{
			auto end = i;
			if (end > start && text[end - 1] == L'\r')
				end--;
			input_lines.push_back(text.substr(start, end - start));
			start = i + 1;
		}
	}
	auto end = text.size();
	if (end > start && text[end - 1] == L'\r')
		end--;
	input_lines.push_back(text.substr(start, end - start));

	text_location result = location;

	if (input_lines.size() == 1)
	{
		// Single-line insert: splice into existing line
		auto& li = _lines[location.y];
		li._text.insert(location.x, input_lines[0]);
		result.x = location.x + static_cast<int>(input_lines[0].size());
		result.y = location.y;
		invalidate_line(location.y);
	}
	else
	{
		// Multi-line insert: split current line, bulk-insert new lines
		auto& first_line = _lines[location.y];
		const auto tail = first_line._text.substr(location.x);
		first_line._text.erase(location.x);
		first_line._text.append(input_lines[0]);

		// Build new intermediate and last lines
		const auto count = static_cast<int>(input_lines.size());
		std::vector<document_line> new_lines;
		new_lines.reserve(count - 1);

		for (int i = 1; i < count - 1; i++)
			new_lines.emplace_back(std::wstring(input_lines[i]));

		// Last input line gets the tail appended
		new_lines.emplace_back(std::wstring(input_lines[count - 1]) + tail);

		_lines.insert(_lines.begin() + location.y + 1,
		              std::make_move_iterator(new_lines.begin()),
		              std::make_move_iterator(new_lines.end()));

		result.y = location.y + count - 1;
		result.x = static_cast<int>(input_lines[count - 1].size());

		_events.invalidate(invalid::view);
	}

	_modified = true;
	return result;
}

text_location document::insert_text(undo_group& ug, const text_location& location, const std::wstring_view text)
{
	const auto result_location = insert_text(location, text);
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
		document_line newLine(li._text.substr(location.x));
		_lines.insert(_lines.begin() + location.y + 1, std::move(newLine));
		_lines[location.y]._text.erase(location.x);

		resultLocation.y = location.y + 1;
		resultLocation.x = 0;

		_events.invalidate(invalid::view);
	}
	else if (c != '\r')
	{
		li._text.insert(li._text.begin() + location.x, c);

		resultLocation.y = location.y;
		resultLocation.x = location.x + 1;

		_events.invalidate(invalid::view);
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
			ug.erase(text_location(static_cast<int>(_lines[location.y - 1].size()), location.y - 1), L'\n');
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
			li._text.erase(li._text.begin() + selection._start.x, li._text.begin() + selection._end.x);

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

			_events.invalidate(invalid::view);
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

			resultPos.x = static_cast<int>(previous.size());
			resultPos.y = location.y - 1;

			previous._text.insert(previous._text.end(), line._text.begin(), line._text.end());
			_lines.erase(_lines.begin() + location.y);

			_events.invalidate(invalid::view);
		}
	}
	else
	{
		auto& li = _lines[location.y];
		li._text.erase(li._text.begin() + location.x - 1, li._text.begin() + location.x);

		resultPos.x = location.x - 1;
		resultPos.y = location.y;

		invalidate_line(location.y);
	}

	return resultPos;
}

bool document::is_json() const
{
	for (const auto& line : _lines)
	{
		for (const auto& c : line._text)
		{
			if (c == '{') return true;
			if (c != ' ' && c != '\n' && c != '\t' && c != '\r') return false;
		}
	}
	return false;
}

void document::reformat_json()
{
	if (!is_json())
		return;

	std::wostringstream result;
	int tabs = 0, tokens = -1;

	for (const auto& line : _lines)
	{
		for (const auto& ch : line._text)
		{
			if (ch == '{')
			{
				tokens++;
				tabs = tokens;
				if (tokens > 0) result << "\n";
				while (tabs)
				{
					result << "\t";
					tabs--;
				}
				result << ch << "\n";
				tabs = tokens + 1;
				while (tabs)
				{
					result << "\t";
					tabs--;
				}
			}
			else if (ch == ':')
			{
				result << " : ";
			}
			else if (ch == ',')
			{
				result << ",\n";
				tabs = tokens + 1;
				while (tabs)
				{
					result << "\t";
					tabs--;
				}
			}
			else if (ch == '}')
			{
				tabs = tokens;
				result << "\n";
				while (tabs)
				{
					result << "\t";
					tabs--;
				}
				result << ch << "\n";
				tokens--;
				tabs = tokens + 1;
				while (tabs)
				{
					result << "\t";
					tabs--;
				}
			}
			else
			{
				if (ch == '\n' || ch == '\t') continue;
				result << ch;
			}
		}
	}

	undo_group ug(shared_from_this());
	select(replace_text(ug, all(), result.str()));
}

void document::sort_remove_duplicates()
{
	auto lines = _lines;

	std::ranges::sort(lines);
	const auto [first, last] = std::ranges::unique(lines);
	lines.erase(first, last);

	undo_group ug(shared_from_this());
	select(replace_text(ug, all(), combine_line_text(lines)));
}

void document::apply_loaded_data(const file_path& path, loaded_file_data data)
{
	if (data.disk_modified_time != 0)
	{
		std::swap(_lines, data.lines);

		if (_lines.empty())
			append_line(L"");

		auto ext = path.extension();
		if (!ext.empty() && ext.starts_with(L'.')) ext = ext.substr(1);

		_read_only = data.encoding == file_encoding::binary;
		_spell_check = is_spell_check_extension(ext);
		_line_ending = data.endings;
		_modified = false;
		_undo_pos = 0;
		_saved_undo_pos = 0;
		_path = path;
		_disk_modified_time = data.disk_modified_time;

		reset();
		_events.invalidate(invalid::view | invalid::title);
	}
}

bool is_binary_extension(const file_path& path)
{
	// is binary 
	static const std::unordered_set<std::wstring_view, ihash, ieq> binary_extensions = {
		L".exe", L".dll", L".obj", L".lib", L".pdb", L".ilk", L".pch",
		L".png", L".jpg", L".jpeg", L".gif", L".bmp", L".ico",
		L".zip", L".7z", L".rar", L".tar", L".gz",
		L".pdf", L".doc", L".docx", L".xls", L".xlsx",
		L".mp3", L".mp4", L".avi", L".mov", L".wav",
		L".ttf", L".otf", L".woff", L".woff2",
		L".bin", L".dat", L".db", L".sqlite",
		L".res", L".recipe",
	};
	return binary_extensions.contains(path.extension());
}

bool is_binary_data(const std::span<const uint8_t> buf)
{
	const auto readLen = buf.size();

	if (readLen == 0)
		return false;

	// Check for BOM signatures indicating a text encoding
	if (readLen >= 2)
	{
		if (buf[0] == 0xFF && buf[1] == 0xFE) return false; // UTF-16 LE (or UTF-32 LE)
		if (buf[0] == 0xFE && buf[1] == 0xFF) return false; // UTF-16 BE
	}
	if (readLen >= 3 && buf[0] == 0xEF && buf[1] == 0xBB && buf[2] == 0xBF)
		return false; // UTF-8 BOM

	// Check for UTF-16 without BOM (alternating null byte pattern)
	if (readLen >= 4)
	{
		if (buf[0] != 0 && buf[1] == 0 && buf[2] != 0 && buf[3] == 0) return false; // UTF-16 LE
		if (buf[0] == 0 && buf[1] != 0 && buf[2] == 0 && buf[3] != 0) return false; // UTF-16 BE
	}

	for (uint32_t i = 0; i < readLen; i++)
	{
		const auto c = buf[i];
		if (c == 0)
			return true;
		if (c < 8 && c != 7)
			return true;
	}

	return false;
}

bool is_binary_file(const file_path& path)
{
	if (is_binary_extension(path))
		return true;

	const auto hFile = pf::open_for_read(path);
	if (hFile == nullptr)
		return false;

	uint8_t buf[8192];
	uint32_t readLen = 0;
	hFile->read(buf, sizeof(buf), &readLen);

	return is_binary_data({buf, readLen});
}
