// tests.cpp — Unit tests for document editing, undo/redo, search, and crypto

#include "pch.h"
#include "app.h"
#include "document.h"
#include "app_state.h"
#include "agent.h"
#include "test.h"


class null_events final : public document_events
{
public:
	void invalidate(uint32_t) override
	{
	}

	void invalidate_lines(int, int) override
	{
	}

	void ensure_visible(const text_location&) override
	{
	}
};

static null_events null_ev;

// stub_window_frame — No-op window for testing without a real platform window
struct stub_window_frame final : pf::window_frame
{
	void set_reactor(pf::frame_reactor_ptr) override
	{
	}

	void notify_size() override
	{
	}

	pf::irect get_client_rect() const override { return {}; }

	void invalidate() override
	{
	}

	void invalidate_rect(const pf::irect&) override
	{
	}

	void set_focus() override
	{
	}

	bool has_focus() const override { return false; }

	void set_capture() override
	{
	}

	void release_capture() override
	{
	}

	uint32_t set_timer(uint32_t, uint32_t) override { return 0; }

	void kill_timer(uint32_t) override
	{
	}

	pf::ipoint screen_to_client(const pf::ipoint pt) const override { return pt; }

	void set_cursor_shape(pf::cursor_shape) override
	{
	}

	void move_window(const pf::irect&) override
	{
	}

	void show(bool) override
	{
	}

	bool is_visible() const override { return false; }

	void set_text(std::u8string_view) override
	{
	}

	std::u8string text_from_clipboard() override { return {}; }
	bool text_to_clipboard(std::u8string_view) override { return false; }
	placement get_placement() const override { return {}; }

	void set_placement(const placement&) override
	{
	}

	void track_mouse_leave() override
	{
	}

	bool is_key_down(unsigned int) const override { return false; }
	bool is_key_down_async(unsigned int) const override { return false; }

	pf::window_frame_ptr create_child(std::u8string_view, uint32_t, pf::color_t) const & override
	{
		return std::make_shared<stub_window_frame>();
	}

	void close() override
	{
	}

	int message_box(std::u8string_view, std::u8string_view, uint32_t) override { return 0; }

	void set_menu(std::vector<pf::menu_command>) override
	{
	}

	std::unique_ptr<pf::measure_context> create_measure_context() const override { return nullptr; }

	void show_popup_menu(const std::vector<pf::menu_command>&, const pf::ipoint&) override
	{
	}

	double get_dpi_scale() const override { return 1.0; }

	void accept_drop_files(bool) override
	{
	}
};

static void insert_chars(const document_ptr& doc, const std::u8string_view chars,
                         text_location location = text_location(0, 0))
{
	undo_group ug(doc);

	for (const auto c : chars)
	{
		location = doc->insert_text(ug, location, c);
	}
}

static void test_edit_undo_redo(const char8_t* initial, const char8_t* expected,
                                const std::function<void(const document_ptr&, undo_group&)>& edit)
{
	const auto doc = std::make_shared<document>(null_ev, initial);
	{
		undo_group ug(doc);
		edit(doc, ug);
		should::is_equal(expected, doc->str());
	}
	doc->undo();
	should::is_equal(initial, doc->str(), u8"undo");
	doc->redo();
	should::is_equal(expected, doc->str(), u8"redo");
}

static void should_insert_single_chars()
{
	const auto text1 = u8"Hello\nWorld";
	const auto text2 = u8"Line\n";


	const auto doc = std::make_shared<document>(null_ev);

	insert_chars(doc, text1);
	should::is_equal(text1, doc->str());

	insert_chars(doc, text2);
	should::is_equal(std::u8string(text2) + text1, doc->str());
}

static void should_split_line()
{
	test_edit_undo_redo(u8"line of text", u8"line\n of text", [](const document_ptr& doc, undo_group& ug)
	{
		doc->insert_text(ug, text_location(4, 0), u8'\n');
	});
}

static void should_combine_line()
{
	test_edit_undo_redo(u8"line \nof text", u8"line of text", [](const document_ptr& doc, undo_group& ug)
	{
		doc->delete_text(ug, text_location(0, 1));
	});
}

static void should_delete_chars()
{
	test_edit_undo_redo(u8"one\ntwo\nthree", u8"oe\nto\ntree", [](const document_ptr& doc, undo_group& ug)
	{
		doc->delete_text(ug, text_location(2, 0));
		doc->delete_text(ug, text_location(2, 1));
		doc->delete_text(ug, text_location(2, 2));
	});
}

static void should_delete_selection()
{
	test_edit_undo_redo(u8"line of text", u8"lixt", [](const document_ptr& doc, undo_group& ug)
	{
		doc->delete_text(ug, text_selection(2, 0, 10, 0));
	});
}

static void should_delete2_line_selection()
{
	test_edit_undo_redo(u8"one\ntwo\nthree", u8"onree", [](const document_ptr& doc, undo_group& ug)
	{
		doc->delete_text(ug, text_selection(2, 0, 2, 2));
	});
}

static void should_delete1_line_selection()
{
	test_edit_undo_redo(u8"one\ntwo\nthree", u8"on", [](const document_ptr& doc, undo_group& ug)
	{
		doc->delete_text(ug, text_selection(2, 1, 2, 2));
		should::is_equal(u8"one\ntwree", doc->str());
		doc->delete_text(ug, text_selection(2, 0, 5, 1));
	});
}

static void should_insert_selection()
{
	test_edit_undo_redo(u8"line of text", u8"line oone\ntwo\nthreef text", [](const document_ptr& doc, undo_group& ug)
	{
		doc->insert_text(ug, text_location(6, 0), u8"one\ntwo\nthree");
	});
}

static void should_insert_crlf_text()
{
	test_edit_undo_redo(u8"ab", u8"aone\ntwo\nthreeb", [](const document_ptr& doc, undo_group& ug)
	{
		doc->insert_text(ug, text_location(1, 0), u8"one\r\ntwo\r\nthree");
	});
}

static void should_return_selection()
{
	const auto doc = std::make_shared<document>(null_ev, u8"one\ntwo\nthree");

	should::is_equal(u8"e\ntwo\nth", combine(doc->text(text_selection(2, 0, 2, 2))));
}

static void should_cut_and_paste()
{
	test_edit_undo_redo(u8"one\ntwo\nthree", u8"one\none\ntwo\nthreetwo\nthree",
	                    [](const document_ptr& doc, undo_group& ug)
	                    {
		                    doc->insert_text(ug, text_location(0, 1), doc->str());
	                    });
}

static void should_calc_sha256()
{
	const auto text = "hello world";
	const auto output = calc_sha256(text);
	should::is_equal(u8"b94d27b9934d3e08a52e52d7da7dabfac484efe37a5380ee9088f7ace2efcde9", to_hex(output));
}

// ── util.h string tests ────────────────────────────────────────────────────────

static void should_to_lower()
{
	should::is_equal(L'a', pf::to_lower(L'A'));
	should::is_equal(L'z', pf::to_lower(L'Z'));
	should::is_equal(L'a', pf::to_lower(L'a'));
	should::is_equal(L'5', pf::to_lower(L'5'));
}

static void should_unquote()
{
	should::is_equal(u8"hello", pf::unquote(u8"\"hello\""));
	should::is_equal(u8"hello", pf::unquote(u8"'hello'"));
	should::is_equal(u8"hello", pf::unquote(u8"hello"));
	should::is_equal(u8"", pf::unquote(u8""));
}

static void should_icmp()
{
	should::is_equal(0, pf::icmp(u8"Hello", u8"hello"));
	should::is_equal(0, pf::icmp(u8"", u8""));
	should::is_equal_true(pf::icmp(u8"abc", u8"def") < 0);
	should::is_equal_true(pf::icmp(u8"def", u8"abc") > 0);
	should::is_equal_true(pf::icmp(u8"ab", u8"abc") < 0);
	should::is_equal_true(pf::icmp(u8"abc", u8"ab") > 0);
	should::is_equal(-1, pf::icmp(u8"", u8"a"));
	should::is_equal(1, pf::icmp(u8"a", u8""));
}

