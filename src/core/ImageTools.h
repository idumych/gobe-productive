// The image part's tools on its bitmap (Parts/ImageProcessing: PencilTool,
// EraserTool, BucketTool, EyeDropperTool, ...): the pixels they change, each
// only where the selection lets it (IMToolSelection: none, or its mask).
// Bitmaps are B_RGBA32 or B_RGB32 as BeOS x86 kept them (B, G, R, A).
#ifndef GOBE_IMAGE_TOOLS_H
#define GOBE_IMAGE_TOOLS_H

#include <array>
#include <cstdint>
#include <vector>

#include "IMPart.h"

namespace gobe {

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

// AirBrushTool's settings (GetFactoryConfig: radii 20, flow rate 15,
// spacing 15, focus 0) and its brush (BuildBrush: concentric ellipses from
// the radii less 1.5 in, each a pixel smaller, their alpha up from the edge
// by the focus to the flow rate's share of 255)
struct AirbrushSettings {
	int xRadius = 20, yRadius = 20, flowRate = 15, spacing = 15, focus = 0;
};
struct AirbrushBrush {
	int width = 0, height = 0;		// the radii twice, and one
	std::vector<IMColor> pixels;
};
AirbrushBrush BuildAirbrush(IMColor color, const AirbrushSettings& settings);
// Spritz: the brush centered on the point, blended in (DrawBitmapMasked,
// B_OP_ALPHA: each channel by the brush's alpha, the alpha added)
IMRect AirbrushStamp(ArchiveBitmap& bitmap, const IMMask& mask, const AirbrushBrush& brush,
	int x, int y);
// AirBrushTool::Modify '_MMV' from the last point to this one: the points
// stamped (a spacing of the radii's sum's percent apart along the way, then
// this one), or, nearer than that, the last point again if `timeUp` (125 ms
// since the last stamp: TPartIM::Idle keeps it spraying), else none. `moved`
// gets whether the last point becomes this one.
std::vector<std::array<int, 2>> AirbrushPath(int x0, int y0, int x1, int y1,
	const AirbrushSettings& settings, bool timeUp, bool& moved);

} // namespace gobe

#endif
