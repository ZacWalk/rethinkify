// tests.cpp — Unit tests for document editing, undo/redo, search, and crypto

#include "pch.h"
#include "app.h"
#include "document.h"
#include "app_state.h"
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

static void insert_chars(const document_ptr& doc, const std::wstring_view chars,
                         text_location location = text_location(0, 0))
{
	undo_group ug(doc);

	for (const auto c : chars)
	{
		location = doc->insert_text(ug, location, c);
	}
}

static void test_edit_undo_redo(const wchar_t* initial, const wchar_t* expected,
                                const std::function<void(const document_ptr&, undo_group&)>& edit)
{
	const auto doc = std::make_shared<document>(null_ev, initial);
	{
		undo_group ug(doc);
		edit(doc, ug);
		should::is_equal(expected, doc->str());
	}
	doc->undo();
	should::is_equal(initial, doc->str(), L"undo");
	doc->redo();
	should::is_equal(expected, doc->str(), L"redo");
}

static void should_insert_single_chars()
{
	const auto text1 = L"Hello\nWorld";
	const auto text2 = L"Line\n";


	const auto doc = std::make_shared<document>(null_ev);

	insert_chars(doc, text1);
	should::is_equal(text1, doc->str());

	insert_chars(doc, text2);
	should::is_equal(std::wstring(text2) + text1, doc->str());
}

static void should_split_line()
{
	test_edit_undo_redo(L"line of text", L"line\n of text", [](const document_ptr& doc, undo_group& ug)
	{
		doc->insert_text(ug, text_location(4, 0), L'\n');
	});
}

static void should_combine_line()
{
	test_edit_undo_redo(L"line \nof text", L"line of text", [](const document_ptr& doc, undo_group& ug)
	{
		doc->delete_text(ug, text_location(0, 1));
	});
}

static void should_delete_chars()
{
	test_edit_undo_redo(L"one\ntwo\nthree", L"oe\nto\ntree", [](const document_ptr& doc, undo_group& ug)
	{
		doc->delete_text(ug, text_location(2, 0));
		doc->delete_text(ug, text_location(2, 1));
		doc->delete_text(ug, text_location(2, 2));
	});
}

static void should_delete_selection()
{
	test_edit_undo_redo(L"line of text", L"lixt", [](const document_ptr& doc, undo_group& ug)
	{
		doc->delete_text(ug, text_selection(2, 0, 10, 0));
	});
}

static void should_delete2_line_selection()
{
	test_edit_undo_redo(L"one\ntwo\nthree", L"onree", [](const document_ptr& doc, undo_group& ug)
	{
		doc->delete_text(ug, text_selection(2, 0, 2, 2));
	});
}

static void should_delete1_line_selection()
{
	test_edit_undo_redo(L"one\ntwo\nthree", L"on", [](const document_ptr& doc, undo_group& ug)
	{
		doc->delete_text(ug, text_selection(2, 1, 2, 2));
		should::is_equal(L"one\ntwree", doc->str());
		doc->delete_text(ug, text_selection(2, 0, 5, 1));
	});
}

static void should_insert_selection()
{
	test_edit_undo_redo(L"line of text", L"line oone\ntwo\nthreef text", [](const document_ptr& doc, undo_group& ug)
	{
		doc->insert_text(ug, text_location(6, 0), L"one\ntwo\nthree");
	});
}

static void should_insert_crlf_text()
{
	test_edit_undo_redo(L"ab", L"aone\ntwo\nthreeb", [](const document_ptr& doc, undo_group& ug)
	{
		doc->insert_text(ug, text_location(1, 0), L"one\r\ntwo\r\nthree");
	});
}

static void should_return_selection()
{
	const auto doc = std::make_shared<document>(null_ev, L"one\ntwo\nthree");

	should::is_equal(L"e\ntwo\nth", str::combine(doc->text(text_selection(2, 0, 2, 2))));
}

static void should_cut_and_paste()
{
	test_edit_undo_redo(L"one\ntwo\nthree", L"one\none\ntwo\nthreetwo\nthree",
	                    [](const document_ptr& doc, undo_group& ug)
	                    {
		                    doc->insert_text(ug, text_location(0, 1), doc->str());
	                    });
}

static void should_calc_sha256()
{
	const auto text = "hello world";
	const auto output = calc_sha256(text);
	should::is_equal(L"b94d27b9934d3e08a52e52d7da7dabfac484efe37a5380ee9088f7ace2efcde9", to_hex(output));
}

// ── util.h string tests ────────────────────────────────────────────────────────

static void should_to_lower()
{
	should::is_equal(L'a', str::to_lower(L'A'));
	should::is_equal(L'z', str::to_lower(L'Z'));
	should::is_equal(L'a', str::to_lower(L'a'));
	should::is_equal(L'5', str::to_lower(L'5'));
}

