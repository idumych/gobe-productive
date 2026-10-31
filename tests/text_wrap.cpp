// text_wrap: Text Wrap and frames moved and resized. The SWra property
// (a BMessage flattened as R5 did) reads back what it was made with, and
// one without its message is Regular, both sides; a shape's wrap sets its
// flag 0x40 and its style's property, None clears the flag; a frame's
// likewise. WrapLine: a line beside a shape narrowed to the side asked
// for, one beside a Top & Bottom shape or with too little room moved
// down. MoveFrame and ResizeFrame change a frame's place and size (a
// spreadsheet's rectangle shown too); all saved and opened again.
// Arguments: none (documents given are ignored).
#include <cstdio>
#include <string>

#include "DocWriter.h"
#include "Document.h"
#include "Drawing.h"
#include "Editing.h"
#include "GRPart.h"
#include "InlineParts.h"
#include "Styles.h"
#include "TextWrap.h"

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


static bool
Same(const ShapeWrap& a, const ShapeWrap& b)
{
	return a.kind == b.kind && a.to == b.to && a.margins[0] == b.margins[0]
		&& a.margins[1] == b.margins[1] && a.margins[2] == b.margins[2]
		&& a.margins[3] == b.margins[3];
}


int
main()
{
	// the property
	ShapeWrap wrap;
	wrap.kind = 3;
	wrap.to = 2;
	wrap.margins[0] = 4;
	wrap.margins[1] = 6;
	wrap.margins[2] = 8;
	wrap.margins[3] = 10;
	Property p = WrapProperty(wrap);
	ShapeWrap back;
	Check(p.type == Sig("SWra") && ReadWrapProperty(p, back) && Same(back, wrap),
		"the property reads back");
	// its message: 'FOB1', its size, 'Wrap', four fields
	uint32_t what = 0;
	std::vector<FlatField> fields;
	std::vector<uint8_t> message(p.data.begin() + 8, p.data.end() - 4);
	Check(UnflattenR5Message(message, what, fields) && what == 0x57726170 && fields.size() == 4
		&& fields[0].name == "Property Type" && fields[3].name == "margins"
		&& fields[3].data.size() == 16, "its message");
	Check(message[8] == uint8_t(message.size()) && message[0] == '1' && message[3] == 'F',
		"its header");
	// a message without the fields: Regular, both sides
	Property empty;
	empty.type = Sig("SWra");
	{
		std::vector<uint8_t> m = FlattenR5Message(0x57726170, {});
		DocWriter w;
		w.UInt32(uint32_t(m.size()));
		w.Sig(Sig("MESG"));
		w.Bytes(m);
		w.Sig(Sig("MesG"));
		empty.data = w.Data();
	}
	Check(ReadWrapProperty(empty, back) && back.kind == 2 && back.to == 0 && back.margins[2] == 0,
		"an empty one: Regular, both sides");

	// a shape and a frame in a new document's drawing
	Document doc;
	std::string error;
	Check(CreateBlankDocument(doc, error), "a new document " + error);
	size_t drawing = doc.sheets.sheets[0].graphicsPart;
	int shape = AddShape(doc, drawing, 0, kToolRect, {{100, 100}, {200, 160}});
	Check(shape >= 0, "a rectangle");
	Check(SetShapeWrap(doc, drawing, 0, size_t(shape), wrap), "its wrap set");
	const Shape& s = doc.GraphicsPart(drawing)->layers[0].shapes[size_t(shape)];
	Check((s.values[0] & kShapeWraps) != 0 && Same(ResolveShapeStyle(doc.styles, s.style).wrap, wrap),
		"the shape wraps, its style has the property");
	float rect[4] = {20, 300, 220, 380};
	int sheet = AddDrawingFrame(doc, drawing, 0, Sig("SPRD"), rect, error);
	int text = AddDrawingFrame(doc, drawing, 0, Sig("WORD"), rect, error);
	Check(sheet >= 0 && text >= 0, "a spreadsheet and a text frame " + error);
	uint32_t sheetFrame[3] = {uint32_t(sheet), 0, 0}, textFrame[3] = {uint32_t(text), 0, 0};
	ShapeWrap regular;
	regular.kind = 1;
	regular.margins[0] = regular.margins[1] = regular.margins[2] = regular.margins[3] = 10;
	Check(SetFrameWrap(doc, sheetFrame, regular), "the frame's wrap set");
	const Frame* f = FrameAt(doc, sheetFrame);
	Check(f != nullptr && (f->shape[0] & kShapeWraps) != 0
		&& Same(ResolveShapeStyle(doc.styles, f->style).wrap, regular), "the frame wraps");
	ShapeWrap none = regular;
	none.kind = 0;
	Check(SetFrameWrap(doc, textFrame, regular) && SetFrameWrap(doc, textFrame, none)
		&& (FrameAt(doc, textFrame)->shape[0] & kShapeWraps) == 0, "None: no wrap");

	// moved and resized
	Check(MoveFrame(doc, textFrame, 30, -20) && FrameAt(doc, textFrame)->origin[0] == 50
		&& FrameAt(doc, textFrame)->origin[1] == 280, "the text frame moved");
	float bigger[4] = {10, 290, 310, 450};
	Check(ResizeFrame(doc, sheetFrame, bigger) && f->origin[0] == 10 && f->extent[0] == 300
		&& f->extent[1] == 160 && f->sheetRect[2] - f->sheetRect[0] == 300,
		"the spreadsheet frame resized, its cells' rectangle too");
	float tiny[4] = {0, 0, 2, 2};
	Check(!ResizeFrame(doc, sheetFrame, tiny), "not smaller than 4 points");

	// saved and opened again
	std::string path = "/tmp/text_wrap_test.gobe";
	Check(doc.Save(path, error), "saved " + error);
	Document again;
	Check(LoadDocument(path, again, error), "opened again " + error);
	const GRPart* gr = again.GraphicsPart(drawing);
	Check(gr != nullptr && size_t(shape) < gr->layers[0].shapes.size()
		&& (gr->layers[0].shapes[size_t(shape)].values[0] & kShapeWraps) != 0
		&& Same(ResolveShapeStyle(again.styles, gr->layers[0].shapes[size_t(shape)].style).wrap,
			wrap), "the shape's wrap read back");
	const Frame* g = FrameAt(again, sheetFrame);
	Check(g != nullptr && (g->shape[0] & kShapeWraps) != 0 && g->extent[0] == 300
		&& g->origin[1] == 290 && g->sheetRect[2] - g->sheetRect[0] == 300
		&& Same(ResolveShapeStyle(again.styles, g->style).wrap, regular),
		"the frame's wrap, place and size read back");
	Check(FrameAt(again, textFrame) != nullptr && FrameAt(again, textFrame)->origin[0] == 50,
		"the moved frame read back");

	// WrapLine: a shape from 100 to 200 across, 50 to 100 down, in a
	// line from 0 to 400
	auto line = [](int kind, int to, float top, float& left, float& right, float& next) {
		std::vector<WrapObstacle> o = {{{100, 50, 200, 100}, kind, to}};
		left = 0;
		right = 400;
		return WrapLine(o, top, top + 12, left, right, 36, next);
	};
	float left, right, next;
	Check(line(1, 0, 10, left, right, next) && left == 0 && right == 400, "a line above it");
	Check(line(1, 0, 60, left, right, next) && left == 200 && right == 400,
		"both sides: the larger");
	Check(line(1, 1, 60, left, right, next) && left == 0 && right == 100, "to the left");
	Check(line(1, 2, 60, left, right, next) && left == 200 && right == 400, "to the right");
	Check(!line(3, 0, 60, left, right, next) && next == 100, "top & bottom: below it");
	Check(line(0, 0, 60, left, right, next) && left == 0 && right == 400, "none");
	{
		std::vector<WrapObstacle> o = {{{20, 50, 380, 100}, 1, 3}};
		left = 0;
		right = 400;
		Check(!WrapLine(o, 60, 72, left, right, 36, next) && next == 100, "no room: below it");
	}

	printf("text_wrap: ok %d, failed %d\n", sOk, sFailed);
	return sFailed ? 1 : 0;
}
