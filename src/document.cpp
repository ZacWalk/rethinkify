#include "pch.h"
#include "document.h"


const TCHAR crlf [] = _T("\r\n");

static auto s_textHighlighter = std::make_shared<TextHighight>();
static auto s_cppHighlighter = std::make_shared<CppSyntax>();

document::document(IView& view, const std::wstring& text, line_endings nCrlfStyle) : _view(view), _highlight(s_textHighlighter)
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

document::~document() {}

uint32_t document::highlight_cookie(int lineIndex) const
{
	auto line_count = _lines.size();

	if (lineIndex < 0 || lineIndex >= line_count)
		return 0;

	auto i = lineIndex;
	while (i >= 0 && _lines[i]._parse_cookie == invalid)
		i--;
	i++;

	int nBlocks;
	while (i <= lineIndex && _lines[i]._parse_cookie == invalid)
	{
		auto dwCookie = 0;

		if (i > 0)
			dwCookie = _lines[i - 1]._parse_cookie;

		const auto& line = _lines[i];
		line._parse_cookie = _highlight->parse_line(dwCookie, line, nullptr, nBlocks);

		assert(line._parse_cookie != invalid);
		i++;
	}

	return _lines[lineIndex]._parse_cookie;
}

uint32_t document::highlight_line(uint32_t cookie, const document_line& line, IHighlight::text_block* pBuf, int& nBlocks) const
{
	return _highlight->parse_line(cookie, line, pBuf, nBlocks);
}

bool document::is_inside_selection(const text_location& ptTextPos) const
{
	auto sel = _selection.normalize();

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

	for (auto& line : _lines)
	{
		line._parse_cookie = invalid;
		line._expanded_length = invalid;
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

		for (auto& line : _lines)
		{
			line._expanded_length = invalid;
		}

		_max_line_len = -1;
		_view.invalidate_view();
	}
}

int document::max_line_length() const
{
	if (_max_line_len == -1)
	{
		_max_line_len = 0;
		auto line_count = _lines.size();

		for (int i = 0; i < line_count; i++)
		{
			update_max_line_length(i);
		}
	}

	return _max_line_len;
}

void document::update_max_line_length(int i) const
{
	int len = expanded_line_length(i);

	if (_max_line_len < len)
		_max_line_len = len;
}

int document::expanded_line_length(int lineIndex) const
{
	const auto& line = _lines[lineIndex];

	if (line._expanded_length == -1)
	{
		auto nActualLength = 0;

		if (!line.empty())
		{
			auto nLength = line.size();
			auto pszCurrent = line.c_str();
			auto tabSize = tab_size();

			for (;;)
			{
				auto psz = wcschr(pszCurrent, L'\t');

				if (psz == nullptr)
				{
					nActualLength += (line.c_str() + nLength - pszCurrent);
					break;
				}

				nActualLength += (psz - pszCurrent);
				nActualLength += (tabSize - nActualLength % tabSize);
				pszCurrent = psz + 1;
			}
		}

		line._expanded_length = nActualLength;
	}

	return line._expanded_length;
}

