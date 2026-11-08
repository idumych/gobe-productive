// TPageView's images (Parts/ImageProcessing: TPartIM): the tool in use on the
// image (TPartIM::Click, the tools' Modify: ImageTools), the PartBar's tools
// and colors (TPartIM::HandleMessage).
#include "TPageView.h"

#include <MessageRunner.h>
#include <Window.h>

#include <cmath>

#include "GobeResources.h"
#include "IMPart.h"
#include "ImageSelect.h"
#include "ImageTools.h"
#include "TImageDialogs.h"
#include "TRibbon.h"

namespace gobe {

namespace {

// TPartIMFactory's tools, by index
enum {
	kMarquee, kLasso, kMagicWand, kMagnify, kFindColor, kPencil, kEraser, kEyeDropper, kBucket,
	kAirbrush
};


const ResourceFile&
ImageResources()
{
	return GobeResources::Part("ImageProcessing");
}


IMColor
ColorOf(const uint8_t c[4])
{
	IMColor color;
	color.r = c[0];
	color.g = c[1];
	color.b = c[2];
	color.a = c[3];
	return color;
}


} // namespace


bool
TPageView::ImageAt(BPoint where, size_t& part, int& x, int& y) const
{
	float dx = fMargin - fBounds.left, dy = fMargin - fBounds.top;
	const Document::PlacedFrame* hit = NULL;
	for (const Document::PlacedFrame& f : fFrames) {
		if (f.part >= fDocument.parts.parts.size() || fDocument.ImagePart(f.part) == NULL)
			continue;
		BRect r(f.x + dx, f.y + dy, f.x + dx + f.width, f.y + dy + f.height);
		if (r.Contains(where) && (hit == NULL || f.depth >= hit->depth))
			hit = &f;
	}
	if (hit == NULL)
		return false;
	// (TFrameIM: the image's rectangle the frame shows)
	const float* shown = hit->frame->sheetRect;
	float left = shown[2] >= shown[0] ? shown[0] : 0, top = shown[2] >= shown[0] ? shown[1] : 0;
	part = hit->part;
	x = int(floorf(where.x - (hit->x + dx) + left));
	y = int(floorf(where.y - (hit->y + dy) + top));
	return true;
}


bool
TPageView::ImagePointOf(size_t part, BPoint where, BPoint& point) const
{
	float dx = fMargin - fBounds.left, dy = fMargin - fBounds.top;
	for (const Document::PlacedFrame& f : fFrames) {
		if (f.part != part)
			continue;
		const float* shown = f.frame->sheetRect;
		float left = shown[2] >= shown[0] ? shown[0] : 0, top = shown[2] >= shown[0] ? shown[1] : 0;
		point.x = where.x - (f.x + dx) + left;
		point.y = where.y - (f.y + dy) + top;
		return true;
	}
	return false;
}


// TPartIM::SetMarqueeHack: the old rectangle and the new redrawn
void
TPageView::SetImageMarquee(size_t part, BRect rect)
{
	fImageMarquee = rect;
	fImageMarqueePart = part;
	Invalidate();
}


// MagnifyTool::Modify: '_MDN' anchors a rectangle, '_MMV' stretches it to the
// point (with Option it stays the anchor), '_MUP' zooms to show it
// (TDocWindow::CalcScaleFromSize), or for a click twice as close (with
// Option half: CalcScale), around its middle; not past 1/64 or 64
bool
TPageView::MagnifyAt(size_t part, BPoint point, int32 phase)
{
	bool option = (modifiers() & B_OPTION_KEY) != 0;
	if (option ? fZoom <= 0.015625f : fZoom >= 64)
		return false;
	// (MagnifyTool::CalcScale)
	float scale = option ? fZoom * 0.5f : fZoom + fZoom;
	if (scale > 64 || scale < 0.015625f)
		scale = -1;
	BPoint anchor = fImageStroke.anchor;
	BRect rect(std::min(point.x, anchor.x), std::min(point.y, anchor.y),
		std::max(point.x, anchor.x), std::max(point.y, anchor.y));
	if (phase == '_MDN') {
		fImageStroke.anchor = point;
		SetImageMarquee(part, BRect(point, point));
	} else if (phase == '_MMV') {
		if (option)
			rect = BRect(anchor, anchor);
		SetImageMarquee(part, rect);
	} else {
		// (TFrameIM::Scale: an image frame shows its pixels as points)
		if (rect.LeftTop() != rect.RightBottom() && !option) {
			BRect bounds = Bounds();
			scale = bounds.Width() / bounds.Height() < rect.Width() / rect.Height()
				? bounds.Width() / rect.Width() : bounds.Height() / rect.Height();
		}
		scale = std::max(0.015625f, std::min(64.0f, scale));
		BPoint middle((rect.right - rect.left) * 0.5f + rect.left,
			(rect.bottom - rect.top) * 0.5f + rect.top);
		// (the frame's point, in the view's points)
		float dx = fMargin - fBounds.left, dy = fMargin - fBounds.top;
		for (const Document::PlacedFrame& f : fFrames)
			if (f.part == part) {
				const float* shown = f.frame->sheetRect;
				float left = shown[2] >= shown[0] ? shown[0] : 0;
				float top = shown[2] >= shown[0] ? shown[1] : 0;
				middle.x += f.x + dx - left;
				middle.y += f.y + dy - top;
				break;
			}
		SetZoomAround(scale, middle);
	}
	return true;
}


// TPartIM::Click: an image document's image (an image in another document is
// edited once its part is active: not yet), the tool in use; Option makes
// the pencil, bucket and airbrush the eyedropper (OptionToggle)
bool
TPageView::ImageClick(BPoint where, int32 clicks, int32 buttons)
{
	int main = fDocument.MainPart();
	size_t part;
	int x, y;
	if (main < 0 || fDocument.ImagePart(size_t(main)) == NULL || !ImageAt(where, part, x, y)
			|| part != size_t(main))
		return false;
	const IMPart* image = fDocument.ImagePart(part);
	int32 tool = fImageTool;
	// the selection tools (Flags 1: TPartIM::Click selects)
	if (tool == kMarquee || tool == kLasso || tool == kMagicWand) {
		BPoint point;
		ImagePointOf(part, where, point);
		return ImageSelectClick(part, point, where);
	}
	bool option = (modifiers() & B_OPTION_KEY) != 0;
	if (option && (tool == kPencil || tool == kBucket || tool == kAirbrush))
		tool = kEyeDropper;
	fImageStroke = ImageStroke();
	fImageStroke.tool = tool;
	fImageStroke.part = part;
	fImageStroke.x = x;
	fImageStroke.y = y;
	fImageStroke.option = option;
	IMColor fore = ColorOf(image->colors[0]), back = ColorOf(image->colors[1]);
	IMRect changed;
	if (tool == kMagnify) {
		BPoint point;
		ImagePointOf(part, where, point);
		MagnifyAt(part, point, '_MDN');
	}
	if (tool == kAirbrush) {
		// AirBrushTool::Modify '_MDN': the brush built, sprayed once
		fUndo.BeginDocument(fDocument, IndexString(ImageResources(), 102, 1).c_str(),
			CurrentViewState());
		IMPart* edit = fDocument.EditableImagePart(part);
		fImageStroke.brush = std::make_shared<AirbrushBrush>(BuildAirbrush(fore,
			ToolConfig(kAirbrush).airbrush));
		int left, top;
		ArchiveBitmap& bits = ToolBitmap(*edit, left, top);
		AirbrushStamp(bits, edit->selection.ToolMask(bits.Width(), bits.Height(), left, top),
			*fImageStroke.brush, x - left, y - top);
		fImageStroke.lastStamp = system_time();
		BMessage tick(kMsgImageIdle);
		fImageStroke.idle = new BMessageRunner(BMessenger(this), &tick, 25000);
		fImages.erase(part);
		Invalidate();
	}
	if (tool == kPencil || tool == kEraser || tool == kBucket) {
		// (each its UndoString: STR# 100 on, index 1)
		int32 strings = tool == kPencil ? 100 : tool == kEraser ? 101 : 105;
		fUndo.BeginDocument(fDocument, IndexString(ImageResources(), strings, 1).c_str(),
			CurrentViewState());
		IMPart* edit = fDocument.EditableImagePart(part);
		int left, top;
		ArchiveBitmap& bits = ToolBitmap(*edit, left, top);
		IMMask mask = edit->selection.ToolMask(bits.Width(), bits.Height(), left, top);
		if (tool == kPencil) {
			IMColor used;
			changed = PencilDown(bits, mask, x - left, y - top, fore, back, used);
			fImageStroke.color[0] = used.r;
			fImageStroke.color[1] = used.g;
			fImageStroke.color[2] = used.b;
			fImageStroke.color[3] = used.a;
		} else if (tool == kEraser)
			changed = EraserDown(bits, mask, x - left, y - top, fZoom, back);
		else
			changed = BucketFill(bits, mask, x - left, y - top, fore,
				ToleranceDistance(ToolConfig(kBucket).tolerance));
		fImages.erase(part);
		Invalidate();
		if (tool == kBucket) {
			if (Window() != NULL)
				Window()->PostMessage(kMsgDocumentChanged);
			return true;
		}
	}
	fImageStroke.active = true;
	SetMouseEventMask(B_POINTER_EVENTS, B_LOCK_WINDOW_FOCUS);
	return true;
}


bool
TPageView::ImageDragMoved(BPoint where)
{
	if (ImageSelectMoved(where))
		return true;
	if (!fImageStroke.active)
		return false;
	int32 tool = fImageStroke.tool;
	if (tool == kMagnify) {
		BPoint point;
		if (ImagePointOf(fImageStroke.part, where, point))
			MagnifyAt(fImageStroke.part, point, '_MMV');
		return true;
	}
	if (tool != kPencil && tool != kEraser && tool != kAirbrush)
		return true;
	float dx = fMargin - fBounds.left, dy = fMargin - fBounds.top;
	(void)dx;
	(void)dy;
	size_t part;
	int x, y;
	// (outside the image the point goes on, as the frame's coordinates)
	if (!ImageAt(where, part, x, y) || part != fImageStroke.part) {
		for (const Document::PlacedFrame& f : fFrames)
			if (f.part == fImageStroke.part) {
				x = int(floorf(where.x - (f.x + fMargin - fBounds.left)));
				y = int(floorf(where.y - (f.y + fMargin - fBounds.top)));
			}
	}
	if (tool == kAirbrush) {
		AirbrushTo(x, y);
		return true;
	}
	if (x == fImageStroke.x && y == fImageStroke.y)
		return true;
	IMPart* edit = fDocument.EditableImagePart(fImageStroke.part);
	int left, top;
	ArchiveBitmap& bits = ToolBitmap(*edit, left, top);
	IMMask mask = edit->selection.ToolMask(bits.Width(), bits.Height(), left, top);
	if (tool == kPencil)
		PencilLine(bits, mask, fImageStroke.x - left, fImageStroke.y - top, x - left, y - top,
			ColorOf(fImageStroke.color));
	else
		EraserMove(bits, mask, fImageStroke.x - left, fImageStroke.y - top, x - left, y - top,
			fZoom, ColorOf(edit->colors[1]));
	fImageStroke.x = x;
	fImageStroke.y = y;
	fImages.erase(fImageStroke.part);
	Invalidate();
	return true;
}


// '_MUP': the stroke done; the eyedropper's color taken (EyeDropperTool:
// the foreground's, with Option the background's)
// AirBrushTool::Modify '_MMV': along the way a spacing apart, or held still
// again each 125 ms
void
TPageView::AirbrushTo(int x, int y)
{
	bool moved;
	bool timeUp = system_time() - fImageStroke.lastStamp >= 125000;
	std::vector<std::array<int, 2>> points = AirbrushPath(fImageStroke.x, fImageStroke.y, x, y,
		ToolConfig(kAirbrush).airbrush, timeUp, moved);
	if (points.empty())
		return;
	IMPart* edit = fDocument.EditableImagePart(fImageStroke.part);
	int left, top;
	ArchiveBitmap& bits = ToolBitmap(*edit, left, top);
	IMMask mask = edit->selection.ToolMask(bits.Width(), bits.Height(), left, top);
	for (const std::array<int, 2>& p : points)
		AirbrushStamp(bits, mask, *fImageStroke.brush, p[0] - left, p[1] - top);
	fImageStroke.lastStamp = system_time();
	if (moved) {
		fImageStroke.x = x;
		fImageStroke.y = y;
	}
	fImages.erase(fImageStroke.part);
	Invalidate();
}


// TPartIM::Idle: the tool given the pointer where it is
void
TPageView::ImageIdle()
{
	if (!fImageStroke.active || fImageStroke.tool != kAirbrush)
		return;
	BPoint where;
	uint32 buttons;
	GetMouse(&where, &buttons, false);
	if (buttons == 0)
		return;
	where.x /= fZoom;
	where.y /= fZoom;
	size_t part;
	int x, y;
	if (!ImageAt(where, part, x, y) || part != fImageStroke.part) {
		x = fImageStroke.x;
		y = fImageStroke.y;
	}
	AirbrushTo(x, y);
}


bool
TPageView::ImageDragUp(BPoint where)
{
	if (ImageSelectUp(where))
		return true;
	if (!fImageStroke.active)
		return false;
	fImageStroke.active = false;
	delete fImageStroke.idle;
	fImageStroke.idle = NULL;
	fImageStroke.brush.reset();
	if (fImageStroke.tool == kMagnify) {
		BPoint point;
		ImagePointOf(fImageStroke.part, where, point);
		// (TPartIM::Click: the rectangle gone once the tool is done)
		SetImageMarquee(fImageStroke.part, BRect(0, 0, -1, -1));
		MagnifyAt(fImageStroke.part, point, '_MUP');
		return true;
	}
	if (fImageStroke.tool == kEyeDropper) {
		size_t part;
		int x, y;
		if (ImageAt(where, part, x, y) && part == fImageStroke.part) {
			// (TPartIM::CurrentBitmap's pixel: the floating bits' if they're
			// there)
			const IMPart* image = fDocument.ImagePart(part);
			IMColor picked = PixelAt(image->image, x, y);
			IMRect m = image->selection.Mask();
			if (!image->floating.bits.empty() && m.Valid())
				picked = PixelAt(image->floating, x - m.left, y - m.top);
			bool back = (modifiers() & B_OPTION_KEY) != 0;
			fUndo.BeginDocument(fDocument, IndexString(ImageResources(), 1, back ? 5 : 4).c_str(),
				CurrentViewState());
			IMPart* edit = fDocument.EditableImagePart(part);
			uint8* c = edit->colors[back ? 1 : 0];
			c[0] = picked.r;
			c[1] = picked.g;
			c[2] = picked.b;
			c[3] = picked.a;
			if (fPartBar != NULL)
				fPartBar->Update();
		}
	}
	if (Window() != NULL)
		Window()->PostMessage(kMsgDocumentChanged);
	return true;
}


void
TPageView::ImageModifiersChanged()
{
	int main = fDocument.MainPart();
	if (main < 0 || fDocument.ImagePart(size_t(main)) == NULL)
		return;
	bool option = (modifiers() & B_OPTION_KEY) != 0;
	int32 tool = fImageTool;
	if (option && fImageToggled < 0
			&& (fImageTool == kPencil || fImageTool == kBucket || fImageTool == kAirbrush)) {
		fImageToggled = fImageTool;
		fImageTool = kEyeDropper;
	} else if (!option && fImageToggled >= 0) {
		// (back only if the eyedropper's still the tool)
		if (fImageTool == kEyeDropper)
			fImageTool = fImageToggled;
		fImageToggled = -1;
	}
	if (tool != fImageTool && fPartBar != NULL)
		fPartBar->Update();
}


int
TPageView::ImageCursor(BPoint where) const
{
	int main = fDocument.MainPart();
	size_t part;
	int x, y;
	if (main < 0 || fDocument.ImagePart(size_t(main)) == NULL || !ImageAt(where, part, x, y)
			|| part != size_t(main))
		return -1;
	// TFrameIM::GetCursor: the marquee and lasso over the selection (and
	// neither Shift nor Command down) the hand, B_HAND_CURSOR (the system's)
	const IMPart* image = fDocument.ImagePart(part);
	if ((fImageTool == kMarquee || fImageTool == kLasso) && image != NULL
			&& !image->selection.Empty() && (modifiers() & (B_SHIFT_KEY | B_COMMAND_KEY)) == 0) {
		BPoint point;
		ImagePointOf(part, where, point);
		if (image->selection.Contains(int(lrintf(point.x)), int(lrintf(point.y))))
			return kImageSystemCursor;
	}
	switch (fImageTool) {
		// (the marquee's is TPartIMFactory's)
		case kMarquee: return 0x644;
		case kLasso: return 0x648;
		case kMagicWand: return 0x647;
		// MagnifyTool::GetCursor: closer, with Option farther, or neither
		// (CalcScale)
		case kMagnify: {
			bool option = (modifiers() & B_OPTION_KEY) != 0;
			float scale = option ? fZoom * 0.5f : fZoom + fZoom;
			if (scale > 64 || scale < 0.015625f)
				return 0x643;
			return option ? 0x642 : 0x641;
		}
		case kPencil: return 0x640;
		case kEraser: return 0x64a;
		case kEyeDropper: return 0x646;
		case kBucket: return 0x645;
		case kAirbrush: return 0x649;
	}
	return -1;
}


// TPartIM::HandleMessage: a tool chosen ('sI07', TPartIM::SetTool), a color
// set ('sI00', 'sI01' with "color"), the colors swapped ('sI09': "Swap
// Colors")
bool
TPageView::ImageCommand(BMessage* message)
{
	if (ImageSelectionCommand(message))
		return true;
	if (message->what != 'sI07' && message->what != 'sI09' && message->what != 'sI00'
			&& message->what != 'sI01' && message->what != 'sI0A')
		return false;
	int main = fDocument.MainPart();
	if (main < 0 || fDocument.ImagePart(size_t(main)) == NULL)
		return true;
	// a tool's settings (TDialogConfigTool, for a tool that has them)
	if (message->what == 'sI0A') {
		int32 tool = message->GetInt32("Tool", -1);
		if (TDialogConfigTool::HasSettings(tool))
			(new TDialogConfigTool(tool))->RunDialog();
		return true;
	}
	if (message->what == 'sI07') {
		int32 tool = message->GetInt32("Tool", -1);
		// (Find Color selects at once, and the tool before it stays)
		if (tool == kFindColor) {
			fImageToggled = -1;
			FindImageColor();
		} else if (tool >= 0 && tool <= kAirbrush) {
			fImageTool = tool;
			fImageToggled = -1;
		}
	} else if (message->what == 'sI00' || message->what == 'sI01') {
		// TPartIM::SetForeColor, SetBackColor ("Set Foreground Color",
		// "Set Background Color")
		const void* data = NULL;
		ssize_t size = 0;
		if (message->FindData("color", B_RGB_COLOR_TYPE, &data, &size) != B_OK
				|| size < ssize_t(sizeof(rgb_color)))
			return true;
		rgb_color color = *(const rgb_color*)data;
		bool back = message->what == 'sI01';
		fUndo.BeginDocument(fDocument, IndexString(ImageResources(), 1, back ? 5 : 4).c_str(),
			CurrentViewState());
		uint8* c = fDocument.EditableImagePart(size_t(main))->colors[back ? 1 : 0];
		c[0] = color.red;
		c[1] = color.green;
		c[2] = color.blue;
		c[3] = 255;
		if (Window() != NULL)
			Window()->PostMessage(kMsgDocumentChanged);
	} else {
		fUndo.BeginDocument(fDocument, IndexString(ImageResources(), 1, 21).c_str(),
			CurrentViewState());
		IMPart* edit = fDocument.EditableImagePart(size_t(main));
		for (int i = 0; i < 4; i++)
			std::swap(edit->colors[0][i], edit->colors[1][i]);
		if (Window() != NULL)
			Window()->PostMessage(kMsgDocumentChanged);
	}
	if (fPartBar != NULL)
		fPartBar->Update();
	return true;
}

} // namespace gobe
