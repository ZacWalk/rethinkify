#include "pch.h"
#include "TextBuffer.h"
#include "Should.h"

static void InsertChars(TextBuffer &buffer, const char *chars, TextLocation location = TextLocation(0, 0))
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
	auto text1 = "Hello\nWorld";
	auto text2 = "Line\n";

	TextBuffer buffer;
	InsertChars(buffer, text1);
	Should::Equal(text1, buffer.str());

	InsertChars(buffer, text2);
	Should::Equal(std::string(text2) + text1, buffer.str());
}

static void ShouldSplitLine()
{
	auto text = "line of text";
	auto expected = "line\n of text";

	TextBuffer buffer(text);

	{
		UndoGroup ug(buffer);
		buffer.InsertText(ug, TextLocation(4, 0), '\n');
		Should::Equal("line\n of text", buffer.str());
	}

	buffer.Undo();
	Should::Equal(text, buffer.str(), "Undo");

	buffer.Redo();
	Should::Equal(expected, buffer.str(), "Redo");
}

static void ShouldCombineLine()
{	
	auto text = "line \nof text";
	auto expected = "line of text";

	TextBuffer buffer(text);

	{
		UndoGroup ug(buffer);
		buffer.DeleteText(ug, TextLocation(0, 1));
		Should::Equal(expected, buffer.str());
	}

	buffer.Undo();
	Should::Equal(text, buffer.str(), "Undo");

	buffer.Redo();
	Should::Equal(expected, buffer.str(), "Redo");
}

static void ShouldDeleteChars()
{
	auto text = "one\ntwo\nthree";
	auto expected = "oe\nto\ntree";

	TextBuffer buffer(text);

	{
		UndoGroup ug(buffer);
		buffer.DeleteText(ug, TextLocation(2, 0));
		buffer.DeleteText(ug, TextLocation(2, 1));
		buffer.DeleteText(ug, TextLocation(2, 2));
		Should::Equal(expected, buffer.str());
	}

	buffer.Undo();
	Should::Equal(text, buffer.str(), "Undo");

	buffer.Redo();
	Should::Equal(expected, buffer.str(), "Redo");
}

static void ShouldDeleteSelection()
{
	auto text = "line of text";
	auto expected = "lixt";

	TextBuffer buffer(text);

	{
		UndoGroup ug(buffer);
		buffer.DeleteText(ug, TextSelection(2, 0, 10, 0));
		Should::Equal(expected, buffer.str());
	}

	buffer.Undo();
	Should::Equal(text, buffer.str(), "Undo");

	buffer.Redo();
	Should::Equal(expected, buffer.str(), "Redo");
}

static void ShouldDelete2LineSelection()
{
	auto text = "one\ntwo\nthree";
	auto expected = "onree";

	TextBuffer buffer(text);

	{
		UndoGroup ug(buffer);
		buffer.DeleteText(ug, TextSelection(2, 0, 2, 2));
		Should::Equal(expected, buffer.str());
	}

	buffer.Undo();
	Should::Equal(text, buffer.str(), "Undo");

	buffer.Redo();
	Should::Equal(expected, buffer.str(), "Redo");
}

static void ShouldDelete1LineSelection()
{
	auto text = "one\ntwo\nthree";
	auto expected = "on";

	TextBuffer buffer(text);

	{
		UndoGroup ug(buffer);
		buffer.DeleteText(ug, TextSelection(2, 1, 2, 2));

		Should::Equal("one\ntwree", buffer.str());

		buffer.DeleteText(ug, TextSelection(2, 0, 5, 1));
		Should::Equal(expected, buffer.str());
	}

	buffer.Undo();
	Should::Equal(text, buffer.str(), "Undo");

	buffer.Redo();
	Should::Equal(expected, buffer.str(), "Redo");
}

static void ShouldInsertSelection()
{
	auto text = "line of text";
	auto selection = "one\ntwo\nthree";
	auto expected = "line oone\ntwo\nthreef text";

	TextBuffer buffer(text);

	{
		UndoGroup ug(buffer);
		buffer.InsertText(ug, TextLocation(6, 0), ToUtf16(selection));
		Should::Equal(expected, buffer.str());
	}

	buffer.Undo();
	Should::Equal(text, buffer.str(), "Undo");

	buffer.Redo();
	Should::Equal(expected, buffer.str(), "Redo");
}

static void ShouldReturnSelection()
{
	TextBuffer buffer("one\ntwo\nthree");
	Should::Equal("e\ntwo\nth", ToUtf8(Combine(buffer.Text(TextSelection(2, 0, 2, 2)))));
}



std::string RunTests()
{	
	Tests tests;

	tests.Register("Should insert chars", ShouldInsertSingleChars);
	tests.Register("Should split line", ShouldSplitLine);
	tests.Register("Should combine line", ShouldCombineLine);
	tests.Register("Should delete chars", ShouldDeleteChars);
	tests.Register("Should delete selection", ShouldDeleteSelection);
	tests.Register("Should delete 1 line selection", ShouldDelete1LineSelection);
	tests.Register("Should delete 2 line selection", ShouldDelete2LineSelection);	
	tests.Register("Should insert selection", ShouldInsertSelection);
	tests.Register("Should return selection", ShouldReturnSelection);

	std::stringstream output;
	tests.Run(output);
	return output.str();
}