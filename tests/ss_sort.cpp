// SortCells (TSSSortCommand, TSSSortTable::Compare, QuickSort; see
// src/core/SSSort.cpp). Two kinds of checks:
// - every spreadsheet part's cells sorted by their first column: each row
//   moves whole (but its empty cells: borders stay), and sorting again
//   changes nothing when the key holds no formulas;
// - small cases worked out by hand from the decomp's rules.
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>

#include "Document.h"
#include "Editing.h"
#include "SSEval.h"
#include "SSSort.h"

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


// the inputs of a column's cells, "|"-separated
static std::string
Column(const Document& doc, size_t p, char column, int from, int to)
{
	const SSPart& ss = *doc.SpreadsheetPart(p);
	std::string s;
	for (int row = from; row <= to; row++)
		s += (row > from ? "|" : "") + CellInput(ss, uint16_t(row - 1), uint16_t(column - 'A'));
	return s;
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
	auto sorted = [&](const char* from, const char* to, std::vector<SortKey> keys) {
		return SortCells(doc, p, Range(from, to), true, keys);
	};

	// SortRank: numbers, then strings, booleans, errors; the rest of the
	// row moves with its key
	doc = start;
	Set(doc, p, "A1", "3");
	Set(doc, p, "A2", "b");
	Set(doc, p, "A3", "1");
	Set(doc, p, "A4", "=1/0");
	Set(doc, p, "A5", "TRUE");
	Set(doc, p, "A6", "a");
	for (int r = 1; r <= 6; r++) {
		std::string cell = "B" + std::to_string(r);
		Set(doc, p, cell.c_str(), ("row" + std::to_string(r)).c_str());
	}
	Check(sorted("A1", "B6", {{Range("A1", "A6"), true}}), "sort A1:B6");
	Check(Column(doc, p, 'A', 1, 6) == "1|3|a|b|TRUE|=1/0",
		"ascending by rank: " + Column(doc, p, 'A', 1, 6));
	Check(Column(doc, p, 'B', 1, 6) == "row3|row1|row6|row2|row5|row4",
		"the rows follow: " + Column(doc, p, 'B', 1, 6));
	// descending turns it round
	Check(sorted("A1", "B6", {{Range("A1", "A6"), false}}), "sort descending");
	Check(Column(doc, p, 'A', 1, 6) == "=1/0|TRUE|b|a|3|1",
		"descending: " + Column(doc, p, 'A', 1, 6));

	// empty cells go last both ways (a comparison with one isn't turned)
	doc = start;
	Set(doc, p, "A1", "2");
	Set(doc, p, "A3", "1");
	Set(doc, p, "A4", "3");
	sorted("A1", "A4", {{Range("A1", "A4"), true}});
	Check(Column(doc, p, 'A', 1, 4) == "1|2|3|", "ascending, empty last: "
		+ Column(doc, p, 'A', 1, 4));
	sorted("A1", "A4", {{Range("A1", "A4"), false}});
	Check(Column(doc, p, 'A', 1, 4) == "3|2|1|", "descending, empty last: "
		+ Column(doc, p, 'A', 1, 4));

	// equal keys keep their order ascending (the cells' order decides) and
	// are turned round descending
	doc = start;
	for (int r = 1; r <= 3; r++) {
		std::string a = "A" + std::to_string(r), b = "B" + std::to_string(r);
		Set(doc, p, a.c_str(), "5");
		Set(doc, p, b.c_str(), std::string(1, char('a' + r - 1)).c_str());
	}
	sorted("A1", "B3", {{Range("A1", "A3"), true}});
	Check(Column(doc, p, 'B', 1, 3) == "a|b|c", "ties ascending: " + Column(doc, p, 'B', 1, 3));
	sorted("A1", "B3", {{Range("A1", "A3"), false}});
	Check(Column(doc, p, 'B', 1, 3) == "c|b|a", "ties descending: " + Column(doc, p, 'B', 1, 3));

	// a second key decides ties; under a descending first key its order is
	// turned round too (each level turns the result of the levels below)
	doc = start;
	Set(doc, p, "A1", "1");
	Set(doc, p, "A2", "1");
	Set(doc, p, "A3", "2");
	Set(doc, p, "B1", "z");
	Set(doc, p, "B2", "y");
	Set(doc, p, "B3", "x");
	sorted("A1", "B3", {{Range("A1", "A3"), true}, {Range("B1", "B3"), true}});
	Check(Column(doc, p, 'B', 1, 3) == "y|z|x", "two keys: " + Column(doc, p, 'B', 1, 3));
	sorted("A1", "B3", {{Range("A1", "A3"), false}, {Range("B1", "B3"), true}});
	Check(Column(doc, p, 'B', 1, 3) == "x|z|y", "descending first key: "
		+ Column(doc, p, 'B', 1, 3));

	// formulas move as they are: a relative reference to its own row still
	// refers to its row; one from outside the range stays where it pointed
	doc = start;
	Set(doc, p, "A1", "2");
	Set(doc, p, "A2", "1");
	Set(doc, p, "B1", "=A1*10");
	Set(doc, p, "B2", "=A2*10");
	Set(doc, p, "D1", "=B1");
	sorted("A1", "B2", {{Range("A1", "A2"), true}});
	Check(Column(doc, p, 'B', 1, 2) == "=A1*10|=A2*10", "formulas: " + Column(doc, p, 'B', 1, 2));
	{
		SSBook book(doc);
		SSEvaluator e(*doc.SpreadsheetPart(p), &book, int(p));
		Check(e.Value(0, 1).number == 10 && e.Value(1, 1).number == 20 && e.Value(0, 3).number == 10,
			"the values after the sort");
	}

	// horizontal: the columns of A1:C2 by row 1
	doc = start;
	Set(doc, p, "A1", "3");
	Set(doc, p, "B1", "1");
	Set(doc, p, "C1", "2");
	Set(doc, p, "A2", "c");
	Set(doc, p, "B2", "a");
	Set(doc, p, "C2", "b");
	Check(SortCells(doc, p, Range("A1", "C2"), false, {{Range("A1", "C1"), true}}),
		"sort horizontally");
	Check(CellInput(*doc.SpreadsheetPart(p), 1, 0) == "a" && CellInput(*doc.SpreadsheetPart(p), 1, 2) == "c",
		"the columns follow");

	// TRange::Set / Name: "A2..B5" both ways; one cell alone, '$' and ':'
	// aren't ranges here
	{
		SSRange r;
		Check(ParseRangeName("a2..b5", r) && r.from.a == 1 && r.from.b == 0 && r.to.a == 4
			&& r.to.b == 1, "a2..b5");
		Check(ParseRangeName(" AB10.. AC12", r) && r.from.b == 27 && r.to.b == 28 && r.to.a == 11,
			"AB10..AC12");
		Check(!ParseRangeName("A2", r) && !ParseRangeName("$A$2..$B$5", r)
			&& !ParseRangeName("A2:B5", r) && !ParseRangeName("A0..B5", r), "not ranges");
		Check(RangeName(Range("A2", "B5")) == "A2..B5" && RangeName(Range("C3", "C3")) == "C3",
			"RangeName");
	}

	// a key not as long as the range: nothing happens
	Check(!sorted("A1", "B3", {{Range("A1", "A2"), true}}), "key shorter than the range");

	// borders stay where they were, the rest of the style moves
	doc = start;
	Set(doc, p, "A1", "2");
	Set(doc, p, "A2", "1");
	{
		SSPart& ss = *doc.EditableSpreadsheetPart(p);
		uint32_t bordered = StyleWithProperty(doc.styles, 0xffffffff,
			Property{Sig("BSiz"), {0, 0, 0, 1, 0x3f, 0x80, 0, 0}});
		for (SSCell& c : ss.cells)
			if (c.id.a == 0 && c.id.b == 0)
				SetCellStyle(doc.styles, c.style, bordered);
	}
	sorted("A1", "A2", {{Range("A1", "A2"), true}});
	{
		const SSPart& ss = *doc.SpreadsheetPart(p);
		auto hasBorder = [&](int row) {
			const SSCell* c = ss.Cell(uint16_t(row), 0);
			if (c == nullptr || c->style >= doc.styles.styles.size())
				return false;
			for (uint32_t prop : doc.styles.styles[c->style].props)
				if (doc.styles.properties[prop].type == Sig("BSiz"))
					return true;
			return false;
		};
		Check(Column(doc, p, 'A', 1, 2) == "1|2", "sorted with a border");
		Check(hasBorder(0) && !hasBorder(1), "the border stayed in A1");
	}
}


