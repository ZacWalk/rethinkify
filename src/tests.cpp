#include "pch.h"
#include "document.h"
#include "should.h"


class view_stub : public IView
{
public:
	std::wstring text_from_clipboard() const override
	{
		return L"test text";
	}

	bool text_to_clipboard(std::wstring_view text) override
	{
		return true;
	}

	static void scroll(int dx, int dy)
	{
	}

	void ensure_visible(const text_location& pt) override
	{
	}

	void invalidate_lines(int start, int end) override
	{
	}
};

static view_stub null_view;

static void insert_chars(document& doc, std::wstring_view chars, text_location location = text_location(0, 0))
{
	undo_group ug(doc);

	for (const auto c : chars)
	{
		location = doc.insert_text(ug, location, c);
	}
}

static void should_insert_single_chars()
{
	const auto text1 = L"Hello\nWorld";
	const auto text2 = L"Line\n";

	document doc(null_view);
	insert_chars(doc, text1);
	should::is_equal(text1, doc.str());

	insert_chars(doc, text2);
	should::is_equal(std::wstring(text2) + text1, doc.str());
}

static void should_split_line()
{
	const auto text = L"line of text";
	const auto expected = L"line\n of text";

	document doc(null_view, text);

	{
		undo_group ug(doc);
		doc.insert_text(ug, text_location(4, 0), L'\n');
		should::is_equal(expected, doc.str());
	}

	doc.undo();
	should::is_equal(text, doc.str(), L"undo");

	doc.redo();
	should::is_equal(expected, doc.str(), L"redo");
}

static void should_combine_line()
{
	const auto text = L"line \nof text";
	const auto expected = L"line of text";

	document doc(null_view, text);

	{
		undo_group ug(doc);
		doc.delete_text(ug, text_location(0, 1));
		should::is_equal(expected, doc.str());
	}

	doc.undo();
	should::is_equal(text, doc.str(), L"undo");

	doc.redo();
	should::is_equal(expected, doc.str(), L"redo");
}

static void should_delete_chars()
{
	const auto text = L"one\ntwo\nthree";
	const auto expected = L"oe\nto\ntree";

	document doc(null_view, text);

	{
		undo_group ug(doc);
		doc.delete_text(ug, text_location(2, 0));
		doc.delete_text(ug, text_location(2, 1));
		doc.delete_text(ug, text_location(2, 2));
		should::is_equal(expected, doc.str());
	}

	doc.undo();
	should::is_equal(text, doc.str(), L"undo");

	doc.redo();
	should::is_equal(expected, doc.str(), L"redo");
}

static void should_delete_selection()
{
	const auto text = L"line of text";
	const auto expected = L"lixt";

	document doc(null_view, text);

	{
		undo_group ug(doc);
		doc.delete_text(ug, text_selection(2, 0, 10, 0));
		should::is_equal(expected, doc.str());
	}

	doc.undo();
	should::is_equal(text, doc.str(), L"undo");

	doc.redo();
	should::is_equal(expected, doc.str(), L"redo");
}

static void should_delete2_line_selection()
{
	const auto text = L"one\ntwo\nthree";
	const auto expected = L"onree";

	document doc(null_view, text);

	{
		undo_group ug(doc);
		doc.delete_text(ug, text_selection(2, 0, 2, 2));
		should::is_equal(expected, doc.str());
	}

	doc.undo();
	should::is_equal(text, doc.str(), L"undo");

	doc.redo();
	should::is_equal(expected, doc.str(), L"redo");
}

static void should_delete1_line_selection()
{
	const auto text = L"one\ntwo\nthree";
	const auto expected = L"on";

	document doc(null_view, text);

	{
		undo_group ug(doc);
		doc.delete_text(ug, text_selection(2, 1, 2, 2));

		should::is_equal(L"one\ntwree", doc.str());

		doc.delete_text(ug, text_selection(2, 0, 5, 1));
		should::is_equal(expected, doc.str());
	}

	doc.undo();
	should::is_equal(text, doc.str(), L"undo");

	doc.redo();
	should::is_equal(expected, doc.str(), L"redo");
}

static void should_insert_selection()
{
	const auto text = L"line of text";
	const auto selection = L"one\ntwo\nthree";
	const auto expected = L"line oone\ntwo\nthreef text";

	document doc(null_view, text);

	{
		undo_group ug(doc);
		doc.insert_text(ug, text_location(6, 0), selection);
		should::is_equal(expected, doc.str());
	}

	doc.undo();
	should::is_equal(text, doc.str(), L"undo");

	doc.redo();
	should::is_equal(expected, doc.str(), L"redo");
}

static void should_return_selection()
{
	const document doc(null_view, L"one\ntwo\nthree");
	should::is_equal(L"e\ntwo\nth", str::combine(doc.text(text_selection(2, 0, 2, 2))));
}

static void should_find()
{
	document doc(null_view, L"one\ntwo\nthree");

	doc.find(L"ne", 0);
	should::is_equal(3, doc.cursor_pos().x);

	doc.find(L"ne", 0);
	should::is_equal(3, doc.cursor_pos().x);
}

static void should_cut_and_paste()
{
	const auto text = L"one\ntwo\nthree";
	const auto expected = L"one\none\ntwo\nthreetwo\nthree";

	document doc(null_view, text);

	{
		undo_group ug(doc);
		const auto copy = doc.str();
		doc.insert_text(ug, text_location(0, 1), copy);
		should::is_equal(expected, doc.str());
	}

	doc.undo();
	should::is_equal(text, doc.str(), L"undo");

	doc.redo();
	should::is_equal(expected, doc.str(), L"redo");
}

static void should_calc_sha256()
{
	const auto text = "hello world";
	const auto output = calc_sha256(text);
	should::is_equal(L"b94d27b9934d3e08a52e52d7da7dabfac484efe37a5380ee9088f7ace2efcde9", to_hex(output));
}

std::wstring run_all_tests()
{
	tests tests;

	tests.register_test(L"should insert chars", should_insert_single_chars);
	tests.register_test(L"should split line", should_split_line);
	tests.register_test(L"should combine line", should_combine_line);
	tests.register_test(L"should delete chars", should_delete_chars);
	tests.register_test(L"should delete selection", should_delete_selection);
	tests.register_test(L"should delete 1 line selection", should_delete1_line_selection);
	tests.register_test(L"should delete 2 line selection", should_delete2_line_selection);
	tests.register_test(L"should insert selection", should_insert_selection);
	tests.register_test(L"should return selection", should_return_selection);
	tests.register_test(L"should cut and paste", should_cut_and_paste);
	tests.register_test(L"should find", should_find);
	tests.register_test(L"should calc sha256", should_calc_sha256);

	std::wstringstream output;
	output << "# Test results" << std::endl << std::endl;
	tests.run_all(output);
	return output.str();
}
