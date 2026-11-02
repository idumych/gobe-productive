// table_edit: the Table menu's operations (TableEdit: TTable's InsertRow,
// InsertColumn, DeleteRow, DeleteColumn, MergeCells, SplitCell, the
// Distribute commands, SetCellGutter, the selections) on a new 3 by 4 table
// (columns 100 points, rows 17), each checked against what TTable's code
// does with it, then stored, saved and read back. No arguments.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "Document.h"
#include "InlineParts.h"
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


static bool
Near(float a, float b)
{
	return std::fabs(a - b) < 0.01f;
}


static std::string
F(float v)
{
	char s[32];
	snprintf(s, sizeof s, "%g", v);
	return s;
}


// a new document with a 3 by 4 table at the start of its text
static bool
MakeDoc(Document& doc, size_t& table)
{
	std::string error;
	if (!CreateBlankDocument(doc, error))
		return false;
	int main = doc.MainPart();
	size_t text = 0;
	int part = AddTablePart(doc, size_t(main), MakeNewTable(3, 4), text, error);
	if (part < 0)
		return false;
	InsertInlineFrame(doc, size_t(main), {0, 0}, size_t(part));
	table = size_t(part);
	return true;
}


static bool
Fresh(TableEdit& edit, Document& doc, size_t& table)
{
	doc = Document();
	if (!MakeDoc(doc, table) || !LoadTableEdit(doc, table, edit)) {
		Check(false, "a new table loads");
		return false;
	}
	return true;
}


// every cell's location matches its place; rows' cells left to right
static void
CheckLocations(const TableEdit& edit, const std::string& what)
{
	bool ok = true;
	for (size_t r = 0; r < edit.rows.size(); r++)
		for (size_t c = 0; c < edit.rows[r].cells.size(); c++)
			ok = ok && edit.rows[r].cells[c].row == int(r) && edit.rows[r].cells[c].column == int(c);
	Check(ok, what + ": locations");
}


static std::string
Text(const TableEdit& edit, int id)
{
	std::string all;
	for (const WPParagraph& p : edit.texts[size_t(id)].paragraphs)
		all += p.text;
	return all;
}