static void should_find_in_text()
{
	should::is_equal(0, static_cast<int>(find_in_text(u8"Hello World", u8"hello")));
	should::is_equal(6, static_cast<int>(find_in_text(u8"Hello World", u8"world")));
	should::is_equal(static_cast<int>(std::u8string_view::npos),
	                 static_cast<int>(find_in_text(u8"Hello", u8"xyz")));
	should::is_equal(static_cast<int>(std::u8string_view::npos),
	                 static_cast<int>(find_in_text(u8"", u8"abc")));
	should::is_equal(static_cast<int>(std::u8string_view::npos),
	                 static_cast<int>(find_in_text(u8"abc", u8"")));
}

static void should_combine_lines()
{
	const std::vector<std::u8string> lines = {u8"one", u8"two", u8"three"};
	should::is_equal(u8"one\ntwo\nthree", combine(lines));
	should::is_equal(u8"one, two, three", combine(lines, u8", "));

	const std::vector<std::u8string> single = {u8"only"};
	should::is_equal(u8"only", combine(single));
}

static void should_replace_string()
{
	should::is_equal(u8"hello world", replace(u8"hello there", u8"there", u8"world"));
	should::is_equal(u8"aXbXc", replace(u8"a.b.c", u8".", u8"X"));
	should::is_equal(u8"unchanged", replace(u8"unchanged", u8"xyz", u8"abc"));
}

static void should_last_char()
{
	should::is_equal(L'd', last_char(u8"abcd"));
	should::is_equal(0, last_char(u8""));
}

static void should_is_empty()
{
	should::is_equal_true(pf::is_empty(static_cast<const char8_t*>(nullptr)));
	should::is_equal_true(pf::is_empty(u8""));
	should::is_equal(false, pf::is_empty(u8"x"));
}

// ── util.h geometry tests ──────────────────────────────────────────────────────

static void should_ipoint_ops()
{
	constexpr pf::ipoint a(3, 4);
	constexpr pf::ipoint b(1, 2);
	constexpr auto sum = a + b;
	should::is_equal(4, sum.x);
	should::is_equal(6, sum.y);

	constexpr auto neg = -a;
	should::is_equal(-3, neg.x);
	should::is_equal(-4, neg.y);

	should::is_equal_true(pf::ipoint(1, 2) == pf::ipoint(1, 2));
	should::is_equal(false, pf::ipoint(1, 2) == pf::ipoint(3, 4));
}

static void should_isize_ops()
{
	should::is_equal_true(pf::isize(10, 20) == pf::isize(10, 20));
	should::is_equal(false, pf::isize(10, 20) == pf::isize(10, 21));
}

static void should_irect_ops()
{
	const pf::irect r(10, 20, 110, 120);
	should::is_equal(100, r.width());
	should::is_equal(100, r.height());

	should::is_equal_true(r.contains(pf::ipoint(50, 50)));
	should::is_equal(false, r.contains(pf::ipoint(0, 0)));
	should::is_equal(false, r.contains(pf::ipoint(200, 200)));

	const auto offset = r.offset(5, 10);
	should::is_equal(15, offset.left);
	should::is_equal(30, offset.top);

	const auto inflated = r.inflate(2);
	should::is_equal(8, inflated.left);
	should::is_equal(18, inflated.top);
	should::is_equal(112, inflated.right);
	should::is_equal(122, inflated.bottom);

	const pf::irect a(0, 0, 10, 10);
	const pf::irect b(5, 5, 15, 15);
	const pf::irect c(20, 20, 30, 30);
	should::is_equal_true(a.intersects(b));
	should::is_equal(false, a.intersects(c));
}

// ── util.h encoding tests ──────────────────────────────────────────────────────

static void should_hex_roundtrip()
{
	const std::vector<uint8_t> data = {0xDE, 0xAD, 0xBE, 0xEF};
	const auto hex = to_hex(data);
	should::is_equal(u8"deadbeef", hex);

	const auto back = hex_to_data(hex);
	should::is_equal(static_cast<int>(data.size()), static_cast<int>(back.size()));
	for (size_t i = 0; i < data.size(); ++i)
		should::is_equal(data[i], back[i]);
}

static void should_base64_encode()
{
	const std::string text = "Hello";
	const std::vector<uint8_t> data(text.begin(), text.end());
	should::is_equal(u8"SGVsbG8=", to_base64(data));

	std::vector<uint8_t> empty;
	should::is_equal(u8"", to_base64(empty));
}

static void should_aes256_roundtrip()
{
	static const uint8_t key_data[32] = {
		0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
		0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10,
		0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
		0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x20
	};
	uint8_t plaintext[16] = {
		'H', 'e', 'l', 'l', 'o', ' ', 'A', 'E',
		'S', '-', '2', '5', '6', '!', '!', '!'
	};
	uint8_t original[16];
	std::copy_n(plaintext, 16, original);

	aes256 cipher(key_data);
	cipher.encrypt_ecb(plaintext);

	// encrypted data should differ from original
	bool differs = false;
	for (int i = 0; i < 16; ++i)
		if (plaintext[i] != original[i]) differs = true;
	should::is_equal_true(differs);

	aes256 decipher(key_data);
	decipher.decrypt_ecb(plaintext);

	for (int i = 0; i < 16; ++i)
		should::is_equal(original[i], plaintext[i]);
}

static void should_clamp_value()
{
	should::is_equal(5, std::clamp(5, 0, 10));
	should::is_equal(0, std::clamp(-1, 0, 10));
	should::is_equal(10, std::clamp(15, 0, 10));
	should::is_equal(5, std::clamp(5, 5, 5));
}

static void should_fnv1a_hash()
{
	const auto h1 = pf::fnv1a_i(u8"hello");
	const auto h2 = pf::fnv1a_i(u8"HELLO");
	const auto h3 = pf::fnv1a_i(u8"world");
	should::is_equal(static_cast<int>(h1), static_cast<int>(h2));
	should::is_equal_true(h1 != h3);
}

// ── pf::file_path tests ────────────────────────────────────────────────────────────

static void should_file_path_ops()
{
	should::is_equal(6, static_cast<int>(pf::file_path::find_ext(u8"readme.txt")));
	should::is_equal(4, static_cast<int>(pf::file_path::find_ext(u8"test.cpp")));
	should::is_equal(4, static_cast<int>(pf::file_path::find_ext(u8"none")));

	should::is_equal(4, static_cast<int>(pf::file_path::find_last_slash(u8"src/file.cpp")));
	should::is_equal(4, static_cast<int>(pf::file_path::find_last_slash(u8"src\\file.cpp")));

	const pf::file_path p(u8"C:\\code\\project");
	const auto combined = p.combine(u8"file.txt");
	should::is_equal(u8"C:\\code\\project\\file.txt", combined.view());

	should::is_equal_true(pf::file_path::is_path_sep(L'/'));
	should::is_equal_true(pf::file_path::is_path_sep(L'\\'));
	should::is_equal(false, pf::file_path::is_path_sep(L'x'));

	// Path separator normalization
	should::is_equal_true(pf::file_path{u8"c:\\folder"} == pf::file_path{u8"c:/folder"}, u8"slash vs backslash");
	should::is_equal(u8"c:\\folder", pf::file_path{u8"c:/folder"}.view(), u8"forward slash normalized");
	should::is_equal(u8"c:\\folder", pf::file_path{u8"c:\\folder\\"}.view(), u8"trailing sep stripped");
	should::is_equal(u8"C:\\", pf::file_path{u8"C:\\"}.view(), u8"root preserved");
}

// ── encoding detection tests ───────────────────────────────────────────────────

static void check_encoding(const uint8_t* data, const size_t size, file_encoding expected_enc,
                           const int expected_header, const std::u8string_view msg)
{
	int headerLen = 0;
	const auto enc = detect_encoding(data, size, headerLen);
	should::is_equal(static_cast<int>(expected_enc), static_cast<int>(enc), msg);
	should::is_equal(expected_header, headerLen, std::u8string(msg) + u8" header");
}

static void should_detect_utf8_bom()
{
	constexpr uint8_t data[] = {0xEF, 0xBB, 0xBF, 'H', 'e', 'l', 'l', 'o'};
	check_encoding(data, sizeof(data), file_encoding::utf8, 3, u8"UTF-8 BOM");
}

static void should_detect_utf16le_bom()
{
	constexpr uint8_t data[] = {0xFF, 0xFE, 'H', 0x00, 'i', 0x00};
	check_encoding(data, sizeof(data), file_encoding::utf16, 2, u8"UTF-16 LE BOM");
}

