#include "TPresentation.h"

#include <Application.h>
#include <Button.h>
#include <GroupLayout.h>
#include <CheckBox.h>
#include <MenuBar.h>
#include <Menu.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <OutlineListView.h>
#include <PopUpMenu.h>
#include <RadioButton.h>
#include <Bitmap.h>
#include <Screen.h>
#include <ScrollView.h>

#include <cmath>
#include <cstdio>

#include "Document.h"
#include "GobeResources.h"
#include "ResourceFile.h"
#include "TDocWindow.h"
#include "TMenu.h"
#include "TPageView.h"

namespace gobe {

namespace {

const ResourceFile&
Strings()
{
	return GobeResources::Part("Presentation");
}


// whether radio button `view` is on
bool
RadioOn(BView* view)
{
	BRadioButton* radio = dynamic_cast<BRadioButton*>(view);
	return radio != NULL && radio->Value() == B_CONTROL_ON;
}

} // namespace


// TPartPRFactory::TransitionAt; ElevatorTransition::DefaultOptions 3
const TransitionKind kTransitions[4] = {
	{"Jump", 0, 0, 0},
	{"Wipe", 'Wipe', 100, 0},
	{"Venetian Blinds", 'Vene', 101, 0},
	{"Elevator Doors", 'Elev', 102, 3}
};


int32
TransitionIndex(uint32 type)
{
	for (int32 i = 0; i < 4; i++)
		if (kTransitions[i].type == type)
			return i;
	return -1;
}


// #pragma mark - New Slide


TDialogPRNewSlide::TDialogPRNewSlide(BLooper* window, int32 selection, int32 count)
	:
	TDialog(11002, 11002, 'PRES', NULL, &Strings()),
	fWindow(window),
	fSelection(selection),
	fCount(count)
{
}


// TDialogPRNewSlide::Init: the name, At End of Presentation, both boxes on
void
TDialogPRNewSlide::Init()
{
	char name[256];
	snprintf(name, sizeof name, IndexString(Strings(), 11002, 0).c_str(), int(fCount + 1));
	SetTextItem(2, name);
	SetRadioItem(5, true);
	SetCheckBoxItem(6, true);
	SetCheckBoxItem(7, true);
	MakeFocusItem(2);
	FixControls();
}


// the background can't be shared at the first slide; OK needs a name and
// a layer (new or shared)
void
TDialogPRNewSlide::FixControls()
{
	bool atSelection = RadioOn(Item(4));
	bool canShare = atSelection ? fSelection != 0 : fCount != 0;
	SetItemEnabled(7, canShare);
	bool ok = TextItem(2).Length() > 0
		&& (CheckBoxItem(6) || (CheckBoxItem(7) && canShare));
	SetItemEnabled(0, ok);
}


bool
TDialogPRNewSlide::ItemHit(int32 index, BMessage* message)
{
	if (index == 0) {
		BMessage slide('PR04');
		slide.AddString("name", TextItem(2).String());
		bool atSelection = RadioOn(Item(4));
		slide.AddInt32("slide", atSelection ? fSelection : fCount);
		if (CheckBoxItem(6))
			slide.AddBool("new layer", true);
		if (CheckBoxItem(7) && (atSelection ? fSelection != 0 : fCount != 0))
			slide.AddBool("share background", true);
		fWindow->PostMessage(&slide);
		return TDialog::ItemHit(index, message);
	}
	if (index == 1)
		return TDialog::ItemHit(index, message);
	FixControls();
	return true;
}


// #pragma mark - Duplicate Slide


TDialogPRDupSlide::TDialogPRDupSlide(BLooper* window, int32 slide, const char* name)
	:
	TDialog(11003, 11003, 'PRES', NULL, &Strings()),
	fWindow(window),
	fSlide(slide),
	fName(name)
{
}


// TDialogPRDupSlide::Init: CopyName of the name, Use Same Layers with Add
// New Layer
void
TDialogPRDupSlide::Init()
{
	SetTextItem(2, CopyName(fName.String()).c_str());
	SetRadioItem(3, true);
	SetCheckBoxItem(5, true);
	MakeFocusItem(2);
	FixControls();
}


void
TDialogPRDupSlide::FixControls()
{
	bool duplicate = RadioOn(Item(6));
	SetItemEnabled(5, !duplicate);
	SetItemEnabled(8, duplicate);
	SetItemEnabled(0, TextItem(2).Length() > 0);
}


bool
TDialogPRDupSlide::ItemHit(int32 index, BMessage* message)
{
	if (index == 0) {
		BMessage dup('PR0A');
		dup.AddInt32("slide", fSlide);
		dup.AddInt32("layer", -1);
		dup.AddString("name", TextItem(2).String());
		bool duplicate = RadioOn(Item(6));
		dup.AddBool("duplicate layers", duplicate);
		dup.AddBool("new layer", !duplicate && CheckBoxItem(5));
		dup.AddBool("share background", duplicate && CheckBoxItem(8));
		fWindow->PostMessage(&dup);
		return TDialog::ItemHit(index, message);
	}
	if (index == 1)
		return TDialog::ItemHit(index, message);
	FixControls();
	return true;
}


// #pragma mark - Slide Transition


TDialogPRTransitions::TDialogPRTransitions(BLooper* window, int32 slide, uint32 type,
		uint32 options)
	:
	TDialog(11004, 11004, 'PRES', NULL, &Strings()),
	fWindow(window),
	fSlide(slide),
	fKind(std::max<int32>(0, TransitionIndex(type))),
	fOptions(options)
{
}


// TDialogPRTransitions::Init: the transitions in the pop-up, the slide's
// marked and its options shown
void
TDialogPRTransitions::Init()
{
	if (BMenuField* field = dynamic_cast<BMenuField*>(Item(2))) {
		BMenu* menu = field->Menu();
		while (menu->CountItems() > 0)
			delete menu->RemoveItem(int32(0));
		for (int32 i = 0; i < 4; i++) {
			BMessage* message = new BMessage('sD02');
			message->AddInt32(kValueString, i);
			menu->AddItem(new BMenuItem(kTransitions[i].name, message));
		}
		menu->SetTargetForItems(this);
		if (BMenuItem* item = menu->ItemAt(fKind))
			item->SetMarked(true);
	}
	LoadOptions(fKind, fOptions);
}


void
TDialogPRTransitions::LoadOptions(int32 kind, uint32 options)
{
	fKind = kind;
	LoadItemSet(4, kTransitions[kind].items);
	switch (kTransitions[kind].type) {
		case 'Wipe':
		case 'Vene':
			SetRadioItem(int32(options), true, 4);
			break;
		case 'Elev':
			// bit 0: Horizontal or Vertical; bit 1: Closing or Opening
			SetRadioItem(int32(options & 1), true, 4);
			SetRadioItem(2 + int32(options >> 1 & 1), true, 4);
			break;
	}
}


uint32
TDialogPRTransitions::Options() const
{
	switch (kTransitions[fKind].type) {
		case 'Wipe':
		case 'Vene':
			for (int32 i = 0; i < 4; i++)
				if (RadioOn(Item(i, 4)))
					return uint32(i);
			return 0;
		case 'Elev':
			return (RadioOn(Item(1, 4)) ? 1 : 0) | (RadioOn(Item(3, 4)) ? 2 : 0);
	}
	return 0;
}


bool
TDialogPRTransitions::ItemHit(int32 index, BMessage* message)
{
	if (message != NULL && message->what == 'sD02') {
		int32 kind = message->GetInt32(kValueString, fKind);
		if (kind != fKind)
			LoadOptions(kind, kTransitions[kind].defaultOptions);
		return true;
	}
	if (index == 0) {
		BMessage set('PR03');
		set.AddInt32("slide", fSlide);
		set.AddInt32("refcon", int32(kTransitions[fKind].type));
		set.AddInt32("transition", int32(Options()));
		fWindow->PostMessage(&set);
	}
	return TDialog::ItemHit(index, message);
}


// #pragma mark - the slide's name


TDialogSlideName::TDialogSlideName(BLooper* window, uint32 what, int32 slide,
		const char* title, const char* name, const char* label)
	:
	TDialog(31, 31, 'APPL', NULL),
	fWindow(window),
	fWhat(what),
	fSlide(slide),
	fTitle(title),
	fName(name),
	fLabel(label)
{
}


void
TDialogSlideName::Init()
{
	SetTitle(fTitle.String());
	if (fLabel.Length() > 0)
		SetLabelItem(2, fLabel.String());
	SetTextItem(2, fName.String());
	MakeFocusItem(2);
}


// TDialogSlideName::ValidateAndSendMsg
bool
TDialogSlideName::ItemHit(int32 index, BMessage* message)
{
	if (index == 0) {
		BMessage name(fWhat);
		name.AddInt32("slide", fSlide);
		name.AddString("name", TextItem(2).String());
		fWindow->PostMessage(&name);
	}
	return TDialog::ItemHit(index, message);
}

// #pragma mark - the Presentation window


namespace {

TDialogPRNavigate* sNavigate = NULL;
const uint32 kMsgRefresh = 'sD=L';
const uint32 kMsgSelect = 'sD=S';

// PresListItem: a slide (level 0) or one of its layers; a slide's row has
// its layers' menu (a pop-up arrow) at its right and, but for the last
// slide, its transition's (a shaded square) left of it
class PresListItem : public BListItem {
public:
	PresListItem(const std::string& label, int32 slide, int32 layer, bool transition)
		:
		BListItem(layer < 0 ? 0 : 1, true),
		fLabel(label),
		fSlide(slide),
		fLayer(layer),
		fTransition(transition)
	{
	}

