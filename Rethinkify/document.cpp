
#include "pch.h"
#include "document.h"
#include "utf.h"
#include "resource.h"
#include "ui.h"

#include <fstream>
#include <clocale>
#include <codecvt>

const TCHAR crlf [] = _T("\r\n");
const int UNDO_BUF_SIZE = 1000;

const auto REG_FIND_SUBKEY = L"Rethinkify\\Find";
const auto REG_REPLACE_SUBKEY = L"Rethinkify\\Replace";
const auto REG_MATCH_CASE = L"MatchCase";
const auto REG_WHOLE_WORD = L"WholeWord";
const auto REG_FIND_WHAT = L"FindWhat";
const auto REG_REPLACE_WITH = L"ReplaceWith";

const auto REG_PAGE_SUBKEY = L"Rethinkify\\PageSetup";
const auto REG_MARGIN_LEFT = L"LeftMargin";
const auto REG_MARGIN_RIGHT = L"RightMargin";
const auto REG_MARGIN_TOP = L"TopMargin";
const auto REG_MARGIN_BOTTOM = L"BottomMargin";

static auto s_textHighlighter = std::make_shared<TextHighight>();


document::document(IView &view, const std::wstring &text, int nCrlfStyle) : _highlight(s_textHighlighter), _view(view)
{    
    _modified = false;
    m_bCreateBackupFile = false;
    m_nUndoPosition = 0;
    m_nCRLFMode = nCrlfStyle;

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

document::~document()
{
}

DWORD document::highlight_cookie(int lineIndex) const
{
    int line_count = _lines.size();

    if (lineIndex < 0 || lineIndex >= line_count)
        return 0;

    int i = lineIndex;
    while (i >= 0 && _lines[i]._parseCookie == invalid)
        i--;
    i++;

    int nBlocks;
    while (i <= lineIndex && _lines[i]._parseCookie == invalid)
    {
        auto dwCookie = 0;

        if (i > 0)
            dwCookie = _lines[i - 1]._parseCookie;

        const auto &line = _lines[i];
        line._parseCookie = _highlight->ParseLine(dwCookie, line, nullptr, nBlocks);

        assert(line._parseCookie != invalid);
        i++;
    }

    return _lines[lineIndex]._parseCookie;
}

DWORD document::highlight_line(DWORD cookie, const document_line &line, IHighlight::TEXTBLOCK *pBuf, int &nBlocks) const
{
    return _highlight->ParseLine(cookie, line, pBuf, nBlocks);
}

bool document::is_inside_selection(const text_location &ptTextPos) const
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
    m_bAutoIndent = true;
    m_tabSize = 4;
    m_nMaxLineLength = -1;
    m_nIdealCharPos = -1;
    m_ptAnchor.x = 0;
    m_ptAnchor.y = 0;

    for (auto &line : _lines)
    {
        line._parseCookie = invalid;
        line._expanded_length = invalid;
    }

    m_ptCursorPos.x = 0;
    m_ptCursorPos.y = 0;
    _selection._start = _selection._end = m_ptCursorPos;
    m_bLastSearch = false;
    m_bShowInactiveSelection = false;
    m_bMultipleSearch = false;
}

void document::tab_size(int tabSize)
{
    assert(tabSize >= 0 && tabSize <= 64);
    if (m_tabSize != tabSize)
    {
        m_tabSize = tabSize;

        for (auto &line : _lines)
        {
            line._expanded_length = invalid;
        }

        m_nMaxLineLength = -1;        
        _view.invalidate_view();        
    }
}

int document::max_line_length() const
{
    if (m_nMaxLineLength == -1)
    {
        m_nMaxLineLength = 0;
        auto line_count = _lines.size();

        for (int i = 0; i < line_count; i++)
        {
            update_max_line_length(i);
        }
    }

    return m_nMaxLineLength;
}

void document::update_max_line_length(int i) const
{
    int len = expanded_line_length(i);

    if (m_nMaxLineLength < len)
        m_nMaxLineLength = len;
}

