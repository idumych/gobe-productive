// The New Document dialog.
#include "TDialogNewDoc.h"

#include <algorithm>
#include <string>
#include <vector>

#include <Application.h>
#include <Bitmap.h>
#include <GroupLayout.h>
#include <ListItem.h>
#include <ListView.h>
#include <Path.h>
#include <ScrollView.h>

#include "GobeResources.h"
#include "ResourceFile.h"
#include "Styles.h"
#include "TDocView.h"

namespace gobe {

namespace {

// the kinds the port makes documents of
bool
CanMake(uint32 kind)
{
	return kind == 'WORD' || kind == 'SPRD' || kind == 'GRFX' || kind == 'PRES';
}


// TIconStringItem: a part's small icon (MICN, 16x16 in the BeOS palette)
// and its name
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


BBitmap*
SmallIcon(const ResourceFile& file, int32 id)
{
	const ResourceFile::Resource* r = file.Find('MICN', id);
	if (r == NULL || r->data.size() < 256)
		return NULL;
	BBitmap* icon = new BBitmap(BRect(0, 0, 15, 15), B_CMAP8);
	icon->SetBits(r->data.data(), 256, 0, B_CMAP8);
	return icon;
}


// the user item: the two words, centered, black over a gray shadow
class TNewDocTitle : public BView {
public:
	TNewDocTitle(BRect frame)
		:
		BView(frame, "title", B_FOLLOW_ALL, B_WILL_DRAW | B_FRAME_EVENTS)
	{
		SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	}

	virtual void FrameResized(float, float)
	{
		Invalidate();
	}

	virtual void Draw(BRect)
	{
		// Dutch801 Rm BT, 30 point (as the port maps Gobe's fonts)
		TextStyle style;
		style.family = "Dutch801 Rm BT";
		style.size = 30;
		BFont font = TDocView::MapFont(style);
		SetFont(&font);
		font_height height;
		font.GetHeight(&height);
		float ascent = roundf(height.ascent);
		float line = roundf(height.ascent + height.descent + height.leading);
		BRect b = Bounds();
		// "Gobe" over "Productive" (as the User's Guide pictures the dialog),
		// each centered
		BString words[2] = {GobeResources::IndexString(5, 19),
			GobeResources::IndexString(5, 20)};
		float y = ascent + (b.Height() - line * 2) / 2 - 3;
		for (const BString& word : words) {
			float x = (b.Width() - StringWidth(word.String())) / 2 - 3;
			SetHighColor(160, 160, 160);
			DrawString(word.String(), BPoint(x, y));
			SetHighColor(0, 0, 0);
			DrawString(word.String(), BPoint(x - 2, y - 2));
			y += line;
		}
	}
};

} // namespace


// the part factories that make documents: the main binary's and the
// add-ons' in Parts (PART: kind, version, makes documents, two flags,
// large icon id, small icon id, name; TPartFactory::TPartFactory), by name
// (NewDialogListItemsCompare), `select`'s selected, those `canMake` refuses
// disabled
void
FillPartKinds(BListView* list, uint32 select, bool (*canMake)(uint32 kind))
{
	std::vector<std::string> files = {GobeResources::Path().String()};
	BPath parts(GobeResources::Path().String());
	parts.GetParent(&parts);
	parts.Append("Parts");
	for (const char* name : {"Spreadsheet", "ImageProcessing", "Presentation", "Chart",
			"Table", "Clock"}) {
		BPath p(parts);
		p.Append(name);
		files.push_back(p.Path());
	}
	std::vector<TIconStringItem*> items;
	for (const std::string& path : files) {
		ResourceFile file;
		if (!file.Load(path))
			continue;
		for (const ResourceFile::Resource* r : file.All('PART')) {
			// (little-endian, the name NUL-terminated)
			const std::vector<uint8_t>& d = r->data;
			auto u32 = [&](size_t at) {
				return at + 4 <= d.size() ? uint32(d[at + 3]) << 24 | uint32(d[at + 2]) << 16
					| uint32(d[at + 1]) << 8 | d[at] : 0;
			};
			if (d.size() < 29 || u32(8) == 0)
				continue;
			uint32 kind = u32(0);
			std::string name;
			for (size_t i = 28; i < d.size() && d[i] != 0; i++)
				name += char(d[i]);
			TIconStringItem* item = new TIconStringItem(name, SmallIcon(file, int32(u32(24))),
				kind);
			item->SetHeight(16);
			item->SetEnabled(canMake(kind));
			items.push_back(item);
		}
	}
	// NewDialogListItemsCompare: by name
	std::sort(items.begin(), items.end(),
		[](TIconStringItem* a, TIconStringItem* b) { return a->Name() < b->Name(); });
	int32 selected = 0;
	for (TIconStringItem* item : items) {
		if (item->Kind() == select)
			selected = list->CountItems();
		list->AddItem(item);
	}
	list->Select(selected);
}


uint32
SelectedPartKind(BListView* list)
{
	TIconStringItem* item = list != NULL
		? dynamic_cast<TIconStringItem*>(list->ItemAt(list->CurrentSelection())) : NULL;
	return item != NULL && item->IsEnabled() ? item->Kind() : 0;
}


TDialogNewDoc::TDialogNewDoc(int32 documents)
	:
	TDialog(2, 2, 'APPL', NULL),
	fDocuments(documents)
{
}


void
TDialogNewDoc::Init()
{
	// "Gobe" "Productive" in the user item
	if (BView* user = Item(3)) {
		user->SetLayout(new BGroupLayout(B_VERTICAL, 0));
		user->GetLayout()->AddView(new TNewDocTitle(BRect()));
	}
	BScrollView* scroll = dynamic_cast<BScrollView*>(Item(4));
	BListView* list = scroll != NULL ? dynamic_cast<BListView*>(scroll->Target()) : NULL;
	if (list == NULL)
		return;
	FillPartKinds(list, 'WORD', CanMake);
	list->MakeFocus(true);
}


uint32
TDialogNewDoc::Selected() const
{
	BScrollView* scroll = dynamic_cast<BScrollView*>(Item(4));
	return SelectedPartKind(scroll != NULL ? dynamic_cast<BListView*>(scroll->Target()) : NULL);
}


bool
TDialogNewDoc::ItemHit(int32 index, BMessage* message)
{
	// TDialogNewDoc::onExit
	if (index == 4) {
		// a double click is OK
		index = 0;
	}
	if (index == 0) {
		uint32 kind = Selected();
		if (!CanMake(kind))
			return true;
		BMessage make('s001');
		make.AddInt32("kind", int32(kind));
		be_app->PostMessage(&make);
	} else if (index == 1) {
		if (fDocuments < 1)
			be_app->PostMessage(B_QUIT_REQUESTED);
	} else if (index == 2)
		be_app->PostMessage('sM01');
	else
		return TDialog::ItemHit(index, message);
	PostMessage(B_QUIT_REQUESTED);
	return true;
}

} // namespace gobe