int
main()
{
	Document doc;
	size_t table = 0;
	TableEdit edit;

	// ---- loading ----
	if (!Fresh(edit, doc, table)) {
		printf("table_edit ok %d, failed %d\n", sOk, sFailed);
		return 1;
	}
	Check(edit.rows.size() == 3 && edit.rows[0].cells.size() == 4, "3 rows of 4");
	Check(edit.frameOrder.size() == 12 && edit.frameOrder[5] == 5, "frames in row order");
	Check(Text(edit, 7) == "\x05", "a cell's text: its break");
	Check(edit.columnEnds.size() == 4 && Near(edit.columnEnds[3], 400), "column ends");
	{
		float left, right;
		edit.VirtualColumn(0, 1, left, right);
		Check(Near(left, 100) && Near(right, 200), "VirtualColumn (0,1): 100 to 200");
	}

	// ---- InsertRow before row 1 ----
	edit.InsertRow(1);
	Check(edit.rows.size() == 4, "InsertRow: 4 rows");
	Check(Near(edit.rows[1].cells[2].x, 200) && Near(edit.rows[1].cells[2].y, 17)
		&& Near(edit.rows[1].cells[2].width, 100) && Near(edit.rows[1].cells[2].height, 17),
		"InsertRow: the new row's cells at y 17");
	Check(Near(edit.rows[2].cells[0].y, 34), "InsertRow: the old row 1 down to 34");
	Check(Near(edit.size[0], 400) && Near(edit.size[1], 68), "InsertRow: 400 x 68, "
		+ F(edit.size[0]) + " x " + F(edit.size[1]));
	Check(edit.frameOrder.size() == 16 && edit.frameOrder[4] == edit.rows[1].cells[0].id
		&& edit.frameOrder[7] == edit.rows[1].cells[3].id, "InsertRow: its frames after row 0's");
	Check(Text(edit, edit.rows[1].cells[0].id) == "\x05"
		&& edit.texts[size_t(edit.rows[1].cells[0].id)].frame.values[3] == 0x48,
		"InsertRow: a new cell's text and frame flags");
	CheckLocations(edit, "InsertRow");

	// ---- InsertRow at the end ----
	Fresh(edit, doc, table);
	edit.InsertRow(3);
	Check(edit.rows.size() == 4 && Near(edit.rows[3].cells[0].y, 51)
		&& edit.frameOrder.back() == edit.rows[3].cells[3].id, "InsertRow(3): at the end, y 51");

	// ---- InsertColumn at cell (0,1)'s column ----
	Fresh(edit, doc, table);
	edit.InsertColumn(100, 200);
	Check(edit.rows[0].cells.size() == 5 && edit.rows[2].cells.size() == 5, "InsertColumn: 5 a row");
	Check(Near(edit.rows[1].cells[1].x, 100) && Near(edit.rows[1].cells[1].width, 100)
		&& Near(edit.rows[1].cells[1].y, 17), "InsertColumn: the new cell at 100");
	Check(Near(edit.rows[1].cells[4].x, 400), "InsertColumn: the last cell moved to 400");
	Check(Near(edit.size[0], 500) && Near(edit.size[1], 51), "InsertColumn: 500 x 51");
	Check(edit.frameOrder[6] == edit.rows[1].cells[1].id, "InsertColumn: its frame after (1,0)'s");
	CheckLocations(edit, "InsertColumn");

	// ---- DeleteRow ----
	Fresh(edit, doc, table);
	int gone = edit.rows[1].cells[0].id;
	edit.DeleteRow(1);
	Check(edit.rows.size() == 2 && Near(edit.rows[1].cells[0].y, 17), "DeleteRow: 2 rows, row 2 up");
	Check(Near(edit.size[1], 34) && edit.frameOrder.size() == 8, "DeleteRow: 34 high, 8 frames");
	Check(std::find(edit.frameOrder.begin(), edit.frameOrder.end(), gone) == edit.frameOrder.end(),
		"DeleteRow: its cells' text gone");
	edit.DeleteRow(0);
	edit.DeleteRow(0);
	Check(edit.rows.size() == 1, "DeleteRow: never the last row");

	// ---- DeleteColumn ----
	Fresh(edit, doc, table);
	edit.DeleteColumn(100, 200);
	Check(edit.rows[0].cells.size() == 3 && Near(edit.rows[0].cells[1].x, 100)
		&& Near(edit.rows[2].cells[2].x, 200), "DeleteColumn: the rest moved left");
	Check(Near(edit.size[0], 300) && edit.frameOrder.size() == 9, "DeleteColumn: 300 wide, 9 frames");

	// ---- selections ----
	Fresh(edit, doc, table);
	{
		TableSelection s = edit.SelectionOf(1, 1, 0, 0);
		Check(s.from == 0 && s.to == 1 && Near(s.left, 0) && Near(s.right, 200),
			"SelectionOf (1,1)-(0,0): rows 0-1, 0 to 200");
		Check(edit.Selected(s).size() == 4, "4 cells selected");
		int count = 0;
		Check(edit.SelectionIsRectangular(s, count) && count == 4, "rectangular, 4");
		float rect[4];
		edit.SpanningRect(s, rect);
		Check(Near(rect[2], 200) && Near(rect[3], 34), "spanning rect 200 x 34");
		int rows, columns;
		edit.SelectionSpan(s, rows, columns);
		Check(rows == 2 && columns == 2, "span 2 by 2");
		TableSelection row = edit.SelectRows(2, 2);
		Check(edit.Selected(row).size() == 4 && Near(row.right, 400), "SelectRows(2, 2)");
		TableSelection column = edit.SelectColumns(300, 400);
		Check(edit.Selected(column).size() == 3, "SelectColumns(300, 400): 3 cells");
	}

	// ---- MergeCells: text joined, cell breaks to line ends but the last ----
	Fresh(edit, doc, table);
	{
		const char* words[] = {"a", "b", "", "", "c", "", "", "", "", "", "", ""};
		for (int i = 0; i < 12; i++)
			edit.texts[size_t(i)].paragraphs[0].text = std::string(words[i]) + "\x05";
		// (0,0) a, (0,1) b, (1,0) c, (1,1) empty
		edit.MergeCells(edit.SelectionOf(0, 0, 1, 1));
		const TableEditCell* c = edit.Cell(0, 0);
		Check(c != nullptr && Near(c->width, 200) && Near(c->height, 34)
			&& c->rowSpan == 2 && c->columnSpan == 2, "MergeCells: (0,0) 200 x 34, spans 2 2");
		Check(edit.rows[0].cells.size() == 3 && edit.rows[1].cells.size() == 2,
			"MergeCells: 3 and 2 cells left");
		Check(Text(edit, c->id) == "a\nb\nc\x05", "MergeCells: \"a\\nb\\nc\\x05\", got \""
			+ Text(edit, c->id) + "\"");
		Check(Text(edit, edit.rows[0].cells[1].id) == "\x05", "MergeCells: (0,2) as it was");
		Check(edit.frameOrder.size() == 9 && edit.frameOrder[0] == c->id, "MergeCells: 9 frames");
		CheckLocations(edit, "MergeCells");
	}
	Fresh(edit, doc, table);
	edit.MergeCells(edit.SelectionOf(0, 0, 0, 3));
	Check(Text(edit, edit.Cell(0, 0)->id) == "\x05" && edit.rows[0].cells.size() == 1,
		"MergeCells: empty cells, one break");

	// ---- SplitCell ----
	Fresh(edit, doc, table);
	{
		TableSelection s = edit.SplitCell(0, 0, 2, 1);
		Check(edit.rows[0].cells.size() == 5 && Near(edit.rows[0].cells[0].width, 50)
			&& Near(edit.rows[0].cells[1].x, 50) && Near(edit.rows[0].cells[1].width, 50),
			"SplitCell 2 x 1: two cells 50 wide");
		Check(edit.rows[1].cells[0].columnSpan == 2 && edit.rows[0].cells[0].columnSpan == 1,
			"SplitCell 2 x 1: the cell below spans 2 columns");
		Check(s.from == 0 && s.to == 0 && Near(s.right, 100), "SplitCell: its cells selected");
		Check(edit.frameOrder[1] == edit.rows[0].cells[1].id, "SplitCell: the frame after the cell's");
	}
	Fresh(edit, doc, table);
	{
		edit.SplitCell(0, 0, 1, 2);
		Check(edit.rows.size() == 4, "SplitCell 1 x 2: a row more");
		Check(Near(edit.rows[0].height, 17) && Near(edit.rows[1].height, 17),
			"SplitCell 1 x 2: rows back to 17 (RecutAll), "
			+ F(edit.rows[0].height) + " " + F(edit.rows[1].height));
		Check(edit.rows[1].cells.size() == 1 && Near(edit.rows[1].cells[0].y, 17),
			"SplitCell 1 x 2: the new cell under it");
		Check(Near(edit.rows[0].cells[1].height, 34) && edit.rows[0].cells[1].rowSpan == 2,
			"SplitCell 1 x 2: its neighbours span both rows, "
			+ F(edit.rows[0].cells[1].height));
		Check(Near(edit.rows[2].cells[0].y, 34) && Near(edit.size[1], 68),
			"SplitCell 1 x 2: the rest down, 68 high");
	}

	// ---- Distribute ----
	Fresh(edit, doc, table);
	edit.ChangeRowHeight(1, 34, 34, true);
	Check(Near(edit.rows[2].cells[0].y, 51), "ChangeRowHeight: the next row moved");
	edit.DistributeRows(0, 2);
	Check(Near(edit.rows[0].height, 68.0f / 3) && Near(edit.rows[2].height, 68.0f / 3)
		&& Near(edit.rows[2].cells[0].y, 2 * 68.0f / 3), "DistributeRows: 22.67 each");
	Fresh(edit, doc, table);
	edit.ChangeColumnWidth(100, 50);
	Check(Near(edit.rows[0].cells[0].width, 150) && Near(edit.rows[2].cells[3].x, 350),
		"ChangeColumnWidth: 150, the rest moved");
	edit.DistributeColumns(0, 450);
	Check(Near(edit.rows[1].cells[0].width, 113) && Near(edit.rows[1].cells[3].x, 339)
		&& Near(edit.size[0], 452), "DistributeColumns: Round(112.5) = 113 each, "
		+ F(edit.rows[1].cells[0].width) + " " + F(edit.rows[1].cells[3].x));

	// ---- Cell Gutter ----
	Fresh(edit, doc, table);
	edit.SetCellGutter(3);
	Check(Near(edit.gutter, 3) && Near(edit.rows[0].height, 21) && Near(edit.size[1], 63),
		"SetCellGutter 3: rows 21 (2 x 3 + 15)");

	// ---- stored, saved, read back ----
	Fresh(edit, doc, table);
	edit.texts[5].paragraphs[0].text = "x\x05";
	edit.InsertRow(1);
	edit.SetCellGutter(2);
	std::string error;
	Check(StoreTableEdit(doc, table, edit, error), "stored: " + error);
	const char* dir = getenv("TMPDIR");
	std::string path = std::string(dir != nullptr ? dir : "/tmp") + "/table_edit.gobe";
	Check(doc.Save(path, error), "saved: " + error);
	Document back;
	Check(LoadDocument(path, back, error), "read back: " + error);
	const TablePart* t = back.Table(table);
	Check(t != nullptr && t->rows.size() == 4 && Near(t->value20, 2), "read back: 4 rows, gutter 2");
	if (t != nullptr && t->rows.size() == 4) {
		bool numbered = true;
		uint32_t k = 0;
		for (const TableRow& r : t->rows)
			for (const TableCell& c : r.cells)
				numbered = numbered && c.frame[2] == k++;
		Check(numbered, "read back: the cells' frames numbered in row order");
		Check(Near(t->rows[2].cells[1].origin[1], t->rows[0].top + t->rows[1].top),
			"read back: row 2 below the new row");
		const WPPart* wp = back.WordPart(t->textPart);
		Check(wp != nullptr && wp->paragraphs.size() == 17 && wp->paragraphs[9].text == "x\x05",
			"read back: 16 cells' text and the end, (2,1) \"x\"");
		const Portal& portal = back.tails[t->textPart].portals[0];
		Check(portal.frames.size() == 16 && portal.frames[9].textStart == 9
			&& portal.frames[9].textLength == 2, "read back: frame 9's text 9+2");
		Check(Near(portal.frames[0].origin[0], 3) && Near(portal.frames[0].origin[1], 2),
			"read back: frames inset 3, 2 (gutter 2)");
		TableEdit again;
		Check(LoadTableEdit(back, table, again) && Text(again, again.Cell(2, 1)->id) == "x\x05",
			"read back: loads as it was");
	}

	printf("table_edit ok %d, failed %d\n", sOk, sFailed);
	return sFailed > 0 ? 1 : 0;
}
