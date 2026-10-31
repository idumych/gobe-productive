// DependentCells: for a part and a list of selections, the formula cells that
// would be invalidated (the formulas in the selections themselves, then every
// formula that refers to them, transitively). Brute-force oracle on every
// sample document's spreadsheet parts.
#include <cstdio>
#include <algorithm>
#include <cstdlib>
#include <vector>
#include <string>

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


// ---------------------------------------------------------------------------
// Oracle: compute the same set as DependentCells, using the spec directly.
// ---------------------------------------------------------------------------

// Check if a cell intersects any range in a list of ranges.
static bool
CellInSelections(const std::vector<SSRange>& selections, CellID cell)
{
	for (const auto& r : selections) {
		uint16_t r0 = r.from.a < r.to.a ? r.from.a : r.to.a;
		uint16_t r1 = r.from.a > r.to.a ? r.from.a : r.to.a;
		uint16_t c0 = r.from.b < r.to.b ? r.from.b : r.to.b;
		uint16_t c1 = r.from.b > r.to.b ? r.from.b : r.to.b;
		if (cell.a >= r0 && cell.a <= r1 && cell.b >= c0 && cell.b <= c1)
			return true;
	}
	return false;
}

// Check if a range (from..to) intersects any range in a list of ranges.
static bool
RangeIntersectsSelections(const SSRange& rng, const std::vector<SSRange>& selections)
{
	uint16_t r0 = rng.from.a < rng.to.a ? rng.from.a : rng.to.a;
	uint16_t r1 = rng.from.a > rng.to.a ? rng.from.a : rng.to.a;
	uint16_t c0 = rng.from.b < rng.to.b ? rng.from.b : rng.to.b;
	uint16_t c1 = rng.from.b > rng.to.b ? rng.from.b : rng.to.b;

	for (const auto& sr : selections) {
		uint16_t sr0 = sr.from.a < sr.to.a ? sr.from.a : sr.to.a;
		uint16_t sr1 = sr.from.a > sr.to.a ? sr.from.a : sr.to.a;
		uint16_t sc0 = sr.from.b < sr.to.b ? sr.from.b : sr.to.b;
		uint16_t sc1 = sr.from.b > sr.to.b ? sr.from.b : sr.to.b;
		if (r1 >= sr0 && r0 <= sr1 && c1 >= sc0 && c0 <= sc1)
			return true;
	}
	return false;
}

// Check if a cell is in the result set (sorted, unique).
static bool
CellInResult(const std::vector<CellID>& result, CellID cell)
{
	for (const auto& id : result) {
		if (id.a == cell.a && id.b == cell.b)
			return true;
	}
	return false;
}

// Does cell F (formula at F.id) have a reference that either meets `selections`
// or refers to a cell already in `result`?
// For 0x19: resolves to one ActualCell.
// For 0x1a: resolves to a range, check if that range meets selections or
//           any cell of it is in result.
// Other token kinds: ignored.
static bool
FormulaRefersToSelectionOrResult(const SSCell& F,
		const std::vector<SSRange>& selections,
		const std::vector<CellID>& result)
{
	for (const auto& token : F.formula) {
		if (token.kind == 0x19 && !token.refs.empty()) {
			if (token.refs[0].flags & 1) continue; // invalid
			CellID target = ActualCell(token.refs[0], F.id);
			if (CellInSelections(selections, target) || CellInResult(result, target))
				return true;
		}
		if (token.kind == 0x1a && token.refs.size() >= 2) {
			if (token.refs[0].flags & 1 || token.refs[1].flags & 1) continue;
			CellID a = ActualCell(token.refs[0], F.id);
			CellID b = ActualCell(token.refs[1], F.id);
			// Check if the range [min(a,b), max(a,b)] meets selections
			SSRange rng{a, b};
			if (RangeIntersectsSelections(rng, selections))
				return true;
			// Check if any cell of the range is in result
			uint16_t r0 = a.a < b.a ? a.a : b.a;
			uint16_t r1 = a.a > b.a ? a.a : b.a;
			uint16_t c0 = a.b < b.b ? a.b : b.b;
			uint16_t c1 = a.b > b.b ? a.b : b.b;
			for (uint16_t r = r0; r <= r1; r++) {
				for (uint16_t c = c0; c <= c1; c++) {
					if (CellInResult(result, CellID{r, c}))
						return true;
				}
			}
		}
		// other token kinds: ignore
	}
	return false;
}

