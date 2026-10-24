// doc_pages: AddPage / RemoveLastPage (TPagePortal::NewPage, DeletePage) on
// a new document and on the samples: the new page's frames, where they are
// placed, saving and reloading them, and removing the page again giving the
// document's bytes back. Arguments: the sample documents.
#include <algorithm>
#include <cmath>
#include <cstdio>
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


static const Document::PlacedFrame*
Placed(const std::vector<Document::PlacedFrame>& placed, size_t part, int index)
{
	for (const Document::PlacedFrame& p : placed)
		if (p.part == part && p.index == index)
			return &p;
	return nullptr;
}


static size_t
Frames(const Document& doc, size_t part)
{
	return doc.tails[part].portals.empty() ? 0 : doc.tails[part].portals[0].frames.size();
}


int
main(int argc, char** argv)
{
	std::string error;
	// a new document: one page; a second one below it, 20 points apart
	Document doc;
	CreateBlankDocument(doc, error);
	Check(doc.PagePart() == 3 && doc.PageCount() == 1, "blank: one page");
	std::string path = "/tmp/doc_pages.gobe";
	doc.Save(path, error);
	std::vector<uint8_t> onePage = doc.bytes;
	Check(doc.AddPage() && doc.PageCount() == 2, "AddPage");
	const Frame& page = doc.tails[3].portals[0].frames[1];
	Check(page.origin[0] == 0 && page.origin[1] == 812 && page.extent[1] == 792,
		"page 2 at 0,812 (792 + 20)");
	Check(page.children.size() == 4 && page.children[0].part == kNoPart
		&& page.children[2] == Frame::Pointer{1, 0, 1} && page.children[3] == Frame::Pointer{0, 0, 1},
		"page 2 lists its drawing and text frames");
	Check(Frames(doc, 0) == 2 && Frames(doc, 1) == 2 && doc.tails[0].portals[0].values[2] == 2,
		"a text and a drawing frame more");
	const Frame& drawing = doc.tails[1].portals[0].frames[1];
	Check(drawing.sheetRect[1] == 648 && drawing.sheetRect[3] == 1296,
		"the drawing's second page");
	const Frame& column = doc.tails[0].portals[0].frames[1];
	Check(column.textStart == 0 && column.textLength == 0, "the new column is empty");
	std::vector<Document::PlacedFrame> placed = doc.PlacedFrames();
	const Document::PlacedFrame* text2 = Placed(placed, 0, 1);
	const Document::PlacedFrame* text1 = Placed(placed, 0, 0);
	Check(text1 != nullptr && text2 != nullptr && text2->x == text1->x
		&& text2->y == text1->y + 812, "page 2's text frame placed on page 2");
	Check(doc.Save(path, error), "save two pages " + error);
	Check(doc.PageCount() == 2 && Frames(doc, 0) == 2 && Frames(doc, 1) == 2,
		"reloaded with two pages");
	placed = doc.PlacedFrames();
	text2 = Placed(placed, 0, 1);
	Check(text2 != nullptr && text2->y == Placed(placed, 0, 0)->y + 812, "reloaded placement");
	Check(doc.RemoveLastPage() && doc.PageCount() == 1, "RemoveLastPage");
	Check(!doc.RemoveLastPage(), "the last page stays");
	Check(doc.Save(path, error) && doc.bytes == onePage, "back to the one page document's bytes");

	// the samples: a page added and removed again gives the bytes back;
	// every frame of the page is new
	int paged = 0;
	for (int i = 1; i < argc; i++) {
		Document d;
		if (!LoadDocument(argv[i], d, error) || d.PagePart() < 0)
			continue;
		paged++;
		std::vector<uint8_t> original = d.bytes;
		int pagePart = d.PagePart();
		size_t children = d.tails[pagePart].portals[0].frames[0].children.size();
		std::vector<std::vector<size_t>> counts(d.tails.size());
		for (size_t p = 0; p < d.tails.size(); p++)
			for (const Portal& portal : d.tails[p].portals)
				counts[p].push_back(portal.frames.size());
		Check(d.AddPage() && d.PageCount() == 2, std::string(argv[i]) + ": AddPage");
		const Frame& added = d.tails[pagePart].portals[0].frames[1];
		bool fresh = added.children.size() == children;
		for (const Frame::Pointer& c : added.children)
			if (c.part != kNoPart)
				fresh = fresh && c.frame >= counts[c.part][c.portal];
		Check(fresh, std::string(argv[i]) + ": the page's frames are new");
		Check(d.Save(path, error) && d.PageCount() == 2, std::string(argv[i]) + ": saved " + error);
		Check(d.RemoveLastPage() && d.Save(path, error) && d.bytes == original,
			std::string(argv[i]) + ": removed, the document's bytes");
	}
	Check(paged >= 5, "paged samples: " + std::to_string(paged));

	// page geometry: wider margins move and resize the frames; back to the
	// document's margins, every frame on its pages is where the original
	// laid it out (TPageFrame::RepositionChildFrames)
	int repositioned = 0;
	for (int i = 1; i < argc; i++) {
		Document d;
		if (!LoadDocument(argv[i], d, error) || d.PagePart() < 0 || d.sheets.sheets.empty())
			continue;
		Document original = d;
		float paper[4], printable[4], wider[4];
		std::copy(d.sheets.sheets[0].paper, d.sheets.sheets[0].paper + 4, paper);
		std::copy(d.sheets.sheets[0].printable, d.sheets.sheets[0].printable + 4, printable);
		float margins[4];
		d.Margins(margins);
		Check(margins[0] == printable[0] - paper[0] && margins[3] == paper[3] - printable[3],
			std::string(argv[i]) + ": margins");
		std::copy(printable, printable + 4, wider);
		wider[0] += 36;
		wider[2] -= 36;
		Check(d.SetPageGeometry(paper, wider) && d.sheetsChanged, std::string(argv[i])
			+ ": wider margins");
		int pagePart = d.PagePart();
		bool moved = false;
		for (const Frame::Pointer& c : d.tails[pagePart].portals[0].frames[0].children)
			if (c.part != kNoPart)
				moved = moved || d.tails[c.part].portals[c.portal].frames[c.frame].extent[0]
					!= original.tails[c.part].portals[c.portal].frames[c.frame].extent[0];
		uint32_t mainKind = d.parts.parts[d.MainPart()].kind;
		if (mainKind == Sig("WORD") || mainKind == Sig("GRFX"))
			Check(moved, std::string(argv[i]) + ": frames resized");
		d.SetPageGeometry(paper, printable);
		bool same = true;
		for (const Frame::Pointer& c : d.tails[pagePart].portals[0].frames[0].children) {
			if (c.part == kNoPart)
				continue;
			const Frame& a = d.tails[c.part].portals[c.portal].frames[c.frame];
			const Frame& b = original.tails[c.part].portals[c.portal].frames[c.frame];
			for (int k = 0; k < 2; k++)
				same = same && std::fabs(a.origin[k] - b.origin[k]) < 0.5f
					&& std::fabs(a.extent[k] - b.extent[k]) < 0.5f;
			if (!same) {
				printf("  %s part %u frame %u: %g,%g %gx%g, the original %g,%g %gx%g\n", argv[i],
					c.part, c.frame, a.origin[0], a.origin[1], a.extent[0], a.extent[1],
					b.origin[0], b.origin[1], b.extent[0], b.extent[1]);
				break;
			}
		}
		Check(same, std::string(argv[i]) + ": frames back where the original put them");
		repositioned++;
	}
	Check(repositioned >= 5, "repositioned samples: " + std::to_string(repositioned));
	remove(path.c_str());
	printf("pages ok %d, failed %d\n", sOk, sFailed);
	return sFailed > 0 ? 1 : 0;
}
