#pragma once


inline std::string UTF16ToUtf8(const std::wstring& wstr)
{
	auto size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], static_cast<int>(wstr.size()), nullptr, 0, nullptr, nullptr);
	std::string result(size_needed, 0);
	WideCharToMultiByte(CP_UTF8, 0, &wstr[0], static_cast<int>(wstr.size()), &result[0], size_needed, nullptr, nullptr);
	return result;
}

inline std::wstring UTF8ToUtf16(const std::string& str)
{
	int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], static_cast<int>(str.size()), nullptr, 0);
	std::wstring result(size_needed, 0);
	MultiByteToWideChar(CP_UTF8, 0, &str[0], static_cast<int>(str.size()), &result[0], size_needed);
	return result;
}


inline std::string UTF16ToAscii(const std::wstring& wstr)
{
	auto size_needed = WideCharToMultiByte(CP_ACP, 0, &wstr[0], static_cast<int>(wstr.size()), nullptr, 0, nullptr, nullptr);
	std::string result(size_needed, 0);
	WideCharToMultiByte(CP_ACP, 0, &wstr[0], static_cast<int>(wstr.size()), &result[0], size_needed, nullptr, nullptr);
	return result;
}

inline std::string UTF16ToAscii(const wchar_t* wword, int wlen)
{
	auto size_needed = WideCharToMultiByte(CP_ACP, 0, wword, wlen, nullptr, 0, nullptr, nullptr);
	std::string result(size_needed, 0);
	WideCharToMultiByte(CP_ACP, 0, wword, wlen, &result[0], size_needed, nullptr, nullptr);
	return result;
}

inline std::wstring AsciiToUtf16(const std::string& str)
{
	auto size_needed = MultiByteToWideChar(CP_ACP, 0, &str[0], static_cast<int>(str.size()), nullptr, 0);
	std::wstring result(size_needed, 0);
	MultiByteToWideChar(CP_ACP, 0, &str[0], static_cast<int>(str.size()), &result[0], size_needed);
	return result;
}

inline int Clamp(int v, int l, int r)
{
	if (v < l) return l;
	if (r < l) return l;
	if (v > r) return r;
	return v;
}

inline std::wstring UnQuote(const std::wstring& text)
{
	if (text.size() > 1 && *text.begin() == '"' && *text.rbegin() == '"')
	{
		return text.substr(1, text.length() - 2);
	}
	else if (text.size() > 1 && *text.begin() == '\'' && *text.rbegin() == '\'')
	{
		return text.substr(1, text.length() - 2);
	}

	return text;
}

static inline wchar_t* wcsistr(wchar_t const* s1, wchar_t const* s2)
{
	auto s = s1;
	auto p = s2;

	do
	{
		if (!*p) return const_cast<wchar_t*>(s1);
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
	}
	while (1);
}

static inline std::wstring Combine(const std::vector<std::wstring>& lines, const wchar_t* endl = L"\n")
{
	if (lines.size() == 1)
	{
		return lines[0];
	}
	else
	{
		std::wstringstream result;
		auto first = true;

		for (const auto& line : lines)
		{
			if (first)
			{
				result << line;
				first = false;
			}
			else
			{
				result << endl << line;
			}
		}

		return result.str();
	}
}

static inline std::wstring Replace(__in const std::wstring& s, __in_z const wchar_t* find, __in_z const wchar_t* replacement)
{
	auto result = s;
	size_t pos = 0;
	auto findLength = wcslen(find);
	auto replacementLength = wcslen(replacement);

	while ((pos = result.find(find, pos)) != std::wstring::npos)
	{
		result.replace(pos, findLength, replacement);
		pos += replacementLength;
	}

	return result;
}

class CPoint : public POINT
{
public:
	CPoint(int xx = 0, int yy = 0)
	{
		x = xx;
		y = yy;
	}

	CPoint(const POINTL& other)
	{
		x = other.x;
		y = other.y;
	}

	bool operator==(const CPoint& other) const
	{
		return x == other.x && y == other.y;
	}

	bool operator!=(const CPoint& other) const
	{
		return x != other.x && y != other.y;
	}
};

class CSize : public SIZE
{
public:
	CSize(int xx = 0, int yy = 0)
	{
		cx = xx;
		cy = yy;
	}

	bool operator==(const CSize& other) const
	{
		return cx == other.cx && cy == other.cy;
	}

	bool operator!=(const CSize& other) const
	{
		return cx != other.cx && cy != other.cy;
	}
};


class CRect : public RECT
{
public:
	CRect(int l = 0, int t = 0, int r = 0, int b = 0)
	{
		left = l;
		top = t;
		right = r;
		bottom = b;
	}

	int Width() const
	{
		return right - left;
	};

	int Height() const
	{
		return bottom - top;
	};

	operator LPRECT()
	{
		return this;
	}

	operator LPCRECT() const
	{
		return this;
	}

	void OffsetRect(int x, int y)
	{
		left += x;
		top += y;
		right += x;
		bottom += y;
	}
};

class String
{
public:
	static int CompareNoCase(const wchar_t* left, const wchar_t* right)
	{
		return _wcsicmp(left, right);
	}

	static std::wstring Format(const wchar_t* format, ...)
	{
		va_list argList;
		va_start(argList, format);

		auto length = _vscwprintf(format, argList);
		auto sz = static_cast<wchar_t*>(_alloca((length + 1) * sizeof(wchar_t)));
		if (sz == nullptr) return L"";
		vswprintf_s(sz, length + 1, format, argList);
		va_end(argList);
		sz[length] = 0;
		return sz;
	}

	static const wchar_t* From(bool val)
	{
		return val ? L"true" : L"false";
	};
};
