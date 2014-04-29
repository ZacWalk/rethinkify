
#include "pch.h"
#include "TextBuffer.h"
#include "utf.h"

#include <fstream>
#include <clocale>
#include <codecvt>

const TCHAR crlf [] = _T("\r\n");
const int UNDO_BUF_SIZE = 1000;

TextBuffer::TextBuffer(const std::wstring &text, int nCrlfStyle)
{
	_modified = false;
	m_bCreateBackupFile = false;
	m_nUndoPosition = 0;
	m_nCRLFMode = nCrlfStyle;

	if (text.empty())
	{
		AppendLine(L"");		
	}
	else
	{
		std::wstringstream lines(text);
		std::wstring line;

		while (std::getline(lines, line))
		{
			AppendLine(line);
		}
	}
}

TextBuffer::~TextBuffer()
{
}

void TextBuffer::AppendLine(const std::wstring &text)
{
	_lines.push_back(Line(text));
}

void TextBuffer::clear()
{
	_lines.clear();
	_undo.clear();
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

bool TextBuffer::LoadFromFile(const std::wstring &path)
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
	//		AppendLine(line);
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

			if (encoding == NCP_UTF8 || encoding == NCP_ASCII)
			{
				std::string line;

				while (readLen > 0)
				{
					int c = buffer[bufferPos];

					if (c != 0x0A && c != 0x0D)
					{
						line += (char) c;
					}

					if (c == 0x0D)
					{
						AppendLine((encoding == NCP_ASCII) ? AsciiToUtf16(line) : UTF8ToUtf16(line));
						line.clear();
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
				}

				AppendLine((encoding == NCP_ASCII) ? AsciiToUtf16(line) : UTF8ToUtf16(line));
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
						AppendLine(line);
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

				AppendLine(line);
			}

			_modified = false;
			m_nUndoPosition = 0;

			success = true;

			InvalidateView();
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

bool TextBuffer::SaveToFile(const std::wstring &path, int nCrlfStyle /*= CRLF_STYLE_AUTOMATIC*/, bool bClearModifiedFlag /*= true*/) const
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

		success = ::MoveFile(tempPath.c_str(), path.c_str()) != 0;

		if (success && bClearModifiedFlag)
		{
			_modified = false;
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


std::vector<std::wstring> TextBuffer::Text(const TextSelection &selection) const
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

std::vector<std::wstring> TextBuffer::Text() const
{
	std::vector<std::wstring> result;

	for (const auto &line : _lines)
	{
		result.push_back(line._text);
	}

	return result;
}

void TextBuffer::AddView(IView *pView)
{
	_views.push_back(pView);
}

void TextBuffer::RemoveView(IView *pView)
{
	auto pos = _views.begin();

	while (pos != _views.end())
	{
		IView *pvw = *pos;
		if (pvw == pView)
		{
			_views.erase(pos);
			return;
		}
		pos++;
	}
	assert(false);
}

void TextBuffer::InvalidateLine(int index)
{
	for (auto v : _views)
	{
		v->InvalidateLine(index);
	}
}

void TextBuffer::InvalidateView()
{
	for (auto v : _views)
	{
		v->InvalidateView();
	}
}


bool TextBuffer::CanUndo() const
{
	assert(m_nUndoPosition >= 0 && m_nUndoPosition <= _undo.size());
	return m_nUndoPosition > 0;
}

bool TextBuffer::CanRedo() const
{
	assert(m_nUndoPosition >= 0 && m_nUndoPosition <= _undo.size());
	return m_nUndoPosition < _undo.size();
}

TextLocation TextBuffer::Undo()
{
	assert(CanUndo());
	m_nUndoPosition--;
	_modified = true;
	return _undo[m_nUndoPosition].Undo(*this);
}

TextLocation TextBuffer::Redo()
{
	assert(CanRedo());	
	_modified = true;
	auto result = _undo[m_nUndoPosition].Redo(*this);
	m_nUndoPosition++;
	return result;
}

TextLocation TextBuffer::InsertText(const TextLocation &location, const std::wstring &text)
{
	TextLocation resultLocation = location;

	for (const auto &c : text)
	{
		resultLocation = InsertText(resultLocation, c);
	}

	_modified = true;

	return resultLocation;
}

TextLocation TextBuffer::InsertText(UndoGroup &ug, const TextLocation &location, const std::wstring &text)
{	
	TextLocation resultLocation = location;

	for (const auto &c : text)
	{
		resultLocation = InsertText(resultLocation, c);
	}

	ug.Insert(TextSelection(location, resultLocation), text);
	_modified = true;

	return resultLocation;
}

TextLocation TextBuffer::InsertText(const TextLocation &location, const wchar_t &c)
{
	TextLocation resultLocation = location;
	auto &li = _lines[location.y];

	if (c == L'\n')
	{
		// Split
		Line newLine(li._text.substr(location.x, li._text.size()));
		_lines.insert(_lines.begin() + location.y + 1, newLine);
		_lines[location.y]._text = _lines[location.y]._text.substr(0, location.x);

		resultLocation.y = location.y + 1;
		resultLocation.x = 0;

		InvalidateView();
	}
	else if (c != '\r')
	{
		auto before = li._text;
		auto after = li._text;

		after.insert(after.begin() + location.x, c);

		li._text = after;

		resultLocation.y = location.y;
		resultLocation.x = location.x + 1;

		InvalidateLine(location.y);
	}

	return resultLocation;
}

TextLocation TextBuffer::InsertText(UndoGroup &ug, const TextLocation &location, const wchar_t &c)
{
	ug.Insert(location, c);
	_modified = true;
	return InsertText(location, c);
}

TextLocation TextBuffer::DeleteText(UndoGroup &ug, const TextLocation &location)
{
	if (location.x == 0)
	{
		if (location.y > 0)
		{
			ug.Delete(TextLocation(_lines[location.y - 1].size(), location.y - 1), location.x > 0 ? _lines[location.y][location.x - 1] : '\n');
		}
	}
	else
	{
		ug.Delete(TextLocation(location.x - 1, location.y), _lines[location.y][location.x - 1]);
	}

	_modified = true;
	return DeleteText(location);
}

TextLocation TextBuffer::DeleteText(const TextSelection &selection)
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

			InvalidateLine(selection._start.y);
		}
		else
		{
			_lines[selection._start.y]._text.erase(_lines[selection._start.y]._text.begin() + selection._start.x, _lines[selection._start.y]._text.end());
			_lines[selection._start.y]._text.append(_lines[selection._end.y]._text.begin() + selection._end.x, _lines[selection._end.y]._text.end());

			if (selection._start.y + 1 < selection._end.y + 1)
			{
				_lines.erase(_lines.begin() + selection._start.y + 1, _lines.begin() + selection._end.y + 1);
			}

			InvalidateView();
		}
	}

	return selection._start;
}

