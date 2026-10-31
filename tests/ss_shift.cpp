// InsertCells / DeleteCells (TSSInsertCommand and TSSDeleteCommand::
// PerformCmd for cells, decomp/Spreadsheet/TSSInsertCommand.c and
// TSSDeleteCommand.c; TRange::Adjust, Gobe Productive/TRange.c). Two kinds
// of checks:
// - in every spreadsheet part, cells inserted down and right at a few
//   places keep their values where they went, and deleted again give the
//   cells as they were (but the empty cells pushed past the sheet's edge,
//   and formulas referring to them, now invalid; a range only meeting the
//   shifted cells doesn't follow them, so formulas with ranges are left out
//   of the values); whole rows and columns inserted and deleted again
//   give everything as it was;
// - small cases worked out by hand from the decomp's rules.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
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


static double
Number(const SSPart& part, uint16_t row, uint16_t column)
{
	const SSCell* c = part.Cell(row, column);
	return c == nullptr ? -1 : c->number;
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


static SSRange
Range(const char* from, const char* to)
{
	return {{uint16_t(atoi(from + 1) - 1), uint16_t(from[0] - 'A')},
		{uint16_t(atoi(to + 1) - 1), uint16_t(to[0] - 'A')}};
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
	const uint16_t lastRow = part().used.to.a, lastColumn = part().used.to.b;

	// Insert Cells down at A2:A3: column A from row 2 moves down 2 rows,
	// column B stays; references follow, a range whose last row moves
	// stretches (TRangeRef::Adjust)
	doc = start;
	Set(doc, p, "A1", "1");
	Set(doc, p, "A2", "2");
	Set(doc, p, "A5", "5");
	Set(doc, p, "B2", "20");
	Set(doc, p, "C1", "=A5");
	Set(doc, p, "D1", "=SUM(A1:A6)");
	Check(InsertCells(doc, p, Range("A2", "A3"), true), "insert down at A2:A3");
	Check(part().Cell(1, 0) == nullptr && part().Cell(2, 0) == nullptr, "A2:A3 empty");
	Check(Number(part(), 3, 0) == 2 && Number(part(), 6, 0) == 5, "A2 to A4, A5 to A7");
	Check(Number(part(), 1, 1) == 20, "B2 stays");
	Check(Formula(part(), 0, 2) == "A7", "=A5: " + Formula(part(), 0, 2));
	Check(Formula(part(), 0, 3) == "SUM(A1..A8)", "=SUM(A1:A6): " + Formula(part(), 0, 3));

	// and deleted again: as it was
	Check(DeleteCells(doc, p, Range("A2", "A3"), true), "delete up at A2:A3");
	Check(Number(part(), 1, 0) == 2 && Number(part(), 4, 0) == 5, "back in A2 and A5");
	Check(Formula(part(), 0, 2) == "A5", "=A5 again: " + Formula(part(), 0, 2));
	Check(Formula(part(), 0, 3) == "SUM(A1..A6)", "SUM again: " + Formula(part(), 0, 3));

	// a cell that isn't empty would go past the last row: refused, nothing changes
	doc = start;
	Set(doc, p, "A2", "2");
	{
		std::string error;
		SetCellInput(*doc.EditableSpreadsheetPart(p), lastRow, 0, "9", nullptr, error);
	}
	std::vector<std::string> before;
	for (const SSCell& c : part().cells)
		before.push_back(Contents(c));
	Check(!InsertCells(doc, p, Range("A2", "A2"), true), "insert refused at the edge");
	std::vector<std::string> after;
	for (const SSCell& c : part().cells)
		after.push_back(Contents(c));
	Check(before == after, "nothing changed by the refused insert");
	// in another column the edge is empty: allowed
	Check(InsertCells(doc, p, Range("B2", "B2"), true), "insert down in column B");

	// Delete Cells up: a reference to a deleted cell becomes invalid, one
	// below follows; the formula cell itself moves up
	doc = start;
	Set(doc, p, "A2", "2");
	Set(doc, p, "A3", "3");
	Set(doc, p, "A4", "4");
	Set(doc, p, "A5", "=A2+A4");
	Set(doc, p, "C1", "=A4");
	Check(DeleteCells(doc, p, Range("A2", "A3"), true), "delete up at A2:A3");
	Check(Number(part(), 1, 0) == 4, "A4 to A2");
	Check(Formula(part(), 0, 2) == "A2", "=A4 after delete: " + Formula(part(), 0, 2));
	{
		const SSCell* c = part().Cell(2, 0);
		bool invalid = false;
		if (c != nullptr)
			for (const FormulaToken& t : c->formula)
				for (const CellRef& r : t.refs)
					invalid |= (r.flags & 1) != 0;
		Check(c != nullptr && c->kind == Sig("Fmla") && invalid,
			"=A2+A4 moved to A3 with A2 invalid");
	}

	// Delete Cells left at B1: C1 moves to B1
	doc = start;
	Set(doc, p, "B1", "2");
	Set(doc, p, "C1", "3");
	Set(doc, p, "C2", "30");
	Check(DeleteCells(doc, p, Range("B1", "B1"), false), "delete left at B1");
	Check(Number(part(), 0, 1) == 3 && part().Cell(0, 2) == nullptr, "C1 to B1");
	Check(Number(part(), 1, 2) == 30, "C2 stays");

	// Insert Cells right at the last column's cells: refused
	doc = start;
	{
		std::string error;
		SetCellInput(*doc.EditableSpreadsheetPart(p), 0, lastColumn, "9", nullptr, error);
	}
	Check(!InsertCells(doc, p, Range("A1", "A1"), false), "insert right refused at the edge");

	// deleting the last rows: nothing below to move up, they're cleared
	doc = start;
	{
		std::string error;
		SetCellInput(*doc.EditableSpreadsheetPart(p), lastRow, 0, "9", nullptr, error);
	}
	SSRange last{{lastRow, 0}, {lastRow, 0}};
	Check(DeleteCells(doc, p, last, true), "delete the last row's cell");
	Check(part().Cell(lastRow, 0) == nullptr, "the last row's cell cleared");

	// locks (TSSMoveCommand::OffsetLocks): a lock the shifted cells meet
	// moves with them; then every lock is adjusted, and on a vertical shift
	// one in other columns whose rows are among the block's moves too (the
	// original checks only the rows)
	doc = start;
	doc.EditableSpreadsheetPart(p)->locked = {Range("A5", "A5"), Range("C5", "C6")};
	Check(InsertCells(doc, p, Range("A2", "A3"), true), "insert with locks");
	Check(part().locked.size() == 2 && part().locked[0].from.a == 6
			&& part().locked[1].from.a == 6 && part().locked[1].to.a == 7,
		"locks A5 and C5:C6 moved down 2");
	// a lock alone in another column: the block doesn't meet it, it stays
	doc = start;
	doc.EditableSpreadsheetPart(p)->locked = {Range("C5", "C5")};
	InsertCells(doc, p, Range("A2", "A3"), true);
	Check(part().locked[0].from.a == 4, "lock C5 stays");
	// a lock the block's first row is in stretches (TRange::Adjust)
	doc = start;
	doc.EditableSpreadsheetPart(p)->locked = {Range("A1", "B4")};
	InsertCells(doc, p, Range("A2", "A3"), true);
	Check(part().locked[0].from.a == 0 && part().locked[0].to.a == 5,
		"lock A1:B4 stretched to A1:B6");

	// whole rows: the row sizes move with them, deleted rows' sizes go, the
	// sheet grows and shrinks; a formula below follows
	doc = start;
	{
		SSPart& ss = *doc.EditableSpreadsheetPart(p);
		SSHeader h3{2, SSCell()}, h6{5, SSCell()};
		h3.cell.number = 30;
		h6.cell.number = 60;
		ss.rows = {h3, h6};
	}
	Set(doc, p, "A6", "6");
	Set(doc, p, "B1", "=A6");
	SSRange rows2{{1, 0}, {2, lastColumn}};	// rows 2 and 3
	Check(InsertCells(doc, p, rows2, false, SelectionKind::rows), "insert rows 2:3");
	Check(part().used.to.a == lastRow + 2, "the sheet 2 rows longer");
	Check(part().rows.size() == 2 && part().rows[0].index == 4 && part().rows[1].index == 7,
		"row sizes of rows 3 and 6 now of rows 5 and 8");
	Check(Formula(part(), 0, 1) == "A8", "=A6 after inserting rows: " + Formula(part(), 0, 1));
	Check(DeleteCells(doc, p, SSRange{{3, 0}, {4, lastColumn}}, false, SelectionKind::rows),
		"delete rows 4:5");
	Check(part().used.to.a == lastRow, "the sheet as long as before");
	Check(part().rows.size() == 1 && part().rows[0].index == 5,
		"row 5's size gone, row 8's back to row 6");
	Check(Formula(part(), 0, 1) == "A6", "=A8 after deleting rows: " + Formula(part(), 0, 1));
	// a column past the last one the sheet can have: refused
	doc = start;
	doc.EditableSpreadsheetPart(p)->used.to.b = 0x2bd;
	Check(!InsertCells(doc, p, SSRange{{0, 1}, {lastRow, 1}}, false, SelectionKind::columns),
		"insert a column refused at column 0x2bd");
}


// the cells a formula refers to, as ranges; `ranges` is set if it has a
// range reference
static std::vector<SSRange>
Refers(const SSCell& c, bool& ranges)
{
	std::vector<SSRange> out;
	ranges = false;
	for (const FormulaToken& t : c.formula) {
		if (t.refs.empty() || (t.refs[0].flags & 1))
			continue;
		CellID a = ActualCell(t.refs[0], c.id);
		CellID b = t.refs.size() > 1 ? ActualCell(t.refs[1], c.id) : a;
		ranges |= t.refs.size() > 1;
		out.push_back({{std::min(a.a, b.a), std::min(a.b, b.b)},
			{std::max(a.a, b.a), std::max(a.b, b.b)}});
	}
	return out;
}


static bool
Meets(const std::vector<SSRange>& list, const SSRange& r)
{
	for (const SSRange& x : list)
		if (!(r.to.a < x.from.a || x.to.a < r.from.a || r.to.b < x.from.b || x.to.b < r.from.b))
			return true;
	return false;
}


int
main(int argc, char** argv)
{
	int parts = 0, values = 0;
	std::string error;
	for (int i = 1; i < argc; i++) {
		Document original;
		if (!LoadDocument(argv[i], original, error))
			continue;
		for (const auto& entry : original.spreadsheetParts) {
			size_t p = entry.first;
			const SSPart& ss = *original.SpreadsheetPart(p);
			if (ss.cells.empty())
				continue;
			// where cells go in: the first cell's row and column, and the
			// middle of the cells
			const SSCell& first = ss.cells.front();
			const SSCell& middle = ss.cells[ss.cells.size() / 2];
			bool counted = false;
			for (const SSCell* at : {&first, &middle}) {
				for (bool vertical : {true, false}) {
					SSRange r{at->id, at->id};
					(vertical ? r.to.a : r.to.b) = uint16_t((vertical ? r.from.a : r.from.b) + 1);
					std::string where = std::string(argv[i]) + " part " + std::to_string(p)
						+ (vertical ? " down at " : " right at ") + CellName(at->id);
					// the cells pushed past the sheet's edge (empty, or the
					// insert is refused) and those that shift
					SSRange edge = vertical
						? SSRange{{uint16_t(ss.used.to.a - 1), r.from.b}, {ss.used.to.a, r.to.b}}
						: SSRange{{r.from.a, uint16_t(ss.used.to.b - 1)}, {r.to.a, ss.used.to.b}};
					SSRange shifts = vertical
						? SSRange{r.from, {ss.used.to.a, r.to.b}}
						: SSRange{r.from, {r.to.a, ss.used.to.b}};
					auto in = [](const SSRange& x, CellID c) {
						return x.from.a <= c.a && c.a <= x.to.a && x.from.b <= c.b && c.b <= x.to.b;
					};
					Document doc = original;
					SSBook book(original), book2(original);
					SSEvaluator before(ss, &book, int(p)), again(ss, &book2, int(p));
					if (!InsertCells(doc, p, r, vertical))
						continue;	// cells at the sheet's edge
					if (!counted)
						parts++;
					counted = true;
					const SSPart& shifted = *doc.SpreadsheetPart(p);
					SSBook bookAfter(doc);
					SSEvaluator after(shifted, &bookAfter, int(p));
					// the values of the cells kept, at their new places; a
					// formula using (through other cells too) a range or the
					// edge can change (a range only meeting the shifted cells
					// stays)
					std::map<uint32_t, int> stable;	// 0 being worked out, 1 yes, 2 no
					std::function<bool(const SSCell&)> isStable = [&](const SSCell& c) {
						uint32_t key = uint32_t(c.id.a) << 16 | c.id.b;
						auto it = stable.find(key);
						if (it != stable.end())
							return it->second == 1;
						stable[key] = 0;
						bool ranges, yes = true;
						std::vector<SSRange> refers = Refers(c, ranges);
						if (ranges || Meets(refers, edge))
							yes = false;
						for (const SSRange& x : refers)
							if (const SSCell* d = yes ? ss.Cell(x.from.a, x.from.b) : nullptr)
								yes = isStable(*d);
						stable[key] = yes ? 1 : 2;
						return yes;
					};
					for (const SSCell& c : ss.cells) {
						if (in(edge, c.id) || !isStable(c))
							continue;
						SSValue v0 = before.Value(c.id.a, c.id.b);
						if (!Same(v0, again.Value(c.id.a, c.id.b)))
							continue;
						int d = in(shifts, c.id) ? 2 : 0;
						values++;
						Check(Same(v0, after.Value(uint16_t(c.id.a + (vertical ? d : 0)),
								uint16_t(c.id.b + (vertical ? 0 : d)))),
							where + ": " + CellName(c.id) + " value changed");
					}
					// deleted again: every cell kept as it was, but formulas
					// that referred to the edge or to the cells deleted (a
					// range with a corner there is invalid now)
					Check(DeleteCells(doc, p, r, vertical), where + ": delete");
					const SSPart& back = *doc.SpreadsheetPart(p);
					for (const SSCell& c : ss.cells) {
						if (in(edge, c.id))
							continue;
						bool ranges;
						std::vector<SSRange> refers = Refers(c, ranges);
						if (Meets(refers, edge) || Meets(refers, r))
							continue;
						const SSCell* now = back.Cell(c.id.a, c.id.b);
						Check(now != nullptr && Contents(*now) == Contents(c),
							where + ": deleted again, cell " + CellName(c.id) + " differs");
					}
				}
			}
		}
	}
	// whole rows and columns: the sheet grows, nothing is lost, and
	// deleted again everything is as it was (cells, sizes, the sheet)
	for (int i = 1; i < argc; i++) {
		Document original;
		if (!LoadDocument(argv[i], original, error))
			continue;
		for (const auto& entry : original.spreadsheetParts) {
			size_t p = entry.first;
			const SSPart& ss = *original.SpreadsheetPart(p);
			if (ss.cells.empty())
				continue;
			const SSCell& middle = ss.cells[ss.cells.size() / 2];
			for (SelectionKind kind : {SelectionKind::rows, SelectionKind::columns}) {
				bool rows = kind == SelectionKind::rows;
				SSRange r = rows
					? SSRange{{middle.id.a, 0}, {uint16_t(middle.id.a + 1), ss.used.to.b}}
					: SSRange{{0, middle.id.b}, {ss.used.to.a, uint16_t(middle.id.b + 1)}};
				std::string where = std::string(argv[i]) + " part " + std::to_string(p)
					+ (rows ? " rows at " : " columns at ") + CellName(middle.id);
				Document doc = original;
				if (!InsertCells(doc, p, r, false, kind)) {
					Check(ss.used.to.a + 2 > 0x3ffe || ss.used.to.b + 2 > 0x2bd,
						where + ": insert refused");
					continue;
				}
				// (cells past the sheet's size are cleared if the shifted ones
				// land on them, so they aren't counted)
				auto inside = [&](const SSPart& part) {
					size_t n = 0;
					for (const SSCell& c : part.cells)
						n += c.id.a <= ss.used.to.a && c.id.b <= ss.used.to.b;
					return n;
				};
				const SSPart& grown = *doc.SpreadsheetPart(p);
				Check(grown.cells.size() >= inside(ss), where + ": cells lost");
				Check(rows ? grown.used.to.a == ss.used.to.a + 2 : grown.used.to.b == ss.used.to.b + 2,
					where + ": the sheet grew");
				const SSCell* moved = grown.Cell(uint16_t(middle.id.a + (rows ? 2 : 0)),
					uint16_t(middle.id.b + (rows ? 0 : 2)));
				// (its formula's references can differ: relative ones to cells
				// that stayed)
				Check(moved != nullptr && moved->kind == middle.kind
						&& moved->number == middle.number && moved->text == middle.text
						&& moved->style == middle.style,
					where + ": the cell moved by 2");
				Check(DeleteCells(doc, p, r, false, kind), where + ": delete");
				const SSPart& back = *doc.SpreadsheetPart(p);
				Check(back.used.from.a == ss.used.from.a && back.used.to.a == ss.used.to.a
						&& back.used.to.b == ss.used.to.b, where + ": the sheet's size");
				Check(inside(back) == inside(ss), where + ": cells after delete");
				for (const SSCell& c : ss.cells) {
					if (c.id.a > ss.used.to.a || c.id.b > ss.used.to.b)
						continue;
					bool ranges;
					if (Meets(Refers(c, ranges), r))
						continue;
					const SSCell* now = back.Cell(c.id.a, c.id.b);
					Check(now != nullptr && Contents(*now) == Contents(c),
						where + ": deleted again, cell " + CellName(c.id) + " differs");
				}
				const std::vector<SSHeader>& h0 = rows ? ss.rows : ss.columns;
				const std::vector<SSHeader>& h1 = rows ? back.rows : back.columns;
				bool same = h0.size() == h1.size();
				for (size_t k = 0; same && k < h0.size(); k++)
					same = h0[k].index == h1[k].index && h0[k].cell.number == h1[k].cell.number;
				Check(same, where + ": sizes after delete");
				parts++;
			}
		}
	}
	HandCases(argc, argv);
	printf("parts %d, values %d, ok %d, failed %d\n", parts, values, ok, failures);
	return failures != 0 || parts == 0;
}
