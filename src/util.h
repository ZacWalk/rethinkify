#pragma once

// util.h — Core utilities: string ops, geometry types (ipoint/isize/irect),
// AES-256 encryption, SHA-256 hashing, base64/hex encoding

namespace str
{
	// String conversions (implemented in util.cpp)
	std::string utf16_to_utf8(std::wstring_view wstr);
	std::wstring utf8_to_utf16(std::string_view str);

	constexpr int to_lower(const wint_t c)
	{
		if (c < 128) return c >= L'A' && c <= L'Z' ? c - L'A' + L'a' : c;
		if (c > USHRT_MAX) return c;
		return towlower(c);
	}

	[[nodiscard]] constexpr std::wstring_view unquote(const std::wstring_view text)
	{
		if (text.size() > 1 && text.front() == '"' && text.back() == '"')
		{
			return text.substr(1, text.length() - 2);
		}
		if (text.size() > 1 && text.front() == '\'' && text.back() == '\'')
		{
			return text.substr(1, text.length() - 2);
		}

		return text;
	}

	[[nodiscard]] constexpr int icmp(const std::wstring_view ll, const std::wstring_view rr)
	{
		if (ll.data() == rr.data() || (ll.empty() && rr.empty())) return 0;
		if (ll.empty()) return -1;
		if (rr.empty()) return 1;

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

	[[nodiscard]] size_t find_in_text(std::wstring_view text, std::wstring_view pattern, bool match_case = false);

	template <typename T, typename Fn>
	std::wstring join(const std::vector<T>& items, Fn text_of, const std::wstring_view endl = L"\n")
	{
		if (items.empty())
		{
			return {};
		}

		if (items.size() == 1)
		{
			return std::wstring(text_of(items[0]));
		}

		size_t total = 0;
		for (const auto& item : items)
			total += text_of(item).size();
		total += (items.size() - 1) * endl.size();

		std::wstring result;
		result.reserve(total);
		auto first = true;

		for (const auto& item : items)
		{
			if (first)
			{
				result.append(text_of(item));
				first = false;
			}
			else
			{
				result.append(endl);
				result.append(text_of(item));
			}
		}

		return result;
	}

	[[nodiscard]] std::wstring combine(const std::vector<std::wstring>& lines, std::wstring_view endl = L"\n");

	[[nodiscard]] std::wstring replace(std::wstring_view s, std::wstring_view find, std::wstring_view replacement);

	[[nodiscard]] constexpr std::wstring_view to_str(const bool val)
	{
		return val ? L"true" : L"false";
	}

	[[nodiscard]] inline std::wstring to_str(const int val)
	{
		return std::to_wstring(val);
	}

	[[nodiscard]] constexpr int last_char(const std::wstring_view sv)
	{
		if (sv.empty()) return 0;
		return sv.back();
	}

	[[nodiscard]] constexpr bool is_empty(const wchar_t* sz)
	{
		return sz == nullptr || sz[0] == 0;
	}
};


class ipoint
{
public:
	int32_t x = 0, y = 0;

	constexpr ipoint(const int xx = 0, const int yy = 0) : x(xx), y(yy)
	{
	}

	bool operator==(const ipoint& other) const = default;

	constexpr ipoint operator -() const
	{
		return ipoint(-x, -y);
	}

	constexpr ipoint operator +(const ipoint& point) const
	{
		return ipoint(x + point.x, y + point.y);
	}
};

class isize
{
public:
	int32_t cx = 0, cy = 0;

	constexpr isize(const int xx = 0, const int yy = 0) : cx(xx), cy(yy)
	{
	}

	bool operator==(const isize& other) const = default;
};


class irect
{
public:
	int32_t left = 0, top = 0, right = 0, bottom = 0;

	irect(const int l = 0, const int t = 0, const int r = 0, const int b = 0)
		: left(l), top(t), right(r), bottom(b)
	{
	}

	[[nodiscard]] int Width() const
	{
		return right - left;
	};

	[[nodiscard]] int Height() const
	{
		return bottom - top;
	};

	void OffsetRect(const int x, const int y)
	{
		left += x;
		top += y;
		right += x;
		bottom += y;
	}

	[[nodiscard]] irect Offset(const ipoint& pt) const
	{
		return irect(left + pt.x, top + pt.y, right + pt.x, bottom + pt.y);
	}

	[[nodiscard]] irect Offset(const int x, const int y) const
	{
		return irect(left + x, top + y, right + x, bottom + y);
	}

	[[nodiscard]] bool Intersects(const irect& other) const
	{
		return left < other.right &&
			top < other.bottom &&
			right > other.left &&
			bottom > other.top;
	}

	[[nodiscard]] irect Inflate(const int xy) const
	{
		return irect(left - xy, top - xy, right + xy, bottom + xy);
	}

	[[nodiscard]] irect Inflate(const int x, const int y) const
	{
		return irect(left - x, top - y, right + x, bottom + y);
	}

	[[nodiscard]] irect Inflate(const isize& s) const
	{
		return irect(left - s.cx, top - s.cy, right + s.cx, bottom + s.cy);
	}

	[[nodiscard]] bool Contains(const ipoint& point) const
	{
		return left <= point.x && right >= point.x && top <= point.y && bottom >= point.y;
	}
};

[[nodiscard]] constexpr int clamp(const int v, const int lo, const int hi)
{
	return std::clamp(v, lo, std::max(lo, hi));
}

class aes256
{
	uint8_t key[32];
	uint8_t enckey[32];
	uint8_t deckey[32];

public:
	explicit aes256(std::span<const uint8_t> key);
	~aes256();