static void should_unquote()
{
	should::is_equal(L"hello", str::unquote(L"\"hello\""));
	should::is_equal(L"hello", str::unquote(L"'hello'"));
	should::is_equal(L"hello", str::unquote(L"hello"));
	should::is_equal(L"", str::unquote(L""));
}

static void should_icmp()
{
	should::is_equal(0, str::icmp(L"Hello", L"hello"));
	should::is_equal(0, str::icmp(L"", L""));
	should::is_equal_true(str::icmp(L"abc", L"def") < 0);
	should::is_equal_true(str::icmp(L"def", L"abc") > 0);
	should::is_equal_true(str::icmp(L"ab", L"abc") < 0);
	should::is_equal_true(str::icmp(L"abc", L"ab") > 0);
	should::is_equal(-1, str::icmp(L"", L"a"));
	should::is_equal(1, str::icmp(L"a", L""));
}

static void should_find_in_text()
{
	should::is_equal(0, static_cast<int>(str::find_in_text(L"Hello World", L"hello")));
	should::is_equal(6, static_cast<int>(str::find_in_text(L"Hello World", L"world")));
	should::is_equal(static_cast<int>(std::wstring_view::npos),
	                 static_cast<int>(str::find_in_text(L"Hello", L"xyz")));
	should::is_equal(static_cast<int>(std::wstring_view::npos),
	                 static_cast<int>(str::find_in_text(L"", L"abc")));
	should::is_equal(static_cast<int>(std::wstring_view::npos),
	                 static_cast<int>(str::find_in_text(L"abc", L"")));
}

static void should_combine_lines()
{
	const std::vector<std::wstring> lines = {L"one", L"two", L"three"};
	should::is_equal(L"one\ntwo\nthree", str::combine(lines));
	should::is_equal(L"one, two, three", str::combine(lines, L", "));

	const std::vector<std::wstring> single = {L"only"};
	should::is_equal(L"only", str::combine(single));
}

static void should_replace_string()
{
	should::is_equal(L"hello world", str::replace(L"hello there", L"there", L"world"));
	should::is_equal(L"aXbXc", str::replace(L"a.b.c", L".", L"X"));
	should::is_equal(L"unchanged", str::replace(L"unchanged", L"xyz", L"abc"));
}

static void should_last_char()
{
	should::is_equal(L'd', str::last_char(L"abcd"));
	should::is_equal(0, str::last_char(L""));
}

static void should_is_empty()
{
	should::is_equal_true(str::is_empty(nullptr));
	should::is_equal_true(str::is_empty(L""));
	should::is_equal(false, str::is_empty(L"x"));
}

// ── util.h geometry tests ──────────────────────────────────────────────────────

static void should_ipoint_ops()
{
	constexpr ipoint a(3, 4);
	constexpr ipoint b(1, 2);
	constexpr auto sum = a + b;
	should::is_equal(4, sum.x);
	should::is_equal(6, sum.y);

	constexpr auto neg = -a;
	should::is_equal(-3, neg.x);
	should::is_equal(-4, neg.y);

	should::is_equal_true(ipoint(1, 2) == ipoint(1, 2));
	should::is_equal(false, ipoint(1, 2) == ipoint(3, 4));
}

static void should_isize_ops()
{
	should::is_equal_true(isize(10, 20) == isize(10, 20));
	should::is_equal(false, isize(10, 20) == isize(10, 21));
}

static void should_irect_ops()
{
	const irect r(10, 20, 110, 120);
	should::is_equal(100, r.Width());
	should::is_equal(100, r.Height());

	should::is_equal_true(r.Contains(ipoint(50, 50)));
	should::is_equal(false, r.Contains(ipoint(0, 0)));
	should::is_equal(false, r.Contains(ipoint(200, 200)));

	const auto offset = r.Offset(5, 10);
	should::is_equal(15, offset.left);
	should::is_equal(30, offset.top);

	const auto inflated = r.Inflate(2);
	should::is_equal(8, inflated.left);
	should::is_equal(18, inflated.top);
	should::is_equal(112, inflated.right);
	should::is_equal(122, inflated.bottom);

	const irect a(0, 0, 10, 10);
	const irect b(5, 5, 15, 15);
	const irect c(20, 20, 30, 30);
	should::is_equal_true(a.Intersects(b));
	should::is_equal(false, a.Intersects(c));
}

// ── util.h encoding tests ──────────────────────────────────────────────────────

static void should_hex_roundtrip()
{
	const std::vector<uint8_t> data = {0xDE, 0xAD, 0xBE, 0xEF};
	const auto hex = to_hex(data);
	should::is_equal(L"deadbeef", hex);

	const auto back = hex_to_data(hex);
	should::is_equal(static_cast<int>(data.size()), static_cast<int>(back.size()));
	for (size_t i = 0; i < data.size(); ++i)
		should::is_equal(data[i], back[i]);
}

