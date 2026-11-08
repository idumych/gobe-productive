// The image part's selection (Parts/ImageProcessing: IMToolSelection,
// TIMSelection), what the selection tools make of it (MarqueeTool,
// LassoTool, MagicWandTool, FindColorTool) and the selection's floating
// bits (TPartIM::LiftBits, StampBits, SetFloatingSelection).
#ifndef GOBE_IMAGE_SELECT_H
#define GOBE_IMAGE_SELECT_H

#include <array>
#include <cstdint>
#include <vector>

#include "ImageTools.h"

namespace gobe {

class DocArchive;
class DocWriter;

// TIMSelection(TPart*, TRect): a mask the image's size with the rectangle
// on (none if it isn't valid)
IMSelection ImageSelection(int imageWidth, int imageHeight, int left = 0, int top = 0,
	int right = -1, int bottom = -1);

// TIMSelection::TIMSelection(TInputArchive&) after the selection's part and
// value (format 0x16 on: a rectangle's definition and mask, or the mask's
// point and its B_GRAY1 bitmap; before: a rectangle)
void ReadIMSelection(DocArchive& a, IMSelection& out);
// TIMSelection::Write's own data
void WriteIMSelection(DocWriter& w, const IMSelection& selection);

// TCombineSelection: Shift adds the tool's selection to the old one,
// Command takes it away, both keep what's in both; neither: the tool's
uint32_t constexpr kIMShift = 0x1, kIMCommand = 0x2, kIMOption = 0x40;
IMSelection CombineSelection(uint32_t modifiers, const IMSelection& old,
	const IMSelection& tool);

// FUN_0005d248 (the marquee's and lasso's Option on '_MUP'): only the
// selected pixels not in the background color stay on
void TrimToBackground(const ArchiveBitmap& image, IMSelection& selection, IMColor back);

// MarqueeTool::Select '_MMV': the rectangle from the anchor to the point,
// both less half a pixel, rounded
void MarqueeRect(float anchorX, float anchorY, float x, float y, int& left, int& top,
	int& right, int& bottom);

// LassoTool '_MMV': the points so far (rounded, TPoint::Round) as a
// polygon outlined and filled into a mask the selection's size
void LassoFill(const std::vector<std::array<int, 2>>& points, IMSelection& selection);

// MagicWandTool::Select: the pixels joined to (x, y) within `distance` of
// its color, as the bucket finds them (false outside the image)
bool MagicWand(const ArchiveBitmap& image, int x, int y, float distance, IMSelection& selection);

// FindColorTool::Select ('SALL'): every pixel within `distance` of the
// foreground color, in the selection if there is one
void FindColor(const ArchiveBitmap& image, IMColor fore, float distance,
	IMSelection& selection);

// ShiftConstrain (TPoint, TPoint*, float): `point` moved onto the nearest
// line through `start` at a multiple of `step` degrees
void ShiftConstrain(float startX, float startY, float& x, float& y, float step = 45);

// FUN_0005ec40 (DrawBitmapMasked's B_OP_ALPHA): src over dst by src's alpha,
// the alpha the larger of dst's and their sum
IMColor BlendOver(IMColor dst, IMColor src);

// TPartIM::LiftBits(bool): the selected pixels into the floating bitmap
// (the selection's Mask() in size, clear around them); `erase` fills them in
// the image with the background color
void LiftBits(IMPart& part, IMSelection& selection, bool erase);
// TPartIM::StampBits: the floating bitmap put down where the selection is
// on (copied, or with `alpha` blended over); unless `keep` it's gone and
// the selection clipped to the image (CropSelection)
void StampBits(IMPart& part, IMSelection& selection, bool alpha, bool keep);
// the image as TFrameIM shows it: the floating bitmap over it where the
// selection is on
ArchiveBitmap ComposeFloating(const IMPart& part, const IMSelection& selection);
// TPartIM::GetMainBits(rect): the image's pixels in a rectangle (clear
// outside it)
ArchiveBitmap MainBits(const ArchiveBitmap& image, const IMRect& rect);

// the outline of the selected pixels (TIMSelection::CalcPath): the edges
// between pixels on and off, as segments (x0, y0, x1, y1) in image
// coordinates, joined where they line up; a rectangle's four sides
std::vector<std::array<int, 4>> SelectionOutline(const IMSelection& selection);

} // namespace gobe

#endif
