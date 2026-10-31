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
	printf("doc_sheets: documents %d, switches %d, ok %d, failed %d\n", documents, switches, sOk,
		sFailed);
	return sFailed != 0 || documents == 0 || switches == 0;
}
