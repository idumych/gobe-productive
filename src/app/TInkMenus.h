// The ink menus the ribbons share (TInkSetMenu, TMICNPickView): an ink
// set's colors as a matrix of cells, and a menu item showing a MICN with
// its set's menu behind it.
#ifndef GOBE_TINKMENUS_H
#define GOBE_TINKMENUS_H

#include <Bitmap.h>
#include <Menu.h>
#include <MenuItem.h>

#include <memory>
#include <vector>

#include "Drawing.h"
#include "Styles.h"

namespace gobe {

class ResourceFile;

// the inks of a COLR resource (TInkSet::LoadColorSetResource): columns,
// cell size, gap, then RGB as three u32s each
struct ColorSet {
	int32 columns = 1, cell = 10, gap = 2;
	std::vector<rgb_color> colors;
};
ColorSet LoadColorSet(int32 id);

// TInkSetMenu: the active color set (COLR 0) as a matrix; each cell sends a
// copy of `model` with "ink name", "color" (B_RGB_COLOR_TYPE) and "ink index"
BMenu* ColorSetMenu(const BMessage& model);

// TInkSetMenu of the pattern set (BPAT 0, 'BPAT') or the gradient set
// (GRAD 0, 'GRAD'): each cell sends a copy of `model` with "ink name" (the
// set's), "ink index" and "ink" (B_RAW_TYPE: the ink's bytes, InkBytes).
// NULL if the set can't be read.
BMenu* InkSetMenu(uint32 type, const BMessage& model);

// TInkMenuItem: a cell of an ink set's matrix menu (a color, a pattern
// in its two colors, a gradient)
class TInkMenuItem : public BMenuItem {
public:
	TInkMenuItem(rgb_color color, int32 cell, BMessage* message);
	TInkMenuItem(const Ink& ink, int32 cell, BMessage* message);

	virtual void DrawContent();
	virtual void GetContentSize(float* width, float* height);

private:
	Ink fInk;
	int32 fCell;
};

// TMICNPickView's icons: an item showing a MICN (the application's, or
// `resources`), with its set's menu
class TIconMenuItem : public BMenuItem {
public:
	TIconMenuItem(int32 micn, BMenu* menu, const ResourceFile* resources = NULL);

	virtual void DrawContent();
	virtual void GetContentSize(float* width, float* height);

	// after the menu closed with nothing chosen: whether the mouse was let
	// go over this item (TMICNMenuBarElement: the icon clicked)
	bool ReleasedOver() const;

private:
	std::unique_ptr<BBitmap> fIcon;
	BRect fScreenFrame;
};

// A pen type's marks along an outline (TDashPen, TDotPen, THashPen::
// StrokePath) in the view's high color, at `size`; a normal pen strokes
// it as is. `offset` is added to the outline's points.
void StrokePenType(BView* view, const Outline& outline, const PenType& pen, float size,
	BPoint offset = B_ORIGIN);
// TArrow::Draw: the arrowheads (0 none, 1 at the start, 2 at the end, 3
// both) of an open outline, filled in the high color
void FillArrowHeads(BView* view, const Outline& outline, int arrows, float size,
	BPoint offset = B_ORIGIN);

// TPenTypeMenuItem and TArrowsMenu's items: a line drawn with a pen type
// or with arrows
class TLineSampleItem : public BMenuItem {
public:
	TLineSampleItem(const PenType& pen, int arrows, BMessage* message);

	virtual void DrawContent();
	virtual void GetContentSize(float* width, float* height);

private:
	PenType fPen;
	int fArrows;
};

// the application's pens (TPenMgr::loadDefaultPens: the PENS resources)
const std::vector<PenType>& AppPens();
// TPenMenu's pen types: `pens` (a document's, DocumentPens) as line
// samples sending 'sf0A' with "pen" (the pen archived: TPen::Write); the
// one the same as `marked` marked
void AddPenTypeItems(BMenu* menu, const std::vector<PenType>& pens,
	const PenType* marked = NULL);
// a pen archived in a message ("pen": TPen::Write)
void AddPenToMessage(BMessage* message, const PenType& pen);
bool PenFromMessage(const BMessage* message, PenType& pen);

} // namespace gobe

#endif