static void should_base64_encode()
{
	const std::string text = "Hello";
	const std::vector<uint8_t> data(text.begin(), text.end());
	should::is_equal(L"SGVsbG8=", to_base64(data));

	std::vector<uint8_t> empty;
	should::is_equal(L"", to_base64(empty));
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
	should::is_equal(5, clamp(5, 0, 10));
	should::is_equal(0, clamp(-1, 0, 10));
	should::is_equal(10, clamp(15, 0, 10));
	should::is_equal(5, clamp(5, 5, 5));
}

static void should_fnv1a_hash()
{
	const auto h1 = fnv1a_i(L"hello");
	const auto h2 = fnv1a_i(L"HELLO");
	const auto h3 = fnv1a_i(L"world");
	should::is_equal(static_cast<int>(h1), static_cast<int>(h2));
	should::is_equal_true(h1 != h3);
}

// ── file_path tests ────────────────────────────────────────────────────────────

static void should_file_path_ops()
{
	should::is_equal(6, file_path::find_ext(L"readme.txt"));
	should::is_equal(4, file_path::find_ext(L"test.cpp"));
	should::is_equal(4, file_path::find_ext(L"none"));

	should::is_equal(4, file_path::find_last_slash(L"src/file.cpp"));
	should::is_equal(4, file_path::find_last_slash(L"src\\file.cpp"));

	const file_path p(L"C:\\code\\project");
	const auto combined = p.combine(L"file.txt");
	should::is_equal(L"C:\\code\\project\\file.txt", combined.view());

	should::is_equal_true(file_path::is_path_sep(L'/'));
	should::is_equal_true(file_path::is_path_sep(L'\\'));
	should::is_equal(false, file_path::is_path_sep(L'x'));
}

// ── encoding detection tests ───────────────────────────────────────────────────

static void check_encoding(const uint8_t* data, const size_t size, file_encoding expected_enc,
                           const int expected_header, const std::wstring_view msg)
{
	int headerLen = 0;
	const auto enc = detect_encoding(data, size, headerLen);
	should::is_equal(static_cast<int>(expected_enc), static_cast<int>(enc), msg);
	should::is_equal(expected_header, headerLen, std::wstring(msg) + L" header");
}

static void should_detect_utf8_bom()
{
	constexpr uint8_t data[] = {0xEF, 0xBB, 0xBF, 'H', 'e', 'l', 'l', 'o'};
	check_encoding(data, sizeof(data), file_encoding::utf8, 3, L"UTF-8 BOM");
}

static void should_detect_utf16le_bom()
{
	constexpr uint8_t data[] = {0xFF, 0xFE, 'H', 0x00, 'i', 0x00};
	check_encoding(data, sizeof(data), file_encoding::utf16, 2, L"UTF-16 LE BOM");
}

static void should_detect_utf16be_bom()
{
	constexpr uint8_t data[] = {0xFE, 0xFF, 0x00, 'H', 0x00, 'i'};
	check_encoding(data, sizeof(data), file_encoding::utf16be, 2, L"UTF-16 BE BOM");
}

static void should_detect_utf32le_bom()
{
	constexpr uint8_t data[] = {0xFF, 0xFE, 0x00, 0x00, 'H', 0x00, 0x00, 0x00};
	check_encoding(data, sizeof(data), file_encoding::utf32, 4, L"UTF-32 LE BOM");
}

static void should_detect_utf32be_bom()
{
	constexpr uint8_t data[] = {0x00, 0x00, 0xFE, 0xFF, 0x00, 0x00, 0x00, 'H'};
	check_encoding(data, sizeof(data), file_encoding::utf32be, 4, L"UTF-32 BE BOM");
}

static void should_detect_utf16le_no_bom()
{
	constexpr uint8_t data[] = {'H', 0x00, 'i', 0x00};
	check_encoding(data, sizeof(data), file_encoding::utf16, 0, L"UTF-16 LE no BOM");
}

static void should_detect_utf16be_no_bom()
{
	constexpr uint8_t data[] = {0x00, 'H', 0x00, 'i'};
	check_encoding(data, sizeof(data), file_encoding::utf16be, 0, L"UTF-16 BE no BOM");
}

static void should_detect_utf8_default()
{
	constexpr uint8_t data[] = {'H', 'e', 'l', 'l', 'o'};
	check_encoding(data, sizeof(data), file_encoding::utf8, 0, L"default UTF-8");
}

static void should_detect_utf8_without_bom()
{
	constexpr uint8_t data[] = {'H', 'e', 'l', 'l', 'o', ' ', 'W', 'o', 'r', 'l', 'd'};
	check_encoding(data, sizeof(data), file_encoding::utf8, 0, L"UTF-8 without BOM");
}

