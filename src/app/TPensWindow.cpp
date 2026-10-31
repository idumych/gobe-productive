#include "TPensWindow.h"

#include <Alert.h>
#include <Application.h>
#include <Button.h>
#include <GroupLayout.h>
#include <GroupView.h>
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

#include <Directory.h>
#include <FilePanel.h>
#include <Path.h>

#include "DocArchive.h"
#include "GobeResources.h"
#include "Measure.h"
#include "TDocWindow.h"
#include "TInkEditors.h"
#include "TInkMenus.h"
#include "TMenu.h"
#include "TRibbon.h"
#include "TInkEditors.h"

namespace gobe {

namespace {

TPensWindow* sPens = NULL;
TDialogEditPen* sEditPen = NULL;

const uint32 kMsgRefresh = 'GpnR';
const uint32 kMsgScript = 'GpnS';
const uint32 kMsgApplyPen = 'sfI9';		// TDialogNewColor::DoApplyPen
const uint32 kMsgMajorType = 'MAJR';	// MENU 150's (ChangeMajorType)
const uint32 kMsgMinorType = 'GinT';	// the kinds' tabs (ChangeMinorType)
const uint32 kMsgSaveSet = 'GisS';		// Save Set As…'s panel (DoSave)



bool
InkFromBytes(const std::vector<uint8_t>& bytes, Ink& ink)
{
	DocArchive a(bytes);
	try {
		return ReadInk(a, ink);
	} catch (const ArchiveError&) {
		return false;
	}
}


// TDialogSetName (TDialogGenericGetString, DIAL/Item 31): a new set's
// name (STR# 2, 5); one the front document has already: STR# 6, 9; OK
// posts 'sMA5' with "ink name" to the Inks window
class TDialogSetName : public TDialog {
public:
	TDialogSetName(BLooper* window, const SetMgr& sets, int kind)
		:
		TDialog(31, 31, 'APPL', NULL),
		fWindow(window),
		fSets(sets),
		fKind(kind)
	{
	}

protected:
	virtual void Init()
	{
		BString title = GobeResources::IndexString(2, 5);
		SetTitle(title.String());
		SetLabelItem(2, title.String());
		SetTextItem(2, "");
		MakeFocusItem(2);
	}

	virtual bool ItemHit(int32 index, BMessage* message)
	{
		if (index == 2)
			return true;
		if (index == 0) {
			std::string name = TextItem(2).String();
			InkSetRef found;
			if (name.empty() || FindInkSet(fKind, name, &fSets, found)) {
				(new BAlert("", GobeResources::IndexString(6, 9).String(), "OK"))->Go(NULL);
				MakeFocusItem(2);
				return true;
			}
			BMessage reply('sMA5');
			reply.AddString("ink name", name.c_str());
			fWindow->PostMessage(&reply);
		}
		return TDialog::ItemHit(index, message);
	}

private:
	BLooper* fWindow;
	SetMgr fSets;
	int fKind;
};


// TDialogSetDimensions (DIAL/Item 55): the columns (1 to 64), the cell
// size (7 to 96; STR# 6, 13 otherwise), Show Gap (2 between cells, or 0);
// OK posts 'sMA6' with "columns", "cellSize" and "gapSize"
class TDialogSetDimensions : public TDialog {
public:
	TDialogSetDimensions(BLooper* window, const InkSet& set)
		:
		TDialog(55, 55, 'APPL', NULL),
		fWindow(window),
		fSet(set)
	{
	}

protected:
	virtual void Init()
	{
		SetTextItem(2, std::to_string(fSet.columns).c_str());
		SetTextItem(3, std::to_string(fSet.cell).c_str());
		SetCheckBoxItem(4, fSet.gap != 0);
		MakeFocusItem(2);
	}

