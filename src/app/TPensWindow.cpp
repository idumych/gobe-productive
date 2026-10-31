#include "TPensWindow.h"

#include <Alert.h>
#include <Application.h>
#include <Button.h>
#include <GroupLayout.h>
#include <ListView.h>
#include <MenuBar.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <PopUpMenu.h>
#include <ScrollView.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <cstdlib>

#include "GobeResources.h"
#include "Measure.h"
#include "TDocWindow.h"
#include "TInkEditors.h"
#include "TInkMenus.h"
#include "TMenu.h"
#include "TRibbon.h"

namespace gobe {

namespace {

TPensWindow* sPens = NULL;
TDialogEditPen* sEditPen = NULL;

const uint32 kMsgRefresh = 'GpnR';
const uint32 kMsgScript = 'GpnS';
const uint32 kMsgWiden = 'GpnW';
const uint32 kMsgApplyPen = 'sfI9';		// TDialogNewColor::DoApplyPen

const uint32 kPenKinds[4] = {'NORM', 'PDOT', 'DASH', 'HASH'};


std::vector<std::string>
UnitNames()
{
	std::vector<std::string> names;
	for (int32 i = 0; i < 6; i++)
		names.push_back(GobeResources::IndexString(7, i).String());
	return names;
}


// a view filling a USER item (TUserItemEngineer's draw proc)
void
FillUserItem(BView* user, BView* view)
{
	if (user == NULL)
		return;
	BGroupLayout* layout = new BGroupLayout(B_VERTICAL, 0);
	user->SetLayout(layout);
	layout->AddView(view);
}


// TPenTypeListItem: a pen's line and its name
class TPenListItem : public BListItem {
public:
	TPenListItem(const PenType& pen)
		:
		fPen(pen)
	{
	}

	virtual void DrawItem(BView* owner, BRect frame, bool complete)
	{
		rgb_color background = IsSelected() ? ui_color(B_LIST_SELECTED_BACKGROUND_COLOR)
			: owner->ViewColor();
		owner->SetHighColor(background);
		owner->FillRect(frame);
		owner->SetHighColor(IsSelected() ? ui_color(B_LIST_SELECTED_ITEM_TEXT_COLOR)
			: ui_color(B_LIST_ITEM_TEXT_COLOR));
		float y = std::floor((frame.top + frame.bottom) / 2);
		Outline line{{{frame.left + 4, y}, {frame.left + 56, y}}, false};
		StrokePenType(owner, line, fPen, 2);
		owner->SetLowColor(background);
		font_height height;
		owner->GetFontHeight(&height);
		owner->DrawString(fPen.name.c_str(), BPoint(frame.left + 64, y + height.ascent / 2 - 1));
	}

	virtual void Update(BView* owner, const BFont* font)
	{
		BListItem::Update(owner, font);
		SetHeight(std::max(Height(), 18.0f));
	}

private:
	PenType fPen;
};


// the minor types above the list (TDialogNewColor's item 0): the colour,
// pattern and gradient sets and the pens; the pens chosen
class TInkKindsView : public BView {
public:
	TInkKindsView()
		:
		BView("kinds", B_WILL_DRAW)
	{
		SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
		static const int32 kIcons[4] = {0x16, 0x18, 0x19, 0x1b};
		for (int32 i = 0; i < 4; i++)
			fIcons[i].reset(TMICNElement::LoadIcon(kIcons[i]));
	}

