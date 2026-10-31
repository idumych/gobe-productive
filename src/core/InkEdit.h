// The Inks window's ink editors' models: a pattern as a grid of colored
// pixels (TPattern, what TDialogEditPattern edits) and a gradient's
// angle or center set by a click (TGradient::EditClick).
#ifndef GOBE_INK_EDIT_H
#define GOBE_INK_EDIT_H

#include <vector>

#include "Styles.h"

namespace gobe {

// TPattern: columns by rows of colors, row by row
struct PatternGrid {
	int cols = 8, rows = 8;
	std::vector<Color> pixels = std::vector<Color>(64);

	Color At(int index) const { return pixels[size_t(index)]; }
};

// a pattern ink's pixels (TPattern(TColorPattern): the bits set in the
// ink's color, the others in its low color; a solid ink all its color)
PatternGrid PatternFromInk(const Ink& ink);
// TPattern::ChangePatternSize: the pixels kept where they still fit, the
// new ones repeating the old pattern
void ResizePattern(PatternGrid& grid, int cols, int rows);
// the replace tool (TDialogEditPattern::HandleCellAction, tool 7): every
// pixel of `from` made `to`
void ReplacePatternColor(PatternGrid& grid, Color from, Color to);
// TPattern::Optimize: an 8 x 8 pattern of at most two colors as a pattern
// ink (the first pixel's color the high one: its bits set); false for
// another size or more colors (the original keeps those as bitmaps,
// which the port can't write yet)
bool OptimizePattern(const PatternGrid& grid, Ink& ink);

// a gradient ink, new (TGradientFactory::CreateGradient('GDdr'): black to
// white at 0 degrees)
Ink NewGradient();
// TGradientFactory::CreateGradient(type, old): the type changed, its
// colors kept
void SetGradientType(Ink& ink, uint32_t type);
// TGradient::EditClick in the edit view (width by height): a directional
// gradient's angle points from the view's middle to the click (degrees,
// counterclockwise from the right, 0 to 360); a radial one's center is
// the click (fractions of the view)
void GradientClick(Ink& ink, float x, float y, float width, float height);

} // namespace gobe

#endif