static void should_detect_small_files()
{
	constexpr uint8_t one[] = {'A'};
	check_encoding(one, 1, file_encoding::utf8, 0, L"1-byte file");

	constexpr uint8_t bom16[] = {0xFF, 0xFE};
	check_encoding(bom16, 2, file_encoding::utf16, 2, L"2-byte UTF-16 LE BOM");

	constexpr uint8_t bom8[] = {0xEF, 0xBB, 0xBF};
	check_encoding(bom8, 3, file_encoding::utf8, 3, L"3-byte UTF-8 BOM");
}

static void should_prioritize_utf32_over_utf16_bom()
{
	const uint8_t data[] = {0xFF, 0xFE, 0x00, 0x00, 0x41, 0x00, 0x00, 0x00};
	check_encoding(data, sizeof(data), file_encoding::utf32, 4, L"UTF-32 LE over UTF-16 LE");
}

// ── encoding conversion tests ──────────────────────────────────────────────────

static void should_utf8_to_utf16_ascii()
{
	// Pure ASCII round-trips through UTF-8 conversion
	should::is_equal(L"Hello", str::utf8_to_utf16("Hello"));
	should::is_equal(L"", str::utf8_to_utf16(""));
}

static void should_utf8_to_utf16_multibyte()
{
	// Em-dash U+2014 is encoded as E2 80 94 in UTF-8
	const std::string utf8_emdash = "\xE2\x80\x94";
	const auto result = str::utf8_to_utf16(utf8_emdash);
	should::is_equal(1, static_cast<int>(result.size()), L"em-dash length");
	should::is_equal(0x2014, result[0], L"em-dash codepoint");
}

static void should_utf16_to_utf8_roundtrip()
{
	// Em-dash U+2014 round-trip through UTF-16 -> UTF-8 -> UTF-16
	const std::wstring emdash(1, 0x2014);
	const auto utf8 = str::utf16_to_utf8(emdash);
	should::is_equal(3, static_cast<int>(utf8.size()), L"em-dash UTF-8 byte count");
	should::is_equal(0xE2, static_cast<uint8_t>(utf8[0]), L"em-dash byte 0");
	should::is_equal(0x80, static_cast<uint8_t>(utf8[1]), L"em-dash byte 1");
	should::is_equal(0x94, static_cast<uint8_t>(utf8[2]), L"em-dash byte 2");

	const auto back = str::utf8_to_utf16(utf8);
	should::is_equal(emdash, back, L"em-dash round-trip");
}

static void should_utf8_to_utf16_mixed()
{
	// Mixed ASCII and multi-byte: "key — value"
	// U+2014 em-dash = E2 80 94 in UTF-8
	const std::string utf8 = "key \xE2\x80\x94 value";
	const auto result = str::utf8_to_utf16(utf8);
	// Should be: 'k','e','y',' ', U+2014, ' ','v','a','l','u','e'
	should::is_equal(11, static_cast<int>(result.size()), L"mixed UTF-8 length");
	should::is_equal(L'k', result[0], L"mixed char 0");
	should::is_equal(0x2014, result[4], L"mixed em-dash");
	should::is_equal(L'v', result[6], L"mixed char after em-dash");
}

static void should_utf8_to_utf16_various_symbols()
{
	// Euro sign U+20AC = E2 82 AC
	should::is_equal(0x20AC,
	                 str::utf8_to_utf16("\xE2\x82\xAC")[0],
	                 L"euro sign");

	// Copyright U+00A9 = C2 A9
	should::is_equal(0x00A9,
	                 str::utf8_to_utf16("\xC2\xA9")[0],
	                 L"copyright sign");

	// Japanese Hiragana 'A' U+3042 = E3 81 82
	should::is_equal(0x3042,
	                 str::utf8_to_utf16("\xE3\x81\x82")[0],
	                 L"hiragana A");
}

// ── line ending detection tests ────────────────────────────────────────────────

static void check_line_ending(const uint8_t* data, const size_t size, line_endings expected,
                              const std::wstring_view msg)
{
	const auto le = detect_line_endings(data, static_cast<int>(size));
	should::is_equal(static_cast<int>(expected), static_cast<int>(le), msg);
}

static void should_detect_crlf_line_endings()
{
	constexpr uint8_t data[] = {'H', 'e', 'l', 'l', 'o', 0x0D, 0x0A, 'W', 'o', 'r', 'l', 'd'};
	check_line_ending(data, sizeof(data), line_endings::crlf_style_dos, L"CRLF detection");
}

static void should_detect_lf_line_endings()
{
	constexpr uint8_t data[] = {'H', 'e', 'l', 'l', 'o', 0x0A, 'W', 'o', 'r', 'l', 'd'};
	check_line_ending(data, sizeof(data), line_endings::crlf_style_unix, L"LF detection");
}

static void should_detect_lfcr_line_endings()
{
	constexpr uint8_t data[] = {'H', 'e', 'l', 'l', 'o', 0x0A, 0x0D, 'W', 'o', 'r', 'l', 'd'};
	check_line_ending(data, sizeof(data), line_endings::crlf_style_unix, L"LFCR detection");
}