	int32 Slide() const { return fSlide; }
	int32 Layer() const { return fLayer; }

	// PresListItem::CalcIconRects: 10 high in the middle of the row, the
	// layers' 3 from the right, the transition's 13 left of it
	void IconRects(BRect frame, BRect& transition, BRect& layers) const
	{
		float top = floorf(frame.top + (frame.Height() - 10) / 2);
		layers.Set(frame.right - 13, top, frame.right - 3, top + 10);
		transition = layers.OffsetByCopy(-13, 0);
		if (!fTransition || fLayer >= 0)
			transition.Set(-10000, -10000, -10000, -10000);
		if (fLayer >= 0)
			layers.Set(-10000, -10000, -10000, -10000);
	}

	virtual void DrawItem(BView* owner, BRect frame, bool complete)
	{
		rgb_color background = owner->ViewColor();
		rgb_color selected = {0x98, 0x98, 0x98, 255};
		owner->SetHighColor(IsSelected() ? selected : background);
		owner->FillRect(frame);
		owner->SetHighColor(0, 0, 0);
		owner->SetLowColor(IsSelected() ? selected : background);
		font_height height;
		owner->GetFontHeight(&height);
		owner->DrawString(fLabel.c_str(), BPoint(frame.left + 2,
			frame.bottom - height.descent - 1));
		if (fLayer >= 0)
			return;
		BRect transition, layers;
		IconRects(frame, transition, layers);
		if (transition.left > -10000) {
			// a square shaded from black to white
			for (int32 x = 1; x < 10; x++) {
				uint8 v = uint8(x * 255 / 10);
				owner->SetHighColor(v, v, v);
				owner->StrokeLine(BPoint(transition.left + x, transition.top + 1),
					BPoint(transition.left + x, transition.bottom - 1));
			}
			owner->SetHighColor(0, 0, 0);
			owner->StrokeRect(transition);
		}
		// a white box with a down arrow, its shadow right and below
		owner->SetHighColor(255, 255, 255);
		owner->FillRect(layers);
		owner->SetHighColor(0, 0, 0);
		owner->StrokeRect(layers);
		owner->StrokeLine(BPoint(layers.left + 1, layers.bottom + 1),
			BPoint(layers.right + 1, layers.bottom + 1));
		owner->StrokeLine(BPoint(layers.right + 1, layers.top + 1),
			BPoint(layers.right + 1, layers.bottom + 1));
		BPoint arrow[3] = {BPoint(layers.left + 2, layers.top + 4),
			BPoint(layers.right - 2, layers.top + 4), BPoint(layers.left + 5, layers.top + 7)};
		owner->FillPolygon(arrow, 3);
	}

private:
	std::string fLabel;
	int32 fSlide, fLayer;
	bool fTransition;
};

} // namespace


// the list: a click on a slide's icons pops up its menu
class PresList : public BOutlineListView {
public:
	PresList() : BOutlineListView("slides", B_SINGLE_SELECTION_LIST) {}

