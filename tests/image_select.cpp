// image_select: the image part's selection (ImageSelect): a rectangle set
// (IMToolSelection::Set, grown and placed left of 0), clipped to the image,
// inverted and combined as Shift, Command and both do; its archive (a
// rectangle's definition and mask, else a point and a B_GRAY1 bitmap) read
// back as written; the marquee's rounding, the lasso's polygon, the magic
// wand and Find Color by tolerance, Option's trim to what isn't the
// background color; bits lifted (the background left behind), moved and put
// down, blended by their alpha; the outline's edges; ShiftConstrain's 45
// degree lines.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "DocArchive.h"
#include "DocWriter.h"
#include "Document.h"
#include "ImageSelect.h"

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


static ArchiveBitmap
Filled(int width, int height, IMColor c)
{
	ArchiveBitmap b;
	b.colorSpace = 0x2008;
	b.bytesPerRow = uint32_t(width) * 4;
	b.bounds[2] = float(width - 1);
	b.bounds[3] = float(height - 1);
	b.bits.resize(size_t(width) * size_t(height) * 4);
	for (int y = 0; y < height; y++)
		for (int x = 0; x < width; x++)
			SetPixel(b, x, y, c);
	return b;
}


static int
Count(const IMSelection& s)
{
	int n = 0;
	for (uint8_t b : s.bits)
		n += b != 0;
	return n;
}


