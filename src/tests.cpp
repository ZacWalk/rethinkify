#include "pch.h"
#include "document.h"
#include "should.h"


class view_stub : public IView
{
public:

	std::wstring text_from_clipboard() const override
	{
		return L"test text";
	};

	bool text_to_clipboard(const std::wstring& text) override
	{
		return true;
	};

	void scroll(int dx, int dy) const { };

	void update_caret() override { };

	void recalc_horz_scrollbar() override { };

	void recalc_vert_scrollbar() override { };

	void invalidate_lines(int start, int end) override { };

	void invalidate_line(int index) override { };

	void invalidate_view() override { };

	void layout() override { };

	void ensure_visible(const text_location& pt) override { };
};

static view_stub null_view;

static void insert_chars(document& doc, const wchar_t* chars, text_location location = text_location(0, 0))
{
	auto p = chars;
	undo_group ug(doc);

	while (*p)
	{
		location = doc.insert_text(ug, location, *p);
		p++;
	}
}

static void ShouldInsertSingleChars()
{
	auto text1 = L"Hello\nWorld";
	auto text2 = L"Line\n";

	document doc(null_view);
	insert_chars(doc, text1);
	should::Equal(text1, doc.str());

	insert_chars(doc, text2);
	should::Equal(std::wstring(text2) + text1, doc.str());
}

static void ShouldSplitLine()
{
	auto text = L"line of text";
	auto expected = L"line\n of text";

	document doc(null_view, text);

	{
		undo_group ug(doc);
		doc.insert_text(ug, text_location(4, 0), L'\n');
		should::Equal(expected, doc.str());
	}

	doc.undo();
	should::Equal(text, doc.str(), L"undo");

	doc.redo();
	should::Equal(expected, doc.str(), L"redo");
}

static void ShouldCombineLine()
{
	auto text = L"line \nof text";
	auto expected = L"line of text";

	document doc(null_view, text);

	{
		undo_group ug(doc);
		doc.delete_text(ug, text_location(0, 1));
		should::Equal(expected, doc.str());
	}

	doc.undo();
	should::Equal(text, doc.str(), L"undo");

	doc.redo();
	should::Equal(expected, doc.str(), L"redo");
}

static void ShouldDeleteChars()
{
	auto text = L"one\ntwo\nthree";
	auto expected = L"oe\nto\ntree";

	document doc(null_view, text);

	{
		undo_group ug(doc);
		doc.delete_text(ug, text_location(2, 0));
		doc.delete_text(ug, text_location(2, 1));
		doc.delete_text(ug, text_location(2, 2));
		should::Equal(expected, doc.str());
	}

	doc.undo();
	should::Equal(text, doc.str(), L"undo");

	doc.redo();
	should::Equal(expected, doc.str(), L"redo");
}

static void ShouldDeleteSelection()
{
	auto text = L"line of text";
	auto expected = L"lixt";

	document doc(null_view, text);

	{
		undo_group ug(doc);
		doc.delete_text(ug, text_selection(2, 0, 10, 0));
		should::Equal(expected, doc.str());
	}

	doc.undo();
	should::Equal(text, doc.str(), L"undo");

	doc.redo();
	should::Equal(expected, doc.str(), L"redo");
}

static void ShouldDelete2LineSelection()
{
	auto text = L"one\ntwo\nthree";
	auto expected = L"onree";

	document doc(null_view, text);

	{
		undo_group ug(doc);
		doc.delete_text(ug, text_selection(2, 0, 2, 2));
		should::Equal(expected, doc.str());
	}

	doc.undo();
	should::Equal(text, doc.str(), L"undo");

	doc.redo();
	should::Equal(expected, doc.str(), L"redo");
}

static void ShouldDelete1LineSelection()
{
	auto text = L"one\ntwo\nthree";
	auto expected = L"on";

	document doc(null_view, text);

	{
		undo_group ug(doc);
		doc.delete_text(ug, text_selection(2, 1, 2, 2));

		should::Equal(L"one\ntwree", doc.str());

		doc.delete_text(ug, text_selection(2, 0, 5, 1));
		should::Equal(expected, doc.str());
	}

	doc.undo();
	should::Equal(text, doc.str(), L"undo");

	doc.redo();
	should::Equal(expected, doc.str(), L"redo");
}

static void ShouldInsertSelection()
{
	auto text = L"line of text";
	auto selection = L"one\ntwo\nthree";
	auto expected = L"line oone\ntwo\nthreef text";

	document doc(null_view, text);

	{
		undo_group ug(doc);
		doc.insert_text(ug, text_location(6, 0), selection);
		should::Equal(expected, doc.str());
	}

	doc.undo();
	should::Equal(text, doc.str(), L"undo");

	doc.redo();
	should::Equal(expected, doc.str(), L"redo");
}

static void ShouldReturnSelection()
{
	document doc(null_view, L"one\ntwo\nthree");
	should::Equal(L"e\ntwo\nth", Combine(doc.text(text_selection(2, 0, 2, 2))));
}

static void ShouldCutAndPaste()
{
	auto text = L"one\ntwo\nthree";
	auto expected = L"one\none\ntwo\nthreetwo\nthree";

	document doc(null_view, text);

	{
		undo_group ug(doc);
		auto copy = Combine(doc.text());
		doc.insert_text(ug, text_location(0, 1), copy);
		should::Equal(expected, doc.str());
	}

	doc.undo();
	should::Equal(text, doc.str(), L"undo");

	doc.redo();
	should::Equal(expected, doc.str(), L"redo");
}


std::wstring RunTests()
{
	tests tests;

	tests.Register(L"should insert chars", ShouldInsertSingleChars);
	tests.Register(L"should split line", ShouldSplitLine);
	tests.Register(L"should combine line", ShouldCombineLine);
	tests.Register(L"should delete chars", ShouldDeleteChars);
	tests.Register(L"should delete selection", ShouldDeleteSelection);
	tests.Register(L"should delete 1 line selection", ShouldDelete1LineSelection);
	tests.Register(L"should delete 2 line selection", ShouldDelete2LineSelection);
	tests.Register(L"should insert selection", ShouldInsertSelection);
	tests.Register(L"should return selection", ShouldReturnSelection);
	tests.Register(L"should cut and paste", ShouldCutAndPaste);

	std::wstringstream output;
	tests.Run(output);
	return output.str();
}
