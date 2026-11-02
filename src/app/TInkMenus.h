// The ink menus the ribbons share (TInkSetMenu, TMICNPickView): an ink
// set's colors as a matrix of cells, and a menu item showing a MICN with
// its set's menu behind it.
#ifndef GOBE_TINKMENUS_H
#define GOBE_TINKMENUS_H

#include <Bitmap.h>
#include <Menu.h>
#include <MenuItem.h>

#include <memory>
#include <string>
#include <vector>

#include "Drawing.h"
#include "InkSets.h"
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

// The ink sets to choose from (TInkSetManager): of a kind (kColorSet,
// kPatternSet, kGradientSet), the application's and the files' in the
// original's "Ink Sets" folder and the port's settings folder "Gobe
// Productive port/Ink Sets" (AvailableInkSets; `path` a file's), then the
// document's own (`document`: its SetMgr)
struct InkSetRef {
	InkSet set;
	std::string path;
	bool document = false;
};
std::vector<std::string> InkSetFolders();
std::vector<InkSetRef> InkSetsOfKind(int kind, const SetMgr* doc);
bool FindInkSet(int kind, const std::string& name, const SetMgr* doc, InkSetRef& ref);
// the active set of a kind (TInkSetManager::ActiveInkSetName,
// MakeInkSetActive: "Standard" until another is chosen)
std::string ActiveInkSetName(int kind, const SetMgr* doc);
void SetActiveInkSet(int kind, const std::string& name);
bool ActiveInkSet(int kind, const SetMgr* doc, InkSetRef& ref);
// an application's or file's set changed (a file's saved to its file; the
// application's kept until quitting); the files read again
bool ChangeInkSet(const InkSetRef& ref, std::string& error);
void ReloadInkSets();
// a copy of `model` for ink `index` of `set`: "ink name", "ink index",
// and "color" (B_RGB_COLOR_TYPE) for a colour or "ink" (B_RAW_TYPE,
// InkBytes) for a pattern or gradient
BMessage* InkSetMessage(const BMessage& model, const InkSet& set, size_t index);
// an ink in a cell (TInkMenuItem, TInkView), its frame grey
void DrawInkCell(BView* view, const Ink& ink, BRect cell, bool selected = false);

// TInkSetMenu: the active color set as a matrix; each cell sends a
// copy of `model` with "ink name", "color" (B_RGB_COLOR_TYPE) and "ink index"
BMenu* ColorSetMenu(const BMessage& model, const SetMgr* doc = NULL);

// TInkSetMenu of the pattern set (BPAT 0, 'BPAT') or the gradient set
// (GRAD 0, 'GRAD'): each cell sends a copy of `model` with "ink name" (the
// set's), "ink index" and "ink" (B_RAW_TYPE: the ink's bytes, InkBytes).
// NULL if the set can't be read.
BMenu* InkSetMenu(uint32 type, const BMessage& model, const SetMgr* doc = NULL);

// the ink a FormatBar command chose: "ink" (an ink set's pattern or
// gradient, its bytes) or "color" (a color set's), as `type`'s property
bool InkFromMessage(BMessage* message, uint32_t type, Property& property);

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