static void should_detect_utf16be_bom()
{
	constexpr uint8_t data[] = {0xFE, 0xFF, 0x00, 'H', 0x00, 'i'};
	check_encoding(data, sizeof(data), file_encoding::utf16be, 2, u8"UTF-16 BE BOM");
}

static void should_detect_utf32le_bom()
{
	constexpr uint8_t data[] = {0xFF, 0xFE, 0x00, 0x00, 'H', 0x00, 0x00, 0x00};
	check_encoding(data, sizeof(data), file_encoding::utf32, 4, u8"UTF-32 LE BOM");
}

static void should_detect_utf32be_bom()
{
	constexpr uint8_t data[] = {0x00, 0x00, 0xFE, 0xFF, 0x00, 0x00, 0x00, 'H'};
	check_encoding(data, sizeof(data), file_encoding::utf32be, 4, u8"UTF-32 BE BOM");
}

static void should_detect_utf16le_no_bom()
{
	constexpr uint8_t data[] = {'H', 0x00, 'i', 0x00};
	check_encoding(data, sizeof(data), file_encoding::utf16, 0, u8"UTF-16 LE no BOM");
}

static void should_detect_utf16be_no_bom()
{
	constexpr uint8_t data[] = {0x00, 'H', 0x00, 'i'};
	check_encoding(data, sizeof(data), file_encoding::utf16be, 0, u8"UTF-16 BE no BOM");
}

static void should_detect_utf8_default()
{
	constexpr uint8_t data[] = {'H', 'e', 'l', 'l', 'o'};
	check_encoding(data, sizeof(data), file_encoding::utf8, 0, u8"default UTF-8");
}

static void should_detect_utf8_without_bom()
{
	constexpr uint8_t data[] = {'H', 'e', 'l', 'l', 'o', ' ', 'W', 'o', 'r', 'l', 'd'};
	check_encoding(data, sizeof(data), file_encoding::utf8, 0, u8"UTF-8 without BOM");
}

static void should_detect_small_files()
{
	constexpr uint8_t one[] = {'A'};
	check_encoding(one, 1, file_encoding::utf8, 0, u8"1-byte file");

	constexpr uint8_t bom16[] = {0xFF, 0xFE};
	check_encoding(bom16, 2, file_encoding::utf16, 2, u8"2-byte UTF-16 LE BOM");

	constexpr uint8_t bom8[] = {0xEF, 0xBB, 0xBF};
	check_encoding(bom8, 3, file_encoding::utf8, 3, u8"3-byte UTF-8 BOM");
}

static void should_prioritize_utf32_over_utf16_bom()
{
	const uint8_t data[] = {0xFF, 0xFE, 0x00, 0x00, 0x41, 0x00, 0x00, 0x00};
	check_encoding(data, sizeof(data), file_encoding::utf32, 4, u8"UTF-32 LE over UTF-16 LE");
}

// ── encoding conversion tests ──────────────────────────────────────────────────

static void should_utf8_to_utf16_ascii()
{
	// Pure ASCII round-trips through UTF-8 conversion
	should::is_equal_true(pf::utf8_to_utf16(u8"Hello") == L"Hello", u8"ascii utf8 to utf16");
	should::is_equal_true(pf::utf8_to_utf16(u8"").empty(), u8"empty utf8 to utf16");
}

static void should_utf8_to_utf16_multibyte()
{
	// Em-dash U+2014 is encoded as E2 80 94 in UTF-8
	const std::u8string utf8_emdash = u8"\u2014";
	const auto result = pf::utf8_to_utf16(utf8_emdash);
	should::is_equal(1, static_cast<int>(result.size()), u8"em-dash length");
	should::is_equal(0x2014, result[0], u8"em-dash codepoint");
}

static void should_utf16_to_utf8_roundtrip()
{
	// Em-dash U+2014 round-trip through UTF-16 -> UTF-8 -> UTF-16
	const std::wstring emdash(1, 0x2014);
	const auto utf8 = pf::utf16_to_utf8(emdash);
	should::is_equal(3, static_cast<int>(utf8.size()), u8"em-dash UTF-8 byte count");
	should::is_equal(0xE2, static_cast<uint8_t>(utf8[0]), u8"em-dash byte 0");
	should::is_equal(0x80, static_cast<uint8_t>(utf8[1]), u8"em-dash byte 1");
	should::is_equal(0x94, static_cast<uint8_t>(utf8[2]), u8"em-dash byte 2");

	const auto back = pf::utf8_to_utf16(utf8);
	should::is_equal_true(emdash == back, u8"em-dash round-trip");
}

static void should_utf8_to_utf16_mixed()
{
	// Mixed ASCII and multi-byte: "key — value"
	// U+2014 em-dash = E2 80 94 in UTF-8
	const std::u8string utf8 = u8"key \u2014 value";
	const auto result = pf::utf8_to_utf16(utf8);
	// Should be: 'k','e','y',' ', U+2014, ' ','v','a','l','u','e'
	should::is_equal(11, static_cast<int>(result.size()), u8"mixed UTF-8 length");
	should::is_equal(L'k', result[0], u8"mixed char 0");
	should::is_equal(0x2014, result[4], u8"mixed em-dash");
	should::is_equal(L'v', result[6], u8"mixed char after em-dash");
}

static void should_utf8_to_utf16_various_symbols()
{
	// Euro sign U+20AC = E2 82 AC
	should::is_equal(0x20AC,
	                 pf::utf8_to_utf16(u8"\u20AC")[0],
	                 u8"euro sign");

	// Copyright U+00A9 = C2 A9
	should::is_equal(0x00A9,
	                 pf::utf8_to_utf16(u8"\u00A9")[0],
	                 u8"copyright sign");

	// Japanese Hiragana 'A' U+3042 = E3 81 82
	should::is_equal(0x3042,
	                 pf::utf8_to_utf16(u8"\u3042")[0],
	                 u8"hiragana A");
}

// ── line ending detection tests ────────────────────────────────────────────────

static void check_line_ending(const uint8_t* data, const size_t size, line_endings expected,
                              const std::u8string_view msg)
{
	const auto le = detect_line_endings(data, static_cast<int>(size));
	should::is_equal(static_cast<int>(expected), static_cast<int>(le), msg);
}

static void should_detect_crlf_line_endings()
{
	constexpr uint8_t data[] = {'H', 'e', 'l', 'l', 'o', 0x0D, 0x0A, 'W', 'o', 'r', 'l', 'd'};
	check_line_ending(data, sizeof(data), line_endings::crlf_style_dos, u8"CRLF detection");
}

static void should_detect_lf_line_endings()
{
	constexpr uint8_t data[] = {'H', 'e', 'l', 'l', 'o', 0x0A, 'W', 'o', 'r', 'l', 'd'};
	check_line_ending(data, sizeof(data), line_endings::crlf_style_unix, u8"LF detection");
}

static void should_detect_lfcr_line_endings()
{
	constexpr uint8_t data[] = {'H', 'e', 'l', 'l', 'o', 0x0A, 0x0D, 'W', 'o', 'r', 'l', 'd'};
	check_line_ending(data, sizeof(data), line_endings::crlf_style_unix, u8"LFCR detection");
}

static int test_md_parse(const char8_t* text, text_block* blocks)
{
	int count = 0;
	const document_line line(std::u8string_view{text});
	const auto highlighter = select_highlighter(doc_type::markdown, {});
	std::u8string line_view;
	line.render(line_view);
	highlighter(0, line_view, blocks, count);
	return count;
}

static void should_md_highlight_heading()
{
	text_block blocks[64];

	const auto h1_count = test_md_parse(u8"# Hello", blocks);
	should::is_equal(2, h1_count, u8"md h1 block count");
	should::is_equal(0, blocks[0]._char_pos, u8"md h1 marker pos");
	should::is_equal(2, blocks[1]._char_pos, u8"md h1 content pos");

	const auto h2_count = test_md_parse(u8"## World", blocks);
	should::is_equal(2, h2_count, u8"md h2 block count");
	should::is_equal(0, blocks[0]._char_pos, u8"md h2 marker pos");
	should::is_equal(3, blocks[1]._char_pos, u8"md h2 content pos");
}

