// The image part's tools on its bitmap (Parts/ImageProcessing: PencilTool,
// EraserTool, BucketTool, EyeDropperTool, ...): the pixels they change, each
// only where the selection lets it (IMToolSelection: none, or its mask).
// Bitmaps are B_RGBA32 or B_RGB32 as BeOS x86 kept them (B, G, R, A).
#ifndef GOBE_IMAGE_TOOLS_H
#define GOBE_IMAGE_TOOLS_H

#include <cstdint>
#include <vector>

#include "IMPart.h"

namespace gobe {

// TColor: red, green, blue, alpha
struct IMColor {
	uint8_t r = 0, g = 0, b = 0, a = 255;
	bool operator==(const IMColor& o) const
		{ return r == o.r && g == o.g && b == o.b && a == o.a; }
	bool operator!=(const IMColor& o) const { return !(*this == o); }
};

// IMToolSelection's mask: what the tools may change (empty: all of it)
struct IMMask {
	int width = 0, height = 0;
	std::vector<uint8_t> bits;		// a byte a pixel, nonzero inside
	bool Empty() const { return bits.empty(); }
	bool Contains(int x, int y) const;
};

// the pixels a tool changed (inclusive), none if left > right
struct IMRect {
	int left = 0, top = 0, right = -1, bottom = -1;
	bool Valid() const { return left <= right && top <= bottom; }
	void Include(int x, int y);
	void Include(const IMRect& r);
};

IMColor PixelAt(const ArchiveBitmap& bitmap, int x, int y);
void SetPixel(ArchiveBitmap& bitmap, int x, int y, IMColor color);

// TColor::Distance: the weighted ("redmean") distance of two colors, as
// integers weigh it (0 to about 721)
float ColorDistance(IMColor a, IMColor b);
// ToleranceConfigView::FindTolerance: a tolerance in percent as a distance
// (722 a hundred percent; 20 percent without one)
float ToleranceDistance(float percent);
// TColor::Compare(TColor const&, float): within that distance
bool ColorsAlike(IMColor a, IMColor b, float distance);

// PencilTool::Modify, '_MDN': the pixel set in the foreground color, or in
// the background color when it is the foreground color already (`used`
// gets which); nothing outside the selection
IMRect PencilDown(ArchiveBitmap& bitmap, const IMMask& mask, int x, int y, IMColor fore,
	IMColor back, IMColor& used);
// '_MMV': a line a pixel wide from the last point to this one (both
// included), in the color the stroke started with
IMRect PencilLine(ArchiveBitmap& bitmap, const IMMask& mask, int x0, int y0, int x1, int y1,
	IMColor color);

// EraserTool::Modify: '_MDN' a square 16 view pixels across (7 before the
// point, 8 after, over `scale`) in the background color; '_MMV' the shape
// from the last square to this one
IMRect EraserDown(ArchiveBitmap& bitmap, const IMMask& mask, int x, int y, float scale,
	IMColor back);
IMRect EraserMove(ArchiveBitmap& bitmap, const IMMask& mask, int x0, int y0, int x1, int y1,
	float scale, IMColor back);

// BucketTool::Modify: from the pixel, the pixels joined to it whose colors
// are within `distance` of its, in the foreground color (a span fill, as
// Heckbert's); nothing if the pixel is outside the selection
IMRect BucketFill(ArchiveBitmap& bitmap, const IMMask& mask, int x, int y, IMColor fore,
	float distance);

} // namespace gobe

#endif
