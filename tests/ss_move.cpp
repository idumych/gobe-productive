// MoveCells / MoveCellsCommand (TSSMoveCommand::MoveCells and PerformCmd,
// decomp/Spreadsheet/TSSMoveCommand.c). Two kinds of checks:
// - every spreadsheet part's cells, moved as one block to empty cells of
//   the sheet, keep their values at their new places (references into the
//   block follow it, named ranges too), and moved back give the cells as
//   they were; the moved document saved and read again gives the same
//   values (the named ranges' selections are rewritten);
// - small cases worked out by hand from the decomp's rules (each comment
//   says which rule): TFormula::MoveReferences (TFormula.c 001988d0),
//   TFormula::InvalidateReferences (001986e8), TRangeRef::Adjust
//   (TRangeRef.c 00168694), TCellRef::OffsetAbsolute/OffsetRelative.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>

#include "Document.h"
#include "Editing.h"
#include "SSEval.h"
#include "SSMove.h"

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
Same(const SSValue& a, const SSValue& b)
{
	if (a.type != b.type)
		return false;
	switch (a.type) {
		case SSValue::kNumber:
		case SSValue::kBool:
			return a.number == b.number || std::fabs(a.number - b.number)
				<= 1e-9 * std::fabs(a.number);
		case SSValue::kString:
			return a.text == b.text;
		case SSValue::kTime:
			return a.time == b.time;
		case SSValue::kError:
			return a.error == b.error;
		default:
			return true;
	}
}