static void should_md_highlight_bold()
{
	text_block blocks[64];
	const auto count = test_md_parse(u8"some **bold** text", blocks);
	should::is_equal_true(count >= 3, u8"md bold block count");
}

static void should_md_highlight_italic()
{
	text_block blocks[64];
	const auto count = test_md_parse(u8"some *italic* text", blocks);
	should::is_equal_true(count >= 3, u8"md italic block count");
}

static void should_md_highlight_link()
{
	text_block blocks[64];
	const auto count = test_md_parse(u8"click [here](http://example.com) now", blocks);
	should::is_equal_true(count >= 5, u8"md link block count");
}

static void should_md_highlight_list()
{
	text_block blocks[64];
	const auto count = test_md_parse(u8"- list item", blocks);
	should::is_equal_true(count >= 2, u8"md list block count");
	should::is_equal(0, blocks[0]._char_pos, u8"md list bullet pos");
}

// ── app_state tests ────────────────────────────────────────────────────────────────────

// sync_scheduler — Test stub that executes tasks immediately on the calling thread.
class sync_scheduler final : public async_scheduler
{
public:
	void run_async(const std::function<void()> task) override { if (task) task(); }
	void run_ui(const std::function<void()> task) override { if (task) task(); }
};


static std::shared_ptr<app_state> create_test_app()
{
	auto state = std::make_shared<app_state>(std::make_shared<sync_scheduler>());
	state->on_create(std::make_shared<stub_window_frame>());
	return state;
}

static pf::file_path create_temp_test_root()
{
	const auto temp_path = pf::file_path{pf::platform_temp_file_path(u8"rtf")};
	pf::platform_recycle_file(temp_path);
	pf::platform_create_directory(temp_path);
	return temp_path;
}

static void write_test_text_file(const pf::file_path& path, const std::u8string_view text)
{
	const auto handle = pf::open_file_for_write(path);
	should::is_equal_true(handle != nullptr, u8"test file opened for write");
	const auto written = handle->write(reinterpret_cast<const uint8_t*>(text.data()),
	                                   static_cast<uint32_t>(text.size()));
	should::is_equal_true(written == text.size(), u8"test file written");
}

static std::u8string read_test_text_file(const pf::file_path& path)
{
	const auto handle = pf::open_for_read(path);
	should::is_equal_true(handle != nullptr, u8"test file opened for read");
	std::vector<uint8_t> data(handle->size());
	uint32_t total = 0;
	while (total < data.size())
	{
		uint32_t read = 0;
		if (!handle->read(data.data() + total, static_cast<uint32_t>(data.size() - total), &read) || read == 0)
			break;
		total += read;
	}
	return std::u8string(reinterpret_cast<const char8_t*>(data.data()), total);
}

static index_item_ptr find_test_item(const index_item_ptr& item, const pf::file_path& path)
{
	if (!item)
		return nullptr;
	if (item->path == path)
		return item;

	for (const auto& child : item->children)
	{
		if (auto found = find_test_item(child, path))
			return found;
	}

	return nullptr;
}

static void should_app_state_new_doc_is_markdown()
{
	const auto state = create_test_app();
	const auto root = std::make_shared<index_item>(pf::file_path{u8"c:\\folder"}, u8"folder", true);
	state->set_root(root);

	const auto result = state->create_new_file(state->save_folder().combine(u8"new", u8"md"), u8"");

	should::is_equal_true(result.created, u8"new doc created");
	should::is_equal_true(is_markdown_path(state->active_item()->path), u8"new doc is markdown");
	should::is_equal_true(state->active_item()->path.is_save_path(), u8"new doc has save path");
	should::is_equal(static_cast<int>(view_mode::edit_text_files), static_cast<int>(state->get_mode()),
	                 u8"new empty doc mode is edit");
}

static void should_app_state_is_markdown_path()
{
	should::is_equal_true(is_markdown_path(pf::file_path{u8"readme.md"}));
	should::is_equal_true(is_markdown_path(pf::file_path{u8"DOC.MARKDOWN"}));
	should::is_equal(false, is_markdown_path(pf::file_path{u8"code.cpp"}));
	should::is_equal(false, is_markdown_path(pf::file_path{u8"noext"}));
}

static void should_create_new_file_with_content()
{
	const auto state = create_test_app();
	const auto root = std::make_shared<index_item>(pf::file_path{u8"c:\\folder"}, u8"folder", true);
	state->set_root(root);

	const auto result = state->create_new_file(state->save_folder().combine(u8"notes", u8"md"), u8"# Hello");

	should::is_equal_true(result.created, u8"new file created");
	should::is_equal(u8"# Hello", state->doc()->str(), u8"new file content");
	should::is_equal(static_cast<int>(view_mode::edit_text_files), static_cast<int>(state->get_mode()),
	                 u8"non-empty content mode is edit");
	should::is_equal(1, static_cast<int>(root->children.size()), u8"file added to root");
}

static void should_create_new_file_added_to_tree()
{
	const auto state = create_test_app();
	const auto root = std::make_shared<index_item>(pf::file_path{u8"c:\\folder"}, u8"folder", true);
	const auto existing = std::make_shared<index_item>(pf::file_path{u8"c:\\folder\\old.txt"}, u8"old.txt", false,
	                                                   std::make_shared<document>(null_ev, u8"old"));
	root->children.push_back(existing);
	state->set_root(root);
	state->set_active_item(existing);

	const auto result = state->create_new_file(state->save_folder().combine(u8"new", u8"md"), u8"");

	// New file should be added alongside existing
	should::is_equal_true(result.created, u8"new file created");
	should::is_equal(2, static_cast<int>(root->children.size()), u8"two files in root");
	// Active item should be the new file
	should::is_equal_true(state->active_item()->path != existing->path, u8"active switched to new");
	should::is_equal_true(is_markdown_path(state->active_item()->path), u8"new file is markdown");
}

static void should_create_new_file_with_unique_name()
{
	const auto state = create_test_app();
	const auto root = std::make_shared<index_item>(pf::file_path{u8"c:\\folder"}, u8"folder", true);
	root->children.push_back(std::make_shared<index_item>(
		pf::file_path{u8"c:\\folder\\new.md"}, u8"new.md", false, std::make_shared<document>(null_ev, u8"old")));
	state->set_root(root);

	const auto result = state->create_new_file(state->save_folder().combine(u8"new", u8"md"), u8"");

	should::is_equal_true(result.created, u8"unique new file created");
	should::is_equal(u8"new-2.md", result.name, u8"collision resolved with numbered name");
	should::is_equal(u8"new-2.md", state->active_item()->name, u8"active item uses resolved name");
	should::is_equal(2, static_cast<int>(root->children.size()), u8"unique file added to tree");
}

static void should_restore_per_document_view_mode()
{
	const auto state = create_test_app();
	const auto root = std::make_shared<index_item>(pf::file_path{u8"c:\\folder"}, u8"folder", true);

	const auto markdown_path = pf::file_path{u8"c:\\folder\\notes.md"};
	const auto text_path = pf::file_path{u8"c:\\folder\\other.txt"};

	const auto markdown_doc = std::make_shared<document>(null_ev, u8"# Notes");
	markdown_doc->path(markdown_path);
	const auto text_doc = std::make_shared<document>(null_ev, u8"plain text");
	text_doc->path(text_path);

	const auto markdown_item = std::make_shared<index_item>(markdown_path, u8"notes.md", false, markdown_doc);
	const auto text_item = std::make_shared<index_item>(text_path, u8"other.txt", false, text_doc);
	root->children.push_back(markdown_item);
	root->children.push_back(text_item);
	state->set_root(root);

	state->set_active_item(markdown_item);
	should::is_equal(static_cast<int>(view_mode::markdown_files), static_cast<int>(state->get_mode()),
	                 u8"markdown file opens in preview");

	state->on_escape();
	should::is_equal(static_cast<int>(view_mode::edit_text_files), static_cast<int>(state->get_mode()),
	                 u8"escape switches markdown file to edit");

	state->set_active_item(text_item);
	should::is_equal(static_cast<int>(view_mode::edit_text_files), static_cast<int>(state->get_mode()),
	                 u8"text file stays in edit");

	state->set_active_item(markdown_item);
	should::is_equal(static_cast<int>(view_mode::edit_text_files), static_cast<int>(state->get_mode()),
	                 u8"markdown file restores its last view mode");
}

