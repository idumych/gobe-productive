// Circular references (TSSDepManager::FixFormulaCells and VisitFormula):
// after UpdateDependencies every formula cell's flags have 1 and 2 set
// (visiting, visited), and 4 (TFormulaCell::Circular) when it is in a
// circle of references or refers to a formula that is. The samples have no
// circles: their formulas keep the flags they were saved with.
#include <cstdio>
#include <algorithm>
#include <cstdlib>

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


int
main(int argc, char** argv)
{
	int parts = 0;
	std::string error;
	Document scratch;
	size_t scratchPart = 0;
	bool haveScratch = false;
	for (int i = 1; i < argc; i++) {
		Document doc;
		if (!LoadDocument(argv[i], doc, error))
			continue;
		for (const auto& entry : doc.spreadsheetParts) {
			size_t p = entry.first;
			std::vector<uint16_t> before;
			for (const SSCell& c : doc.SpreadsheetPart(p)->cells)
				if (c.kind == Sig("Fmla"))
					before.push_back(c.flags);
			if (before.empty())
				continue;
			parts++;
			UpdateDependencies(doc, p);
			std::vector<uint16_t> after;
			for (const SSCell& c : doc.SpreadsheetPart(p)->cells)
				if (c.kind == Sig("Fmla"))
					after.push_back(c.flags);
			Check(before == after, std::string(argv[i]) + " part " + std::to_string(p)
				+ ": formula flags changed");
			const SSPart& ss = *doc.SpreadsheetPart(p);
			if (!haveScratch && ss.used.to.a >= 20 && ss.used.to.b >= 8) {
				scratch = doc;
				scratchPart = p;
				haveScratch = true;
			}
		}
	}
	Check(haveScratch, "a spreadsheet part for the small cases");
	if (haveScratch) {
		SSPart& ss = *scratch.EditableSpreadsheetPart(scratchPart);
		ss.cells.clear();
		ss.dependents.clear();
		auto set = [&](const char* cell, const char* input) {
			uint16_t column = uint16_t(cell[0] - 'A'), row = uint16_t(atoi(cell + 1) - 1);
			SetCellInput(ss, row, column, input, nullptr, error);
		};
		set("A1", "=B1");
		set("B1", "=A1");
		set("C1", "=A1+1");		// refers to a circle
		set("D1", "=5");
		set("E1", "=SUM(E2:E3)");
		set("E2", "=E1");
		set("F1", "=F1");		// itself
		set("G1", "=D1*2");
		UpdateDependencies(scratch, scratchPart);
		auto flags = [&](uint16_t column) {
			const SSCell* c = ss.Cell(0, column);
			return c == nullptr ? -1 : int(c->flags & 7);
		};
		Check(flags(0) == 7 && flags(1) == 7, "A1 and B1 refer to each other");
		Check(flags(2) == 7, "C1 refers to the circle");
		Check(flags(3) == 3 && flags(6) == 3, "D1 and G1 aren't circular");
		Check(flags(4) == 7 && ss.Cell(1, 4) != nullptr && (ss.Cell(1, 4)->flags & 7) == 7,
			"E1 and E2 through a range");
		Check(flags(5) == 7, "F1 refers to itself");
		// the circle broken: the marks go
		set("B1", "1");
		UpdateDependencies(scratch, scratchPart);
		Check(flags(0) == 3 && flags(2) == 3, "no circle after B1 is a number");

		// Select Dependents (TSSDepManager::CellsChanged): the formulas in
		// the cells and those that depend on them, through others too
		ss.cells.clear();
		ss.dependents.clear();
		set("A1", "1");
		set("B1", "=A1*2");
		set("C1", "=B1+1");
		set("D1", "5");
		set("E1", "=SUM(A1:A3)");
		set("F1", "=D1");
		set("G1", "=F1+C1");
		UpdateDependencies(scratch, scratchPart);
		auto names = [&](const char* from, const char* to) {
			SSRange r{{uint16_t(atoi(from + 1) - 1), uint16_t(from[0] - 'A')},
				{uint16_t(atoi(to + 1) - 1), uint16_t(to[0] - 'A')}};
			std::vector<CellID> cells = DependentCells(ss, {r});
			std::vector<std::string> out;
			for (CellID id : cells)
				out.push_back(CellName(id));
			std::sort(out.begin(), out.end());
			std::string s;
			for (const std::string& n : out)
				s += n + " ";
			return s;
		};
		Check(names("A1", "A1") == "B1 C1 E1 G1 ", "dependents of A1: " + names("A1", "A1"));
		Check(names("B1", "B1") == "B1 C1 G1 ", "of B1 (itself a formula): " + names("B1", "B1"));
		Check(names("D1", "D1") == "F1 G1 ", "of D1: " + names("D1", "D1"));
		Check(names("A3", "A3") == "E1 ", "of A3 (empty, in a range): " + names("A3", "A3"));
		Check(names("H5", "H9") == "", "of cells nothing refers to");
	}
	printf("parts %d, ok %d, failed %d\n", parts, ok, failures);
	return failures != 0 || parts == 0;
}
