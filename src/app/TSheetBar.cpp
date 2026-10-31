/*
 * The SheetBar: TSheetBar::LoadAllElements' TTabControl (colors 0xeeeeee
 * for the shown sheet's tab, 0xd8d8d8 for the others, 0xc8c8c8 lines), one
 * tab per sheet in the SheetMgr's order, after the options menu (MENU 110:
 * New Sheet… (spreadsheets), Delete Sheet, Rename Sheet…). A
 * double click on a tab renames it ('s00D').
 */
#include "TSheetBar.h"

#include <MenuItem.h>
#include <PopUpMenu.h>
#include <Region.h>
#include <Window.h>

#include "TMenu.h"

#include "Document.h"

namespace gobe {

static const float kHeight = 20;
static const float kPad = 8;


TSheetBar::TSheetBar(const Document& document)
	:
	BView("SheetBar", B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE),
	fDocument(document)
{
	SetViewColor(B_TRANSPARENT_COLOR);
	SetFont(be_plain_font);
}


BSize
TSheetBar::MinSize()
{
	return BSize(50, kHeight);
}


BSize
TSheetBar::MaxSize()
{
	return BSize(B_SIZE_UNLIMITED, kHeight);
}


BSize
TSheetBar::PreferredSize()
{
	return BSize(400, kHeight);
}


// the options menu's button
static BRect
OptionsFrame()
{
	return BRect(3, 3, 3 + 14, kHeight - 3);
}


BRect
TSheetBar::TabFrame(int32 index) const
{
	// the tabs from the first shown; those before it aren't
	if (index < fFirst)
		return BRect();
	float x = OptionsFrame().right + 6;
	for (int32 i = fFirst; i < int32(fDocument.SheetCount()); i++) {
		float width = StringWidth(fDocument.sheets.sheets[size_t(i)].name.c_str()) + 2 * kPad;
		if (i == index)
			return BRect(x, 2, x + width, kHeight - 2);
		x += width + 2;
	}
	return BRect();
}


bool
TSheetBar::Overflows() const
{
	int32 count = int32(fDocument.SheetCount());
	if (count == 0)
		return false;
	float x = OptionsFrame().right + 6;
	for (int32 i = 0; i < count; i++)
		x += StringWidth(fDocument.sheets.sheets[size_t(i)].name.c_str()) + 2 * kPad + 2;
	return x > Bounds().right - 2;
}


BRect
TSheetBar::ArrowFrame(bool right) const
{
	float edge = Bounds().right - 2;
	return right ? BRect(edge - 14, 3, edge, kHeight - 3)
		: BRect(edge - 30, 3, edge - 16, kHeight - 3);
}


void
TSheetBar::Draw(BRect updateRect)
{
	BRect bounds = Bounds();
	SetHighColor(216, 216, 216);
	FillRect(bounds);
	SetHighColor(200, 200, 200);
	StrokeLine(bounds.LeftTop(), bounds.RightTop());
	StrokeLine(bounds.LeftBottom(), bounds.RightBottom());
	// the options menu (MICN 0x24 in the original): a pop-up triangle
	BRect options = OptionsFrame();
	SetHighColor(120, 120, 120);
	FillTriangle(BPoint(options.left + 3, options.top + 5), BPoint(options.right - 3,
		options.top + 5), BPoint((options.left + options.right) / 2, options.bottom - 4));
	font_height fh;
	GetFontHeight(&fh);
	int32 shown = fDocument.CurrentSheet();
	bool arrows = Overflows() || fFirst > 0;
	if (arrows) {
		// (tabs under the arrows are clipped)
		BRegion clip(BRect(0, 0, ArrowFrame(false).left - 2, bounds.bottom));
		ConstrainClippingRegion(&clip);
	}
	for (int32 i = 0; i < int32(fDocument.SheetCount()); i++) {
		BRect tab = TabFrame(i);
		if (i == shown)
			SetHighColor(238, 238, 238);
		else
			SetHighColor(216, 216, 216);
		FillRect(tab);
		SetHighColor(200, 200, 200);
		StrokeRect(tab);
		if (i == fDropAt && fPressed >= 0 && fPressed != i) {
			// where a dragged tab would go
			SetHighColor(0, 0, 0);
			BRect mark = tab;
			if (fPressed > i)
				mark.right = mark.left + 1;
			else
				mark.left = mark.right - 1;
			FillRect(mark);
		}
		SetHighColor(0, 0, 0);
		SetLowColor(i == shown ? (rgb_color){238, 238, 238, 255} : (rgb_color){216, 216, 216, 255});
		DrawString(fDocument.sheets.sheets[size_t(i)].name.c_str(),
			BPoint(tab.left + kPad, (tab.top + tab.bottom + fh.ascent - fh.descent) / 2));
	}
	if (arrows) {
		ConstrainClippingRegion(NULL);
		for (bool right : {false, true}) {
			BRect r = ArrowFrame(right);
			SetHighColor(216, 216, 216);
			FillRect(r);
			SetHighColor(200, 200, 200);
			StrokeRect(r);
			bool on = right ? Overflows() : fFirst > 0;
			SetHighColor(on ? 80 : 170, on ? 80 : 170, on ? 80 : 170);
			float cy = (r.top + r.bottom) / 2;
			if (right)
				FillTriangle(BPoint(r.left + 4, cy - 4), BPoint(r.left + 4, cy + 4),
					BPoint(r.right - 4, cy));
			else
				FillTriangle(BPoint(r.right - 4, cy - 4), BPoint(r.right - 4, cy + 4),
					BPoint(r.left + 4, cy));
		}
	}
}


void
TSheetBar::MouseDown(BPoint where)
{
	if (OptionsFrame().InsetByCopy(-2, -2).Contains(where)) {
		// TSheetBar::FixOptionsMenu: Delete with more than one sheet
		TMenu items(110, true);
		BPopUpMenu menu("options", false, false);
		while (BMenuItem* item = items.RemoveItem(int32(0))) {
			uint32 what = item->Message() != NULL ? item->Message()->what : 0;
			item->SetEnabled(what == kMsgSheetRename || what == kMsgSheetNew
				|| (what == kMsgSheetDelete && fDocument.SheetCount() > 1));
			menu.AddItem(item);
		}
		BMenuItem* chosen = menu.Go(ConvertToScreen(OptionsFrame().LeftBottom()), false, true);
		if (chosen != NULL && chosen->Message() != NULL)
			Window()->PostMessage(chosen->Message());
		return;
	}
	if (Overflows() || fFirst > 0) {
		// the arrows scroll the tabs a tab at a time
		if (ArrowFrame(true).Contains(where)) {
			if (Overflows())
				fFirst++;
			Invalidate();
			return;
		}
		if (ArrowFrame(false).Contains(where)) {
			if (fFirst > 0)
				fFirst--;
			Invalidate();
			return;
		}
		if (where.x >= ArrowFrame(false).left - 2)
			return;
	}
	int32 clicks = 1;
	if (BMessage* current = Window()->CurrentMessage())
		current->FindInt32("clicks", &clicks);
	int32 tab = TabAt(where);
	if (tab < 0)
		return;
	BMessage message(clicks == 2 ? kMsgSheetRename : kMsgSheetSwitch);
	message.AddInt32("ActiveTab", tab);
	Window()->PostMessage(&message);
	// a tab dragged along moves its sheet
	fPressed = clicks == 2 ? -1 : tab;
	fDropAt = -1;
	SetMouseEventMask(B_POINTER_EVENTS, B_LOCK_WINDOW_FOCUS);
}


int32
TSheetBar::TabAt(BPoint where) const
{
	for (int32 i = 0; i < int32(fDocument.SheetCount()); i++)
		if (TabFrame(i).Contains(where))
			return i;
	return -1;
}


void
TSheetBar::MouseMoved(BPoint where, uint32 transit, const BMessage* dragged)
{
	if (fPressed < 0)
		return;
	int32 at = TabAt(where);
	if (at != fDropAt) {
		fDropAt = at;
		Invalidate();
	}
}


void
TSheetBar::MouseUp(BPoint where)
{
	int32 from = fPressed, to = TabAt(where);
	fPressed = fDropAt = -1;
	Invalidate();
	if (from < 0 || to < 0 || from == to)
		return;
	BMessage message(kMsgSheetReorder);
	message.AddInt32("OldIndex", from);
	message.AddInt32("NewIndex", to);
	Window()->PostMessage(&message);
}


void
TSheetBar::Update()
{
	// the shown sheet's tab in view
	int32 shown = fDocument.CurrentSheet();
	if (fFirst >= int32(fDocument.SheetCount()))
		fFirst = 0;
	if (shown < fFirst)
		fFirst = shown;
	while (fFirst < shown && TabFrame(shown).right > (Overflows() || fFirst > 0
			? ArrowFrame(false).left - 2 : Bounds().right))
		fFirst++;
	Invalidate();
}

} // namespace gobe
