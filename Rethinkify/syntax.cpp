#include "pch.h"
#include "TextView.h"

struct caseInsensitiveCompare : public std::binary_function < const wchar_t *, const wchar_t *, bool > {
	bool operator()(const wchar_t *lhs, const wchar_t *rhs) const {
		return _wcsicmp(lhs, rhs) < 0;
	}
};

static bool IsKeyword(const wchar_t * pszChars, int nLength)
{
	static const wchar_t *raw_keywords [] =
	{
		L"__asm",
		L"__based",
		L"__cdecl",
		L"__declspec",
		L"__except",
		L"__fastcall",
		L"__finally",
		L"__inline",
		L"__int16",
		L"__int32",
		L"__int64",
		L"__int8",
		L"__leave",
		L"__multiple_inheritance",
		L"__single_inheritance",
		L"__stdcall",
		L"__try",
		L"__uuidof",
		L"__virtual_inheritance",
		L"_persistent",
		L"alignas",
		L"alignof",
		L"and",
		L"and_eq",
		L"asm",
		L"auto",
		L"bitand",
		L"bitor",
		L"bool",
		L"break",
		L"case",
		L"catch",
		L"char",
		L"char16_t",
		L"char32_t",
		L"class",
		L"compl",
		L"const",
		L"const_cast",
		L"constexpr",
		L"continue",
		L"cset",
		L"decltype",
		L"default",
		L"delete",
		L"depend",
		L"dllexport",
		L"dllimport",
		L"do",
		L"double",
		L"dynamic_cast",
		L"else",
		L"enum",
		L"explicit",
		L"export",
		L"extern",
		L"false",
		L"float",
		L"for",
		L"friend",
		L"goto",
		L"if",
		L"indexdef",
		L"inline",
		L"int",
		L"interface",
		L"long",
		L"main",
		L"mutable",
		L"naked",
		L"namespace",
		L"new",
		L"noexcept",
		L"not",
		L"not_eq",
		L"nullptr",
		L"ondemand",
		L"operator",
		L"or",
		L"or_eq",
		L"persistent",
		L"private",
		L"protected",
		L"public",
		L"register",
		L"reinterpret_cast",
		L"return",
		L"short",
		L"signed",
		L"sizeof",
		L"static",
		L"static_assert",
		L"static_cast",
		L"struct",
		L"switch",
		L"template",
		L"this",
		L"thread",
		L"thread_local",
		L"throw",
		L"transient",
		L"true",
		L"try",
		L"typedef",
		L"typeid",
		L"typename",
		L"union",
		L"unsigned",
		L"useindex",
		L"using",
		L"uuid",
		L"virtual",
		L"void",
		L"volatile",
		L"wchar_t",
		L"while",
		L"wmain",
		L"xalloc",
		L"xor",
		L"xor_eq",
		nullptr
	};

	static std::set<const wchar_t *, caseInsensitiveCompare> keywords;

	if (keywords.empty())
	{
		for (auto i = raw_keywords; *i != nullptr; i++)
		{
			keywords.insert(*i);
		}
	}

	const int bufferLen = 100;
	wchar_t sz[bufferLen];
	wcsncpy_s(sz, pszChars, min(bufferLen - 1, nLength));

	return keywords.find(sz) != keywords.end();
}

static bool IsNumber(const wchar_t * pszChars, int nLength)
{
	if (nLength > 2 && pszChars[0] == '0' && pszChars[1] == 'x')
	{
		for (int i = 2; i < nLength; i++)
		{
			if (iswdigit(pszChars[i]) || (pszChars[i] >= 'A' && pszChars[i] <= 'F') ||
				(pszChars[i] >= 'a' && pszChars[i] <= 'f'))
				continue;
			return false;
		}
		return true;
	}
	if (!iswdigit(pszChars[0]))
		return false;
	for (int i = 1; i < nLength; i++)
	{
		if (!iswdigit(pszChars[i]) && pszChars[i] != '+' &&
			pszChars[i] != '-' && pszChars[i] != '.' && pszChars[i] != 'e' &&
			pszChars[i] != 'E')
			return false;
	}
	return true;
}

static const int COOKIE_COMMENT = 0x0001;
static const int COOKIE_PREPROCESSOR = 0x0002;
static const int COOKIE_EXT_COMMENT = 0x0004;
static const int COOKIE_STRING = 0x0008;
static const int COOKIE_CHAR = 0x0010;

