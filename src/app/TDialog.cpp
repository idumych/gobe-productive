#include "TDialog.h"

#include <Bitmap.h>

#include <stdio.h>
#include <stdlib.h>

#include <Box.h>
#include <Button.h>
#include <CheckBox.h>
#include <ColorControl.h>
#include <GroupLayout.h>
#include <GroupView.h>
#include <LayoutBuilder.h>
#include <ListView.h>
#include <MenuField.h>
#include <OutlineListView.h>
#include <PopUpMenu.h>
#include <RadioButton.h>
#include <ScrollView.h>
#include <Slider.h>
#include <SpaceLayoutItem.h>
#include <String.h>
#include <StringView.h>
#include <TextControl.h>
#include <TextView.h>

#include "GobeResources.h"
#include "TRibbon.h"
#include "TMenu.h"

namespace gobe {

static DialogData
LoadDialog(int32 id)
{
	DialogData d;
	const ResourceFile::Resource* r = GobeResources::File().Find(FourCC("DIAL"), id);
	if (r == NULL || !ParseDialog(r->data, d)) {
		fprintf(stderr, "TDialog: DIAL %d missing or malformed\n", (int)id);
		d.title = "Dialog";
		d.look = B_TITLED_WINDOW_LOOK;
		d.feel = B_NORMAL_WINDOW_FEEL;
	}
	return d;
}


bool
TDialog::Exists(int32 dialogId)
{
	return GobeResources::File().Find(FourCC("DIAL"), dialogId) != NULL;
}


TDialog::TDialog(int32 dialogId, int32 itemsId, uint32 command, BHandler* target)
	:
	BWindow(BRect(100, 100, 400, 300), LoadDialog(dialogId).title.c_str(),
		(window_look)LoadDialog(dialogId).look, (window_feel)LoadDialog(dialogId).feel,
		LoadDialog(dialogId).flags | B_AUTO_UPDATE_SIZE_LIMITS),
	fDialogId(dialogId),
	fCommand(command),
	fTarget(target)
{
	// a panel-coloured top view, like TDialogView in the original
	BGroupView* top = new BGroupView("TDialogView", B_VERTICAL, B_USE_DEFAULT_SPACING);
	top->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	BGroupLayout* root = top->GroupLayout();
	root->SetInsets(B_USE_WINDOW_INSETS);
	SetLayout(new BGroupLayout(B_VERTICAL, 0));
	GetLayout()->AddView(top);

	ItemList list;
	const ResourceFile::Resource* r = GobeResources::File().Find(FourCC("Item"), itemsId);
	if (r == NULL) {
		root->AddView(new BStringView("missing", "(no Item resource for this dialog)"));
	} else {
		if (!ParseItems(r->data, list))
			fprintf(stderr, "TDialog: Item %d did not parse completely\n", (int)itemsId);
		AddItems(root, list.items);
	}
	CenterOnScreen();
}


// An item's layout value is a BView resizing mode (B_FOLLOW_LEFT_TOP
// 0x1212, B_FOLLOW_RIGHT_BOTTOM 0x3434, ...). In a group (ItemGroup::
// Recalc) items following the far edge (the bottom of a column, the right
// of a row) are stacked from it, the first outermost: OK, Cancel come out
// bottom right as "Cancel OK". Those, and centred ones, are aligned so.
static bool
FollowsEnd(const ItemData& item, bool vertical)
{
	uint32 mode = item.layout[0];
	return vertical ? (mode & 0xf0f0) == 0x3030 : (mode & 0x0f0f) == 0x0404;
}


static void
AlignItem(BLayoutItem* layoutItem, const ItemData& item)
{
	uint32 mode = item.layout[0];
	alignment horizontal = B_ALIGN_USE_FULL_WIDTH;
	vertical_alignment vertical = B_ALIGN_USE_FULL_HEIGHT;
	if ((mode & 0x0f0f) == 0x0404)
		horizontal = B_ALIGN_RIGHT;
	else if ((mode & 0x0f0f) == 0x0505)
		horizontal = B_ALIGN_HORIZONTAL_CENTER;
	if ((mode & 0xf0f0) == 0x3030)
		vertical = B_ALIGN_BOTTOM;
	else if ((mode & 0xf0f0) == 0x5050)
		vertical = B_ALIGN_VERTICAL_CENTER;
	if (layoutItem != NULL && (horizontal != B_ALIGN_USE_FULL_WIDTH
			|| vertical != B_ALIGN_USE_FULL_HEIGHT))
		layoutItem->SetExplicitAlignment(BAlignment(horizontal, vertical));
}


void
TDialog::AddItems(BGroupLayout* layout, const std::vector<ItemData>& items, int32 group)
{
	bool vertical = layout->Orientation() == B_VERTICAL;
	std::vector<const ItemData*> order;
	for (const ItemData& item : items)
		if (!FollowsEnd(item, vertical))
			order.push_back(&item);
	for (auto i = items.rbegin(); i != items.rend(); i++)
		if (FollowsEnd(*i, vertical))
			order.push_back(&*i);

	for (const ItemData* entry : order) {
		const ItemData& item = *entry;
		if (item.type == FourCC("GRUP") || item.type == FourCC("FGRP")) {
			BGroupLayout* inner = new BGroupLayout(
				item.vertical ? B_VERTICAL : B_HORIZONTAL,
				item.spacing > 0 ? item.spacing : B_USE_SMALL_SPACING);
			if (item.type == FourCC("FGRP"))
				inner->SetInsets(item.insets[0], item.insets[1], item.insets[0],
					item.insets[1]);
			layout->AddItem(inner);
			AlignItem(inner, item);
			AddItems(inner, item.children, group);
		} else if (item.type == FourCC("GBOX")) {
			BBox* box = new BBox("box");
			if (!item.label.empty())
				box->SetLabel(item.label.c_str());
			BGroupLayout* inner = new BGroupLayout(B_VERTICAL, B_USE_SMALL_SPACING);
			box->SetLayout(inner);
			inner->SetInsets(B_USE_SMALL_INSETS, B_USE_BIG_INSETS, B_USE_SMALL_INSETS,
				B_USE_SMALL_INSETS);
			AlignItem(layout->AddView(box), item);
			(fBuildingSet >= 0 ? fSetItems : fItems)[item.index] = box;
			AddItems(inner, item.children, item.index);
		} else {
			BView* view = MakeControl(item);
			if (view != NULL) {
				AlignItem(layout->AddView(view), item);
				(fBuildingSet >= 0 ? fSetItems : fItems)[item.index] = view;
				if (item.type == FourCC("RADI"))
					(fBuildingSet >= 0 ? fSetRadios : fRadios)[group].push_back(item.index);
			}
		}
	}
}


BMessage*
TDialog::ControlMessage(const ItemData& item) const
{
	// an item set's controls: the set's command, their index as "SubID"
	int32 index = fBuildingSet >= 0 ? fBuildingSet : item.index;
	BMessage* message = new BMessage(fCommand + index);
	message->AddInt32("dialog", fDialogId);
	message->AddInt32("index", index);
	if (fBuildingSet >= 0)
		message->AddInt32("SubID", item.index);
	return message;
}


static alignment
Alignment(uint32 align)
{
	switch (align) {
		case 1: return B_ALIGN_RIGHT;
		case 2: return B_ALIGN_CENTER;
		default: return B_ALIGN_LEFT;
	}
}


BView*
TDialog::MakeControl(const ItemData& item)
{
	const char* label = item.label.c_str();
	BView* view = NULL;

	switch (item.type) {
		case FourCC("BUTT"):
		{
			BButton* button = new BButton("button", label, ControlMessage(item));
			button->SetEnabled(item.enabled);
			if (item.isDefault)
				SetDefaultButton(button);
			view = button;
			break;
		}
		case FourCC("CHEC"):
		case FourCC("TCHK"):
		{
			BCheckBox* box = new BCheckBox("check", label, ControlMessage(item));
			box->SetEnabled(item.enabled);
			view = box;
			break;
		}
		case FourCC("RADI"):
		{
			BRadioButton* radio = new BRadioButton("radio", label, ControlMessage(item));
			radio->SetEnabled(item.enabled);
			view = radio;
			break;
		}
		case FourCC("STAT"):
		case FourCC("SPRf"):
		{
			// SPRf is a printf-style label filled in by the dialog's code
			BStringView* text = new BStringView("label", label);
			text->SetAlignment(Alignment(item.align));
			if (item.bold)
				text->SetFont(be_bold_font);
			view = text;
			break;
		}
		case FourCC("EDIT"):
		{
			BTextControl* edit = new BTextControl("edit", label, item.text.c_str(),
				ControlMessage(item));
			view = edit;
			break;
		}
		case FourCC("TEXT"):
		{
			BTextView* text = new BTextView("text");
			text->MakeSelectable(item.values.size() > 2 && item.values[2] != 0);
			text->MakeEditable(item.values.size() > 3 && item.values[3] != 0);
			text->SetAlignment(Alignment(item.align));
			text->SetText(label);
			view = new BScrollView("text scroll", text, 0, false, true);
			break;
		}
		case FourCC("POPM"):
		{
			BPopUpMenu* popup = new BPopUpMenu(label);
			if (item.menu == 0xb) {
				popup->AddItem(new BMenuItem("(font list)", NULL));
			} else if (item.menu == 0x79) {
				popup->AddItem(new BMenuItem("(arrows)", NULL));
			} else if (GobeResources::File().Find(FourCC("MENU"), item.menu) != NULL) {
				// TPopUpMenu(id, ...): the MENU's items; their messages are
				// rewritten to the control's command like the original
				TMenu source(item.menu, true);
				while (source.CountItems() > 0) {
					BMenuItem* mi = source.RemoveItem((int32)0);
					if (mi->Message() != NULL) {
						mi->Message()->what = fCommand + item.index;
						// an item set's: to the dialog with the set and
						// "SubID"
						if (fBuildingSet >= 0) {
							mi->Message()->what = fCommand + fBuildingSet;
							mi->Message()->AddInt32("dialog", fDialogId);
							mi->Message()->AddInt32("index", fBuildingSet);
							mi->Message()->AddInt32("SubID", item.index);
						}
					}
					popup->AddItem(mi);
				}
			}
			popup->SetLabelFromMarked(true);
			if (popup->CountItems() > 0 && popup->ItemAt(0) != NULL)
				popup->ItemAt(0)->SetMarked(true);
			BMenuField* field = new BMenuField("popup", label, popup);
			field->SetEnabled(item.enabled);
			view = field;
			break;
		}
		case FourCC("LIST"):
		case FourCC("OLST"):
		case FourCC("CLST"):
		{
			list_view_type type = item.values.size() > 4 && item.values[4] == 1
				? B_MULTIPLE_SELECTION_LIST : B_SINGLE_SELECTION_LIST;
			BListView* list = item.type == FourCC("OLST")
				? new BOutlineListView("list", type) : new BListView("list", type);
			list->SetInvocationMessage(ControlMessage(item));
			bool h = item.values.size() > 2 && item.values[2] != 0;
			bool v = item.values.size() > 3 && item.values[3] != 0;
			view = new BScrollView("list scroll", list, 0, h, v || !h);
			// LIST params: rows (17 each) and a width in digits
			// (ListEngineer::Recalc: that many "0"s, 10 more and the scroll
			// bar's 15; 0: wide enough for a name)
			if (item.values.size() >= 2 && item.values[0] > 0) {
				float width = 170;
				if (item.values[1] > 0)
					width = item.values[1] * be_plain_font->StringWidth("0") + 10 + (v ? 15 : 0);
				BSize size(width, item.values[0] * 17 + 4);
				view->SetExplicitMinSize(size);
				view->SetExplicitPreferredSize(size);
			}
			break;
		}
		case FourCC("BSLI"):
		{
			int32 minimum = item.values.size() > 0 ? item.values[0] : 0;
			int32 maximum = item.values.size() > 1 ? item.values[1] : 100;
			if (maximum <= minimum)
				maximum = minimum + 100;
			BSlider* slider = new BSlider("slider", label, ControlMessage(item),
				minimum, maximum, B_HORIZONTAL);
			if (item.strings.size() >= 2)
				slider->SetLimitLabels(item.strings[0].c_str(), item.strings[1].c_str());
			slider->SetEnabled(item.enabled);
			view = slider;
			break;
		}
		case FourCC("BCOL"):
		{
			color_control_layout layout = B_CELLS_32x8;
			if (!item.values.empty()) {
				switch (item.values[0]) {
					case 4: layout = B_CELLS_4x64; break;
					case 8: layout = B_CELLS_8x32; break;
					case 16: layout = B_CELLS_16x16; break;
					case 64: layout = B_CELLS_64x4; break;
				}
			}
			float cell = item.values.size() > 1 && item.values[1] > 0 ? item.values[1] : 8;
			view = new BColorControl(B_ORIGIN, layout, cell, "color", ControlMessage(item));
			break;
		}
		case FourCC("USER"):
		{
			// TUserItemEngineer: a view of the given size the dialog draws in
			BView* user = new BView("user", B_WILL_DRAW);
			if (item.values.size() >= 2) {
				BSize size(item.values[0], item.values[1]);
				user->SetExplicitMinSize(size);
				user->SetExplicitPreferredSize(size);
				user->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, size.height));
			}
			user->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
			view = user;
			break;
		}
		case FourCC("ICON"):
		{
			// a 16 x 16 icon (MICN)
			BBitmap* icon = item.values.size() >= 2 && uint32(item.values[0]) == FourCC("MICN")
				? TMICNElement::LoadIcon(item.values[1]) : NULL;
			class IconView : public BView {
			public:
				IconView(BBitmap* icon)
					: BView("icon", B_WILL_DRAW), fIcon(icon)
				{
					SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
					SetExplicitMinSize(BSize(15, 15));
					SetExplicitMaxSize(BSize(15, 15));
				}
				~IconView() { delete fIcon; }
				virtual void Draw(BRect)
				{
					if (fIcon == NULL)
						return;
					SetDrawingMode(B_OP_OVER);
					DrawBitmap(fIcon, BPoint(0, 0));
				}
			private:
				BBitmap* fIcon;
			};
			view = new IconView(icon);
			break;
		}
		case FourCC("ISET"):
		{
			// TItemSet: the controls LoadItemSet puts in it
			BGroupView* set = new BGroupView("itemset", B_VERTICAL, B_USE_SMALL_SPACING);
			set->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
			view = set;
			break;
		}
		default:
		{
			// IBUT, ICON, 256P, PIKR, CUST: Gobe-specific views, not ported
			// yet
			BString text;
			text << "[" << FourCCString(item.type).c_str() << "]";
			if (!item.label.empty())
				text << " " << label;
			view = new BStringView("placeholder", text.String());
			break;
		}
	}
	return view;
}