	virtual void MouseDown(BPoint where)
	{
		int32 index = IndexOf(where);
		PresListItem* item = dynamic_cast<PresListItem*>(ItemAt(index));
		if (item != NULL && sNavigate != NULL) {
			BRect transition, layers;
			item->IconRects(ItemFrame(index), transition, layers);
			if (transition.Contains(where)) {
				sNavigate->TransitionMenu(item->Slide(), ConvertToScreen(transition.LeftBottom()));
				return;
			}
			if (layers.Contains(where)) {
				sNavigate->LayersMenu(item->Slide(), ConvertToScreen(layers.LeftBottom()));
				return;
			}
		}
		BOutlineListView::MouseDown(where);
	}
};


TDialogPRNavigate::TDialogPRNavigate()
	:
	TDialog(11001, 11001, 'PRES', NULL, &Strings())
{
	sNavigate = this;
}


TDialogPRNavigate::~TDialogPRNavigate()
{
	if (sNavigate == this)
		sNavigate = NULL;
}


TDialogPRNavigate*
TDialogPRNavigate::Open()
{
	return sNavigate;
}


void
TDialogPRNavigate::Refresh()
{
	if (sNavigate != NULL)
		sNavigate->PostMessage(kMsgRefresh);
}


// TDialogPRNavigate::Init: the subset, the menus, the list
void
TDialogPRNavigate::Init()
{
	for (int32 i = 0; BWindow* window = be_app->WindowAt(i); i++)
		if (dynamic_cast<TDocWindow*>(window) != NULL)
			AddToSubset(window);
	BMenuBar* bar = new BMenuBar("mMenuBar");
	fEdit = new TMenu(11003, Strings());
	fOptions = new TMenu(11004, Strings());
	bar->AddItem(fEdit);
	bar->AddItem(fOptions);
	if (BGroupLayout* layout = dynamic_cast<BGroupLayout*>(GetLayout()))
		layout->AddView(0, bar);
	// (CLST: a TOutlineListView of PresListItems, the list's font 9)
	if (BScrollView* scroll = dynamic_cast<BScrollView*>(Item(2))) {
		BListView* old = dynamic_cast<BListView*>(scroll->Target());
		fList = new PresList;
		BFont font(be_plain_font);
		font.SetSize(9);
		fList->SetFont(&font);
		// (CLST params: 10 rows, of the list's font)
		font_height height;
		font.GetHeight(&height);
		float row = ceilf(height.ascent + height.descent + height.leading) + 2;
		BSize size(155, 10 * row + 4);
		scroll->SetExplicitMinSize(size);
		scroll->SetExplicitPreferredSize(size);
		scroll->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, size.height));
		fList->SetSelectionMessage(new BMessage(kMsgSelect));
		if (old != NULL) {
			scroll->SetTarget(NULL);
			old->RemoveSelf();
			delete old;
		}
		// (BScrollView::SetTarget adds it as a child)
		scroll->SetTarget(fList);
	}
	Update();
}


