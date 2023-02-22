#pragma once

namespace str
{
	inline std::string utf16_to_utf8(std::wstring_view wstr)
	{
		const auto size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()), nullptr, 0,
			nullptr, nullptr);
		std::string result(size_needed, 0);
		WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()), result.data(), size_needed, nullptr,
			nullptr);
		return result;
	}

	inline std::wstring utf8_to_utf16(std::string_view str)
	{
		const int size_needed = MultiByteToWideChar(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), nullptr, 0);
		std::wstring result(size_needed, 0);
		MultiByteToWideChar(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), result.data(), size_needed);
		return result;
	}

	inline std::string utf16_to_ascii(std::wstring_view wstr)
	{
		const auto size_needed = WideCharToMultiByte(CP_ACP, 0, wstr.data(), static_cast<int>(wstr.size()), nullptr, 0,
			nullptr, nullptr);
		std::string result(size_needed, 0);
		WideCharToMultiByte(CP_ACP, 0, wstr.data(), static_cast<int>(wstr.size()), result.data(), size_needed, nullptr,
			nullptr);
		return result;
	}

	inline std::wstring ascii_to_utf16(std::string_view str)
	{
		const auto size_needed = MultiByteToWideChar(CP_ACP, 0, str.data(), static_cast<int>(str.size()), nullptr, 0);
		std::wstring result(size_needed, 0);
		MultiByteToWideChar(CP_ACP, 0, str.data(), static_cast<int>(str.size()), result.data(), size_needed);
		return result;
	}

	constexpr int to_lower(const wint_t c)
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

	static inline size_t find_in_text(std::wstring_view text, std::wstring_view pattern)
	{
		auto t = text.begin();
		auto p = pattern.begin();
		size_t pos = 0;

		if (text.empty()) return std::wstring_view::npos;
		if (pattern.empty()) return std::wstring_view::npos;

		do
		{
			if (p == pattern.end()) return pos;
			if (t == text.end()) return std::wstring_view::npos;

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
				if (text.empty()) return std::wstring_view::npos;
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

	static std::wstring_view to_str(const bool val)
	{
		return val ? L"true" : L"false";
	}

	static std::wstring to_str(const int val)
	{
		static constexpr int size = 64;
		wchar_t sz[size];
		_itow_s(val, sz, size, 10);
		return sz;
	}

	constexpr int last_char(const std::wstring_view sv)
	{
		if (sv.empty()) return 0;
		return sv.back();
	}

	constexpr bool is_empty(const wchar_t* sz)
	{
		return sz == nullptr || sz[0] == 0;
	}

	
};


class ipoint : public POINT
{
public:
	ipoint(int xx = 0, int yy = 0)
	{
		x = xx;
		y = yy;
	}

	ipoint(const POINTL& other)
	{
		x = other.x;
		y = other.y;
	}

	bool operator==(const ipoint& other) const
	{
		return x == other.x && y == other.y;
	}

	bool operator!=(const ipoint& other) const
	{
		return x != other.x && y != other.y;
	}

	ipoint operator -() const
	{
		return ipoint(-x, -y);
	}

	ipoint operator +(const POINT& point) const
	{
		return ipoint(x + point.x, y + point.y);
	}
};

class isize : public SIZE
{
public:
	isize(int xx = 0, int yy = 0)
	{
		cx = xx;
		cy = yy;
	}

	bool operator==(const isize& other) const
	{
		return cx == other.cx && cy == other.cy;
	}

	bool operator!=(const isize& other) const
	{
		return cx != other.cx && cy != other.cy;
	}
};


class irect : public RECT
{
public:
	irect(int l = 0, int t = 0, int r = 0, int b = 0)
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

	inline irect Offset(const ipoint& pt) const
	{
		return irect(left + pt.x, top + pt.y, right + pt.x, bottom + pt.y);
	}

	inline irect Offset(int x, int y) const
	{
		return irect(left + x, top + y, right + x, bottom + y);
	}

	inline bool Intersects(const irect& other) const
	{
		return left < other.right &&
			top < other.bottom &&
			right > other.left &&
			bottom > other.top;
	}

	inline irect Inflate(int xy) const
	{
		return irect(left - xy, top - xy, right + xy, bottom + xy);
	}

	inline irect Inflate(int x, int y) const
	{
		return irect(left - x, top - y, right + x, bottom + y);
	}

	inline irect Inflate(const isize& s) const
	{
		return irect(left - s.cx, top - s.cy, right + s.cx, bottom + s.cy);
	}

	inline bool Contains(const POINT& point) const
	{
		return left <= point.x && right >= point.x && top <= point.y && bottom >= point.y;
	}
};

inline int clamp(int v, int l, int r)
{
	if (v < l) return l;
	if (r < l) return l;
	if (v > r) return r;
	return v;
}

class aes256
{
private:
	uint8_t key[32];
	uint8_t enckey[32];
	uint8_t deckey[32];

public:
	aes256(uint8_t* key);
	aes256(const std::vector<uint8_t>& key);
	~aes256();

	void encrypt_ecb(uint8_t* /* plaintext */);
	void decrypt_ecb(uint8_t* /* cipertext */);
};

class sha256
{
private:
	uint32_t total[2];
	uint32_t state[8];
	uint8_t buffer[64];

	void process(const uint8_t data[64]);

public:
	sha256();
	void update(const uint8_t* input, size_t length);
	void finish(uint8_t digest[32]);
};

inline std::wstring to_base64(const uint8_t* bytes_to_encode, size_t in_len)
{
	static const std::string base64_chars =
		"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
		"abcdefghijklmnopqrstuvwxyz"
		"0123456789+/";

	std::wstring ret;
	int i = 0;
	int j = 0;
	uint8_t char_array_3[3];
	uint8_t char_array_4[4];

	while (in_len--)
	{
		char_array_3[i++] = *(bytes_to_encode++);
		if (i == 3)
		{
			char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
			char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
			char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
			char_array_4[3] = char_array_3[2] & 0x3f;

			for (i = 0; (i < 4); i++)
				ret += base64_chars[char_array_4[i]];
			i = 0;
		}
	}

	if (i)
	{
		for (j = i; j < 3; j++)
			char_array_3[j] = '\0';

		char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
		char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
		char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
		char_array_4[3] = char_array_3[2] & 0x3f;

		for (j = 0; (j < i + 1); j++)
			ret += base64_chars[char_array_4[j]];

		while ((i++ < 3))
			ret += '=';
	}

	return ret;
}

inline std::wstring to_hex(const uint8_t* src, size_t len)
{
	static wchar_t digits[] = L"0123456789abcdef";
	size_t n = 0, sn = 0;
	std::wstring result;

	while (sn < len)
	{
		const auto ch = src[sn++];
		result += digits[(ch & 0xf0) >> 4];
		result += digits[ch & 0x0f];
	}

	return result;
}

inline std::wstring to_hex(const std::vector<uint8_t>& src)
{
	static wchar_t digits[] = L"0123456789abcdef";
	std::wstring result;

	for (const auto ch : src)
	{
		result += digits[(ch & 0xf0) >> 4];
		result += digits[ch & 0x0f];
	}

	return result;
}

inline int char_to_hex(const wchar_t c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
}

inline std::vector<uint8_t> hex_to_data(const std::wstring_view text)
{
	std::vector<uint8_t> result;

	auto high_part = ((text.size() % 2) == 0);

	if (!high_part)
		result.push_back(0);
	
	for (const auto c : text)
	{
		if (high_part)
			result.push_back(0x10 * char_to_hex(c));
		else
			result.back() += char_to_hex(c);

		high_part = !high_part;
	}

	return result;
}

inline std::vector<uint8_t> calc_sha256(const std::string_view text)
{
	uint8_t result[32];

	sha256 h;
	h.update((const uint8_t*)text.data(), text.size());
	h.finish(result);

	return { result, result + 32 };
}

static constexpr uint32_t FNV_PRIME_32 = 16777619u;
static constexpr uint32_t OFFSET_BASIS_32 = 2166136261u;

inline uint32_t fnv1a_i(const std::wstring_view sv)
{
	uint32_t result = OFFSET_BASIS_32;

	for (const auto s : sv)
	{
		result ^= str::to_lower(s);
		result *= FNV_PRIME_32;
	}

	return result;
}