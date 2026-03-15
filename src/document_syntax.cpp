// document_syntax.cpp — Syntax highlighting parsers for C++, plain text, hex, and markdown

#include "pch.h"
#include "document.h"

static bool is_keyword(const std::wstring_view text)
{
	static std::unordered_set<std::wstring_view, ihash, ieq> keywords =
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
		L"uint32_t",
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

static bool is_number(const std::wstring_view text)
{
	const auto len = static_cast<int>(text.size());
	if (len == 0) return false;

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

	int pos = 1;
	while (pos < len && iswdigit(text[pos])) pos++;

	if (pos < len && text[pos] == '.')
	{
		pos++;
		if (pos >= len || !iswdigit(text[pos])) return false;
		while (pos < len && iswdigit(text[pos])) pos++;
	}

	if (pos < len && (text[pos] == 'e' || text[pos] == 'E'))
	{
		pos++;
		if (pos < len && (text[pos] == '+' || text[pos] == '-')) pos++;
		if (pos >= len || !iswdigit(text[pos])) return false;
		while (pos < len && iswdigit(text[pos])) pos++;
	}

	return pos == len;
}

static constexpr int COOKIE_COMMENT = 0x0001;
static constexpr int COOKIE_PREPROCESSOR = 0x0002;
static constexpr int COOKIE_EXT_COMMENT = 0x0004;
static constexpr int COOKIE_STRING = 0x0008;
static constexpr int COOKIE_CHAR = 0x0010;

static void add_block(text_block* pBuf, int& nActualItems, const int pos, const style colorindex)
{
	if (pBuf != nullptr)
	{
		if (nActualItems == 0 || pBuf[nActualItems - 1]._char_pos <= pos)
		{
			pBuf[nActualItems]._char_pos = pos;
			pBuf[nActualItems]._color = colorindex;
			nActualItems++;
		}
	}
}

uint32_t highlight_cpp(uint32_t dwCookie, const document_line& line, text_block* pBuf,
                       int& nActualItems)
{
	if (line.empty())
	{
		return dwCookie & COOKIE_EXT_COMMENT;
	}

	const auto line_view = line.view();
	const auto len = static_cast<int>(line_view.size());
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
			if (c == '"')
			{
				int bs = 0;
				for (int j = i - 1; j >= 0 && line[j] == '\\'; j--) bs++;
				if (bs % 2 == 0)
				{
					dwCookie &= ~COOKIE_STRING;
					bRedefineBlock = true;
				}
			}
			continue;
		}

		//	Char constant '..'
		if (dwCookie & COOKIE_CHAR)
		{
			if (c == '\'')
			{
				int bs = 0;
				for (int j = i - 1; j >= 0 && line[j] == '\\'; j--) bs++;
				if (bs % 2 == 0)
				{
					dwCookie &= ~COOKIE_CHAR;
					bRedefineBlock = true;
				}
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

uint32_t highlight_text(uint32_t dwCookie, const document_line& line, text_block* pBuf,
                        int& nActualItems)
{
	if (pBuf)
	{
		const auto line_view = line.view();
		const auto len = static_cast<int>(line_view.size());
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

uint32_t highlight_hex(uint32_t dwCookie, const document_line& line, text_block* pBuf,
                       int& nActualItems)
{
	nActualItems = 0;
	if (line.empty()) return 0;

	const auto len = static_cast<int>(line.size());

	// Offset address (first 8 hex chars + 2 spaces) -> number color
	add_block(pBuf, nActualItems, 0, style::code_number);

	// Hex bytes section -> keyword color
	if (len > 10)
		add_block(pBuf, nActualItems, 10, style::code_keyword);

	// Find the ASCII separator "|"
	const auto bar_pos = line._text.find(L'|', 10);
	if (bar_pos != std::wstring::npos)
	{
		// ASCII section -> string color
		add_block(pBuf, nActualItems, static_cast<int>(bar_pos), style::code_string);
	}

	return 0;
}

static int find_md_marker(const std::wstring_view text, const int start, const wchar_t ch, const int count)
{
	const auto len = static_cast<int>(text.size());
	for (int i = start; i <= len - count; i++)
	{
		bool match = true;
		for (int j = 0; j < count; j++)
		{
			if (text[i + j] != ch)
			{
				match = false;
				break;
			}
		}
		if (match)
			return i;
	}
	return -1;
}

static int find_md_char(const std::wstring_view text, const int start, const wchar_t ch)
{
	const auto len = static_cast<int>(text.size());
	for (int i = start; i < len; i++)
	{
		if (text[i] == ch)
			return i;
	}
	return -1;
}

static void parse_md_inline(const std::wstring_view text, const int start,
                            text_block* pBuf, int& nActualItems)
{
	const auto len = static_cast<int>(text.size());
	int pos = start;

	while (pos < len)
	{
		// Bold: **text** or __text__
		if (pos + 1 < len && ((text[pos] == L'*' && text[pos + 1] == L'*') ||
			(text[pos] == L'_' && text[pos + 1] == L'_')))
		{
			const auto end = find_md_marker(text, pos + 2, text[pos], 2);
			if (end >= 0)
			{
				add_block(pBuf, nActualItems, pos, style::md_marker);
				add_block(pBuf, nActualItems, pos + 2, style::md_bold);
				add_block(pBuf, nActualItems, end, style::md_marker);
				if (end + 2 < len)
					add_block(pBuf, nActualItems, end + 2, style::normal_text);
				pos = end + 2;
				continue;
			}
		}

		// Italic: *text* or _text_
		if (text[pos] == L'*' || text[pos] == L'_')
		{
			const auto end = find_md_marker(text, pos + 1, text[pos], 1);
			if (end >= 0)
			{
				add_block(pBuf, nActualItems, pos, style::md_marker);
				add_block(pBuf, nActualItems, pos + 1, style::md_italic);
				add_block(pBuf, nActualItems, end, style::md_marker);
				if (end + 1 < len)
					add_block(pBuf, nActualItems, end + 1, style::normal_text);
				pos = end + 1;
				continue;
			}
		}

		// Link: [text](url)
		if (text[pos] == L'[')
		{
			const auto close_bracket = find_md_char(text, pos + 1, L']');
			if (close_bracket >= 0 && close_bracket + 1 < len && text[close_bracket + 1] == L'(')
			{
				const auto close_paren = find_md_char(text, close_bracket + 2, L')');
				if (close_paren >= 0)
				{
					add_block(pBuf, nActualItems, pos, style::md_marker);
					add_block(pBuf, nActualItems, pos + 1, style::md_link_text);
					add_block(pBuf, nActualItems, close_bracket, style::md_marker);
					add_block(pBuf, nActualItems, close_bracket + 2, style::md_link_url);
					add_block(pBuf, nActualItems, close_paren, style::md_marker);
					if (close_paren + 1 < len)
						add_block(pBuf, nActualItems, close_paren + 1, style::normal_text);
					pos = close_paren + 1;
					continue;
				}
			}
		}

		pos++;
	}
}

uint32_t highlight_markdown(uint32_t dwCookie, const document_line& line, text_block* pBuf,
                            int& nActualItems)
{
	nActualItems = 0;
	if (line.empty()) return 0;

	const auto text = line.view();
	const auto len = static_cast<int>(text.size());
	int pos = 0;

	// Heading: # ## ###
	if (text[0] == L'#')
	{
		int level = 0;
		while (pos < len && text[pos] == L'#' && level < 3)
		{
			level++;
			pos++;
		}
		if (pos < len && text[pos] == L' ')
		{
			add_block(pBuf, nActualItems, 0, style::md_marker);
			const auto heading_style = static_cast<style>(
				static_cast<int>(style::md_heading1) + level - 1);
			add_block(pBuf, nActualItems, pos + 1, heading_style);
			return 0;
		}
		pos = 0;
	}

	// Unordered list: - or *
	if (len >= 2 && (text[0] == L'-' || text[0] == L'*') && text[1] == L' ')
	{
		add_block(pBuf, nActualItems, 0, style::md_bullet);
		add_block(pBuf, nActualItems, 2, style::normal_text);
		parse_md_inline(text, 2, pBuf, nActualItems);
		return 0;
	}

	// Ordered list: digits followed by ". "
	if (len >= 3 && text[0] >= L'0' && text[0] <= L'9')
	{
		int p = 0;
		while (p < len && text[p] >= L'0' && text[p] <= L'9') p++;
		if (p < len - 1 && text[p] == L'.' && text[p + 1] == L' ')
		{
			add_block(pBuf, nActualItems, 0, style::md_bullet);
			add_block(pBuf, nActualItems, p + 2, style::normal_text);
			parse_md_inline(text, p + 2, pBuf, nActualItems);
			return 0;
		}
	}

	// Normal line with inline formatting
	parse_md_inline(text, 0, pBuf, nActualItems);
	return 0;
}

static bool is_cpp_extension(const std::wstring_view ext)
{
	static const std::set<std::wstring_view, iless> extensions = {
		L"c", L"cpp", L"cxx", L"cc", L"h", L"hh", L"hpp", L"hxx", L"inl"
	};

	return extensions.contains(ext);
}

highlight_fn select_highlighter(const doc_type type, const file_path& path)
{
	switch (type)
	{
	case doc_type::hex:
		return highlight_hex;
	case doc_type::markdown:
		return highlight_markdown;
	default:
		break;
	}

	auto ext = path.extension();
	if (!ext.empty() && ext.starts_with(L'.')) ext = ext.substr(1);

	if (is_cpp_extension(ext))
		return highlight_cpp;
	return highlight_text;
}