void
TDialogPRNavigate::Send(BMessage* message)
{
	if (TDocWindow* window = TDocWindow::Front())
		window->PostMessage(message);
}


bool
TDialogPRNavigate::Selection(int32& slide, int32& layer) const
{
	slide = fState.currentSlide;
	layer = -1;
	if (fList == NULL)
		return false;
	PresListItem* item = dynamic_cast<PresListItem*>(
		fList->FullListItemAt(fList->FullListCurrentSelection()));
	if (item == NULL)
		return false;
	slide = item->Slide();
	layer = item->Layer();
	return true;
}


// TDialogPRNavigate::UpdateGuts: the slides, their layers under them,
// collapsed as saved; the current slide (or layer) selected
void
TDialogPRNavigate::Update()
{
	PresentationState state;
	TDocWindow* window = TDocWindow::Front();
	if (window != NULL && window->LockWithTimeout(100000) == B_OK) {
		window->PresentationState(state);
		window->Unlock();
	}
	fState = state;
	if (fList == NULL)
		return;
	fUpdating = true;
	// (no selection message while rebuilding: it would choose the slide
	// again, and refresh this again)
	BMessage* selectMessage = fList->SelectionMessage() != NULL
		? new BMessage(*fList->SelectionMessage()) : NULL;
	fList->SetSelectionMessage(NULL);
	while (fList->FullListCountItems() > 0) {
		BListItem* item = fList->FullListItemAt(0);
		fList->RemoveItem(item);
		delete item;
	}
	int32 select = -1;
	for (size_t k = 0; k < state.slides.size(); k++) {
		const PresentationState::SlideRow& row = state.slides[k];
		PresListItem* slide = new PresListItem(row.name, int32(k), -1,
			k + 1 < state.slides.size());
		fList->AddItem(slide);
		if (int32(k) == state.currentSlide && state.currentLayer < 0)
			select = fList->FullListCountItems() - 1;
		for (size_t l = 0; l < row.layers.size(); l++) {
			fList->AddItem(new PresListItem(row.layers[l], int32(k), int32(l), false));
			if (int32(k) == state.currentSlide && state.currentLayer == int32(l))
				select = fList->FullListCountItems() - 1;
		}
		if (row.collapsed)
			fList->Collapse(slide);
	}
	if (select >= 0) {
		int32 index = fList->IndexOf(fList->FullListItemAt(select));
		if (index >= 0)
			fList->Select(index);
	}
	fList->SetSelectionMessage(selectMessage);
	fUpdating = false;
	FixButtons();
}


