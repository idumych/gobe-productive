#include "TDialog.h"

#include <Screen.h>

#include <Bitmap.h>

#include <algorithm>

#include <stdio.h>
#include <stdlib.h>

#include <Alert.h>
#include <Box.h>
#include <Button.h>
#include <CheckBox.h>
#include <ColorControl.h>
#include <Control.h>
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
#include <cmath>
#include <ControlLook.h>
#include <TextView.h>

#include "GobeResources.h"
#include "TIconButton.h"
#include "TIconStringItem.h"
#include "TRibbon.h"
#include "TMenu.h"

namespace gobe {

static DialogData
LoadDialog(int32 id, const ResourceFile* resources)
{
	DialogData d;
	const ResourceFile::Resource* r = (resources != NULL ? *resources : GobeResources::File())
		.Find(FourCC("DIAL"), id);
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


TDialog::TDialog(int32 dialogId, int32 itemsId, uint32 command, BHandler* target,
		const ResourceFile* resources)
	:
	BWindow(BRect(100, 100, 400, 300), LoadDialog(dialogId, resources).title.c_str(),
		(window_look)LoadDialog(dialogId, resources).look,
		(window_feel)LoadDialog(dialogId, resources).feel,
		LoadDialog(dialogId, resources).flags | B_AUTO_UPDATE_SIZE_LIMITS),
	fDialogId(dialogId),
	fResources(resources != NULL ? resources : &GobeResources::File()),
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
	const ResourceFile::Resource* r = fResources->Find(FourCC("Item"), itemsId);
	if (r == NULL) {
		root->AddView(new BStringView("missing", "(no Item resource for this dialog)"));
	} else {
		if (!ParseItems(r->data, list))
			fprintf(stderr, "TDialog: Item %d did not parse completely\n", (int)itemsId);
		AddItems(root, list.items);
	}
	OurCenterWindow();
}


// OurCenterWindow (TDialog::SetUpWindow): halfway across the screen, a
// third of the way down the room it leaves
void
TDialog::OurCenterWindow()
{
	BRect screen = BScreen(this).Frame();
	BRect bounds = Bounds();
	MoveTo((screen.Width() - bounds.Width()) * 0.5f, (screen.Height() - bounds.Height()) / 3);
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
			// (ItemGroup::Recalc: an item at the last one's right or bottom
			// edge plus its spacing; a group of icon buttons, theirs, 0:
			// one's edge on the last one's)
			float spacing = item.spacing > 0 ? item.spacing : B_USE_SMALL_SPACING;
			bool iconButtons = item.spacing == 0 && !item.children.empty();
			for (const ItemData& child : item.children)
				iconButtons &= child.type == FourCC("IBUT") && child.values.size() > 2;
			if (iconButtons)
				spacing = item.children[0].values[2] - 1;
			BGroupLayout* inner = new BGroupLayout(
				item.vertical ? B_VERTICAL : B_HORIZONTAL, spacing);
			// (a fixed group: ItemGroup's frame starts that big, and grows
			// to hold its items)
			if (item.type == FourCC("FGRP"))
				inner->SetExplicitMinSize(BSize(item.insets[0] > 0 ? item.insets[0] : B_SIZE_UNSET,
					item.insets[1] > 0 ? item.insets[1] : B_SIZE_UNSET));
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


// TSliderControl: a slider that shows its value above its thumb in its
// format ("%d%%"), sprintf'd
class TSliderControl : public BSlider {
public:
	TSliderControl(const char* label, BMessage* message, int32 minimum, int32 maximum,
			const char* format)
		:
		BSlider("slider", label, message, minimum, maximum, B_HORIZONTAL),
		fFormat(format)
	{
	}

	virtual void DrawThumb()
	{
		BSlider::DrawThumb();
		if (fFormat.IsEmpty())
			return;
		BView* view = OffscreenView();
		char text[256];
		snprintf(text, sizeof(text), fFormat.String(), int(Value()));
		view->SetFont(be_plain_font);
		view->SetHighColor(0, 0, 0);
		view->SetLowColor(ViewColor());
		float width = view->StringWidth(text);
		BRect bounds = Bounds(), thumb = ThumbFrame();
		float x = (thumb.left + thumb.right) / 2 - width / 2;
		x = std::max(bounds.left, std::min(x, bounds.right - width));
		view->SetDrawingMode(B_OP_OVER);
		// (its baseline two above the bar)
		view->DrawString(text, BPoint(x, BarFrame().top - 2));
	}

private:
	BString fFormat;
};


// TextEngineer: its label as wide as `labelDigits` "0"s (its own width
// with 0), its text as wide as `textDigits` (its text's with 0), not
// stretched
class TEditControl : public BTextControl {
public:
	TEditControl(const char* label, const char* text, BMessage* message, int32 labelDigits,
			int32 textDigits)
		:
		BTextControl("edit", label, text, message)
	{
		float zero = TextView()->StringWidth("0");
		fLabelWidth = label == NULL || label[0] == '\0' ? 0
			: labelDigits != 0 ? labelDigits * zero : ceilf(StringWidth(label));
		fTextWidth = textDigits != 0 ? textDigits * zero : TextView()->StringWidth(text);
		fTextWidth = std::max(fTextWidth, zero);
		SetExplicitAlignment(BAlignment(B_ALIGN_LEFT, B_ALIGN_VERTICAL_CENTER));
	}

	virtual BSize MinSize()
	{
		BSize size = BTextControl::MinSize();
		size.width = Divider() + fTextWidth + 2 * kFrameMargin + 2 * kTextInset;
		return size;
	}

	virtual BSize MaxSize() { return MinSize(); }
	virtual BSize PreferredSize() { return MinSize(); }

	virtual void DoLayout()
	{
		BTextControl::DoLayout();
		// (the text after the label's room, not right after the label)
		BRect frame(Divider(), 0, Bounds().right, Bounds().bottom);
		frame.InsetBy(kFrameMargin, kFrameMargin);
		TextView()->MoveTo(frame.LeftTop());
		TextView()->ResizeTo(frame.Width(), frame.Height());
		TextView()->SetTextRect(frame.OffsetToCopy(B_ORIGIN));
	}

private:
	float Divider() const
	{
		return fLabelWidth > 0 ? fLabelWidth + be_control_look->DefaultLabelSpacing() : 0;
	}

	static const int32 kFrameMargin = 2;	// BTextControl's
	static const int32 kTextInset = 3;		// its text view's
	float fLabelWidth;
	float fTextWidth;
};


// TPopUpMenuEngineer: as wide as its label and items, not stretched
class TPopUpField : public BMenuField {
public:
	TPopUpField(const char* label, BMenu* menu)
		:
		BMenuField("popup", label, menu)
	{
		SetExplicitAlignment(BAlignment(B_ALIGN_LEFT, B_ALIGN_VERTICAL_CENTER));
	}

	virtual BSize MaxSize()
	{
		BSize size = BMenuField::MaxSize();
		size.width = PreferredSize().width;
		return size;
	}
};


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
			BTextControl* edit = new TEditControl(label, item.text.c_str(), ControlMessage(item),
				item.values.size() > 0 ? item.values[0] : 0,
				item.values.size() > 1 ? item.values[1] : 0);
			view = edit;
			break;
		}
		case FourCC("TEXT"):
		{
			// TextBoxEngineer: a text view on the dialog's gray, no scroll
			// bar, as wide as its params' "0"s and 2 more and as tall as
			// their lines and 10 more, its text 5 in
			BTextView* text = new BTextView("text");
			text->MakeSelectable(item.values.size() > 2 && item.values[2] != 0);
			text->MakeEditable(item.values.size() > 3 && item.values[3] != 0);
			text->SetAlignment(Alignment(item.align));
			text->SetText(label);
			text->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
			text->SetInsets(5, 5, 5, 5);
			text->SetWordWrap(true);
			if (item.values.size() >= 2 && item.values[0] > 0 && item.values[1] > 0) {
				font_height height;
				be_plain_font->GetHeight(&height);
				BSize size(item.values[0] * be_plain_font->StringWidth("0") + 2,
					(height.ascent + height.descent + height.leading) * item.values[1] + 10);
				text->SetExplicitMinSize(size);
				text->SetExplicitPreferredSize(size);
				text->SetExplicitMaxSize(size);
			}
			view = text;
			break;
		}
		case FourCC("POPM"):
		{
			BPopUpMenu* popup = new BPopUpMenu(label);
			if (item.menu == 0xb) {
				popup->AddItem(new BMenuItem("(font list)", NULL));
			} else if (item.menu == 0x79) {
				popup->AddItem(new BMenuItem("(arrows)", NULL));
			} else if (fResources->Find(FourCC("MENU"), item.menu) != NULL) {
				// TPopUpMenu(id, ...): the MENU's items; their messages are
				// rewritten to the control's command like the original
				TMenu source(item.menu, *fResources, true);
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
			BMenuField* field = new TPopUpField(label, popup);
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
				else
					fNameWidthLists.push_back(view);
				BSize size(width, item.values[0] * 17 + 4);
				view->SetExplicitMinSize(size);
				view->SetExplicitPreferredSize(size);
			}
			break;
		}
		case FourCC("BSLI"):
		{
			// TSliderEngineer: its width, minimum, maximum and hash marks;
			// the limit labels
			int32 minimum = item.values.size() > 1 ? item.values[1] : 0;
			int32 maximum = item.values.size() > 2 ? item.values[2] : 100;
			if (maximum <= minimum)
				maximum = minimum + 100;
			BSlider* slider = new TSliderControl(label, ControlMessage(item), minimum, maximum,
				item.strings.size() > 2 ? item.strings[2].c_str() : "");
			if (item.values.size() > 0 && item.values[0] > 0)
				slider->SetExplicitMinSize(BSize(item.values[0], B_SIZE_UNSET));
			if (item.values.size() > 3 && item.values[3] > 0) {
				slider->SetHashMarks(B_HASH_MARKS_BOTH);
				slider->SetHashMarkCount(item.values[3]);
			}
			// (both, or neither)
			if (item.strings.size() >= 2 && item.strings[0].size() > 0
					&& item.strings[1].size() > 0)
				slider->SetLimitLabels(item.strings[0].c_str(), item.strings[1].c_str());
			slider->SetBarColor(make_color(0xa0, 0xa0, 0xa0));
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
		case FourCC("IBUT"):
		{
			// TIconButton with a MICN (16 x 16): a button that sends its
			// message when pressed and again while held (its still-down
			// message)
			if (item.values.size() >= 2 && uint32(item.values[0]) == FourCC("MICN")) {
				class RepeatButton : public BControl {
				public:
					RepeatButton(BBitmap* icon, BMessage* message)
						: BControl("iconbutton", NULL, message, B_WILL_DRAW),
						fIcon(icon)
					{
						SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
						SetExplicitMinSize(BSize(19, 19));
						SetExplicitMaxSize(BSize(19, 19));
					}
					~RepeatButton() { delete fIcon; }
					virtual void Draw(BRect)
					{
						BRect r = Bounds();
						bool down = Value() == B_CONTROL_ON;
						rgb_color panel = ui_color(B_PANEL_BACKGROUND_COLOR);
						SetHighColor(down ? tint_color(panel, B_DARKEN_2_TINT) : panel);
						FillRect(r);
						SetHighColor(tint_color(panel, down ? B_DARKEN_4_TINT
							: B_LIGHTEN_MAX_TINT));
						StrokeLine(r.LeftBottom(), r.LeftTop());
						StrokeLine(r.RightTop());
						SetHighColor(tint_color(panel, down ? B_LIGHTEN_MAX_TINT
							: B_DARKEN_3_TINT));
						StrokeLine(r.RightBottom());
						StrokeLine(r.LeftBottom());
						if (fIcon == NULL)
							return;
						SetDrawingMode(B_OP_OVER);
						DrawBitmap(fIcon, BPoint(2 + (down ? 1 : 0), 2 + (down ? 1 : 0)));
						SetDrawingMode(B_OP_COPY);
					}
					virtual void MouseDown(BPoint)
					{
						if (!IsEnabled())
							return;
						SetValue(B_CONTROL_ON);
						Invoke();
						// again every tenth of a second after a third, while
						// the button is held over it
						bigtime_t next = system_time() + 300000;
						BPoint where;
						uint32 buttons;
						do {
							snooze(20000);
							GetMouse(&where, &buttons, false);
							if (buttons != 0 && Bounds().Contains(where)
									&& system_time() >= next) {
								Invoke();
								next = system_time() + 100000;
							}
						} while (buttons != 0);
						SetValue(B_CONTROL_OFF);
					}
				private:
					BBitmap* fIcon;
				};
				view = new RepeatButton(TMICNElement::LoadIcon(item.values[1], &Resources()),
					ControlMessage(item));
				break;
			}
			// TIconButton: a 32 x 32 icon (ICON), its id, spacing,
			// selection set and whether it's shown (TIconButtonEngineer)
			TIconButton* button = new TIconButton(ControlMessage(item),
				item.values.size() > 1 ? item.values[1] : 0,
				item.values.size() > 3 ? item.values[3] : 0,
				item.values.size() <= 4 || item.values[4] != 0, &Resources());
			button->SetEnabled(item.enabled);
			view = button;
			break;
		}
		default:
		{
			// ICON other than MICN, 256P, PIKR, CUST: Gobe-specific views, not ported
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
TDialog::DispatchMessage(BMessage* message, BHandler* target)
{
	const char* bytes;
	int32 modifiers;
	bool floater = Feel() == B_FLOATING_SUBSET_WINDOW_FEEL
		|| Feel() == B_FLOATING_APP_WINDOW_FEEL || Feel() == B_FLOATING_ALL_WINDOW_FEEL;
	if (message->what == B_KEY_DOWN && !floater
			&& message->FindString("bytes", &bytes) == B_OK && bytes[0] == B_ESCAPE
			&& message->FindInt32("modifiers", &modifiers) == B_OK
			&& (modifiers & (B_COMMAND_KEY | B_CONTROL_KEY | B_OPTION_KEY)) == 0) {
		if ((Flags() & B_NOT_CLOSABLE) == 0) {
			PostMessage(B_QUIT_REQUESTED);
			return;
		}
		// the Cancel button shown pressed for a moment, then invoked
		if (BButton* cancel = dynamic_cast<BButton*>(Item(1))) {
			cancel->SetValue(1);
			UpdateIfNeeded();
			snooze(100000);
			cancel->SetValue(0);
			UpdateIfNeeded();
			snooze(100000);
			cancel->Invoke();
			return;
		}
	}
	BWindow::DispatchMessage(message, target);
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


// every layout in `layout`, nested or in a view, forgets its sizes
static void
InvalidateLayouts(BLayout* layout)
{
	if (layout == NULL)
		return;
	layout->InvalidateLayout();
	for (int32 i = 0; BLayoutItem* item = layout->ItemAt(i); i++) {
		if (BLayout* nested = dynamic_cast<BLayout*>(item))
			InvalidateLayouts(nested);
		else if (item->View() != NULL)
			InvalidateLayouts(item->View()->GetLayout());
	}
}


void
TDialog::RunDialog()
{
	if (Lock()) {
		Init();
		// a list without a width in digits as wide as its widest item
		// (ListEngineer::Recalc: its text, 25 more by an icon; 10 at the
		// least), 10 more and the scroll bar's 15
		for (BView* view : fNameWidthLists) {
			BScrollView* scroll = dynamic_cast<BScrollView*>(view);
			BListView* list = scroll != NULL ? dynamic_cast<BListView*>(scroll->Target()) : NULL;
			if (list == NULL || list->IsEmpty())
				continue;
			float width = 10;
			// (an outline list's items after their latch and level's indent,
			// as BOutlineListView draws them)
			BOutlineListView* outline = dynamic_cast<BOutlineListView*>(list);
			int32 count = outline != NULL ? outline->FullListCountItems() : list->CountItems();
			for (int32 i = 0; i < count; i++) {
				BListItem* item = outline != NULL ? outline->FullListItemAt(i) : list->ItemAt(i);
				float indent = outline == NULL ? 0 : be_plain_font->Size()
					+ item->OutlineLevel() * be_control_look->DefaultItemSpacing()
					+ be_control_look->DefaultLabelSpacing();
				if (BStringItem* text = dynamic_cast<BStringItem*>(item))
					width = std::max(width, list->StringWidth(text->Text()) + indent);
				else if (TIconStringItem* icon = dynamic_cast<TIconStringItem*>(item))
					width = std::max(width, list->StringWidth(icon->Name().c_str()) + 25 + indent);
			}
			width += 10 + (scroll->ScrollBar(B_VERTICAL) != NULL ? 15 : 0);
			BSize size(width, view->ExplicitPreferredSize().height);
			view->SetExplicitMinSize(size);
			view->SetExplicitPreferredSize(size);
		}
		// as large as the items want (the window isn't resizable); Init may
		// have changed sizes inside nested group layouts, which keep the
		// sizes they cached (a view's InvalidateLayout() doesn't reach them
		// before its own sizes were asked for)
		InvalidateLayouts(GetLayout());
		ResizeToPreferred();
		PlaceWindow();
		Unlock();
	}
	Show();
}


bool
TDialog::CheckTextItem(int32 index, int32* value, int32 low, int32 high, int32 set)
{
	// (TString::Num)
	BString text = TextItem(index, set);
	char* end;
	long number = strtol(text.String(), &end, 10);
	*value = int32(number);
	if (end != text.String() && number >= low && number <= high)
		return true;
	MakeFocusItem(index, set);
	BString alert;
	alert.SetToFormat(GobeResources::IndexString(6, 13).String(), int(low), int(high));
	(new BAlert("", alert.String(), "OK"))->Go(NULL);
	return false;
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
	else if (BTextView* view = dynamic_cast<BTextView*>(Item(index, set)))
		view->SetText(text);	// a TEXT item
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
	// (a check box, an icon button)
	if (BControl* box = dynamic_cast<BControl*>(Item(index, set)))
		box->SetValue(on ? B_CONTROL_ON : B_CONTROL_OFF);
}


bool
TDialog::CheckBoxItem(int32 index, int32 set) const
{
	BControl* box = dynamic_cast<BControl*>(Item(index, set));
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
TDialog::LoadItemSet(int32 index, int32 itemsId, const ResourceFile* file)
{
	BView* set = Item(index);
	if (set == NULL)
		return;
	bool user = dynamic_cast<BGroupView*>(set) == NULL;
	if (user && dynamic_cast<BGroupLayout*>(set->GetLayout()) == NULL) {
		set->SetLayout(new BGroupLayout(B_VERTICAL, 0));
		set->SetExplicitMinSize(BSize(B_SIZE_UNSET, B_SIZE_UNSET));
		set->SetExplicitPreferredSize(BSize(B_SIZE_UNSET, B_SIZE_UNSET));
		set->SetExplicitMaxSize(BSize(B_SIZE_UNSET, B_SIZE_UNSET));
	}
	BGroupLayout* layout = dynamic_cast<BGroupLayout*>(set->GetLayout());
	while (BView* child = set->ChildAt(0)) {
		child->RemoveSelf();
		delete child;
	}
	// (and what isn't a view: glue, groups)
	while (BLayoutItem* item = layout->RemoveItem(int32(0)))
		delete item;
	fSetItems.clear();
	fSetRadios.clear();
	ItemList list;
	const ResourceFile* own = fResources;
	if (file != NULL)
		fResources = file;
	const ResourceFile::Resource* r = fResources->Find(FourCC("Item"), itemsId);
	if (r == NULL || !ParseItems(r->data, list)) {
		fResources = own;
		return;
	}
	fBuildingSet = index;
	AddItems(layout, list.items);
	fBuildingSet = -1;
	fResources = own;
	// the controls at the top of the set's room
	if (!user)
		layout->AddItem(BSpaceLayoutItem::CreateGlue());
}

} // namespace gobe
