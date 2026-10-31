// CopyCells / PasteCells (TSSPasteCommand; src/core/SSPaste.cpp): cases
// worked out from the original's rules. Formulas keep their relative
// references (they refer to the cells around where they land), absolute
// ones stay; values only pastes what the formulas showed; formatting only
// keeps the cells' contents; a block that doesn't fit is refused; formats
// don't go to another document.
#include <cstdio>
#include <cstdlib>

#include "Document.h"
#include "Editing.h"
#include "SSEval.h"
#include "SSPaste.h"
#include "SSStyle.h"

using namespace gobe;

static int ok = 0, failures = 0;

static void
Check(bool cond, const std::string& what)
{
	if (cond) {
		ok++;
		return;
	}
	if (failures < 40)
		printf("FAIL: %s\n", what.c_str());
	failures++;
}


static bool
Scratch(int argc, char** argv, Document& doc, size_t& p)
{
	std::string error;
	for (int i = 1; i < argc; i++) {
		doc = Document();
		if (!LoadDocument(argv[i], doc, error) || doc.spreadsheetParts.empty())
			continue;
		p = doc.spreadsheetParts[0].first;
		SSPart* ss = doc.EditableSpreadsheetPart(p);
		if (ss->used.to.a < 40 || ss->used.to.b < 12)
			continue;
		ss->cells.clear();
		ss->dependents.clear();
		ss->locked.clear();
		return true;
	}
	return false;
}


static CellID
At(const char* cell)
{
	return {uint16_t(atoi(cell + 1) - 1), uint16_t(cell[0] - 'A')};
}


static void
Set(Document& doc, size_t p, const char* cell, const char* input)
{
	std::string error;
	CellID id = At(cell);
	SetCellInput(*doc.EditableSpreadsheetPart(p), id.a, id.b, input, nullptr, error);
	UpdateDependencies(doc, p);
}


static std::string
Input(const Document& doc, size_t p, const char* cell)
{
	CellID id = At(cell);
	return CellInput(*doc.SpreadsheetPart(p), id.a, id.b);
}


int
main(int argc, char** argv)
{
	Document doc;
	size_t p = 0;
	if (!Scratch(argc, argv, doc, p)) {
		printf("no spreadsheet part to work on\n");
		return 1;
	}
	const Document start = doc;
	const SSPart& ss = *doc.SpreadsheetPart(p);
	auto copy = [&](const char* from, const char* to) {
		return CopyCells(doc, p, SSRange{At(from), At(to)});
	};

	// formulas: relative references move with the paste, absolute ones stay
	Set(doc, p, "A1", "2");
	Set(doc, p, "B1", "=A1*10");
	Set(doc, p, "B2", "=$A$1+1");
	Set(doc, p, "D5", "7");
	CopiedCells c = copy("A1", "B2");
	Check(c.rows == 2 && c.columns == 2 && c.cells.size() == 3, "copied A1:B2");
	Check(PasteCells(doc, p, At("D5"), c, true, true, true), "paste at D5");
	Check(Input(doc, p, "D5") == "2", "D5 holds A1's 2: " + Input(doc, p, "D5"));
	Check(Input(doc, p, "E5") == "=D5*10", "E5: " + Input(doc, p, "E5"));
	Check(Input(doc, p, "E6") == "=$A$1+1", "E6: " + Input(doc, p, "E6"));
	Check(Input(doc, p, "D6") == "", "D6 empty (A2 was)");
	{
		SSBook book(doc);
		SSEvaluator e(ss, &book, int(p));
		Check(e.Value(At("E5").a, At("E5").b).number == 20, "E5's value");
	}

	// values only: the formulas' values as they were when copied
	doc = start;
	Set(doc, p, "A1", "2");
	Set(doc, p, "B1", "=A1*10");
	c = copy("A1", "B1");
	Set(doc, p, "A1", "5");		// changed after the copy
	Check(PasteCells(doc, p, At("A3"), c, true, false, true), "paste values");
	Check(Input(doc, p, "B3") == "20", "B3 holds the value 20: " + Input(doc, p, "B3"));

	// an empty cell of the block empties the cell it lands on
	doc = start;
	Set(doc, p, "A1", "1");
	Set(doc, p, "C3", "9");
	Set(doc, p, "D3", "8");
	c = copy("A1", "B1");
	PasteCells(doc, p, At("C3"), c, true, true, true);
	Check(Input(doc, p, "C3") == "1" && Input(doc, p, "D3") == "", "D3 emptied");

	// formatting only: the contents stay, the style comes
	doc = start;
	Set(doc, p, "A1", "1");
	Set(doc, p, "C1", "3");
	{
		SSPart& part = *doc.EditableSpreadsheetPart(p);
		uint32_t style = StyleWithProperty(doc.styles, 0xffffffff, CellHAlignProperty(3));
		for (SSCell& cell : part.cells)
			if (cell.id.a == 0 && cell.id.b == 0)
				SetCellStyle(doc.styles, cell.style, style);
	}
	c = copy("A1", "A1");
	Check(PasteCells(doc, p, At("C1"), c, false, true, true), "paste formatting");
	{
		const SSCell* a = ss.Cell(0, 0);
		const SSCell* target = ss.Cell(0, 2);
		Check(target != nullptr && a != nullptr && target->style == a->style
			&& Input(doc, p, "C1") == "3", "C1 keeps 3 with A1's style");
	}
	// formats don't go to another document; data alone does
	Document other = start;
	Check(!PasteCells(other, p, At("C1"), c, false, true, true),
		"formatting only into another document: nothing");
	Check(PasteCells(other, p, At("C1"), c, true, true, true), "data into another document");
	{
		const SSCell* target = other.SpreadsheetPart(p)->Cell(0, 2);
		Check(target != nullptr && target->style == 0xffffffff
			&& Input(other, p, "C1") == "1", "C1 holds 1 without the style");
	}

	// a block that doesn't fit is refused
	doc = start;
	Set(doc, p, "A1", "1");
	c = copy("A1", "B2");
	CellID corner = {ss.used.to.a, ss.used.to.b};
	Check(!PasteFits(ss, corner, c) && !PasteCells(doc, p, corner, c, true, true, true),
		"no room at the sheet's last cell");
	Check(PasteFits(ss, {uint16_t(ss.used.to.a - 1), uint16_t(ss.used.to.b - 1)}, c),
		"room one row and column in");

	printf("ss_paste: ok %d, failed %d\n", ok, failures);
	return failures != 0 || ok == 0;
}
