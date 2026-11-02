// table_clip: copying a table's selected cells (TTable::CopySelection) and
// pasting them into its cells (TTable::Paste, PasteCell) and into text as
// a table of their own; saved and read back. No arguments.
#include <cstdio>
#include <cstdlib>
#include <string>

#include "Document.h"
#include "InlineParts.h"
#include "TableClip.h"
#include "TableEdit.h"
#include "TablePart.h"
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


// cell (r, c)'s text, its paragraphs joined by '|', its break left off
static std::string
CellText(const Document& doc, size_t table, int r, int c)
{
	TableEdit edit;
	if (!LoadTableEdit(doc, table, edit) || edit.Cell(r, c) == nullptr)
		return "?";
	std::string s;
	for (const WPParagraph& p : edit.texts[size_t(edit.Cell(r, c)->id)].paragraphs) {
		if (!s.empty())
			s += '|';
		s += p.text;
	}
	while (!s.empty() && (s.back() == '\x05' || s.back() == '\n'))
		s.pop_back();
	for (char& ch : s)
		if (ch == '\n')
			ch = ' ';
	return s;
}


int
main()
{
	Document doc;
	std::string error;
	Check(CreateBlankDocument(doc, error), "blank document: " + error);
	int main = doc.MainPart();
	size_t text = 0;
	int part = main >= 0 ? AddTablePart(doc, size_t(main), MakeNewTable(3, 4), text, error) : -1;
	Check(part >= 0, "a 3 by 4 table: " + error);
	if (part < 0) {
		printf("table_clip ok %d, failed %d\n", sOk, sFailed);
		return 1;
	}
	InsertInlineFrame(doc, size_t(main), {0, 0}, size_t(part));
	size_t table = size_t(part);
	WPPart* wp = doc.EditableWordPart(text);
	wp->paragraphs[0].text = "ab\x05";
	wp->paragraphs[1].text = "cd\x05";
	wp->paragraphs[4].text = "x\x05";

	// ---- copied: (0,0) and (0,1) ----
	TableEdit edit;
	Check(LoadTableEdit(doc, table, edit), "loads");
	TableClip clip = CopyTableCells(doc, table, edit, edit.SelectionOf(0, 0, 0, 1));
	Check(clip.rows.size() == 1 && clip.rows[0].cells.size() == 2, "a row of two cells copied");
	if (clip.rows.size() == 1 && clip.rows[0].cells.size() == 2) {
		Check(clip.rows[0].cells[0].text.text == "ab" && clip.rows[0].cells[1].text.text == "cd",
			"their text, without the breaks: \"" + clip.rows[0].cells[0].text.text + "\"");
		Check(clip.rows[0].cells[0].width == 100 && clip.rows[0].height == 17,
			"100 wide, the row 17 high");
		Check(!clip.rows[0].cells[0].frame.empty(), "the frame style's properties");
	}
	TableClip block = CopyTableCells(doc, table, edit, edit.SelectionOf(0, 0, 1, 1));
	Check(block.rows.size() == 2 && block.rows[1].cells.size() == 2
		&& block.rows[1].cells[0].text.text == "x", "two rows of two: (1,0) \"x\"");

	// ---- pasted at (1,2): (1,2) and (1,3) ----
	Check(PasteTableCells(doc, table, 1, 2, clip, error), "pasted at (1,2): " + error);
	Check(CellText(doc, table, 1, 2) == "ab" && CellText(doc, table, 1, 3) == "cd",
		"(1,2) \"ab\", (1,3) \"cd\": " + CellText(doc, table, 1, 2) + ", "
			+ CellText(doc, table, 1, 3));
	Check(CellText(doc, table, 1, 0) == "x" && CellText(doc, table, 0, 0) == "ab",
		"the other cells as they were");

	// ---- pasted at (2,3): the second cell left out; over text ----
	wp = doc.EditableWordPart(text);
	Check(PasteTableCells(doc, table, 2, 3, clip, error) && CellText(doc, table, 2, 3) == "ab"
		&& CellText(doc, table, 2, 2) == "", "at (2,3): \"ab\", the rest past the edge");
	Check(PasteTableCells(doc, table, 1, 0, block, error) && CellText(doc, table, 1, 0) == "ab"
		&& CellText(doc, table, 1, 1) == "cd" && CellText(doc, table, 2, 0) == "x"
		&& CellText(doc, table, 2, 1) == "", "a block over (1,0): its text replaced");

	// ---- into the text: a table of its own ----
	size_t tables = doc.tableParts.size();
	int made = PasteTableClip(doc, size_t(main), {0, 1}, block, error);
	Check(made >= 0 && doc.tableParts.size() == tables + 1, "a new table: " + error);
	if (made >= 0) {
		const TablePart* t = doc.Table(size_t(made));
		Check(t != nullptr && t->rows.size() == 2 && t->rows[0].cells.size() == 2
			&& t->size[0] == 200, "2 by 2, 200 wide");
		Check(CellText(doc, size_t(made), 0, 0) == "ab" && CellText(doc, size_t(made), 1, 0) == "x",
			"its cells' text");
	}

	// ---- saved and read back ----
	const char* dir = getenv("TMPDIR");
	std::string path = std::string(dir != nullptr ? dir : "/tmp") + "/table_clip.gobe";
	Check(doc.Save(path, error), "save: " + error);
	Document back;
	Check(LoadDocument(path, back, error), "load: " + error);
	Check(CellText(back, table, 1, 2) == "ab" && CellText(back, table, 1, 3) == "cd"
		&& CellText(back, table, 2, 3) == "ab", "read back: the pasted cells");
	if (made >= 0)
		Check(CellText(back, size_t(made), 0, 1) == "cd", "read back: the new table");

	printf("table_clip ok %d, failed %d\n", sOk, sFailed);
	return sFailed > 0 ? 1 : 0;
}