static void should_create_multiple_new_files()
{
	const auto state = create_test_app();
	const auto root = std::make_shared<index_item>(pf::file_path{u8"c:\\folder"}, u8"folder", true);
	state->set_root(root);

	const auto first = state->create_new_file(state->save_folder().combine(u8"a", u8"md"), u8"aaa");
	const auto second = state->create_new_file(state->save_folder().combine(u8"b", u8"md"), u8"bbb");

	should::is_equal_true(first.created, u8"first file created");
	should::is_equal_true(second.created, u8"second file created");
	should::is_equal(2, static_cast<int>(root->children.size()), u8"two files created");
	should::is_equal(u8"bbb", state->doc()->str(), u8"active is second file");
}

static void should_create_new_file_sorted_in_tree()
{
	const auto state = create_test_app();
	const auto root = std::make_shared<index_item>(pf::file_path{u8"c:\\folder"}, u8"folder", true);
	root->children.push_back(std::make_shared<index_item>(
		pf::file_path{u8"c:\\folder\\zeta.md"}, u8"zeta.md", false, std::make_shared<document>(null_ev, u8"z")));
	root->children.push_back(std::make_shared<index_item>(
		pf::file_path{u8"c:\\folder\\beta.md"}, u8"beta.md", false, std::make_shared<document>(null_ev, u8"b")));
	state->set_root(root);

	const auto result = state->create_new_file(state->save_folder().combine(u8"alpha", u8"md"), u8"");

	should::is_equal_true(result.created, u8"sorted file created");
	should::is_equal(u8"alpha.md", root->children[0]->name, u8"new file inserted in sorted position");
	should::is_equal(u8"beta.md", root->children[1]->name, u8"existing file order preserved");
	should::is_equal(u8"zeta.md", root->children[2]->name, u8"later file remains last");
}

static void should_create_new_folder_with_unique_name()
{
	const auto state = create_test_app();
	const auto root_path = create_temp_test_root();
	const auto existing_path = root_path.combine(u8"new-folder");
	should::is_equal_true(pf::platform_create_directory(existing_path), u8"existing folder created");
	state->set_root(app_state::load_index(root_path, {}));

	const auto result = state->create_new_folder(root_path);

	should::is_equal_true(result.created, u8"new folder created");
	should::is_equal(u8"new-folder-2", result.name, u8"folder collision resolved with numbered name");
	should::is_equal_true(pf::is_directory(result.path), u8"resolved folder exists on disk");

	pf::platform_recycle_file(root_path);
}

static void should_refresh_index_preserve_unsaved_doc_folder()
{
	const auto state = create_test_app();
	const auto root_path = create_temp_test_root();
	const auto notes_path = root_path.combine(u8"notes");
	should::is_equal_true(pf::platform_create_directory(notes_path), u8"notes folder created");

	state->set_root(app_state::load_index(root_path, {}));
	const auto created = state->create_new_file(notes_path.combine(u8"draft", u8"md"), u8"draft");
	should::is_equal_true(created.created, u8"draft file created in nested folder");

	state->refresh_index(root_path);

	const auto refreshed_notes = find_test_item(state->root_item(), notes_path);
	const auto refreshed_draft = find_test_item(state->root_item(), created.path);

	should::is_equal_true(refreshed_notes != nullptr, u8"notes folder still exists after refresh");
	should::is_equal_true(refreshed_draft != nullptr, u8"draft file preserved after refresh");
	should::is_equal_true(refreshed_draft != state->root_item(), u8"draft file not promoted to root");
	should::is_equal_true(find_test_item(refreshed_notes, created.path) != nullptr,
	                      u8"draft file remains under original folder");

	pf::platform_recycle_file(root_path);
}

static void should_remember_recent_root_folders_most_recent_first()
{
	const auto state = create_test_app();
	state->_recent_root_folders.clear();
	state->_recent_root_documents.clear();

	state->remember_root_folder(pf::file_path{u8"c:\\one"});
	state->remember_root_folder(pf::file_path{u8"c:\\two"});
	state->remember_root_folder(pf::file_path{u8"c:\\one"});

	should::is_equal(2, static_cast<int>(state->recent_root_folders().size()), u8"deduped list size");
	should::is_equal(u8"c:\\one", state->recent_root_folders()[0].view(), u8"latest folder moved to front");
	should::is_equal(u8"c:\\two", state->recent_root_folders()[1].view(), u8"older folder shifted back");
}

static void should_cap_recent_root_folders_at_eight()
{
	const auto state = create_test_app();
	state->_recent_root_folders.clear();
	state->_recent_root_documents.clear();

	for (int i = 0; i < 10; ++i)
		state->remember_root_folder(pf::file_path{pf::format(u8"c:\\folder{}", i)});

	should::is_equal(8, static_cast<int>(state->recent_root_folders().size()), u8"capped at eight");
	should::is_equal(u8"c:\\folder9", state->recent_root_folders()[0].view(), u8"newest folder kept first");
	should::is_equal(u8"c:\\folder2", state->recent_root_folders()[7].view(), u8"oldest retained folder kept last");
}

static void should_restore_last_open_file_when_switching_recent_root_folder()
{
	const auto state = create_test_app();
	state->_recent_root_folders.clear();
	state->_recent_root_documents.clear();

	const auto first_root = create_temp_test_root();
	const auto second_root = create_temp_test_root();
	const auto first_file = first_root.combine(u8"alpha", u8"md");
	const auto second_file = second_root.combine(u8"beta", u8"md");

	write_test_text_file(first_file, u8"alpha");
	write_test_text_file(second_file, u8"beta");

	state->refresh_index(first_root);
	state->load_doc(first_file);
	state->refresh_index(second_root);
	state->load_doc(second_file);

	const auto menu = state->build_recent_root_folder_menu();
	should::is_equal_true(menu.size() >= 2, u8"recent root menu contains both roots");
	menu[1].action();

	should::is_equal(first_root.view(), state->root_item()->path.view(), u8"recent folder switch restored root");
	should::is_equal(first_file.view(), state->active_item()->path.view(),
	                 u8"recent folder switch restored last open file");
	should::is_equal_true(find_test_item(state->root_item(), second_file) == nullptr,
	                      u8"recent folder switch does not carry old root file into new tree");

	pf::platform_recycle_file(first_root);
	pf::platform_recycle_file(second_root);
}

static void should_doc_is_json()
{
	const auto d1 = std::make_shared<document>(null_ev, u8"{\"key\":\"value\"}");
	should::is_equal_true(d1->is_json());

	const auto d2 = std::make_shared<document>(null_ev, u8"hello world");
	should::is_equal(false, d2->is_json());

	const auto d3 = std::make_shared<document>(null_ev, u8"  \t{}");
	should::is_equal_true(d3->is_json());
}


static void should_doc_sort_remove_duplicates()
{
	const auto d = std::make_shared<document>(null_ev,
	                                          u8"banana\napple\nbanana\ncherry\napple");
	d->sort_remove_duplicates();

	should::is_equal(u8"apple\nbanana\ncherry", d->str());
}

static void should_doc_reformat_json()
{
	const auto d = std::make_shared<document>(null_ev, u8"{\"a\":\"b\"}");
	d->reformat_json();

	// After reformat, the doc should contain formatted JSON with braces on separate lines
	const auto result = d->str();
	should::is_equal_true(result.find(u8'{') != std::u8string::npos, u8"json has open brace");
	should::is_equal_true(result.find(u8'}') != std::u8string::npos, u8"json has close brace");
	// The key-value should have spaces around colon
	should::is_equal_true(result.find(u8" : ") != std::u8string::npos, u8"json has spaced colon");
}

static void should_undo_back_to_clean()
{
	const auto d = std::make_shared<document>(null_ev, u8"hello");
	should::is_equal(false, d->is_modified());

	// Make an edit
	{
		undo_group ug(d);
		d->insert_text(ug, text_location(5, 0), u8" world");
	}
	should::is_equal_true(d->is_modified());

	// Undo → back to saved state
	d->edit_undo();
	should::is_equal(false, d->is_modified(), u8"undo to clean");

	// Redo → modified again
	d->edit_redo();
	should::is_equal_true(d->is_modified(), u8"redo is modified");

	// Undo again
	d->edit_undo();
	should::is_equal(false, d->is_modified(), u8"undo again to clean");
}

