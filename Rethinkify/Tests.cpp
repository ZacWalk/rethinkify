#include "pch.h"
#include "text_buffer.h"
#include "should.h"

static void InsertChars(text_buffer &buffer, const wchar_t *chars, text_location location = text_location(0, 0))
{
	auto p = chars;
	undo_group ug(buffer);

	while (*p)
	{
		location = buffer.insert_text(ug, location, *p);
		p++;
	}
}

static void ShouldInsertSingleChars()
{
	auto text1 = L"Hello\nWorld";
	auto text2 = L"Line\n";

	text_buffer buffer;
	InsertChars(buffer, text1);
	should::Equal(text1, buffer.str());

	InsertChars(buffer, text2);
	should::Equal(std::wstring(text2) + text1, buffer.str());
}

static void ShouldSplitLine()
{
	auto text = L"line of text";
	auto expected = L"line\n of text";

	text_buffer buffer(text);

	{
		undo_group ug(buffer);
		buffer.insert_text(ug, text_location(4, 0), L'\n');
		should::Equal(expected, buffer.str());
	}

	buffer.undo();
	should::Equal(text, buffer.str(), L"undo");

	buffer.redo();
	should::Equal(expected, buffer.str(), L"redo");
}

static void ShouldCombineLine()
{	
	auto text = L"line \nof text";
	auto expected = L"line of text";

	text_buffer buffer(text);

	{
		undo_group ug(buffer);
		buffer.delete_text(ug, text_location(0, 1));
		should::Equal(expected, buffer.str());
	}

	buffer.undo();
	should::Equal(text, buffer.str(), L"undo");

	buffer.redo();
	should::Equal(expected, buffer.str(), L"redo");
}

static void ShouldDeleteChars()
{
	auto text = L"one\ntwo\nthree";
	auto expected = L"oe\nto\ntree";

	text_buffer buffer(text);

	{
		undo_group ug(buffer);
		buffer.delete_text(ug, text_location(2, 0));
		buffer.delete_text(ug, text_location(2, 1));
		buffer.delete_text(ug, text_location(2, 2));
		should::Equal(expected, buffer.str());
	}

	buffer.undo();
	should::Equal(text, buffer.str(), L"undo");

	buffer.redo();
	should::Equal(expected, buffer.str(), L"redo");
}

static void ShouldDeleteSelection()
{
	auto text = L"line of text";
	auto expected = L"lixt";

	text_buffer buffer(text);

	{
		undo_group ug(buffer);
		buffer.delete_text(ug, text_selection(2, 0, 10, 0));
		should::Equal(expected, buffer.str());
	}

	buffer.undo();
	should::Equal(text, buffer.str(), L"undo");

	buffer.redo();
	should::Equal(expected, buffer.str(), L"redo");
}

static void ShouldDelete2LineSelection()
{
	auto text = L"one\ntwo\nthree";
	auto expected = L"onree";

	text_buffer buffer(text);

	{
		undo_group ug(buffer);
		buffer.delete_text(ug, text_selection(2, 0, 2, 2));
		should::Equal(expected, buffer.str());
	}

	buffer.undo();
	should::Equal(text, buffer.str(), L"undo");

	buffer.redo();
	should::Equal(expected, buffer.str(), L"redo");
}

static void ShouldDelete1LineSelection()
{
	auto text = L"one\ntwo\nthree";
	auto expected = L"on";

	text_buffer buffer(text);

	{
		undo_group ug(buffer);
		buffer.delete_text(ug, text_selection(2, 1, 2, 2));

		should::Equal(L"one\ntwree", buffer.str());

		buffer.delete_text(ug, text_selection(2, 0, 5, 1));
		should::Equal(expected, buffer.str());
	}

	buffer.undo();
	should::Equal(text, buffer.str(), L"undo");

	buffer.redo();
	should::Equal(expected, buffer.str(), L"redo");
}

static void ShouldInsertSelection()
{
	auto text = L"line of text";
	auto selection = L"one\ntwo\nthree";
	auto expected = L"line oone\ntwo\nthreef text";

	text_buffer buffer(text);

	{
		undo_group ug(buffer);
		buffer.insert_text(ug, text_location(6, 0), selection);
		should::Equal(expected, buffer.str());
	}

	buffer.undo();
	should::Equal(text, buffer.str(), L"undo");

	buffer.redo();
	should::Equal(expected, buffer.str(), L"redo");
}

static void ShouldReturnSelection()
{
	text_buffer buffer(L"one\ntwo\nthree");
	should::Equal(L"e\ntwo\nth", Combine(buffer.text(text_selection(2, 0, 2, 2))));
}

static void ShouldCutAndPaste()
{
	auto text = L"one\ntwo\nthree";
	auto expected = L"one\none\ntwo\nthreetwo\nthree";

	text_buffer buffer(text);

	{
		undo_group ug(buffer);
		auto copy = Combine(buffer.text());
		buffer.insert_text(ug, text_location(0, 1), copy);
		should::Equal(expected, buffer.str());
	}

	buffer.undo();
	should::Equal(text, buffer.str(), L"undo");

	buffer.redo();
	should::Equal(expected, buffer.str(), L"redo");
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