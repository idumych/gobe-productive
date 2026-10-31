// SortCells: for every sample spreadsheet's part and block, verify that
// SortCells puts rows in the correct order per the spec (ascending and
// descending by the first column).
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "Document.h"
#include "SSEval.h"
#include "SSPart.h"
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


// ---------------------------------------------------------------------------
// Rank helpers — the sort order spec's rank table.
// ---------------------------------------------------------------------------

static int32_t
RankOf(SSValue::Type type)
{
	switch (type) {
		case SSValue::kNumber:
		case SSValue::kTime:
			return 100;
		case SSValue::kString:
			return 200;
		case SSValue::kBool:
			return 300;
		case SSValue::kNone:
			return 32000;
		case SSValue::kError:
		case SSValue::kRange:
			return 400;
		default:
			return 32000;
	}
}

// Compare two values ascending: returns <0, 0, or >0.
static int
CompareAsc(const SSValue& a, const SSValue& b)
{
	int32_t ra = RankOf(a.type);
	int32_t rb = RankOf(b.type);
	if (ra != rb)
		return ra - rb;

	// Same rank — compare within rank.
	switch (a.type) {
		case SSValue::kNone:
			return 0;
		case SSValue::kError:
			return a.error - b.error;
		case SSValue::kBool:
			return (int)a.number - (int)b.number;
		case SSValue::kNumber:
		case SSValue::kTime: {
			double va = 0, vb = 0;
			a.GetNum(va);
			b.GetNum(vb);
			if (va < vb) return -1;
			if (va > vb) return 1;
			return 0;
		}
		case SSValue::kString:
			return strcmp(a.text.c_str(), b.text.c_str());
		default:
			return 0;
	}
}



// ---------------------------------------------------------------------------
// Main test logic.
// ---------------------------------------------------------------------------

