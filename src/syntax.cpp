#include "pch.h"
#include "document.h"

struct iless
{
	bool operator()(std::wstring_view lhs, std::wstring_view rhs) const
	{
		return str::icmp(lhs, rhs) < 0;
	}
};

static bool is_keyword(std::wstring_view text)
{
	static std::set<std::wstring_view, iless> keywords =
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
		L"xor_eq"
	};

	return keywords.contains(text);
}

static bool is_number(std::wstring_view text)
{
	const auto len = text.size();

	if (len > 2 && text[0] == '0' && text[1] == 'x')
	{
		for (auto i = 2; i < len; i++)
		{
			if (iswdigit(text[i]) || (text[i] >= 'A' && text[i] <= 'F') ||
				(text[i] >= 'a' && text[i] <= 'f'))
				continue;
			return false;
		}
		return true;
	}
	if (!iswdigit(text[0]))
		return false;
	for (auto i = 1; i < len; i++)
	{
		if (!iswdigit(text[i]) && text[i] != '+' &&
			text[i] != '-' && text[i] != '.' && text[i] != 'e' &&
			text[i] != 'E')
			return false;
	}
	return true;
}

static const int COOKIE_COMMENT = 0x0001;
static const int COOKIE_PREPROCESSOR = 0x0002;
static const int COOKIE_EXT_COMMENT = 0x0004;
static const int COOKIE_STRING = 0x0008;
static const int COOKIE_CHAR = 0x0010;

static void add_block(highlighter::text_block* pBuf, int& nActualItems, int pos, style colorindex)
{
	if (pBuf != nullptr)
	{
		if (nActualItems == 0 || pBuf[nActualItems - 1]._char_pos <= pos)
		{
			pBuf[nActualItems]._char_pos = (pos);
			pBuf[nActualItems]._color = (colorindex);
			nActualItems++;
		}
	}
}

uint32_t cpp_highlight::parse_line(uint32_t dwCookie, const document_line& line, text_block* pBuf,
	int& nActualItems) const
{
	if (line.empty())
	{
		return dwCookie & COOKIE_EXT_COMMENT;
	}

	const auto line_view = line.view();
	const auto len = line_view.size();
	auto bFirstChar = (dwCookie & ~COOKIE_EXT_COMMENT) == 0;
	auto bRedefineBlock = true;
	auto bDecIndex = false;
	auto block_start = -1;
	auto i = 0;

	for (i = 0;; i++)
	{
		if (bRedefineBlock)
		{
			auto nPos = i;
			if (bDecIndex)
				nPos--;

			if (dwCookie & (COOKIE_COMMENT | COOKIE_EXT_COMMENT))
			{
				add_block(pBuf, nActualItems, nPos, style::code_comment);
			}
			else if (dwCookie & (COOKIE_CHAR | COOKIE_STRING))
			{
				add_block(pBuf, nActualItems, nPos, style::code_string);
			}
			else if (dwCookie & COOKIE_PREPROCESSOR)
			{
				add_block(pBuf, nActualItems, nPos, style::code_preprocessor);
			}
			else
			{
				add_block(pBuf, nActualItems, nPos, style::normal_text);
			}

			bRedefineBlock = false;
			bDecIndex = false;
		}

		if (i == len)
			break;

		if (dwCookie & COOKIE_COMMENT)
		{
			add_block(pBuf, nActualItems, i, style::code_comment);
			dwCookie |= COOKIE_COMMENT;
			break;
		}

		const auto c = line[i];

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
			add_block(pBuf, nActualItems, i - 1, style::code_comment);
			dwCookie |= COOKIE_COMMENT;
			break;
		}

		//	Preprocessor directive #....
		if (dwCookie & COOKIE_PREPROCESSOR)
		{
			if (i > 0 && c == '*' && line[i - 1] == '/')
			{
				add_block(pBuf, nActualItems, i - 1, style::code_comment);
				dwCookie |= COOKIE_EXT_COMMENT;
			}
			continue;
		}

		//	Normal text
		if (c == '"')
		{
			add_block(pBuf, nActualItems, i, style::code_string);
			dwCookie |= COOKIE_STRING;
			continue;
		}

		if (c == '\'')
		{
			add_block(pBuf, nActualItems, i, style::code_string);
			dwCookie |= COOKIE_CHAR;
			continue;
		}

		if (i > 0 && c == '*' && line[i - 1] == '/')
		{
			add_block(pBuf, nActualItems, i - 1, style::code_comment);
			dwCookie |= COOKIE_EXT_COMMENT;
			continue;
		}

		if (bFirstChar)
		{
			if (c == '#')
			{
				add_block(pBuf, nActualItems, i, style::code_preprocessor);
				dwCookie |= COOKIE_PREPROCESSOR;
				continue;
			}
			if (!iswspace(c))
				bFirstChar = false;
		}

		if (pBuf == nullptr)
			continue; //	We don't need to extract keywords,
		//	for faster parsing skip the rest of loop

		if (iswalnum(c) || c == '_' || c == '.')
		{
			if (block_start == -1)
				block_start = i;
		}
		else
		{
			if (block_start >= 0)
			{
				const auto block_len = i - block_start;

				if (is_keyword(line_view.substr(block_start, block_len)))
				{
					add_block(pBuf, nActualItems, block_start, style::code_keyword);
				}
				else if (is_number(line_view.substr(block_start, block_len)))
				{
					add_block(pBuf, nActualItems, block_start, style::code_number);
				}

				bRedefineBlock = true;
				bDecIndex = true;
				block_start = -1;
			}
		}
	}

	if (block_start >= 0)
	{
		const auto block_len = i - block_start;

		if (is_keyword(line_view.substr(block_start, block_len)))
		{
			add_block(pBuf, nActualItems, block_start, style::code_keyword);
		}
		else if (is_number(line_view.substr(block_start, block_len)))
		{
			add_block(pBuf, nActualItems, block_start, style::code_number);
		}
	}

	if (line[len - 1] != '\\')
		dwCookie &= COOKIE_EXT_COMMENT;
	return dwCookie;
}

uint32_t text_highight::parse_line(uint32_t dwCookie, const document_line& line, text_block* pBuf,
	int& nActualItems) const
{
	if (pBuf)
	{
		const auto line_view = line.view();
		const auto len = line_view.size();
		auto block_start = -1;

		for (auto i = 0; i <= len; i++)
		{
			if (i < len && iswalnum(line[i]))
			{
				if (block_start == -1)
					block_start = i;
			}
			else
			{
				if (block_start >= 0)
				{
					const auto block_len = i - block_start;

					if (is_number(line_view.substr(block_start, block_len)))
					{
						add_block(pBuf, nActualItems, block_start, style::code_number);
					}
					else if (!_check.is_word_valid(line_view.substr(block_start, block_len)))
					{
						add_block(pBuf, nActualItems, block_start, style::error_text);
					}
					else
					{
						add_block(pBuf, nActualItems, block_start, style::normal_text);
					}
				}

				block_start = -1;
			}
		}
	}

	return 0;
}

std::vector<std::wstring> text_highight::suggest(std::wstring_view wword) const
{
	return _check.suggest(wword);
}
