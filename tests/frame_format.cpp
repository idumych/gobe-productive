// frame_format: the FormatBar's inks, transparency and pen size on a frame
// (SetFrameProperty: TFormatCommand on a selected frame's style).
// LoanCalculator's instructions frame (a text frame in the sheet's
// drawing) gets a red fill at half opacity, a blue pen 3 points wide;
// saved and opened again, its style resolves to them, and the frames that
// shared its old style keep theirs.
// Arguments: documents (LoanCalculator is used).
#include <cstdio>
#include <string>

#include "Document.h"
#include "Drawing.h"
#include "InlineParts.h"
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


static Property
SolidInk(const char (&type)[5], uint8_t r, uint8_t g, uint8_t b)
{
	Property p{Sig(type), std::vector<uint8_t>(17, 0xff)};
	const uint8_t head[9] = {1, r, g, b, 0xff, 0x77, 0x74, 0x77, 0};
	std::copy(head, head + 9, p.data.begin());
	return p;
}


int
main(int argc, char** argv)
{
	bool found = false;
	for (int i = 1; i < argc; i++) {
		std::string path = argv[i];
		if (path.size() < 14 || path.compare(path.size() - 14, 14, "LoanCalculator") != 0)
			continue;
		found = true;
		Document doc;
		std::string error;
		Check(LoadDocument(path, doc, error), "LoanCalculator " + error);
		// the text frame in the sheet's drawing, and its pointer
		uint32_t pointer[3] = {};
		const Frame* frame = nullptr;
		for (size_t p = 0; p < doc.tails.size() && frame == nullptr; p++) {
			if (!doc.tailRead[p] || doc.parts.parts[p].kind != Sig("WORD"))
				continue;
			for (const Portal& portal : doc.tails[p].portals)
				for (const Frame& f : portal.frames)
					if (frame == nullptr && f.container < doc.parts.parts.size()
							&& doc.parts.parts[f.container].kind == Sig("GRFX")) {
						frame = &f;
						pointer[0] = uint32_t(p);
						pointer[1] = portal.values[0];
						pointer[2] = f.values[1];
					}
		}
		Check(frame != nullptr && FrameAt(doc, pointer) == frame, "the instructions frame");
		if (frame == nullptr)
			break;
		uint32_t oldStyle = frame->style;
		ShapeStyle before = ResolveShapeStyle(doc.styles, oldStyle);
		Check(SetFrameProperty(doc, pointer, SolidInk("FCol", 255, 0, 0)), "fill");
		Check(SetFrameProperty(doc, pointer, SolidInk("PCol", 0, 0, 255)), "pen");
		Check(SetFrameProperty(doc, pointer, OpacityProperty(Sig("PENz"), 3)), "pen size");
		Check(SetFrameProperty(doc, pointer, OpacityProperty(Sig("FTra"), 0.5f)), "fill opacity");
		uint32_t bad[3] = {pointer[0], pointer[1], 0xffff};
		Check(!SetFrameProperty(doc, bad, SolidInk("FCol", 1, 2, 3)), "no such frame");
		std::string out = "/tmp/frame_format_test.gobe";
		Check(doc.Save(out, error), "saved " + error);
		Document back;
		Check(LoadDocument(out, back, error) && back.warnings.empty(), "opened " + error);
		const Frame* again = FrameAt(back, pointer);
		Check(again != nullptr, "the frame again");
		if (again == nullptr)
			break;
		ShapeStyle s = ResolveShapeStyle(back.styles, again->style);
		Check(s.fill.set && s.fill.color.r == 255 && s.fill.color.g == 0 && s.fill.color.b == 0,
			"its fill red");
		Check(s.pen.set && s.pen.color.b == 255 && s.pen.color.r == 0, "its pen blue");
		Check(s.penSize == 3, "its pen 3 points: " + std::to_string(s.penSize));
		Check(s.fillOpacity == 0.5f, "its fill at half opacity");
		Check((again->values[3] & 2) != 0, "its border on (a pen given)");
		// the style it had is still what it was (others may use it)
		ShapeStyle old = ResolveShapeStyle(back.styles, oldStyle);
		Check(old.fill.color.r == before.fill.color.r && old.penSize == before.penSize
			&& old.fillOpacity == before.fillOpacity, "its old style unchanged");
	}
	Check(found, "LoanCalculator among the documents");
	printf("frame_format: ok %d, failed %d\n", sOk, sFailed);
	return sFailed ? 1 : 0;
}
