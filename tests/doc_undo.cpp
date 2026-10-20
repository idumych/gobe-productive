// Undo stack: load a document, edit word/spreadsheet parts through Begin+Insert/Split/Delete/SetCell,
// undo/redo three steps, check Saved(doc) matches the pre-edit snapshot, then test
// DocumentSaved() makes undo keep the part dirty.
#include <cstdio>
#include <cstring>

#include "Document.h"
#include "Editing.h"
#include "WPPart.h"
#include "DocWriter.h"
#include "Undo.h"

using namespace gobe;

static int failures = 0;

static void
Fail(const char* docPath, int part, const char* check)
{
	printf("%s: part %d: %s\n", docPath, part, check);
	failures++;
}

// Build a DocChanges that rewrites every part (like doc_save.cpp), plus the
// LinksMgr section, and returns SaveDocument(doc, changes).
static std::vector<uint8_t>
Saved(const Document& doc)
{
	DocChanges changes;
	for (size_t p = 0; p < doc.parts.parts.size(); p++)
		changes.parts[p] = doc.parts.parts[p].data;
	changes.sections["LinksMgr"] = WriteLinksMgr(doc.links);
	for (const auto& [p, ss] : doc.spreadsheetParts)
		changes.parts[p] = WriteSSPart(ss, doc.parts.parts[p].data);
	for (const auto& [p, wp] : doc.wordParts)
		changes.parts[p] = WriteWPPart(wp, doc.parts.parts[p].data);
	return SaveDocument(doc, changes);
}

struct UndoHeap {
	UndoStack stack;
};

// Is part p a word processing part?
static bool
IsWord(const Document& doc, size_t p)
{
	return doc.WordPart(p) != nullptr;
}

// Is part p a spreadsheet part?
static bool
IsSpreadsheet(const Document& doc, size_t p)
{
	return doc.SpreadsheetPart(p) != nullptr;
}

