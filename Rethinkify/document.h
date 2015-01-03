#pragma once


#include "spell_check.h"
#include "Util.h"

class undo_group;
class text_view;
class document_line;
class text_location;

typedef DWORD DROPEFFECT;
typedef int POSITION;

const int RETHINKIFY_TIMER_DRAGSEL = 1001;
const auto invalid = -1;

const auto TAB_CHARACTER = 0xBB;
const auto SPACE_CHARACTER = 0x95;
const auto DEFAULT_PRINT_MARGIN = 1000; //	10 millimeters
const auto DRAG_BORDER_X = 5;
const auto DRAG_BORDER_Y = 5;

class IHighlight
{
public:

    enum
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

    struct TEXTBLOCK
    {
        size_t	m_nCharPos;
        int m_nColorIndex;
    };

    virtual DWORD ParseLine(DWORD dwCookie, const document_line &line, TEXTBLOCK *pBuf, int &nActualItems) const = 0;
    virtual std::vector<std::wstring> suggest(const std::wstring &wword) const = 0;
    virtual bool can_add(const std::wstring &word) const { return false; };
    virtual void add_word(const std::wstring &word) const { };
};

class CppSyntax : public IHighlight
{
    DWORD ParseLine(DWORD dwCookie, const document_line &line, TEXTBLOCK *pBuf, int &nActualItems) const;
    std::vector<std::wstring> suggest(const std::wstring &wword) const { return std::vector<std::wstring>(); };
};

class TextHighight : public IHighlight
{
    mutable spell_check _check;

    DWORD ParseLine(DWORD dwCookie, const document_line &line, TEXTBLOCK *pBuf, int &nActualItems) const;
    std::vector<std::wstring> suggest(const std::wstring &wword) const;
    bool can_add(const std::wstring &word) const { return !_check.is_word_valid(word.c_str(), word.size()); };
    void add_word(const std::wstring &word) const { _check.add_word(word); };
};

class IView
{
public:

    virtual std::wstring text_from_clipboard() const = 0;
    virtual bool text_to_clipboard(const std::wstring &text) = 0;
    virtual void update_caret() = 0;
    virtual void RecalcHorzScrollBar(bool bPositionOnly = false) = 0;
    virtual void RecalcVertScrollBar(bool bPositionOnly = false) = 0;
    virtual void ScrollToLine(int nNewTopLine, bool bTrackScrollBar = true) = 0;
    virtual void ScrollToChar(int nNewOffsetChar, bool bTrackScrollBar = true) = 0;
    virtual void invalidate_lines(int nLine1, int nLine2, bool bInvalidateMargin = false) = 0;
    virtual void invalidate_line(int index) = 0;
    virtual void invalidate_view() = 0;
    virtual void layout() = 0;
    virtual void ensure_visible(const text_location &pt) = 0;
};

typedef enum CRLFSTYLE
{
    CRLF_STYLE_AUTOMATIC = -1,
    CRLF_STYLE_DOS = 0,
    CRLF_STYLE_UNIX = 1,
    CRLF_STYLE_MAC = 2
} line_endings;

enum
{
    FIND_MATCH_CASE = 0x0001,
    FIND_WHOLE_WORD = 0x0002,
    FIND_DIRECTION_UP = 0x0010,
    REPLACE_SELECTION = 0x0100
};

class text_location
{
public:
    int x;
    int y;

    text_location(int xx = 0, int yy = 0) { x = xx; y = yy; }

    bool operator==(const text_location &other) const { return x == other.x && y == other.y; }
    bool operator!=(const text_location &other) const { return x != other.x && y != other.y; }
};

class text_selection
{
public:
    text_location _start;
    text_location _end;

    text_selection() { }
    text_selection(const text_location &start, const text_location &end) { _start = start; _end = end; }
    text_selection(int x1, int y1, int x2, int y2) : _start(x1, y1), _end(x2, y2) { }

    bool operator==(const text_selection &other) const { return _start == other._start && _end == other._end; }
    bool operator!=(const text_selection &other) const { return _start != other._start && _end != other._end; }

    bool empty() const { return _start == _end; };

    text_selection normalize() const
    {
        text_selection result;

        if (_start.y < _end.y || (_start.y == _end.y && _start.x < _end.x))
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
};

class document_line
{
public:

    std::wstring _text;
    DWORD _flags = 0;
    int _y = 0;
    int _cy = 0;

    mutable int _expanded_length = invalid;
    mutable int _parseCookie = invalid;

    document_line() { };
    document_line(const std::wstring &line) : _flags(0), _text(line) { };
    document_line(const document_line &other) : _flags(other._flags), _text(other._text) {};

    const document_line& operator=(const document_line &other)
    {
        _flags = other._flags;
        _text = other._text;
        return *this;
    };