static void AddBlock(IHighlight::TEXTBLOCK *pBuf, int &nActualItems, int pos, int colorindex)
{
	if (pBuf != nullptr)
	{
		if (nActualItems == 0 || pBuf[nActualItems - 1].m_nCharPos <= (pos))
		{
			pBuf[nActualItems].m_nCharPos = (pos);
			pBuf[nActualItems].m_nColorIndex = (colorindex);
			nActualItems++;
		}
	}
}

DWORD CppSyntax::ParseLine(DWORD dwCookie, const TextBuffer::Line &line, TEXTBLOCK *pBuf, int &nActualItems) const
{
	if (line.empty())
	{
		return dwCookie & COOKIE_EXT_COMMENT;
	}

	auto nLength = line.size();
	auto bFirstChar = (dwCookie & ~COOKIE_EXT_COMMENT) == 0;
	auto bRedefineBlock = true;
	auto bDecIndex = false;
	auto nIdentBegin = -1;
	auto i = 0;

	for (i = 0;; i++)
	{
		if (bRedefineBlock)
		{
			int nPos = i;
			if (bDecIndex)
				nPos--;

			if (dwCookie & (COOKIE_COMMENT | COOKIE_EXT_COMMENT))
			{
				AddBlock(pBuf, nActualItems, nPos, COLORINDEX_COMMENT);
			}
			else if (dwCookie & (COOKIE_CHAR | COOKIE_STRING))
			{
				AddBlock(pBuf, nActualItems, nPos, COLORINDEX_STRING);
			}
			else if (dwCookie & COOKIE_PREPROCESSOR)
			{
				AddBlock(pBuf, nActualItems, nPos, COLORINDEX_PREPROCESSOR);
			}
			else
			{
				AddBlock(pBuf, nActualItems, nPos, COLORINDEX_NORMALTEXT);
			}

			bRedefineBlock = false;
			bDecIndex = false;
		}

		if (i == nLength)
			break;

		if (dwCookie & COOKIE_COMMENT)
		{
			AddBlock(pBuf, nActualItems, i, COLORINDEX_COMMENT);
			dwCookie |= COOKIE_COMMENT;
			break;
		}

		auto c = line[i];

		//	String constant "...."
		if (dwCookie & COOKIE_STRING)
		{
			if (c == '"' && (i == 0 || line[i - 1] != '\\'))
			{
				dwCookie &= ~COOKIE_STRING;
				bRedefineBlock = true;
			}
			continue;
		}

		//	Char constant '..'
		if (dwCookie & COOKIE_CHAR)
		{
			if (c == '\'' && (i == 0 || line[i - 1] != '\\'))
			{
				dwCookie &= ~COOKIE_CHAR;
				bRedefineBlock = true;
			}
			continue;
		}

		//	Extended comment /*....*/
		if (dwCookie & COOKIE_EXT_COMMENT)
		{
			if (i > 0 && c == '/' && line[i - 1] == '*')
			{
				dwCookie &= ~COOKIE_EXT_COMMENT;
				bRedefineBlock = true;
			}
			continue;
		}

		if (i > 0 && c == '/' && line[i - 1] == '/')
		{
			AddBlock(pBuf, nActualItems, i - 1, COLORINDEX_COMMENT);
			dwCookie |= COOKIE_COMMENT;
			break;
		}

		//	Preprocessor directive #....
		if (dwCookie & COOKIE_PREPROCESSOR)
		{
			if (i > 0 && c == '*' && line[i - 1] == '/')
			{
				AddBlock(pBuf, nActualItems, i - 1, COLORINDEX_COMMENT);
				dwCookie |= COOKIE_EXT_COMMENT;
			}
			continue;
		}

		//	Normal text
		if (c == '"')
		{
			AddBlock(pBuf, nActualItems, i, COLORINDEX_STRING);
			dwCookie |= COOKIE_STRING;
			continue;
		}

		if (c == '\'')
		{
			AddBlock(pBuf, nActualItems, i, COLORINDEX_STRING);
			dwCookie |= COOKIE_CHAR;
			continue;
		}

		if (i > 0 && c == '*' && line[i - 1] == '/')
		{
			AddBlock(pBuf, nActualItems, i - 1, COLORINDEX_COMMENT);
			dwCookie |= COOKIE_EXT_COMMENT;
			continue;
		}

		if (bFirstChar)
		{
			if (c == '#')
			{
				AddBlock(pBuf, nActualItems, i, COLORINDEX_PREPROCESSOR);
				dwCookie |= COOKIE_PREPROCESSOR;
				continue;
			}
			if (!iswspace(c))
				bFirstChar = false;
		}

		if (pBuf == nullptr)
			continue;	//	We don't need to extract keywords,
		//	for faster parsing skip the rest of loop

		if (iswalnum(c) || c == '_' || c == '.')
		{
			if (nIdentBegin == -1)
				nIdentBegin = i;
		}
		else
		{
			if (nIdentBegin >= 0)
			{
				auto pszChars = line.c_str();

				if (IsKeyword(pszChars + nIdentBegin, i - nIdentBegin))
				{
					AddBlock(pBuf, nActualItems, nIdentBegin, COLORINDEX_KEYWORD);
				}
				else if (IsNumber(pszChars + nIdentBegin, i - nIdentBegin))
				{
					AddBlock(pBuf, nActualItems, nIdentBegin, COLORINDEX_NUMBER);
				}

				bRedefineBlock = true;
				bDecIndex = true;
				nIdentBegin = -1;
			}
		}
	}

	if (nIdentBegin >= 0)
	{
		auto pszChars = line.c_str();

		if (IsKeyword(pszChars + nIdentBegin, i - nIdentBegin))
		{
			AddBlock(pBuf, nActualItems, nIdentBegin, COLORINDEX_KEYWORD);
		}
		else if (IsNumber(pszChars + nIdentBegin, i - nIdentBegin))
		{
			AddBlock(pBuf, nActualItems, nIdentBegin, COLORINDEX_NUMBER);
		}
	}

	if (line[nLength - 1] != '\\')
		dwCookie &= COOKIE_EXT_COMMENT;
	return dwCookie;
}