// TDialogPRNavigate::FixButtons: Start Here for a slide after the first
void
TDialogPRNavigate::FixButtons()
{
	int32 slide, layer;
	Selection(slide, layer);
	if (BView* here = Item(1)) {
		// (IsHidden(here): the window may not be shown yet)
		bool show = fState.presentation && slide > 0;
		if (show && here->IsHidden(here))
			here->Show();
		else if (!show && !here->IsHidden(here))
			here->Hide();
	}
	SetItemEnabled(0, fState.presentation && !fState.slides.empty());
}


// TDialogPRNavigate::MenusBeginning: Rename, Delete and Duplicate for the
// slide or the layer chosen (STR# 11002, 14/15, 10/11, 12/13); Auto-start
// and Cursor marked
void
TDialogPRNavigate::MenusBeginning()
{
	int32 slide, layer;
	bool chosen = Selection(slide, layer);
	bool isLayer = layer >= 0;
	if (fEdit != NULL) {
		if (BMenuItem* item = fEdit->FindItem('PR0C'))
			item->SetLabel(IndexString(Strings(), 11002, isLayer ? 15 : 14).c_str());
		if (BMenuItem* item = fEdit->FindItem('PR08'))
			item->SetLabel(IndexString(Strings(), 11002, isLayer ? 11 : 10).c_str());
		if (BMenuItem* item = fEdit->FindItem('PR0A'))
			item->SetLabel(IndexString(Strings(), 11002, isLayer ? 13 : 12).c_str());
		for (int32 i = 0; BMenuItem* item = fEdit->ItemAt(i); i++)
			item->SetEnabled(fState.presentation && (chosen || item->Message() == NULL
				|| item->Message()->what == 'PR04'));
	}
	if (fOptions != NULL) {
		if (BMenuItem* item = fOptions->FindItem('PR06'))
			item->SetMarked(fState.autoStart);
		if (BMenuItem* item = fOptions->FindItem('PR07'))
			item->SetMarked(fState.cursor);
	}
	TDialog::MenusBeginning();
}


void
TDialogPRNavigate::TransitionMenu(int32 slide, BPoint where)
{
	if (slide < 0 || slide >= int32(fState.slides.size()))
		return;
	// the transitions, the slide's marked; Settings… (MENU 11002)
	BPopUpMenu menu("transition", false, false);
	for (int32 i = 0; i < 4; i++) {
		BMessage* message = new BMessage('PR03');
		message->AddInt32("slide", slide);
		message->AddInt32("refcon", int32(kTransitions[i].type));
		message->AddInt32("transition", int32(kTransitions[i].defaultOptions));
		BMenuItem* item = new BMenuItem(kTransitions[i].name, message);
		item->SetMarked(kTransitions[i].type == fState.slides[slide].transition);
		menu.AddItem(item);
	}
	TMenu settings(11002, Strings());
	while (settings.CountItems() > 0) {
		BMenuItem* item = settings.RemoveItem(int32(0));
		if (item->Message() != NULL)
			item->Message()->AddInt32("slide", slide);
		menu.AddItem(item);
	}
	BMenuItem* chosen = menu.Go(where, false, true, false);
	if (chosen == NULL || chosen->Message() == NULL)
		return;
	if (chosen->Message()->what == 'PR0K')
		PostMessage(chosen->Message());
	else
		Send(chosen->Message());
}


void
TDialogPRNavigate::LayersMenu(int32 slide, BPoint where)
{
	if (slide < 0 || slide >= int32(fState.slides.size()))
		return;
	// every layer of the drawing, those in the slide marked: a marked one
	// out of it ('PR09'), another into it ('PR05')
	BPopUpMenu menu("layers", false, false);
	for (size_t l = 0; l < fState.partLayers.size(); l++) {
		bool in = slide < int32(fState.inSlide.size()) && l < fState.inSlide[slide].size()
			&& fState.inSlide[slide][l];
		BMessage* message = new BMessage(in ? 'PR09' : 'PR05');
		message->AddInt32("slide", slide);
		message->AddInt32("index", int32(l));
		BMenuItem* item = new BMenuItem(fState.partLayers[l].c_str(), message);
		item->SetMarked(in);
		menu.AddItem(item);
	}
	BMenuItem* chosen = menu.Go(where, false, true, false);
	if (chosen != NULL && chosen->Message() != NULL)
		Send(chosen->Message());
}