static void should_undo_multiple_to_clean()
{
	const auto d = std::make_shared<document>(null_ev, u8"abc");

	// Two edits
	{
		undo_group ug(d);
		d->insert_text(ug, text_location(3, 0), u8"d");
	}
	{
		undo_group ug(d);
		d->insert_text(ug, text_location(4, 0), u8"e");
	}
	should::is_equal_true(d->is_modified());

	// Undo one → still modified
	d->edit_undo();
	should::is_equal_true(d->is_modified(), u8"undo one still modified");

	// Undo two → clean
	d->edit_undo();
	should::is_equal(false, d->is_modified(), u8"undo two clean");
}


// ── Search model tests ─────────────────────────────────────────────────────────

static void should_search_doc_basic()
{
	const auto state = create_test_app();

	const auto item = std::make_shared<index_item>(pf::file_path{u8"test.txt"}, u8"test.txt", false,
	                                               std::make_shared<document>(
		                                               null_ev, u8"hello world\ngoodbye world\nhello again"));

	const auto root = std::make_shared<index_item>(pf::file_path{u8"root"}, u8"root", true);
	root->children.push_back(item);
	state->set_root(root);

	state->execute_search(u8"hello");

	should::is_equal(2, static_cast<int>(item->search_results.size()), u8"search result count");
	should::is_equal(0, item->search_results[0].line_number, u8"first result line");
	should::is_equal(2, item->search_results[1].line_number, u8"second result line");
}

static void should_search_doc_match_positions()
{
	const auto state = create_test_app();

	const auto item = std::make_shared<index_item>(pf::file_path{u8"test.txt"}, u8"test.txt", false,
	                                               std::make_shared<document>(null_ev, u8"\tone two one"));

	const auto root = std::make_shared<index_item>(pf::file_path{u8"root"}, u8"root", true);
	root->children.push_back(item);
	state->set_root(root);

	state->execute_search(u8"one");

	should::is_equal(2, static_cast<int>(item->search_results.size()), u8"match count");

	// First match at position 1 (after tab), trimmed text starts at 0
	should::is_equal(1, item->search_results[0].line_match_pos, u8"first match pos");
	should::is_equal(0, item->search_results[0].text_match_start, u8"first trimmed pos");
	should::is_equal(3, item->search_results[0].text_match_length, u8"first match len");

	// Second match
	should::is_equal(9, item->search_results[1].line_match_pos, u8"second match pos");
	should::is_equal(8, item->search_results[1].text_match_start, u8"second trimmed pos");
}

static void should_search_doc_case_insensitive()
{
	const auto state = create_test_app();

	const auto item = std::make_shared<index_item>(pf::file_path{u8"test.txt"}, u8"test.txt", false,
	                                               std::make_shared<document>(null_ev, u8"Hello HELLO hello"));

	const auto root = std::make_shared<index_item>(pf::file_path{u8"root"}, u8"root", true);
	root->children.push_back(item);
	state->set_root(root);

	state->execute_search(u8"hello");

	should::is_equal(3, static_cast<int>(item->search_results.size()), u8"case insensitive count");
}

static void should_search_doc_empty_clears()
{
	const auto state = create_test_app();

	const auto item = std::make_shared<index_item>(pf::file_path{u8"test.txt"}, u8"test.txt", false,
	                                               std::make_shared<document>(null_ev, u8"hello world"));

	const auto root = std::make_shared<index_item>(pf::file_path{u8"root"}, u8"root", true);
	root->children.push_back(item);
	state->set_root(root);

	state->execute_search(u8"hello");
	should::is_equal(1, static_cast<int>(item->search_results.size()), u8"before clear");

	state->execute_search(u8"");
	should::is_equal(0, static_cast<int>(item->search_results.size()), u8"after clear");
}

static void should_search_multiple_files()
{
	const auto state = create_test_app();

	const auto item1 = std::make_shared<index_item>(pf::file_path{u8"a.txt"}, u8"a.txt", false,
	                                                std::make_shared<document>(null_ev, u8"foo bar"));
	const auto item2 = std::make_shared<index_item>(pf::file_path{u8"b.txt"}, u8"b.txt", false,
	                                                std::make_shared<document>(null_ev, u8"bar baz"));

	const auto root = std::make_shared<index_item>(pf::file_path{u8"root"}, u8"root", true);
	root->children.push_back(item1);
	root->children.push_back(item2);
	state->set_root(root);

	state->execute_search(u8"bar");

	should::is_equal(1, static_cast<int>(item1->search_results.size()), u8"file1 results");
	should::is_equal(1, static_cast<int>(item2->search_results.size()), u8"file2 results");
}

static void should_search_no_match()
{
	const auto state = create_test_app();

	const auto item = std::make_shared<index_item>(pf::file_path{u8"test.txt"}, u8"test.txt", false,
	                                               std::make_shared<document>(null_ev, u8"hello world"));

	const auto root = std::make_shared<index_item>(pf::file_path{u8"root"}, u8"root", true);
	root->children.push_back(item);
	state->set_root(root);

	state->execute_search(u8"xyz");

	should::is_equal(0, static_cast<int>(item->search_results.size()), u8"no match");
}

// ── Command system tests ───────────────────────────────────────────────

static void should_commands_tokenize_simple()
{
	commands cmds;
	bool called = false;
	std::vector<std::u8string> received_args;

	cmds.set_commands({
		{
			{u8"t", u8"test"}, u8"Test command", {}, 0, {}, nullptr, nullptr,
			[&](const std::vector<std::u8string>& args)
			{
				called = true;
				received_args = args;
				return command_result{u8"ok", true};
			}
		},
	});

	const auto result = cmds.execute(u8"t arg1 arg2");
	should::is_equal(true, called, u8"command was called");
	should::is_equal(true, result.success, u8"result success");
	should::is_equal(2, static_cast<int>(received_args.size()), u8"arg count");
	should::is_equal(std::u8string_view(u8"arg1"), std::u8string_view(received_args[0]), u8"arg0");
	should::is_equal(std::u8string_view(u8"arg2"), std::u8string_view(received_args[1]), u8"arg1");
}

static void should_commands_quoted_args()
{
	commands cmds;
	std::vector<std::u8string> received_args;

	cmds.set_commands({
		{
			{u8"e", u8"echo"}, u8"Echo", {}, 0, {}, nullptr, nullptr,
			[&](const std::vector<std::u8string>& args)
			{
				received_args = args;
				return command_result{u8"ok", true};
			}
		},
	});

	cmds.execute(u8"e \"hello world\" foo");
	should::is_equal(2, static_cast<int>(received_args.size()), u8"quoted arg count");
	should::is_equal(std::u8string_view(u8"hello world"), std::u8string_view(received_args[0]), u8"quoted arg");
	should::is_equal(std::u8string_view(u8"foo"), std::u8string_view(received_args[1]), u8"second arg");
}

static void should_commands_unknown_command()
{
	commands cmds;
	cmds.set_commands({});

	const auto result = cmds.execute(u8"bogus");
	should::is_equal(false, result.success, u8"unknown command fails");
	should::is_equal(true, result.output.find(u8"Unknown command") != std::u8string::npos, u8"error message");
}

static void should_commands_short_and_long()
{
	commands cmds;
	int call_count = 0;

	cmds.set_commands({
		{
			{u8"s", u8"save"}, u8"Save", {}, 0, {}, nullptr, nullptr,
			[&](const std::vector<std::u8string>&)
			{
				call_count++;
				return command_result{u8"saved", true};
			}
		},
	});

	cmds.execute(u8"s");
	cmds.execute(u8"save");
	cmds.execute(u8"SAVE");
	should::is_equal(3, call_count, u8"both aliases work case-insensitively");
}

static void should_commands_help_text()
{
	commands cmds;

	cmds.set_commands({
		{
			{u8"s", u8"save"}, u8"Save the file", {}, 0, {}, nullptr, nullptr,
			[](const std::vector<std::u8string>&)
			{
				return command_result{u8"", true};
			}
		},
		{
			{u8"q", u8"exit"}, u8"Exit app", {}, 0, {}, nullptr, nullptr,
			[](const std::vector<std::u8string>&)
			{
				return command_result{u8"", true};
			}
		},
	});

	const auto text = cmds.help_text();
	should::is_equal(true, text.find(u8"save") != std::u8string::npos, u8"help contains save");
	should::is_equal(true, text.find(u8"exit") != std::u8string::npos, u8"help contains exit");
	should::is_equal(true, text.find(u8"Save the file") != std::u8string::npos, u8"help contains description");
}