void
TDialog::MessageReceived(BMessage* message)
{
	if (message->HasInt32("dialog")) {
		int32 index = message->GetInt32("index", -1);
		BString code = FourCCString(message->what).c_str();
		printf("dialog %d: control %d sent '%s'\n", (int)fDialogId, (int)index,
			code.String());
		ItemHit(index, message);
		return;
	}
	BWindow::MessageReceived(message);
}


void
TDialog::RunDialog()
{
	if (Lock()) {
		Init();
		// as large as the items want (the window isn't resizable); Init may
		// have changed sizes inside nested group layouts
		ResizeToPreferred();
		CenterOnScreen();
		Unlock();
	}
	Show();
}


bool
TDialog::ItemHit(int32 index, BMessage* message)
{
	// OK / Cancel are the first two controls in Gobe's dialogs
	if (index <= 1)
		PostMessage(B_QUIT_REQUESTED);
	return false;
}


BView*
TDialog::Item(int32 index, int32 set) const
{
	const std::map<int32, BView*>& items = set >= 0 ? fSetItems : fItems;
	auto it = items.find(index);
	return it == items.end() ? NULL : it->second;
}


void
TDialog::SetTextItem(int32 index, const char* text, int32 set)
{
	if (BTextControl* control = dynamic_cast<BTextControl*>(Item(index, set)))
		control->SetText(text);
	else if (BStringView* label = dynamic_cast<BStringView*>(Item(index, set)))
		label->SetText(text);
}