void
TDialogPRNavigate::MessageReceived(BMessage* message)
{
	int32 slide, layer;
	Selection(slide, layer);
	TDocWindow* window = TDocWindow::Front();
	switch (message->what) {
		case kMsgRefresh:
			Update();
			return;
		case 'sE05':
		{
			BWindow* doc;
			if (message->FindPointer("window", (void**)&doc) == B_OK)
				AddToSubset(doc);
			return;
		}
		case kMsgSelect:
			if (fUpdating)
				return;
			// a slide shown, or one of its layers alone (TPRSwitchLayer)
			if (Selection(slide, layer)) {
				BMessage show(layer < 0 ? 'PR0H' : 'PR02');
				show.AddInt32("slide", slide);
				show.AddInt32("layer", layer);
				Send(&show);
			}
			FixButtons();
			return;
		case 'PR04':	// New Slide…
			if (window != NULL)
				(new TDialogPRNewSlide(window, slide + 1,
					int32(fState.slides.size())))->RunDialog();
			return;
		case 'PR0G':	// New Layer… ("untitled layer N", STR# 11002, 4, 8, 9)
		{
			if (window == NULL)
				return;
			char name[256];
			snprintf(name, sizeof name, IndexString(Strings(), 11002, 4).c_str(),
				int(fState.partLayers.size()));
			(new TDialogSlideName(window, 'PR0G', slide,
				IndexString(Strings(), 11002, 8).c_str(), name,
				IndexString(Strings(), 11002, 9).c_str()))->RunDialog();
			// (the layer it goes after)
			return;
		}
		case 'PR0C':	// Rename Slide… or Layer…
			if (window == NULL || slide < 0 || slide >= int32(fState.slides.size()))
				return;
			if (layer < 0)
				(new TDialogSlideName(window, 'PR0C', slide,
					IndexString(Strings(), 11001, 2).c_str(),
					fState.slides[slide].name.c_str(),
					IndexString(Strings(), 11002, 1).c_str()))->RunDialog();
			else {
				// TPartGR's Rename Layer ('sM4H' with the part layer)
				BMessage rename('sM4H');
				rename.AddInt32("slide", slide);
				rename.AddInt32("layer", layer);
				Send(&rename);
			}
			return;
		case 'PR0A':	// Duplicate Slide… or Layer
			if (window == NULL || slide < 0 || slide >= int32(fState.slides.size()))
				return;
			if (layer < 0)
				(new TDialogPRDupSlide(window, slide,
					fState.slides[slide].name.c_str()))->RunDialog();
			else {
				BMessage dup('PR0A');
				dup.AddInt32("slide", slide);
				dup.AddInt32("layer", layer);
				Send(&dup);
			}
			return;
		case 'PR08':	// Delete Slide or Layer
		{
			BMessage remove('PR08');
			remove.AddInt32("slide", slide);
			remove.AddInt32("layer", layer);
			Send(&remove);
			return;
		}
		case 'PR06':
		case 'PR07':
			Send(message);
			return;
		case 'PR0K':	// Settings…: the slide's transition
		{
			int32 which = message->GetInt32("slide", slide);
			if (window != NULL && which >= 0 && which < int32(fState.slides.size()))
				(new TDialogPRTransitions(window, which, fState.slides[which].transition,
					fState.slides[which].options))->RunDialog();
			return;
		}
	}
	TDialog::MessageReceived(message);
}


// Start (0) and Start Here (1): the slide show from the first slide or the
// one chosen ('PR01' with "start_slide")
bool
TDialogPRNavigate::ItemHit(int32 index, BMessage* message)
{
	if (index == 0 || index == 1) {
		int32 slide, layer;
		Selection(slide, layer);
		BMessage start('PR01');
		start.AddInt32("start_slide", index == 0 ? 0 : std::max<int32>(0, slide));
		Send(&start);
		return true;
	}
	return TDialog::ItemHit(index, message);
}

// #pragma mark - the slide show


namespace {

class TSlideShowWindow;

// the show running (TPartPR's ThePresentation)
static BWindow* sShow = NULL;

class TSlideView : public BView {
public:
	TSlideView(BRect frame, TSlideShowWindow* show)
		:
		BView(frame, "slide", B_FOLLOW_ALL, B_WILL_DRAW),
		fShow(show)
	{
		SetViewColor(B_TRANSPARENT_COLOR);
	}

