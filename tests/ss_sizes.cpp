// SetLineSize: sets column widths and row heights of a spreadsheet part.
// Also verifies that reading the sizes back matches what was set, and that
// size 0 reverts to the default.
#include <algorithm>
#include <cstdio>
#include <string>

#include "Document.h"
#include "Editing.h"
#include "SSPart.h"

using namespace gobe;

static int ok = 0, failed = 0;

static void Check(bool cond, const std::string& desc)
{
	if (cond) {
		ok++;
		return;
	}
	failed++;
	if (failed <= 20)
		printf("FAIL: %s\n", desc.c_str());
}

int main(int argc, char** argv)
{
	// Load the first document that has a spreadsheet part with NO custom
	// column/row headers (the test verifies that SetLineSize correctly
	// creates headers from a clean state).
	Document doc;
	std::string error;
	bool found = false;
	for (int i = 1; i < argc; i++) {
		Document doc2;
		std::string err2;
		if (LoadDocument(argv[i], doc2, err2) && !doc2.spreadsheetParts.empty()) {
			// Check if any column/row header has a non-default size.
			// We want a "clean" spreadsheet where no headers are set yet.
			bool clean = true;
			for (auto& [idx, ss] : doc2.spreadsheetParts) {
				for (auto& h : ss.columns)
					if (h.cell.number != ss.defaultWidth) { clean = false; break; }
				for (auto& h : ss.rows)
					if (h.cell.number != ss.defaultHeight) { clean = false; break; }
				if (!clean) break;
			}
			if (clean) {
				doc = std::move(doc2);
				found = true;
				break;
			}
		}
	}
	if (!found) {
		printf("sizes ok 0, failed 0\n");
		return 0;
	}

	size_t partIdx = doc.spreadsheetParts.begin()->first;
	SSPart* ss = doc.EditableSpreadsheetPart(partIdx);
	if (ss == nullptr) {
		printf("sizes ok 0, failed 0\n");
		return 0;
	}

	// --- Default values ---
	float defW = ss->defaultWidth;
	float defH = ss->defaultHeight;
	Check(defW > 0, "default column width is positive");
	Check(defH > 0, "default row height is positive");

	// --- Read default column width / row height (no headers set) ---
	float w0 = ss->ColumnWidth(0);
	float h0 = ss->RowHeight(0);
	Check(w0 == defW, "column 0 width is default before any change");
	Check(h0 == defH, "row 0 height is default before any change");

	// --- Set column width ---
	bool changed = SetLineSize(*ss, true, 0, 0, 120.0f);
	Check(changed, "SetLineSize column width returns true on first set");
	Check(ss->ColumnWidth(0) == 120.0f, "column 0 width is 120 after SetLineSize");
	Check(ss->ColumnWidth(1) == defW, "column 1 still uses default");

	// --- Set row height ---
	changed = SetLineSize(*ss, false, 0, 0, 30.0f);
	Check(changed, "SetLineSize row height returns true on first set");
	Check(ss->RowHeight(0) == 30.0f, "row 0 height is 30 after SetLineSize");
	Check(ss->RowHeight(1) == defH, "row 1 still uses default");

	// --- Set a range ---
	changed = SetLineSize(*ss, true, 1, 3, 90.0f);
	Check(changed, "SetLineSize column range returns true");
	Check(ss->ColumnWidth(1) == 90.0f, "column 1 width is 90");
	Check(ss->ColumnWidth(2) == 90.0f, "column 2 width is 90");
	Check(ss->ColumnWidth(3) == 90.0f, "column 3 width is 90");
	Check(ss->ColumnWidth(4) == defW, "column 4 still default");

	changed = SetLineSize(*ss, false, 1, 2, 25.0f);
	Check(changed, "SetLineSize row range returns true");
	Check(ss->RowHeight(1) == 25.0f, "row 1 height is 25");
	Check(ss->RowHeight(2) == 25.0f, "row 2 height is 25");
	Check(ss->RowHeight(3) == defH, "row 3 still default");

	// --- Change existing header ---
	changed = SetLineSize(*ss, true, 0, 0, 200.0f);
	Check(changed, "SetLineSize returns true on value change");
	Check(ss->ColumnWidth(0) == 200.0f, "column 0 width updated to 200");

	// --- Set same value again (no change) ---
	changed = SetLineSize(*ss, true, 0, 0, 200.0f);
	Check(!changed, "SetLineSize returns false when value unchanged");

	// --- Set to 0 (revert to default) ---
	changed = SetLineSize(*ss, true, 0, 0, 0.0f);
	Check(changed, "SetLineSize returns true when deleting header");
	Check(ss->ColumnWidth(0) == defW, "column 0 reverts to default after size 0");

	changed = SetLineSize(*ss, false, 0, 0, 0.0f);
	Check(changed, "SetLineSize returns true when deleting row header");
	Check(ss->RowHeight(0) == defH, "row 0 reverts to default after size 0");

	// --- Revert range ---
	changed = SetLineSize(*ss, true, 1, 3, 0.0f);
	Check(changed, "SetLineSize reverts range");
	Check(ss->ColumnWidth(1) == defW, "column 1 reverted to default");
	Check(ss->ColumnWidth(2) == defW, "column 2 reverted to default");
	Check(ss->ColumnWidth(3) == defW, "column 3 reverted to default");

	// --- Set size 0 on a column with no header (no-op) ---
	changed = SetLineSize(*ss, true, 5, 5, 0.0f);
	Check(!changed, "SetLineSize returns false when nothing to delete");

	// --- Multiple columns / rows at once ---
	changed = SetLineSize(*ss, true, 0, 4, 150.0f);
	Check(changed, "SetLineSize sets 5 columns at once");
	for (int i = 0; i <= 4; i++)
		Check(ss->ColumnWidth(i) == 150.0f, std::string("column ").append(std::to_string(i)).append(" width is 150").c_str());

	changed = SetLineSize(*ss, false, 0, 4, 40.0f);
	Check(changed, "SetLineSize sets 5 rows at once");
	for (int i = 0; i <= 4; i++)
		Check(ss->RowHeight(i) == 40.0f, std::string("row ").append(std::to_string(i)).append(" height is 40").c_str());

	// --- Swap first/last ---
	changed = SetLineSize(*ss, true, 3, 1, 80.0f);
	Check(changed, "SetLineSize handles swapped first/last");
	Check(ss->ColumnWidth(1) == 80.0f, "column 1 is 80 (swapped first/last)");
	Check(ss->ColumnWidth(2) == 80.0f, "column 2 is 80");
	Check(ss->ColumnWidth(3) == 80.0f, "column 3 is 80");

	printf("sizes ok %d, failed %d\n", ok, failed);
	return failed ? 1 : 0;
}
