#pragma once


inline std::string ToUtf8(const std::wstring &wstr)
{
	int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int) wstr.size(), NULL, 0, NULL, NULL);
	std::string result(size_needed, 0);
	WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int) wstr.size(), &result[0], size_needed, NULL, NULL);
	return result;
}

inline std::wstring ToUtf16(const std::string &str)
{
	int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int) str.size(), NULL, 0);
	std::wstring result(size_needed, 0);
	MultiByteToWideChar(CP_UTF8, 0, &str[0], (int) str.size(), &result[0], size_needed);
	return result;
}

inline int Clamp(int v, int l, int r)
{
	if (v < l) return l;
	if (v > r) return r;
	return v;
}

inline std::wstring UnQuote(const std::wstring &text)
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

static inline std::wstring Combine(const std::vector<std::wstring> &lines, const wchar_t *crlf = L"\n")
{
	std::wstring result;
	auto first = true;
	for (const auto &line : lines)
	{
		if (first)
		{
			result = line;
			first = false;
		}
		else
		{
			result += crlf;
			result += line;

		}
	}

	return result;
}

class CPoint : public POINT
{
public:
	CPoint(int xx = 0, int yy = 0) { x = xx; y = yy; }

	bool operator==(const CPoint &other) const { return x == other.x && y == other.y; }
	bool operator!=(const CPoint &other) const { return x != other.x && y != other.y; }
};

class CSize : public SIZE
{
public:
	CSize(int xx = 0, int yy = 0) { cx = xx; cy = yy; }

	bool operator==(const CSize &other) const { return cx == other.cx && cy == other.cy; }
	bool operator!=(const CSize &other) const { return cx != other.cx && cy != other.cy; }
};


class CRect : public RECT
{
public:
	CRect(int l = 0, int t = 0, int r = 0, int b = 0) { left = l; top = t; right = r; bottom = b; }

	int Width() const { return right - left; };
	int Height() const { return bottom - top; };

	operator LPRECT() { return this; }
	operator LPCRECT() const { return this; }

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
	static int CompareNoCase(const char * left, const char * right)
	{
		return _stricmp(left, right);
	}

	static std::string Format(const char *format, ...)
	{
		va_list argList;
		va_start(argList, format);

		auto length = _vscprintf(format, argList);
		auto sz = (char*) _alloca(length + 1);
		if (sz == nullptr) return "";
		vsprintf_s(sz, length + 1, format, argList);
		va_end(argList);
		sz[length] = 0;
		return sz;
	}

	static const char *From(bool val) { return val ? "true" : "false"; };
};