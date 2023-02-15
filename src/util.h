#pragma once


inline std::string UTF16ToUtf8(std::wstring_view wstr)
{
	const auto size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()), nullptr, 0,
		nullptr, nullptr);
	std::string result(size_needed, 0);
	WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()), result.data(), size_needed, nullptr, nullptr);
	return result;
}

inline std::wstring UTF8ToUtf16(std::string_view str)
{
	const int size_needed = MultiByteToWideChar(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), nullptr, 0);
	std::wstring result(size_needed, 0);
	MultiByteToWideChar(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), result.data(), size_needed);
	return result;
}

inline std::string UTF16ToAscii(std::wstring_view wstr)
{
	const auto size_needed = WideCharToMultiByte(CP_ACP, 0, wstr.data(), static_cast<int>(wstr.size()), nullptr, 0,
		nullptr, nullptr);
	std::string result(size_needed, 0);
	WideCharToMultiByte(CP_ACP, 0, wstr.data(), static_cast<int>(wstr.size()), result.data(), size_needed, nullptr, nullptr);
	return result;
}

inline std::wstring AsciiToUtf16(std::string_view str)
{
	const auto size_needed = MultiByteToWideChar(CP_ACP, 0, str.data(), static_cast<int>(str.size()), nullptr, 0);
	std::wstring result(size_needed, 0);
	MultiByteToWideChar(CP_ACP, 0, str.data(), static_cast<int>(str.size()), result.data(), size_needed);
	return result;
}

inline int clamp(int v, int l, int r)
{
	if (v < l) return l;
	if (r < l) return l;
	if (v > r) return r;
	return v;
}

namespace str
{
	constexpr int to_lower(const int c)
	{
		if (c < 128) return ((c >= L'A') && (c <= L'Z')) ? c - L'A' + L'a' : c;
		if (c > USHRT_MAX) return c;
		return towlower(c);
	}

	inline std::wstring_view unquote(std::wstring_view text)
	{
		if (text.size() > 1 && *text.begin() == '"' && *text.rbegin() == '"')
		{
			return text.substr(1, text.length() - 2);
		}
		if (text.size() > 1 && *text.begin() == '\'' && *text.rbegin() == '\'')
		{
			return text.substr(1, text.length() - 2);
		}

		return text;
	}

	constexpr int icmp(const std::wstring_view ll, const std::wstring_view rr)
	{
		if (ll.data() == rr.data() || (ll.empty() && rr.empty())) return 0;
		if (ll.empty()) return 1;
		if (rr.empty()) return -1;

		auto cl = 0;
		auto cr = 0;

		auto il = ll.begin();
		auto ir = rr.begin();
		const auto el = ll.end();
		const auto er = rr.end();

		while (il < el && ir < er)
		{
			cl = to_lower(*il++);
			cr = to_lower(*ir++);
			if (cl < cr) return -1;
			if (cl > cr) return 1;
		}

		if (il == el) cl = 0;
		if (ir == er) cr = 0;
		return cl - cr;
	}

	static inline size_t wcsistr(std::wstring_view text, std::wstring_view pattern)
	{
		auto t = text.begin();
		auto p = pattern.begin();
		size_t pos = 0;

		if (text.empty()) return std::wstring_view::npos;
		if (pattern.empty()) return std::wstring_view::npos;

		do
		{
			if (p == pattern.end()) return pos;

			if ((*p == *t) || (towlower(*p) == towlower(*t)))
			{
				++p;
				++t;
			}
			else
			{
				p = pattern.begin();
				text = text.substr(1);
				pos += 1;
				if (text.empty()) return {};
				t = text.begin();
			}
		} while (true);
	}

	static inline std::wstring combine(const std::vector<std::wstring>& lines, std::wstring_view endl = L"\n")
	{
		if (lines.size() == 1)
		{
			return lines[0];
		}
		std::wostringstream result;
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
				result << std::endl << line;
			}
		}

		return result.str();
	}

	static inline std::wstring replace(std::wstring_view s, std::wstring_view find, std::wstring_view replacement)
	{
		std::wstring result(s);
		size_t pos = 0;
		const auto findLength = find.size();
		const auto replacementLength = replacement.size();

		while ((pos = result.find(find, pos)) != std::wstring::npos)
		{
			result.replace(pos, findLength, replacement);
			pos += replacementLength;
		}

		return result;
	}

	static std::wstring_view From(bool val)
	{
		return val ? L"true" : L"false";
	}

	constexpr int last_char(const std::wstring_view sv)
	{
		if (sv.empty()) return 0;
		return sv.back();
	}
};


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



