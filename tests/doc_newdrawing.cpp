// doc_newdrawing: File > New for drawings (CreateBlankDrawing) and
// presentations (CreateBlankPresentation). The
// document has the original's drawing documents' layout (GobeNewsletter,
// TheWidget): the drawing is the sheet's main and graphics part, framed
// inside the margins of a US Letter page of a paged root, in the page's
// last frame slot; one page; it saves and reloads with the same bytes.
// The presentation: a Background layer and one slide "untitled slide 1"
// with its own layer, 1024 x 768, shown in a browse root; it too saves
// and reloads unchanged.
// Arguments: none (documents given are ignored).
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


int
main()
{
	Document doc;
	std::string error;
	Check(CreateBlankDrawing(doc, error), "a new drawing " + error);
	Check(doc.parts.parts.size() == 3 && doc.parts.parts[0].kind == Sig("GRFX")
		&& doc.parts.parts[1].kind == Sig("PROT") && doc.parts.parts[2].kind == Sig("PAGE"),
		"parts: the drawing, the paged root, the page");
	Check(doc.MainPart() == 0 && doc.sheets.sheets.size() == 1
		&& doc.sheets.sheets[0].graphicsPart == 0 && doc.sheets.sheets[0].pageView,
		"the sheet: the drawing as main and graphics part, page view");
	Check(doc.GraphicsPart(0) != nullptr, "the drawing read");
	if (const GRPart* gr = doc.GraphicsPart(0))
		Check(gr->layers.size() == 1 && gr->layers[0].name == "Layer 1"
			&& gr->layers[0].shapes.empty(), "one empty layer, Layer 1");
	Check(doc.wordParts.empty(), "no text part");
	const Frame* drawing = doc.FrameOf(0, 0);
	const Frame* page = doc.FrameOf(2, 0);
	Check(drawing != nullptr && drawing->part == 0 && drawing->container == 2
		&& drawing->origin[0] == 72 && drawing->origin[1] == 72 && drawing->extent[0] == 468
		&& drawing->extent[1] == 648, "the drawing's frame inside the page's margins");
	Check(doc.tails[0].portals.size() == 1 && doc.tails[0].portals[0].values[1] == 12,
		"its portal: the sheet's main part (12)");
	Check(page != nullptr && page->extent[0] == 612 && page->extent[1] == 792
		&& page->children.size() == 4 && page->children[3].part == 0
		&& page->children[2].part == kNoPart, "the page: US Letter, the drawing in its last slot");
	Check(doc.PageCount() == 1, "one page: " + std::to_string(doc.PageCount()));
	int placed = 0;
	for (const Document::PlacedFrame& f : doc.PlacedFrames())
		if (f.part == 0 && f.x == 72 && f.y == 72)
			placed++;
	Check(placed == 1, "the drawing placed at 72, 72");

	std::string path = "/tmp/doc_newdrawing_test";
	std::vector<uint8_t> bytes = doc.bytes;
	Check(doc.Save(path, error), "save " + error);
	Document back;
	Check(LoadDocument(path, back, error), "reload " + error);
	Check(back.bytes == bytes, "saved and reloaded unchanged");
	Check(back.MainPart() == 0 && back.parts.parts.size() == 3, "the reloaded drawing");
	Document show;
	Check(CreateBlankPresentation(show, error, 1024, 768), "a new presentation " + error);
	Check(show.parts.parts.size() == 2 && show.parts.parts[0].kind == Sig("PRES")
		&& show.parts.parts[1].kind == Sig("BROT") && show.MainPart() == 0,
		"parts: the presentation, the browse root");
	Check(show.presentationPart == 0 && show.slides.size() == 1
		&& show.slides[0].name == "untitled slide 1" && show.currentSlide == 0,
		"one slide, untitled slide 1, shown");
	if (const GRPart* gr = show.GraphicsPart(0)) {
		Check(gr->size[0] == 1024 && gr->size[1] == 768, "the screen's size");
		Check(gr->layers.size() == 2 && gr->layers[0].name == "Background"
			&& gr->layers[1].name == "untitled layer 1", "Background and the slide's layer");
		Check(gr->views.size() == 1 && gr->views[0].layers.size() == 2
			&& gr->views[0].layers[0].flags[0] && gr->views[0].layers[1].flags[1],
			"the slide shows both, its own layer current");
	} else
		Check(false, "the presentation's drawing read");
	Check(show.warnings.empty(), "no warnings: " + (show.warnings.empty() ? ""
		: show.warnings[0]));
	int shown = 0;
	for (const Document::PlacedFrame& f : show.PlacedFrames())
		shown += f.part == 0;
	Check(shown == 1, "the presentation placed");
	std::vector<uint8_t> showBytes = show.bytes;
	path = "/tmp/doc_newpresentation_test";
	Check(show.Save(path, error), "save the presentation " + error);
	Document showBack;
	Check(LoadDocument(path, showBack, error) && showBack.bytes == showBytes
		&& showBack.slides.size() == 1, "the presentation reloaded unchanged");
	printf("doc_newdrawing: ok %d, failed %d\n", sOk, sFailed);
	return sFailed ? 1 : 0;
}
