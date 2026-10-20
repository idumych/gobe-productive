// InsertRowsColumns: inserting rows or columns doesn't change any computed
// value (every cell keeps its value at its new place, formulas follow);
// deleting what was inserted gives back the same part record; deleting a
// row turns references into it into #REF! and keeps the others.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>

#include "Document.h"
#include "Editing.h"
#include "SSEval.h"

using namespace gobe;

static int failures = 0;

static void
Check(bool ok, const std::string& what)
{
	if (!ok) {
		if (failures < 20)
			printf("FAIL: %s\n", what.c_str());
		failures++;
	}
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


int main(int argc, char** argv)
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
			parts++;
			SSBook book(original);
			SSEvaluator before(ss, &book, int(p));
			// RAND and what depends on it change by themselves: left out
			SSBook book2(original);
			SSEvaluator again(ss, &book2, int(p));
			for (bool columns : {false, true}) {
				// an insertion point inside the used area
				int lo = columns ? ss.used.from.b : ss.used.from.a;
				int hi = columns ? ss.used.to.b : ss.used.to.a;
				uint16_t at = uint16_t((lo + hi + 1) / 2);
				std::string where = std::string(argv[i]) + " part " + std::to_string(p)
					+ (columns ? " columns at " : " rows at ") + std::to_string(at);

				Document doc = original;
				Check(InsertRowsColumns(doc, p, columns, at, 2), where + ": insert");
				const SSPart& moved = *doc.SpreadsheetPart(p);
				SSBook bookAfter(doc);
				SSEvaluator after(moved, &bookAfter, int(p));
				for (const SSCell& c : ss.cells) {
					uint16_t r = c.id.a, k = c.id.b;
					if ((columns ? k : r) >= at)
						(columns ? k : r) += 2;
					SSValue v0 = before.Value(c.id.a, c.id.b), v1 = after.Value(r, k);
					if (!Same(v0, again.Value(c.id.a, c.id.b)))
						continue;
					values++;
					Check(Same(v0, v1), where + ": " + CellName(c.id) + " value changed");
				}
				// and back
				Check(InsertRowsColumns(doc, p, columns, at, -2), where + ": delete");
				const SSPart& back = *doc.SpreadsheetPart(p);
				// the same record, except that the dependency block's lists of
				// cells using each link may be grouped into ranges differently
				std::vector<uint8_t> x = WriteSSPart(ss, original.parts.parts[p].data);
				std::vector<uint8_t> y = WriteSSPart(back, doc.parts.parts[p].data);
				size_t tail = x.size() - ss.depmEnd;
				Check(y.size() >= ss.depmStart + tail
						&& std::equal(x.begin(), x.begin() + ss.depmStart, y.begin())
						&& std::equal(x.end() - tail, x.end(), y.end() - tail),
					where + ": insert + delete differs");
				Check(back.dependents.size() == ss.dependents.size(),
					where + ": calculation order lost cells");
				for (size_t l = 0; l < ss.linkDepInfo.size() && l < back.linkDepInfo.size(); l++) {
					std::set<uint32_t> want, got;
					for (int pass = 0; pass < 2; pass++)
						for (const SSRange& r : (pass ? back : ss).linkDepInfo[l].cells)
							for (int row = r.from.a; row <= r.to.a; row++)
								for (int col = r.from.b; col <= r.to.b; col++)
									(pass ? got : want).insert(uint32_t(row) << 16 | col);
					Check(want == got, where + ": cells using link " + std::to_string(l));
				}
			}
		}
	}

	// named ranges are written in place on save: insert rows at the top of
	// every spreadsheet in documents that use names, save, reload, compare
	int named = 0;
	for (int i = 1; i < argc; i++) {
		Document doc;
		std::string error;
		if (!LoadDocument(argv[i], doc, error) || doc.spreadsheetParts.empty())
			continue;
		bool usesNames = false;
		for (const Selection& sel : doc.selections.selections)
			usesNames |= sel.kind == Sig("SPRD") && !sel.ranges.empty();
		if (!usesNames)
			continue;
		named++;
		// values before, twice (RAND and its dependents are left out)
		std::map<size_t, std::vector<SSValue>> before;
		std::map<size_t, std::vector<bool>> stable;
		{
			SSBook book(doc), book2(doc);
			for (const auto& [p, ss] : doc.spreadsheetParts) {
				SSEvaluator e(ss, &book, int(p)), e2(ss, &book2, int(p));
				for (const SSCell& c : ss.cells) {
					SSValue v = e.Value(c.id.a, c.id.b);
					before[p].push_back(v);
					stable[p].push_back(Same(v, e2.Value(c.id.a, c.id.b)));
				}
			}
		}
		std::vector<size_t> parts;
		for (const auto& [p, ss] : doc.spreadsheetParts)
			parts.push_back(p);
		for (size_t p : parts)
			InsertRowsColumns(doc, p, false, 0, 3);
		std::string path = std::string(getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp")
			+ "/ss_rows_save";
		bool saved = doc.Save(path, error);
		Check(saved, std::string(argv[i]) + ": save " + error);
		if (saved) {
			// cells keep their order: the k-th cell is the k-th before, 3 rows down
			SSBook book(doc);
			for (size_t p : parts) {
				const SSPart& ss = *doc.SpreadsheetPart(p);
				SSEvaluator e(ss, &book, int(p));
				Check(ss.cells.size() == before[p].size(), std::string(argv[i]) + ": cells");
				for (size_t k = 0; k < ss.cells.size() && k < before[p].size(); k++) {
					if (!stable[p][k])
						continue;
					const SSCell& c = ss.cells[k];
					Check(Same(before[p][k], e.Value(c.id.a, c.id.b)),
						std::string(argv[i]) + " after save: " + CellName(c.id));
				}
			}
		}
		remove(path.c_str());
	}
	Check(named > 0, "no document with named ranges");

	// deleting a referenced row: =A2 becomes #REF!, =A3 follows
	{
		Document doc;
		std::string error;
		for (int i = 1; i < argc && doc.spreadsheetParts.empty(); i++)
			LoadDocument(argv[i], doc, error);
		if (!doc.spreadsheetParts.empty()) {
			size_t p = doc.spreadsheetParts[0].first;
			SSPart* ss = doc.EditableSpreadsheetPart(p);
			ClearRange(*ss, 0, 0, 200, 30);
			SetCellInput(*ss, 1, 0, "5", nullptr, error);
			SetCellInput(*ss, 2, 0, "7", nullptr, error);
			SetCellInput(*ss, 5, 2, "=A2", nullptr, error);
			SetCellInput(*ss, 6, 2, "=A3*2", nullptr, error);
			SetCellInput(*ss, 7, 2, "=SUM(A2:A3)", nullptr, error);
			InsertRowsColumns(doc, p, false, 1, -1);	// delete row 2
			const SSPart& after = *doc.SpreadsheetPart(p);
			SSBook book(doc);
			SSEvaluator e(after, &book, int(p));
			Check(e.Value(4, 2).type == SSValue::kError, "=A2 after deleting row 2 is an error");
			Check(e.Value(5, 2).number == 14, "=A3*2 follows to =A2*2");
			Check(e.Value(6, 2).number == 7, "SUM(A2:A3) shrinks to A2:A2");
			Check(CellInput(after, 5, 2) == "=A2*2", "formula text " + CellInput(after, 5, 2));
		}
	}
	printf("spreadsheet parts ok %d, failed %d; values compared %d\n", failures ? 0 : parts,
		failures, values);
	return failures != 0;
}
