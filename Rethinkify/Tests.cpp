#include "pch.h"
#include "TextBuffer.h"
#include "Should.h"

static void InsertChars(TextBuffer &buffer, const char *chars, CPoint location = CPoint(0, 0))
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

	TextBuffer buffer;
	InsertChars(buffer, text);
	Should::Equal(text, buffer.str());

	InsertChars(buffer, "\n", CPoint(4, 0));
	Should::Equal("line\n of text", buffer.str());
}

static void ShouldCombineLine()
{	
	auto text = "line \nof text";

	TextBuffer buffer;
	InsertChars(buffer, text);
	Should::Equal(text, buffer.str());

	UndoGroup ug(buffer);
	buffer.DeleteText(ug, CPoint(0, 1));

	Should::Equal("line of text", buffer.str());
}

static void ShouldDeleteChars()
{
	auto text = "one\ntwo\nthree";

	TextBuffer buffer;
	InsertChars(buffer, text);
	Should::Equal(text, buffer.str());

	UndoGroup ug(buffer);

	buffer.DeleteText(ug, CPoint(2, 0));
	buffer.DeleteText(ug, CPoint(2, 1));
	buffer.DeleteText(ug, CPoint(2, 2));

	Should::Equal("oe\nto\ntree", buffer.str());
}

static void ShouldDeleteSelection()
{
	auto text = "line of text";

	TextBuffer buffer;	
	InsertChars(buffer, text);
	Should::Equal(text, buffer.str());

	UndoGroup ug(buffer);
	buffer.DeleteText(ug, CPoint(2, 0), CPoint(10, 0));

	Should::Equal("lixt", buffer.str());
}

static void ShouldDelete2LineSelection()
{
	auto text = "one\ntwo\nthree";

	TextBuffer buffer;
	InsertChars(buffer, text);
	Should::Equal(text, buffer.str());

	UndoGroup ug(buffer);
	buffer.DeleteText(ug, CPoint(2, 0), CPoint(2, 2));

	Should::Equal("onree", buffer.str());
}

static void ShouldDelete1LineSelection()
{
	auto text = "one\ntwo\nthree";

	TextBuffer buffer;
	InsertChars(buffer, text);
	Should::Equal(text, buffer.str());

	UndoGroup ug(buffer);
	buffer.DeleteText(ug, CPoint(2, 1), CPoint(2, 2));

	Should::Equal("one\ntwree", buffer.str());

	buffer.DeleteText(ug, CPoint(2, 0), CPoint(5, 1));
	Should::Equal("on", buffer.str());
}

static void ShouldInsertSelection()
{
	auto text = "line of text";
	auto selection = "one\ntwo\nthree";

	TextBuffer buffer;
	InsertChars(buffer, text);
	Should::Equal(text, buffer.str());

	UndoGroup ug(buffer);
	buffer.InsertText(ug, CPoint(6, 0), ToUtf16(selection));

	Should::Equal("line oone\ntwo\nthreef text", buffer.str());
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

	std::stringstream output;
	tests.Run(output);
	return output.str();
}