// ss_sylk: every sample spreadsheet part written as SYLK (SpreadsheetToSylk,
// WriteSylk) and read back (ReadSylk): the same cells with their values as
// calculated, each formula back as typed through R1C1 (FormulaToSylk,
// FormulaFromSylk), the bounds, the widths of the columns that aren't the
// default; imported into a new spreadsheet (SylkToSpreadsheet), the same
// formulas and values.
// Arguments: the sample documents.
#include <cmath>
#include <cstdio>
#include <map>

#include "Document.h"
#include "Editing.h"
#include "SSEval.h"
#include "SSStyle.h"
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
						Check(c.kind == SylkCell::kNumber && (c.number == v.number
							|| (c.format >= 0 && std::fabs(c.number - v.number - 24106) < 1e-9)),
							name + ": number");
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
					std::string again = FormulaFromSylk(c.formula, cell.id.a, cell.id.b);
					Check(again == input, name + ": formula " + input + " -> " + c.formula + " -> "
						+ again);
				} else
					Check(c.formula.empty(), name + ": no formula");
			}
			Check(volatileCells < 20, where + ": few cells change between calculations");
			// imported into a new spreadsheet: every formula as typed, the
			// other cells' values (times come back as numbers)
			{
				Document imported;
				int bad = 0;
				Check(CreateBlankSpreadsheet(imported, error)
					&& SylkToSpreadsheet(imported, 0, back, bad), where + ": imported " + error);
				// formulas naming a selection (or another part's) can't be
				// read without the names, which SYLK doesn't carry here
				int named = 0;
				for (const SSCell& cell : ss.cells)
					for (const FormulaToken& t : cell.formula)
						if (t.kind == 0x1b || t.kind == 0x1e || t.kind == 0x1f) {
							named++;
							break;
						}
				Check(bad <= named, where + ": every formula without names read back: "
					+ std::to_string(bad) + " not, " + std::to_string(named) + " with names");
				const SSPart& is = *imported.SpreadsheetPart(0);
				SSBook ib(imported);
				SSEvaluator ie(is, &ib, 0);
				for (const SSCell& cell : ss.cells) {
					if (cell.id.b == 0xffff)
						continue;
					std::string name = where + " " + CellName(cell.id) + " imported";
					bool formula = cell.kind == Sig("Fmla") || cell.kind == Sig("FmEr");
					bool usesNames = false;
					for (const FormulaToken& t : cell.formula)
						usesNames |= t.kind == 0x1b || t.kind == 0x1e || t.kind == 0x1f;
					if (usesNames)
						continue;
					if (formula) {
						// (a formula naming another part or a name isn't in the
						// new document: it stays a formula, its value differs)
						Check(CellInput(is, cell.id.a, cell.id.b) == CellInput(ss, cell.id.a,
							cell.id.b), name + ": formula " + CellInput(is, cell.id.a, cell.id.b) + " for "
							+ CellInput(ss, cell.id.a, cell.id.b));
						continue;
					}
					SSValue v = eval.Value(cell.id.a, cell.id.b);
					SSValue w = ie.Value(cell.id.a, cell.id.b);
					// the format comes along (F;P), and a date or time format
					// turns the number back into Gobe's days
					auto formatOf = [&](const SSPart& part, const SSCell* c, uint16_t r, uint16_t col) {
						std::vector<uint32_t> stack = {part.style};
						if (const SSHeader* h = part.Column(col))
							stack.push_back(h->cell.style);
						if (const SSHeader* h = part.Row(r))
							stack.push_back(h->cell.style);
						if (c != nullptr)
							stack.push_back(c->style);
						std::string f = ResolveCellStyle(doc.styles, stack).format;
						return f.empty() ? std::string("General") : f;
					};
					std::string format = formatOf(ss, &cell, cell.id.a, cell.id.b);
					std::string importedFormat = [&]() {
						std::vector<uint32_t> stack = {is.style};
						if (const SSCell* c = is.Cell(cell.id.a, cell.id.b))
							stack.push_back(c->style);
						std::string f = ResolveCellStyle(imported.styles, stack).format;
						return f.empty() ? std::string("General") : f;
					}();
					if (v.type != SSValue::kNone)
						Check(importedFormat == format, name + ": format " + format + " / "
							+ importedFormat);
					bool date = IsDateNumberFormat(format) && format != "General";
					switch (v.type) {
						case SSValue::kNumber:
							Check(w.type == v.type && std::fabs(w.number - v.number) < 1e-6,
								name + ": number");
							break;
						case SSValue::kBool:
							Check(w.type == v.type && w.number == v.number, name + ": bool");
							break;
						case SSValue::kTime:
							Check(w.type == SSValue::kNumber && std::fabs(w.number
								- (v.time / 86400.0 + (date ? 0 : 24106))) < 1e-6,
								name + ": time as a number");
							break;
						case SSValue::kString:
							Check(w.type == SSValue::kString && w.text == v.text, name + ": text");
							break;
						case SSValue::kError:
							Check(w.type == SSValue::kError, name + ": error");
							break;
						default:
							break;
					}
				}
				for (const SSHeader& column : ss.columns)
					if (column.index < 0xffff && column.cell.number != ss.defaultWidth
							&& std::lround(column.cell.number / 7.45) > 0)
						Check(std::fabs(is.ColumnWidth(uint16_t(column.index))
							- std::lround(column.cell.number / 7.45) * 7) < 0.01,
							where + ": imported column " + std::to_string(column.index) + " width");
			}
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