	virtual bool ItemHit(int32 index, BMessage* message)
	{
		if (index == 0) {
			auto number = [&](int32 item, int low, int high, int& value) {
				char* end = NULL;
				BString text = TextItem(item);
				long n = strtol(text.String(), &end, 10);
				if (text.IsEmpty() || *end != 0 || n < low || n > high) {
					BString alert;
					alert.SetToFormat(GobeResources::IndexString(6, 13).String(), low, high);
					(new BAlert("", alert.String(), "OK"))->Go(NULL);
					MakeFocusItem(item);
					return false;
				}
				value = int(n);
				return true;
			};
			int columns = 0, cell = 0;
			if (!number(2, 1, 64, columns) || !number(3, 7, 96, cell))
				return true;
			BMessage reply('sMA6');
			reply.AddInt32("columns", columns);
			reply.AddInt32("cellSize", cell);
			reply.AddInt32("gapSize", CheckBoxItem(4) ? 2 : 0);
			fWindow->PostMessage(&reply);
		}
		return TDialog::ItemHit(index, message);
	}

private:
	BLooper* fWindow;
	InkSet fSet;
};

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


// TMICNTabView (TDialogNewColor's item 0): the colours, patterns,
// gradients and, for the pen ink, the pens; a click chooses
class TInkKindsView : public BView {
public:
	TInkKindsView(TPensWindow* window)
		:
		BView("kinds", B_WILL_DRAW),
		fWindow(window)
	{
		SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
		static const int32 kIcons[4] = {0x16, 0x18, 0x19, 0x1b};
		for (int32 i = 0; i < 4; i++)
			fIcons[i].reset(TMICNElement::LoadIcon(kIcons[i]));
		SetExplicitMinSize(BSize(4 * 24 + 4, 22));
	}

	virtual void Draw(BRect)
	{
		int32 tabs = fWindow->MajorType() == 1 ? 4 : 3;
		for (int32 i = 0; i < tabs; i++) {
			BRect r = Tab(i);
			if (i == fWindow->MinorType())
				TRibbon::DrawOutlineDown(this, r);
			if (fIcons[i] == NULL)
				continue;
			SetDrawingMode(B_OP_OVER);
			DrawBitmap(fIcons[i].get(), r.LeftTop() + BPoint(2, 2));
			SetDrawingMode(B_OP_COPY);
		}
	}

	virtual void MouseDown(BPoint where)
	{
		int32 tabs = fWindow->MajorType() == 1 ? 4 : 3;
		for (int32 i = 0; i < tabs; i++)
			if (Tab(i).Contains(where)) {
				BMessage tab(kMsgMinorType);
				tab.AddInt32(kValueString, i);
				Window()->PostMessage(&tab);
			}
	}

private:
	static BRect Tab(int32 i) { return BRect(4 + i * 24, 1, 4 + i * 24 + 19, 20); }

	TPensWindow* fWindow;
	std::unique_ptr<BBitmap> fIcons[4];
};


} // namespace


// TInkView: the active set's inks as its matrix; a click selects one and
// applies it ('sfI3', DoApplyInk), a double click edits it ('sMA3')
class TInkSetView : public BView {
public:
	TInkSetView()
		:
		BView("inks", B_WILL_DRAW)
	{
		SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	}

	void SetSet(const InkSet& set)
	{
		fSet = set;
		if (fSelected >= int32(fSet.inks.size()))
			fSelected = -1;
		BSize size(Columns() * Step() + fSet.gap, Rows() * Step() + fSet.gap);
		SetExplicitMinSize(size);
		SetExplicitPreferredSize(size);
		InvalidateLayout();
		Invalidate();
	}

	int32 Selected() const { return fSelected; }
	void Select(int32 index)
	{
		fSelected = index;
		Invalidate();
	}

	virtual void Draw(BRect)
	{
		for (size_t k = 0; k < fSet.inks.size(); k++)
			DrawInkCell(this, fSet.inks[k], Cell(k), int32(k) == fSelected);
	}

