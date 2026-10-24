#include "TDialog.h"

#include <stdio.h>

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
#include <String.h>
#include <StringView.h>
#include <TextControl.h>
#include <TextView.h>

#include "GobeResources.h"
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


void
TDialog::AddItems(BGroupLayout* layout, const std::vector<ItemData>& items, int32 group)
{
	for (const ItemData& item : items) {
		if (item.type == FourCC("GRUP") || item.type == FourCC("FGRP")) {
			BGroupLayout* inner = new BGroupLayout(
				item.vertical ? B_VERTICAL : B_HORIZONTAL,
				item.spacing > 0 ? item.spacing : B_USE_SMALL_SPACING);
			if (item.type == FourCC("FGRP"))
				inner->SetInsets(item.insets[0], item.insets[1], item.insets[0],
					item.insets[1]);
			layout->AddItem(inner);
			AddItems(inner, item.children, group);
		} else if (item.type == FourCC("GBOX")) {
			BBox* box = new BBox("box");
			if (!item.label.empty())
				box->SetLabel(item.label.c_str());
			BGroupLayout* inner = new BGroupLayout(B_VERTICAL, B_USE_SMALL_SPACING);
			box->SetLayout(inner);
			inner->SetInsets(B_USE_SMALL_INSETS, B_USE_BIG_INSETS, B_USE_SMALL_INSETS,
				B_USE_SMALL_INSETS);
			layout->AddView(box);
			fItems[item.index] = box;
			AddItems(inner, item.children, item.index);
		} else {
			BView* view = MakeControl(item);
			if (view != NULL) {
				layout->AddView(view);
				fItems[item.index] = view;
				if (item.type == FourCC("RADI"))
					fRadios[group].push_back(item.index);
			}
		}
	}
}


BMessage*
TDialog::ControlMessage(const ItemData& item) const
{
	BMessage* message = new BMessage(fCommand + item.index);
	message->AddInt32("dialog", fDialogId);
	message->AddInt32("index", item.index);
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
					if (mi->Message() != NULL)
						mi->Message()->what = fCommand + item.index;
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
		default:
		{
			// IBUT, ICON, 256P, PIKR, CUST, USER, ISET: Gobe-specific views,
			// not ported yet
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
TDialog::Item(int32 index) const
{
	auto it = fItems.find(index);
	return it == fItems.end() ? NULL : it->second;
}


void
TDialog::SetTextItem(int32 index, const char* text)
{
	if (BTextControl* control = dynamic_cast<BTextControl*>(Item(index)))
		control->SetText(text);
	else if (BStringView* label = dynamic_cast<BStringView*>(Item(index)))
		label->SetText(text);
}


BString
TDialog::TextItem(int32 index) const
{
	if (BTextControl* control = dynamic_cast<BTextControl*>(Item(index)))
		return control->Text();
	return BString();
}


void
TDialog::SetCheckBoxItem(int32 index, bool on)
{
	if (BCheckBox* box = dynamic_cast<BCheckBox*>(Item(index)))
		box->SetValue(on ? B_CONTROL_ON : B_CONTROL_OFF);
}


bool
TDialog::CheckBoxItem(int32 index) const
{
	BCheckBox* box = dynamic_cast<BCheckBox*>(Item(index));
	return box != NULL && box->Value() == B_CONTROL_ON;
}


void
TDialog::MakeFocusItem(int32 index)
{
	if (BTextControl* control = dynamic_cast<BTextControl*>(Item(index)))
		control->MakeFocus(true);
}

void
TDialog::SetLabelItem(int32 index, const char* label)
{
	if (BControl* control = dynamic_cast<BControl*>(Item(index)))
		control->SetLabel(label);
}


void
TDialog::SetRadioItem(int32 index, bool on)
{
	if (BRadioButton* radio = dynamic_cast<BRadioButton*>(Item(index)))
		radio->SetValue(on ? B_CONTROL_ON : B_CONTROL_OFF);
}


int32
TDialog::RadioValue(int32 group) const
{
	auto radios = fRadios.find(group);
	if (radios == fRadios.end())
		return -1;
	for (size_t i = 0; i < radios->second.size(); i++) {
		BRadioButton* radio = dynamic_cast<BRadioButton*>(Item(radios->second[i]));
		if (radio != NULL && radio->Value() == B_CONTROL_ON)
			return int32(i);
	}
	return -1;
}

} // namespace gobe
