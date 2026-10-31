// doc_sheets: switching the shown sheet (Document::ShowSheet) in the
// samples with several sheets in a browse root: the new sheet's main and
// graphics parts get the root's frames and are placed, the old sheet's
// aren't; a sheet's drawing's own frames (CheckBook's chart) come with it;
// it saves, reloads on the sheet it was switched to, and switches back.
// Arguments: the sample documents (those with several sheets are used).
#include <cstdio>
#include <set>
#include <string>

#include "Document.h"
#include "Editing.h"
#include "SSEval.h"

using namespace gobe;

static int sOk = 0, sFailed = 0;

static void
Check(bool ok, const std::string& what)
{
	if (ok)
		sOk++;
	else {
		sFailed++;
		printf("FAIL %s\n", what.c_str());
	}
}


static std::set<size_t>
Placed(const Document& doc)
{
	std::set<size_t> parts;
	for (const Document::PlacedFrame& f : doc.PlacedFrames())
		parts.insert(f.part);
	return parts;
}


int
main(int argc, char** argv)
{
	int documents = 0, switches = 0;
	for (int i = 1; i < argc; i++) {
		Document doc;
		std::string error;
		if (!LoadDocument(argv[i], doc, error) || doc.sheets.sheets.size() < 2)
			continue;
		int root = doc.RootPart();
		if (root < 0 || doc.parts.parts[size_t(root)].kind != Sig("BROT"))
			continue;
		documents++;
		std::string name = argv[i];
		int first = doc.CurrentSheet();
		Check(doc.MainPart() == int(doc.sheets.sheets[size_t(first)].mainPart),
			name + ": the main part is the shown sheet's");
		for (size_t s = 0; s < doc.sheets.sheets.size(); s++) {
			const LeafSheet& sheet = doc.sheets.sheets[s];
			std::string where = name + " sheet " + sheet.name;
			if (!doc.ShowSheet(s, error)) {
				printf("  %s: %s\n", where.c_str(), error.c_str());
				continue;
			}
			switches++;
			Check(doc.CurrentSheet() == int(s) && doc.MainPart() == int(sheet.mainPart),
				where + ": shown");
			std::set<size_t> placed = Placed(doc);
			Check(placed.count(sheet.mainPart) == 1, where + ": its main part placed");
			Check(sheet.graphicsPart == kNoPart || placed.count(sheet.graphicsPart) == 1,
				where + ": its drawing placed");
			for (size_t o = 0; o < doc.sheets.sheets.size(); o++)
				if (o != s)
					Check(placed.count(doc.sheets.sheets[o].mainPart) == 0,
						where + ": not sheet " + doc.sheets.sheets[o].name + "'s");
			// frames inside the drawing (a chart) come along
			for (size_t p = 0; p < doc.tails.size(); p++)
				for (const Portal& portal : doc.tails[p].portals)
					for (const Frame& f : portal.frames)
						if (f.container == sheet.graphicsPart && sheet.graphicsPart != kNoPart)
							Check(placed.count(p) == 1, where + ": part " + std::to_string(p)
								+ " in its drawing placed");
		}
		// saved on a sheet, reloaded there, switched back
		size_t last = doc.sheets.sheets.size() - 1;
		Check(doc.ShowSheet(last, error), name + ": to the last sheet " + error);
		std::string path = "/tmp/doc_sheets.gobe";
		Check(doc.Save(path, error), name + ": save " + error);
		Document back;
		Check(LoadDocument(path, back, error), name + ": reload " + error);
		Check(back.CurrentSheet() == int(last)
			&& back.MainPart() == int(back.sheets.sheets[last].mainPart), name + ": reloaded on it");
		Check(Placed(back).count(back.sheets.sheets[last].mainPart) == 1, name + ": placed after reload");
		Check(back.ShowSheet(size_t(first), error) && back.Save(path, error), name + ": back " + error);
		Document again;
		Check(LoadDocument(path, again, error) && again.CurrentSheet() == first
			&& Placed(again).count(again.sheets.sheets[size_t(first)].mainPart) == 1,
			name + ": back on the first sheet " + error);
		remove(path.c_str());
	}
	// New Sheet… (a spreadsheet) in a new spreadsheet and in CheckBook:
	// shown, cells typed, saved, reloaded with the sheet and its cells
	for (int which = 0; which < 2; which++) {
		Document doc;
		std::string error;
		std::string name = which == 0 ? "new spreadsheet" : "CheckBook";
		bool loaded = which == 0 ? CreateBlankSpreadsheet(doc, error) : false;
		for (int i = 1; which == 1 && i < argc && !loaded; i++)
			if (std::string(argv[i]).find("CheckBook") != std::string::npos)
				loaded = LoadDocument(argv[i], doc, error);
		if (!loaded)
			continue;
		size_t sheetsBefore = doc.sheets.sheets.size(), partsBefore = doc.parts.parts.size();
		int added = doc.AddSpreadsheetSheet("", error);
		Check(added == int(sheetsBefore), name + ": sheet added " + error);
		if (added < 0)
			continue;
		const LeafSheet& sheet = doc.sheets.sheets[size_t(added)];
		Check(sheet.name == "Sheet " + std::to_string(sheetsBefore + 1), name + ": named " + sheet.name);
		Check(doc.parts.parts.size() == partsBefore + 2 && sheet.mainPart == partsBefore
			&& sheet.graphicsPart == partsBefore + 1, name + ": two parts");
		Check(doc.AddSpreadsheetSheet("Budget", error) == added + 1, name + ": a second, named");
		// "SS Default" and the display styles counted once per spreadsheet
		size_t spreadsheets = doc.spreadsheetParts.size();
		for (const Style& st : doc.styles.styles)
			if (st.name == "SS Default" || st.name == "GridStyle" || st.name == "LockStyle")
				Check(st.value4 == spreadsheets + 1, name + ": " + st.name + " used by "
					+ std::to_string(st.value4 - 1) + " of " + std::to_string(spreadsheets));
		Check(doc.ShowSheet(size_t(added), error), name + ": shown " + error);
		Check(Placed(doc).count(sheet.mainPart) == 1, name + ": placed");
		SSPart* ss = doc.EditableSpreadsheetPart(sheet.mainPart);
		Check(ss != nullptr && SetCellInput(*ss, 0, 0, "41", nullptr, error)
			&& SetCellInput(*ss, 1, 0, "=A1+1", nullptr, error), name + ": typed " + error);
		UpdateDependencies(doc, sheet.mainPart);
		std::string path = "/tmp/doc_sheets_new.gobe";
		Check(doc.Save(path, error), name + ": save " + error);
		Document back;
		Check(LoadDocument(path, back, error), name + ": reload " + error);
		Check(back.sheets.sheets.size() == sheetsBefore + 2 && back.CurrentSheet() == added
			&& back.sheets.sheets[sheetsBefore + 1].name == "Budget", name + ": the sheets back");
		const SSPart* again = back.SpreadsheetPart(back.sheets.sheets[size_t(added)].mainPart);
		if (again != nullptr) {
			SSBook book(back);
			SSEvaluator e(*again, &book, int(back.sheets.sheets[size_t(added)].mainPart));
			Check(e.Value(1, 0).type == SSValue::kNumber && e.Value(1, 0).number == 42,
				name + ": =A1+1 is 42");
		} else
			Check(false, name + ": the new sheet's spreadsheet back");
		Check(back.ShowSheet(size_t(added) + 1, error) && back.ShowSheet(0, error), name
			+ ": the sheets shown after reloading " + error);
		// Delete Sheet: the shown one goes past those in use, the one before
		// it is shown; saved and reloaded
		size_t count = back.SheetCount();
		Check(back.ShowSheet(size_t(added), error) && back.DeleteSheet(size_t(added), error),
			name + ": deleted " + error);
		Check(back.SheetCount() == count - 1 && back.CurrentSheet() == added - 1
			&& back.sheets.sheets.back().name == "Sheet " + std::to_string(sheetsBefore + 1),
			name + ": the sheet before shown, the deleted one last");
		Check(!back.ShowSheet(count - 1, error), name + ": a deleted sheet isn't shown");
		Check(back.Save(path, error), name + ": save after deleting " + error);
		Document third;
		Check(LoadDocument(path, third, error) && third.SheetCount() == count - 1
			&& third.sheets.sheets[count - 2].name == "Budget", name + ": deleted after reloading");
		Check(third.AddSpreadsheetSheet("Later", error) == int(count - 1)
			&& third.sheets.sheets[count - 1].name == "Later", name + ": a new sheet before the deleted");
		// moved: the shown sheet stays shown
		{
			int shownMain = int(third.sheets.sheets[size_t(third.CurrentSheet())].mainPart);
			std::string firstName = third.sheets.sheets[0].name;
			Check(third.MoveSheet(0, third.SheetCount() - 1)
				&& third.sheets.sheets[third.SheetCount() - 1].name == firstName
				&& third.MainPart() == shownMain, name + ": the first sheet moved last");
			Check(!third.MoveSheet(0, third.sheets.sheets.size() + 3), name + ": not past the sheets");
		}
		// the last one left can't go
		Document one;
		if (CreateBlankSpreadsheet(one, error))
			Check(!one.DeleteSheet(0, error), "the last sheet stays");
		remove(path.c_str());
	}
	printf("doc_sheets: documents %d, switches %d, ok %d, failed %d\n", documents, switches, sOk,
		sFailed);
	return sFailed != 0 || documents == 0 || switches == 0;
}