DWORD TextHighight::ParseLine(DWORD dwCookie, const TextBuffer::Line &line, TEXTBLOCK *pBuf, int &nActualItems) const
{
	if (line.empty())
	{
		return dwCookie & COOKIE_EXT_COMMENT;
	}

	auto nLength = line.size();
	auto bFirstChar = (dwCookie & ~COOKIE_EXT_COMMENT) == 0;
	auto bRedefineBlock = true;
	auto bDecIndex = false;
	auto nIdentBegin = -1;
	auto i = 0;

	for (i = 0;; i++)
	{
		if (bRedefineBlock)
		{
			int nPos = i;
			if (bDecIndex)
				nPos--;

			if (dwCookie & (COOKIE_COMMENT | COOKIE_EXT_COMMENT))
			{
				AddBlock(pBuf, nActualItems, nPos, COLORINDEX_COMMENT);
			}
			else if (dwCookie & (COOKIE_CHAR | COOKIE_STRING))
			{
				AddBlock(pBuf, nActualItems, nPos, COLORINDEX_STRING);
			}
			else if (dwCookie & COOKIE_PREPROCESSOR)
			{
				AddBlock(pBuf, nActualItems, nPos, COLORINDEX_PREPROCESSOR);
			}
			else
			{
				AddBlock(pBuf, nActualItems, nPos, COLORINDEX_NORMALTEXT);
			}

			bRedefineBlock = false;
			bDecIndex = false;
		}

		if (i == nLength)
			break;

		if (dwCookie & COOKIE_COMMENT)
		{
			AddBlock(pBuf, nActualItems, i, COLORINDEX_COMMENT);
			dwCookie |= COOKIE_COMMENT;
			break;
		}

		auto c = line[i];		

		if (pBuf == nullptr)
			continue;

		//	We don't need to extract words,
		//	for faster parsing skip the rest of loop

		if (iswalnum(c) || c == '_' || c == '\'')
		{
			if (nIdentBegin == -1)
				nIdentBegin = i;
		}
		else
		{
			if (nIdentBegin >= 0)
			{
				auto pszChars = line.c_str();

				if (IsNumber(pszChars + nIdentBegin, i - nIdentBegin))
				{
					AddBlock(pBuf, nActualItems, nIdentBegin, COLORINDEX_NUMBER);
				}
				else if (!_check.WordValid(pszChars + nIdentBegin, i - nIdentBegin))
				{
					AddBlock(pBuf, nActualItems, nIdentBegin, COLORINDEX_ERRORTEXT);
				}

				bRedefineBlock = true;
				bDecIndex = true;
				nIdentBegin = -1;
			}
		}
	}

	if (nIdentBegin >= 0)
	{
		auto pszChars = line.c_str();

		if (IsNumber(pszChars + nIdentBegin, i - nIdentBegin))
		{
			AddBlock(pBuf, nActualItems, nIdentBegin, COLORINDEX_NUMBER);
		}
		else if (!_check.WordValid(pszChars + nIdentBegin, i - nIdentBegin))
		{
			AddBlock(pBuf, nActualItems, nIdentBegin, COLORINDEX_ERRORTEXT);
		}
	}

	return dwCookie;
}

std::vector<std::wstring> TextHighight::Suggest(const std::wstring &wword) const
{
	return _check.Suggest(wword);
}