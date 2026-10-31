// Named selections (src/core/Names.cpp): TString::ValidName's rules, and a
// selection of cells named in a sample spreadsheet: the name refused a
// second time, saved and read back with its ranges, and usable in a
// formula.
#include <cstdio>
#include <cstdlib>

#include "Document.h"
#include "Editing.h"
#include "Names.h"
#include "SSEval.h"

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
	// ValidName
	Check(ValidName("Totals") && ValidName("Rate_2") && ValidName("Tax Rate")
		&& ValidName("ABC1") && ValidName("A.B") && ValidName("A_1"), "valid names");
	Check(!ValidName("") && !ValidName("1st") && !ValidName("A1") && !ValidName("AB12")
		&& !ValidName("Net-Pay") && !ValidName(std::string(65, 'a')), "names refused");

	int parts = 0;
	std::string error;
	for (int i = 1; i < argc && parts == 0; i++) {
		Document doc;
		if (!LoadDocument(argv[i], doc, error) || doc.spreadsheetParts.empty())
			continue;
		size_t p = doc.spreadsheetParts[0].first;
		if (!doc.tailRead[p] || doc.SpreadsheetPart(p)->used.to.a < 40)
			continue;
		parts++;
		std::string where = argv[i];
		// three numbers in a column far from the rest, named
		SSPart& ss = *doc.EditableSpreadsheetPart(p);
		uint16_t column = uint16_t(ss.used.to.b), row = uint16_t(ss.used.to.a - 5);
		for (int k = 0; k < 3; k++)
			SetCellInput(ss, uint16_t(row + k), column, std::to_string(k + 1), nullptr, error);
		SSRange range{{row, column}, {uint16_t(row + 2), column}};
		size_t selections = doc.selections.selections.size();
		Check(NameCellSelection(doc, p, "Trio", {range}, range.from), where + ": named");
		Check(!NameCellSelection(doc, p, "Trio", {range}, range.from),
			where + ": the same name refused");
		Check(!NameCellSelection(doc, p, "B7", {range}, range.from), where + ": B7 refused");
		Check(doc.selections.selections.size() == selections + 1, where + ": one selection more");
		// a formula using it
		auto isName = [&](const std::string& n) { return doc.NamedSelection(n, int(p)) != nullptr; };
		Check(SetCellInput(ss, uint16_t(row + 4), column, "=SUM(Trio)", isName, error),
			where + ": =SUM(Trio) " + error);
		UpdateDependencies(doc, p);
		{
			SSBook book(doc);
			SSEvaluator e(*doc.SpreadsheetPart(p), &book, int(p));
			SSValue v = e.Value(uint16_t(row + 4), column);
			Check(v.type == SSValue::kNumber && v.number == 6, where + ": SUM(Trio) is 6");
		}
		// saved and read back
		std::string path = std::string(getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp")
			+ "/names_save";
		Check(doc.Save(path, error), where + ": save " + error);
		Document again;
		Check(LoadDocument(path, again, error), where + ": load " + error);
		const Selection* sel = again.NamedSelection("Trio", int(p));
		Check(sel != nullptr && sel->ranges.size() == 1 && sel->ranges[0][0] == row
			&& sel->ranges[0][1] == column && sel->ranges[0][2] == row + 2,
			where + ": Trio read back");
		{
			SSBook book(again);
			SSEvaluator e(*again.SpreadsheetPart(p), &book, int(p));
			SSValue v = e.Value(uint16_t(row + 4), column);
			Check(v.type == SSValue::kNumber && v.number == 6, where + ": SUM(Trio) after save");
		}
		std::vector<std::string> names = SelectionNames(again);
		bool listed = false;
		for (const std::string& n : names)
			listed |= n == "Trio";
		Check(listed, where + ": Trio among the names");
		remove(path.c_str());
	}
	printf("names: parts %d, ok %d, failed %d\n", parts, ok, failures);
	return failures != 0 || parts == 0;
}
