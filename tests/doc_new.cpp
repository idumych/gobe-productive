// doc_new: File > New (CreateBlankDocument) against the original's saved
// documents (the arguments): the header and the sections every document
// has byte for byte are the same; the parts are a WORD sheet's (main text,
// sheet drawing, paged root, page) with the text frame inside the margins;
// the styles are consistent (TPropsTable order, union sets, use counts);
// it saves, reloads and edits.
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "Document.h"
#include "Editing.h"
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
	Check(CreateBlankDocument(doc, error), "CreateBlankDocument " + error);
	Check(doc.name == "untitled" && doc.path.empty(), "named untitled, no path: " + doc.name);

	// the samples: header, table of contents, sections the same everywhere
	std::vector<Document> samples;
	for (int i = 1; i < argc; i++) {
		Document d;
		if (LoadDocument(argv[i], d, error))
			samples.push_back(std::move(d));
	}
	Check(samples.size() >= 14, "samples loaded: " + std::to_string(samples.size()));
	int headers = 0;
	for (const Document& s : samples) {
		bool same = s.bytes.size() >= 98 && doc.bytes.size() >= 98;
		for (size_t i = 0; same && i < 98; i++) {
			if ((i >= 0x14 && i < 0x18) || i == 0x50 || i == 0x51)	// TOC offset, last version word
				continue;
			same = s.bytes[i] == doc.bytes[i];
		}
		headers += same;
	}
	// (36 documents end the test double with 0x53, 8 with 0x52)
	Check(headers >= int(samples.size()) * 3 / 4, "header like the samples': "
		+ std::to_string(headers));
	if (!samples.empty()) {
		std::vector<std::string> names, blankNames;
		for (const TocEntry& e : samples[0].file.toc)
			names.push_back(e.name);
		bool kinds = true;
		for (const TocEntry& e : doc.file.toc) {
			blankNames.push_back(e.name);
			kinds = kinds && e.kind == 1;
		}
		Check(names == blankNames && kinds, "the samples' sections, in order, kind 1");
	}
	// sections most documents have the same: the blank one has those bytes
	// (Document Misc, SetMgr, PenMgr, LinksMgr, CommandMgr, ...)
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
	// the factory table as in the samples with the same kinds of parts
	int tables = 0;
	for (const Document& s : samples) {
		bool same = s.factories.size() == 4;
		for (size_t i = 0; same && i < 4; i++)
			same = s.factories[i].kind == doc.parts.parts[i].kind;
		if (same) {
			Check(Section(doc, "PartFactory Table") == Section(s, "PartFactory Table"),
				"PartFactory Table like a WORD document's");
			tables++;
		}
	}
	Check(tables > 0, "a sample with the same part kinds");
	std::vector<uint8_t> versions = Section(doc, "VersionMgr");
	Check(versions.size() == 28 && memcmp(&versions[8], &doc.bytes[0x42], 14) == 0,
		"VersionMgr: one version, the header's");

	// parts: WORD, GRFX, PROT, PAGE; all tails read
	bool kinds = doc.parts.parts.size() == 4 && doc.parts.parts[0].kind == Sig("WORD")
		&& doc.parts.parts[1].kind == Sig("GRFX") && doc.parts.parts[2].kind == Sig("PROT")
		&& doc.parts.parts[3].kind == Sig("PAGE");
	Check(kinds, "parts WORD GRFX PROT PAGE");
	bool tails = doc.tailRead.size() == 4;
	for (bool t : doc.tailRead)
		tails = tails && t;
	Check(tails, "all part tails read");
	Check(doc.graphicsParts.size() == 1 && doc.wordParts.size() == 1, "GRFX and WORD parsed");
	if (kinds && tails) {
		const Frame& text = doc.tails[0].portals[0].frames[0];
		const Frame& drawing = doc.tails[1].portals[0].frames[0];
		const Frame& page = doc.tails[3].portals[0].frames[0];
		Check(text.part == 0 && text.container == 3 && text.origin[0] == 72
			&& text.origin[1] == 72 && text.extent[0] == 468 && text.extent[1] == 648,
			"text frame in the page, inside 1 inch margins");
		Check(drawing.part == 1 && drawing.container == 3 && drawing.origin[0] == 72
			&& drawing.extent[0] == 468 && drawing.extent[1] == 648, "sheet drawing frame");
		Check(page.part == 3 && page.container == 2 && page.extent[0] == 612
			&& page.extent[1] == 792, "page frame: US Letter in the root");
		Check(doc.tails[0].portals[0].part == 3 && doc.tails[0].portals[0].values[1] == 12
			&& doc.tails[1].portals[0].values[1] == 4, "portals: main part 12, drawing 4");
		int placed = 0;
		for (const Document::PlacedFrame& f : doc.PlacedFrames())
			if (f.part == 0 && f.x == 72 && f.y == 72 && f.width == 468)
				placed++;
		Check(placed == 1, "text frame placed at 72,72");
	}
	Check(doc.MainPart() == 0 && !doc.sheets.sheets.empty()
		&& doc.sheets.sheets[0].graphicsPart == 1 && doc.sheets.sheets[0].printable[0] == 72
		&& doc.sheets.sheets[0].printable[3] == 720, "SheetMgr: main 0, drawing 1, margins");
	const Selection* textSelection = doc.selections.Find(1);
	Check(doc.selections.selections.size() == 2 && textSelection != nullptr
		&& textSelection->kind == Sig("WORD") && textSelection->part == 0
		&& doc.wordParts[0].second.header.selection == 1, "selections: drawing 0, text 1");

	// styles
	const StyleMgr& styles = doc.styles;
	std::map<std::string, uint32_t> named;
	bool ids = true, consistent = true;
	for (size_t i = 0; i < styles.styles.size(); i++) {
		const Style& s = styles.styles[i];
		ids = ids && s.id == Sig("STYL") && s.valueC == 0xffff;
		consistent = consistent && PropsSorted(styles, s.props) && PropsSorted(styles, s.props2)
			&& StyleUnionSet(styles, s) == s.props2;
		if (!s.name.empty())
			named[s.name] = uint32_t(i);
	}
	Check(ids, "style ids 'STYL'");
	Check(consistent, "styles sorted, union sets right");
	const char* expected[] = {"Default", "Text Background", "GR Default", "WP Default", "Body"};
	for (const char* n : expected)
		Check(named.count(n) == 1, std::string("style \"") + n + "\"");
	if (doc.wordParts.size() == 1 && named.count("Body")) {
		const WPParagraph& p = doc.wordParts[0].second.paragraphs[0];
		Check(p.text.empty() && p.runs.size() == 1 && p.runs[0].position == 0,
			"one empty paragraph with a run");
		Check(styles.styles[p.parStyle].links[0] == named["Body"]
			&& styles.styles[p.parStyle].value10 == Sig("Para"), "paragraph style based on Body");
		TextStyle t = ResolveTextStyle(styles, p.parStyle);
		Check(t.family == "Dutch801 Rm BT" && t.size == 12 && t.lineSpacing == 1
			&& t.alignment == 0, "Body: Dutch801 Rm BT 12, single spaced, left");
		const Style& run = styles.styles[p.runs[0].style];
		Check(run.value10 == Sig("Basi") && run.links[0] == 0xffffffff && run.props.empty(),
			"run style: new, no parent");
		Check(doc.wordParts[0].second.sections.size() == 1
			&& doc.wordParts[0].second.sections[0].columns.size() == 1, "one section, one column");
	}
	// use counts: the manager, child styles, paragraphs, runs, frames, the
	// document's default style
	std::vector<uint32_t> users(styles.styles.size(), 1);
	for (const Style& s : styles.styles)
		for (uint32_t link : s.links)
			if (link < users.size())
				users[link]++;
	for (const auto& [index, wp] : doc.wordParts)
		for (const WPParagraph& p : wp.paragraphs) {
			if (p.parStyle < users.size())
				users[p.parStyle]++;
			for (const WPStyleRun& r : p.runs)
				if (r.style < users.size())
					users[r.style]++;
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

	// saves byte for byte, reloads, edits
	std::string path = "/tmp/doc_new_blank.gobe";
	std::vector<uint8_t> bytes = doc.bytes;
	Check(doc.Save(path, error), "save " + error);
	Check(doc.bytes == bytes, "saved unchanged: the same bytes");
	WPPart* wp = doc.EditableWordPart(0);
	InsertText(*wp, {0, 0}, "Hello");
	ApplyCharacterProperty(doc, 0, {0, 0}, {0, 5}, TextFaceProperty(2));
	// Copy Format: the run's own face and the paragraph style's character
	// properties, no paragraph ones
	auto has = [&](const std::vector<Property>& format, uint32_t type) {
		for (const Property& p : format)
			if (p.type == type)
				return true;
		return false;
	};
	std::vector<Property> runFormat = CharacterFormat(doc.styles,
		doc.wordParts[0].second.paragraphs[0].runs[0].style);
	Check(has(runFormat, Sig("TFac")) && !has(runFormat, Sig("PAlg")), "Copy Format of the run");
	std::vector<Property> parFormat = CharacterFormat(doc.styles,
		doc.wordParts[0].second.paragraphs[0].parStyle);
	Check(has(parFormat, Sig("TFon")) && has(parFormat, Sig("TSiz")) && !has(parFormat, Sig("PAlg"))
		&& !has(parFormat, Sig("LSpa")) && !has(parFormat, Sig("SpaB")) && !has(parFormat, Sig("PLef")),
		"Copy Format of the paragraph style: its character properties");
	Check(doc.Save(path, error), "save edited " + error);
	Document back;
	Check(LoadDocument(path, back, error), "reload " + error);
	Check(back.wordParts.size() == 1 && back.wordParts[0].second.paragraphs[0].text == "Hello"
		&& ResolveTextStyle(back.styles,
			back.wordParts[0].second.paragraphs[0].runs[0].style).faceFlags == 2, "edit kept");
	remove(path.c_str());

	printf("doc_new ok %d, failed %d\n", sOk, sFailed);
	return sFailed > 0 ? 1 : 0;
}