// Oracle: compute the smallest set D such that:
// 1. every formula cell whose id is inside a range of `selections` is in D;
// 2. a formula cell F is in D if one of its references meets `selections`
//    (intersects a range of it) or refers to a cell already in D.
// Returns the set sorted by cell id.
static std::vector<CellID>
DependentCellsOracle(const SSPart& part, const std::vector<SSRange>& selections)
{
	// Collect all formula cells.
	std::vector<const SSCell*> formulaCells;
	for (const auto& c : part.cells)
		if (c.kind == Sig("Fmla"))
			formulaCells.push_back(&c);

	// Oracle result.
	std::vector<CellID> result;

	// Pass 1: rule 1 — formula cells whose id is inside a selection range.
	for (const auto* fc : formulaCells) {
		if (CellInSelections(selections, fc->id)) {
			result.push_back(fc->id);
		}
	}

	// Fixed-point: rule 2 — add formula cells whose references meet selections
	// or refer to cells already in result.
	bool changed = true;
	while (changed) {
		changed = false;
		for (const auto* fc : formulaCells) {
			// Already in result?
			bool in = false;
			for (const auto& id : result) {
				if (id.a == fc->id.a && id.b == fc->id.b) {
					in = true;
					break;
				}
			}
			if (in) continue;
			// Check rule 2.
			if (FormulaRefersToSelectionOrResult(*fc, selections, result)) {
				result.push_back(fc->id);
				changed = true;
			}
		}
	}

	std::sort(result.begin(), result.end(), [](CellID a, CellID b) {
		if (a.a != b.a) return a.a < b.a;
		return a.b < b.b;
	});
	return result;
}


int
main(int argc, char** argv)
{
	int parts = 0, checks = 0;
	std::string error;

	for (int i = 1; i < argc; i++) {
		Document doc;
		if (!LoadDocument(argv[i], doc, error))
			continue;
		for (const auto& entry : doc.spreadsheetParts) {
			size_t p = entry.first;
			SSPart* ssPtr = doc.EditableSpreadsheetPart(p);
			if (!ssPtr) continue;
			SSPart& ss = *ssPtr;

			// Count formula cells.
			std::vector<const SSCell*> formulaCells;
			for (const auto& c : ss.cells)
				if (c.kind == Sig("Fmla"))
					formulaCells.push_back(&c);
			if (formulaCells.empty())
				continue;

			// UpdateDependencies first.
			UpdateDependencies(doc, p);

			// Skip if any formula cell has flags & 4 (circular).
			bool hasCircular = false;
			for (const auto* fc : formulaCells)
				if (fc->flags & 4) {
					hasCircular = true;
					break;
				}
			if (hasCircular)
				continue;

			parts++;

			// Build the list of selections to check.
			std::vector<SSRange> selections;

			// a) 20 single cells: ss.cells[k * step] for k = 0..19.
			if (!ss.cells.empty()) {
				int step = int(ss.cells.size()) / 20;
				if (step < 1) step = 1;
				for (int k = 0; k < 20 && size_t(k) * step < ss.cells.size(); k++) {
					CellID id = ss.cells[k * step].id;
					selections.push_back({id, id});
				}
			}

			// b) the whole used range.
			selections.push_back(ss.used);

			// c) first 3 columns of used range.
			for (int col = 0; col < 3; col++) {
				uint16_t c = ss.used.from.b + col;
				if (c > ss.used.to.b) break;
				SSRange colRange{{ss.used.from.a, c}, {ss.used.to.a, c}};
				selections.push_back(colRange);
			}

			// For each selection, compare oracle against DependentCells.
			for (const auto& sel : selections) {
				auto oracle = DependentCellsOracle(ss, {sel});
				auto result = DependentCells(ss, {sel});

				// Sort both.
				std::sort(oracle.begin(), oracle.end(), [](CellID a, CellID b) {
					if (a.a != b.a) return a.a < b.a;
					return a.b < b.b;
				});
				std::sort(result.begin(), result.end(), [](CellID a, CellID b) {
					if (a.a != b.a) return a.a < b.a;
					return a.b < b.b;
				});

				// Build a label for this selection.
				std::string label;
				if (sel.from.a == sel.to.a && sel.from.b == sel.to.b) {
					label = CellName(sel.from);
				} else {
					label = CellName(sel.from) + ":" + CellName(sel.to);
				}

				// Compare as sets.
				bool same = (oracle.size() == result.size());
				if (same) {
					for (size_t s = 0; s < oracle.size(); s++) {
						if (oracle[s].a != result[s].a || oracle[s].b != result[s].b) {
							same = false;
							break;
						}
					}
				}

				checks++;
				if (!same) {
					// Collect first 5 differing cell names.
					std::vector<std::string> diffs;
					size_t oi = 0, ri = 0;
					while ((oi < oracle.size() || ri < result.size()) && diffs.size() < 5) {
						if (oi >= oracle.size()) {
							diffs.push_back("extra " + CellName(result[ri]));
							ri++;
						} else if (ri >= result.size()) {
							diffs.push_back("missing " + CellName(oracle[oi]));
							oi++;
						} else if (oracle[oi].a != result[ri].a ||
						           oracle[oi].b != result[ri].b) {
							diffs.push_back(CellName(oracle[oi]) + "!=" + CellName(result[ri]));
							oi++; ri++;
						} else {
							oi++; ri++;
						}
					}
					std::string desc = std::string(argv[i]) + " part " + std::to_string(p)
					                   + " " + label;
					if (!diffs.empty()) {
						desc += ": ";
						for (size_t d = 0; d < diffs.size(); d++) {
							if (d > 0) desc += ", ";
							desc += diffs[d];
						}
					}
					Check(false, desc);
				} else {
					ok++;
				}
			}
		}
	}
	printf("parts %d, checks %d, ok %d, failed %d\n", parts, checks, ok, failures);
	return failures != 0 || parts == 0 || checks == 0;
}