	virtual void Draw(BRect)
	{
		for (int32 i = 0; i < 4; i++) {
			BRect r(4 + i * 24, 1, 4 + i * 24 + 19, 20);
			if (i == 3)
				TRibbon::DrawOutlineDown(this, r);
			if (fIcons[i] == NULL)
				continue;
			SetDrawingMode(B_OP_OVER);
			DrawBitmap(fIcons[i].get(), r.LeftTop() + BPoint(2, 2));
			SetDrawingMode(B_OP_COPY);
		}
	}

private:
	std::unique_ptr<BBitmap> fIcons[4];
};

} // namespace


// TDialogNewColor (DIAL 65): the menus (MENU 150 Kind, 152 Edit), the
// kinds (item 0) and the pens (item 1, TPenView's TPensListView)
TPensWindow::TPensWindow()
	:
	TDialog(65, 65, 'APPL', NULL)
{
	sPens = this;
}


TPensWindow::~TPensWindow()
{
	if (sPens == this)
		sPens = NULL;
}


void
TPensWindow::Open()
{
	if (sPens != NULL) {
		sPens->PostMessage(kMsgRefresh);
		sPens->Activate();
		return;
	}
	(new TPensWindow())->RunDialog();
}


void
TPensWindow::Refresh()
{
	if (sPens != NULL)
		sPens->PostMessage(kMsgRefresh);
}


void
TPensWindow::Script(const std::string& command)
{
	if (sPens == NULL) {
		printf("pens: no Inks window\n");
		fflush(stdout);
		return;
	}
	BMessage script(kMsgScript);
	script.AddString("command", command.c_str());
	sPens->PostMessage(&script);
}


// TDialogNewColor::Init, SetWindowTitle (STR# 5, 3: the pens')
void
TPensWindow::Init()
{
	for (int32 i = 0; BWindow* window = be_app->WindowAt(i); i++)
		if (dynamic_cast<TDocWindow*>(window) != NULL)
			AddToSubset(window);
	SetTitle(GobeResources::IndexString(5, 3).String());
	BMenuBar* bar = new BMenuBar("mMenuBar");
	// Kind: Pen Ink (the others not ported)
	TMenu* kind = new TMenu(150);
	for (int32 i = 0; BMenuItem* item = kind->ItemAt(i); i++) {
		item->SetMarked(i == 1);
		item->SetEnabled(i == 1);
	}
	bar->AddItem(kind);
	// Edit: Cut, Copy, Paste, Clear, New Pen…, Edit Pen…
	// (STR# 8, 3 and 11)
	fEdit = new TMenu(152);
	if (BMenuItem* item = fEdit->FindItem('sMA4'))
		item->SetLabel(GobeResources::IndexString(8, 3).String());
	if (BMenuItem* item = fEdit->FindItem('sMA3'))
		item->SetLabel(GobeResources::IndexString(8, 11).String());
	bar->AddItem(fEdit);
	if (BGroupLayout* layout = dynamic_cast<BGroupLayout*>(GetLayout()))
		layout->AddView(0, bar);
	FillUserItem(Item(0), new TInkKindsView());
	BListView* list = new BListView("pens");
	list->SetInvocationMessage(new BMessage(kMsgApplyPen));
	FillUserItem(Item(1), new BScrollView("scroll", list, 0, false, true));
	// TPenView's width controls, right of the list
	if (BGroupLayout* layout = dynamic_cast<BGroupLayout*>(Item(1)->GetLayout())) {
		layout->SetOrientation(B_HORIZONTAL);
		layout->AddView(fWidth = new TPenWidthView());
		// (the window wider by them, once shown)
		PostMessage(kMsgWiden);
	}
	Update();
}


BListView*
TPensWindow::List() const
{
	BView* user = Item(1);
	BScrollView* scroll = user != NULL ? dynamic_cast<BScrollView*>(user->ChildAt(0)) : NULL;
	return scroll != NULL ? dynamic_cast<BListView*>(scroll->Target()) : NULL;
}


// TPenView::UpdateContents: the front document's pens
void
TPensWindow::Update()
{
	fPens.clear();
	TDocWindow* window = TDocWindow::Front();
	if (window != NULL && window->LockWithTimeout(100000) == B_OK) {
		fPens = window->Pens();
		window->Unlock();
	} else
		fPens = AppPens();
	BListView* list = List();
	if (list == NULL)
		return;
	int32 selected = list->CurrentSelection();
	while (BListItem* item = list->RemoveItem(int32(0)))
		delete item;
	for (const PenType& pen : fPens)
		list->AddItem(new TPenListItem(pen));
	if (selected >= 0)
		list->Select(std::min(selected, list->CountItems() - 1));
	if (fWidth != NULL)
		fWidth->Update();
}


// TDialogNewColor::MenusBeginning: with the pens, Cut, Copy, Clear and
// Edit Pen… for a pen selected that can be edited (TPen::IsEditable: not
// the normal pen); Paste for a pen copied (+0x88)
void
TPensWindow::MenusBeginning()
{
	TDialog::MenusBeginning();
	BListView* list = List();
	int32 selected = list != NULL ? list->CurrentSelection() : -1;
	bool editable = selected >= 0 && size_t(selected) < fPens.size()
		&& fPens[size_t(selected)].kind > 0;
	if (fEdit == NULL)
		return;
	for (int32 i = 0; BMenuItem* item = fEdit->ItemAt(i); i++) {
		uint32 what = item->Message() != NULL ? item->Message()->what : 0;
		item->SetEnabled(what == 'sMA4' || (what == 'PSTE' && InkClipboard::Kind() == 3)
			|| ((what == 'sMA3' || what == 'sM12' || what == 'CCUT' || what == 'COPY')
				&& editable));
	}
}


// TDialogNewColor::DoNewPen, DoEditPen (TPenFactory::EditPenDialog)
void
TPensWindow::EditPen(bool make)
{
	BListView* list = List();
	int32 selected = list != NULL ? list->CurrentSelection() : -1;
	if (make) {
		new TDialogEditPen(NULL, this, BMessage(kMsgAddPen));
		return;
	}
	// (TPen::IsEditable: not the normal pen)
	if (selected < 0 || size_t(selected) >= fPens.size() || fPens[size_t(selected)].kind <= 0)
		return;
	BMessage reply(kMsgReplacePen);
	reply.AddInt32("ink index", selected);
	new TDialogEditPen(&fPens[size_t(selected)], this, reply);
}


void
TPensWindow::MessageReceived(BMessage* message)
{
	BListView* list = List();
	int32 selected = list != NULL ? list->CurrentSelection() : -1;
	TDocWindow* front = TDocWindow::Front();
	switch (message->what) {
		case kMsgRefresh:
			Update();
			return;
		case kMsgWiden:
			if (fWidth != NULL)
				ResizeBy(fWidth->PreferredSize().width, 0);
			return;
		case 'sMA4':
		case 'sMA3':
			EditPen(message->what == 'sMA4');
			return;
		case 'COPY':
		case 'CCUT':
			// DoCopy: the pen kept for Paste; Cut: DoCopy, then DoClear
			if (selected >= 0 && size_t(selected) < fPens.size())
				InkClipboard::CopyPen(fPens[size_t(selected)]);
			if (message->what == 'COPY')
				return;
			// fall through
		case 'sM12':
			// Clear (DoClear: TPenMgr::DeletePen)
			if (selected >= 0 && front != NULL) {
				BMessage clear(kMsgDeletePen);
				clear.AddInt32("ink index", selected);
				front->PostMessage(&clear);
			}
			return;
		case 'PSTE':
			// DoPaste: a colour pasted is kept as the ink copied; the pen
			// copied added to the front document's pens (DoAddPen)
			InkClipboard::TakeColor(message);
			if (InkClipboard::HasPen() && front != NULL) {
				BMessage add(kMsgAddPen);
				AddPenToMessage(&add, InkClipboard::CopiedPen());
				front->PostMessage(&add);
			}
			return;
		case kMsgApplyPen:
			// DoApplyPen: the selected pen to the front document's
			// selection ('sf0A')
			if (selected >= 0 && size_t(selected) < fPens.size() && front != NULL) {
				BMessage apply('sf0A');
				AddPenToMessage(&apply, fPens[size_t(selected)]);
				front->PostMessage(&apply);
			}
			return;
		case kMsgAddPen:
		case kMsgReplacePen:
			// the pen dialog's: to the front document (DoAddPen,
			// DoReplacePen)
			if (front != NULL)
				front->PostMessage(message);
			return;
		case 'GieR':
		{
			// (the test hook's ink editor replied)
			const void* data;
			ssize_t size = 0;
			message->FindData("ink", B_RAW_TYPE, &data, &size);
			printf("pens ink replied %d bytes:", int(size));
			for (ssize_t i = 0; i < size; i++)
				printf(" %02x", ((const uint8*)data)[i]);
			printf("\n");
			fflush(stdout);
			return;
		}
		case kMsgScript:
		{
			std::string command = message->GetString("command", "");
			if (command.compare(0, 7, "select ") == 0 && list != NULL)
				list->Select(atoi(command.c_str() + 7));
			else if (command == "new" || command == "edit") {
				MenusBeginning();
				EditPen(command == "new");
			} else if (command == "clear" || command == "cut" || command == "copy"
					|| command == "paste") {
				BMessage edit(command == "clear" ? 'sM12' : command == "cut" ? 'CCUT'
					: command == "copy" ? 'COPY' : 'PSTE');
				MessageReceived(&edit);
			} else if (command.compare(0, 6, "width ") == 0 && fWidth != NULL) {
				// "width +|-|N": the width buttons or the slider at N
				BMessage width(command[6] == '+' ? 'PEN+' : command[6] == '-' ? 'PEN-'
					: 'PEN#');
				if (width.what == 'PEN#')
					fWidth->SetPenWidth(float(atof(command.c_str() + 6)));
				fWidth->MessageReceived(&width);
				printf("pens width %.1f\n", fWidth->PenWidth());
				fflush(stdout);
			} else if (command.compare(0, 8, "editink ") == 0) {
				// "editink 0|1|2": a new ink's editor, its reply printed
				EditInk(atoi(command.c_str() + 8), NULL, this, BMessage('GieR'));
			} else if (command.compare(0, 7, "editor ") == 0) {
				InkEditorScript(command.substr(7));
			} else if (command == "apply") {
				BMessage apply(kMsgApplyPen);
				MessageReceived(&apply);
			} else if (command == "show") {
				for (size_t i = 0; i < fPens.size(); i++)
					printf("pens %d %s%s\n", int(i), fPens[i].name.c_str(),
						int32(i) == selected ? " (selected)" : "");
				fflush(stdout);
			}
			return;
		}
	}
	TDialog::MessageReceived(message);
}


// the pen dialog's sample (item 10, TDialogEditPen::UserDrawProc): a
// line across, 5 in from each end, at 4 points with the pen, in black
class TPenSampleView : public BView {
public:
	TPenSampleView()
		:
		BView("sample", B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE)
	{
		SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	}

