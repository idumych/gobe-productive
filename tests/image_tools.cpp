// image_tools: the image part's tools on a bitmap (ImageTools): the
// original's color distance (TColor::Distance: black to white is
// sqrt(2 * 255^2 + 4 * 255^2 + 2 * 255^2) = 721.25), the pencil (a pixel,
// the background color over the foreground one, lines with both ends), the
// eraser's 16 pixel square at 100% and 8 at 200%, the paint bucket held by a
// wall and by the tolerance, and the selection's mask kept to by each.
#include <cmath>
#include <cstdio>
#include <string>

#include "ImageTools.h"

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
White(int width, int height)
{
	ArchiveBitmap b;
	b.colorSpace = 0x2008;
	b.bytesPerRow = uint32_t(width) * 4;
	b.bounds[2] = float(width - 1);
	b.bounds[3] = float(height - 1);
	b.bits.assign(size_t(width) * size_t(height) * 4, 0xff);
	return b;
}


static int
Count(const ArchiveBitmap& b, IMColor c)
{
	int n = 0;
	for (int y = 0; y < b.Height(); y++)
		for (int x = 0; x < b.Width(); x++)
			n += PixelAt(b, x, y) == c;
	return n;
}


int
main()
{
	IMColor black, white, red;
	white.r = white.g = white.b = 255;
	red.r = 255;
	Check(std::fabs(ColorDistance(black, white) - 721.25f) < 0.01f, "black to white");
	Check(ColorDistance(red, red) == 0, "a color to itself");
	Check(std::fabs(ToleranceDistance(20) - 144.4f) < 0.001f, "20 percent");

	ArchiveBitmap b = White(32, 32);
	IMMask none;
	IMColor used;
	IMRect r = PencilDown(b, none, 3, 4, black, white, used);
	Check(used == black && PixelAt(b, 3, 4) == black && r.left == 3 && r.right == 3,
		"the pencil's pixel");
	r = PencilDown(b, none, 3, 4, black, white, used);
	Check(used == white && PixelAt(b, 3, 4) == white, "over the foreground: the background");
	b = White(32, 32);
	r = PencilLine(b, none, 0, 0, 5, 2, black);
	Check(Count(b, black) == 6 && PixelAt(b, 0, 0) == black && PixelAt(b, 5, 2) == black,
		"a line, both ends in");

	b = White(64, 64);
	r = EraserDown(b, none, 20, 20, 1, red);
	Check(Count(b, red) == 16 * 16 && r.left == 13 && r.right == 28, "the eraser at 100%");
	b = White(64, 64);
	EraserDown(b, none, 20, 20, 2, red);
	Check(Count(b, red) == 8 * 8, "the eraser at 200%");
	b = White(64, 64);
	EraserMove(b, none, 20, 20, 30, 20, 1, red);
	Check(Count(b, red) == 26 * 16, "the eraser swept");

	// a wall across at x 10; the bucket from the left fills 10 columns
	b = White(20, 10);
	PencilLine(b, none, 10, 0, 10, 9, black);
	r = BucketFill(b, none, 2, 5, red, ToleranceDistance(20));
	Check(Count(b, red) == 100 && r.left == 0 && r.right == 9, "the bucket stops at the wall");
	// a near color is taken, a far one not
	b = White(20, 10);
	IMColor nearWhite = white;
	nearWhite.b = 230;
	SetPixel(b, 3, 3, nearWhite);
	IMColor grey;
	grey.r = grey.g = grey.b = 100;
	SetPixel(b, 4, 3, grey);
	BucketFill(b, none, 0, 0, red, ToleranceDistance(20));
	Check(PixelAt(b, 3, 3) == red && PixelAt(b, 4, 3) == grey, "the tolerance");

	// a mask: only the left half
	IMMask half;
	half.width = 20;
	half.height = 10;
	half.bits.assign(200, 0);
	for (int y = 0; y < 10; y++)
		for (int x = 0; x < 10; x++)
			half.bits[size_t(y) * 20 + size_t(x)] = 1;
	b = White(20, 10);
	BucketFill(b, half, 2, 2, red, ToleranceDistance(20));
	Check(Count(b, red) == 100, "the bucket inside the selection");
	b = White(20, 10);
	PencilLine(b, half, 0, 5, 19, 5, black);
	Check(Count(b, black) == 10, "the pencil inside the selection");
	b = White(20, 10);
	Check(!BucketFill(b, half, 15, 2, red, ToleranceDistance(20)).Valid(),
		"nothing from outside the selection");

	printf("image_tools: ok %d, failed %d\n", sOk, sFailed);
	return sFailed == 0 ? 0 : 1;
}
