// undo_shared: one document, one UndoStack, two windows (New Window, the
// windows of a TDoc sharing its command history). Window A types, window
// B undoes it and A redoes it; each undo hands back the view state of the
// window that made the step; Changes() moves with every step, so each
// window can tell the document changed under it; merged typing from one
// window stays one step for both.
// Arguments: none (documents given are ignored).
#include <cstdio>
#include <string>

#include "Document.h"
#include "Editing.h"
#include "Undo.h"
#include "WPPart.h"

using namespace gobe;

static int sOk = 0, sFailed = 0;

static void
Check(bool ok, const std::string& what)
{
	if (ok)
		sOk++;
	else {
		sFailed++;
		printf("FAIL %s\n", what.c_str());
	}
}


static std::string
Text(const Document& doc, size_t part)
{
	const WPPart* wp = doc.WordPart(part);
	return wp != nullptr && !wp->paragraphs.empty() ? wp->paragraphs[0].text : "";
}


int
main()
{
	Document doc;
	std::string error;
	Check(CreateBlankDocument(doc, error), "a new document " + error);
	size_t part = size_t(doc.MainPart());
	UndoStack shared;
	const UndoStack::ViewState windowA = {1, 0, 5}, windowB = {2, 0, 0};
	std::string before = Text(doc, part);

	// A types "Hello" (merged: one step), B sees the change counted
	uint64_t seenByB = shared.Changes();
	for (const char* piece : {"He", "ll", "o"}) {
		shared.Begin(doc, part, "Typing", windowA, 42);
		WPPart* wp = doc.EditableWordPart(part);
		InsertText(*wp, {0, Text(doc, part).find('\n') == std::string::npos
			? Text(doc, part).size() : Text(doc, part).find('\n')}, piece);
	}
	Check(Text(doc, part).compare(0, 5, "Hello") == 0, "A typed: " + Text(doc, part));
	Check(shared.Changes() != seenByB, "B can tell the document changed");
	size_t done = 0;
	Check(shared.Labels(done).size() == 1 && done == 1, "the typing is one step");

	// B undoes A's typing: the text as it was, A's view state handed back
	UndoStack::ViewState view = windowB;
	seenByB = shared.Changes();
	Check(shared.Undo(doc, view) == int(part), "B undoes it");
	Check(Text(doc, part) == before, "the text as before: " + Text(doc, part));
	Check(view == windowA, "the view state A had when it typed");
	Check(shared.Changes() != seenByB && shared.CanRedo(), "counted, and redoable");

	// A redoes it
	view = windowA;
	Check(shared.Redo(doc, view) == int(part) && Text(doc, part).compare(0, 5, "Hello") == 0,
		"A redoes it");

	// B types after A's step: a new step (another window's merge key)
	shared.Begin(doc, part, "Typing", windowB, 7);
	InsertText(*doc.EditableWordPart(part), {0, 0}, "X");
	Check(shared.Labels(done).size() == 2 && done == 2, "two steps");
	uint64_t count = shared.Changes();
	shared.Clear();
	Check(shared.Changes() != count && !shared.CanUndo(), "Clear counted (Revert)");

	printf("undo_shared: ok %d, failed %d\n", sOk, sFailed);
	return sFailed ? 1 : 0;
}