TextLocation TextBuffer::DeleteText(UndoGroup &ug, const TextSelection &selection)
{
	ug.Delete(selection, Combine(Text(selection)));
	_modified = true;
	return DeleteText(selection);
}

TextLocation TextBuffer::DeleteText(const TextLocation &location)
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

			InvalidateView();
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

		InvalidateLine(location.y);
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

bool TextBuffer::FindText(const std::wstring &text, const TextLocation &ptStartPos, DWORD dwFlags, bool bWrapSearch, TextLocation *pptFoundPos)
{
	int nLineCount = _lines.size();

	return FindTextInBlock(text, ptStartPos, TextLocation(0, 0), TextLocation(_lines[nLineCount - 1]._text.size(), nLineCount - 1), dwFlags, bWrapSearch, pptFoundPos);
}

bool TextBuffer::FindTextInBlock(const std::wstring &what, const TextLocation &ptStartPosition, const TextLocation &ptBlockBegin, const TextLocation &ptBlockEnd, DWORD dwFlags, bool bWrapSearch, TextLocation *pptFoundPos)
{
	TextLocation ptCurrentPos = ptStartPosition;

	assert(ptBlockBegin.y < ptBlockEnd.y || ptBlockBegin.y == ptBlockEnd.y && ptBlockBegin.x <= ptBlockEnd.x);

	if (ptBlockBegin == ptBlockEnd)
		return false;

	if (ptCurrentPos.y < ptBlockBegin.y || ptCurrentPos.y == ptBlockBegin.y && ptCurrentPos.x < ptBlockBegin.x)
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
			while (ptCurrentPos.y >= 0)
			{
				const auto &line = _lines[ptCurrentPos.y];
				const auto nLineLength = line._text.size() - ptCurrentPos.x;

				if (nLineLength <= 0)
				{
					ptCurrentPos.x = 0;
					ptCurrentPos.y--;
					continue;
				}

				auto nPos = ::FindStringHelper(line._text.c_str() + ptCurrentPos.x, what.c_str(), wholeWord);

				if (nPos >= 0)		//	Found text!
				{
					ptCurrentPos.x += nPos;
					*pptFoundPos = ptCurrentPos;
					return true;
				}

				ptCurrentPos.x = 0;
				ptCurrentPos.y--;
			}

			//	Beginning of text reached
			if (!bWrapSearch)
				return false;

			//	Start again from the end of text
			bWrapSearch = false;
			ptCurrentPos = TextLocation(0, _lines.size() - 1);
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

				if (nLineLength <= 0)
				{
					ptCurrentPos.x = 0;
					ptCurrentPos.y++;
					continue;
				}

				//	Perform search in the line
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