static int test_md_parse(const wchar_t* text, text_block* blocks)
{
	int count = 0;
	const document_line line(std::wstring_view{text});
	const auto highlighter = select_highlighter(doc_type::markdown, {});
	highlighter(0, line, blocks, count);
	return count;
}

static void should_md_highlight_heading()
{
	text_block blocks[64];

	const auto h1_count = test_md_parse(L"# Hello", blocks);
	should::is_equal(2, h1_count, L"md h1 block count");
	should::is_equal(0, blocks[0]._char_pos, L"md h1 marker pos");
	should::is_equal(2, blocks[1]._char_pos, L"md h1 content pos");

	const auto h2_count = test_md_parse(L"## World", blocks);
	should::is_equal(2, h2_count, L"md h2 block count");
	should::is_equal(0, blocks[0]._char_pos, L"md h2 marker pos");
	should::is_equal(3, blocks[1]._char_pos, L"md h2 content pos");
}

static void should_md_highlight_bold()
{
	text_block blocks[64];
	const auto count = test_md_parse(L"some **bold** text", blocks);
	should::is_equal_true(count >= 3, L"md bold block count");
}

static void should_md_highlight_italic()
{
	text_block blocks[64];
	const auto count = test_md_parse(L"some *italic* text", blocks);
	should::is_equal_true(count >= 3, L"md italic block count");
}

static void should_md_highlight_link()
{
	text_block blocks[64];
	const auto count = test_md_parse(L"click [here](http://example.com) now", blocks);
	should::is_equal_true(count >= 5, L"md link block count");
}

static void should_md_highlight_list()
{
	text_block blocks[64];
	const auto count = test_md_parse(L"- list item", blocks);
	should::is_equal_true(count >= 2, L"md list block count");
	should::is_equal(0, blocks[0]._char_pos, L"md list bullet pos");
}

// ── app_state tests ────────────────────────────────────────────────────────────────────

static void should_app_state_new_doc()
{
	app_state state(null_ev);
	state.new_doc();
	should::is_equal(L"new-1.md", state.active_item()->path.view());

	state.new_doc();
	should::is_equal(L"new-2.md", state.active_item()->path.view());

	// New doc paths are not save paths
	should::is_equal(false, state.active_item()->path.is_save_path());
}


static void should_app_state_new_doc_is_markdown()
{
	app_state state(null_ev);
	state.new_doc();

	// new-1.md should be recognized as a markdown path
	should::is_equal_true(is_markdown_path(state.active_item()->path));

	// But it should open in edit mode (not preview), so view_type is markdown
	// but is_save_path is false (the caller uses this to decide edit vs preview)
	should::is_equal(false, state.active_item()->path.is_save_path());
}

static void should_app_state_is_markdown_path()
{
	should::is_equal_true(is_markdown_path(file_path{L"readme.md"}));
	should::is_equal_true(is_markdown_path(file_path{L"DOC.MARKDOWN"}));
	should::is_equal(false, is_markdown_path(file_path{L"code.cpp"}));
	should::is_equal(false, is_markdown_path(file_path{L"noext"}));
}

static void should_doc_is_json()
{
	const auto d1 = std::make_shared<document>(null_ev, L"{\"key\":\"value\"}");
	should::is_equal_true(d1->is_json());

	const auto d2 = std::make_shared<document>(null_ev, L"hello world");
	should::is_equal(false, d2->is_json());

	const auto d3 = std::make_shared<document>(null_ev, L"  \t{}");
	should::is_equal_true(d3->is_json());
}


static void should_doc_sort_remove_duplicates()
{
	const auto d = std::make_shared<document>(null_ev,
	                                          L"banana\napple\nbanana\ncherry\napple");
	d->sort_remove_duplicates();

	should::is_equal(L"apple\nbanana\ncherry", d->str());
}

static void should_doc_reformat_json()
{
	const auto d = std::make_shared<document>(null_ev, L"{\"a\":\"b\"}");
	d->reformat_json();

	// After reformat, the doc should contain formatted JSON with braces on separate lines
	const auto result = d->str();
	should::is_equal_true(result.find(L'{') != std::wstring::npos, L"json has open brace");
	should::is_equal_true(result.find(L'}') != std::wstring::npos, L"json has close brace");
	// The key-value should have spaces around colon
	should::is_equal_true(result.find(L" : ") != std::wstring::npos, L"json has spaced colon");
}

static void should_undo_back_to_clean()
{
	const auto d = std::make_shared<document>(null_ev, L"hello");
	should::is_equal(false, d->is_modified());

	// Make an edit
	{
		undo_group ug(d);
		d->insert_text(ug, text_location(5, 0), L" world");
	}
	should::is_equal_true(d->is_modified());

	// Undo → back to saved state
	d->edit_undo();
	should::is_equal(false, d->is_modified(), L"undo to clean");

	// Redo → modified again
	d->edit_redo();
	should::is_equal_true(d->is_modified(), L"redo is modified");

	// Undo again
	d->edit_undo();
	should::is_equal(false, d->is_modified(), L"undo again to clean");
}

