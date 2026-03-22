// util.h — Core utilities: string ops, geometry types, crypto, hashing, encoding helpers

#pragma once


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

[[nodiscard]] std::u8string to_base64(std::span<const uint8_t> input);

[[nodiscard]] std::u8string to_hex(std::span<const uint8_t> src);

[[nodiscard]] constexpr int char_to_hex(const char8_t c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return 0;
}

[[nodiscard]] std::vector<uint8_t> hex_to_data(std::u8string_view text);

std::vector<uint8_t> calc_sha256(std::string_view text);


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


[[nodiscard]] size_t find_in_text(std::u8string_view text, std::u8string_view pattern, bool match_case = false);

template <typename T, typename Fn>
std::u8string join(const std::vector<T>& items, Fn text_of, const std::u8string_view endl = u8"\n")
{
	if (items.empty())
	{
		return {};
	}

	if (items.size() == 1)
	{
		return std::u8string(text_of(items[0]));
	}

	size_t total = 0;
	for (const auto& item : items)
		total += text_of(item).size();
	total += (items.size() - 1) * endl.size();

	std::u8string result;
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

[[nodiscard]] std::u8string combine(const std::vector<std::u8string>& lines, std::u8string_view endl = u8"\n");

[[nodiscard]] std::u8string replace(std::u8string_view s, std::u8string_view find, std::u8string_view replacement);

[[nodiscard]] constexpr std::u8string_view to_str(const bool val)
{
	return val ? u8"true" : u8"false";
}

[[nodiscard]] inline std::u8string to_str(const int val)
{
	const auto s = std::to_string(val);
	return std::u8string(reinterpret_cast<const char8_t*>(s.data()), s.size());
}

[[nodiscard]] inline std::u8string to_str(const double val)
{
	const auto s = std::to_string(val);
	return std::u8string(reinterpret_cast<const char8_t*>(s.data()), s.size());
}

[[nodiscard]] constexpr int last_char(const std::u8string_view sv)
{
	if (sv.empty()) return 0;
	return sv.back();
}
