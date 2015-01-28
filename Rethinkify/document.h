#pragma once


#include "spell_check.h"
#include "Util.h"

class undo_group;
class text_view;
class document_line;
class text_location;

const auto invalid = -1;
const auto TAB_CHARACTER = 0xBB;
const auto SPACE_CHARACTER = 0x95;

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
    virtual void RecalcHorzScrollBar() = 0;
    virtual void RecalcVertScrollBar() = 0;
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
};

class text_location
{
public:
    int x;
    int y;

    explicit text_location(int xx = 0, int yy = 0) { x = xx; y = yy; }

    bool operator==(const text_location &other) const { return x == other.x && y == other.y; }
    bool operator!=(const text_location &other) const { return x != other.x || y != other.y; }

    bool operator<(const text_location &other) const
    {
        return y < other.y || (y == other.y && x < other.x);
    }

    static const text_location null;
};

class text_selection
{
public:
    text_location _start;
    text_location _end;

    text_selection() { }
    text_selection(const text_location &start, const text_location &end) : _start(start), _end(end) { }
    text_selection(const text_location &loc) : _start(loc), _end(loc) { }
    text_selection(int x1, int y1, int x2, int y2) : _start(x1, y1), _end(x2, y2) { }

    bool operator==(const text_selection &other) const { return _start == other._start && _end == other._end; }
    bool operator!=(const text_selection &other) const { return _start != other._start || _end != other._end; }

    bool empty() const { return _start == _end; };
    bool is_valid() const { return _start.x >= 0 && _start.y >= 0 && _end.x >= 0 && _end.y >= 0; };

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
};


class document_line
{
public:

    std::wstring _text;
    DWORD _flags = 0;

    mutable int _expanded_length = invalid;
    mutable int _parseCookie = invalid;

    document_line() { };
    document_line(const std::wstring &line) : _flags(0), _text(line) { };
    document_line(const document_line &other) : _flags(other._flags), _text(other._text) {};
    document_line(document_line && other) : _flags(other._flags), _text(std::move(other._text)) {};

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

    enum line_type {
        LineEmpty = 0x001,		// empty line (whitespace or nothing)
        LineLabel = 0x002,		// ends with colon
        LineCase = 0x004,		// case
        LineAccess = 0x008,		// public/private
        LineBraceOpen = 0x010,		//line with opening brace
        LineBraceClose = 0x020,		//line with closing brace
        LineClosed = 0x040,		//Regular line (semicolon)
        LineOpen = 0x080,		//unfinished line (also for single-line if/else/while/for)
        LinePreprocessor = 0x100,		//preprocessor directive (starts with '#'), act like label
        LineComment = 0x200,		//single line comments
        LineMask = 0x3FF,		//All LineTypes
    };

    //enum line_flags {
    //    FlagNone = 0x01,	//No special flags for the line
    //    FlagBraceSingle = 0x02,	//The line only contains a brace
    //    FlagBraceText = 0x04,	//The line contains a brace and text
    //    FlagMask = 0x07	//All LineFlags
    //};

    static inline bool is_text(const wchar_t ch)
    {
        return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_';
    }

    static inline bool is_whitespace(const wchar_t ch)
    {
        return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
    }

    line_type calc_line_type() const 
    {
        auto first = _text.find_first_not_of(L" \t\r\n");
        auto len = _text.size();

        if (_text.empty() || first == std::string::npos)
        {
            return LineEmpty;
        }
        else if (_text[first] == '#')
        {
            return LinePreprocessor;
        }
        else if (first < (len - 1) && _text[first] == '/' && _text[first + 1] == '/')
        {
            return LineComment;
        }

        auto last = _text.find_last_not_of(L" \t\r\n");

        switch (_text[last]) 
        {
        case ':': 
        {
            const int max_word_len = 64;
            wchar_t word[max_word_len + 1];

            auto i = first;
            auto j = 0;
            auto c = _text[i++];

            while (j < max_word_len && i < last && is_text(c)) 
            {
                if (is_text(c))
                {
                    word[j++] = c;
                }
                
                c = _text[i++];
            }

            word[j] = 0;

            static std::map<const wchar_t *, line_type, ltstr> keywords;

            if (keywords.empty())
            {
                keywords[L"case"] = LineCase;
                keywords[L"default"] = LineCase;
                keywords[L"public"] = LineAccess;
                keywords[L"private"] = LineAccess;
                keywords[L"protected"] = LineAccess;
            }

            auto found = keywords.find(word);
            return found != keywords.end() ? found->second : LineLabel;
        }
        case '{':
            return LineBraceOpen;
        case '}':
            return LineBraceClose;
        case ';':
            return LineClosed;
        default:
            return LineOpen;
        }
    }
};

class document
{
private:

    IView &_view;

    
    text_location m_ptAnchor;
    text_location m_ptCursorPos;
    text_selection _selection;
    text_selection m_ptSavedSel;
    bool m_bAutoIndent = false;
    bool m_bViewTabs = false;
    int m_nIdealCharPos = 0;
    int m_tabSize = 4;
    mutable int m_nMaxLineLength = -1;

    std::wstring _path;
    DWORD _find_flags = 0;
    std::wstring _find_text;
    std::shared_ptr<IHighlight> _highlight;

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