static void should_commands_tokenize_redirection_operators()
{
	commands cmds;
	std::vector<std::u8string> received_args;

	cmds.set_commands({
		{
			{u8"e"}, u8"Echo", {}, 0, {}, nullptr, nullptr,
			[&](const std::vector<std::u8string>& args)
			{
				received_args = args;
				return command_result{u8"ok", true};
			}
		},
	});

	cmds.execute(u8"e \"hello world\">>out.txt");
	should::is_equal(3, static_cast<int>(received_args.size()), u8"redirection arg count");
	should::is_equal(std::u8string_view(u8"hello world"), std::u8string_view(received_args[0]), u8"quoted text");
	should::is_equal(std::u8string_view(u8">>"), std::u8string_view(received_args[1]), u8"append operator");
	should::is_equal(std::u8string_view(u8"out.txt"), std::u8string_view(received_args[2]), u8"output file");
}

static void should_filter_commands_by_availability()
{
	commands cmds;

	cmds.set_commands({
		{
			{u8"copy"}, u8"Copy text", {}, 0, {}, nullptr, nullptr,
			[](const std::vector<std::u8string>&)
			{
				return command_result{u8"", true};
			}
		},
		{
			{u8"q", u8"quote"}, u8"Fetch quote", {}, 0, {}, nullptr, nullptr,
			[](const std::vector<std::u8string>&)
			{
				return command_result{u8"ok", true};
			},
			command_availability::console | command_availability::agent
		},
	});

	const auto agent_help = cmds.help_text(command_availability::agent);
	should::is_equal(false, agent_help.find(u8"Copy text") != std::u8string::npos,
	                 u8"ui command hidden from agent help");
	should::is_equal(true, agent_help.find(u8"Fetch quote") != std::u8string::npos, u8"agent command shown in help");
}

static void should_reject_commands_outside_context()
{
	commands cmds;
	bool called = false;

	cmds.set_commands({
		{
			{u8"copy"}, u8"Copy text", {}, 0, {}, nullptr, nullptr,
			[&](const std::vector<std::u8string>&)
			{
				called = true;
				return command_result{u8"copied", true};
			}
		},
	});

	const auto result = cmds.invoke(u8"copy", {}, command_availability::agent);
	should::is_equal(false, result.success, u8"agent invoke rejected");
	should::is_equal(false, called, u8"restricted command not executed");
	should::is_equal(true, result.output.find(u8"not available") != std::u8string::npos, u8"restriction message shown");
}

static void should_summarize_command_be_console_only()
{
	const auto state = create_test_app();
	const auto root_path = create_temp_test_root();
	state->set_root(std::make_shared<index_item>(root_path, u8"root", true));

	const auto help = state->get_commands().help_text();
	should::is_equal(true, help.find(u8"summarize") != std::u8string::npos, u8"console help contains summarize");

	const auto agent_help = state->get_commands().help_text(command_availability::agent);
	should::is_equal(false, agent_help.find(u8"summarize") != std::u8string::npos,
	                 u8"agent help hides summarize");

	const auto result = state->get_commands().invoke(u8"summarize", {}, command_availability::agent);
	should::is_equal(false, result.success, u8"agent summarize rejected");
	should::is_equal(true, result.output.find(u8"not available") != std::u8string::npos,
	                 u8"agent summarize restriction message");

	pf::platform_recycle_file(root_path);
}

static void should_calc_expression()
{
	const auto state = create_test_app();
	const auto root_path = create_temp_test_root();
	state->set_root(std::make_shared<index_item>(root_path, u8"root", true));

	const auto result = state->get_commands().execute(u8"calc 1 + 2 * (3 + 4)");
	should::is_equal(true, result.success, u8"calc succeeded");
	should::is_equal(u8"15", result.output, u8"calc result");

	pf::platform_recycle_file(root_path);
}

static void should_echo_redirection_append_to_file()
{
	const auto state = create_test_app();
	const auto root_path = create_temp_test_root();
	state->set_root(std::make_shared<index_item>(root_path, u8"root", true));

	const auto first = state->get_commands().execute(u8"echo hello > note.txt");
	should::is_equal(true, first.success, u8"first echo succeeded");
	const auto second = state->get_commands().execute(u8"echo world >> note.txt");
	should::is_equal(true, second.success, u8"second echo succeeded");

	should::is_equal(u8"helloworld", read_test_text_file(root_path.combine(u8"note.txt")), u8"appended file contents");

	pf::platform_recycle_file(root_path);
}

static void should_save_and_open_new_file_at_requested_path()
{
	const auto state = create_test_app();
	const auto root_path = create_temp_test_root();
	state->set_root(app_state::load_index(root_path, {}));

	const auto output_path = root_path.combine(u8"summary", u8"md");
	const auto ok = save_and_open_text_file(*state, output_path, u8"# Summary", false);

	should::is_equal(true, ok, u8"save and open succeeded");
	should::is_equal_true(output_path.exists(), u8"output file exists on disk");
	should::is_equal(output_path.view(), state->active_item()->path.view(), u8"active path matches requested path");
	should::is_equal(u8"summary.md", state->active_item()->name, u8"active item uses requested name");
	should::is_equal(u8"# Summary", state->doc()->str(), u8"document content loaded");

	pf::platform_recycle_file(root_path);
}

static void should_copy_move_rename_and_delete_in_sandbox()
{
	const auto state = create_test_app();
	const auto root_path = create_temp_test_root();
	pf::platform_create_directory(root_path.combine(u8"sub"));
	write_test_text_file(root_path.combine(u8"a.txt"), u8"alpha");
	state->refresh_index(root_path);

	const auto copy_result = state->get_commands().execute(u8"cp a.txt sub");
	should::is_equal(true, copy_result.success, u8"copy succeeded");
	should::is_equal(u8"alpha", read_test_text_file(root_path.combine(u8"sub\\a.txt")), u8"copied file contents");

	const auto move_result = state->get_commands().execute(u8"mv sub\\a.txt moved.txt");
	should::is_equal(true, move_result.success, u8"move succeeded");
	should::is_equal(true, !root_path.combine(u8"sub\\a.txt").exists(), u8"source moved away");
	should::is_equal(u8"alpha", read_test_text_file(root_path.combine(u8"moved.txt")), u8"moved file contents");

	const auto rename_result = state->get_commands().execute(u8"rename moved.txt renamed.txt");
	should::is_equal(true, rename_result.success, u8"rename succeeded");
	should::is_equal(true, root_path.combine(u8"renamed.txt").exists(), u8"renamed file exists");

	const auto delete_result = state->get_commands().execute(u8"rm renamed.txt");
	should::is_equal(true, delete_result.success, u8"delete succeeded");
	should::is_equal(false, root_path.combine(u8"renamed.txt").exists(), u8"deleted file missing");

	pf::platform_recycle_file(root_path);
}

static void should_parse_gemini_env_value()
{
	const auto env = u8"# comment\nGEMINI_API_KEY=abc123\nOTHER=x\n";
	should::is_equal(u8"abc123", agent_parse_env_value(env, u8"GEMINI_API_KEY"), u8"env key parsed");
}

static void should_normalize_gemini_paths()
{
	should::is_equal(u8"c:\\code\\rethinkify\\src\\app.cpp",
	                 arent_normalize_path(u8"C:/code/rethinkify/src/../src/app.cpp"),
	                 u8"normalized path");
}

static void should_restrict_gemini_to_root()
{
	should::is_equal(true, agent_is_within_root(u8"c:\\code\\rethinkify", u8"c:\\code\\rethinkify\\src\\app.cpp"),
	                 u8"inside root");
	should::is_equal(false, agent_is_within_root(u8"c:\\code\\rethinkify", u8"c:\\code\\other\\app.cpp"),
	                 u8"outside root");
}