static void should_undo_multiple_to_clean()
{
	const auto d = std::make_shared<document>(null_ev, L"abc");

	// Two edits
	{
		undo_group ug(d);
		d->insert_text(ug, text_location(3, 0), L"d");
	}
	{
		undo_group ug(d);
		d->insert_text(ug, text_location(4, 0), L"e");
	}
	should::is_equal_true(d->is_modified());

	// Undo one → still modified
	d->edit_undo();
	should::is_equal_true(d->is_modified(), L"undo one still modified");

	// Undo two → clean
	d->edit_undo();
	should::is_equal(false, d->is_modified(), L"undo two clean");
}


// ── Search model tests ─────────────────────────────────────────────────────────

static void should_search_doc_basic()
{
	app_state state(null_ev);

	const auto item = std::make_shared<index_item>(file_path{L"test.txt"}, L"test.txt", false,
	                                               std::make_shared<document>(
		                                               null_ev, L"hello world\ngoodbye world\nhello again"));

	const auto root = std::make_shared<index_item>(file_path{L"root"}, L"root", true);
	root->children.push_back(item);
	state.set_root(root);

	state.execute_search(L"hello");

	should::is_equal(2, static_cast<int>(item->search_results.size()), L"search result count");
	should::is_equal(0, item->search_results[0].line_number, L"first result line");
	should::is_equal(2, item->search_results[1].line_number, L"second result line");
}

static void should_search_doc_match_positions()
{
	app_state state(null_ev);

	const auto item = std::make_shared<index_item>(file_path{L"test.txt"}, L"test.txt", false,
	                                               std::make_shared<document>(null_ev, L"\tone two one"));

	const auto root = std::make_shared<index_item>(file_path{L"root"}, L"root", true);
	root->children.push_back(item);
	state.set_root(root);

	state.execute_search(L"one");

	should::is_equal(2, static_cast<int>(item->search_results.size()), L"match count");

	// First match at position 1 (after tab), trimmed text starts at 0
	should::is_equal(1, item->search_results[0].line_match_pos, L"first match pos");
	should::is_equal(0, item->search_results[0].text_match_start, L"first trimmed pos");
	should::is_equal(3, item->search_results[0].text_match_length, L"first match len");

	// Second match
	should::is_equal(9, item->search_results[1].line_match_pos, L"second match pos");
	should::is_equal(8, item->search_results[1].text_match_start, L"second trimmed pos");
}

static void should_search_doc_case_insensitive()
{
	app_state state(null_ev);

	const auto item = std::make_shared<index_item>(file_path{L"test.txt"}, L"test.txt", false,
	                                               std::make_shared<document>(null_ev, L"Hello HELLO hello"));

	const auto root = std::make_shared<index_item>(file_path{L"root"}, L"root", true);
	root->children.push_back(item);
	state.set_root(root);

	state.execute_search(L"hello");

	should::is_equal(3, static_cast<int>(item->search_results.size()), L"case insensitive count");
}

static void should_search_doc_empty_clears()
{
	app_state state(null_ev);

	const auto item = std::make_shared<index_item>(file_path{L"test.txt"}, L"test.txt", false,
	                                               std::make_shared<document>(null_ev, L"hello world"));

	const auto root = std::make_shared<index_item>(file_path{L"root"}, L"root", true);
	root->children.push_back(item);
	state.set_root(root);

	state.execute_search(L"hello");
	should::is_equal(1, static_cast<int>(item->search_results.size()), L"before clear");

	state.execute_search(L"");
	should::is_equal(0, static_cast<int>(item->search_results.size()), L"after clear");
}

static void should_search_multiple_files()
{
	app_state state(null_ev);

	const auto item1 = std::make_shared<index_item>(file_path{L"a.txt"}, L"a.txt", false,
	                                                std::make_shared<document>(null_ev, L"foo bar"));
	const auto item2 = std::make_shared<index_item>(file_path{L"b.txt"}, L"b.txt", false,
	                                                std::make_shared<document>(null_ev, L"bar baz"));

	const auto root = std::make_shared<index_item>(file_path{L"root"}, L"root", true);
	root->children.push_back(item1);
	root->children.push_back(item2);
	state.set_root(root);

	state.execute_search(L"bar");

	should::is_equal(1, static_cast<int>(item1->search_results.size()), L"file1 results");
	should::is_equal(1, static_cast<int>(item2->search_results.size()), L"file2 results");
}

static void should_search_no_match()
{
	app_state state(null_ev);

	const auto item = std::make_shared<index_item>(file_path{L"test.txt"}, L"test.txt", false,
	                                               std::make_shared<document>(null_ev, L"hello world"));

	const auto root = std::make_shared<index_item>(file_path{L"root"}, L"root", true);
	root->children.push_back(item);
	state.set_root(root);

	state.execute_search(L"xyz");

	should::is_equal(0, static_cast<int>(item->search_results.size()), L"no match");
}