    void Path(const std::wstring &path) { _path = path; }
    const std::wstring Path() const { return _path; }

    void append_line(const std::wstring &text);

    text_selection replace_text(undo_group &ug, const text_selection &selection, const std::wstring &text);

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
    void record_undo(const undo_item &ui);

    text_selection find_next(const std::wstring &text, text_location loc, const text_selection &selection, DWORD dwFlags, bool wrap_search) const;
    text_selection find_previous(const std::wstring &text, text_location loc, const text_selection &selection, DWORD dwFlags, bool wrap_search) const;

    void find_next(const std::wstring &text, DWORD flags = 0);
    void find_next();
    void find_previous(const std::wstring &text, DWORD flags = 0);
    void find_previous();
    bool can_find_next() const { return !_find_text.empty(); };

    void HighlightFromExtension(const wchar_t *ext);

    bool CanPaste();
    
    bool has_selection() const { return !_selection.empty(); };
    bool OnSetCursor(CWindow wnd, UINT nHitTest, UINT message);
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
    text_selection selection() const { return _selection.normalize(); };
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

    bool is_inside_selection(const text_location &loc) const;
    void reset();

    wchar_t char_at(const text_location &loc) const
    {
        return _lines[loc.y][loc.x];
    }

    text_location next(const text_location &loc, bool forward = true) const
    {
        auto last_line = _lines.size() - 1;
        auto result = text_location::null;

        if (loc.y >= 0 && loc.y <= last_line)
        {
            if (forward)
            {
                if (loc.x < _lines[last_line].size())
                {
                    result.x = loc.x + 1;
                    result.y = loc.y;
                }
                else if (loc.y < last_line)
                {
                    result.x = 0;
                    result.y = loc.y + 1;
                }
            }
            else
            {
                if (loc.x > 0)
                {
                    result.x = loc.x - 1;
                    result.y = loc.y;
                }
                else if (loc.y > 0)
                {                    
                    result.y = loc.y - 1;
                    result.x = _lines[result.y].size();
                }
            }
        }
        
        return result;
    }

    text_location end() const
    {
        auto last_line = _lines.size() - 1;
        return text_location(_lines[last_line].size(), last_line);
    }

    text_selection all() const
    {  
        return text_selection(text_location(0, 0), end());
    }
    
    int tab_size() const { return m_tabSize; };
    void tab_size(int nTabSize);
    void view_tabs(bool bViewTabs);
    int max_line_length() const;
    text_location WordToLeft(text_location pt) const;
    text_location WordToRight(text_location pt) const;
    text_location match_brace(const text_location &loc) const;
    text_location auto_indent(const text_location &loc);
    text_location auto_indent(const text_location &loc, int can_match);

    text_location indent(int y, int size);
    int indent(int y) const;
    
    
    DWORD highlight_cookie(int y) const;
    DWORD highlight_line(DWORD dwCookie, const document_line &line, IHighlight::TEXTBLOCK *pBuf, int &nActualItems) const;

    bool view_tabs() const;
    bool HighlightText(const text_location &ptStartPos, int nLength);

    int calc_offset(int y, int nCharIndex) const;
    int calc_offset_approx(int y, int nOffset) const;
    int expanded_line_length(int y) const;


    std::wstring expanded_chars(const std::wstring &text, int nOffset, int nCount) const;

    const text_location &cursor_pos() const { return m_ptCursorPos; };
    const text_location &anchor_pos() const { return m_ptAnchor; };

    void anchor_pos(const text_location &ptNewAnchor);
    void cursor_pos(const text_location &ptCursorPos);

    void update_max_line_length(int y) const;

    void move_to(text_location pos, bool selecting)
    {
        auto limit = _lines[pos.y].size();

        if (pos.x > limit)
        {
            pos.x = limit;
        }

        if (!selecting)
        {
            m_ptAnchor = m_ptCursorPos;
        }

        select(text_selection(m_ptAnchor, m_ptCursorPos));
    }

    text_selection word_selection(const text_location &pos, bool from_anchor) const
    {
        auto ptStart = from_anchor ? m_ptAnchor : pos;
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
        if (m_ptCursorPos < m_ptAnchor)
        {
            return text_selection(WordToLeft(m_ptCursorPos), WordToRight(m_ptAnchor));
        }
        else
        {
            return text_selection(WordToLeft(m_ptAnchor), WordToRight(m_ptCursorPos));
        }
    }

    text_selection line_selection(int first_line, int last_line) const
    {
        auto limit = _lines.size() - 1;

        first_line = Clamp(first_line, 0, limit);
        last_line = Clamp(last_line, 0, limit);

        return text_selection(0, Clamp(first_line, 0, limit), _lines[last_line].size(), last_line);
    }

    text_selection line_selection(const text_location &pos, bool from_anchor) const
    {
        return line_selection(from_anchor ? m_ptAnchor.y : pos.y, pos.y);
    }

    text_selection pos_selection(const text_location &pos, bool from_anchor) const
    {
        return text_selection(from_anchor ? m_ptAnchor : pos, pos);
    }

    void select(const text_selection &selection)
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
};


class undo_group
{
    document &_doc;
    document::undo_item _undo;

public:

    undo_group(document &d) : _doc(d)
    {
    }

    ~undo_group()
    {
        _doc.record_undo(_undo);
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