std::wstring document::expanded_chars(const std::wstring& text, int nOffset, int nCount) const
{
	std::wstring result;

	if (nCount > 0)
	{
		auto pszChars = text.c_str();
		auto tabSize = tab_size();
		auto nActualOffset = 0;

		for (auto i = 0; i < nOffset; i++)
		{
			if (pszChars[i] == _T('\t'))
			{
				nActualOffset += (tabSize - nActualOffset % tabSize);
			}
			else
			{
				nActualOffset++;
			}
		}

		pszChars += nOffset;
		auto nLength = nCount;

		auto nTabCount = 0;
		for (auto i = 0; i < nLength; i++)
		{
			if (pszChars[i] == _T('\t'))
				nTabCount++;
		}

		auto nCurPos = 0;

		if (nTabCount > 0 || _view_tabs)
		{
			for (auto i = 0; i < nLength; i++)
			{
				if (pszChars[i] == _T('\t'))
				{
					auto nSpaces = tabSize - (nActualOffset + nCurPos) % tabSize;

					if (_view_tabs)
					{
						result += TAB_CHARACTER;
						nCurPos++;
						nSpaces--;
					}
					while (nSpaces > 0)
					{
						result += _T(' ');
						nCurPos++;
						nSpaces--;
					}
				}
				else
				{
					result += (pszChars[i] == _T(' ') && _view_tabs) ? SPACE_CHARACTER : pszChars[i];
					nCurPos++;
				}
			}
		}
		else
		{
			result.append(pszChars, nLength);
			nCurPos = nLength;
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
	int tabSize = tab_size();

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
		_view.update_caret();
	}
}

bool document::HighlightText(const text_location& ptStartPos, int nLength)
{
	_cursor_loc = ptStartPos;
	_cursor_loc.x += nLength;
	_anchor_loc = _cursor_loc;
	select(text_selection(ptStartPos, _cursor_loc));
	_view.ensure_visible(_cursor_loc);
	return true;
}


static int find_in_line(const wchar_t* pszFindWhere, const wchar_t* pszFindWhat, bool bWholeWord)
{
	assert(pszFindWhere != nullptr);
	assert(pszFindWhat != nullptr);

	auto nCur = 0;
	auto nLength = wcslen(pszFindWhat);

	for (;;)
	{
		auto pszPos = wcsistr(pszFindWhere, pszFindWhat);

		if (pszPos == nullptr)
		{
			return -1;
		}

		if (!bWholeWord)
		{
			return nCur + (pszPos - pszFindWhere);
		}

		if (pszPos > pszFindWhere && (iswalnum(pszPos[-1]) || pszPos[-1] == _T('_')))
		{
			nCur += (pszPos - pszFindWhere);
			pszFindWhere = pszPos + 1;
			continue;
		}

		if (iswalnum(pszPos[nLength]) || pszPos[nLength] == _T('_'))
		{
			nCur += (pszPos - pszFindWhere + 1);
			pszFindWhere = pszPos + 1;
			continue;
		}

		return nCur + (pszPos - pszFindWhere);
	}
}


void document::find(const std::wstring& text, uint32_t flags)
{
	text_location loc;

	if (find(text, _cursor_loc, all(), flags, true, &loc))
	{
		_find_text = text;
		_find_flags = flags;

		auto end = loc;
		end.x += text.size();
		select(text_selection(loc, end));
	}
	//	CWinApp *pApp = AfxGetApp();
	//	assert(pApp != nullptr);
	//
	//	CFindTextDlg dlg(this);
	//	if (m_bLastSearch)
	//	{
	//		//	Get the latest search parameters
	//		dlg.m_bMatchCase = (_find_flags & FIND_MATCH_CASE) != 0;
	//		dlg.m_bWholeWord = (_find_flags & FIND_WHOLE_WORD) != 0;
	//		dlg.m_nDirection = (_find_flags & FIND_DIRECTION_UP) != 0 ? 0 : 1;
	//		if (_find_text != nullptr)
	//			dlg.m_sText = _find_text;
	//	}
	//	else
	//	{
	//		//	Take search parameters from registry
	//		dlg.m_bMatchCase = pApp->GetProfileInt(REG_FIND_SUBKEY, REG_MATCH_CASE, false);
	//		dlg.m_bWholeWord = pApp->GetProfileInt(REG_FIND_SUBKEY, REG_WHOLE_WORD, false);
	//		dlg.m_nDirection = 1;		//	Search down
	//		dlg.m_sText = pApp->GetProfileString(REG_FIND_SUBKEY, REG_FIND_WHAT, _T(""));
	//	}
	//
	//	//	Take the current selection, if any
	//	if (has_selection())
	//	{
	//		text_location ptSelStart, ptSelEnd;
	//		selection(ptSelStart, ptSelEnd);		if (ptSelStart.y == ptSelEnd.y)
	//		{
	//			const wchar_t * pszChars = GetLineChars(ptSelStart.y);
	//			int nChars = ptSelEnd.x - ptSelStart.x;
	//			lstrcpyn(dlg.m_sText.GetBuffer(nChars + 1), pszChars + ptSelStart.x, nChars + 1);
	//			dlg.m_sText.ReleaseBuffer();
	//		}
	//	}
	//
	//	//	Execute Find dialog
	//	dlg.m_ptCurrentPos = m_ptCursorPos;		//	Search from cursor position
	//	m_bShowInactiveSelection = true;
	//	dlg.DoModal();
	//	m_bShowInactiveSelection = false;
	//
	//	//	Save search parameters for 'F3' command
	//	m_bLastSearch = true;
	//	if (_find_text != nullptr)
	//		free(_find_text);
	//	_find_text = _wcsdup(dlg.m_sText);
	//
	//	_find_flags = 0;
	//	if (dlg.m_bMatchCase)
	//		_find_flags |= FIND_MATCH_CASE;
	//	if (dlg.m_bWholeWord)
	//		_find_flags |= FIND_WHOLE_WORD;
	//	if (dlg.m_nDirection == 0)
	//		_find_flags |= FIND_DIRECTION_UP;
	//
	//	//	Save search parameters to registry
	//	pApp->WriteProfileInt(REG_FIND_SUBKEY, REG_MATCH_CASE, dlg.m_bMatchCase);
	//	pApp->WriteProfileInt(REG_FIND_SUBKEY, REG_WHOLE_WORD, dlg.m_bWholeWord);
	//	pApp->WriteProfileString(REG_FIND_SUBKEY, REG_FIND_WHAT, dlg.m_sText);
}

void document::find_next()
{
	//if (m_bLastSearch)
	//{
	//	text_location ptFoundPos;
	//	if (! FindText(_find_text, m_ptCursorPos, _find_flags, true, &ptFoundPos))
	//	{
	//		std::wstring prompt;
	//		prompt.Format(IDS_EDIT_TEXT_NOT_FOUND, _find_text);
	//		AfxMessageBox(prompt);
	//		return;
	//	}
	//	HighlightText(ptFoundPos, lstrlen(_find_text));
	//	m_bMultipleSearch = true;       // More search       
	//}
}

void document::find_previous()
{
	auto dwSaveSearchFlags = _find_flags;

	if ((_find_flags & FIND_DIRECTION_UP) != 0)
		_find_flags &= ~FIND_DIRECTION_UP;
	else
		_find_flags |= FIND_DIRECTION_UP;
	find_next();
	_find_flags = dwSaveSearchFlags;
}

bool document::find(const std::wstring& what, const text_location& ptStartPos, const text_selection& selection, uint32_t dwFlags, bool bWrapSearch, text_location* pptFoundPos)
{
	auto ptCurrentPos = ptStartPos;

	if (selection.empty())
		return false;

	if (ptCurrentPos < selection._start)
	{
		ptCurrentPos = selection._start;
	}

	auto matchCase = (dwFlags & FIND_MATCH_CASE) != 0;
	auto wholeWord = (dwFlags & FIND_WHOLE_WORD) != 0;

	if (dwFlags & FIND_DIRECTION_UP)
	{
		//	Let's check if we deal with whole text.
		//	At this point, we cannot search *up* in selection
		//	Proceed as if we have whole text search.

		for (;;)
		{
			if (ptCurrentPos.x == 0)
			{
				ptCurrentPos.y--;
			}

			while (ptCurrentPos.y >= 0)
			{
				const auto& line = _lines[ptCurrentPos.y];
				const auto nLineLength = line._text.size() - ptCurrentPos.x;

				if (nLineLength > 0)
				{
					auto nPos = ::find_in_line(line._text.c_str() + ptCurrentPos.x, what.c_str(), wholeWord);

					if (nPos >= 0) //	Found text!
					{
						ptCurrentPos.x += nPos;
						*pptFoundPos = ptCurrentPos;
						return true;
					}
				}

				ptCurrentPos.x = 0;
				ptCurrentPos.y--;
			}

			//	Beginning of text reached
			if (!bWrapSearch)
				return false;

			//	Start again from the end of text
			bWrapSearch = false;
			ptCurrentPos = text_location(0, _lines.size() - 1);
		}
	}
	else
	{
		for (;;)
		{
			while (ptCurrentPos.y <= selection._end.y)
			{
				const auto& line = _lines[ptCurrentPos.y];
				const auto nLineLength = line._text.size() - ptCurrentPos.x;

				if (nLineLength > 0)
				{
					auto nPos = ::find_in_line(line._text.c_str() + ptCurrentPos.x, what.c_str(), wholeWord);

					if (nPos >= 0)
					{
						ptCurrentPos.x += nPos;
						//	Check of the text found is outside the block.
						if (selection._end < ptCurrentPos)
							break;

						*pptFoundPos = ptCurrentPos;
						return true;
					}
				}

				//	Go further, text was not found
				ptCurrentPos.x = 0;
				ptCurrentPos.y++;
			}

			//	End of text reached
			if (!bWrapSearch)
				return false;

			//	Start from the beginning
			bWrapSearch = false;
			ptCurrentPos = selection._start;
		}
	}
}

bool document::view_tabs() const
{
	return _view_tabs;
}

void document::view_tabs(bool bViewTabs)
{
	if (bViewTabs != _view_tabs)
	{
		_view_tabs = bViewTabs;
		_view.invalidate_view();
	}
}


void document::MoveLeft(bool selecting)
{
	auto sel = _selection.normalize();

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

void document::MoveRight(bool selecting)
{
	auto sel = _selection.normalize();

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

void document::MoveWordLeft(bool selecting)
{
	auto sel = _selection.normalize();

	if (!sel.empty() && !selecting)
	{
		MoveLeft(selecting);
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

	WordToLeft(_cursor_loc);

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

void document::MoveWordRight(bool selecting)
{
	auto sel = _selection.normalize();

	if (!sel.empty() && !selecting)
	{
		MoveRight(selecting);
		return;
	}

	if (_cursor_loc.x == _lines[_cursor_loc.y].size())
	{
		if (_cursor_loc.y == _lines.size() - 1)
			return;
		_cursor_loc.y++;
		_cursor_loc.x = 0;
	}

	auto nLength = _lines[_cursor_loc.y].size();

	if (_cursor_loc.x == nLength)
	{
		MoveRight(selecting);
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

void document::MoveUp(bool selecting)
{
	auto sel = _selection.normalize();

	if (!sel.empty() && !selecting)
		_cursor_loc = sel._start;

	if (_cursor_loc.y > 0)
	{
		if (_ideal_char_pos == -1)
			_ideal_char_pos = calc_offset(_cursor_loc.y, _cursor_loc.x);
		_cursor_loc.y--;
		_cursor_loc.x = calc_offset_approx(_cursor_loc.y, _ideal_char_pos);

		auto size = _lines[_cursor_loc.y].size();

		if (_cursor_loc.x > size)
			_cursor_loc.x = size;
	}
	_view.ensure_visible(_cursor_loc);
	if (!selecting)
		_anchor_loc = _cursor_loc;

	select(text_selection(_anchor_loc, _cursor_loc));
}

void document::MoveDown(bool selecting)
{
	auto sel = _selection.normalize();

	if (!sel.empty() && !selecting)
		_cursor_loc = sel._end;

	if (_cursor_loc.y < _lines.size() - 1)
	{
		if (_ideal_char_pos == -1)
			_ideal_char_pos = calc_offset(_cursor_loc.y, _cursor_loc.x);

		_cursor_loc.y++;
		_cursor_loc.x = calc_offset_approx(_cursor_loc.y, _ideal_char_pos);

		auto size = _lines[_cursor_loc.y].size();

		if (_cursor_loc.x > size)
			_cursor_loc.x = size;
	}
	_view.ensure_visible(_cursor_loc);
	if (!selecting)
		_anchor_loc = _cursor_loc;
	select(text_selection(_anchor_loc, _cursor_loc));
}

void document::MoveHome(bool selecting)
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

void document::MoveEnd(bool selecting)
{
	_cursor_loc.x = _lines[_cursor_loc.y].size();
	_ideal_char_pos = calc_offset(_cursor_loc.y, _cursor_loc.x);
	_view.ensure_visible(_cursor_loc);
	if (!selecting)
		_anchor_loc = _cursor_loc;
	select(text_selection(_anchor_loc, _cursor_loc));
}

void document::MoveCtrlHome(bool selecting)
{
	_cursor_loc.x = 0;
	_cursor_loc.y = 0;
	_ideal_char_pos = calc_offset(_cursor_loc.y, _cursor_loc.x);
	_view.ensure_visible(_cursor_loc);
	if (!selecting)
		_anchor_loc = _cursor_loc;
	select(text_selection(_anchor_loc, _cursor_loc));
}

void document::MoveCtrlEnd(bool selecting)
{
	_cursor_loc.y = _lines.size() - 1;
	_cursor_loc.x = _lines[_cursor_loc.y].size();
	_ideal_char_pos = calc_offset(_cursor_loc.y, _cursor_loc.x);
	_view.ensure_visible(_cursor_loc);
	if (!selecting)
		_anchor_loc = _cursor_loc;
	select(text_selection(_anchor_loc, _cursor_loc));
}

text_location document::WordToRight(text_location pt) const
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

text_location document::WordToLeft(text_location pt) const
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

void document::Copy()
{
	if (_selection._start == _selection._end)
		return;

	auto sel = _selection.normalize();
	_view.text_to_clipboard(Combine(text(sel), L"\r\n"));
}

bool document::CanPaste()
{
	return IsClipboardFormatAvailable(CF_UNICODETEXT) != 0;
}

bool document::QueryEditable()
{
	return true;
}

void document::Paste()
{
	if (QueryEditable())
	{
		auto text = _view.text_from_clipboard();

		if (!text.empty())
		{
			undo_group ug(*this);
			auto pos = delete_text(ug, selection());
			select(insert_text(ug, pos, text));
		}
	}
}

void document::Cut()
{
	if (QueryEditable() && has_selection())
	{
		auto sel = selection();
		_view.text_to_clipboard(Combine(text(sel)));

		undo_group ug(*this);
		select(delete_text(ug, sel));
	}
}

void document::OnEditDelete()
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

void document::OnEditDeleteBack()
{
	if (QueryEditable())
	{
		if (has_selection())
		{
			OnEditDelete();
		}
		else
		{
			undo_group ug(*this);
			select(delete_text(ug, cursor_pos()));
		}
	}
}

void document::OnEditTab()
{
	if (QueryEditable())
	{
		auto sel = selection();

		if (sel._end.y > sel._start.y)
		{
			undo_group ug(*this);

			int nStartLine = sel._start.y;
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

			static const TCHAR pszText [] = _T("\t");

			for (int i = nStartLine; i <= nEndLine; i++)
			{
				insert_text(ug, text_location(0, i), pszText);
			}

			_view.recalc_horz_scrollbar();
		}
		else
		{
			undo_group ug(*this);
			auto pos = delete_text(ug, selection());
			select(insert_text(ug, pos, L'\t'));
		}
	}
}

void document::OnEditUntab()
{
	if (QueryEditable())
	{
		auto sel = selection();

		if (sel._end.y > sel._start.y)
		{
			undo_group ug(*this);

			int nStartLine = sel._start.y;
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

			_view.recalc_horz_scrollbar();
		}
		else
		{
			auto ptCursorPos = cursor_pos();

			if (ptCursorPos.x > 0)
			{
				int tabSize = tab_size();
				int nOffset = calc_offset(ptCursorPos.y, ptCursorPos.x);
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

void document::OnEditUndo()
{
	if (can_undo())
	{
		select(undo());
	}
}

void document::OnEditRedo()
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

void document::SetAutoIndent(bool bAutoIndent)
{
	_auto_indent = bAutoIndent;
}

static bool IsCppExtension(const wchar_t* ext)
{
	static auto comp = [](const wchar_t* l, const wchar_t* r)
		{
			return _wcsicmp(l, r) < 0;
		};
	static std::set<const wchar_t*, std::function<bool(const wchar_t*, const wchar_t*)>> extensions(comp);

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

	return extensions.find(ext) != extensions.end();
};

void document::HighlightFromExtension(const wchar_t* ext)
{
	if (*ext == L'.') ext++;

	if (IsCppExtension(ext))
	{
		_highlight = s_cppHighlighter;
	}
	else
	{
		_highlight = s_textHighlighter;
	}
}

void document::append_line(const std::wstring& text)
{
	_lines.emplace_back(document_line(text));
}

void document::clear()
{
	_lines.clear();
	_undo.clear();

	reset();
}

static const char* crlfs [] =
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
	auto sb = is.rdbuf();

	for (;;)
	{
		auto c = sb->sbumpc();
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
} ;

struct _BOM_LOOKUP
{
	uint32_t bom;
	ULONG len;
	Encoding type;
};

struct _BOM_LOOKUP BOMLOOK [] =
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
			return line_endings::CRLF_STYLE_DOS;
		}
		
		return (i < len - 1 && buffer[i + 1] == 0x0d) ? line_endings::CRLF_STYLE_UNIX : line_endings::CRLF_STYLE_MAC;
	}

	return line_endings::CRLF_STYLE_DOS; // guess
}

bool document::load_from_file(const std::wstring& path)
{
	clear();

	auto success = false;
	//std::wifstream f(path);

	//size_t bom = 0;
	//bom = f.get() + (bom << 8);
	//bom = f.get() + (bom << 8);
	//bom = f.get() + (bom << 8);

	////f.read((char*) &bom, 3);
	////bom &= 0xFFFFFF;

	//if (bom == 0xEFBBBF) //UTF8
	//{
	//	f.imbue(std::locale(f.getloc(), new std::codecvt_utf8<wchar_t, 1114111UL>));
	//}
	//else
	//{	
	//	bom &= 0xFFFF;

	//	if (bom == 0xFEFF) //UTF16LE
	//	{
	//		f.imbue(std::locale(f.getloc(), new std::codecvt_utf16<wchar_t, 1114111UL, std::little_endian>));
	//		f.seekg(2, std::ios::beg);
	//	}
	//	else if (bom == 0xFFFE) //UTF16BE
	//	{
	//		f.imbue(std::locale(f.getloc(), new std::codecvt_utf16<wchar_t, 1114111UL>));
	//		f.seekg(2, std::ios::beg);
	//	}
	//	else //ANSI
	//	{
	//		bom = 0;
	//		//f.imbue(std::locale(f.getloc()));
	//		f.seekg(std::ios::beg);
	//	}
	//}

	//std::u16string line;

	//if (f)
	//{
	//	while (f.good())
	//	{
	//		std::getline(f, line);
	//		append_line(line);
	//	}

	//	success = true;
	//}


	auto hFile = ::CreateFile(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);

	if (hFile != INVALID_HANDLE_VALUE)
	{
		const auto bufferLen = 1024 * 64;
		uint8_t buffer[bufferLen];

		DWORD readLen;
		if (::ReadFile(hFile, buffer, bufferLen, &readLen, nullptr))
		{
			auto headerLen = 0;
			auto size = GetFileSize(hFile, nullptr);
			auto encoding = detect_encoding(buffer, size, headerLen);

			_line_ending = detect_line_endings(buffer, readLen);
			auto crlf = crlfs[static_cast<int>(_line_ending)];

			auto bufferPos = headerLen;
			auto last_char = 0;

			if (encoding == Encoding::UTF8 || encoding == Encoding::ASCII)
			{
				std::string line;

				while (readLen > 0)
				{
					int c = buffer[bufferPos];

					if ((last_char == 0x0A && c == 0x0D) || (last_char == 0x0A && c != 0x0D))
					{
						append_line((encoding == Encoding::ASCII) ? AsciiToUtf16(line) : UTF8ToUtf16(line));
						line.clear();
					}

					if (c != 0x0A && c != 0x0D)
					{
						line += static_cast<char>(c);
					}

					bufferPos++;

					if (bufferPos == readLen)
					{
						if (::ReadFile(hFile, buffer, bufferLen, &readLen, nullptr))
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

				append_line((encoding == Encoding::ASCII) ? AsciiToUtf16(line) : UTF8ToUtf16(line));
			}
			else if (encoding == Encoding::UTF16BE || encoding == Encoding::UTF16)
			{
				auto buffer16 = reinterpret_cast<const wchar_t *>(buffer);
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
						if (::ReadFile(hFile, buffer, bufferLen, &readLen, nullptr))
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

			HighlightFromExtension(PathFindExtension(path.c_str()));

			_path = path;
			_view.invalidate_view();
		}

		if (hFile != nullptr)
			::CloseHandle(hFile);
	}

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
	auto error = GetLastError();

	if (error)
	{
		LPVOID lpMsgBuf;
		auto bufLen = FormatMessageW(
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
			result = static_cast<LPCWSTR>(lpMsgBuf);
			LocalFree(lpMsgBuf);
		}
	}
	return result;
}

bool document::save_to_file(const std::wstring& path, line_endings nCrlfStyle /*= CRLF_STYLE_AUTOMATIC*/, bool bClearModifiedFlag /*= true*/) const
{
	auto success = false;
	auto tempPath = temp_file_path();
	std::ofstream stream(tempPath);

	if (stream)
	{
		auto first = true;

		static char smarker[3] = { 0xEF, 0xBB, 0xBF };
		stream.write(smarker, 3);

		for (const auto& line : _lines)
		{
			if (!first) stream << std::endl;
			stream << UTF16ToUtf8(line._text);
			first = false;
		}

		stream.close();

		success = ::MoveFileEx(tempPath.c_str(), path.c_str(), MOVEFILE_COPY_ALLOWED | MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;

		if (success && bClearModifiedFlag)
		{
			_modified = false;
		}

		if (!success)
		{
			MessageBox(GetActiveWindow(), last_error_message().c_str(), L"Diffractor", MB_OK);
		}
	}

	//assert(nCrlfStyle == CRLF_STYLE_AUTOMATIC || nCrlfStyle == CRLF_STYLE_DOS ||
	//	nCrlfStyle == CRLF_STYLE_UNIX || nCrlfStyle == CRLF_STYLE_MAC);

	//TCHAR szTempFileDir[_MAX_PATH + 1];
	//TCHAR szTempFileName[_MAX_PATH + 1];
	//TCHAR szBackupFileName[_MAX_PATH + 1];
	//auto success = false;

	//TCHAR drive[_MAX_PATH], dir[_MAX_PATH], name[_MAX_PATH], ext[_MAX_PATH];
	//_wsplitpath_s(pszFileName, drive, dir, name, ext);

	//lstrcpy(szTempFileDir, drive);
	//lstrcat(szTempFileDir, dir);
	//lstrcpy(szBackupFileName, pszFileName);
	//lstrcat(szBackupFileName, _T(".bak"));

	//if (::GetTempFileName(szTempFileDir, _T("CRE"), 0, szTempFileName) != 0)
	//{
	//	auto hTempFile = ::CreateFile(szTempFileName, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

	//	if (hTempFile != INVALID_HANDLE_VALUE)
	//	{
	//		if (nCrlfStyle == CRLF_STYLE_AUTOMATIC)
	//			nCrlfStyle = m_nCRLFMode;

	//		assert(nCrlfStyle >= 0 && nCrlfStyle <= 2);

	//		auto pszCRLF = crlfs[nCrlfStyle];
	//		auto nCRLFLength = strlen(pszCRLF);
	//		auto first = true;

	//		for (const auto &line : _lines)
	//		{
	//			auto len = line._text.size();
	//			uint32_t dwWrittenBytes;

	//			if (!first)
	//			{
	//				::WriteFile(hTempFile, pszCRLF, nCRLFLength, &dwWrittenBytes, nullptr);
	//			}
	//			else
	//			{
	//				first = false;
	//			}

	//			if (!line._text.empty())
	//			{
	//				auto utf8 = ToUtf8(line._text);
	//				::WriteFile(hTempFile, utf8.c_str(), utf8.size(), &dwWrittenBytes, nullptr);
	//			}
	//		}

	//		::CloseHandle(hTempFile);
	//		hTempFile = INVALID_HANDLE_VALUE;

	//		if (m_bCreateBackupFile)
	//		{
	//			WIN32_FIND_DATA wfd;
	//			auto hSearch = ::FindFirstFile(pszFileName, &wfd);
	//			if (hSearch != INVALID_HANDLE_VALUE)
	//			{
	//				//	File exist - create backup file
	//				::DeleteFile(szBackupFileName);
	//				::MoveFile(pszFileName, szBackupFileName);
	//				::FindClose(hSearch);
	//			}
	//		}
	//		else
	//		{
	//			::DeleteFile(pszFileName);
	//		}

	//		//	Move temporary file to target name
	//		success = ::MoveFile(szTempFileName, pszFileName) != 0;

	//		if (bClearModifiedFlag)
	//		{
	//			_modified = false;
	//		}
	//	}
	//}

	return success;
}


std::vector<std::wstring> document::text(const text_selection& selection) const
{
	std::vector<std::wstring> result;

	if (!selection.empty())
	{
		if (selection._start.y == selection._end.y)
		{
			result.emplace_back(_lines[selection._start.y]._text.substr(selection._start.x, selection._end.x - selection._start.x));
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

std::vector<std::wstring> document::text() const
{
	std::vector<std::wstring> result;

	for (const auto& line : _lines)
	{
		result.emplace_back(line._text);
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
	auto result = _undo[_undo_pos].redo(*this);
	_undo_pos++;
	return result;
}

void document::record_undo(const undo_item& ui)
{
	_undo.erase(_undo.begin() + _undo_pos, _undo.end());
	_undo.emplace_back(ui);
	_undo_pos = _undo.size();
}

text_selection document::replace_text(undo_group& ug, const text_selection& selection, const std::wstring& text)
{
	text_selection result;
	result._start = delete_text(ug, selection);
	result._end = insert_text(ug, selection._start, text);
	return result;
}

text_location document::insert_text(const text_location& location, const std::wstring& text)
{
	text_location resultLocation = location;

	for (const auto& c : text)
	{
		resultLocation = insert_text(resultLocation, c);
	}

	_modified = true;

	return resultLocation;
}

text_location document::insert_text(undo_group& ug, const text_location& location, const std::wstring& text)
{
	text_location resultLocation = location;

	for (const auto& c : text)
	{
		resultLocation = insert_text(resultLocation, c);
	}

	ug.insert(text_selection(location, resultLocation), text);
	_modified = true;

	return resultLocation;
}

text_location document::insert_text(const text_location& location, const wchar_t& c)
{
	text_location resultLocation = location;
	auto& li = _lines[location.y];

	if (c == L'\n')
	{
		// Split
		document_line newLine(li._text.substr(location.x, li._text.size()));
		_lines.insert(_lines.begin() + location.y + 1, newLine);
		_lines[location.y]._text = _lines[location.y]._text.substr(0, location.x);

		resultLocation.y = location.y + 1;
		resultLocation.x = 0;

		_view.invalidate_view();
	}
	else if (c != '\r')
	{
		auto before = li._text;
		auto after = li._text;

		after.insert(after.begin() + location.x, c);

		li._text = after;

		resultLocation.y = location.y;
		resultLocation.x = location.x + 1;

		_view.invalidate_line(location.y);
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
			ug.erase(text_location(_lines[location.y - 1].size(), location.y - 1), location.x > 0 ? _lines[location.y][location.x - 1] : '\n');
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

			_view.invalidate_line(selection._start.y);
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

			_view.invalidate_view();
		}
	}

	

	return selection._start;
}

text_location document::delete_text(undo_group& ug, const text_selection& selection)
{
	ug.erase(selection, Combine(text(selection)));
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

			_view.invalidate_view();
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

		_view.invalidate_line(location.y);
	}

	return resultPos;
}
