// doc_headers: Document > Insert/Remove Header and Footer
// (TDocHeaderFooterCommand) and the header growing with its text: the new
// part, its styles and selection, its frame on every page, the text
// columns between header and footer (TPageFrame::RepositionChildFrames),
// saving and reloading. Expected positions are the original's own: the
// samples' frames as it saved them (Resume's header 81 high puts its
// columns at 154, 566 high; TheWidget's footer 15 high at 741 on a 756
// bottom). Arguments: the sample documents.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include "Document.h"
#include "Editing.h"

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


static const Frame*
Child(const Document& d, size_t page, size_t child)
{
	int pagePart = d.PagePart();
	if (pagePart < 0 || page >= d.tails[pagePart].portals[0].frames.size())
		return nullptr;
	const Frame& p = d.tails[pagePart].portals[0].frames[page];
	if (child >= p.children.size() || p.children[child].part == kNoPart)
		return nullptr;
	const Frame::Pointer& c = p.children[child];
	return &d.tails[c.part].portals[c.portal].frames[c.frame];
}


static bool
Near(float a, float b)
{
	return std::fabs(a - b) < 0.01f;
}


static std::string
Describe(const Frame* f)
{
	if (f == nullptr)
		return "none";
	char s[96];
	snprintf(s, sizeof s, "%g,%g %gx%g", f->origin[0], f->origin[1], f->extent[0], f->extent[1]);
	return s;
}


static const char*
Leaf(const char* path)
{
	const char* slash = strrchr(path, '/');
	return slash != nullptr ? slash + 1 : path;
}