tests::run_result run_all_tests_result()
{
	tests tests;

	// Document tests
	tests.register_test(u8"should insert chars", should_insert_single_chars);
	tests.register_test(u8"should split line", should_split_line);
	tests.register_test(u8"should combine line", should_combine_line);
	tests.register_test(u8"should delete chars", should_delete_chars);
	tests.register_test(u8"should delete selection", should_delete_selection);
	tests.register_test(u8"should delete 1 line selection", should_delete1_line_selection);
	tests.register_test(u8"should delete 2 line selection", should_delete2_line_selection);
	tests.register_test(u8"should insert selection", should_insert_selection);
	tests.register_test(u8"should insert crlf text", should_insert_crlf_text);
	tests.register_test(u8"should return selection", should_return_selection);
	tests.register_test(u8"should cut and paste", should_cut_and_paste);
	tests.register_test(u8"should calc pf::sha256", should_calc_sha256);

	// String utility tests
	tests.register_test(u8"should to_lower", should_to_lower);
	tests.register_test(u8"should unquote", should_unquote);
	tests.register_test(u8"should icmp", should_icmp);
	tests.register_test(u8"should find_in_text", should_find_in_text);
	tests.register_test(u8"should combine lines", should_combine_lines);
	tests.register_test(u8"should replace string", should_replace_string);
	tests.register_test(u8"should last_char", should_last_char);
	tests.register_test(u8"should is_empty", should_is_empty);

	// Geometry tests
	tests.register_test(u8"should pf::ipoint ops", should_ipoint_ops);
	tests.register_test(u8"should pf::isize ops", should_isize_ops);
	tests.register_test(u8"should pf::irect ops", should_irect_ops);

	// Encoding tests
	tests.register_test(u8"should hex roundtrip", should_hex_roundtrip);
	tests.register_test(u8"should base64 encode", should_base64_encode);
	tests.register_test(u8"should pf::aes256 roundtrip", should_aes256_roundtrip);

	// Misc utility tests
	tests.register_test(u8"should clamp value", should_clamp_value);
	tests.register_test(u8"should fnv1a hash", should_fnv1a_hash);
	tests.register_test(u8"should pf::file_path ops", should_file_path_ops);

	// Encoding detection tests (BOM)
	tests.register_test(u8"should detect UTF-8 BOM", should_detect_utf8_bom);
	tests.register_test(u8"should detect UTF-16 LE BOM", should_detect_utf16le_bom);
	tests.register_test(u8"should detect UTF-16 BE BOM", should_detect_utf16be_bom);
	tests.register_test(u8"should detect UTF-32 LE BOM", should_detect_utf32le_bom);
	tests.register_test(u8"should detect UTF-32 BE BOM", should_detect_utf32be_bom);
	tests.register_test(u8"should detect UTF-16 LE no BOM", should_detect_utf16le_no_bom);
	tests.register_test(u8"should detect UTF-16 BE no BOM", should_detect_utf16be_no_bom);
	tests.register_test(u8"should detect UTF-8 default", should_detect_utf8_default);
	tests.register_test(u8"should detect UTF-8 without BOM", should_detect_utf8_without_bom);
	tests.register_test(u8"should detect small files", should_detect_small_files);
	tests.register_test(u8"should prioritize UTF-32 over UTF-16", should_prioritize_utf32_over_utf16_bom);

	// Encoding conversion tests
	tests.register_test(u8"should UTF-8 to UTF-16 ASCII", should_utf8_to_utf16_ascii);
	tests.register_test(u8"should UTF-8 to UTF-16 multibyte", should_utf8_to_utf16_multibyte);
	tests.register_test(u8"should UTF-16 to UTF-8 roundtrip", should_utf16_to_utf8_roundtrip);
	tests.register_test(u8"should UTF-8 to UTF-16 mixed", should_utf8_to_utf16_mixed);
	tests.register_test(u8"should UTF-8 to UTF-16 various symbols", should_utf8_to_utf16_various_symbols);

	// Line ending detection tests
	tests.register_test(u8"should detect CRLF line endings", should_detect_crlf_line_endings);
	tests.register_test(u8"should detect LF line endings", should_detect_lf_line_endings);
	tests.register_test(u8"should detect LFCR line endings", should_detect_lfcr_line_endings);

	// Markdown tests
	tests.register_test(u8"should md highlight heading", should_md_highlight_heading);
	tests.register_test(u8"should md highlight bold", should_md_highlight_bold);
	tests.register_test(u8"should md highlight italic", should_md_highlight_italic);
	tests.register_test(u8"should md highlight link", should_md_highlight_link);
	tests.register_test(u8"should md highlight list", should_md_highlight_list);

	// App state tests
	tests.register_test(u8"should app_state new_doc is markdown", should_app_state_new_doc_is_markdown);
	tests.register_test(u8"should app_state is_markdown_path", should_app_state_is_markdown_path);
	tests.register_test(u8"should create_new_file with content", should_create_new_file_with_content);
	tests.register_test(u8"should create_new_file added to tree", should_create_new_file_added_to_tree);
	tests.register_test(u8"should create_new_file with unique name", should_create_new_file_with_unique_name);
	tests.register_test(u8"should restore per-document view mode", should_restore_per_document_view_mode);
	tests.register_test(u8"should create multiple new files", should_create_multiple_new_files);
	tests.register_test(u8"should create_new_file sorted in tree", should_create_new_file_sorted_in_tree);
	tests.register_test(u8"should create_new_folder with unique name", should_create_new_folder_with_unique_name);
	tests.register_test(u8"should refresh_index preserve unsaved doc folder",
	                    should_refresh_index_preserve_unsaved_doc_folder);
	tests.register_test(u8"should remember recent root folders most recent first",
	                    should_remember_recent_root_folders_most_recent_first);
	tests.register_test(u8"should cap recent root folders at eight",
	                    should_cap_recent_root_folders_at_eight);
	tests.register_test(u8"should restore last open file when switching recent root folder",
	                    should_restore_last_open_file_when_switching_recent_root_folder);
	tests.register_test(u8"should doc is_json", should_doc_is_json);
	tests.register_test(u8"should doc sort_remove_duplicates", should_doc_sort_remove_duplicates);
	tests.register_test(u8"should doc reformat_json", should_doc_reformat_json);
	tests.register_test(u8"should undo back to clean", should_undo_back_to_clean);
	tests.register_test(u8"should undo multiple to clean", should_undo_multiple_to_clean);

	// Search tests
	tests.register_test(u8"should search doc basic", should_search_doc_basic);
	tests.register_test(u8"should search doc match positions", should_search_doc_match_positions);
	tests.register_test(u8"should search doc case insensitive", should_search_doc_case_insensitive);
	tests.register_test(u8"should search doc empty clears", should_search_doc_empty_clears);
	tests.register_test(u8"should search multiple files", should_search_multiple_files);
	tests.register_test(u8"should search no match", should_search_no_match);

	// Command system tests
	tests.register_test(u8"should commands tokenize simple", should_commands_tokenize_simple);
	tests.register_test(u8"should commands quoted args", should_commands_quoted_args);
	tests.register_test(u8"should commands unknown command", should_commands_unknown_command);
	tests.register_test(u8"should commands short and long", should_commands_short_and_long);
	tests.register_test(u8"should commands help text", should_commands_help_text);
	tests.register_test(u8"should commands tokenize redirection operators",
	                    should_commands_tokenize_redirection_operators);
	tests.register_test(u8"should filter commands by availability", should_filter_commands_by_availability);
	tests.register_test(u8"should reject commands outside context", should_reject_commands_outside_context);
	tests.register_test(u8"should summarize command be console only", should_summarize_command_be_console_only);
	tests.register_test(u8"should calc expression", should_calc_expression);
	tests.register_test(u8"should echo redirection append to file", should_echo_redirection_append_to_file);
	tests.register_test(u8"should save and open new file at requested path",
	                    should_save_and_open_new_file_at_requested_path);
	tests.register_test(u8"should copy move rename and delete in sandbox",
	                    should_copy_move_rename_and_delete_in_sandbox);
	tests.register_test(u8"should parse gemini env value", should_parse_gemini_env_value);
	tests.register_test(u8"should normalize gemini paths", should_normalize_gemini_paths);
	tests.register_test(u8"should restrict gemini to root", should_restrict_gemini_to_root);

	auto result = tests.run_all_result();
	result.output = u8"# Test results\n\n" + result.output;
	return result;
}

std::u8string run_all_tests()
{
	return run_all_tests_result().output;
}
