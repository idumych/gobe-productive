#include "TInkMenus.h"

#include <algorithm>

#include "GobeResources.h"
#include "TRibbon.h"

namespace gobe {

ColorSet
LoadColorSet(int32 id)
{
	ColorSet set;
	const ResourceFile::Resource* r = GobeResources::File().Find('COLR', id);
	if (r == NULL)
		return set;
	const std::vector<uint8_t>& d = r->data;
	auto u32 = [&](size_t at) {
		return uint32(d[at + 3]) << 24 | uint32(d[at + 2]) << 16 | uint32(d[at + 1]) << 8 | d[at];
	};
	if (d.size() < 12)
		return set;
	set.columns = std::max<int32>(1, int32(u32(0)));
	set.cell = int32(u32(4));
	set.gap = int32(u32(8));
	for (size_t at = 12; at + 12 <= d.size(); at += 12)
		set.colors.push_back({uint8(u32(at)), uint8(u32(at + 4)), uint8(u32(at + 8)), 255});
	return set;
}


BMenu*
ColorSetMenu(const BMessage& model)
{
	ColorSet set = LoadColorSet(0);
	BMenu* palette = new BMenu("", B_ITEMS_IN_MATRIX);
	int32 step = set.cell + set.gap;
	for (size_t k = 0; k < set.colors.size(); k++) {
		BMessage* message = new BMessage(model);
		message->AddString("ink name", "Standard");
		message->AddData("color", B_RGB_COLOR_TYPE, &set.colors[k], 4);
		message->AddInt32("ink index", int32(k));
		float x = set.gap + (k % set.columns) * step;
		float y = set.gap + (k / set.columns) * step;
		palette->AddItem(new TInkMenuItem(set.colors[k], set.cell, message),
			BRect(x, y, x + set.cell - 1, y + set.cell - 1));
	}
	return palette;
}


TInkMenuItem::TInkMenuItem(rgb_color color, int32 cell, BMessage* message)
	:
	BMenuItem("", message),
	fColor(color),
	fCell(cell)
{
}


void
TInkMenuItem::DrawContent()
{
	BMenu* menu = Menu();
	BRect r = Frame();
	r.right = r.left + fCell - 1;
	r.bottom = r.top + fCell - 1;
	menu->SetHighColor(fColor);
	menu->FillRect(r);
	if (IsSelected())
		menu->SetHighColor(0, 0, 0);
	else
		menu->SetHighColor(0xa0, 0xa0, 0xa0);
	menu->StrokeRect(r);
	menu->SetHighColor(0, 0, 0);
}


void
TInkMenuItem::GetContentSize(float* width, float* height)
{
	*width = fCell;
	*height = fCell;
}


TIconMenuItem::TIconMenuItem(int32 micn, BMenu* menu, const ResourceFile* resources)
	:
	BMenuItem(menu),
	fIcon(TMICNElement::LoadIcon(micn, resources))
{
}


void
TIconMenuItem::DrawContent()
{
	if (fIcon == NULL)
		return;
	BMenu* menu = Menu();
	menu->SetDrawingMode(B_OP_OVER);
	menu->DrawBitmap(fIcon.get(), ContentLocation());
	menu->SetDrawingMode(B_OP_COPY);
}


void
TIconMenuItem::GetContentSize(float* width, float* height)
{
	*width = 16;
	*height = 16;
}

} // namespace gobe