int
main(int argc, char** argv)
{
	std::string error, path = "/tmp/doc_headers.gobe";
	// a new document: a header, then a footer
	Document doc;
	CreateBlankDocument(doc, error);
	size_t partsBefore = doc.parts.parts.size(), selectionsBefore = doc.selections.selections.size();
	uint32_t nextId = doc.parts.value14;
	Check(doc.HeaderPart(0) < 0 && !doc.RemoveHeaderFooter(0), "blank: no header");
	Check(doc.InsertHeaderFooter(0), "Insert Header");
	Check(!doc.InsertHeaderFooter(0), "a second header isn't inserted");
	int header = doc.HeaderPart(0);
	Check(header == int(partsBefore) && doc.parts.parts[header].kind == Sig("WORD")
		&& doc.parts.value14 == nextId + 1, "a new text part with the next id");
	const WPPart* wp = doc.WordPart(size_t(header));
	Check(wp != nullptr && wp->paragraphs.size() == 1 && wp->paragraphs[0].text.empty()
		&& wp->version98 == 3, "one empty paragraph, +0x98 = 3");
	if (wp != nullptr) {
		const Style& par = doc.styles.styles[wp->paragraphs[0].parStyle];
		const Style& named = doc.styles.styles[par.links[0]];
		Check(named.name == "Header" && named.value10 == Sig("Para")
			&& doc.styles.styles[named.links[0]].name == "WP Default" && named.props.empty(),
			"paragraph style based on \"Header\", based on \"WP Default\"");
	}
	bool consistent = true;
	for (const Style& s : doc.styles.styles)
		consistent = consistent && PropsSorted(doc.styles, s.props2)
			&& StyleUnionSet(doc.styles, s) == s.props2;
	Check(consistent, "styles' union sets");
	Check(doc.selections.selections.size() == selectionsBefore + 1
		&& doc.selections.selections.back().part == uint32_t(header)
		&& wp != nullptr && wp->header.selection == selectionsBefore, "its own selection");
	const Frame* h = Child(doc, 0, 0);
	const Frame* column = Child(doc, 0, 3);
	Check(h != nullptr && h->part == uint32_t(header) && Near(h->origin[0], 72)
		&& Near(h->origin[1], 72) && Near(h->extent[0], 468) && (h->values[3] & 8),
		"header frame at the printable top, as wide, growing: " + Describe(h));
	// TPageFrame::ChildHeight: 1 + the header's height
	Check(column != nullptr && Near(column->origin[1], 73) && Near(column->extent[1], 647),
		"text column below it: " + Describe(column));
	Check(doc.SetHeaderHeight(0, 81) && !doc.SetHeaderHeight(0, 81), "the header grows to 81");
	column = Child(doc, 0, 3);
	Check(column != nullptr && Near(column->origin[1], 154) && Near(column->extent[1], 566),
		"column at 154, 566 high (Resume's): " + Describe(column));
	Check(doc.InsertHeaderFooter(1) && doc.SetHeaderHeight(1, 15), "Insert Footer, 15 high");
	const Frame* f = Child(doc, 0, 1);
	column = Child(doc, 0, 3);
	Check(f != nullptr && Near(f->origin[1], 705) && Near(f->extent[1], 15),
		"footer at the printable bottom (720 - 15): " + Describe(f));
	Check(column != nullptr && Near(column->origin[1] + column->extent[1], 704),
		"column ends 1 above the footer: " + Describe(column));
	// a second page shows the same header and footer
	Check(doc.AddPage(), "AddPage");
	int pagePart = doc.PagePart();
	const Frame& page2 = doc.tails[pagePart].portals[0].frames[1];
	Check(page2.children[0] == doc.tails[pagePart].portals[0].frames[0].children[0]
		&& page2.children[1] == doc.tails[pagePart].portals[0].frames[0].children[1]
		&& doc.tails[header].portals[0].frames.size() == 1, "page 2: the same header and footer");
	int placedHeaders = 0;
	for (const Document::PlacedFrame& p : doc.PlacedFrames())
		if (int(p.part) == header)
			placedHeaders += (Near(p.y, 72) || Near(p.y, 72 + 812)) && Near(p.x, 72);
	Check(placedHeaders == 2, "the header placed on both pages: " + std::to_string(placedHeaders));
	// typed into, saved, reloaded
	WPPart* editable = doc.EditableWordPart(size_t(header));
	InsertText(*editable, {0, 0}, "Running head");
	Check(doc.Save(path, error), "save " + error);
	header = doc.HeaderPart(0);
	Check(header == int(partsBefore) && doc.HeaderPart(1) == int(partsBefore) + 1
		&& doc.WordPart(size_t(header)) != nullptr
		&& doc.WordPart(size_t(header))->paragraphs[0].text == "Running head",
		"reloaded: header and footer parts, the header's text");
	Check(doc.selections.selections.size() == selectionsBefore + 2, "reloaded: their selections");
	h = Child(doc, 1, 0);
	Check(h != nullptr && Near(h->extent[1], 81) && doc.PageCount() == 2,
		"reloaded: page 2's header 81 high: " + Describe(h));
	// Remove Header: off the pages, the columns back up
	Check(doc.RemoveHeaderFooter(0) && doc.HeaderPart(0) < 0, "Remove Header");
	column = Child(doc, 0, 3);
	Check(Child(doc, 0, 0) == nullptr && Child(doc, 1, 0) == nullptr && column != nullptr
		&& Near(column->origin[1], 72), "no header on the pages, column at 72: "
		+ Describe(column));
	Check(doc.Save(path, error) && doc.HeaderPart(0) < 0 && doc.HeaderPart(1) >= 0,
		"saved without the header " + error);

	// the samples with headers and footers: removed and inserted again
	// at the height they had, the frames are where the original put them
	int samples = 0;
	for (int i = 1; i < argc; i++) {
		Document d;
		if (!LoadDocument(argv[i], d, error) || d.PagePart() < 0)
			continue;
		for (int which = 0; which < 2; which++) {
			if (d.HeaderPart(which) < 0)
				continue;
			Document original = d;
			const Frame* before = Child(original, 0, which);
			float height = before->extent[1];
			Check(d.RemoveHeaderFooter(which) && d.InsertHeaderFooter(which)
				&& d.SetHeaderHeight(which, height), std::string(Leaf(argv[i])) + ": again");
			const Frame* now = Child(d, 0, which);
			Check(now != nullptr && Near(now->origin[0], before->origin[0])
				&& Near(now->origin[1], before->origin[1]) && Near(now->extent[0], before->extent[0]),
				std::string(Leaf(argv[i])) + (which ? ": footer " : ": header ") + Describe(now)
				+ ", the original " + Describe(before));
			bool same = true;
			for (size_t k = 2; k < original.tails[original.PagePart()].portals[0].frames[0].children.size(); k++) {
				const Frame* a = Child(d, 0, k);
				const Frame* b = Child(original, 0, k);
				same = same && (a == nullptr) == (b == nullptr) && (a == nullptr
					|| (Near(a->origin[1], b->origin[1]) && Near(a->extent[1], b->extent[1])));
			}
			Check(same, std::string(Leaf(argv[i])) + ": the other frames where they were");
			Check(d.Save(path, error), std::string(Leaf(argv[i])) + ": saved " + error);
			samples++;
		}
	}
	Check(samples >= 4, "sample headers and footers: " + std::to_string(samples));
	remove(path.c_str());
	printf("headers ok %d, failed %d\n", sOk, sFailed);
	return sFailed > 0 ? 1 : 0;
}
