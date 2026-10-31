// ss_new: File > New for spreadsheets (CreateBlankSpreadsheet) against the
// original's documents (the arguments): the sections most documents share
// byte for byte; the factory table of the spreadsheet documents with the
// same parts (StockPrice); the parts a SPRD sheet has (the spreadsheet, the
// sheet's drawing, a browse root) and what TPartSS::TPartSS gives the
// spreadsheet; the styles consistent, with their use counts; it saves
// unchanged, takes cells and formulas, saves and reloads.
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "Document.h"
#include "Editing.h"
#include "SSEval.h"
#include "Styles.h"

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


static std::vector<uint8_t>
Section(const Document& doc, const std::string& name)
{
	for (const TocEntry& e : doc.file.toc) {
		if (e.name != name)
			continue;
		size_t end = doc.file.tocOffset;
		for (const TocEntry& o : doc.file.toc)
			if (o.offset > e.offset && o.offset < end)
				end = o.offset;
		return std::vector<uint8_t>(doc.bytes.begin() + e.offset, doc.bytes.begin() + end);
	}
	return {};
}


int
main(int argc, char** argv)
{
	Document doc;
	std::string error;
	Check(CreateBlankSpreadsheet(doc, error), "CreateBlankSpreadsheet " + error);
	Check(doc.name.compare(0, 8, "untitled") == 0 && doc.path.empty(), "untitled, no path");

	std::vector<Document> samples;
	for (int i = 1; i < argc; i++) {
		Document d;
		if (LoadDocument(argv[i], d, error))
			samples.push_back(std::move(d));
	}
	Check(samples.size() >= 14, "samples loaded: " + std::to_string(samples.size()));
	// the sections most documents have the same bytes
	int common = 0;
	for (const TocEntry& e : doc.file.toc) {
		if (e.name == "PartFactory Table")
			continue;	// the parts' kinds: below
		std::map<std::vector<uint8_t>, int> variants;
		for (const Document& s : samples)
			variants[Section(s, e.name)]++;
		auto mode = variants.begin();
		for (auto it = variants.begin(); it != variants.end(); ++it)
			if (it->second > mode->second)
				mode = it;
		if (mode == variants.end() || mode->second * 3 < int(samples.size()) * 2)
			continue;
		Check(Section(doc, e.name) == mode->first, e.name + " like in "
			+ std::to_string(mode->second) + " samples");
		common++;
	}
	Check(common >= 5, "sections most samples share: " + std::to_string(common));
	int tables = 0;
	for (const Document& s : samples) {
		bool same = s.factories.size() == 3;
		for (size_t i = 0; same && i < 3; i++)
			same = s.factories[i].kind == doc.parts.parts[i].kind;
		if (same) {
			Check(Section(doc, "PartFactory Table") == Section(s, "PartFactory Table"),
				"PartFactory Table like StockPrice's");
			tables++;
		}
	}
	Check(tables > 0, "a sample with the same part kinds");

	// the parts
	bool kinds = doc.parts.parts.size() == 3 && doc.parts.parts[0].kind == Sig("SPRD")
		&& doc.parts.parts[1].kind == Sig("GRFX") && doc.parts.parts[2].kind == Sig("BROT");
	Check(kinds, "parts SPRD GRFX BROT");
	bool tails = doc.tailRead.size() == 3;
	for (bool t : doc.tailRead)
		tails = tails && t;
	Check(tails, "all part tails read");
	Check(doc.spreadsheetParts.size() == 1 && doc.graphicsParts.size() == 1, "SPRD and GRFX parsed");
	Check(doc.MainPart() == 0 && doc.sheets.sheets.size() == 1
		&& doc.sheets.sheets[0].graphicsPart == 1 && !doc.sheets.sheets[0].pageView,
		"one sheet: main 0, drawing 1, not paged");
	if (kinds && tails) {
		const Frame& sheet = doc.tails[0].portals[0].frames[0];
		const Frame& drawing = doc.tails[1].portals[0].frames[0];
		const Frame& root = doc.tails[2].portals[0].frames[0];
		Check(sheet.part == 0 && sheet.container == 2 && drawing.part == 1
			&& drawing.container == 2 && root.part == 2, "frames in the browse root");
		Check(doc.tails[0].portals[0].values[1] == 12 && doc.tails[1].portals[0].values[1] == 4,
			"portals: main part 12, drawing 4");
	}
	const Selection* cells = doc.selections.Find(1);
	Check(cells != nullptr && cells->kind == Sig("SPRD") && cells->part == 0
		&& cells->ranges.size() == 1 && cells->cell[0] == 0 && cells->cell[1] == 0,
		"selection 1: A1");

	const StyleMgr& styles = doc.styles;
	std::map<std::string, uint32_t> named;
	bool consistent = true;
	for (size_t i = 0; i < styles.styles.size(); i++) {
		const Style& s = styles.styles[i];
		consistent = consistent && s.id == Sig("STYL") && PropsSorted(styles, s.props)
			&& PropsSorted(styles, s.props2) && StyleUnionSet(styles, s) == s.props2;
		if (!s.name.empty())
			named[s.name] = uint32_t(i);
	}
	Check(consistent, "styles sorted, union sets right");
	for (const char* n : {"Default", "GR Default", "SS Default", "GridStyle", "HeaderStyle",
			"ColumnStyle", "RowStyle", "LockStyle"})
		Check(named.count(n) == 1, std::string("style \"") + n + "\"");

	if (!doc.spreadsheetParts.empty()) {
		const SSPart& ss = doc.spreadsheetParts[0].second;
		Check(ss.cells.empty() && ss.used.from.a == 0 && ss.used.to.a == 499
			&& ss.used.to.b == 19, "no cells, rows 1-500, columns A-T");
		Check(ss.style == named["SS Default"] && ss.autoCalculate && ss.locked.empty(),
			"part style SS Default, Auto Calculate");
		Check(ss.defaultHeight == 16 && ss.defaultWidth == 72, "16 by 72");
		Check(ss.displayStyles[0] == named["GridStyle"] && ss.displayStyles[1] == named["HeaderStyle"]
			&& ss.displayStyles[2] == named["ColumnStyle"] && ss.displayStyles[3] == named["RowStyle"]
			&& ss.displayStyles[4] == named["LockStyle"], "display styles");
		Check(ss.options[0] && ss.options[1] && !ss.options[2] && ss.options[3] && ss.options[4]
			&& ss.options[5], "display options");
		TextStyle t = ResolveTextStyle(styles, ss.style);
		Check(t.size == 10 && t.family == "Dutch801 Rm BT", "SS Default: 10 points");
	}

	// use counts: the manager, child styles, the part's style, its display
	// styles, frames, the document's default style
	std::vector<uint32_t> users(styles.styles.size(), 1);
	for (const Style& s : styles.styles)
		for (uint32_t link : s.links)
			if (link < users.size())
				users[link]++;
	for (const auto& [index, ss] : doc.spreadsheetParts) {
		if (ss.style < users.size())
			users[ss.style]++;
		for (uint32_t d : ss.displayStyles)
			if (d < users.size())
				users[d]++;
	}
	for (const PartTail& tail : doc.tails)
		for (const Portal& portal : tail.portals)
			for (const Frame& f : portal.frames)
				if (f.style < users.size())
					users[f.style]++;
	if (styles.afterSection < users.size())
		users[styles.afterSection]++;
	bool counts = true;
	for (size_t i = 0; i < users.size(); i++)
		if (styles.styles[i].value4 != users[i]) {
			counts = false;
			printf("  style %zu: count %u, users %u\n", i, styles.styles[i].value4, users[i]);
		}
	Check(counts, "style use counts");

	// saves unchanged; cells and a formula; saved and reloaded
	std::string path = "/tmp/ss_new_blank.gobe";
	std::vector<uint8_t> bytes = doc.bytes;
	Check(doc.Save(path, error), "save " + error);
	Check(doc.bytes == bytes, "saved unchanged: the same bytes");
	SSPart* ss = doc.EditableSpreadsheetPart(0);
	Check(ss != nullptr, "editable");
	if (ss != nullptr) {
		Check(SetCellInput(*ss, 0, 0, "2", nullptr, error)
			&& SetCellInput(*ss, 1, 0, "3.5", nullptr, error)
			&& SetCellInput(*ss, 2, 0, "Total", nullptr, error)
			&& SetCellInput(*ss, 3, 0, "=SUM(A1..A2)", nullptr, error), "cells typed " + error);
		UpdateDependencies(doc, 0);
		Check(doc.Save(path, error), "save edited " + error);
		Document back;
		Check(LoadDocument(path, back, error), "reload " + error);
		const SSPart* again = back.SpreadsheetPart(0);
		Check(again != nullptr && again->cells.size() == 4, "four cells back");
		if (again != nullptr) {
			SSBook book(back);
			SSEvaluator e(*again, &book, 0);
			SSValue v = e.Value(3, 0);
			Check(v.type == SSValue::kNumber && v.number == 5.5, "=SUM(A1..A2) is 5.5");
			Check(e.Value(2, 0).text == "Total", "text kept");
		}
	}
	remove(path.c_str());

	printf("ss_new ok %d, failed %d\n", sOk, sFailed);
	return sFailed > 0 || sOk == 0;
}