int
main(int argc, char** argv)
{
	int parts = 0;
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
			// the block of cells in use, by its first column
			SSRange r{{0xffff, 0xffff}, {0, 0}};
			for (const SSCell& c : ss.cells) {
				if (c.id.a > ss.used.to.a || c.id.b > ss.used.to.b)
					continue;
				r.from.a = std::min(r.from.a, c.id.a);
				r.from.b = std::min(r.from.b, c.id.b);
				r.to.a = std::max(r.to.a, c.id.a);
				r.to.b = std::max(r.to.b, c.id.b);
			}
			if (r.from.a >= r.to.a)
				continue;
			std::string where = std::string(argv[i]) + " part " + std::to_string(p);
			SortKey key{{r.from, {r.to.a, r.from.b}}, true};
			// each row's cells (what they hold), to find them again
			auto rowText = [&](const SSPart& part, int row) {
				std::string s;
				for (const SSCell& c : part.cells)
					if (c.id.a == row && c.id.b >= r.from.b && c.id.b <= r.to.b
							&& c.kind != Sig("NONE"))
						s += std::to_string(c.id.b) + ":" + std::to_string(c.kind) + ":"
							+ std::to_string(c.number) + ":" + c.text + ";";
				return s;
			};
			std::multiset<std::string> rowsBefore;
			for (int row = r.from.a; row <= r.to.a; row++)
				rowsBefore.insert(rowText(ss, row));
			Document doc = original;
			Check(SortCells(doc, p, r, true, {key}), where + ": sort");
			parts++;
			const SSPart& after = *doc.SpreadsheetPart(p);
			std::multiset<std::string> rowsAfter;
			for (int row = r.from.a; row <= r.to.a; row++)
				rowsAfter.insert(rowText(after, row));
			Check(rowsBefore == rowsAfter, where + ": rows moved whole");
			// sorting again gives the same, unless the key has formulas
			// (referring to other rows, their values change with the order)
			bool formulas = false;
			for (const SSCell& c : ss.cells)
				formulas |= c.id.b == r.from.b && c.kind == Sig("Fmla");
			if (formulas)
				continue;
			Document again = doc;
			Check(SortCells(again, p, r, true, {key}), where + ": sort again");
			std::string a, b;
			for (int row = r.from.a; row <= r.to.a; row++) {
				a += rowText(after, row) + "\n";
				b += rowText(*again.SpreadsheetPart(p), row) + "\n";
			}
			Check(a == b, where + ": sorting again changed the order");
		}
	}
	HandCases(argc, argv);
	printf("parts %d, ok %d, failed %d\n", parts, ok, failures);
	return failures != 0 || parts == 0;
}
