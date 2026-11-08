// TPageView's image selections (Parts/ImageProcessing): the marquee, lasso
// and magic wand (TPartIM::Click's selection tools), the selection moved,
// duplicated, floated and put down again (TIMMoveSelectionCommand,
// TIMStampBitsCommand), the Image menu's and Edit menu's commands on it
// (TPartIM::HandleMessage), its keys (TPartIM::KeyDown, ArrowKey), pictures
// pasted and dropped in (TIMPasteCommand, TIMDropBitmapCommand) and the
// marching ants around it (TIMSelection::Draw, TPartIM::Idle).
#include "TPageView.h"

#include <Beep.h>
#include <Bitmap.h>
#include <Clipboard.h>
#include <Entry.h>
#include <File.h>
#include <MessageRunner.h>
#include <TranslationUtils.h>
#include <Window.h>

#include <cmath>

#include "GobeResources.h"
#include "ImageSelect.h"
#include "TImageDialogs.h"
#include "TRibbon.h"

namespace gobe {

namespace {

// TPartIMFactory's tools, by index
enum {
	kMarquee, kLasso, kMagicWand, kMagnify, kFindColor, kPencil, kEraser, kEyeDropper, kBucket,
	kAirbrush
};

// STR# 1 of the image part (from 0)
enum {
	kStrPasteImage = 3, kStrInsert = 20, kStrDragAndDrop = 6, kStrCropImage = 7, kStrMoveSelection = 8,
	kStrDeselect = 9, kStrFloatSelection = 10, kStrUnfloatSelection = 11, kStrSelectAll = 13,
	kStrCut = 16, kStrDelete = 17, kStrClear = 18, kStrDuplicate = 19, kStrInvertSelection = 22
};

const ResourceFile&
ImageResources()
{
	return GobeResources::Part("ImageProcessing");
}


std::string
ImageString(int index)
{
	return IndexString(ImageResources(), 1, index);
}


// a tool's UndoString (STR# 103 on, index 1)
std::string
ToolUndoString(int32 tool)
{
	static const int32 kStrings[] = {103, 104, 107, 108, 109, 100, 101, 106, 105, 102};
	if (tool < 0 || tool > kAirbrush)
		return std::string();
	return IndexString(ImageResources(), kStrings[tool], 1);
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


uint32
ImageModifiers(uint32 modifiers)
{
	uint32 m = 0;
	if (modifiers & B_SHIFT_KEY)
		m |= kIMShift;
	if (modifiers & B_COMMAND_KEY)
		m |= kIMCommand;
	if (modifiers & B_OPTION_KEY)
		m |= kIMOption;
	return m;
}


int
Rounded(float v)
{
	return int(lrintf(v));
}


// TIMSelection::CopyBitmap: the selected pixels (the floating bitmap's, or
// the image's under the selection), clear white around them
ArchiveBitmap
SelectionBits(const IMPart& im)
{
	const IMSelection& s = im.selection;
	IMRect m = s.Mask();
	ArchiveBitmap bits = im.floating.bits.empty() ? MainBits(im.image, m) : im.floating;
	for (int v = 0; v < bits.Height(); v++)
		for (int u = 0; u < bits.Width(); u++)
			if (!s.Contains(m.left + u, m.top + v)) {
				uint8_t* p = &bits.bits[size_t(v) * bits.bytesPerRow + size_t(u) * 4];
				p[0] = p[1] = p[2] = 0xff;
				p[3] = 0;
			}
	return bits;
}


BBitmap*
ToBBitmap(const ArchiveBitmap& bits)
{
	BBitmap* bitmap = new BBitmap(BRect(0, 0, bits.Width() - 1, bits.Height() - 1), B_RGBA32);
	if (bitmap->InitCheck() != B_OK || bitmap->ImportBits(bits.bits.data(), bits.bits.size(),
			bits.bytesPerRow, 0, color_space(bits.colorSpace)) != B_OK) {
		delete bitmap;
		return NULL;
	}
	return bitmap;
}


// ValidateBitmap: the picture as the image keeps its pixels (B_RGBA32)
bool
FromBBitmap(BBitmap* bitmap, ArchiveBitmap& out, uint32 colorSpace)
{
	if (bitmap == NULL || !bitmap->IsValid())
		return false;
	BRect b = bitmap->Bounds();
	int w = int(b.Width()) + 1, h = int(b.Height()) + 1;
	BBitmap converted(BRect(0, 0, w - 1, h - 1), B_RGBA32);
	if (converted.InitCheck() != B_OK || converted.ImportBits(bitmap) != B_OK)
		return false;
	out = ArchiveBitmap();
	out.colorSpace = colorSpace == 0x0008 ? 0x0008 : 0x2008;
	out.bytesPerRow = uint32(w) * 4;
	out.bounds[2] = float(w - 1);
	out.bounds[3] = float(h - 1);
	out.bits.resize(size_t(out.bytesPerRow) * size_t(h));
	const uint8* from = (const uint8*)converted.Bits();
	for (int y = 0; y < h; y++)
		memcpy(&out.bits[size_t(y) * out.bytesPerRow],
			from + size_t(y) * size_t(converted.BytesPerRow()), out.bytesPerRow);
	return true;
}


// GetBitmapFromMessage: an archived bitmap under one of its names, an
// archived bitmap itself, or the first file of "refs" through the
// translators
BBitmap*
BitmapFromMessage(const BMessage* message)
{
	if (message->what == 'ARCV') {
		const char* name = NULL;
		if (message->FindString("class", &name) == B_OK && strcmp(name, "BBitmap") == 0)
			return new BBitmap(const_cast<BMessage*>(message));
		return NULL;
	}
	static const char* kNames[] = {"image/x-vnd.Be-bitmap", "image/x-vnd.be-bitmap",
		"image/x-be-bitmap", "bitmap", "icon/large", "icon/mini", "image/bitmap"};
	BMessage archive;
	for (const char* name : kNames)
		if (message->FindMessage(name, &archive) == B_OK)
			return new BBitmap(&archive);
	entry_ref ref;
	if (message->FindRef("refs", &ref) != B_OK)
		return NULL;
	BEntry entry(&ref, true);
	BFile file(&entry, B_READ_ONLY);
	if (!entry.IsFile() || file.InitCheck() != B_OK)
		return NULL;
	return BTranslationUtils::GetBitmap(&file);
}

} // namespace


IMPart*
TPageView::MainImage(size_t* part)
{
	int main = fDocument.MainPart();
	if (main < 0 || fDocument.ImagePart(size_t(main)) == NULL)
		return NULL;
	if (part != NULL)
		*part = size_t(main);
	return fDocument.EditableImagePart(size_t(main));
}


const IMPart*
TPageView::MainImage(size_t* part) const
{
	int main = fDocument.MainPart();
	if (main < 0)
		return NULL;
	if (part != NULL)
		*part = size_t(main);
	return fDocument.ImagePart(size_t(main));
}


// the image changed: redrawn, the document marked changed
void
TPageView::ImageChanged(size_t part, bool changed)
{
	fImages.erase(part);
	Invalidate();
	if (fPartBar != NULL)
		fPartBar->Update();
	if (changed && Window() != NULL)
		Window()->PostMessage(kMsgDocumentChanged);
	UpdateImageAnts();
}


// TPartIM::SetSelection: the selection, and where pictures are pasted
// (CalcPastePt: the middle of the last selection there was)
void
TPageView::SetImageSelection(size_t part, IMPart& im, IMSelection selection)
{
	im.selection = std::move(selection);
	IMRect m = im.selection.Mask();
	if (m.Valid())
		fImagePaste[part] = BRect(m.left, m.top, m.right + 1, m.bottom + 1);
}


// TIMSelectCommand::Stamp: the floating bitmap put down (TIMStampBitsCommand
// (false, true): blended over the image, the selection clipped to it)
void
TPageView::StampImage(IMPart& im)
{
	if (!im.floating.bits.empty())
		StampBits(im, im.selection, true, false);
}


// TIMSelectCommand(name, stamp, mode): the floating bits put down, then
// nothing selected (mode 1), all of it (mode 2) or the selection kept (0)
void
TPageView::ImageSelectCommand(int nameIndex, int mode)
{
	size_t part;
	IMPart* im = MainImage(&part);
	if (im == NULL)
		return;
	fUndo.BeginDocument(fDocument, ImageString(nameIndex), CurrentViewState());
	im = fDocument.EditableImagePart(part);
	StampImage(*im);
	int w = im->image.Width(), h = im->image.Height();
	if (mode == 1)
		SetImageSelection(part, *im, ImageSelection(w, h));
	else if (mode == 2)
		SetImageSelection(part, *im, ImageSelection(w, h, 0, 0, w - 1, h - 1));
	ImageChanged(part);
}


// TIMMoveSelectionCommand(part, duplicate): its TIMStampBitsCommand (lift
// the bits unless they're floating already or duplicated; put a copy down
// when duplicated): bits lifted from the image leave the background color
// behind, a duplicate's don't
void
TPageView::BeginImageMove(IMPart& im, bool duplicate)
{
	bool floating = !im.floating.bits.empty();
	bool lift = !floating || duplicate, stamp = duplicate;
	if (!floating) {
		if (lift)
			LiftBits(im, im.selection, !stamp);
	} else if (stamp)
		StampBits(im, im.selection, true, lift);
}


// TPartIM::Click with a selection tool ('_MDN'): the tool's selection
// begun, or (in the selection, with neither Shift nor Command) the
// selection moved once the pointer goes 4 pixels
bool
TPageView::ImageSelectClick(size_t part, BPoint point, BPoint where)
{
	IMPart* im = MainImage();
	if (im == NULL)
		return false;
	int w = im->image.Width(), h = im->image.Height();
	ImageSelect& s = fImageSelect;
	s = ImageSelect();
	s.part = part;
	s.tool = fImageTool;
	// (the tool Option made the eyedropper doesn't get Option)
	s.modifiers = ImageModifiers(modifiers());
	if (fImageToggled >= 0)
		s.modifiers &= ~kIMOption;
	s.start = where;
	// TPoint::Pin: kept to the image
	point.x = std::max(0.0f, std::min(point.x, float(w - 1)));
	point.y = std::max(0.0f, std::min(point.y, float(h - 1)));
	s.old = im->selection;
	s.cur = im->selection;
	bool selecting = false;
	if (s.tool == kMarquee) {
		// MarqueeTool::Select '_MDN': "anchor", "prev"; in the selection
		// it moves
		s.anchor = s.prev = point;
		selecting = !s.cur.Contains(Rounded(point.x), Rounded(point.y));
		s.cur.Clear();
	} else if (s.tool == kLasso) {
		s.prev = point;
		s.points.push_back({int(point.x > 0 ? floorf(point.x + 0.5f) : ceilf(point.x - 0.5f)),
			int(point.y > 0 ? floorf(point.y + 0.5f) : ceilf(point.y - 0.5f))});
		selecting = !s.cur.Contains(Rounded(point.x), Rounded(point.y));
		if (selecting)
			s.cur.Clear();
	} else {
		// MagicWandTool::Select: from the pixel, within the tolerance
		s.cur.Clear();
		selecting = MagicWand(im->image, Rounded(point.x), Rounded(point.y),
			ToleranceDistance(ToolConfig(kMagicWand).tolerance), s.cur);
	}
	bool combining = (s.modifiers & (kIMShift | kIMCommand)) != 0;
	if (!selecting && !combining) {
		// (the wand outside the image does nothing)
		if (s.tool == kMagicWand)
			return true;
		s.mode = ImageSelect::kWaiting;
		SetMouseEventMask(B_POINTER_EVENTS, B_LOCK_WINDOW_FOCUS);
		return true;
	}
	// TIMSelectCommand(the tool's UndoString, false, 0), stamped
	s.before.reset(new Document(fDocument));
	s.beforeView = CurrentViewState();
	s.label = ToolUndoString(s.tool);
	im = fDocument.EditableImagePart(part);
	StampImage(*im);
	if (!combining)
		SetImageSelection(part, *im, s.cur);
	else {
		IMSelection kept = s.old;
		kept.Clip(w, h);
		SetImageSelection(part, *im, kept);
	}
	if (s.tool == kMagicWand) {
		// (it selects at once: TCombineSelection)
		SetImageSelection(part, *im, CombineSelection(s.modifiers, s.old, s.cur));
		fUndo.BeginDocument(*s.before, s.label, s.beforeView);
		s = ImageSelect();
		ImageChanged(part);
		return true;
	}
	s.mode = ImageSelect::kSelecting;
	ImageChanged(part, false);
	SetMouseEventMask(B_POINTER_EVENTS, B_LOCK_WINDOW_FOCUS);
	return true;
}


bool
TPageView::ImageSelectMoved(BPoint where)
{
	ImageSelect& s = fImageSelect;
	if (s.mode == ImageSelect::kNone)
		return false;
	IMPart* im = fDocument.EditableImagePart(s.part);
	if (im == NULL)
		return true;
	BPoint point;
	ImagePointOf(s.part, where, point);
	int w = im->image.Width(), h = im->image.Height();
	if (s.mode == ImageSelect::kSelecting) {
		point.x = std::max(0.0f, std::min(point.x, float(w - 1)));
		point.y = std::max(0.0f, std::min(point.y, float(h - 1)));
		if (point == s.prev)
			return true;
		s.prev = point;
		if (s.tool == kMarquee) {
			// '_MMV': the rectangle from the anchor
			int l, t, r, b;
			MarqueeRect(s.anchor.x, s.anchor.y, point.x, point.y, l, t, r, b);
			s.cur.SetRect(l, t, r, b);
		} else {
			s.points.push_back({int(point.x > 0 ? floorf(point.x + 0.5f) : ceilf(point.x - 0.5f)),
				int(point.y > 0 ? floorf(point.y + 0.5f) : ceilf(point.y - 0.5f))});
			LassoFill(s.points, s.cur);
		}
		SetImageSelection(s.part, *im, CombineSelection(s.modifiers, s.old, s.cur));
		ImageChanged(s.part, false);
		return true;
	}
	if (s.mode == ImageSelect::kWaiting) {
		// (4 of the image's pixels away)
		float dx = (where.x - s.start.x) / ZoomOf(s.part), dy = (where.y - s.start.y) / ZoomOf(s.part);
		if (dx <= 4 && dx >= -4 && dy <= 4 && dy >= -4)
			return true;
		if (im->selection.Empty()) {
			s.mode = ImageSelect::kNone;
			return true;
		}
		// TIMMoveSelectionCommand, Option making it a copy
		fUndo.BeginDocument(fDocument, ImageString(kStrMoveSelection), CurrentViewState());
		im = fDocument.EditableImagePart(s.part);
		BeginImageMove(*im, (modifiers() & B_OPTION_KEY) != 0);
		ImagePointOf(s.part, s.start, s.anchor);
		s.startX = im->selection.x;
		s.startY = im->selection.y;
		s.mode = ImageSelect::kMoving;
	}
	// the selection where the pointer takes it (Shift: along 45 degree
	// lines, ShiftConstrain)
	if ((modifiers() & B_SHIFT_KEY) != 0)
		ShiftConstrain(s.anchor.x, s.anchor.y, point.x, point.y, 45);
	int x = s.startX + Rounded(point.x - s.anchor.x), y = s.startY + Rounded(point.y - s.anchor.y);
	if (x == im->selection.x && y == im->selection.y)
		return true;
	im->selection.x = x;
	im->selection.y = y;
	ImageChanged(s.part, false);
	return true;
}


bool
TPageView::ImageSelectUp(BPoint where)
{
	ImageSelect& s = fImageSelect;
	if (s.mode == ImageSelect::kNone)
		return false;
	size_t part = s.part;
	IMPart* im = fDocument.EditableImagePart(part);
	if (im == NULL) {
		s = ImageSelect();
		return true;
	}
	int w = im->image.Width(), h = im->image.Height();
	if (s.mode == ImageSelect::kWaiting) {
		// a click in the selection: Deselect
		s = ImageSelect();
		ImageSelectCommand(kStrDeselect, 1);
		return true;
	}
	if (s.mode == ImageSelect::kMoving) {
		SetImageSelection(part, *im, im->selection);
		s = ImageSelect();
		ImageChanged(part);
		return true;
	}
	// '_MUP': with Option, only what isn't the background color
	if ((s.modifiers & kIMOption) != 0) {
		TrimToBackground(im->image, s.cur, ColorOf(im->colors[1]));
		SetImageSelection(part, *im, s.cur);
	}
	bool wasEmpty = s.old.Empty();
	if (im->selection.Empty()) {
		SetImageSelection(part, *im, ImageSelection(w, h));
		// (nothing was selected and nothing is: no command)
		if (wasEmpty && im->floating.bits.empty() && s.before->ImagePart(part) != NULL
				&& s.before->ImagePart(part)->floating.bits.empty()) {
			s = ImageSelect();
			ImageChanged(part, false);
			return true;
		}
	}
	fUndo.BeginDocument(*s.before, s.label, s.beforeView);
	s = ImageSelect();
	ImageChanged(part);
	return true;
}


// the zoom an image is shown at (TFrameIM::Scale: its pixels as points)
float
TPageView::ZoomOf(size_t) const
{
	return fZoom;
}


// TPartIM::HandleMessage: the selection's commands
bool
TPageView::ImageSelectionCommand(BMessage* message)
{
	size_t part;
	IMPart* im = MainImage(&part);
	if (im == NULL)
		return false;
	int w = im->image.Width(), h = im->image.Height();
	bool selected = !im->selection.Empty();
	switch (message->what) {
		case 'sI05':
			ImageSelectCommand(kStrDeselect, 1);
			return true;
		case B_SELECT_ALL:
			ImageSelectCommand(kStrSelectAll, 2);
			return true;
		case 'sI06':
			ImageSelectCommand(kStrUnfloatSelection, 0);
			return true;
		case 'sI12': {
			// Invert Selection: the selection kept to the image, inverted
			fUndo.BeginDocument(fDocument, ImageString(kStrInvertSelection),
				CurrentViewState());
			im = fDocument.EditableImagePart(part);
			StampImage(*im);
			IMSelection inverted = im->selection;
			inverted.Clip(w, h);
			inverted.Invert();
			SetImageSelection(part, *im, inverted);
			ImageChanged(part);
			return true;
		}
		case 'sI11':
		case 'sM15': {
			// TIMFloatSelectionCommand, TIMDuplicateSelectionCommand (10
			// pixels right and down)
			if (!selected)
				return true;
			bool duplicate = message->what == 'sM15';
			fUndo.BeginDocument(fDocument, ImageString(duplicate ? kStrDuplicate
				: kStrFloatSelection), CurrentViewState());
			im = fDocument.EditableImagePart(part);
			BeginImageMove(*im, true);
			if (im->floating.bits.empty())
				LiftBits(*im, im->selection, false);
			IMSelection moved = im->selection;
			if (duplicate)
				moved.OffsetBy(10, 10);
			SetImageSelection(part, *im, moved);
			ImageChanged(part);
			return true;
		}
		case 'sI04': {
			// TIMCropCommand: the floating bits (lifted, if they weren't)
			// put down at the top left, where the selection was on, on the
			// background color, nothing selected, the image the selection's
			// rectangle in size (TIMSetPartSizeCommand)
			if (!selected)
				return true;
			fUndo.BeginDocument(fDocument, ImageString(kStrCropImage), CurrentViewState());
			im = fDocument.EditableImagePart(part);
			IMRect m = im->selection.Mask();
			if (im->floating.bits.empty())
				LiftBits(*im, im->selection, false);
			IMColor back = ColorOf(im->colors[1]);
			for (int y = 0; y <= std::min(m.bottom - m.top, h - 1); y++)
				for (int x = 0; x <= std::min(m.right - m.left, w - 1); x++)
					SetPixel(im->image, x, y, back);
			IMSelection at = im->selection;
			at.OffsetBy(-m.left, -m.top);
			StampBits(*im, at, false, true);
			im->floating = ArchiveBitmap();
			SetImageSelection(part, *im, ImageSelection(w, h));
			SetImageSize(fDocument, part, float(m.right - m.left + 1),
				float(m.bottom - m.top + 1));
			GeometryChanged();
			ImageChanged(part);
			return true;
		}
		case B_COPY:
		case B_CUT: {
			if (!selected)
				return true;
			CopyImageSelection(*im);
			if (message->what == B_CUT)
				DeleteImageSelection(kStrCut);
			return true;
		}
		case 'sM12':
			DeleteImageSelection(kStrClear);
			return true;
		case B_PASTE: {
			// TIMPasteCommand, then the marquee
			ArchiveBitmap pasted;
			bool found = false;
			if (be_clipboard->Lock()) {
				if (BMessage* data = be_clipboard->Data()) {
					BBitmap* bitmap = BitmapFromMessage(data);
					found = FromBBitmap(bitmap, pasted, im->image.colorSpace);
					delete bitmap;
				}
				be_clipboard->Unlock();
			}
			if (found)
				PasteImage(part, pasted, PastePoint(part), kStrPasteImage);
			fImageTool = kMarquee;
			fImageToggled = -1;
			if (fPartBar != NULL)
				fPartBar->Update();
			return true;
		}
	}
	return false;
}


// TIMDeleteSelectionCommand: the selected pixels gone (lifted, leaving the
// background color, and thrown away), nothing selected
void
TPageView::DeleteImageSelection(int nameIndex)
{
	size_t part;
	IMPart* im = MainImage(&part);
	if (im == NULL || im->selection.Empty())
		return;
	fUndo.BeginDocument(fDocument, ImageString(nameIndex), CurrentViewState());
	im = fDocument.EditableImagePart(part);
	if (im->floating.bits.empty())
		LiftBits(*im, im->selection, true);
	im->floating = ArchiveBitmap();
	SetImageSelection(part, *im, ImageSelection(im->image.Width(), im->image.Height()));
	ImageChanged(part);
}


// TPartIM::CopySelection: the selected pixels on the clipboard as an
// archived bitmap ("image/x-vnd.Be-bitmap") and the selection's mask
// ("mask", B_GRAY1)
void
TPageView::CopyImageSelection(const IMPart& im)
{
	if (!be_clipboard->Lock())
		return;
	BBitmap* bitmap = ToBBitmap(SelectionBits(im));
	if (bitmap != NULL) {
		be_clipboard->Clear();
		BMessage* data = be_clipboard->Data();
		BMessage archive;
		bitmap->Archive(&archive);
		data->AddMessage("image/x-vnd.Be-bitmap", &archive);
		const IMSelection& s = im.selection;
		BBitmap mask(BRect(0, 0, std::max(0, s.width - 1), std::max(0, s.height - 1)),
			B_GRAY1);
		if (mask.InitCheck() == B_OK) {
			uint8* bits = (uint8*)mask.Bits();
			memset(bits, 0, mask.BitsLength());
			for (int v = 0; v < s.height; v++)
				for (int u = 0; u < s.width; u++)
					if (s.bits[size_t(v) * size_t(s.width) + size_t(u)])
						bits[size_t(v) * size_t(mask.BytesPerRow()) + size_t(u >> 3)]
							|= uint8(0x80 >> (u & 7));
			BMessage maskArchive;
			mask.Archive(&maskArchive);
			data->AddMessage("mask", &maskArchive);
		}
		be_clipboard->Commit();
		delete bitmap;
	}
	be_clipboard->Unlock();
}


// TPartIM::PastePt: the middle of the last selection, if it's in the
// image, else of the image
BPoint
TPageView::PastePoint(size_t part) const
{
	const IMPart* im = fDocument.ImagePart(part);
	if (im == NULL)
		return BPoint(0, 0);
	float w = float(im->image.Width()), h = float(im->image.Height());
	BRect r(0, 0, w, h);
	auto found = fImagePaste.find(part);
	if (found != fImagePaste.end())
		r = found->second;
	BPoint middle((r.right - r.left) * 0.5f + r.left, (r.bottom - r.top) * 0.5f + r.top);
	if (!BRect(0, 0, w - 1, h - 1).Contains(middle))
		middle.Set((w - 1) * 0.5f, (h - 1) * 0.5f);
	return BPoint(floorf(middle.x), floorf(middle.y));
}


// TIMPasteCommand::Perform: the floating bits put down, then the picture
// floating with its middle at `point` (SetFloatingSelection, centered),
// all of it selected
void
TPageView::PasteImage(size_t part, const ArchiveBitmap& picture, BPoint point, int nameIndex)
{
	IMPart* im = fDocument.EditableImagePart(part);
	if (im == NULL || picture.bits.empty())
		return;
	fUndo.BeginDocument(fDocument, ImageString(nameIndex), CurrentViewState());
	im = fDocument.EditableImagePart(part);
	StampImage(*im);
	im->floating = picture;
	int pw = picture.Width(), ph = picture.Height();
	IMSelection s = ImageSelection(im->image.Width(), im->image.Height(), 0, 0, pw - 1, ph - 1);
	float x = floorf(point.x - float(pw - 1) / 2), y = floorf(point.y - float(ph - 1) / 2);
	s.OffsetBy(Rounded(x), Rounded(y));
	SetImageSelection(part, *im, s);
	ImageChanged(part);
}


// TPartIM::InsertMessage: File > Insert… in an image, the file's picture
// pasted (TIMInsertCommand: a TIMPasteCommand named "Insert"); false
// outside an image
bool
TPageView::InsertImageFile(const char* path)
{
	size_t part;
	const IMPart* im = MainImage(&part);
	if (im == NULL)
		return false;
	BBitmap* bitmap = BTranslationUtils::GetBitmap(path);
	ArchiveBitmap picture;
	bool found = FromBBitmap(bitmap, picture, im->image.colorSpace);
	delete bitmap;
	if (!found) {
		beep();
		return true;
	}
	PasteImage(part, picture, PastePoint(part), kStrInsert);
	return true;
}


// TFrameIM::DropNative: a picture (or a picture file) dropped on the image
// pasted where it lands (TIMDropBitmapCommand)
bool
TPageView::ImageDrop(BMessage* message)
{
	size_t part;
	const IMPart* im = MainImage(&part);
	if (im == NULL || !message->WasDropped())
		return false;
	BPoint where = ConvertFromScreen(message->DropPoint());
	where.x /= fZoom;
	where.y /= fZoom;
	BPoint point;
	if (!ImagePointOf(part, where, point))
		return false;
	BBitmap* bitmap = BitmapFromMessage(message);
	ArchiveBitmap picture;
	bool found = FromBBitmap(bitmap, picture, im->image.colorSpace);
	delete bitmap;
	if (!found)
		return false;
	PasteImage(part, picture, BPoint(rintf(point.x), rintf(point.y)), kStrDragAndDrop);
	return true;
}


// TPartIM::KeyDown: Backspace and Delete delete the selection, Escape
// deselects (with nothing selected the window's Escape), the arrows move
// the selection a pixel (ArrowKey: Option copies it, unless it's floating)
bool
TPageView::ImageKey(const char* bytes, int32 count)
{
	size_t part;
	IMPart* im = MainImage(&part);
	if (im == NULL || count < 1)
		return false;
	switch (bytes[0]) {
		case B_BACKSPACE:
		case B_DELETE:
			DeleteImageSelection(kStrDelete);
			return true;
		case B_ESCAPE:
			if (im->selection.Empty())
				return false;
			ImageSelectCommand(kStrDeselect, 1);
			return true;
		case B_LEFT_ARROW:
		case B_RIGHT_ARROW:
		case B_UP_ARROW:
		case B_DOWN_ARROW: {
			if (im->selection.Empty())
				return true;
			int dx = bytes[0] == B_LEFT_ARROW ? -1 : bytes[0] == B_RIGHT_ARROW ? 1 : 0;
			int dy = bytes[0] == B_UP_ARROW ? -1 : bytes[0] == B_DOWN_ARROW ? 1 : 0;
			bool option = (modifiers() & B_OPTION_KEY) != 0;
			fUndo.BeginDocument(fDocument, ImageString(kStrMoveSelection), CurrentViewState());
			im = fDocument.EditableImagePart(part);
			bool floating = !im->floating.bits.empty();
			BeginImageMove(*im, !floating && option);
			if (im->floating.bits.empty())
				LiftBits(*im, im->selection, !option);
			IMSelection moved = im->selection;
			moved.OffsetBy(dx, dy);
			SetImageSelection(part, *im, moved);
			ImageChanged(part);
			return true;
		}
	}
	return false;
}


// 'sI07' with Find Color (Flags 0x13: it selects at once, then the tool
// before it is back): every pixel near the foreground color, in the
// selection without Shift or Command, combined with it as the tools do
void
TPageView::FindImageColor()
{
	size_t part;
	IMPart* im = MainImage(&part);
	if (im == NULL)
		return;
	uint32 mods = ImageModifiers(modifiers());
	IMSelection old = im->selection, found = im->selection;
	if ((mods & (kIMShift | kIMCommand)) != 0)
		found.Clear();
	FindColor(im->image, ColorOf(im->colors[0]),
		ToleranceDistance(ToolConfig(kFindColor).tolerance), found);
	fUndo.BeginDocument(fDocument, ToolUndoString(kFindColor), CurrentViewState());
	im = fDocument.EditableImagePart(part);
	StampImage(*im);
	SetImageSelection(part, *im, CombineSelection(mods, old, found));
	ImageChanged(part);
}


// the bitmap a painting tool works on (TPartIM::CurrentBitmap: the
// floating bits if there are any) and where it is in the image
ArchiveBitmap&
TPageView::ToolBitmap(IMPart& im, int& left, int& top)
{
	if (!im.floating.bits.empty()) {
		IMRect m = im.selection.Mask();
		left = m.Valid() ? m.left : 0;
		top = m.Valid() ? m.top : 0;
		return im.floating;
	}
	left = top = 0;
	return im.image;
}


void
TPageView::DetachedFromWindow()
{
	delete fImageAnts;
	fImageAnts = NULL;
	EndImageEditor();
	BView::DetachedFromWindow();
}


// TPartIM::Idle: the marching ants a step on (each byte of the pattern
// turned a bit right) every 50 ms while there's a selection; without one
// the floating bits are gone
void
TPageView::UpdateImageAnts()
{
	size_t part;
	const IMPart* im = MainImage(&part);
	bool ants = im != NULL && !im->selection.Empty();
	if (ants && fImageAnts == NULL) {
		BMessage tick(kMsgImageAnts);
		fImageAnts = new BMessageRunner(BMessenger(this), &tick, 50000);
	} else if (!ants && fImageAnts != NULL) {
		delete fImageAnts;
		fImageAnts = NULL;
	}
}


void
TPageView::ImageAntsTick()
{
	size_t part;
	const IMPart* im = MainImage(&part);
	if (im == NULL || im->selection.Empty()) {
		if (im != NULL && !im->floating.bits.empty())
			fDocument.EditableImagePart(part)->floating = ArchiveBitmap();
		UpdateImageAnts();
		return;
	}
	for (uint8& b : fAnts.data)
		b = uint8(b >> 1 | b << 7);
	IMRect m = im->selection.Mask();
	BRect r = ImageViewRect(part, BRect(m.left, m.top, m.right + 1, m.bottom + 1));
	if (r.IsValid())
		Invalidate(r.InsetByCopy(-2, -2));
}


// a rectangle of the image's pixels in the view's coordinates
BRect
TPageView::ImageViewRect(size_t part, BRect pixels) const
{
	float dx = fMargin - fBounds.left, dy = fMargin - fBounds.top;
	for (const Document::PlacedFrame& f : fFrames) {
		if (f.part != part)
			continue;
		const float* shown = f.frame->sheetRect;
		float left = shown[2] >= shown[0] ? shown[0] : 0, top = shown[2] >= shown[0] ? shown[1] : 0;
		BRect r = pixels.OffsetByCopy(f.x + dx - left, f.y + dy - top);
		return BRect(r.left * fZoom, r.top * fZoom, r.right * fZoom, r.bottom * fZoom);
	}
	return BRect(0, 0, -1, -1);
}


// TIMSelection::Draw: the selection's outline (CalcPath) a pixel wide at any
// zoom in the marching ants (TPartIM's TPenInk: black and white stripes)
void
TPageView::DrawImageSelection(const IMPart& im, BRect source, BRect dest)
{
	if (im.selection.Empty() || IsPrinting())
		return;
	std::vector<std::array<int, 4>> outline = SelectionOutline(im.selection);
	PushState();
	SetScale(1 / fZoom);
	SetDrawingMode(B_OP_COPY);
	SetHighColor(0, 0, 0);
	SetLowColor(255, 255, 255);
	SetPenSize(1);
	float ox = dest.left - source.left, oy = dest.top - source.top;
	for (const auto& seg : outline)
		StrokeLine(BPoint((seg[0] + ox) * fZoom, (seg[1] + oy) * fZoom),
			BPoint((seg[2] + ox) * fZoom, (seg[3] + oy) * fZoom), fAnts);
	PopState();
}

} // namespace gobe