	virtual void Draw(BRect update);
	virtual void KeyDown(const char* bytes, int32 count);
	virtual void MouseDown(BPoint where);

private:
	TSlideShowWindow* fShow;
};


class TSlideShowWindow : public BWindow {
public:
	TSlideShowWindow(TDocWindow* window, Document* document, int32 slide)
		:
		BWindow(BScreen().Frame(), "Presentation", B_NO_BORDER_WINDOW_LOOK,
			B_FLOATING_ALL_WINDOW_FEEL, B_NOT_MOVABLE | B_NOT_RESIZABLE | B_NOT_ZOOMABLE
				| B_NOT_MINIMIZABLE | B_ASYNCHRONOUS_CONTROLS),
		fWindow(window),
		fDocument(document),
		fSlide(slide)
	{
		BRect screen = BScreen().Frame();
		// TPRWinHelper::FinishDocWindow: the slide as large as the screen
		// allows, in the middle, black round it
		float width = 640, height = 480;
		if (const GRPart* gr = fDocument->PresentationGraphics()) {
			width = std::max(1.0f, gr->size[0]);
			height = std::max(1.0f, gr->size[1]);
		}
		fZoom = std::min((screen.Width() + 1) / width, (screen.Height() + 1) / height);
		float w = floorf(width * fZoom), h = floorf(height * fZoom);
		fArea.Set(floorf((screen.Width() + 1 - w) / 2), floorf((screen.Height() + 1 - h) / 2), 0, 0);
		fArea.right = fArea.left + w - 1;
		fArea.bottom = fArea.top + h - 1;
		fView = new TSlideView(Bounds(), this);
		AddChild(fView);
		fView->MakeFocus();
		{
			BScreen display(this);
			display_mode mode;
			if (display.GetMode(&mode) == B_OK && mode.timing.pixel_clock > 0
					&& mode.timing.h_total > 0 && mode.timing.v_total > 0)
				fRate = int32(mode.timing.pixel_clock * 1000.0
					/ (mode.timing.h_total * mode.timing.v_total) + 0.5);
			if (fRate < 20 || fRate > 240)
				fRate = 60;
		}
		fShown = Render(fSlide);
		if (!fDocument->presentation.cursor) {
			be_app->HideCursor();
			fHidCursor = true;
		}
	}

	virtual ~TSlideShowWindow()
	{
		delete fShown;
	}

	BBitmap* Shown() const { return fShown; }
	BRect Area() const { return fArea; }

	// TPRWinHelper::HandleKey
	void Key(char key, uint32 modifiers)
	{
		int32 count = int32(fDocument->slides.size());
		int32 to = fSlide;
		switch (key) {
			case B_HOME:
			case B_UP_ARROW:
				to = 0;
				break;
			case B_END:
			case B_DOWN_ARROW:
				to = count - 1;
				break;
			case B_BACKSPACE:
			case B_PAGE_UP:
			case B_LEFT_ARROW:
				to--;
				break;
			case B_TAB:
			case B_PAGE_DOWN:
			case B_RIGHT_ARROW:
			case B_SPACE:
				to++;
				break;
			case '.':
				if ((modifiers & B_COMMAND_KEY) == 0)
					return;
				// fall through
			case B_ESCAPE:
			case 'Q':
			case 'q':
				PostMessage(B_QUIT_REQUESTED);
				return;
			case 'R':
			case 'r':
				fView->Invalidate();
				return;
			default:
				return;
		}
		if (to == fSlide || to < 0 || to >= count)
			return;
		BBitmap* next = Render(to);
		if (next == NULL)
			return;
		// the slide left's transition, going on to the next; any other
		// change a jump
		if (to == fSlide + 1)
			Transit(next, fDocument->slides[size_t(fSlide)].transition,
				fDocument->slides[size_t(fSlide)].options);
		delete fShown;
		fShown = next;
		fSlide = to;
		fView->Invalidate();
	}

	virtual bool QuitRequested()
	{
		// back in the window on the slide the show ended on
		if (fHidCursor)
			be_app->ShowCursor();
		BMessage show('PR0H');
		show.AddInt32("slide", fSlide);
		show.AddInt32("layer", -1);
		fWindow->PostMessage(&show);
		sShow = NULL;
		return true;
	}

private:
	// TDocBitmap: the slide at the show's zoom, drawn with the document's
	// window locked
	BBitmap* Render(int32 slide)
	{
		BBitmap* bitmap = NULL;
		if (fWindow->LockWithTimeout(2000000) == B_OK) {
			fDocument->ActivateSlideLayer(size_t(slide), -1);
			bitmap = TPageView::RenderFrame(*fDocument, size_t(fDocument->presentationPart),
				fZoom);
			fWindow->Unlock();
		}
		return bitmap;
	}

	// the transitions' step (the Wipe's and Elevator's): the next screen
	// retrace or, without one, 800000 / refresh rate microseconds after the
	// last step
	void Step()
	{
		fView->Sync();
		if (BScreen().WaitForRetrace() == B_OK)
			return;
		bigtime_t next = fLastStep + 800000 / fRate;
		bigtime_t now = system_time();
		if (now < next)
			snooze(next - now);
		fLastStep = system_time();
	}

	void DrawPart(BBitmap* bitmap, BRect source)
	{
		source = source & bitmap->Bounds();
		if (!source.IsValid())
			return;
		fView->DrawBitmap(bitmap, source, source.OffsetByCopy(fArea.LeftTop()));
	}

