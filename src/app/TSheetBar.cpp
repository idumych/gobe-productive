/*
 * The SheetBar: TSheetBar::LoadAllElements' TTabControl (colors 0xeeeeee
 * for the shown sheet's tab, 0xd8d8d8 for the others, 0xc8c8c8 lines), one
 * tab per sheet in the SheetMgr's order, after the options menu (MENU 110:
 * New Sheet…, Delete Sheet, Rename Sheet…; only Rename is ported). A
 * double click on a tab renames it ('s00D').
 */
#include "TSheetBar.h"

#include <MenuItem.h>
#include <PopUpMenu.h>
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
	float x = OptionsFrame().right + 6;
	for (int32 i = 0; i < int32(fDocument.sheets.sheets.size()); i++) {
		float width = StringWidth(fDocument.sheets.sheets[size_t(i)].name.c_str()) + 2 * kPad;
		if (i == index)
			return BRect(x, 2, x + width, kHeight - 2);
		x += width + 2;
	}
	return BRect();
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
	for (int32 i = 0; i < int32(fDocument.sheets.sheets.size()); i++) {
		BRect tab = TabFrame(i);
		if (i == shown)
			SetHighColor(238, 238, 238);
		else
			SetHighColor(216, 216, 216);
		FillRect(tab);
		SetHighColor(200, 200, 200);
		StrokeRect(tab);
		SetHighColor(0, 0, 0);
		SetLowColor(i == shown ? (rgb_color){238, 238, 238, 255} : (rgb_color){216, 216, 216, 255});
		DrawString(fDocument.sheets.sheets[size_t(i)].name.c_str(),
			BPoint(tab.left + kPad, (tab.top + tab.bottom + fh.ascent - fh.descent) / 2));
	}
}


void
TSheetBar::MouseDown(BPoint where)
{
	if (OptionsFrame().InsetByCopy(-2, -2).Contains(where)) {
		// TSheetBar::FixOptionsMenu: Delete with more than one sheet; New
		// and Delete aren't ported
		TMenu items(110, true);
		BPopUpMenu menu("options", false, false);
		while (BMenuItem* item = items.RemoveItem(int32(0))) {
			uint32 what = item->Message() != NULL ? item->Message()->what : 0;
			item->SetEnabled(what == kMsgSheetRename);
			menu.AddItem(item);
		}
		BMenuItem* chosen = menu.Go(ConvertToScreen(OptionsFrame().LeftBottom()), false, true);
		if (chosen != NULL && chosen->Message() != NULL)
			Window()->PostMessage(chosen->Message());
		return;
	}
	int32 clicks = 1;
	if (BMessage* current = Window()->CurrentMessage())
		current->FindInt32("clicks", &clicks);
	for (int32 i = 0; i < int32(fDocument.sheets.sheets.size()); i++)
		if (TabFrame(i).Contains(where)) {
			BMessage message(clicks == 2 ? kMsgSheetRename : kMsgSheetSwitch);
			message.AddInt32("ActiveTab", i);
			Window()->PostMessage(&message);
			return;
		}
}


void
TSheetBar::Update()
{
	Invalidate();
}

} // namespace gobe