BString
TDialog::TextItem(int32 index, int32 set) const
{
	if (BTextControl* control = dynamic_cast<BTextControl*>(Item(index, set)))
		return control->Text();
	return BString();
}


void
TDialog::SetCheckBoxItem(int32 index, bool on, int32 set)
{
	if (BCheckBox* box = dynamic_cast<BCheckBox*>(Item(index, set)))
		box->SetValue(on ? B_CONTROL_ON : B_CONTROL_OFF);
}


bool
TDialog::CheckBoxItem(int32 index, int32 set) const
{
	BCheckBox* box = dynamic_cast<BCheckBox*>(Item(index, set));
	return box != NULL && box->Value() == B_CONTROL_ON;
}


void
TDialog::MakeFocusItem(int32 index, int32 set)
{
	if (BTextControl* control = dynamic_cast<BTextControl*>(Item(index, set)))
		control->MakeFocus(true);
}

void
TDialog::SetLabelItem(int32 index, const char* label, int32 set)
{
	if (BControl* control = dynamic_cast<BControl*>(Item(index, set)))
		control->SetLabel(label);
}


void
TDialog::SetRadioItem(int32 index, bool on, int32 set)
{
	if (BRadioButton* radio = dynamic_cast<BRadioButton*>(Item(index, set)))
		radio->SetValue(on ? B_CONTROL_ON : B_CONTROL_OFF);
}