	void Transit(BBitmap* to, uint32 type, uint32 options)
	{
		if (!LockLooper())
			return;
		BRect r = to->Bounds();
		float w = r.Width() + 1, h = r.Height() + 1;
		switch (type) {
			case 'Wipe':
			{
				// WipeTransition: a strip the new slide grows by, from the
				// side the options name (left, top, right, bottom)
				for (int32 i = 0; i < fRate; i++) {
					float a = floorf(i * (options % 2 == 0 ? w : h) / fRate);
					float b = floorf((i + 1) * (options % 2 == 0 ? w : h) / fRate);
					BRect strip;
					switch (options) {
						case 0: strip.Set(a, 0, b, h - 1); break;
						case 1: strip.Set(0, a, w - 1, b); break;
						case 2: strip.Set(w - 1 - b, 0, w - 1 - a, h - 1); break;
						default: strip.Set(0, h - 1 - b, w - 1, h - 1 - a); break;
					}
					DrawPart(to, strip);
					Step();
				}
				break;
			}
			case 'Vene':
			{
				// VenetianTransition::Go: ten blinds, each a 2-pixel strip
				// further a pass, synced strip by strip with no wait; left
				// to right, top down, right to left or bottom up (options 0
				// to 3)
				bool across = options == 1 || options == 3;
				bool back = options == 2 || options == 3;
				float size = across ? h : w;
				float blind = size / 10;
				int32 passes = int32(roundf(blind));
				for (int32 d = 0; d <= passes; d += 2) {
					for (int32 k = 0; k < 10; k++) {
						float at = back ? size - 1 - d - k * blind - 2 : d + k * blind;
						DrawPart(to, across ? BRect(0, at, w - 1, at + 2)
							: BRect(at, 0, at + 2, h - 1));
						fView->Sync();
					}
				}
				break;
			}
			case 'Elev':
			{
				// ElevatorTransition::Go: two strips of the new slide a
				// step, top and bottom (bit 0 off) or left and right; from
				// the edges in (closing) or, bit 1, from the middle out
				bool vertical = (options & 1) != 0, opening = (options & 2) != 0;
				float size = vertical ? w : h, half = size / 2;
				for (int32 i = 0; i < fRate; i++) {
					float a = floorf(half * i / fRate), b = floorf(half * (i + 1) / fRate);
					float s1, e1, s2, e2;
					if (opening) {
						s1 = half - b; e1 = half - a; s2 = half + a; e2 = half + b;
					} else {
						s1 = a; e1 = b; s2 = size - b; e2 = size - a;
					}
					DrawPart(to, vertical ? BRect(s1, 0, e1, h - 1) : BRect(0, s1, w - 1, e1));
					DrawPart(to, vertical ? BRect(s2, 0, e2, h - 1) : BRect(0, s2, w - 1, e2));
					Step();
				}
				break;
			}
		}
		UnlockLooper();
	}

	TDocWindow* fWindow;
	Document* fDocument;
	int32 fSlide;
	float fZoom = 1;
	BRect fArea;
	int32 fRate = 60;
	bigtime_t fLastStep = 0;
	BBitmap* fShown = NULL;
	TSlideView* fView = NULL;
	bool fHidCursor = false;
};


void
TSlideView::Draw(BRect update)
{
	SetHighColor(0, 0, 0);
	FillRect(Bounds());
	if (BBitmap* shown = fShow->Shown())
		DrawBitmap(shown, fShow->Area().LeftTop());
}


void
TSlideView::KeyDown(const char* bytes, int32 count)
{
	int32 modifiers = 0;
	if (Window()->CurrentMessage() != NULL)
		Window()->CurrentMessage()->FindInt32("modifiers", &modifiers);
	fShow->Key(bytes[0], uint32(modifiers));
}


// TPRWinHelper::HandleClick: as Space
void
TSlideView::MouseDown(BPoint where)
{
	fShow->Key(B_SPACE, 0);
}

} // namespace


void
StartSlideShow(TDocWindow* window, Document* document, int32 slide)
{
	if (window == NULL || document == NULL || document->presentationPart < 0
			|| document->slides.empty())
		return;
	slide = std::max<int32>(0, std::min<int32>(slide, int32(document->slides.size()) - 1));
	// TPartPR 'PR01': one show at a time (ThePresentation)
	if (sShow != NULL)
		return;
	TSlideShowWindow* show = new TSlideShowWindow(window, document, slide);
	sShow = show;
	show->Show();
	show->Activate();
}

} // namespace gobe
