// drawing_frames: part frames made in a drawing (the ToolBar's part tool,
// the Frame menu: AddDrawingFrame, AddLinkedFrame, CloneFrame,
// FrameCommandFlags). A new document's sheet drawing gets a text, a
// spreadsheet, a drawing, a table and a picture frame; each is its part's
// frame at the rectangle, a FRAM shape in the drawing, placed on the
// page; a text frame gets a linked frame (the same portal), a spreadsheet
// a clone (a portal of its own); the frame commands' flags; saved and
// opened again, all of it reads back.
// Arguments: none (documents given are ignored).
#include <cstdio>
#include <string>

#include "Document.h"
#include "Editing.h"
#include "GRPart.h"
#include "InlineParts.h"

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


static size_t
FrameShapes(const Document& doc, size_t drawing, size_t part)
{
	size_t n = 0;
	if (const GRPart* gr = doc.GraphicsPart(drawing))
		for (const GRLayer& l : gr->layers)
			for (const Shape& s : l.shapes)
				n += s.kind == Sig("FRAM") && s.frame[0] == part && FrameAt(doc, s.frame) != nullptr;
	return n;
}


int
main()
{
	Document doc;
	std::string error;
	Check(CreateBlankDocument(doc, error), "a new document " + error);
	size_t drawing = doc.sheets.sheets[0].graphicsPart;
	Check(doc.GraphicsPart(drawing) != nullptr, "the sheet's drawing");

	const uint32_t kinds[5] = {Sig("WORD"), Sig("SPRD"), Sig("GRFX"), Sig("TBLE"), Sig("BITS")};
	int parts[5];
	for (int k = 0; k < 5; k++) {
		float rect[4] = {20.0f + k * 10, 30.0f + k * 100, 220.0f + k * 10, 110.0f + k * 100};
		parts[k] = AddDrawingFrame(doc, drawing, 0, kinds[k], rect, error);
		std::string what = "frame " + std::to_string(k);
		Check(parts[k] >= 0, what + " made " + error);
		if (parts[k] < 0)
			continue;
		uint32_t pointer[3] = {uint32_t(parts[k]), 0, 0};
		const Frame* f = FrameAt(doc, pointer);
		Check(doc.parts.parts[size_t(parts[k])].kind == kinds[k], what + ": its kind");
		Check(f != nullptr && f->origin[0] == rect[0] && f->origin[1] == rect[1]
			&& f->extent[0] == 200 && f->extent[1] == 80 && f->container == drawing,
			what + ": its frame at the rectangle");
		Check(FrameShapes(doc, drawing, size_t(parts[k])) == 1, what + ": a FRAM shape");
	}
	// placed on the page (the drawing's frame + the rectangle)
	auto placed = doc.PlacedFrames();
	for (int k = 0; k < 5; k++) {
		bool found = false;
		for (const auto& p : placed)
			found |= p.part == size_t(parts[k]);
		Check(found, "frame " + std::to_string(k) + " placed");
	}
	// the shape ids: one each, different
	std::vector<uint32_t> ids;
	for (int k = 0; k < 5; k++) {
		uint32_t pointer[3] = {uint32_t(parts[k]), 0, 0};
		if (const Frame* f = FrameAt(doc, pointer))
			ids.push_back(f->shape[2]);
	}
	bool distinct = ids.size() == 5;
	for (size_t i = 0; i < ids.size(); i++)
		for (size_t j = i + 1; j < ids.size(); j++)
			distinct = distinct && ids[i] != ids[j];
	Check(distinct, "a shape id each");

	// a linked text frame: the same portal, right of it; a clone of the
	// spreadsheet: a portal of its own
	uint32_t text[3] = {uint32_t(parts[0]), 0, 0}, linked[3];
	Check(AddLinkedFrame(doc, text, linked, error), "linked " + error);
	const Frame* l = FrameAt(doc, linked);
	Check(l != nullptr && doc.tails[size_t(parts[0])].portals.size() == 1
		&& doc.tails[size_t(parts[0])].portals[0].frames.size() == 2 && l->origin[0] == 20 + 200 + 18,
		"the linked frame: the second in the portal, right of the first");
	Check(FrameShapes(doc, drawing, size_t(parts[0])) == 2, "a FRAM shape for it");
	uint32_t sheet[3] = {uint32_t(parts[1]), 0, 0}, clone[3];
	Check(CloneFrame(doc, sheet, clone, error), "cloned " + error);
	Check(doc.tails[size_t(parts[1])].portals.size() == 2 && clone[1] == 1
		&& FrameAt(doc, clone) != nullptr && FrameAt(doc, clone)->origin[0] == 30 + 18,
		"the clone: a portal of its own, down and right");
	// text typed into the linked frames' part
	InsertMultiline(*doc.EditableWordPart(size_t(parts[0])), {0, 0}, "Linked text");

	// the frame commands
	Check(FrameCommandFlags(0, Sig("sM34")) == 8 && FrameCommandFlags(8, Sig("sM34")) == 0
		&& FrameCommandFlags(0x20, Sig("sM35")) == 4 && FrameCommandFlags(0xc, Sig("sM36")) == 0x20
		&& FrameCommandFlags(0x20, Sig("sM36")) == 0, "Autogrow and Scrollbars");

	// saved and opened again
	std::string path = "/tmp/drawing_frames_test.gobe";
	Check(doc.Save(path, error), "saved " + error);
	Document back;
	Check(LoadDocument(path, back, error), "opened again " + error);
	for (int k = 0; k < 5; k++) {
		if (parts[k] < 0)
			continue;
		uint32_t pointer[3] = {uint32_t(parts[k]), 0, 0};
		const Frame* f = FrameAt(back, pointer);
		Check(size_t(parts[k]) < back.parts.parts.size()
			&& back.parts.parts[size_t(parts[k])].kind == kinds[k] && f != nullptr
			&& f->extent[0] == 200 && f->container == drawing,
			"frame " + std::to_string(k) + " read back");
	}
	Check(FrameShapes(back, drawing, size_t(parts[0])) == 2 && FrameAt(back, linked) != nullptr,
		"the linked frame read back");
	Check(FrameAt(back, clone) != nullptr, "the clone read back");
	Check(back.WordPart(size_t(parts[0])) != nullptr
		&& back.WordPart(size_t(parts[0]))->paragraphs[0].text.compare(0, 11, "Linked text") == 0,
		"the text read back");
	Check(back.Table(size_t(parts[3])) != nullptr && back.ImagePart(size_t(parts[4])) != nullptr
		&& back.SpreadsheetPart(size_t(parts[1])) != nullptr
		&& back.GraphicsPart(size_t(parts[2])) != nullptr, "each part read back");

	printf("drawing_frames: ok %d, failed %d\n", sOk, sFailed);
	return sFailed ? 1 : 0;
}