int32
TDialog::RadioValue(int32 group, int32 set) const
{
	const std::map<int32, std::vector<int32>>& sets = set >= 0 ? fSetRadios : fRadios;
	auto radios = sets.find(group);
	if (radios == sets.end())
		return -1;
	for (size_t i = 0; i < radios->second.size(); i++) {
		BRadioButton* radio = dynamic_cast<BRadioButton*>(Item(radios->second[i], set));
		if (radio != NULL && radio->Value() == B_CONTROL_ON)
			return int32(i);
	}
	return -1;
}

static BMenu*
PopUp(BView* view)
{
	BMenuField* field = dynamic_cast<BMenuField*>(view);
	return field != NULL ? field->Menu() : NULL;
}


void
TDialog::SetPopUpItem(int32 index, int32 value, int32 set)
{
	BMenu* menu = PopUp(Item(index, set));
	if (menu == NULL)
		return;
	for (int32 i = 0; i < menu->CountItems(); i++) {
		BMenuItem* item = menu->ItemAt(i);
		int32 v;
		if (item->Message() != NULL && item->Message()->FindInt32(kValueString, &v) == B_OK
				&& v == value) {
			item->SetMarked(true);
			return;
		}
	}
}


int32
TDialog::PopUpValue(int32 index, int32 set) const
{
	BMenu* menu = PopUp(Item(index, set));
	BMenuItem* marked = menu != NULL ? menu->FindMarked() : NULL;
	int32 v;
	if (marked == NULL || marked->Message() == NULL
			|| marked->Message()->FindInt32(kValueString, &v) != B_OK)
		return -1;
	return v;
}