	void SetPen(const PenType& pen)
	{
		fPen = pen;
		Invalidate();
	}

	virtual void Draw(BRect)
	{
		BRect b = Bounds();
		float y = std::floor((b.top + b.bottom) / 2);
		SetHighColor(0, 0, 0);
		StrokePenType(this, Outline{{{b.left + 5, y}, {b.right - 5, y}}, false}, fPen, 4);
	}

private:
	PenType fPen;
};


TDialogEditPen::TDialogEditPen(const PenType* pen, BLooper* target, const BMessage& reply)
	:
	TDialog(32, 32, 'APPL', NULL),
	fEditing(pen != NULL),
	fTarget(target),
	fReply(reply)
{
	sEditPen = this;
	if (pen != NULL) {
		// SetData
		fOriginal = *pen;
		fName = pen->name;
		fKind = pen->kind;
		fPrint = pen->print;
		if (pen->kind == 1 || pen->kind == 2 || pen->kind == 3)
			fGap = pen->gap;
		if (pen->kind == 2)
			fDash = pen->dash;
		if (pen->kind == 1)
			fFilled = pen->filled;
	}
	CreateNewPen();
	RunDialog();
}


void
TDialogEditPen::Script(const std::string& command)
{
	if (sEditPen == NULL) {
		printf("pendialog: no pen dialog\n");
		fflush(stdout);
		return;
	}
	BMessage script(kMsgScript);
	script.AddString("command", command.c_str());
	sEditPen->PostMessage(&script);
}


// TDialogEditPen::Init: a new pen's title STR# 8, 3 (less its "…"); the
// name, Printable; the kinds Dash, Dot and Hash (STR# 10, 2, 1, 3); the
// kind's options; Revert only when editing
void
TDialogEditPen::Init()
{
	if (!fEditing) {
		BString title = GobeResources::IndexString(8, 3);
		title.TruncateChars(std::max(int32(0), title.CountChars() - 1));
		SetTitle(title.String());
	}
	fSample = new TPenSampleView();
	FillUserItem(Item(10), fSample);
	fSample->SetPen(fPen);
	MakeFocusItem(3);
	SetTextItem(3, fName.c_str());
	SetCheckBoxItem(5, fPrint);
	if (BMenuField* field = dynamic_cast<BMenuField*>(Item(4))) {
		BMenu* menu = field->Menu();
		while (BMenuItem* item = menu->RemoveItem(int32(0)))
			delete item;
		static const int32 kKinds[3] = {2, 1, 3};
		for (int32 kind : kKinds) {
			BMessage* message = new BMessage('APPL' + 4);
			message->AddInt32("dialog", 32);
			message->AddInt32("index", 4);
			message->AddInt32(kValueString, int32(kPenKinds[kind]));
			menu->AddItem(new BMenuItem(GobeResources::IndexString(10, kind).String(), message));
		}
	}
	SetTypePopup();
	InitOptions();
	if (!fEditing)
		if (BView* revert = Item(2))
			revert->Hide();
}


void
TDialogEditPen::SetTypePopup()
{
	SetPopUpItem(4, int32(kPenKinds[std::max(0, std::min(3, fKind))]));
}


// TDialogEditPen::InitOptions: Item 100 (dashes: gap and dash widths),
// 101 (dots: gap, Fill Dot) or 102 (hash: gap), in points
void
TDialogEditPen::InitOptions()
{
	LoadItemSet(7, fKind == 3 ? 102 : fKind == 1 ? 101 : 100);
	std::vector<std::string> units = UnitNames();
	if (fKind == 2)
		SetTextItem(1, MeasureText(fDash, 4, kMeasureWithUnit | kMeasureTwoPlaces, units).c_str(), 7);
	if (fKind == 1)
		SetCheckBoxItem(1, fFilled, 7);
	SetTextItem(0, MeasureText(fGap, 4, kMeasureWithUnit | kMeasureTwoPlaces, units).c_str(), 7);
}


// TDialogEditPen::CreateNewPen (TPenFactory::RestorePen from the fields)
void
TDialogEditPen::CreateNewPen()
{
	fPen = PenType();
	fPen.kind = fKind;
	fPen.name = fName;
	fPen.print = fPrint;
	if (fKind == 1 || fKind == 2 || fKind == 3)
		fPen.gap = fGap;
	if (fKind == 2)
		fPen.dash = fDash;
	if (fKind == 1)
		fPen.filled = fFilled;
	if (fSample != NULL)
		fSample->SetPen(fPen);
}


// TDialogEditPen::ValidData: an edited pen's new name not another pen's
// (STR# 6, 18); the widths at least 1 point (STR# 6, 4)
bool
TDialogEditPen::ValidData()
{
	fName = TextItem(3).String();
	fPrint = CheckBoxItem(5);
	if (fEditing && fName != fOriginal.name) {
		std::vector<PenType> pens;
		TDocWindow* front = TDocWindow::Front();
		if (front != NULL && front->LockWithTimeout(100000) == B_OK) {
			pens = front->Pens();
			front->Unlock();
		}
		for (const PenType& pen : pens)
			if (pen.name == fName) {
				(new BAlert("", GobeResources::IndexString(6, 18).String(), "OK"))->Go(NULL);
				MakeFocusItem(3);
				return false;
			}
	}
	std::vector<std::string> units = UnitNames();
	auto width = [&](int32 index, float& value) {
		float points = 0;
		if (!ParseMeasure(TextItem(index, 7).String(), 4, units, false, false, points)
				|| points < 1) {
			(new BAlert("", GobeResources::IndexString(6, 4).String(), "OK"))->Go(NULL);
			MakeFocusItem(index, 7);
			return false;
		}
		value = points;
		SetTextItem(index, MeasureText(points, 4, kMeasureWithUnit | kMeasureTwoPlaces,
			units).c_str(), 7);
		return true;
	};
	if (!width(0, fGap))
		return false;
	if (fKind == 1)
		fFilled = CheckBoxItem(1, 7);
	if (fKind == 2 && !width(1, fDash))
		return false;
	CreateNewPen();
	return true;
}


void
TDialogEditPen::Revert()
{
	fName = fOriginal.name;
	fKind = fOriginal.kind;
	fPrint = fOriginal.print;
	fGap = fOriginal.gap;
	fDash = fOriginal.dash;
	fFilled = fOriginal.filled;
	CreateNewPen();
	SetTextItem(3, fName.c_str());
	SetCheckBoxItem(5, fPrint);
	SetTypePopup();
	InitOptions();
}


// TDialogEditPen::ItemHit: OK (the data valid: the pen posted), Cancel,
// Revert (an edited pen as it was), the kind (its options), the options
// (checked, the sample drawn again)
bool
TDialogEditPen::ItemHit(int32 index, BMessage* message)
{
	switch (index) {
		case 0:
			if (TextItem(3).Length() == 0 || !ValidData())
				return true;
			if (fTarget != NULL) {
				BMessage reply(fReply);
				AddPenToMessage(&reply, fPen);
				fTarget->PostMessage(&reply);
			}
			PostMessage(B_QUIT_REQUESTED);
			if (sEditPen == this)
				sEditPen = NULL;
			return false;
		case 1:
			PostMessage(B_QUIT_REQUESTED);
			if (sEditPen == this)
				sEditPen = NULL;
			return false;
		case 2:
			if (fEditing)
				Revert();
			return true;
		case 4:
		{
			int32 value = message->GetInt32(kValueString, -1);
			int kind = int(std::find(kPenKinds, kPenKinds + 4, uint32(value)) - kPenKinds);
			if (kind <= 3 && kind != fKind) {
				fKind = kind;
				InitOptions();
				CreateNewPen();
			}
			return true;
		}
		case 7:
			ValidData();
			return true;
	}
	return true;
}


void
TDialogEditPen::MessageReceived(BMessage* message)
{
	if (message->what != kMsgScript) {
		TDialog::MessageReceived(message);
		return;
	}
	std::string command = message->GetString("command", "");
	std::string value = command.find(' ') != std::string::npos
		? command.substr(command.find(' ') + 1) : "";
	if (command.compare(0, 5, "name ") == 0)
		SetTextItem(3, value.c_str());
	else if (command.compare(0, 5, "type ") == 0 && value.size() == 4) {
		BMessage type('APPL' + 4);
		type.AddInt32(kValueString, int32(uint32(value[0]) << 24 | uint32(value[1]) << 16
			| uint32(value[2]) << 8 | uint32(value[3])));
		SetPopUpItem(4, type.GetInt32(kValueString, 0));
		ItemHit(4, &type);
	} else if (command.compare(0, 4, "gap ") == 0)
		SetTextItem(0, value.c_str(), 7);
	else if (command.compare(0, 5, "dash ") == 0)
		SetTextItem(1, value.c_str(), 7);
	else if (command.compare(0, 7, "filled ") == 0)
		SetCheckBoxItem(1, value == "1", 7);
	else if (command.compare(0, 6, "print ") == 0)
		SetCheckBoxItem(5, value == "1");
	else if (command == "ok" || command == "cancel" || command == "revert") {
		BMessage hit('APPL');
		ItemHit(command == "ok" ? 0 : command == "cancel" ? 1 : 2, &hit);
	} else if (command == "check") {
		ItemHit(7, message);
		printf("pendialog \"%s\" kind %d gap %g dash %g filled %d print %d\n", fPen.name.c_str(),
			fPen.kind, fPen.gap, fPen.dash, fPen.filled, fPen.print);
		fflush(stdout);
	}
}

} // namespace gobe
