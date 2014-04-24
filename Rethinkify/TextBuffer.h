#pragma once

#include "Util.h"

class UndoGroup;

typedef int POSITION;

enum CRLFSTYLE
{
	CRLF_STYLE_AUTOMATIC = -1,
	CRLF_STYLE_DOS = 0,
	CRLF_STYLE_UNIX = 1,
	CRLF_STYLE_MAC = 2
};

enum
{
	FIND_MATCH_CASE = 0x0001,
	FIND_WHOLE_WORD = 0x0002,
	FIND_DIRECTION_UP = 0x0010,
	REPLACE_SELECTION = 0x0100
};


struct IView
{
	virtual void InvalidateLine(int index) = 0;
	virtual void InvalidateView() = 0;
};

class TextBuffer
{

private:

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

	struct UndoStep
	{
	public:

		CPoint _location;
		bool _insert;
		wchar_t _c;

	public:

		UndoStep() : _c(0), _insert(false){};
		UndoStep(const CPoint &location, const wchar_t &c, bool insert) : _c(c), _location(location), _insert(insert){};
		UndoStep(const UndoStep &other) : _c(other._c), _location(other._location), _insert(other._insert) {};

		const UndoStep& operator=(const UndoStep &other)
		{
			_c = other._c;
			_location = other._location;
			_insert = other._insert;
			return *this;
		};

		CPoint Undo(TextBuffer &buffer)
		{
			if (_insert)
			{
				if (_c == '\n')
				{
					return buffer.DeleteText(CPoint(0, _location.y + 1));
				}
				else
				{
					return buffer.DeleteText(_location);
				}
			}
			else 
			{
				return buffer.InsertText(_location, _c);
			}
		}
	};

	struct UndoItem
	{
	public:

		CPoint _before;
		CPoint _after;
		std::vector<UndoStep> _steps;

	public:

		UndoItem()  {};
		UndoItem(const CPoint &before, const CPoint &after) : _before(before), _after(after) {};
		UndoItem(const UndoItem &other) : _steps(other._steps), _before(other._before), _after(other._after) {};

		const UndoItem& operator=(const UndoItem &other)
		{
			_after = other._after;
			_before = other._before;
			_steps = other._steps;
			return *this;
		};

		CPoint Undo(TextBuffer &buffer)
		{
			for (auto i = _steps.rbegin(); i != _steps.rend(); i++)
			{
				i->Undo(buffer);
			}

			return _before;
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

	void AppendLine(const std::string &text);
	void AppendLine(const std::wstring &text);

	std::string str() const
	{
		bool first = true;
		std::string result;

		for (const auto &line : _lines)
		{
			if (!first) result += '\n';
			result.append(ToUtf8(line._text));
			first = false;
		}		

		return result;
	}


	TextBuffer(const std::string &text = std::string(), int nCrlfStyle = CRLF_STYLE_DOS);
	~TextBuffer();

	bool LoadFromFile(const std::wstring &path, int nCrlfStyle = CRLF_STYLE_AUTOMATIC);
	bool SaveToFile(const std::wstring &path, int nCrlfStyle = CRLF_STYLE_AUTOMATIC, bool bClearModifiedFlag = TRUE) const;
	void clear();

	bool IsModified() const { return _modified; }

	void AddView(IView *pView);
	void RemoveView(IView *pView);

	const size_t LineCount() const { return _lines.size(); };
	const Line &operator[](int n) const { return _lines[n];  };
	std::vector<std::wstring> Text(const CPoint &locationStart, const CPoint &locationEnd);

	CPoint InsertText(UndoGroup &ug, const CPoint &location, const std::wstring &text);
	CPoint InsertText(UndoGroup &ug, const CPoint &location, const wchar_t &c);
	void DeleteText(UndoGroup &ug, const CPoint &locationStart, const CPoint &locationEnd);
	CPoint DeleteText(UndoGroup &ug, const CPoint &location);

	CPoint InsertText(const CPoint &location, const std::wstring &text);
	CPoint InsertText(const CPoint &location, const wchar_t &c);
	void DeleteText(const CPoint &locationStart, const CPoint &locationEnd);
	CPoint DeleteText(const CPoint &location);

	bool CanUndo();
	bool CanRedo();
	CPoint Undo();
	CPoint Redo();

	bool FindText(const std::wstring &text, const CPoint &ptStartPos, DWORD dwFlags, bool bWrapSearch, CPoint *pptFoundPos);
	bool FindTextInBlock(const std::wstring &text, const CPoint &ptStartPos, const CPoint &ptBlockBegin, const CPoint &ptBlockEnd, DWORD dwFlags, bool bWrapSearch, CPoint *pptFoundPos);

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

	void Insert(const CPoint &location, const wchar_t &c)
	{
		_undo._steps.push_back(TextBuffer::UndoStep(location, c, true));
	}

	void Delete(const CPoint &location, const wchar_t &c)
	{
		_undo._steps.push_back(TextBuffer::UndoStep(location, c, false));
	}
};