	virtual void MouseDown(BPoint where)
	{
		int32 clicks = 1;
		if (Window()->CurrentMessage() != NULL)
			Window()->CurrentMessage()->FindInt32("clicks", &clicks);
		for (size_t k = 0; k < fSet.inks.size(); k++)
			if (Cell(k).InsetByCopy(-1, -1).Contains(where)) {
				Select(int32(k));
				Window()->PostMessage(clicks > 1 ? 'sMA3' : kMsgApplyInk);
				return;
			}
	}

private:
	int32 Columns() const { return std::max<int32>(1, fSet.columns); }
	int32 Rows() const { return (int32(fSet.inks.size()) + Columns() - 1) / Columns(); }
	int32 Step() const { return fSet.cell + fSet.gap; }
	BRect Cell(size_t k) const
	{
		float x = fSet.gap + (int32(k) % Columns()) * Step();
		float y = fSet.gap + (int32(k) / Columns()) * Step();
		return BRect(x, y, x + fSet.cell - 1, y + fSet.cell - 1);
	}

	InkSet fSet;
	int32 fSelected = -1;
};


// TDialogNewColor (DIAL 65, 'sW00' with "MajorType" and "MinorType"): the
// menus (MENU 150 Kind, 151 File, 152 Edit, 153 Sets), the kinds (item 0)
// and the active set's inks (TInkView) or the pens (TPenView's
// TPensListView) in item 1
TPensWindow::TPensWindow(int32 major, int32 minor)
	:
	TDialog(65, 65, 'APPL', NULL),
	fMajor(std::max(0, std::min(2, int(major)))),
	fMinor(std::max(0, std::min(3, int(minor))))
{
	if (fMinor == 3 && fMajor != 1)
		fMinor = 0;
	sPens = this;
}


TPensWindow::~TPensWindow()
{
	delete fSavePanel;
	if (sPens == this)
		sPens = NULL;
}


// TApp::MessageReceived 'sW00': the floater shown on that kind of ink, or
// brought to the front and switched to it
void
TPensWindow::Open(int32 major, int32 minor)
{
	if (sPens != NULL) {
		BMessage change(kMsgMajorType);
		change.AddInt32(kValueString, major);
		change.AddInt32("minor", minor);
		sPens->PostMessage(&change);
		sPens->Activate();
		return;
	}
	(new TPensWindow(major, minor))->RunDialog();
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
		printf("inks: no Inks window\n");
		fflush(stdout);
		return;
	}
	BMessage script(kMsgScript);
	script.AddString("command", command.c_str());
	sPens->PostMessage(&script);
}


// TDialogNewColor::Init, InitPostRecalc
void
TPensWindow::Init()
{
	for (int32 i = 0; BWindow* window = be_app->WindowAt(i); i++)
		if (dynamic_cast<TDocWindow*>(window) != NULL)
			AddToSubset(window);
	BMenuBar* bar = new BMenuBar("mMenuBar");
	fKind = new TMenu(150);
	for (int32 i = 0; BMenuItem* item = fKind->ItemAt(i); i++) {
		BMessage* message = new BMessage(kMsgMajorType);
		message->AddInt32(kValueString, i);
		item->SetMessage(message);
	}
	bar->AddItem(fKind);
	fFile = new TMenu(151);
	bar->AddItem(fFile);
	fEdit = new TMenu(152);
	bar->AddItem(fEdit);
	fSets = new TMenu(153);
	bar->AddItem(fSets);
	if (BGroupLayout* layout = dynamic_cast<BGroupLayout*>(GetLayout()))
		layout->AddView(0, bar);
	fKinds = new TInkKindsView(this);
	FillUserItem(Item(0), fKinds);
	fInks = new TInkSetView();
	BListView* list = new BListView("pens");
	list->SetInvocationMessage(new BMessage(kMsgApplyPen));
	fPenScroll = new BScrollView("scroll", list, 0, false, true);
	if (BView* user = Item(1)) {
		// (the item's height as the contents need)
		user->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNLIMITED));
		user->SetExplicitMinSize(BSize(150, 20));
		BGroupLayout* layout = new BGroupLayout(B_VERTICAL, 0);
		user->SetLayout(layout);
		layout->AddView(fInks);
		// (TPenView: the list, its width controls right of it)
		BGroupView* pens = new BGroupView(B_HORIZONTAL, 0);
		pens->GroupLayout()->AddView(fPenScroll);
		pens->GroupLayout()->AddView(fWidth = new TPenWidthView());
		layout->AddView(pens);
	}
	ChangeMajorType(fMajor);
	ChangeMinorType(fMinor);
}


