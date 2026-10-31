// ink_edit: the Inks window's editors' models (InkEdit.h). Every pattern
// of the application's set (BPAT 0) as pixels and back draws the same;
// the pencil, Replace and the sizes; a third colour or another size can't
// be kept; a gradient's type, angle and center set as the editor does,
// written and read back.
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>

#include "DocArchive.h"
#include "InkEdit.h"
#include "InkSets.h"
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


static Ink
ReadBack(const Ink& ink)
{
	Ink back;
	DocArchive a(InkBytes(ink));
	ReadInk(a, back);
	return back;
}


int
main()
{
	std::ifstream file("../original/resources/Gobe Productive/BPAT/0_Black_White.bin",
		std::ios::binary);
	std::vector<uint8_t> resource((std::istreambuf_iterator<char>(file)),
		std::istreambuf_iterator<char>());
	InkSet set;
	Check(ParsePatternSet(resource, set) && set.inks.size() > 10, "BPAT 0");
	int same = 0;
	for (const Ink& ink : set.inks) {
		PatternGrid grid = PatternFromInk(ink);
		Ink made;
		if (OptimizePattern(grid, made)
				&& PatternFromInk(ReadBack(made)).pixels == grid.pixels)
			same++;
	}
	Check(same == int(set.inks.size()), "every pattern back as it was: "
		+ std::to_string(same) + " of " + std::to_string(set.inks.size()));

	// the pencil: a black dot on white; the first pixel's colour is the
	// high one
	PatternGrid grid = PatternFromInk(set.inks[0]);
	for (Color& c : grid.pixels)
		c = Color{255, 255, 255, 255};
	grid.pixels[9] = Color{0, 0, 0, 255};
	Ink dot;
	Check(OptimizePattern(grid, dot) && dot.color.r == 255 && dot.low.r == 0
		&& dot.pattern[0] == 0xff && dot.pattern[1] == 0xbf && dot.pattern[2] == 0xff,
		"a dot: white bits set, the dot clear");
	// Replace: the white made red
	ReplacePatternColor(grid, Color{255, 255, 255, 255}, Color{255, 0, 0, 255});
	Check(grid.At(0).r == 255 && grid.At(0).g == 0 && grid.At(9).r == 0, "white replaced by red");
	// a third colour: not kept
	PatternGrid three = grid;
	three.pixels[20] = Color{0, 0, 255, 255};
	Ink none;
	Check(!OptimizePattern(three, none) && !none.set, "three colours not a pattern ink");
	// sizes: 16 x 8 repeats the pattern; back to 8 x 8 as it was
	PatternGrid wide = grid;
	ResizePattern(wide, 16, 8);
	Check(wide.cols == 16 && wide.pixels.size() == 128 && wide.At(16 + 1).r == 0
		&& wide.At(16 + 9).r == 0 && wide.At(16 + 2).r == 255, "16 wide repeats");
	Check(!OptimizePattern(wide, none), "16 x 8 not a pattern ink");
	ResizePattern(wide, 8, 8);
	Check(wide.pixels == grid.pixels, "back to 8 x 8");

	// gradients
	Ink g = NewGradient();
	Check(g.gradient && g.gradientType == Sig("GDdr") && g.colorCount == 2
		&& g.colors[0].r == 0 && g.colors[1].r == 255, "a new gradient: black to white");
	GradientClick(g, 120, 60, 120, 120);
	Check(g.angle == 0, "a click right of the middle: 0 degrees");
	GradientClick(g, 60, 0, 120, 120);
	Check(g.angle == 90, "above: 90");
	GradientClick(g, 0, 120, 120, 120);
	Check(g.angle == 225, "lower left: 225: " + std::to_string(g.angle));
	SetGradientType(g, Sig("GDrl"));
	GradientClick(g, 30, 90, 120, 120);
	Check(g.gradientType == Sig("GDrl") && g.center[0] == 0.25f && g.center[1] == 0.75f
		&& g.angle == 225, "radial: the click its center");
	g.colorCount = 3;
	g.colors[2] = Color{255, 0, 0, 255};
	Ink back = ReadBack(g);
	Check(back.gradient && back.gradientType == Sig("GDrl") && back.colorCount == 3
		&& back.colors[2].r == 255 && back.colors[2].g == 0
		&& std::fabs(back.center[0] - 0.25f) < 0.001f, "the gradient written and read back");
	SetGradientType(g, Sig("XXXX"));
	Check(g.gradientType == Sig("GDrl"), "an unknown type ignored");

	printf("ink_edit: ok %d, failed %d\n", sOk, sFailed);
	return sFailed ? 1 : 0;
}
