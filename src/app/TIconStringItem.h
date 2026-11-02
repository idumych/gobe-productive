// TIconStringItem: a list item with a small icon (MICN, 16x16 in the BeOS
// palette) and a name (the New dialog's parts, the Chart Type dialog's
// major types)
#ifndef GOBE_TICON_STRING_ITEM_H
#define GOBE_TICON_STRING_ITEM_H

#include <string>

#include <Bitmap.h>
#include <InterfaceDefs.h>
#include <ListItem.h>
#include <View.h>

namespace gobe {

class TIconStringItem : public BListItem {
public:
	TIconStringItem(const std::string& name, BBitmap* icon, uint32 kind)
		:
		fName(name),
		fIcon(icon),
		fKind(kind)
	{
	}

	virtual ~TIconStringItem()
	{
		delete fIcon;
	}

	virtual void DrawItem(BView* owner, BRect frame, bool complete)
	{
		rgb_color background = IsSelected() ? ui_color(B_LIST_SELECTED_BACKGROUND_COLOR)
			: ui_color(B_LIST_BACKGROUND_COLOR);
		owner->SetHighColor(background);
		owner->FillRect(frame);
		if (fIcon != NULL) {
			owner->SetDrawingMode(B_OP_OVER);
			owner->DrawBitmap(fIcon, BPoint(frame.left + 2, frame.top));
			owner->SetDrawingMode(B_OP_COPY);
		}
		owner->SetHighColor(IsEnabled() ? ui_color(IsSelected()
			? B_LIST_SELECTED_ITEM_TEXT_COLOR : B_LIST_ITEM_TEXT_COLOR)
			: tint_color(ui_color(B_LIST_ITEM_TEXT_COLOR), B_LIGHTEN_1_TINT));
		owner->SetLowColor(background);
		font_height height;
		owner->GetFontHeight(&height);
		owner->DrawString(fName.c_str(), BPoint(frame.left + 22,
			frame.top + (frame.Height() + height.ascent - height.descent) / 2));
	}

	const std::string& Name() const { return fName; }
	uint32 Kind() const { return fKind; }

private:
	std::string fName;
	BBitmap* fIcon;
	uint32 fKind;
};

} // namespace gobe

#endif