void
TDialog::RemoveLastPopUpItems(int32 index, int32 count, int32 set)
{
	BMenu* menu = PopUp(Item(index, set));
	for (int32 i = 0; menu != NULL && i < count && menu->CountItems() > 0; i++)
		delete menu->RemoveItem(menu->CountItems() - 1);
}

void
TDialog::SetItemEnabled(int32 index, bool on, int32 set)
{
	if (BControl* control = dynamic_cast<BControl*>(Item(index, set)))
		control->SetEnabled(on);
	else if (BMenuField* field = dynamic_cast<BMenuField*>(Item(index, set)))
		field->SetEnabled(on);
}


void
TDialog::LoadItemSet(int32 index, int32 itemsId)
{
	BGroupView* set = dynamic_cast<BGroupView*>(Item(index));
	if (set == NULL)
		return;
	while (BView* child = set->ChildAt(0)) {
		child->RemoveSelf();
		delete child;
	}
	// (and what isn't a view: glue, groups)
	while (BLayoutItem* item = set->GroupLayout()->RemoveItem(int32(0)))
		delete item;
	fSetItems.clear();
	fSetRadios.clear();
	ItemList list;
	const ResourceFile::Resource* r = GobeResources::File().Find(FourCC("Item"), itemsId);
	if (r == NULL || !ParseItems(r->data, list))
		return;
	fBuildingSet = index;
	AddItems(set->GroupLayout(), list.items);
	fBuildingSet = -1;
	// the controls at the top of the set's room
	set->GroupLayout()->AddItem(BSpaceLayoutItem::CreateGlue());
}

} // namespace gobe
