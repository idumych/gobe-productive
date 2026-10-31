// ss_sylk: every sample spreadsheet part written as SYLK (SpreadsheetToSylk,
// WriteSylk) and read back (ReadSylk): the same cells with their values as
// calculated, each formula back as typed through R1C1 (FormulaToSylk,
// FormulaFromSylk), the bounds, the widths of the columns that aren't the
// default.
// Arguments: the sample documents.
#include <cmath>
#include <cstdio>
#include <map>

#include "Document.h"
#include "Editing.h"
#include "SSEval.h"
#include "SSSylk.h"

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
	int parts = 0, cells = 0, formulas = 0;
	for (int i = 1; i < argc; i++) {
		Document doc;
		std::string error;
		if (!LoadDocument(argv[i], doc, error))
			continue;
		for (const auto& entry : doc.spreadsheetParts) {
			size_t p = entry.first;
			const SSPart& ss = *doc.SpreadsheetPart(p);
			std::string where = std::string(argv[i]) + " part " + std::to_string(p);
			SylkSheet sheet = SpreadsheetToSylk(doc, p);
			SylkSheet back;
			Check(ReadSylk(WriteSylk(sheet), back, error), where + ": read back " + error);
			Check(back.cells.size() == sheet.cells.size(), where + ": as many cells");
			if (back.cells.size() != sheet.cells.size())
				continue;
			parts++;
			SSBook book(doc), book2(doc);
			SSEvaluator eval(ss, &book, int(p)), eval2(ss, &book2, int(p));
			int volatileCells = 0;
			// every cell with a value or a formula is there, once
			std::map<std::pair<int, int>, const SylkCell*> byCell;
			for (const SylkCell& c : back.cells)
				byCell[{c.row, c.column}] = &c;
			Check(byCell.size() == back.cells.size(), where + ": each cell once");
			for (const SSCell& cell : ss.cells) {
				if (cell.id.b == 0xffff)
					continue;
				std::string name = where + " " + CellName(cell.id);
				SSValue v = eval.Value(cell.id.a, cell.id.b);
				bool formula = cell.kind == Sig("Fmla") || cell.kind == Sig("FmEr");
				auto found = byCell.find({cell.id.a, cell.id.b});
				if (v.type == SSValue::kNone && !formula) {
					Check(found == byCell.end(), name + ": an empty cell left out");
					continue;
				}
				if (found == byCell.end()) {
					Check(false, name + ": missing");
					continue;
				}
				const SylkCell& c = *found->second;
				cells++;
				// a value that changes from one calculation to the next
				// (RAND, in SuperMind) isn't compared
				SSValue v2 = eval2.Value(cell.id.a, cell.id.b);
				bool same = v2.type == v.type && v2.number == v.number && v2.text == v.text
					&& v2.time == v.time && v2.error == v.error;
				if (!same) {
					volatileCells++;
					v.type = SSValue::kRange;	// (not compared below)
				}
				switch (v.type) {
					case SSValue::kRange:
						break;
					case SSValue::kNumber:
						Check(c.kind == SylkCell::kNumber && c.number == v.number, name + ": number");
						break;
					case SSValue::kTime:
						Check(c.kind == SylkCell::kNumber
							&& std::fabs(c.number - (v.time / 86400.0 + 24106)) < 1e-9, name + ": time");
						break;
					case SSValue::kBool:
						Check(c.kind == SylkCell::kBool && c.number == v.number, name + ": bool");
						break;
					case SSValue::kString:
						Check(c.kind == SylkCell::kString && c.text == v.text, name + ": text");
						break;
					case SSValue::kError:
						Check(c.kind == SylkCell::kError && !c.text.empty(), name + ": error");
						break;
					default:
						Check(c.kind == SylkCell::kEmpty, name + ": no value");
						break;
				}
				if (formula) {
					formulas++;
					std::string input = CellInput(ss, cell.id.a, cell.id.b);
					Check(!c.formula.empty() && c.formula[0] != '=', name + ": an E field");
					// (rtd_C_field_E writes a range as "..", UnParse as ':';
					// the parser reads both)
					std::string again = FormulaFromSylk(c.formula, cell.id.a, cell.id.b);
					bool quoted = false;
					for (size_t k = 0; k + 1 < again.size(); k++) {
						quoted ^= again[k] == '"';
						if (!quoted && again[k] == '.' && again[k + 1] == '.')
							again.replace(k, 2, ":");
					}
					Check(again == input, name + ": formula " + input + " -> " + c.formula + " -> "
						+ again);
				} else
					Check(c.formula.empty(), name + ": no formula");
			}
			Check(volatileCells < 20, where + ": few cells change between calculations");
			if (!sheet.cells.empty())
				Check(back.rows == ss.used.to.a + 1 && back.columns == ss.used.to.b + 1,
					where + ": bounds");
			for (const SSHeader& column : ss.columns)
				if (column.index < 0xffff && column.cell.number != ss.defaultWidth) {
					auto w = back.columnWidths.find(uint16_t(column.index));
					Check(w != back.columnWidths.end()
						&& w->second == int(std::lround(column.cell.number / 7.45)),
						where + ": column " + std::to_string(column.index) + " width");
				}
		}
	}
	printf("parts %d, cells %d, formulas %d, ok %d, failed %d\n", parts, cells, formulas, ok,
		failures);
	return failures != 0 || parts == 0 || cells == 0 || formulas == 0;
}