// ── Command system tests ───────────────────────────────────────────────

static void should_commands_tokenize_simple()
{
	commands cmds;
	bool called = false;
	std::vector<std::wstring> received_args;

	cmds.set_commands({
		{{L"t", L"test"}, L"Test command", {}, 0, {}, nullptr, nullptr,
			[&](const std::vector<std::wstring>& args)
			{
				called = true;
				received_args = args;
				return command_result{L"ok", true};
			}
		},
	});

	const auto result = cmds.execute(L"t arg1 arg2");
	should::is_equal(true, called, L"command was called");
	should::is_equal(true, result.success, L"result success");
	should::is_equal(2, static_cast<int>(received_args.size()), L"arg count");
	should::is_equal(std::wstring_view(L"arg1"), std::wstring_view(received_args[0]), L"arg0");
	should::is_equal(std::wstring_view(L"arg2"), std::wstring_view(received_args[1]), L"arg1");
}

static void should_commands_quoted_args()
{
	commands cmds;
	std::vector<std::wstring> received_args;

	cmds.set_commands({
		{{L"e", L"echo"}, L"Echo", {}, 0, {}, nullptr, nullptr,
			[&](const std::vector<std::wstring>& args)
			{
				received_args = args;
				return command_result{L"ok", true};
			}
		},
	});

	cmds.execute(L"e \"hello world\" foo");
	should::is_equal(2, static_cast<int>(received_args.size()), L"quoted arg count");
	should::is_equal(std::wstring_view(L"hello world"), std::wstring_view(received_args[0]), L"quoted arg");
	should::is_equal(std::wstring_view(L"foo"), std::wstring_view(received_args[1]), L"second arg");
}

static void should_commands_unknown_command()
{
	commands cmds;
	cmds.set_commands({});

	const auto result = cmds.execute(L"bogus");
	should::is_equal(false, result.success, L"unknown command fails");
	should::is_equal(true, result.output.find(L"Unknown command") != std::wstring::npos, L"error message");
}

static void should_commands_short_and_long()
{
	commands cmds;
	int call_count = 0;

	cmds.set_commands({
		{{L"s", L"save"}, L"Save", {}, 0, {}, nullptr, nullptr,
			[&](const std::vector<std::wstring>&)
			{
				call_count++;
				return command_result{L"saved", true};
			}
		},
	});

	cmds.execute(L"s");
	cmds.execute(L"save");
	cmds.execute(L"SAVE");
	should::is_equal(3, call_count, L"both aliases work case-insensitively");
}

static void should_commands_help_text()
{
	commands cmds;

	cmds.set_commands({
		{{L"s", L"save"}, L"Save the file", {}, 0, {}, nullptr, nullptr,
			[](const std::vector<std::wstring>&)
			{
				return command_result{L"", true};
			}
		},
		{{L"q", L"exit"}, L"Exit app", {}, 0, {}, nullptr, nullptr,
			[](const std::vector<std::wstring>&)
			{
				return command_result{L"", true};
			}
		},
	});

	const auto text = cmds.help_text();
	should::is_equal(true, text.find(L"save") != std::wstring::npos, L"help contains save");
	should::is_equal(true, text.find(L"exit") != std::wstring::npos, L"help contains exit");
	should::is_equal(true, text.find(L"Save the file") != std::wstring::npos, L"help contains description");
}