    bool empty() const { return _text.empty(); };
    size_t size() const { return _text.size(); };
    const wchar_t *c_str() const { return _text.c_str(); };
    const wchar_t &operator[](int n) const { return _text[n]; };
};



class document
{
private:

    IView &_view;

    DWORD m_dwLastSearchFlags;
    text_location m_ptAnchor;
    text_location m_ptCursorPos;
    text_selection _selection;
    text_selection m_ptSavedSel;
    bool _overtype;
    bool m_bAutoIndent;
    bool m_bDraggingText;
    bool m_bFocused;
    bool m_bLastSearch;
    bool m_bMultipleSearch;
    bool m_bSelMargin;
    bool m_bSelectionPushed;
    bool m_bShowInactiveSelection;
    bool m_bViewTabs;
    int m_nIdealCharPos;
    int m_tabSize;
    mutable int m_nMaxLineLength;
    
    std::wstring _lastFindWhat;
    std::shared_ptr<IHighlight> _highlight;

    int margin_width() const;
    COLORREF GetColor(int nColorIndex) const;
    const text_location &cursor_pos() const { return m_ptCursorPos; };
    text_location WordToLeft(text_location pt) const;
    text_location WordToRight(text_location pt) const;
    DWORD prse_cookie(int lineIndex) const;

    std::wstring GetFromClipboard() const;
    bool selection_margin() const;
    bool view_tabs() const;
    bool HighlightText(const text_location &ptStartPos, int nLength);
    bool is_inside_selection(const text_location &loc) const;
    bool PutToClipboard(const std::wstring &text);
    int ApproxActualOffset(int lineIndex, int nOffset);
    int CalculateActualOffset(int lineIndex, int nCharIndex);
    int expanded_line_length(int lineIndex) const;
    int max_line_length() const;
    void update_max_line_length(int lineIndex) const;

    int tab_size() const;
    int top_offset() const;
    std::wstring ExpandChars(const std::wstring &text, int nOffset, int nCount) const;

private:

    struct undo_step
    {
    public:

        text_selection _selection;
        bool _insert;
        std::wstring _text;

    public:

        undo_step() : _insert(false){};
        undo_step(const text_location &location, const wchar_t &c, bool insert) : _text(1, c), _selection(location, location), _insert(insert){};
        undo_step(const text_location &location, const std::wstring &text, bool insert) : _text(text), _selection(location, location), _insert(insert){};
        undo_step(const text_selection &selection, const std::wstring &text, bool insert) : _text(text), _selection(selection), _insert(insert){};
        undo_step(const undo_step &other) : _text(other._text), _selection(other._selection), _insert(other._insert) {};

        const undo_step& operator=(const undo_step &other)
        {
            _text = other._text;
            _selection = other._selection;
            _insert = other._insert;
            return *this;
        };

        text_location undo(document &buffer)
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

        text_location redo(document &buffer)
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

        undo_item()  {};
        undo_item(const undo_item &other) : _steps(other._steps) {};

        const undo_item& operator=(const undo_item &other)
        {
            _steps = other._steps;
            return *this;
        };

        text_location undo(document &buffer)
        {
            text_location location;

            for (auto i = _steps.rbegin(); i != _steps.rend(); i++)
            {
                location = i->undo(buffer);
            }

            return location;
        }

        text_location redo(document &buffer)
        {
            text_location location;

            for (auto i = _steps.begin(); i != _steps.end(); i++)
            {
                location = i->redo(buffer);
            }

            return location;
        }

    };

    mutable bool _modified;
    int m_nCRLFMode;
    bool m_bCreateBackupFile;

    std::vector<document_line> _lines;
    std::vector<undo_item> _undo;

    size_t m_nUndoPosition;

public:

    document(IView &view, const std::wstring &text = std::wstring(), int nCrlfStyle = CRLF_STYLE_DOS);
    ~document();

    bool LoadFromFile(const std::wstring &path);
    bool SaveToFile(const std::wstring &path, int nCrlfStyle = CRLF_STYLE_AUTOMATIC, bool bClearModifiedFlag = true) const;
    void clear();

    bool IsModified() const { return _modified; }

    const bool empty() const { return _lines.empty(); };
    const size_t size() const { return _lines.size(); };
    const document_line &operator[](int n) const { return _lines[n]; };
    document_line &operator[](int n) { return _lines[n]; };

    std::vector<std::wstring> text(const text_selection &selection) const;
    std::vector<std::wstring> text() const;
    std::wstring str() const { return Combine(text()); }

    void append_line(const std::wstring &text);

    text_location insert_text(undo_group &ug, const text_location &location, const std::wstring &text);
    text_location insert_text(undo_group &ug, const text_location &location, const wchar_t &c);
    text_location delete_text(undo_group &ug, const text_selection &selection);
    text_location delete_text(undo_group &ug, const text_location &location);