	void encrypt_ecb(uint8_t* /* plaintext */);
	void decrypt_ecb(uint8_t* /* cipertext */);
};

class sha256
{
	uint32_t total[2];
	uint32_t state[8];
	uint8_t buffer[64];

	void process(const uint8_t data[64]);

public:
	sha256();
	void update(const uint8_t* input, size_t length);
	void finish(uint8_t digest[32]);
};

[[nodiscard]] std::wstring to_base64(std::span<const uint8_t> input);

[[nodiscard]] std::wstring to_hex(std::span<const uint8_t> src);

[[nodiscard]] constexpr int char_to_hex(const wchar_t c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return 0;
}

[[nodiscard]] std::vector<uint8_t> hex_to_data(std::wstring_view text);

std::vector<uint8_t> calc_sha256(std::string_view text);

static constexpr uint32_t FNV_PRIME_32 = 16777619u;
static constexpr uint32_t OFFSET_BASIS_32 = 2166136261u;

uint32_t fnv1a_i(std::wstring_view sv);

static constexpr uint64_t FNV_PRIME_64 = 1099511628211ULL;
static constexpr uint64_t OFFSET_BASIS_64 = 14695981039346656037ULL;

uint64_t fnv1a_i_64(std::wstring_view sv);

struct color_t
{
	uint8_t r = 0;
	uint8_t g = 0;
	uint8_t b = 0;

	constexpr color_t() = default;

	constexpr color_t(const uint8_t r, const uint8_t g, const uint8_t b) : r(r), g(g), b(b)
	{
	}

	bool operator==(const color_t&) const = default;
	auto operator<=>(const color_t&) const = default;

	[[nodiscard]] constexpr uint32_t rgb() const
	{
		return (r & 0xff) | (g & 0xff) << 8 | (b & 0xff) << 16;
	}

	static constexpr uint8_t clamp_byte(const int n)
	{
		return static_cast<uint8_t>(n > 255 ? 255 : n < 0 ? 0 : n);
	}

	[[nodiscard]] constexpr color_t lighten(const int n = 32) const
	{
		return {clamp_byte(r + n), clamp_byte(g + n), clamp_byte(b + n)};
	}

	[[nodiscard]] constexpr color_t darken(const int n = 32) const
	{
		return lighten(-n);
	}

	[[nodiscard]] constexpr color_t emphasize(const int n = 48) const
	{
		const bool is_light = r > 0x80 || g > 0x80 || b > 0x80;
		return lighten(is_light ? -n : n);
	}
};


class file_path
{
	std::wstring _path;

public:
	file_path(const std::wstring_view path) : _path(path)
	{
	}

	file_path() = default;

	[[nodiscard]] const wchar_t* c_str() const
	{
		return _path.c_str();
	}

	[[nodiscard]] std::wstring_view view() const
	{
		return _path;
	}

	bool operator==(const file_path& other) const
	{
		return str::icmp(_path, other._path) == 0;
	}


	static constexpr std::wstring_view::size_type find_ext(const std::wstring_view path)
	{
		const auto last = path.find_last_of(L"./\\");
		if (last == std::wstring_view::npos || path[last] != '.') return path.size();
		return last;
	}

	static constexpr std::wstring_view::size_type find_last_slash(const std::wstring_view path)
	{
		const auto last = path.find_last_of(L"/\\");
		if (last == std::wstring_view::npos) return 0;
		return last + 1;
	}

	[[nodiscard]] std::wstring without_extension() const
	{
		return _path.substr(0, find_ext(_path));
	}

	[[nodiscard]] std::wstring extension() const
	{
		return _path.substr(find_ext(_path));
	}

	file_path combine(const std::wstring& name, const std::wstring& extension) const
	{
		const auto with_name = combine(name);
		auto result = std::wstring{with_name.without_extension()};

		if (!extension.empty())
		{
			if (extension[0] != L'.') result += L'.';
			result += extension;
		}
		return file_path{result};
	}

	static constexpr bool is_path_sep(const wchar_t c)
	{
		return c == L'\\' || c == L'/';
	}

	[[nodiscard]] file_path combine(const std::wstring_view part) const
	{
		auto result = _path;

		if (!part.empty())
		{
			if (!is_path_sep(str::last_char(result)) && !is_path_sep(part[0])) result += '\\';
			result += part;
		}
		return file_path{result};
	}

	[[nodiscard]] bool exists() const;

	[[nodiscard]] bool is_save_path() const
	{
		return _path.find_first_of(L"/\\") != std::wstring::npos;
	}

	[[nodiscard]] bool empty() const
	{
		return _path.empty();
	}

	[[nodiscard]] std::wstring name() const
	{
		return _path.substr(find_last_slash(_path));
	}

	[[nodiscard]] std::wstring folder() const
	{
		return _path.substr(0, find_last_slash(_path));
	}

	static file_path module_folder();

	static file_path app_data_folder();
};

struct ihash
{
	size_t operator()(const file_path& path) const
	{
		return fnv1a_i(path.view());
	}

	size_t operator()(const std::wstring_view s) const
	{
		return fnv1a_i(s);
	}
};

struct iless
{
	bool operator()(const file_path& l, const file_path& r) const
	{
		return str::icmp(l.view(), r.view()) < 0;
	}

	bool operator()(const std::wstring_view l, const std::wstring_view r) const
	{
		return str::icmp(l, r) < 0;
	}
};

struct ieq
{
	bool operator()(const file_path& l, const file_path& r) const
	{
		return str::icmp(l.view(), r.view()) == 0;
	}

	bool operator()(const std::wstring_view l, const std::wstring_view r) const
	{
		return str::icmp(l, r) == 0;
	}
};