int
main(int argc, char** argv)
{
	for (int i = 1; i < argc; i++) {
		const char* docPath = argv[i];
		Document doc;
		std::string error;
		if (!LoadDocument(docPath, doc, error))
			continue;

		// Collect word part indices and spreadsheet part indices.
		std::vector<size_t> wordParts, sheetParts;
		for (size_t p = 0; p < doc.parts.parts.size(); p++) {
			if (IsWord(doc, p))
				wordParts.push_back(p);
			if (IsSpreadsheet(doc, p))
				sheetParts.push_back(p);
		}

		std::vector<uint8_t> before = Saved(doc);
		bool wordBeforeDirty = false;
		for (size_t p : wordParts)
			if (doc.dirtyParts.count(p)) { wordBeforeDirty = true; break; }

		// ---- Word parts: first 3 only ----
		size_t wordCount = 0;
		for (size_t p : wordParts) {
			if (wordCount >= 3)
				break;
			wordCount++;

			UndoHeap* u = new UndoHeap;
			UndoStack::ViewState v = { int64_t(1), int64_t(2) };

			// Save paragraph 0 text before edits for later comparison.
			std::string origPar0;
			{
				const WPPart* wp = doc.WordPart(p);
				if (wp && !wp->paragraphs.empty())
					origPar0 = wp->paragraphs[0].text;
			}

			// a) Begin("Typing") + InsertText "abc" + Begin("Typing") + InsertText "d"
			u->stack.Begin(doc, p, "Typing", v, 7);
			{
				WPPart* wp = doc.EditableWordPart(p);
				if (wp && !wp->paragraphs.empty())
					InsertText(*wp, TextPosition{0, 0}, "abc");
			}
			u->stack.Begin(doc, p, "Typing", v, 7);
			{
				WPPart* wp = doc.EditableWordPart(p);
				if (wp && !wp->paragraphs.empty())
					InsertText(*wp, TextPosition{0, 0}, "d");
			}
			u->stack.Break();
			// b) Begin("Split") + SplitParagraph
			u->stack.Begin(doc, p, "Split", UndoStack::ViewState{3});
			{
				WPPart* wp = doc.EditableWordPart(p);
				if (wp && !wp->paragraphs.empty())
					SplitParagraph(*wp, TextPosition{0, 1});
			}
			// c) Begin("Delete") + DeleteRange
			u->stack.Begin(doc, p, "Delete", UndoStack::ViewState{4});
			{
				WPPart* wp = doc.EditableWordPart(p);
				if (wp) {
					TextPosition res;
					DeleteRange(*wp, TextPosition{0, 0}, TextPosition{1, 1}, res);
				}
			}

			// Now undo three times
			// Undo 1 (Delete step) -- view becomes {4}
			{
				int result = u->stack.Undo(doc, v);
				if (result != int(p))
					Fail(docPath, int(p), "undo 1 returns part");
				if (v.size() != 1 || v[0] != 4)
					Fail(docPath, int(p), "undo 1 view = {4}");
			}
			// Undo 2 (Split step) -- view becomes {3}
			{
				int result = u->stack.Undo(doc, v);
				if (result != int(p))
					Fail(docPath, int(p), "undo 2 returns part");
				if (v.size() != 1 || v[0] != 3)
					Fail(docPath, int(p), "undo 2 view = {3}");
			}
			// Undo 3 (merged Typing step) -- view restored to {1, 2}
			{
				int result = u->stack.Undo(doc, v);
				if (result != int(p))
					Fail(docPath, int(p), "undo 3 returns part");
				if (v.size() != 2 || v[0] != 1 || v[1] != 2)
					Fail(docPath, int(p), "undo 3 view = {1, 2}");
			}
			// Undo 4 should fail
			{
				int result = u->stack.Undo(doc, v);
				if (result != -1)
					Fail(docPath, int(p), "undo 4 returns -1");
			}
			// After undoing all three steps, Saved(doc) should match before.
			{
				std::vector<uint8_t> afterUndo = Saved(doc);
				if (afterUndo != before)
					Fail(docPath, int(p), "Saved(doc) == after undo 3");
			}
			// dirtyParts: p should no longer be in dirtyParts (if it wasn't before)
			{
				if (!wordBeforeDirty && doc.dirtyParts.count(p))
					Fail(docPath, int(p), "dirtyParts removed after undo");
			}

			// Redo three times
			{
				for (int r = 0; r < 3; r++) {
					int result = u->stack.Redo(doc, v);
					if (result != int(p))
						Fail(docPath, int(p), "redo");
				}
			}
			// After redos, paragraph 0 text should differ from the original
			// (the edits came back).
			{
				std::string curPar0;
				const WPPart* wp = doc.WordPart(p);
				if (wp && !wp->paragraphs.empty())
					curPar0 = wp->paragraphs[0].text;
				if (curPar0 == origPar0)
					Fail(docPath, int(p), "paragraph 0 text differs from original after redo");
			}
			// Now undo three times again: Saved(doc) == before again.
			{
				for (int r = 0; r < 3; r++) {
					int result = u->stack.Undo(doc, v);
					if (result != int(p))
						Fail(docPath, int(p), "undo after redo");
				}
				std::vector<uint8_t> afterUndo2 = Saved(doc);
				if (afterUndo2 != before)
					Fail(docPath, int(p), "Saved(doc) == after undo-redo-undo");
			}

			delete u;
		}

		// ---- Spreadsheet parts ----
		for (size_t p : sheetParts) {
			size_t linksBefore = doc.links.links.size();
			UndoHeap* u = new UndoHeap;
			UndoStack::ViewState v;

			{
				u->stack.Begin(doc, p, "Cell", v);
				SSPart* ss = doc.EditableSpreadsheetPart(p);
				if (ss) {
					std::string errorCell;
					SetCellInput(*ss, 0, 0, "=1+2", nullptr, errorCell);
					UpdateDependencies(doc, p);
				}
			}

			// Undo
			u->stack.Undo(doc, v);

			// Check Saved(doc) == before
			{
				std::vector<uint8_t> afterUndo = Saved(doc);
				if (afterUndo != before)
					Fail(docPath, int(p), "Saved(doc) == after cell undo");
			}
			// links.size() should equal its original size
			if (doc.links.links.size() != linksBefore)
				Fail(docPath, int(p), "links.size() after undo");

			delete u;
		}

		// ---- DocumentSaved() test ----
		// After DocumentSaved(), an undo leaves the part dirty (trustDirty=false
		// on steps). We need steps created *before* DocumentSaved.
		{
			if (wordParts.empty())
				break;
			size_t p = wordParts[0];
			UndoHeap* uSaved = new UndoHeap;
			// Create a step by doing an edit
			UndoStack::ViewState vSaved = { int64_t(1) };
			uSaved->stack.Begin(doc, p, "SavedEdit", vSaved, 7);
			{
				WPPart* wp = doc.EditableWordPart(p);
				if (wp && !wp->paragraphs.empty())
					InsertText(*wp, TextPosition{0, 0}, "x");
			}
			// Now mark the document as saved
			uSaved->stack.DocumentSaved();
			// Undo: the step's trustDirty is false, so p stays in dirtyParts
			{
				int result = uSaved->stack.Undo(doc, vSaved);
				if (result != int(p))
					Fail(docPath, int(p), "DocumentSaved undo returns part");
			}
			// After undo on a saved document, the part should be dirty.
			if (!doc.dirtyParts.count(p))
				Fail(docPath, int(p), "dirtyParts after DocumentSaved+undo");
			// Saved(doc) should == before (undo restored original state).
			{
				std::vector<uint8_t> afterSavedUndo = Saved(doc);
				if (afterSavedUndo != before)
					Fail(docPath, int(p), "Saved(doc) == after DocumentSaved+undo");
			}

			delete uSaved;
		}
	}

	if (failures == 0)
		printf("doc_undo: ok\n");
	else
		printf("%d failures\n", failures);
	return failures != 0;
}