BListView*
TPensWindow::List() const
{
	return fPenScroll != NULL ? dynamic_cast<BListView*>(fPenScroll->Target()) : NULL;
}


// SetWindowTitle: STR# 5, the kind (Fill, Pen, Text Formatting), or 3
// with the pens
void
TPensWindow::SetWindowTitle()
{
	SetTitle(GobeResources::IndexString(5, fMajor == 1 && fMinor == 3 ? 3 : fMajor).String());
}


// ChangeMajorType: the pens only for the pen ink
void
TPensWindow::ChangeMajorType(int32 major)
{
	fMajor = std::max(0, std::min(2, int(major)));
	for (int32 i = 0; fKind != NULL && i < fKind->CountItems(); i++)
		fKind->ItemAt(i)->SetMarked(i == fMajor);
	if (fInks != NULL)
		fInks->Select(-1);
	if (fMajor != 1 && fMinor == 3)
		ChangeMinorType(0);
	SetWindowTitle();
	if (fKinds != NULL)
		fKinds->Invalidate();
}


// ChangeMinorType: the active set's inks or the pens; the window as large
// as they need
void
TPensWindow::ChangeMinorType(int32 minor)
{
	fMinor = std::max(0, std::min(fMajor == 1 ? 3 : 2, int(minor)));
	SetWindowTitle();
	Update();
	if (fInks != NULL && fPenScroll != NULL) {
		if (fMinor == 3) {
			if (!fInks->IsHidden())
				fInks->Hide();
			if (fPenScroll->Parent()->IsHidden())
				fPenScroll->Parent()->Show();
		} else {
			if (fInks->IsHidden())
				fInks->Show();
			if (!fPenScroll->Parent()->IsHidden())
				fPenScroll->Parent()->Hide();
			fInks->Select(-1);
		}
	}
	if (fKinds != NULL)
		fKinds->Invalidate();
	ResizeToPreferred();
}


// SwitchToSet (TInkSetManager::MakeInkSetActive)
void
TPensWindow::SwitchToSet(const std::string& name)
{
	SetActiveInkSet(fMinor, name);
	ChangeMinorType(fMinor);
}