std::wstring run_all_tests()
{
	tests tests;

	// Document tests
	tests.register_test(L"should insert chars", should_insert_single_chars);
	tests.register_test(L"should split line", should_split_line);
	tests.register_test(L"should combine line", should_combine_line);
	tests.register_test(L"should delete chars", should_delete_chars);
	tests.register_test(L"should delete selection", should_delete_selection);
	tests.register_test(L"should delete 1 line selection", should_delete1_line_selection);
	tests.register_test(L"should delete 2 line selection", should_delete2_line_selection);
	tests.register_test(L"should insert selection", should_insert_selection);
	tests.register_test(L"should insert crlf text", should_insert_crlf_text);
	tests.register_test(L"should return selection", should_return_selection);
	tests.register_test(L"should cut and paste", should_cut_and_paste);
	tests.register_test(L"should calc sha256", should_calc_sha256);

	// String utility tests
	tests.register_test(L"should to_lower", should_to_lower);
	tests.register_test(L"should unquote", should_unquote);
	tests.register_test(L"should icmp", should_icmp);
	tests.register_test(L"should find_in_text", should_find_in_text);
	tests.register_test(L"should combine lines", should_combine_lines);
	tests.register_test(L"should replace string", should_replace_string);
	tests.register_test(L"should last_char", should_last_char);
	tests.register_test(L"should is_empty", should_is_empty);

	// Geometry tests
	tests.register_test(L"should ipoint ops", should_ipoint_ops);
	tests.register_test(L"should isize ops", should_isize_ops);
	tests.register_test(L"should irect ops", should_irect_ops);

	// Encoding tests
	tests.register_test(L"should hex roundtrip", should_hex_roundtrip);
	tests.register_test(L"should base64 encode", should_base64_encode);
	tests.register_test(L"should aes256 roundtrip", should_aes256_roundtrip);

	// Misc utility tests
	tests.register_test(L"should clamp value", should_clamp_value);
	tests.register_test(L"should fnv1a hash", should_fnv1a_hash);
	tests.register_test(L"should file_path ops", should_file_path_ops);

	// Encoding detection tests (BOM)
	tests.register_test(L"should detect UTF-8 BOM", should_detect_utf8_bom);
	tests.register_test(L"should detect UTF-16 LE BOM", should_detect_utf16le_bom);
	tests.register_test(L"should detect UTF-16 BE BOM", should_detect_utf16be_bom);
	tests.register_test(L"should detect UTF-32 LE BOM", should_detect_utf32le_bom);
	tests.register_test(L"should detect UTF-32 BE BOM", should_detect_utf32be_bom);
	tests.register_test(L"should detect UTF-16 LE no BOM", should_detect_utf16le_no_bom);
	tests.register_test(L"should detect UTF-16 BE no BOM", should_detect_utf16be_no_bom);
	tests.register_test(L"should detect UTF-8 default", should_detect_utf8_default);
	tests.register_test(L"should detect UTF-8 without BOM", should_detect_utf8_without_bom);
	tests.register_test(L"should detect small files", should_detect_small_files);
	tests.register_test(L"should prioritize UTF-32 over UTF-16", should_prioritize_utf32_over_utf16_bom);

	// Encoding conversion tests
	tests.register_test(L"should UTF-8 to UTF-16 ASCII", should_utf8_to_utf16_ascii);
	tests.register_test(L"should UTF-8 to UTF-16 multibyte", should_utf8_to_utf16_multibyte);
	tests.register_test(L"should UTF-16 to UTF-8 roundtrip", should_utf16_to_utf8_roundtrip);
	tests.register_test(L"should UTF-8 to UTF-16 mixed", should_utf8_to_utf16_mixed);
	tests.register_test(L"should UTF-8 to UTF-16 various symbols", should_utf8_to_utf16_various_symbols);

	// Line ending detection tests
	tests.register_test(L"should detect CRLF line endings", should_detect_crlf_line_endings);
	tests.register_test(L"should detect LF line endings", should_detect_lf_line_endings);
	tests.register_test(L"should detect LFCR line endings", should_detect_lfcr_line_endings);

	// Markdown tests
	tests.register_test(L"should md highlight heading", should_md_highlight_heading);
	tests.register_test(L"should md highlight bold", should_md_highlight_bold);
	tests.register_test(L"should md highlight italic", should_md_highlight_italic);
	tests.register_test(L"should md highlight link", should_md_highlight_link);
	tests.register_test(L"should md highlight list", should_md_highlight_list);

	// App state tests
	tests.register_test(L"should app_state new_doc", should_app_state_new_doc);
	tests.register_test(L"should app_state new_doc is markdown", should_app_state_new_doc_is_markdown);
	tests.register_test(L"should app_state is_markdown_path", should_app_state_is_markdown_path);
	tests.register_test(L"should doc is_json", should_doc_is_json);
	tests.register_test(L"should doc sort_remove_duplicates", should_doc_sort_remove_duplicates);
	tests.register_test(L"should doc reformat_json", should_doc_reformat_json);
	tests.register_test(L"should undo back to clean", should_undo_back_to_clean);
	tests.register_test(L"should undo multiple to clean", should_undo_multiple_to_clean);

	// Search tests
	tests.register_test(L"should search doc basic", should_search_doc_basic);
	tests.register_test(L"should search doc match positions", should_search_doc_match_positions);
	tests.register_test(L"should search doc case insensitive", should_search_doc_case_insensitive);
	tests.register_test(L"should search doc empty clears", should_search_doc_empty_clears);
	tests.register_test(L"should search multiple files", should_search_multiple_files);
	tests.register_test(L"should search no match", should_search_no_match);

	// Command system tests
	tests.register_test(L"should commands tokenize simple", should_commands_tokenize_simple);
	tests.register_test(L"should commands quoted args", should_commands_quoted_args);
	tests.register_test(L"should commands unknown command", should_commands_unknown_command);
	tests.register_test(L"should commands short and long", should_commands_short_and_long);
	tests.register_test(L"should commands help text", should_commands_help_text);

	std::wstringstream output;
	output << "# Test results" << std::endl << std::endl;
	tests.run_all(output);
	return output.str();
}