    text_location insert_text(const text_location &location, const std::wstring &text);
    text_location insert_text(const text_location &location, const wchar_t &c);
    text_location delete_text(const text_selection &selection);
    text_location delete_text(const text_location &location);

    bool can_undo() const;
    bool can_redo() const;
    text_location undo();
    text_location redo();

    bool Find(const std::wstring &text, const text_location &ptStartPos, DWORD dwFlags, bool bWrapSearch, text_location *pptFoundPos);
    bool FindInBlock(const std::wstring &text, const text_location &ptStartPos, const text_location &ptBlockBegin, const text_location &ptBlockEnd, DWORD dwFlags, bool bWrapSearch, text_location *pptFoundPos);
    
    void HighlightFromExtension(const wchar_t *ext);

    bool CanPaste();
    bool CanFindNext() const { return m_bLastSearch; };
    bool has_selection() const { return !_selection.empty(); };
    bool GetAutoIndent() const;
    bool GetOverwriteMode() const;
    bool OnSetCursor(CWindow wnd, UINT nHitTest, UINT message);
    bool QueryEditable();
    bool ReplaceSelection(const wchar_t * pszNewText);
    void Cut();
    void Copy();
    void OnEditDelete();
    void OnEditDeleteBack();
    //void OnEditFind() { Find(_lastFindWhat); };
    void Find(const std::wstring &text, DWORD flags);
    void OnEditFindPrevious();
    void OnEditRedo();
    void OnEditRepeat();
    void OnEditReplace();
    void OnEditTab();
    void OnEditUndo();
    void OnEditUntab();
    void Paste();
    void SetAutoIndent(bool bAutoIndent);
    void SetOverwriteMode(bool bOvrMode = true);
    const text_selection &selection() const;
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

    void reset();
    void SelectAll();
    void SetAnchor(const text_location &ptNewAnchor);
    void SetCursorPos(const text_location &ptCursorPos);
    void SetDisableDragAndDrop(bool bDDAD);
    void selection_margin(bool bSelMargin);
    void tab_size(int nTabSize);
    void view_tabs(bool bViewTabs);

    text_selection word_selection() const
    {
        text_location ptStart, ptEnd;

        if (m_ptCursorPos.y < m_ptAnchor.y ||
            (m_ptCursorPos.y == m_ptAnchor.y && m_ptCursorPos.x < m_ptAnchor.x))
        {
            ptStart = WordToLeft(m_ptCursorPos);
            ptEnd = WordToRight(m_ptAnchor);
        }
        else
        {
            ptStart = WordToLeft(m_ptAnchor);
            ptEnd = WordToRight(m_ptCursorPos);
        }

        return text_selection(ptStart, ptEnd);
    }

    text_selection select_word(const text_location &loc)
    {
        m_ptAnchor = m_ptCursorPos = loc;

        auto selection = word_selection();
        select(selection);
        return selection;
    }

    void select(const text_selection &selection)
    {
        _view.invalidate_lines(selection._start.y, selection._end.y);
        _view.invalidate_lines(_selection._start.y, _selection._end.y);

        _selection = selection;

        SetAnchor(selection._start);
        SetCursorPos(selection._end);

        _view.ensure_visible(selection._end);
        _view.update_caret();
    }

    void locate(const text_location &pos)
    {
        select(text_selection(pos, pos));
        SetAnchor(pos);
        SetCursorPos(pos);
        _view.ensure_visible(pos);
    }

    void add_word(const std::wstring &word)
    {
        _highlight->add_word(word);
        _view.invalidate_view();
    }

    std::vector<std::wstring> suggest(const std::wstring &word) const
    {
        return _highlight->suggest(word);
    }

    bool can_add(const std::wstring &word) const
    {
        return _highlight->can_add(word);
    }

    friend class undo_group;
    friend class text_view;
};


class undo_group
{
    document &_buffer;
    document::undo_item _undo;

public:

    undo_group(document &buffer) : _buffer(buffer)
    {
    }

    ~undo_group()
    {
        _buffer._undo.push_back(_undo);
        _buffer.m_nUndoPosition = _buffer._undo.size();
    }

    void insert(const text_location &location, const wchar_t &c)
    {
        _undo._steps.push_back(document::undo_step(location, c, true));
    }

    void insert(const text_selection &selection, const std::wstring &text)
    {
        _undo._steps.push_back(document::undo_step(selection, text, true));
    }

    void erase(const text_location &location, const wchar_t &c)
    {
        _undo._steps.push_back(document::undo_step(location, c, false));
    }

    void erase(const text_selection &selection, const std::wstring &text)
    {
        _undo._steps.push_back(document::undo_step(selection, text, false));
    }
};