// what a cell holds, comparable
static std::string
Contents(const SSCell& cell)
{
	std::string s = std::to_string(cell.id.a) + "," + std::to_string(cell.id.b) + "|"
		+ std::to_string(cell.kind) + "|" + std::to_string(cell.number) + "|" + cell.text
		+ "|" + std::to_string(cell.style) + "|";
	for (const FormulaToken& t : cell.formula) {
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


static std::string
Formula(const SSPart& part, uint16_t row, uint16_t column)
{
	const SSCell* c = part.Cell(row, column);
	if (c == nullptr)
		return "(none)";
	return c->kind == Sig("Fmla") ? FormulaText(c->formula, c->id) : "(not a formula)";
}


// a scratch document: the first spreadsheet part, emptied
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


static void
Set(Document& doc, size_t p, const char* cell, const char* input)
{
	std::string error;
	uint16_t column = uint16_t(cell[0] - 'A'), row = uint16_t(atoi(cell + 1) - 1);
	SetCellInput(*doc.EditableSpreadsheetPart(p), row, column, input, nullptr, error);
	UpdateDependencies(doc, p);
}


static std::vector<SSRange>
Block(const char* from, const char* to)
{
	SSRange r{{uint16_t(atoi(from + 1) - 1), uint16_t(from[0] - 'A')},
		{uint16_t(atoi(to + 1) - 1), uint16_t(to[0] - 'A')}};
	return {r};
}


static void
HandCases(int argc, char** argv)
{
	Document doc;
	size_t p = 0;
	if (!Scratch(argc, argv, doc, p)) {
		Check(false, "no spreadsheet part for the small cases");
		return;
	}
	const Document start = doc;
	auto part = [&]() -> const SSPart& { return *doc.SpreadsheetPart(p); };

	// a cell referred to moves: the relative reference follows it (OffsetRelative
	// and OffsetAbsolute by the move: the cell isn't in the block)
	doc = start;
	Set(doc, p, "A1", "1");
	Set(doc, p, "B1", "=A1");
	Set(doc, p, "C1", "=$A$1");
	Set(doc, p, "D1", "=A$1");
	Check(MoveCellsCommand(doc, p, Block("A1", "A1"), 2, 0) == MoveResult::moved, "move A1");
	Check(Formula(part(), 0, 1) == "A3", "=A1 after A1 moved to A3: " + Formula(part(), 0, 1));
	Check(Formula(part(), 0, 2) == "$A$3", "=$A$1: " + Formula(part(), 0, 2));
	Check(Formula(part(), 0, 3) == "A$3", "=A$1: " + Formula(part(), 0, 3));
	Check(part().Cell(0, 0) == nullptr, "A1 is empty after the move");
	Check(part().Cell(2, 0) != nullptr && part().Cell(2, 0)->number == 1, "A3 holds the 1");

	// the formula cell moves, what it refers to doesn't: OffsetRelative by
	// minus the move, so it still refers to A1 (absolute parts unchanged)
	doc = start;
	Set(doc, p, "A1", "1");
	Set(doc, p, "B1", "=A1+$A$1");
	MoveCellsCommand(doc, p, Block("B1", "B1"), 0, 2);
	Check(Formula(part(), 0, 3) == "A1+$A$1", "=A1+$A$1 moved to D1: " + Formula(part(), 0, 3));

	// both move: OffsetAbsolute only (relative references stay relative)
	doc = start;
	Set(doc, p, "A1", "1");
	Set(doc, p, "B1", "=A1+$A$1");
	MoveCellsCommand(doc, p, Block("A1", "B1"), 3, 0);
	Check(Formula(part(), 3, 1) == "A4+$A$4", "A1:B1 moved to row 4: " + Formula(part(), 3, 1));

	// a reference to a cell the block lands on becomes invalid
	doc = start;
	Set(doc, p, "A1", "1");
	Set(doc, p, "C1", "=E1");
	Set(doc, p, "E1", "5");
	MoveCellsCommand(doc, p, Block("A1", "A1"), 0, 4);
	{
		const SSCell* c = part().Cell(0, 2);
		Check(c != nullptr && !c->formula.empty() && !c->formula[0].refs.empty()
				&& (c->formula[0].refs[0].flags & 1),
			"=E1 is invalid after A1 moved onto E1");
		Check(part().Cell(0, 4) != nullptr && part().Cell(0, 4)->number == 1,
			"E1 holds the moved 1");
	}

	// a range's first row moves up: TRangeRef::Adjust (columns 0, its top
	// edge in the block) moves its first corner only: A2:A4 -> A1:A4
	doc = start;
	Set(doc, p, "A2", "2");
	Set(doc, p, "A3", "3");
	Set(doc, p, "A4", "4");
	Set(doc, p, "C1", "=SUM(A2:A4)");
	MoveCellsCommand(doc, p, Block("A2", "A2"), -1, 0);
	Check(Formula(part(), 0, 2) == "SUM(A1:A4)", "SUM(A2:A4), A2 moved up: "
		+ Formula(part(), 0, 2));

	// its last row moves down: the second corner only: A2:A4 -> A2:A6
	doc = start;
	Set(doc, p, "A2", "2");
	Set(doc, p, "A3", "3");
	Set(doc, p, "A4", "4");
	Set(doc, p, "C1", "=SUM(A2:A4)");
	MoveCellsCommand(doc, p, Block("A4", "A4"), 2, 0);
	Check(Formula(part(), 0, 2) == "SUM(A2:A6)", "SUM(A2:A4), A4 moved down: "
		+ Formula(part(), 0, 2));

	// a middle cell moves sideways: the range meets the block but neither
	// edge is in it: unchanged
	doc = start;
	Set(doc, p, "A2", "2");
	Set(doc, p, "A3", "3");
	Set(doc, p, "A4", "4");
	Set(doc, p, "C1", "=SUM(A2:A4)");
	MoveCellsCommand(doc, p, Block("A3", "A3"), 0, 5);
	Check(Formula(part(), 0, 2) == "SUM(A2:A4)", "SUM(A2:A4), A3 moved right: "
		+ Formula(part(), 0, 2));

	// a diagonal move of its last cell: rows and columns both non-zero, so
	// only a range inside the block follows: unchanged
	doc = start;
	Set(doc, p, "A2", "2");
	Set(doc, p, "A3", "3");
	Set(doc, p, "C1", "=SUM(A2:A3)");
	MoveCellsCommand(doc, p, Block("A3", "A3"), 5, 5);
	Check(Formula(part(), 0, 2) == "SUM(A2:A3)", "SUM(A2:A3), A3 moved diagonally: "
		+ Formula(part(), 0, 2));

	// the whole range inside the block: both corners follow
	doc = start;
	Set(doc, p, "A2", "2");
	Set(doc, p, "A3", "3");
	Set(doc, p, "C1", "=SUM(A2:A3)");
	MoveCellsCommand(doc, p, Block("A1", "A5"), 5, 5);
	Check(Formula(part(), 0, 2) == "SUM(F7:F8)", "SUM(A2:A3), A1:A5 moved by 5,5: "
		+ Formula(part(), 0, 2));

	// the formula moves, the range doesn't meet the block: OffsetRelative by
	// minus the move on both corners: still A2:A3 from the new place
	doc = start;
	Set(doc, p, "A2", "2");
	Set(doc, p, "A3", "3");
	Set(doc, p, "C1", "=SUM(A2:A3)");
	MoveCellsCommand(doc, p, Block("C1", "C1"), 4, 3);
	Check(Formula(part(), 4, 5) == "SUM(A2:A3)", "SUM(A2:A3) moved to F5: "
		+ Formula(part(), 4, 5));

	// formula and range in the block together: OffsetAbsolute only
	doc = start;
	Set(doc, p, "A2", "2");
	Set(doc, p, "A3", "3");
	Set(doc, p, "B1", "=SUM(A2:A3)+SUM($A$2:$A$3)");
	MoveCellsCommand(doc, p, Block("A1", "B3"), 0, 3);
	Check(Formula(part(), 0, 4) == "SUM(D2:D3)+SUM($D$2:$D$3)", "A1:B3 moved right by 3: "
		+ Formula(part(), 0, 4));

	// the formula moves with the range's top edge (moving down): the corner
	// in the block OffsetAbsolute, the other OffsetRelative by minus the move
	doc = start;
	Set(doc, p, "A2", "2");
	Set(doc, p, "A3", "3");
	Set(doc, p, "A4", "4");
	Set(doc, p, "B2", "=SUM(A2:A4)");
	MoveCellsCommand(doc, p, Block("A2", "B2"), -1, 0);
	Check(Formula(part(), 0, 1) == "SUM(A1:A4)", "A2:B2 moved up, SUM(A2:A4) in B2: "
		+ Formula(part(), 0, 1));

	// above the first row or left of the first column: refused, nothing changes
	doc = start;
	Set(doc, p, "A1", "1");
	Set(doc, p, "B1", "=A1");
	{
		std::vector<std::string> before;
		for (const SSCell& c : part().cells)
			before.push_back(Contents(c));
		Check(MoveCellsCommand(doc, p, Block("A1", "B1"), -1, 0) == MoveResult::failed,
			"moving row 1 up fails");
		Check(MoveCellsCommand(doc, p, Block("A1", "B1"), 0, -1) == MoveResult::failed,
			"moving column A left fails");
		std::vector<std::string> after;
		for (const SSCell& c : part().cells)
			after.push_back(Contents(c));
		Check(before == after, "a refused move changed cells");
	}

	// landing on a locked cell: refused (TPartSS::IsLocked)
	doc = start;
	Set(doc, p, "A1", "1");
	LockCells(*doc.EditableSpreadsheetPart(p), 3, 0, 3, 0, true);
	Check(MoveCellsCommand(doc, p, Block("A1", "A1"), 3, 0) == MoveResult::locked,
		"moving onto locked A4");
	Check(part().Cell(0, 0) != nullptr, "A1 still there after a locked move");
	Check(MoveCellsCommand(doc, p, Block("A1", "A1"), 4, 0) == MoveResult::moved,
		"moving past locked A4");

	// what was where the block lands outside itself is cleared, style and all
	doc = start;
	Set(doc, p, "A1", "1");
	Set(doc, p, "A2", "2");
	Set(doc, p, "A3", "3");
	MoveCellsCommand(doc, p, Block("A1", "A2"), 1, 0);
	Check(part().Cell(0, 0) == nullptr, "A1 empty after A1:A2 moved down 1");
	Check(part().Cell(1, 0) != nullptr && part().Cell(1, 0)->number == 1, "A2 holds 1");
	Check(part().Cell(2, 0) != nullptr && part().Cell(2, 0)->number == 2, "A3 holds 2");
}


int
main(int argc, char** argv)
{
	int parts = 0, values = 0;
	for (int i = 1; i < argc; i++) {
		Document original;
		std::string error;
		if (!LoadDocument(argv[i], original, error))
			continue;
		for (const auto& [p, ss] : original.spreadsheetParts) {
			if (ss.cells.empty())
				continue;
			// the block: every cell; moved down past itself, or right
			SSRange box{ss.cells[0].id, ss.cells[0].id};
			for (const SSCell& c : ss.cells) {
				box.from.a = std::min(box.from.a, c.id.a);
				box.from.b = std::min(box.from.b, c.id.b);
				box.to.a = std::max(box.to.a, c.id.a);
				box.to.b = std::max(box.to.b, c.id.b);
			}
			int rows = 0, columns = 0;
			if (box.to.a + (box.to.a - box.from.a + 3) <= ss.used.to.a)
				rows = box.to.a - box.from.a + 3;
			else if (box.to.b + (box.to.b - box.from.b + 3) <= ss.used.to.b)
				columns = box.to.b - box.from.b + 3;
			else
				continue;
			parts++;
			std::string where = std::string(argv[i]) + " part " + std::to_string(p);

			// values before, twice (RAND and its dependents are left out)
			SSBook book(original), book2(original);
			SSEvaluator before(ss, &book, int(p)), again(ss, &book2, int(p));
			// (where it lands may be locked, which refuses the move: unlocked)
			Document doc = original;
			doc.EditableSpreadsheetPart(p)->locked.clear();
			Check(MoveCellsCommand(doc, p, {box}, rows, columns) == MoveResult::moved,
				where + ": move");
			const SSPart& moved = *doc.SpreadsheetPart(p);
			Check(moved.cells.size() == ss.cells.size(), where + ": cells lost");
			SSBook bookAfter(doc);
			SSEvaluator after(moved, &bookAfter, int(p));
			for (const SSCell& c : ss.cells) {
				SSValue v0 = before.Value(c.id.a, c.id.b);
				if (!Same(v0, again.Value(c.id.a, c.id.b)))
					continue;
				values++;
				SSValue v1 = after.Value(uint16_t(c.id.a + rows), uint16_t(c.id.b + columns));
				Check(Same(v0, v1), where + ": " + CellName(c.id) + " value changed");
			}

			// saved and read again: the same values
			std::string path = std::string(getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp")
				+ "/ss_move_save";
			Document saved = doc;
			bool wrote = saved.Save(path, error);
			Check(wrote, where + ": save " + error);
			if (wrote) {
				SSBook bookSaved(saved);
				const SSPart* reread = saved.SpreadsheetPart(p);
				Check(reread != nullptr && reread->cells.size() == ss.cells.size(),
					where + ": cells after save");
				if (reread != nullptr) {
					SSEvaluator e(*reread, &bookSaved, int(p));
					for (const SSCell& c : ss.cells) {
						SSValue v0 = before.Value(c.id.a, c.id.b);
						if (!Same(v0, again.Value(c.id.a, c.id.b)))
							continue;
						Check(Same(v0, e.Value(uint16_t(c.id.a + rows),
								uint16_t(c.id.b + columns))),
							where + " after save: " + CellName(c.id) + " value changed");
					}
				}
			}
			remove(path.c_str());

			// and back: the cells as they were
			SSRange there{{uint16_t(box.from.a + rows), uint16_t(box.from.b + columns)},
				{uint16_t(box.to.a + rows), uint16_t(box.to.b + columns)}};
			Check(MoveCellsCommand(doc, p, {there}, -rows, -columns) == MoveResult::moved,
				where + ": move back");
			const SSPart& back = *doc.SpreadsheetPart(p);
			Check(back.cells.size() == ss.cells.size(), where + ": cells after moving back");
			for (size_t k = 0; k < ss.cells.size() && k < back.cells.size(); k++)
				Check(Contents(back.cells[k]) == Contents(ss.cells[k]),
					where + ": moved back, cell " + CellName(ss.cells[k].id) + " differs");
		}
	}
	HandCases(argc, argv);
	printf("parts %d, values %d, ok %d, failed %d\n", parts, values, ok, failures);
	return failures != 0 || parts == 0;
}