int
main()
{
	IMColor white{255, 255, 255, 255}, black{0, 0, 0, 255}, red{255, 0, 0, 255};

	// a rectangle on a mask the image's size
	IMSelection s = ImageSelection(20, 10, 2, 3, 5, 4);
	Check(s.isRect && Count(s) == 8, "rectangle: 4 by 2 pixels");
	IMRect m = s.Mask();
	Check(m.left == 2 && m.top == 3 && m.right == 5 && m.bottom == 4, "rectangle's mask");
	Check(s.Contains(2, 3) && s.Contains(5, 4) && !s.Contains(6, 4) && !s.Contains(1, 3),
		"rectangle contains its corners only");

	// left of 0: the mask placed there
	s = ImageSelection(20, 10);
	s.SetRect(-3, -2, 4, 5);
	Check(s.x == -3 && s.y == -2 && s.Mask().left == -3 && s.Mask().right == 4,
		"rectangle left of 0 moves the mask");
	s.Clip(20, 10);
	m = s.Mask();
	Check(s.x == 0 && s.y == 0 && m.left == 0 && m.top == 0 && m.right == 4 && m.bottom == 5
		&& s.width == 20 && s.height == 10, "clipped to the image");

	// inverted
	s = ImageSelection(10, 10, 0, 0, 4, 9);
	s.Invert();
	Check(!s.isRect && Count(s) == 50 && s.Contains(5, 0) && !s.Contains(4, 0), "inverted");

	// combined: Shift adds, Command takes away, both intersect
	IMSelection a = ImageSelection(10, 10, 0, 0, 5, 5), b = ImageSelection(10, 10, 3, 3, 8, 8);
	Check(Count(CombineSelection(0, a, b)) == 36, "neither: the tool's");
	Check(Count(CombineSelection(kIMShift, a, b)) == 36 + 36 - 9, "Shift: both");
	Check(Count(CombineSelection(kIMCommand, a, b)) == 36 - 9, "Command: the old less the tool's");
	Check(Count(CombineSelection(kIMShift | kIMCommand, a, b)) == 9, "both: what's in both");

	// the archive
	for (int round = 0; round < 2; round++) {
		IMSelection out = round == 0 ? ImageSelection(30, 20, 4, 5, 9, 7) : a;
		if (round == 1) {
			out.Invert();
			out.OffsetBy(2, 1);
		}
		DocWriter w;
		WriteIMSelection(w, out);
		DocArchive arc(w.Data());
		arc.SetVersions(2, 0, 0x17);
		IMSelection in;
		ReadIMSelection(arc, in);
		bool same = in.isRect == out.isRect && Count(in) == Count(out);
		for (int y = -2; y < 25 && same; y++)
			for (int x = -2; x < 35 && same; x++)
				same = in.Contains(x, y) == out.Contains(x, y);
		Check(same, round == 0 ? "a rectangle read back" : "a mask read back");
	}

	// the marquee: both points less half a pixel, rounded
	int l, t, r, bt;
	MarqueeRect(10.3f, 10.7f, 20.6f, 15.2f, l, t, r, bt);
	Check(l == 10 && t == 10 && r == 20 && bt == 15, "marquee rectangle");
	MarqueeRect(20.6f, 15.2f, 10.3f, 10.7f, l, t, r, bt);
	Check(l == 10 && t == 10 && r == 20 && bt == 15, "marquee dragged up and left");

	// the lasso: a square, outline and inside
	s = ImageSelection(20, 20);
	s.Clear();
	LassoFill({{2, 2}, {10, 2}, {10, 10}, {2, 10}}, s);
	Check(Count(s) == 81 && s.Contains(2, 2) && s.Contains(10, 10) && !s.Contains(11, 10),
		"lasso square, 9 by 9");

	// the magic wand and Find Color
	ArchiveBitmap image = Filled(10, 10, white);
	for (int y = 0; y < 10; y++)
		SetPixel(image, 5, y, black);
	SetPixel(image, 8, 8, IMColor{250, 250, 250, 255});
	s = ImageSelection(10, 10);
	Check(MagicWand(image, 1, 1, ToleranceDistance(0), s) && Count(s) == 50,
		"wand stops at the wall, exact color");
	Check(MagicWand(image, 1, 1, ToleranceDistance(20), s) && Count(s) == 50,
		"wand across the wall's side only");
	s = ImageSelection(10, 10);
	Check(MagicWand(image, 7, 7, ToleranceDistance(20), s) && Count(s) == 40
		&& s.Contains(8, 8), "wand takes the near color");
	Check(!MagicWand(image, 12, 1, ToleranceDistance(20), s), "wand outside the image");
	s = ImageSelection(10, 10);
	FindColor(image, black, ToleranceDistance(20), s);
	Check(Count(s) == 10 && s.Contains(5, 3), "Find Color everywhere");
	s = ImageSelection(10, 10, 0, 0, 9, 4);
	FindColor(image, black, ToleranceDistance(20), s);
	Check(Count(s) == 5, "Find Color in the selection");

	// Option's trim
	s = ImageSelection(10, 10, 3, 0, 7, 9);
	TrimToBackground(image, s, white);
	Check(Count(s) == 10 && s.Contains(5, 0), "trimmed to what isn't the background");

	// lifted, moved, put down
	IMPart part;
	part.image = Filled(10, 10, white);
	SetPixel(part.image, 2, 2, red);
	part.colors[1][0] = 0;
	part.colors[1][1] = 0;
	part.colors[1][2] = 255;
	part.colors[1][3] = 255;
	part.selection = ImageSelection(10, 10, 1, 1, 3, 3);
	LiftBits(part, part.selection, true);
	Check(part.floating.Width() == 3 && PixelAt(part.floating, 1, 1) == red
		&& PixelAt(part.image, 2, 2) == (IMColor{0, 0, 255, 255}),
		"lifted: the background left behind");
	part.selection.OffsetBy(4, 0);
	ArchiveBitmap shown = ComposeFloating(part, part.selection);
	Check(PixelAt(shown, 6, 2) == red && PixelAt(part.image, 6, 2) == white,
		"floating bits shown where they are");
	StampBits(part, part.selection, true, false);
	Check(part.floating.bits.empty() && PixelAt(part.image, 6, 2) == red
		&& part.selection.Mask().left == 5, "put down, the selection kept");
	IMColor half = BlendOver(white, IMColor{0, 0, 0, 128});
	Check(half.r == 127 && half.a == 255, "blended by alpha");

	// the outline
	s = ImageSelection(10, 10, 2, 2, 4, 4);
	Check(SelectionOutline(s).size() == 4, "a rectangle's outline: 4 sides");
	s.Invert();
	s.Invert();
	std::vector<std::array<int, 4>> edges = SelectionOutline(s);
	int length = 0;
	for (const auto& e : edges)
		length += std::abs(e[2] - e[0]) + std::abs(e[3] - e[1]);
	Check(edges.size() == 4 && length == 12, "a mask's outline: 3 by 3, joined");

	// ShiftConstrain
	float x = 10, y = 1;
	ShiftConstrain(0, 0, x, y);
	Check(x == 10 && fabsf(y) < 0.001f, "Shift: horizontal");
	x = 9, y = 10;
	ShiftConstrain(0, 0, x, y);
	Check(fabsf(x - y) < 0.01f, "Shift: diagonal");

	// saved with the document (the SelectionMgr's TIMSelection record) and
	// read back, a mask and a rectangle
	for (int round = 0; round < 2; round++) {
		Document doc;
		std::string error;
		if (!LoadDocument("../original/blanks/imgblank", doc, error)) {
			Check(false, "imgblank: " + error);
			break;
		}
		int main = doc.MainPart();
		IMPart* im = main >= 0 ? doc.EditableImagePart(size_t(main)) : nullptr;
		Check(im != nullptr && im->selection.Empty(), "imgblank: nothing selected");
		if (im == nullptr)
			break;
		int w = im->image.Width(), h = im->image.Height();
		im->selection = ImageSelection(w, h, 10, 20, 99, 49);
		if (round == 1)
			im->selection.Invert();
		IMSelection saved = im->selection;
		std::string path = std::string(getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp")
			+ "/image_select.gobe";
		Document again;
		bool ok = doc.Save(path, error) && LoadDocument(path, again, error);
		const IMPart* back = ok ? again.ImagePart(size_t(again.MainPart())) : nullptr;
		bool same = back != nullptr && back->selection.isRect == saved.isRect;
		for (int y = 0; y < h && same; y++)
			for (int x = 0; x < w && same; x++)
				same = back->selection.Contains(x, y) == saved.Contains(x, y);
		Check(same, std::string("saved and read back: ") + (round ? "a mask" : "a rectangle")
			+ " " + error);
	}

	printf("image_select: ok %d, failed %d\n", sOk, sFailed);
	return sFailed != 0;
}
