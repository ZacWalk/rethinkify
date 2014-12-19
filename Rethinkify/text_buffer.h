#pragma once

#include "Util.h"

class undo_group;
class text_view;

typedef int POSITION;

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
};

class text_buffer
{

public:

    struct Line
    {
        std::wstring _text;
        DWORD _flags = 0;
        int _y = 0;
        int _cy = 0;

        Line() { };
        Line(const std::wstring &line) : _flags(0), _text(line) { };
        Line(const Line &other) : _flags(other._flags), _text(other._text) {};

        const Line& operator=(const Line &other)
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

        text_location undo(text_buffer &buffer)
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

        text_location redo(text_buffer &buffer)
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

        text_location undo(text_buffer &buffer)
        {
            text_location location;

            for (auto i = _steps.rbegin(); i != _steps.rend(); i++)
            {
                location = i->undo(buffer);
            }

            return location;
        }

        text_location redo(text_buffer &buffer)
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

    std::vector <Line> _lines;

    std::vector<undo_item> _undo;
    size_t m_nUndoPosition;

    std::vector <text_view*> _views;

public:

    text_buffer(const std::wstring &text = std::wstring(), int nCrlfStyle = CRLF_STYLE_DOS);
    ~text_buffer();

    bool LoadFromFile(const std::wstring &path);
    bool SaveToFile(const std::wstring &path, int nCrlfStyle = CRLF_STYLE_AUTOMATIC, bool bClearModifiedFlag = true) const;
    void clear();

    bool IsModified() const { return _modified; }

    void AddView(text_view *pView);
    void RemoveView(text_view *pView);

    const bool empty() const { return _lines.empty(); };
    const size_t size() const { return _lines.size(); };
    const Line &operator[](int n) const { return _lines[n]; };
    Line &operator[](int n) { return _lines[n]; };

    std::vector<std::wstring> text(const text_selection &selection) const;
    std::vector<std::wstring> text() const;
    std::wstring str() const { return Combine(text()); }

    void AppendLine(const std::wstring &text);

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

    void invalidate_line(int index);
    void invalidate_view();

    friend class undo_group;
};

class undo_group
{
    text_buffer &_buffer;
    text_buffer::undo_item _undo;

public:

    undo_group(text_buffer &buffer) : _buffer(buffer)
    {
    }

    ~undo_group()
    {
        _buffer._undo.push_back(_undo);
        _buffer.m_nUndoPosition = _buffer._undo.size();
    }

    void insert(const text_location &location, const wchar_t &c)
    {
        _undo._steps.push_back(text_buffer::undo_step(location, c, true));
    }

    void insert(const text_selection &selection, const std::wstring &text)
    {
        _undo._steps.push_back(text_buffer::undo_step(selection, text, true));
    }

    void erase(const text_location &location, const wchar_t &c)
    {
        _undo._steps.push_back(text_buffer::undo_step(location, c, false));
    }

    void erase(const text_selection &selection, const std::wstring &text)
    {
        _undo._steps.push_back(text_buffer::undo_step(selection, text, false));
    }
};