int document::expanded_line_length(int lineIndex) const
{
    const auto &line = _lines[lineIndex];

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

std::wstring document::expanded_chars(const std::wstring &text, int nOffset, int nCount) const
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
        int nLength = nCount;

        int nTabCount = 0;
        for (auto i = 0; i < nLength; i++)
        {
            if (pszChars[i] == _T('\t'))
                nTabCount++;
        }

        int nCurPos = 0;

        if (nTabCount > 0 || m_bViewTabs)
        {
            for (auto i = 0; i < nLength; i++)
            {
                if (pszChars[i] == _T('\t'))
                {
                    int nSpaces = tabSize - (nActualOffset + nCurPos) % tabSize;

                    if (m_bViewTabs)
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
                    result += (pszChars[i] == _T(' ') && m_bViewTabs) ? SPACE_CHARACTER : pszChars[i];
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
    int result = 0;

    if (lineIndex >= 0 && lineIndex < _lines.size())
    {
        const auto &line = _lines[lineIndex];
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

    const auto &line = _lines[lineIndex];
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



void document::anchor_pos(const text_location &ptNewAnchor)
{
    m_ptAnchor = ptNewAnchor;
}

void document::cursor_pos(const text_location &pos)
{
    if (m_ptCursorPos != pos)
    {
        m_ptCursorPos = pos;
        m_nIdealCharPos = calc_offset(m_ptCursorPos.y, m_ptCursorPos.x);
        _view.update_caret();
    }
}

bool document::HighlightText(const text_location &ptStartPos, int nLength)
{
    m_ptCursorPos = ptStartPos;
    m_ptCursorPos.x += nLength;
    m_ptAnchor = m_ptCursorPos;
    select(text_selection(ptStartPos, m_ptCursorPos));
    _view.ensure_visible(m_ptCursorPos);
    return true;
}


void document::Find(const std::wstring &text, DWORD flags)
{
    text_location loc;

    if (Find(text, m_ptCursorPos, flags, true, &loc))
    {
        auto end = loc;
        end.x += text.size();
        select(text_selection(loc, end));
        m_ptCursorPos = loc;
        _view.update_caret();

        _lastFindWhat = text;
        m_dwLastSearchFlags = flags;
        m_bShowInactiveSelection = true;
    }
    //	CWinApp *pApp = AfxGetApp();
    //	assert(pApp != nullptr);
    //
    //	CFindTextDlg dlg(this);
    //	if (m_bLastSearch)
    //	{
    //		//	Get the latest search parameters
    //		dlg.m_bMatchCase = (m_dwLastSearchFlags & FIND_MATCH_CASE) != 0;
    //		dlg.m_bWholeWord = (m_dwLastSearchFlags & FIND_WHOLE_WORD) != 0;
    //		dlg.m_nDirection = (m_dwLastSearchFlags & FIND_DIRECTION_UP) != 0 ? 0 : 1;
    //		if (_lastFindWhat != nullptr)
    //			dlg.m_sText = _lastFindWhat;
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
    //	if (_lastFindWhat != nullptr)
    //		free(_lastFindWhat);
    //	_lastFindWhat = _wcsdup(dlg.m_sText);
    //
    //	m_dwLastSearchFlags = 0;
    //	if (dlg.m_bMatchCase)
    //		m_dwLastSearchFlags |= FIND_MATCH_CASE;
    //	if (dlg.m_bWholeWord)
    //		m_dwLastSearchFlags |= FIND_WHOLE_WORD;
    //	if (dlg.m_nDirection == 0)
    //		m_dwLastSearchFlags |= FIND_DIRECTION_UP;
    //
    //	//	Save search parameters to registry
    //	pApp->WriteProfileInt(REG_FIND_SUBKEY, REG_MATCH_CASE, dlg.m_bMatchCase);
    //	pApp->WriteProfileInt(REG_FIND_SUBKEY, REG_WHOLE_WORD, dlg.m_bWholeWord);
    //	pApp->WriteProfileString(REG_FIND_SUBKEY, REG_FIND_WHAT, dlg.m_sText);
}

void document::OnEditRepeat()
{
    //if (m_bLastSearch)
    //{
    //	text_location ptFoundPos;
    //	if (! FindText(_lastFindWhat, m_ptCursorPos, m_dwLastSearchFlags, true, &ptFoundPos))
    //	{
    //		std::wstring prompt;
    //		prompt.Format(IDS_EDIT_TEXT_NOT_FOUND, _lastFindWhat);
    //		AfxMessageBox(prompt);
    //		return;
    //	}
    //	HighlightText(ptFoundPos, lstrlen(_lastFindWhat));
    //	m_bMultipleSearch = true;       // More search       
    //}
}



void document::OnEditFindPrevious()
{
    DWORD dwSaveSearchFlags = m_dwLastSearchFlags;
    if ((m_dwLastSearchFlags & FIND_DIRECTION_UP) != 0)
        m_dwLastSearchFlags &= ~FIND_DIRECTION_UP;
    else
        m_dwLastSearchFlags |= FIND_DIRECTION_UP;
    OnEditRepeat();
    m_dwLastSearchFlags = dwSaveSearchFlags;
}

bool document::view_tabs() const
{
    return m_bViewTabs;
}

void document::view_tabs(bool bViewTabs)
{
    if (bViewTabs != m_bViewTabs)
    {
        m_bViewTabs = bViewTabs;
        _view.invalidate_view();
    }
}



void document::MoveLeft(bool selecting)
{
    auto sel = _selection.normalize();

    if (!sel.empty() && !selecting)
    {
        m_ptCursorPos = sel._start;
    }
    else
    {
        if (m_ptCursorPos.x == 0)
        {
            if (m_ptCursorPos.y > 0)
            {
                m_ptCursorPos.y--;
                m_ptCursorPos.x = _lines[m_ptCursorPos.y].size();
            }
        }
        else
            m_ptCursorPos.x--;
    }
    m_nIdealCharPos = calc_offset(m_ptCursorPos.y, m_ptCursorPos.x);
    _view.ensure_visible(m_ptCursorPos);
    if (!selecting)
        m_ptAnchor = m_ptCursorPos;
    select(text_selection(m_ptAnchor, m_ptCursorPos));
}

void document::MoveRight(bool selecting)
{
    auto sel = _selection.normalize();

    if (!sel.empty() && !selecting)
    {
        m_ptCursorPos = sel._end;
    }
    else
    {
        if (m_ptCursorPos.x == _lines[m_ptCursorPos.y].size())
        {
            if (m_ptCursorPos.y < _lines.size() - 1)
            {
                m_ptCursorPos.y++;
                m_ptCursorPos.x = 0;
            }
        }
        else
            m_ptCursorPos.x++;
    }
    m_nIdealCharPos = calc_offset(m_ptCursorPos.y, m_ptCursorPos.x);
    _view.ensure_visible(m_ptCursorPos);
    if (!selecting)
        m_ptAnchor = m_ptCursorPos;
    select(text_selection(m_ptAnchor, m_ptCursorPos));
}

void document::MoveWordLeft(bool selecting)
{
    auto sel = _selection.normalize();

    if (!sel.empty() && !selecting)
    {
        MoveLeft(selecting);
        return;
    }

    if (m_ptCursorPos.x == 0)
    {
        if (m_ptCursorPos.y == 0)
            return;

        m_ptCursorPos.y--;
        m_ptCursorPos.x = _lines[m_ptCursorPos.y].size();
    }

    const auto &line = _lines[m_ptCursorPos.y];
    auto nPos = m_ptCursorPos.x;

    WordToLeft(m_ptCursorPos);

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

    m_ptCursorPos.x = nPos;
    m_nIdealCharPos = calc_offset(m_ptCursorPos.y, m_ptCursorPos.x);
    _view.ensure_visible(m_ptCursorPos);
    if (!selecting)
        m_ptAnchor = m_ptCursorPos;
    select(text_selection(m_ptAnchor, m_ptCursorPos));
}

void document::MoveWordRight(bool selecting)
{
    auto sel = _selection.normalize();

    if (!sel.empty() && !selecting)
    {
        MoveRight(selecting);
        return;
    }

    if (m_ptCursorPos.x == _lines[m_ptCursorPos.y].size())
    {
        if (m_ptCursorPos.y == _lines.size() - 1)
            return;
        m_ptCursorPos.y++;
        m_ptCursorPos.x = 0;
    }

    auto nLength = _lines[m_ptCursorPos.y].size();

    if (m_ptCursorPos.x == nLength)
    {
        MoveRight(selecting);
        return;
    }

    const auto &line = _lines[m_ptCursorPos.y];
    int nPos = m_ptCursorPos.x;

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

    m_ptCursorPos.x = nPos;
    m_nIdealCharPos = calc_offset(m_ptCursorPos.y, m_ptCursorPos.x);
    _view.ensure_visible(m_ptCursorPos);
    if (!selecting)
        m_ptAnchor = m_ptCursorPos;
    select(text_selection(m_ptAnchor, m_ptCursorPos));
}

void document::MoveUp(bool selecting)
{
    auto sel = _selection.normalize();

    if (!sel.empty() && !selecting)
        m_ptCursorPos = sel._start;

    if (m_ptCursorPos.y > 0)
    {
        if (m_nIdealCharPos == -1)
            m_nIdealCharPos = calc_offset(m_ptCursorPos.y, m_ptCursorPos.x);
        m_ptCursorPos.y--;
        m_ptCursorPos.x = calc_offset_approx(m_ptCursorPos.y, m_nIdealCharPos);

        auto size = _lines[m_ptCursorPos.y].size();

        if (m_ptCursorPos.x > size)
            m_ptCursorPos.x = size;
    }
    _view.ensure_visible(m_ptCursorPos);
    if (!selecting)
        m_ptAnchor = m_ptCursorPos;

    select(text_selection(m_ptAnchor, m_ptCursorPos));
}

void document::MoveDown(bool selecting)
{
    auto sel = _selection.normalize();

    if (!sel.empty() && !selecting)
        m_ptCursorPos = sel._end;

    if (m_ptCursorPos.y < _lines.size() - 1)
    {
        if (m_nIdealCharPos == -1)
            m_nIdealCharPos = calc_offset(m_ptCursorPos.y, m_ptCursorPos.x);

        m_ptCursorPos.y++;
        m_ptCursorPos.x = calc_offset_approx(m_ptCursorPos.y, m_nIdealCharPos);

        auto size = _lines[m_ptCursorPos.y].size();

        if (m_ptCursorPos.x > size)
            m_ptCursorPos.x = size;
    }
    _view.ensure_visible(m_ptCursorPos);
    if (!selecting)
        m_ptAnchor = m_ptCursorPos;
    select(text_selection(m_ptAnchor, m_ptCursorPos));
}

void document::MoveHome(bool selecting)
{
    const auto &line = _lines[m_ptCursorPos.y];

    int nHomePos = 0;
    while (nHomePos < line.size() && iswspace(line[nHomePos]))
        nHomePos++;
    if (nHomePos == line.size() || m_ptCursorPos.x == nHomePos)
        m_ptCursorPos.x = 0;
    else
        m_ptCursorPos.x = nHomePos;
    m_nIdealCharPos = calc_offset(m_ptCursorPos.y, m_ptCursorPos.x);
    _view.ensure_visible(m_ptCursorPos);
    if (!selecting)
        m_ptAnchor = m_ptCursorPos;
    select(text_selection(m_ptAnchor, m_ptCursorPos));
}

void document::MoveEnd(bool selecting)
{
    m_ptCursorPos.x = _lines[m_ptCursorPos.y].size();
    m_nIdealCharPos = calc_offset(m_ptCursorPos.y, m_ptCursorPos.x);
    _view.ensure_visible(m_ptCursorPos);
    if (!selecting)
        m_ptAnchor = m_ptCursorPos;
    select(text_selection(m_ptAnchor, m_ptCursorPos));
}

void document::MoveCtrlHome(bool selecting)
{
    m_ptCursorPos.x = 0;
    m_ptCursorPos.y = 0;
    m_nIdealCharPos = calc_offset(m_ptCursorPos.y, m_ptCursorPos.x);
    _view.ensure_visible(m_ptCursorPos);
    if (!selecting)
        m_ptAnchor = m_ptCursorPos;
    select(text_selection(m_ptAnchor, m_ptCursorPos));
}

void document::MoveCtrlEnd(bool selecting)
{
    m_ptCursorPos.y = _lines.size() - 1;
    m_ptCursorPos.x = _lines[m_ptCursorPos.y].size();
    m_nIdealCharPos = calc_offset(m_ptCursorPos.y, m_ptCursorPos.x);
    _view.ensure_visible(m_ptCursorPos);
    if (!selecting)
        m_ptAnchor = m_ptCursorPos;
    select(text_selection(m_ptAnchor, m_ptCursorPos));
}

text_location document::WordToRight(text_location pt) const
{
    const auto &line = _lines[pt.y];

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
    const auto &line = _lines[pt.y];

    while (pt.x > 0)
    {
        if (!iswalnum(line[pt.x - 1]) && line[pt.x - 1] != _T('_'))
            break;
        pt.x--;
    }
    return pt;
}

void document::SelectAll()
{
    int line_count = _lines.size();
    m_ptCursorPos.x = _lines[line_count - 1].size();
    m_ptCursorPos.y = line_count - 1;
    select(text_selection(text_location(0, 0), m_ptCursorPos));
    _view.update_caret();
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

            _view.RecalcHorzScrollBar();
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
                const auto &line = _lines[i];

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

            _view.RecalcHorzScrollBar();
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

                const auto &line = _lines[ptCursorPos.y];
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

void document::OnEditReplace()
{
    //if (! QueryEditable())
    //	return;

    //CWinApp *pApp = AfxGetApp();
    //assert(pApp != nullptr);

    //CEditReplaceDlg dlg(this);

    ////	Take search parameters from registry
    //dlg.m_bMatchCase = pApp->GetProfileInt(REG_REPLACE_SUBKEY, REG_MATCH_CASE, false);
    //dlg.m_bWholeWord = pApp->GetProfileInt(REG_REPLACE_SUBKEY, REG_WHOLE_WORD, false);
    //dlg.m_sText = pApp->GetProfileString(REG_REPLACE_SUBKEY, REG_FIND_WHAT, _T(""));
    //dlg.m_sNewText = pApp->GetProfileString(REG_REPLACE_SUBKEY, REG_REPLACE_WITH, _T(""));

    //if (has_selection())
    //{
    //	selection(m_ptSavedSelStart, m_ptSavedSelEnd);
    //	m_bSelectionPushed = true;

    //	dlg.m_nScope = 0;	//	Replace in current selection
    //	dlg.m_ptCurrentPos = m_ptSavedSelStart;
    //	dlg.m_bEnableScopeSelection = true;
    //	dlg.m_ptBlockBegin = m_ptSavedSelStart;
    //	dlg.m_ptBlockEnd = m_ptSavedSelEnd;
    //}
    //else
    //{
    //	dlg.m_nScope = 1;	//	Replace in whole text
    //	dlg.m_ptCurrentPos = cursor_pos();
    //	dlg.m_bEnableScopeSelection = false;
    //}

    ////	Execute Replace dialog
    //m_bShowInactiveSelection = true;
    //dlg.DoModal();
    //m_bShowInactiveSelection = false;

    ////	Restore selection
    //if (m_bSelectionPushed)
    //{
    //	select(m_ptSavedSelStart, m_ptSavedSelEnd);
    //	m_bSelectionPushed = false;
    //}

    ////	Save search parameters to registry
    //pApp->WriteProfileInt(REG_REPLACE_SUBKEY, REG_MATCH_CASE, dlg.m_bMatchCase);
    //pApp->WriteProfileInt(REG_REPLACE_SUBKEY, REG_WHOLE_WORD, dlg.m_bWholeWord);
    //pApp->WriteProfileString(REG_REPLACE_SUBKEY, REG_FIND_WHAT, dlg.m_sText);
    //pApp->WriteProfileString(REG_REPLACE_SUBKEY, REG_REPLACE_WITH, dlg.m_sNewText);
}

bool document::ReplaceSelection(const wchar_t * pszNewText)
{
    //assert(pszNewText != nullptr);
    //if (! has_selection())
    //	return false;

    //DeleteCurrentSelection();

    //text_location ptCursorPos = cursor_pos();
    //int x, y;
    //_lines.insert_text(this, ptCursorPos.y, ptCursorPos.x, pszNewText, y, x, CE_ACTION_REPLACE);
    //text_location ptEndOfBlock = text_location(x, y);
    //anchor_pos(ptEndOfBlock);
    //select(ptCursorPos, ptEndOfBlock);
    //cursor_pos(ptEndOfBlock);
    //ensure_visible(ptEndOfBlock);
    return true;
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



//void document::OnEditOperation(int nAction, const std::wstring &text)
//{
//	if (m_bAutoIndent)
//	{
//		//	Analyse last action...
//		if (nAction == CE_ACTION_TYPING && _tcscmp(text.c_str(), _T("\r\n")) == 0 && !_overtype)
//		{
//			//	Enter stroke!
//			text_location ptCursorPos = cursor_pos();
//			assert(ptCursorPos.y > 0);
//
//			//	Take indentation from the previos line
//			const auto &line = _lines.GetLineChars(ptCursorPos.y - 1);
//			const auto len = line.size();
//
//			int nPos = 0;
//			while (nPos < len && iswspace(line[nPos]))
//				nPos++;
//
//			if (nPos > 0)
//			{
//				//	insert part of the previos line
//				TCHAR *pszInsertStr = (TCHAR *) _alloca(sizeof(TCHAR) * (len + 1));
//				_tcsncpy_s(pszInsertStr, (len + 1), line.c_str(), nPos);
//				pszInsertStr[nPos] = 0;
//
//				int x, y;
//				_lines.insert_text(ug, ptCursorPos.y, ptCursorPos.x, pszInsertStr, y, x, CE_ACTION_AUTOINDENT);
//
//				text_location pt(x, y);
//				cursor_pos(pt);
//				select(pt, pt);
//				anchor_pos(pt);
//				ensure_visible(pt);
//			}
//		}
//	}
//}

bool document::GetOverwriteMode() const
{
    return _overtype;
}

void document::SetOverwriteMode(bool bOvrMode /*= true*/)
{
    _overtype = bOvrMode;
}

bool document::GetAutoIndent() const
{
    return m_bAutoIndent;
}

void document::SetAutoIndent(bool bAutoIndent)
{
    m_bAutoIndent = bAutoIndent;
}

static bool IsCppExtension(const wchar_t *ext)
{
    static auto comp = [](const wchar_t *l, const wchar_t *r) { return _wcsicmp(l, r) < 0; };
    static std::set<const wchar_t*, std::function<bool(const wchar_t *, const wchar_t *)>> extensions(comp);

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

void document::HighlightFromExtension(const wchar_t *ext)
{
    if (*ext == L'.') ext++;

    if (IsCppExtension(ext))
    {
        static auto highlighter = std::make_shared<CppSyntax>();
        _highlight = highlighter;
    }
    else
    {
        _highlight = s_textHighlighter;
    }
}

void document::append_line(const std::wstring &text)
{
	_lines.push_back(document_line(text));
}

void document::clear()
{    
	_lines.clear();
	_undo.clear();

    reset();
}

static const char *crlfs [] =
{
	"\x0d\x0a",			//	DOS/Windows style
	"\x0a\x0d",			//	UNIX style
	"\x0a"				//	Macintosh style
};

static std::istream& safeGetline(std::istream& is, std::string& t)
{
	t.clear();

	// The characters in the stream are read one-by-one using a std::streambuf.
	// That is faster than reading them one-by-one using the std::istream.
	// Code that uses streambuf this way must be guarded by a sentry object.
	// The sentry object performs various tasks,
	// such as thread synchronization and updating the stream state.

	std::istream::sentry se(is, true);
	std::streambuf* sb = is.rdbuf();

	for (;;) {
		int c = sb->sbumpc();
		switch (c) {
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
			t += (char) c;
		}
	}
}



struct _BOM_LOOKUP
{
	DWORD  bom;
	ULONG  len;
	Encoding    type;
};

struct _BOM_LOOKUP BOMLOOK [] =
{
	// define longest headers first
	{ 0x0000FEFF, 4, NCP_UTF32 },
	{ 0xFFFE0000, 4, NCP_UTF32BE },
	{ 0xBFBBEF, 3, NCP_UTF8 },
	{ 0xFFFE, 2, NCP_UTF16BE },
	{ 0xFEFF, 2, NCP_UTF16 },
	{ 0, 0, NCP_ASCII },
};

//
//	00 00 FE FF			UTF-32, big-endian 
//	FF FE 00 00			UTF-32, little-endian 
//	FE FF				UTF-16, big-endian 
//	FF FE				UTF-16, little-endian 
//	EF BB BF			UTF-8 
//
static Encoding detect_encoding(const unsigned char *header, size_t filesize, int &headerLen)
{
	for (int i = 0; BOMLOOK[i].len; i++)
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
		return NCP_UTF16;
	}

	if (header[0] != 0 && header[1] == 0 && header[2] != 0 && header[3] == 0)
	{
		return NCP_UTF16BE;
	}
	
	return NCP_ASCII;
}

static line_endings detect_line_endings(const char *buffer, const int len)
{
	int I = 0;

	for (I = 0; I < len; I++)
	{
		if (buffer[I] == 0x0a)
			break;
	}
	if (I < len)
	{
		if (I > 0 && buffer[I - 1] == 0x0d)
		{
			return CRLF_STYLE_DOS;
		}
		else
		{
			if (I < len - 1 && buffer[I + 1] == 0x0d)
				return CRLF_STYLE_UNIX;
			else
				return CRLF_STYLE_MAC;
		}
	}

	return CRLF_STYLE_DOS; // guess
}

bool document::LoadFromFile(const std::wstring &path)
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
		const int bufferLen = 1024 * 64;
		char buffer[bufferLen];

		DWORD readLen;
		if (::ReadFile(hFile, buffer, bufferLen, &readLen, nullptr))
		{
			auto headerLen = 0;
			auto size = GetFileSize(hFile, nullptr);
			auto encoding = detect_encoding((const unsigned char *)buffer, size, headerLen);

			m_nCRLFMode = detect_line_endings(buffer, readLen);
			const char *crlf = crlfs[m_nCRLFMode];

			auto bufferPos = headerLen;
            auto last_char = 0;

			if (encoding == NCP_UTF8 || encoding == NCP_ASCII)
			{
				std::string line;

				while (readLen > 0)
				{
					int c = buffer[bufferPos];

                    if ((last_char == 0x0A && c == 0x0D) || (last_char == 0x0A && c != 0x0D))
                    {
                        append_line((encoding == NCP_ASCII) ? AsciiToUtf16(line) : UTF8ToUtf16(line));
                        line.clear();
                    }

					if (c != 0x0A && c != 0x0D)
					{
						line += (char) c;
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

				append_line((encoding == NCP_ASCII) ? AsciiToUtf16(line) : UTF8ToUtf16(line));
			}
			else if (encoding == NCP_UTF16BE || encoding == NCP_UTF16)
			{
				auto buffer16 = (const wchar_t *) buffer;
				readLen /= 2;

				std::wstring line;

				while (readLen > 0)
				{
					wchar_t c = buffer16[bufferPos];

					if (encoding == NCP_UTF16)
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
			m_nUndoPosition = 0;

			success = true;

			_view.invalidate_view();
		}

		if (hFile != nullptr)
			::CloseHandle(hFile);
	}

	return success;
}


static std::wstring TempPathName()
{
	wchar_t result[MAX_PATH + 1] = { 0 };
	GetTempPath(MAX_PATH, result);
	GetTempFileName(result, L"CC_", 0, result);
	return result;
}

// Create a string with last error message
static std::wstring GetLastErrorMessage()
{
	std::wstring result;
	auto error = GetLastError();

	if (error)
	{
		LPVOID lpMsgBuf;
		DWORD bufLen = FormatMessageW(
			FORMAT_MESSAGE_ALLOCATE_BUFFER |
			FORMAT_MESSAGE_FROM_SYSTEM |
			FORMAT_MESSAGE_IGNORE_INSERTS,
			NULL,
			error,
			MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
			(LPWSTR) &lpMsgBuf,
			0, NULL);

		if (bufLen)
		{
			result = (LPCWSTR) lpMsgBuf;
			LocalFree(lpMsgBuf);
		}
	}
	return result;
}

bool document::SaveToFile(const std::wstring &path, int nCrlfStyle /*= CRLF_STYLE_AUTOMATIC*/, bool bClearModifiedFlag /*= true*/) const
{
	auto success = false;
	auto tempPath = TempPathName();
	std::ofstream fout(tempPath);

	if (fout)
	{
		bool first = true;

		for (const auto &line : _lines)
		{
			if (!first) fout << std::endl;
			fout << UTF16ToAscii(line._text);
			first = false;
		}

		fout.close();

		success = ::MoveFileEx(tempPath.c_str(), path.c_str(), MOVEFILE_COPY_ALLOWED | MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;

		if (success && bClearModifiedFlag)
		{
			_modified = false;
		}

		if (!success)
		{
			MessageBox(GetActiveWindow(), GetLastErrorMessage().c_str(), L"Diffractor", MB_OK);
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
	//			DWORD dwWrittenBytes;

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


std::vector<std::wstring> document::text(const text_selection &selection) const
{
	std::vector<std::wstring> result;

	if (!selection.empty())
	{
		if (selection._start.y == selection._end.y)
		{
			result.push_back(_lines[selection._start.y]._text.substr(selection._start.x, selection._end.x - selection._start.x));
		}
		else
		{
			for (int y = selection._start.y; y <= selection._end.y; y++)
			{
				const auto &text = _lines[y]._text;

				if (y == selection._start.y)
				{
					result.push_back(text.substr(selection._start.x, text.size() - selection._start.x));
				}
				else if (y == selection._end.y)
				{
					result.push_back(text.substr(0, selection._end.x));
				}
				else
				{
					result.push_back(text);
				}
			}
		}
	}

	return result;
}

std::vector<std::wstring> document::text() const
{
	std::vector<std::wstring> result;

	for (const auto &line : _lines)
	{
		result.push_back(line._text);
	}

	return result;
}

bool document::can_undo() const
{
	assert(m_nUndoPosition >= 0 && m_nUndoPosition <= _undo.size());
	return m_nUndoPosition > 0;
}

bool document::can_redo() const
{
	assert(m_nUndoPosition >= 0 && m_nUndoPosition <= _undo.size());
	return m_nUndoPosition < _undo.size();
}

text_location document::undo()
{
	assert(can_undo());
	m_nUndoPosition--;
	_modified = true;
	return _undo[m_nUndoPosition].undo(*this);
}

text_location document::redo()
{
	assert(can_redo());	
	_modified = true;
	auto result = _undo[m_nUndoPosition].redo(*this);
	m_nUndoPosition++;
	return result;
}

text_location document::insert_text(const text_location &location, const std::wstring &text)
{
	text_location resultLocation = location;

	for (const auto &c : text)
	{
		resultLocation = insert_text(resultLocation, c);
	}

	_modified = true;

	return resultLocation;
}

text_location document::insert_text(undo_group &ug, const text_location &location, const std::wstring &text)
{	
	text_location resultLocation = location;

	for (const auto &c : text)
	{
		resultLocation = insert_text(resultLocation, c);
	}

	ug.insert(text_selection(location, resultLocation), text);
	_modified = true;

	return resultLocation;
}

text_location document::insert_text(const text_location &location, const wchar_t &c)
{
	text_location resultLocation = location;
	auto &li = _lines[location.y];

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

text_location document::insert_text(undo_group &ug, const text_location &location, const wchar_t &c)
{
	ug.insert(location, c);
	_modified = true;
	return insert_text(location, c);
}

text_location document::delete_text(undo_group &ug, const text_location &location)
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

text_location document::delete_text(const text_selection &selection)
{
	if (!selection.empty())
	{
		if (selection._start.y == selection._end.y)
		{
			auto &li = _lines[selection._start.y];
			auto before = li._text;
			auto after = li._text;

			after.erase(after.begin() + selection._start.x, after.begin() + selection._end.x);
			li._text = after;

			_view.invalidate_line(selection._start.y);
		}
		else
		{
			_lines[selection._start.y]._text.erase(_lines[selection._start.y]._text.begin() + selection._start.x, _lines[selection._start.y]._text.end());
			_lines[selection._start.y]._text.append(_lines[selection._end.y]._text.begin() + selection._end.x, _lines[selection._end.y]._text.end());

			if (selection._start.y + 1 < selection._end.y + 1)
			{
				_lines.erase(_lines.begin() + selection._start.y + 1, _lines.begin() + selection._end.y + 1);
			}

            _view.invalidate_view();
		}
	}

	return selection._start;
}

text_location document::delete_text(undo_group &ug, const text_selection &selection)
{
	ug.erase(selection, Combine(text(selection)));
	_modified = true;
	return delete_text(selection);
}

text_location document::delete_text(const text_location &location)
{
	auto &line = _lines[location.y];
	auto resultPos = location;

	if (location.x == 0)
	{
		if (location.y > 0)
		{
			auto &previous = _lines[location.y - 1];

			resultPos.x = previous.size();
			resultPos.y = location.y - 1;

			previous._text.insert(previous._text.end(), line._text.begin(), line._text.end());
			_lines.erase(_lines.begin() + location.y);

            _view.invalidate_view();
		}
	}
	else
	{
		auto &li = _lines[location.y];
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



static wchar_t* wcsistr(wchar_t const* s1, wchar_t const* s2)
{
	auto s = s1;
	auto p = s2;

	do
	{
		if (!*p) return (wchar_t*) s1;
		if ((*p == *s) || (towlower(*p) == towlower(*s)))
		{
			++p;
			++s;
		}
		else
		{
			p = s2;
			if (!*s) return nullptr;
			s = ++s1;
		}

	} while (1);

	return nullptr;
}


static int FindStringHelper(const wchar_t * pszFindWhere, const wchar_t * pszFindWhat, bool bWholeWord)
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

	assert(false);		// Unreachable
	return -1;
}

bool document::Find(const std::wstring &text, const text_location &ptStartPos, DWORD dwFlags, bool bWrapSearch, text_location *pptFoundPos)
{
	int line_count = _lines.size();

	return FindInBlock(text, ptStartPos, text_location(0, 0), text_location(_lines[line_count - 1]._text.size(), line_count - 1), dwFlags, bWrapSearch, pptFoundPos);
}

bool document::FindInBlock(const std::wstring &what, const text_location &ptStartPosition, const text_location &ptBlockBegin, const text_location &ptBlockEnd, DWORD dwFlags, bool bWrapSearch, text_location *pptFoundPos)
{
	text_location ptCurrentPos = ptStartPosition;

	assert(ptBlockBegin < ptBlockEnd);

	if (ptBlockBegin == ptBlockEnd)
		return false;

	if (ptCurrentPos < ptBlockBegin)
	{
		ptCurrentPos = ptBlockBegin;
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
				const auto &line = _lines[ptCurrentPos.y];
				const auto nLineLength = line._text.size() - ptCurrentPos.x;

				if (nLineLength > 0)
				{
					auto nPos = ::FindStringHelper(line._text.c_str() + ptCurrentPos.x, what.c_str(), wholeWord);

					if (nPos >= 0)		//	Found text!
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
			while (ptCurrentPos.y <= ptBlockEnd.y)
			{
				const auto &line = _lines[ptCurrentPos.y];
				const auto nLineLength = line._text.size() - ptCurrentPos.x;

				if (nLineLength > 0)
				{
					int nPos = ::FindStringHelper(line._text.c_str() + ptCurrentPos.x, what.c_str(), wholeWord);

					if (nPos >= 0)
					{
						ptCurrentPos.x += nPos;
						//	Check of the text found is outside the block.
						if (ptCurrentPos.y == ptBlockEnd.y && ptCurrentPos.x >= ptBlockEnd.x)
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
			ptCurrentPos = ptBlockBegin;
		}
	}

	assert(false);		// Unreachable
	return false;
}