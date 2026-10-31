#include "TToolBar.h"

#include <MenuItem.h>
#include <PopUpMenu.h>

#include "Drawing.h"
#include "GobeResources.h"
#include "TPageView.h"

namespace gobe {

namespace {

// TheTools, ToolIcons and ToolTips (the original's data)
const struct {
	uint32 tool;
	int32 icon;
	int32 tip;
} kTools[] = {
	{kToolArrow, 0x65, 12}, {kToolPart, 0xca, 13}, {kToolRect, 0x66, 14},
	{kToolEllipse, 0x67, 15}, {kToolRoundRect, 0x68, 16}, {kToolArc, 0x69, 17},
	{kToolLine, 0x6a, 18}, {kToolFree, 0x6b, 19}, {kToolBezier, 0x6d, 20},
	{kToolPolygon, 0xfa, 21}, {kToolShapes, 0xfa, 22}
};

const uint32 kToolFamily = 'TOOL';
const float kWidth = 26;


// the part tool (TPartMenu behind its button): a pop-up of the part kinds
// a frame can be made of; the one chosen sends TDocWindow::SetTool's
// 'sM4Q' with Tool 'PART' and its Kind
class TPartToolElement : public TMICNButtonElement {
public:
	TPartToolElement(BPoint where, int32 micn)
		:
		TMICNButtonElement(kToolPart, where, micn, NULL)
	{
	}

	virtual void Invoke(TRibbon* ribbon)
	{
		static const struct {
			const char* name;
			uint32 kind;
		} kParts[] = {
			{"Word Processing", 'WORD'}, {"Spreadsheet", 'SPRD'}, {"Graphics", 'GRFX'},
			{"Table", 'TBLE'}, {"Image Processing", 'BITS'}
		};
		BPopUpMenu* menu = new BPopUpMenu("parts", false, false);
		for (const auto& p : kParts) {
			BMessage* message = new BMessage('sM4Q');
			message->AddInt32("Tool", int32(kToolPart));
			message->AddInt32("Kind", int32(p.kind));
			menu->AddItem(new BMenuItem(p.name, message));
		}
		BPoint at = ribbon->ConvertToScreen(BPoint(rect.right + 1, rect.top));
		BMenuItem* chosen = menu->Go(at, false, true, false);
		if (chosen != NULL && chosen->Message() != NULL)
			ribbon->Send(chosen->Message());
		delete menu;
	}
};

} // namespace


TToolBar::TToolBar(TPageView* view)
	:
	TRibbon("ToolBar"),
	fView(view)
{
	// TToolBar::LoadAllElements
	float y = 2;
	for (const auto& t : kTools) {
		TMICNButtonElement* button;
		if (t.tool == kToolPart)
			button = new TPartToolElement(BPoint(2, y), t.icon);
		else {
			BMessage* message = new BMessage(kMsgTool);
			message->AddInt32("tool", int32(t.tool));
			button = new TMICNButtonElement(t.tool, BPoint(2, y), t.icon, message);
		}
		button->family = kToolFamily;
		button->selectable = true;
		button->enabled = t.tool == kToolArrow || t.tool == kToolPart || ToolMakesShapes(t.tool);
		button->tip = GobeResources::IndexString(4, t.tip).String();
		AddElement(button);
		y += 27;
		if (t.tool == kToolPart)
			y += 6;
	}
	SelectElement(kToolArrow, kToolFamily);
}


BSize
TToolBar::MinSize()
{
	return BSize(kWidth, 100);
}


BSize
TToolBar::MaxSize()
{
	return BSize(kWidth, B_SIZE_UNLIMITED);
}


BSize
TToolBar::PreferredSize()
{
	return BSize(kWidth, 320);
}


void
TToolBar::Draw(BRect updateRect)
{
	TRibbon::Draw(updateRect);
	// (a column: the edge on the right instead of the bottom)
	BRect bounds = Bounds();
	SetHighColor(tint_color(ui_color(B_PANEL_BACKGROUND_COLOR), B_DARKEN_2_TINT));
	StrokeLine(BPoint(bounds.right, bounds.top), BPoint(bounds.right, bounds.bottom));
}


void
TToolBar::Update()
{
	if (fView != NULL)
		SelectElement(fView->Tool(), kToolFamily);
}

} // namespace gobe
