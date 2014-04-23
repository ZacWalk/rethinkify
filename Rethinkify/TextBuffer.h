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

	struct UndoLine
	{
	public:

		int _pos;
		std::wstring _before;
		std::wstring _after;		

	public:

		UndoLine() : _pos(0) {};
		UndoLine(int pos, const std::wstring &before, const std::wstring &after) : _pos(pos), _before(before), _after(after) {};
		UndoLine(const UndoLine &other) : _pos(other._pos), _before(other._before), _after(other._after) {};

		const UndoLine& operator=(const UndoLine &other)
		{
			_after = other._after;
			_before = other._before;
			_pos = other._pos;
			return *this;
		};

	};

	struct UndoItem
	{
	public:

		CPoint _before;
		CPoint _after;
		std::vector<UndoLine> _lines;

	public:

		UndoItem()  {};
		UndoItem(const CPoint &before, const CPoint &after) : _before(before), _after(after) {};
		UndoItem(const UndoItem &other) : _lines(other._lines), _before(other._before), _after(other._after) {};

		const UndoItem& operator=(const UndoItem &other)
		{
			_after = other._after;
			_before = other._before;
			_lines = other._lines;
			return *this;
		};

	};

	mutable bool _modified;
	int m_nCRLFMode;
	bool m_bCreateBackupFile;
	int m_nUndoBufSize;

	std::vector <Line> _lines;

	std::vector<UndoItem> _undo;
	size_t m_nUndoPosition;
	
	std::vector <IView*> _views;

public:

	void AppendLine(const std::string &text);
	void AppendLine(const std::wstring &text);


	TextBuffer(int nCrlfStyle = CRLF_STYLE_DOS);
	~TextBuffer();

	bool LoadFromFile(LPCTSTR pszFileName, int nCrlfStyle = CRLF_STYLE_AUTOMATIC);
	bool SaveToFile(LPCTSTR pszFileName, int nCrlfStyle = CRLF_STYLE_AUTOMATIC, bool bClearModifiedFlag = TRUE) const;
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

	void LineChanged(int index, const std::wstring &before, const std::wstring &after)
	{
		_undo._lines.push_back(TextBuffer::UndoLine(index, before, after));
	}

	/*void AddUndoRecord(bool bInsert, const CPoint &ptStartPos, const CPoint &ptEndPos, const std::wstring &text, int nActionType = CE_ACTION_UNKNOWN)
	{
		TextBuffer::UndoItem u(text);
		u._flags = nActionType;
		u._action = nActionType;
		u.m_ptStartPos = ptStartPos;
		u.m_ptEndPos = ptEndPos;

		_undo.push_back(u);
	}*/
};

