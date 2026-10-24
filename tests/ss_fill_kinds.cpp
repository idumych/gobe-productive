// FillCells: Fill Down, Right, Up and Left as TSSFillCommand::DoFillDown,
// DoFillRight, DoFillUp and DoFillLeft do them (decomp/Spreadsheet/
// TSSFillCommand.c): in each column (row) of the block the top (bottom,
// left, right) cell is cloned into the others; a block one cell high (wide)
// isn't filled; the block is pinned to the sheet (TRange::Pin to the part's
// range at +0x60, which TSSSetDocSizeCommand sets).
// Works in a scratch block of every spreadsheet part of the documents, below
// and right of its cells, so the expectations are only the inputs moved.
#include <algorithm>
#include <cstdio>
#include <string>

#include "Document.h"
#include "Editing.h"

using namespace gobe;

static int ok = 0, failed = 0;

static void
Check(bool cond, const std::string& desc)
{
	if (cond) {
		ok++;
		return;
	}
	failed++;
	if (failed <= 20)
		printf("FAIL: %s\n", desc.c_str());
}


// what a cell holds, comparable: kind, number, text, formula tokens, style
static std::string
Contents(const SSPart& part, uint16_t row, uint16_t column)
{
	const SSCell* cell = part.Cell(row, column);
	if (cell == nullptr)
		return "(none)";
	std::string s = std::to_string(cell->kind) + "|" + std::to_string(cell->number) + "|"
		+ cell->text + "|" + std::to_string(cell->style) + "|";
	for (const FormulaToken& t : cell->formula) {
		s += std::to_string(t.kind) + "," + std::to_string(t.number) + ","
			+ std::to_string(t.value);
		for (const std::string& text : t.strings)
			s += "," + text;
		for (const CellRef& ref : t.refs)
			s += "," + std::to_string(ref.cell.a) + ":" + std::to_string(ref.cell.b) + ":"
				+ std::to_string(ref.flags);
		s += ";";
	}
	return s;
}


// the 3x3 block at (r, c): a different input in each cell
static void
Setup(SSPart& part, uint16_t r, uint16_t c)
{
	const char* inputs[3][3] = {
		{"1", "two", "=1+2"},
		{"4", "five", "=4+2"},
		{"7", "eight", "=7+2"},
	};
	std::string error;
	for (int i = 0; i < 3; i++)
		for (int j = 0; j < 3; j++)
			SetCellInput(part, uint16_t(r + i), uint16_t(c + j), inputs[i][j], nullptr, error);
}


int
main(int argc, char** argv)
{
	int parts = 0;
	for (int i = 1; i < argc; i++) {
		Document doc;
		std::string error;
		if (!LoadDocument(argv[i], doc, error))
			continue;
		for (size_t p = 0; p < doc.spreadsheetParts.size(); p++) {
			SSPart* part = doc.EditableSpreadsheetPart(doc.spreadsheetParts[p].first);
			if (part == nullptr)
				continue;
			// a block below and right of every cell, inside the sheet
			uint16_t r = 0, c = 0;
			for (const SSCell& cell : part->cells) {
				r = std::max<uint16_t>(r, uint16_t(cell.id.a + 2));
				c = std::max<uint16_t>(c, uint16_t(cell.id.b + 2));
			}
			if (r + 3 > part->used.to.a || c + 3 > part->used.to.b)
				continue;
			parts++;
			std::string name = std::string(argv[i]) + " part " + std::to_string(p);

			struct Case {
				FillKind kind;
				const char* label;
				int sourceRow, sourceColumn;	// -1: the cell's own
			} cases[] = {
				{FillKind::down, "Fill Down", 0, -1},
				{FillKind::up, "Fill Up", 2, -1},
				{FillKind::right, "Fill Right", -1, 0},
				{FillKind::left, "Fill Left", -1, 2},
			};
			for (const Case& k : cases) {
				Setup(*part, r, c);
				std::string before[3][3];
				for (int i = 0; i < 3; i++)
					for (int j = 0; j < 3; j++)
						before[i][j] = Contents(*part, uint16_t(r + i), uint16_t(c + j));
				bool changed = FillCells(*part, r, c, uint16_t(r + 2), uint16_t(c + 2), k.kind);
				Check(changed, name + ": " + k.label + " changed nothing");
				for (int i = 0; i < 3; i++)
					for (int j = 0; j < 3; j++) {
						int si = k.sourceRow < 0 ? i : k.sourceRow;
						int sj = k.sourceColumn < 0 ? j : k.sourceColumn;
						Check(Contents(*part, uint16_t(r + i), uint16_t(c + j)) == before[si][sj],
							name + ": " + k.label + " cell " + std::to_string(i) + ","
								+ std::to_string(j) + " isn't the copy of " + std::to_string(si)
								+ "," + std::to_string(sj));
					}
			}

			// one row high: Fill Down and Up leave it; one column wide: Right, Left
			Setup(*part, r, c);
			Check(!FillCells(*part, r, c, r, uint16_t(c + 2), FillKind::down),
				name + ": Fill Down filled a one-row block");
			Check(!FillCells(*part, r, c, r, uint16_t(c + 2), FillKind::up),
				name + ": Fill Up filled a one-row block");
			Check(!FillCells(*part, r, c, uint16_t(r + 2), c, FillKind::right),
				name + ": Fill Right filled a one-column block");
			Check(!FillCells(*part, r, c, uint16_t(r + 2), c, FillKind::left),
				name + ": Fill Left filled a one-column block");

			// an empty source empties the cells it fills
			Setup(*part, r, c);
			std::string error;
			SetCellInput(*part, r, c, "", nullptr, error);
			bool emptied = part->Cell(r, c) == nullptr || part->Cell(r, c)->formula.empty();
			FillCells(*part, r, c, uint16_t(r + 2), c, FillKind::down);
			if (part->Cell(r, c) == nullptr) {
				Check(part->Cell(uint16_t(r + 1), c) == nullptr
						&& part->Cell(uint16_t(r + 2), c) == nullptr,
					name + ": Fill Down from an empty cell left cells below");
			} else
				Check(emptied, name + ": (setup) the emptied cell still has a formula");

			// pinned to the sheet: nothing is made beyond its last row
			uint16_t last = part->used.to.a;
			if (last < 0xfffe) {
				SetCellInput(*part, uint16_t(last - 1), c, "9", nullptr, error);
				FillCells(*part, uint16_t(last - 1), c, uint16_t(last + 1), c, FillKind::down);
				Check(Contents(*part, last, c) == Contents(*part, uint16_t(last - 1), c),
					name + ": Fill Down didn't reach the sheet's last row");
				Check(part->Cell(uint16_t(last + 1), c) == nullptr,
					name + ": Fill Down made a cell below the sheet");
			}
		}
	}
	printf("parts %d, ok %d, failed %d\n", parts, ok, failed);
	return failed == 0 && parts > 0 ? 0 : 1;
}
