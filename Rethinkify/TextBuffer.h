#pragma once

#include "Util.h"

class UndoGroup;

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

class TextLocation
{
public:
	int x;
	int y;

	TextLocation(int xx = 0, int yy = 0) { x = xx; y = yy; }

	bool operator==(const TextLocation &other) const { return x == other.x && y == other.y; }
	bool operator!=(const TextLocation &other) const { return x != other.x && y != other.y; }
};

class TextSelection
{
public:
	TextLocation _start;
	TextLocation _end;

	TextSelection() { }
	TextSelection(const TextLocation &start, const TextLocation &end) { _start = start; _end = end; }
	TextSelection(int x1, int y1, int x2, int y2) : _start(x1, y1), _end(x2, y2) { }

	bool operator==(const TextSelection &other) const { return _start == other._start && _end == other._end; }
	bool operator!=(const TextSelection &other) const { return _start != other._start && _end != other._end; }

	bool empty() const { return _start == _end; };
};



struct IView
{
	virtual void InvalidateLine(int index) = 0;
	virtual void InvalidateView() = 0;
};

class TextBuffer
{

public:

	struct Line
	{
		std::wstring _text;
		DWORD	_flags;		

		Line() : _flags(0) { };
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

	struct UndoStep
	{
	public:

		TextSelection _selection;
		bool _insert;
		std::wstring _text;

	public:

		UndoStep() : _insert(false){};
		UndoStep(const TextLocation &location, const wchar_t &c, bool insert) : _text(1, c), _selection(location, location), _insert(insert){};
		UndoStep(const TextLocation &location, const std::wstring &text, bool insert) : _text(text), _selection(location, location), _insert(insert){};
		UndoStep(const TextSelection &selection, const std::wstring &text, bool insert) : _text(text), _selection(selection), _insert(insert){};
		UndoStep(const UndoStep &other) : _text(other._text), _selection(other._selection), _insert(other._insert) {};

		const UndoStep& operator=(const UndoStep &other)
		{
			_text = other._text;
			_selection = other._selection;
			_insert = other._insert;
			return *this;
		};

		TextLocation Undo(TextBuffer &buffer)
		{
			if (_insert)
			{
				if (_text == L"\n")
				{
					return buffer.DeleteText(TextLocation(0, _selection._start.y + 1));
				}
				else if (_text.size() == 1)
				{
					return buffer.DeleteText(TextLocation(_selection._start.x + 1, _selection._start.y));
				}
				else 
				{
					return buffer.DeleteText(_selection);
				}
			}
			else 
			{
				return buffer.InsertText(_selection._start, _text);
			}
		}

		TextLocation Redo(TextBuffer &buffer)
		{
			if (_insert)
			{
				return buffer.InsertText(_selection._start, _text);
			}
			else
			{
				if (_text == L"\n")
				{
					return buffer.DeleteText(TextLocation(0, _selection._start.y + 1));
				}
				else if (_text.size() == 1)
				{
					return buffer.DeleteText(TextLocation(_selection._start.x + 1, _selection._start.y));
				}
				else
				{
					return buffer.DeleteText(_selection);
				}
			}
		}
	};

	struct UndoItem
	{
	public:

		std::vector<UndoStep> _steps;

	public:

		UndoItem()  {};
		UndoItem(const UndoItem &other) : _steps(other._steps) {};

		const UndoItem& operator=(const UndoItem &other)
		{
			_steps = other._steps;
			return *this;
		};

		TextLocation Undo(TextBuffer &buffer)
		{
			TextLocation location;

			for (auto i = _steps.rbegin(); i != _steps.rend(); i++)
			{
				location = i->Undo(buffer);
			}

			return location;
		}

		TextLocation Redo(TextBuffer &buffer)
		{
			TextLocation location;

			for (auto i = _steps.begin(); i != _steps.end(); i++)
			{
				location = i->Redo(buffer);
			}

			return location;
		}

	};

	mutable bool _modified;
	int m_nCRLFMode;
	bool m_bCreateBackupFile;

	std::vector <Line> _lines;

	std::vector<UndoItem> _undo;
	size_t m_nUndoPosition;
	
	std::vector <IView*> _views;

public:

	void AppendLine(const std::wstring &text);

	TextBuffer(const std::wstring &text = std::wstring(), int nCrlfStyle = CRLF_STYLE_DOS);
	~TextBuffer();

	bool LoadFromFile(const std::wstring &path);
	bool SaveToFile(const std::wstring &path, int nCrlfStyle = CRLF_STYLE_AUTOMATIC, bool bClearModifiedFlag = true) const;
	void clear();

	bool IsModified() const { return _modified; }

	void AddView(IView *pView);
	void RemoveView(IView *pView);

	const size_t LineCount() const { return _lines.size(); };
	const Line &operator[](int n) const { return _lines[n];  };

	std::vector<std::wstring> Text(const TextSelection &selection) const;
	std::vector<std::wstring> Text() const;
	std::wstring str() const { return Combine(Text()); }

	TextLocation InsertText(UndoGroup &ug, const TextLocation &location, const std::wstring &text);
	TextLocation InsertText(UndoGroup &ug, const TextLocation &location, const wchar_t &c);
	TextLocation DeleteText(UndoGroup &ug, const TextSelection &selection);
	TextLocation DeleteText(UndoGroup &ug, const TextLocation &location);

	TextLocation InsertText(const TextLocation &location, const std::wstring &text);
	TextLocation InsertText(const TextLocation &location, const wchar_t &c);
	TextLocation DeleteText(const TextSelection &selection);
	TextLocation DeleteText(const TextLocation &location);

	bool CanUndo() const;
	bool CanRedo() const;
	TextLocation Undo();
	TextLocation Redo();

	bool Find(const std::wstring &text, const TextLocation &ptStartPos, DWORD dwFlags, bool bWrapSearch, TextLocation *pptFoundPos);
	bool FindInBlock(const std::wstring &text, const TextLocation &ptStartPos, const TextLocation &ptBlockBegin, const TextLocation &ptBlockEnd, DWORD dwFlags, bool bWrapSearch, TextLocation *pptFoundPos);

	void InvalidateLine(int index);
	void InvalidateView();

	friend class UndoGroup;
};

class UndoGroup
{
	TextBuffer &_buffer;
	TextBuffer::UndoItem _undo;

public:

	UndoGroup(TextBuffer &buffer) : _buffer(buffer)
	{
	}

	~UndoGroup()
	{
		_buffer._undo.push_back(_undo);
		_buffer.m_nUndoPosition = _buffer._undo.size();
	}

	void Insert(const TextLocation &location, const wchar_t &c)
	{
		_undo._steps.push_back(TextBuffer::UndoStep(location, c, true));
	}

	void Insert(const TextSelection &selection, const std::wstring &text)
	{
		_undo._steps.push_back(TextBuffer::UndoStep(selection, text, true));
	}

	void Delete(const TextLocation &location, const wchar_t &c)
	{
		_undo._steps.push_back(TextBuffer::UndoStep(location, c, false));
	}

	void Delete(const TextSelection &selection, const std::wstring &text)
	{
		_undo._steps.push_back(TextBuffer::UndoStep(selection, text, false));
	}
};