int
main(int argc, char** argv)
{
	int parts = 0, checks = 0;

	for (int i = 1; i < argc; i++) {
		Document doc;
		std::string error;
		if (!LoadDocument(argv[i], doc, error))
			continue;

		for (const auto& entry : doc.spreadsheetParts) {
			size_t p = entry.first;
			SSPart* ssPtr = doc.EditableSpreadsheetPart(p);
			if (!ssPtr)
				continue;
			SSPart& ss = *ssPtr;

			// Find the smallest block holding every cell of the part inside
			// ss.used.
			SSRange block = ss.used;
			for (const auto& c : ss.cells) {
				if (c.id.a < block.from.a) block.from.a = c.id.a;
				if (c.id.a > block.to.a)  block.to.a  = c.id.a;
				if (c.id.b < block.from.b) block.from.b = c.id.b;
				if (c.id.b > block.to.b)  block.to.b  = c.id.b;
			}

			// Skip if block is one row high.
			if (block.from.a == block.to.a)
				continue;

			// Skip if any cell in the first column of the block is a formula.
			uint16_t firstCol = block.from.b;
			bool skip = false;
			for (int r = (int)block.from.a; r <= (int)block.to.a; r++) {
				const SSCell* cell = ss.Cell(r, firstCol);
				if (cell && cell->kind == Sig("Fmla")) {
					skip = true;
					break;
				}
			}
			if (skip)
				continue;

			parts++;

			// ---- Ascending sort ----
			Document docAsc = doc;
			bool sortOk = SortCells(docAsc, p, block, true,
				{SortKey{SSRange{block.from, {block.to.a, block.from.b}}, true}});
			checks++;
			Check(sortOk, std::string(argv[i]) + " part " + std::to_string(p)
				+ " asc SortCells returned false");

			// Cell count preserved.
			SSPart* ssAsc = docAsc.EditableSpreadsheetPart(p);
			if (ssAsc) {
				size_t countAfter = ssAsc->cells.size();
				size_t countBefore = ss.cells.size();
				checks++;
				Check(countAfter == countBefore,
					std::string(argv[i]) + " part " + std::to_string(p)
					+ " asc cell count " + std::to_string(countAfter)
					+ " != " + std::to_string(countBefore));
			}

			// Verify ascending order using evaluator.
			SSBook bookAsc(docAsc);
			SSEvaluator eval(*ssAsc, &bookAsc, (int)p);
			bool ascOk = true;
			for (int r = (int)block.from.a; r + 1 <= (int)block.to.a; r++) {
				SSValue v1 = eval.Value(r, firstCol);
				SSValue v2 = eval.Value(r + 1, firstCol);
				checks++;
				if (CompareAsc(v1, v2) > 0) {
					if (failures < 40) {
						std::string desc = std::string(argv[i]) + " part "
							+ std::to_string(p) + " asc: "
							+ CellName(CellID{(uint16_t)r, firstCol})
							+ " vs "
							+ CellName(CellID{(uint16_t)(r + 1), firstCol});
						printf("FAIL: %s\n", desc.c_str());
					}
					failures++;
					ascOk = false;
				}
			}
			if (ascOk)
				ok++;

			// ---- Descending sort ----
			Document docDesc = doc;
			sortOk = SortCells(docDesc, p, block, true,
				{SortKey{SSRange{block.from, {block.to.a, block.from.b}}, false}});
			checks++;
			Check(sortOk, std::string(argv[i]) + " part " + std::to_string(p)
				+ " desc SortCells returned false");

			// Cell count preserved.
			SSPart* ssDesc = docDesc.EditableSpreadsheetPart(p);
			if (ssDesc) {
				size_t countAfter = ssDesc->cells.size();
				size_t countBefore = ss.cells.size();
				checks++;
				Check(countAfter == countBefore,
					std::string(argv[i]) + " part " + std::to_string(p)
					+ " desc cell count " + std::to_string(countAfter)
					+ " != " + std::to_string(countBefore));
			}

			// Verify descending order using evaluator.
			SSBook bookDesc(docDesc);
			SSEvaluator evalDesc(*ssDesc, &bookDesc, (int)p);
			bool descOk = true;
			for (int r = (int)block.from.a; r + 1 <= (int)block.to.a; r++) {
				SSValue v1 = evalDesc.Value(r, firstCol);
				SSValue v2 = evalDesc.Value(r + 1, firstCol);
				checks++;
				// Descending: a >= b ascending, with empty cells always last.
				if (v2.type == SSValue::kNone && v1.type != SSValue::kNone) {
					// non-empty before empty — OK (empty at bottom)
				} else if (v1.type == SSValue::kNone && v2.type != SSValue::kNone) {
					// empty before non-empty — wrong
					if (failures < 40) {
						std::string desc = std::string(argv[i]) + " part "
							+ std::to_string(p) + " desc: "
							+ CellName(CellID{(uint16_t)r, firstCol})
							+ " vs "
							+ CellName(CellID{(uint16_t)(r + 1), firstCol});
						printf("FAIL: %s\n", desc.c_str());
					}
					failures++;
					descOk = false;
				} else if (v1.type == SSValue::kNone && v2.type == SSValue::kNone) {
					// both empty — fine
				} else if (CompareAsc(v1, v2) < 0) {
					// a < b ascending means a should come after b in descending
					if (failures < 40) {
						std::string desc = std::string(argv[i]) + " part "
							+ std::to_string(p) + " desc: "
							+ CellName(CellID{(uint16_t)r, firstCol})
							+ " vs "
							+ CellName(CellID{(uint16_t)(r + 1), firstCol});
						printf("FAIL: %s\n", desc.c_str());
					}
					failures++;
					descOk = false;
				}
			}
			if (descOk)
				ok++;
		}
	}

	printf("parts %d, checks %d, ok %d, failed %d\n", parts, checks, ok, failures);
	return failures != 0 || parts == 0 || checks == 0;
}
