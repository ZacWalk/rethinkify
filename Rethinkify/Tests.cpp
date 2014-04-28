#include "pch.h"
#include "TextBuffer.h"
#include "Should.h"

static void InsertChars(TextBuffer &buffer, const wchar_t *chars, TextLocation location = TextLocation(0, 0))
{
	auto p = chars;
	UndoGroup ug(buffer);

	while (*p)
	{
		location = buffer.InsertText(ug, location, *p);
		p++;
	}
}

static void ShouldInsertSingleChars()
{
	auto text1 = L"Hello\nWorld";
	auto text2 = L"Line\n";

	TextBuffer buffer;
	InsertChars(buffer, text1);
	Should::Equal(text1, buffer.str());

	InsertChars(buffer, text2);
	Should::Equal(std::wstring(text2) + text1, buffer.str());
}

static void ShouldSplitLine()
{
	auto text = L"line of text";
	auto expected = L"line\n of text";

	TextBuffer buffer(text);

	{
		UndoGroup ug(buffer);
		buffer.InsertText(ug, TextLocation(4, 0), L'\n');
		Should::Equal(expected, buffer.str());
	}

	buffer.Undo();
	Should::Equal(text, buffer.str(), L"Undo");

	buffer.Redo();
	Should::Equal(expected, buffer.str(), L"Redo");
}

static void ShouldCombineLine()
{	
	auto text = L"line \nof text";
	auto expected = L"line of text";

	TextBuffer buffer(text);

	{
		UndoGroup ug(buffer);
		buffer.DeleteText(ug, TextLocation(0, 1));
		Should::Equal(expected, buffer.str());
	}

	buffer.Undo();
	Should::Equal(text, buffer.str(), L"Undo");

	buffer.Redo();
	Should::Equal(expected, buffer.str(), L"Redo");
}

static void ShouldDeleteChars()
{
	auto text = L"one\ntwo\nthree";
	auto expected = L"oe\nto\ntree";

	TextBuffer buffer(text);

	{
		UndoGroup ug(buffer);
		buffer.DeleteText(ug, TextLocation(2, 0));
		buffer.DeleteText(ug, TextLocation(2, 1));
		buffer.DeleteText(ug, TextLocation(2, 2));
		Should::Equal(expected, buffer.str());
	}

	buffer.Undo();
	Should::Equal(text, buffer.str(), L"Undo");

	buffer.Redo();
	Should::Equal(expected, buffer.str(), L"Redo");
}

static void ShouldDeleteSelection()
{
	auto text = L"line of text";
	auto expected = L"lixt";

	TextBuffer buffer(text);

	{
		UndoGroup ug(buffer);
		buffer.DeleteText(ug, TextSelection(2, 0, 10, 0));
		Should::Equal(expected, buffer.str());
	}

	buffer.Undo();
	Should::Equal(text, buffer.str(), L"Undo");

	buffer.Redo();
	Should::Equal(expected, buffer.str(), L"Redo");
}

static void ShouldDelete2LineSelection()
{
	auto text = L"one\ntwo\nthree";
	auto expected = L"onree";

	TextBuffer buffer(text);

	{
		UndoGroup ug(buffer);
		buffer.DeleteText(ug, TextSelection(2, 0, 2, 2));
		Should::Equal(expected, buffer.str());
	}

	buffer.Undo();
	Should::Equal(text, buffer.str(), L"Undo");

	buffer.Redo();
	Should::Equal(expected, buffer.str(), L"Redo");
}

static void ShouldDelete1LineSelection()
{
	auto text = L"one\ntwo\nthree";
	auto expected = L"on";

	TextBuffer buffer(text);

	{
		UndoGroup ug(buffer);
		buffer.DeleteText(ug, TextSelection(2, 1, 2, 2));

		Should::Equal(L"one\ntwree", buffer.str());

		buffer.DeleteText(ug, TextSelection(2, 0, 5, 1));
		Should::Equal(expected, buffer.str());
	}

	buffer.Undo();
	Should::Equal(text, buffer.str(), L"Undo");

	buffer.Redo();
	Should::Equal(expected, buffer.str(), L"Redo");
}

static void ShouldInsertSelection()
{
	auto text = L"line of text";
	auto selection = L"one\ntwo\nthree";
	auto expected = L"line oone\ntwo\nthreef text";

	TextBuffer buffer(text);

	{
		UndoGroup ug(buffer);
		buffer.InsertText(ug, TextLocation(6, 0), selection);
		Should::Equal(expected, buffer.str());
	}

	buffer.Undo();
	Should::Equal(text, buffer.str(), L"Undo");

	buffer.Redo();
	Should::Equal(expected, buffer.str(), L"Redo");
}

static void ShouldReturnSelection()
{
	TextBuffer buffer(L"one\ntwo\nthree");
	Should::Equal(L"e\ntwo\nth", Combine(buffer.Text(TextSelection(2, 0, 2, 2))));
}

static void ShouldCutAndPaste()
{
	auto text = L"one\ntwo\nthree";
	auto expected = L"one\none\ntwo\nthreetwo\nthree";

	TextBuffer buffer(text);

	{
		UndoGroup ug(buffer);
		auto copy = Combine(buffer.Text());
		buffer.InsertText(ug, TextLocation(0, 1), copy);
		Should::Equal(expected, buffer.str());
	}

	buffer.Undo();
	Should::Equal(text, buffer.str(), L"Undo");

	buffer.Redo();
	Should::Equal(expected, buffer.str(), L"Redo");
}



std::wstring RunTests()
{	
	Tests tests;

	tests.Register(L"Should insert chars", ShouldInsertSingleChars);
	tests.Register(L"Should split line", ShouldSplitLine);
	tests.Register(L"Should combine line", ShouldCombineLine);
	tests.Register(L"Should delete chars", ShouldDeleteChars);
	tests.Register(L"Should delete selection", ShouldDeleteSelection);
	tests.Register(L"Should delete 1 line selection", ShouldDelete1LineSelection);
	tests.Register(L"Should delete 2 line selection", ShouldDelete2LineSelection);	
	tests.Register(L"Should insert selection", ShouldInsertSelection);
	tests.Register(L"Should return selection", ShouldReturnSelection);
	tests.Register(L"Should cut and paste", ShouldCutAndPaste);

	std::wstringstream output;
	tests.Run(output);
	return output.str();
}