// TPenView::UpdateContents (the front document's pens), TInkView's set
void
TPensWindow::Update()
{
	if (fMinor < 3) {
		fDocSets = FrontInkSets();
		fSet = InkSetRef();
		ActiveInkSet(fMinor, &fDocSets, fSet);
		if (fInks != NULL)
			fInks->SetSet(fSet.set);
		return;
	}
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


bool
TPensWindow::SelectedInk(std::string& set, int32& index, Ink& ink) const
{
	if (fMinor == 3 || fInks == NULL)
		return false;
	index = fInks->Selected();
	if (index < 0 || size_t(index) >= fSet.set.inks.size())
		return false;
	set = fSet.set.name;
	ink = fSet.set.inks[size_t(index)];
	return true;
}


bool
TPensWindow::SelectedPen(int32& index, PenType& pen) const
{
	BListView* list = List();
	index = fMinor == 3 && list != NULL ? list->CurrentSelection() : -1;
	if (index < 0 || size_t(index) >= fPens.size())
		return false;
	pen = fPens[size_t(index)];
	return true;
}


// TDialogNewColor::MenusBeginning: Edit's New and Edit (STR# 8: the
// kind's), Clear and Edit with an ink or pen selected (Edit for the pens
// that can be edited); File and Sets only for inks, Sets naming the kind's
// sets with the active one marked (FixSetMenu, BuildInkSetPopup 'sMA7')
void
TPensWindow::MenusBeginning()
{
	TDialog::MenusBeginning();
	std::string setName;
	int32 index = -1;
	Ink ink;
	PenType pen;
	bool selected = fMinor == 3 ? SelectedPen(index, pen) : SelectedInk(setName, index, ink);
	bool editable = fMinor == 3 ? selected && pen.kind > 0 : selected;
	if (fEdit != NULL) {
		if (BMenuItem* item = fEdit->FindItem('sMA4'))
			item->SetLabel(GobeResources::IndexString(8, fMinor).String());
		if (BMenuItem* item = fEdit->FindItem('sMA3'))
			item->SetLabel(GobeResources::IndexString(8, fMinor + 8).String());
		for (int32 i = 0; BMenuItem* item = fEdit->ItemAt(i); i++) {
			uint32 what = item->Message() != NULL ? item->Message()->what : 0;
			bool on = (what == 'sMA4' && true)
				|| (what == 'sM12' && selected)
				|| (what == 'sMA3' && editable && true);
			if (what == B_CUT || what == B_COPY || what == B_PASTE)
				on = Clipboard(what, false);
			item->SetEnabled(on);
		}
	}
	if (fFile != NULL) {
		fFile->SetEnabled(fMinor < 3);
		for (int32 i = 0; BMenuItem* item = fFile->ItemAt(i); i++)
			item->SetEnabled(item->Message() != NULL && (item->Message()->what != 'sM04'
				|| !fSet.path.empty()));
	}
	if (fSets != NULL) {
		while (BMenuItem* item = fSets->RemoveItem(int32(0)))
			delete item;
		fSets->SetEnabled(fMinor < 3);
		std::string active = ActiveInkSetName(fMinor, &fDocSets);
		for (const InkSetRef& set : InkSetsOfKind(fMinor, &fDocSets)) {
			const std::string& name = set.set.name;
			BMessage* message = new BMessage('sMA7');
			message->AddString("ink name", name.c_str());
			BMenuItem* item = new BMenuItem(name.c_str(), message);
			item->SetMarked(name == active);
			fSets->AddItem(item);
		}
	}
}


// DoNewPen, DoEditPen (TPenFactory::EditPenDialog)
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


SetMgr
TPensWindow::FrontInkSets()
{
	SetMgr sets;
	TDocWindow* window = TDocWindow::Front();
	if (window != NULL && window->LockWithTimeout(100000) == B_OK) {
		sets = window->InkSets();
		window->Unlock();
	}
	return sets;
}


void
TPensWindow::StoreSet(const InkSetRef& set, const std::string& name)
{
	if (set.document) {
		TDocWindow* front = TDocWindow::Front();
		if (front == NULL)
			return;
		BMessage change(kMsgDocInkSet);
		change.AddInt32("kind", set.set.kind);
		change.AddString("ink name", name.c_str());
		std::vector<uint8_t> bytes = InkSetBytes(set.set);
		change.AddData("set", B_RAW_TYPE, bytes.data(), ssize_t(bytes.size()));
		front->PostMessage(&change);
		// (the document lists it again: Refresh)
		return;
	}
	std::string error;
	if (!ChangeInkSet(set, error))
		(new BAlert("", GobeResources::IndexString(6, 7).String(), "OK"))->Go(NULL);
	Update();
}


// DoNewInk, DoEditInk (TDialogNewColor::DoEditInkDialog): the ink
// editors' dialog, replying 'sfI0' or 'sfI2' with "ink name" (and "ink
// index")
void
TPensWindow::EditInk(bool make)
{
	std::string set;
	int32 index = -1;
	Ink ink;
	bool selected = SelectedInk(set, index, ink);
	if (!make && !selected)
		return;
	BMessage reply(make ? kMsgAddInk : kMsgReplaceInk);
	reply.AddString("ink name", fSet.set.name.c_str());
	if (!make)
		reply.AddInt32("ink index", index);
	gobe::EditInk(fMinor, make ? NULL : &ink, this, reply);
}


// Cut, Copy, Paste (DoCopy, DoClear, DoPaste): the selected ink or pen
// kept (InkClipboard); Paste adds the ink copied to the set after the
// selected one, or the pen copied to the front document's pens (a colour
// pasted from elsewhere becomes the ink copied: "RGBColor"); `run` false
// asks whether it's on
bool
TPensWindow::Clipboard(uint32 what, bool run)
{
	if (what == B_PASTE && run && CurrentMessage() != NULL)
		InkClipboard::TakeColor(CurrentMessage());
	if (fMinor == 3) {
		int32 index = -1;
		PenType pen;
		bool selected = SelectedPen(index, pen) && pen.kind > 0;
		TDocWindow* front = TDocWindow::Front();
		if (what == B_PASTE) {
			if (InkClipboard::Kind() != 3 || !InkClipboard::HasPen())
				return false;
			if (run && front != NULL) {
				BMessage add(kMsgAddPen);
				AddPenToMessage(&add, InkClipboard::CopiedPen());
				front->PostMessage(&add);
			}
			return true;
		}
		if (!selected)
			return false;
		if (run) {
			InkClipboard::CopyPen(pen);
			if (what == B_CUT && front != NULL) {
				BMessage clear(kMsgDeletePen);
				clear.AddInt32("ink index", index);
				front->PostMessage(&clear);
			}
		}
		return true;
	}
	std::string name;
	int32 index = -1;
	Ink ink;
	bool selected = SelectedInk(name, index, ink);
	if (what == B_PASTE) {
		if (InkClipboard::Kind() != fMinor || !InkClipboard::HasInk())
			return false;
		if (run) {
			InkSetRef set = fSet;
			if (selected)
				set.set.inks.insert(set.set.inks.begin() + index + 1, InkClipboard::CopiedInk());
			else
				set.set.inks.push_back(InkClipboard::CopiedInk());
			StoreSet(set, fSet.set.name);
		}
		return true;
	}
	if (!selected)
		return false;
	if (run) {
		InkClipboard::CopyInk(fMinor, ink);
		if (what == B_CUT) {
			InkSetRef set = fSet;
			set.set.inks.erase(set.set.inks.begin() + index);
			StoreSet(set, fSet.set.name);
		}
	}
	return true;
}


// the File menu: New Set… (DoNewSet: a document's own set, empty, made
// active), Save Set (DoSave: a file's to its file, else as Save Set As),
// Save Set As… (DoSaveAs: a file panel in the settings folder's "Ink
// Sets"), Revert To Saved Set (DoRevert: the file read again), Set
// Dimensions… (DoConfigureSet)
void
TPensWindow::FileCommand(BMessage* message)
{
	switch (message->what) {
		case 'sMA5':
		{
			if (!message->HasString("ink name")) {
				(new TDialogSetName(this, fDocSets, fMinor))->RunDialog();
				return;
			}
			InkSetRef set;
			set.document = true;
			set.set.kind = fMinor;
			set.set.name = message->GetString("ink name", "");
			TDocWindow* front = TDocWindow::Front();
			if (front == NULL)
				return;
			BMessage add(kMsgDocInkSet);
			add.AddInt32("kind", fMinor);
			add.AddBool("add", true);
			std::vector<uint8_t> bytes = InkSetBytes(set.set);
			add.AddData("set", B_RAW_TYPE, bytes.data(), ssize_t(bytes.size()));
			front->PostMessage(&add);
			SetActiveInkSet(fMinor, set.set.name);
			return;
		}
		case 'sM02':
			if (!fSet.path.empty()) {
				std::string error;
				if (!SaveInkSetFile(fSet.path, fSet.set, error))
					(new BAlert("", GobeResources::IndexString(6, 7).String(), "OK"))->Go(NULL);
				return;
			}
			// (no file: Save Set As…)
		case 'sM03':
		{
			std::vector<std::string> folders = InkSetFolders();
			BPath folder(folders.empty() ? "/boot/home" : folders.back().c_str());
			create_directory(folder.Path(), 0755);
			entry_ref ref;
			get_ref_for_path(folder.Path(), &ref);
			delete fSavePanel;
			BMessage save(kMsgSaveSet);
			fSavePanel = new BFilePanel(B_SAVE_PANEL, new BMessenger(this), &ref, 0, false,
				&save);
			fSavePanel->SetSaveText(fSet.set.name.c_str());
			fSavePanel->Show();
			return;
		}
		case 'sM04':
			ReloadInkSets();
			Update();
			return;
		case 'sMA6':
		{
			if (!message->HasInt32("columns")) {
				(new TDialogSetDimensions(this, fSet.set))->RunDialog();
				return;
			}
			InkSetRef set = fSet;
			set.set.columns = message->GetInt32("columns", set.set.columns);
			set.set.cell = message->GetInt32("cellSize", set.set.cell);
			set.set.gap = message->GetInt32("gapSize", set.set.gap);
			StoreSet(set, fSet.set.name);
			ChangeMinorType(fMinor);
			return;
		}
	}
}


void
TPensWindow::MessageReceived(BMessage* message)
{
	BListView* list = List();
	int32 selected = list != NULL && fMinor == 3 ? list->CurrentSelection() : -1;
	TDocWindow* front = TDocWindow::Front();
	switch (message->what) {
		case kMsgRefresh:
			Update();
			return;
		case kMsgMajorType:
			ChangeMajorType(message->GetInt32(kValueString, fMajor));
			if (message->HasInt32("minor"))
				ChangeMinorType(message->GetInt32("minor", fMinor));
			return;
		case kMsgMinorType:
			ChangeMinorType(message->GetInt32(kValueString, fMinor));
			return;
		case 'sMA7':
			SwitchToSet(message->GetString("ink name", ""));
			return;
		case 'sMA4':
		case 'sMA3':
			if (fMinor == 3)
				EditPen(message->what == 'sMA4');
			else
				EditInk(message->what == 'sMA4');
			return;
		case 'sM12':
			// Clear (DoClear: TPenMgr::DeletePen; an ink: the set's)
			if (fMinor < 3) {
				int32 index = fInks != NULL ? fInks->Selected() : -1;
				if (index >= 0 && size_t(index) < fSet.set.inks.size()) {
					InkSetRef set = fSet;
					set.set.inks.erase(set.set.inks.begin() + index);
					StoreSet(set, fSet.set.name);
				}
			} else if (selected >= 0 && front != NULL) {
				BMessage clear(kMsgDeletePen);
				clear.AddInt32("ink index", selected);
				front->PostMessage(&clear);
			}
			return;
		case B_CUT:
		case B_COPY:
		case B_PASTE:
			Clipboard(message->what, true);
			return;
		case kMsgAddInk:
		case kMsgReplaceInk:
		{
			// DoAddInk, DoReplaceInk: the ink dialog's ink into the set
			const void* data = NULL;
			ssize_t size = 0;
			Ink ink;
			if (message->FindData("ink", B_RAW_TYPE, &data, &size) != B_OK
					|| !InkFromBytes(std::vector<uint8_t>((const uint8_t*)data,
						(const uint8_t*)data + size), ink))
				return;
			InkSetRef set = fSet;
			if (message->GetString("ink name", "") != set.set.name)
				return;
			int32 index = message->GetInt32("ink index", -1);
			if (message->what == kMsgAddInk)
				set.set.inks.push_back(ink);
			else if (index >= 0 && size_t(index) < set.set.inks.size())
				set.set.inks[size_t(index)] = ink;
			else
				return;
			StoreSet(set, fSet.set.name);
			return;
		}
		case kMsgSaveSet:
		{
			// DoSave: the set saved as the file chosen, made active
			entry_ref directory;
			const char* name = NULL;
			if (message->FindRef("directory", &directory) != B_OK
					|| message->FindString("name", &name) != B_OK)
				return;
			BPath path(&directory);
			path.Append(name);
			InkSet set = fSet.set;
			set.name = name;
			std::string error;
			if (!SaveInkSetFile(path.Path(), set, error)) {
				(new BAlert("", GobeResources::IndexString(6, 7).String(), "OK"))->Go(NULL);
				return;
			}
			ReloadInkSets();
			SetActiveInkSet(fMinor, name);
			Update();
			return;
		}
		case kMsgApplyInk:
		{
			// DoApplyInk: the ink to the front document's selection as
			// the kind's command (fill, pen, text: colour, pattern,
			// gradient)
			static const uint32 kCommands[3][3] = {{'sf03', 'sf04', 'sf05'},
				{'sf06', 'sf07', 'sf08'}, {'sf02', 'sf0M', 'sf0N'}};
			int32 index = fInks != NULL ? fInks->Selected() : -1;
			if (fMinor < 3 && index >= 0 && front != NULL) {
				std::unique_ptr<BMessage> apply(InkSetMessage(BMessage(kCommands[fMajor][fMinor]),
					fSet.set, size_t(index)));
				front->PostMessage(apply.get());
			}
			return;
		}
		case 'sMA5':
		case 'sM02':
		case 'sM03':
		case 'sM04':
		case 'sMA6':
			// File: New Set…, Save Set, Save Set As…, Revert To Saved Set,
			// Set Dimensions…
			if (fMinor < 3)
				FileCommand(message);
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
			if (command.compare(0, 7, "select ") == 0) {
				int32 index = atoi(command.c_str() + 7);
				if (fMinor == 3 && list != NULL)
					list->Select(index);
				else if (fInks != NULL)
					fInks->Select(index);
			} else if (command.compare(0, 5, "kind ") == 0)
				ChangeMajorType(atoi(command.c_str() + 5));
			else if (command.compare(0, 4, "tab ") == 0)
				ChangeMinorType(atoi(command.c_str() + 4));
			else if (command.compare(0, 4, "set ") == 0)
				SwitchToSet(command.substr(4));
			else if (command == "new" || command == "edit") {
				MenusBeginning();
				BMessage edit(command == "new" ? 'sMA4' : 'sMA3');
				MessageReceived(&edit);
			} else if (command == "clear" || command == "cut" || command == "copy"
					|| command == "paste") {
				BMessage edit(command == "clear" ? 'sM12' : command == "cut" ? B_CUT
					: command == "copy" ? B_COPY : B_PASTE);
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
				gobe::EditInk(atoi(command.c_str() + 8), NULL, this, BMessage('GieR'));
			} else if (command.compare(0, 7, "editor ") == 0) {
				InkEditorScript(command.substr(7));
			} else if (command == "apply") {
				BMessage apply(fMinor == 3 ? kMsgApplyPen : kMsgApplyInk);
				MessageReceived(&apply);
			} else if (command == "show") {
				MenusBeginning();
				printf("inks \"%s\" kind %d tab %d set \"%s\" inks %d\n", Title(), int(fMajor),
					int(fMinor), fMinor < 3 ? fSet.set.name.c_str() : "", int(fSet.set.inks.size()));
				for (int32 i = 0; fSets != NULL && i < fSets->CountItems(); i++)
					printf("inks set %s%s\n", fSets->ItemAt(i)->Label(),
						fSets->ItemAt(i)->IsMarked() ? " (marked)" : "");
				for (size_t i = 0; fMinor == 3 && i < fPens.size(); i++)
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
