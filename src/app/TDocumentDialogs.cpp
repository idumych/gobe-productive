// Document dialogs: Margins (TDialogFormatDocument).
#include "TDocumentDialogs.h"

#include <stdio.h>

#include <Alert.h>
#include <Button.h>
#include <GroupLayout.h>
#include <ListView.h>
#include <MenuBar.h>
#include <MenuItem.h>
#include <ScrollView.h>
#include <StringView.h>
#include <Application.h>
#include <Window.h>
#include <Looper.h>
#include <Message.h>

#include "Document.h"
#include "GobeResources.h"
#include "Measure.h"
#include "TDocWindow.h"
#include "TMenu.h"

namespace gobe {

static const uint32 kCmdMargins = 'sM22';


TDialogFormatDocument::TDialogFormatDocument(BLooper* window, const Document& document)
	:
	TDialog(20, 20, 'APPL', NULL),
	fWindow(window),
	fUnit(0),
	fMirror(false),
	fShowMargins(true),
	fShowGuides(true)
{
	document.Margins(fMargins);
	for (int i = 0; i < 4; i++)
		fPaper[i] = 0;
	if (!document.sheets.sheets.empty()) {
		const LeafSheet& sheet = document.sheets.sheets[0];
		std::copy(sheet.paper, sheet.paper + 4, fPaper);
		fUnit = int(sheet.unit);
		fShowMargins = sheet.flags[0];
		fShowGuides = sheet.flags[1];
		fMirror = sheet.flags[2];
	}
	for (int32 i = 0; i < 6; i++)
		fUnitNames.push_back(GobeResources::IndexString(7, i).String());
}


void
TDialogFormatDocument::Init()
{
	// the margins in the sheet's unit, with its name, two decimals
	for (int32 i = 0; i < 4; i++)
		SetTextItem(3 + i, MeasureText(fMargins[i], fUnit, kMeasureWithUnit | kMeasureTwoPlaces,
			fUnitNames).c_str());
	SetCheckBoxItem(8, fShowGuides);
	MakeFocusItem(3);
}


// the largest margin a field may have: the paper less the opposite margin
float
TDialogFormatDocument::Maximum(int32 index) const
{
	float width = fPaper[2] - fPaper[0], height = fPaper[3] - fPaper[1];
	switch (index) {
		case 3: return width - fMargins[2];
		case 4: return height - fMargins[3];
		case 5: return width - fMargins[0];
		case 6: return height - fMargins[1];
	}
	return 0;
}


// TDialogFormatDocument::validTextControl: a length from 0 to the maximum;
// otherwise the original's range alert (STR# 6, 15)
bool
TDialogFormatDocument::ValidTextControl(int32 index, float& value)
{
	float maximum = Maximum(index);
	float points;
	if (ParseMeasure(TextItem(index).String(), fUnit, fUnitNames, false, false, points)
			&& points >= 0 && points <= maximum) {
		value = points;
		return true;
	}
	BString text;
	text.SetToFormat(GobeResources::IndexString(6, 15).String(),
		MeasureText(0, fUnit, kMeasureWithUnit | kMeasureTwoPlaces, fUnitNames).c_str(),
		MeasureText(maximum, fUnit, kMeasureWithUnit | kMeasureTwoPlaces, fUnitNames).c_str());
	(new BAlert("Margins", text.String(), "OK"))->Go();
	MakeFocusItem(index);
	return false;
}


bool
TDialogFormatDocument::ItemHit(int32 index, BMessage* message)
{
	switch (index) {
		case 0: {
			for (int32 i = 3; i <= 6; i++)
				if (!ValidTextControl(i, fMargins[i - 3]))
					return true;
			BMessage margins(kCmdMargins);
			margins.AddRect("Margins", BRect(fMargins[0], fMargins[1], fMargins[2],
				fMargins[3]));
			margins.AddBool("MirrorFacingPages", fMirror);
			margins.AddBool("ShowMargins", fShowMargins);
			margins.AddBool("ShowPageGuides", fShowGuides);
			fWindow->PostMessage(&margins);
			PostMessage(B_QUIT_REQUESTED);
			return false;
		}
		case 1:
			PostMessage(B_QUIT_REQUESTED);
			return false;
		case 3:
		case 4:
		case 5:
		case 6:
			return !ValidTextControl(index, fMargins[index - 3]);
		case 8:
			fShowGuides = CheckBoxItem(8);
			return false;
	}
	return false;
}

// #pragma mark - TDialogHistory


namespace {

TDialogHistory* sHistory = NULL;

BListView*
HistoryList(BView* view)
{
	if (BScrollView* scroll = dynamic_cast<BScrollView*>(view))
		return dynamic_cast<BListView*>(scroll->Target());
	return dynamic_cast<BListView*>(view);
}

} // namespace


TDialogHistory::TDialogHistory()
	:
	TDialog(36, 36, 'APPL', NULL)
{
	sHistory = this;
}


TDialogHistory::~TDialogHistory()
{
	if (sHistory == this)
		sHistory = NULL;
}


TDialogHistory*
TDialogHistory::Open()
{
	return sHistory;
}


void
TDialogHistory::Choose(int32 row)
{
	if (sHistory == NULL)
		return;
	BMessage choose('Gchs');
	choose.AddInt32("row", row);
	sHistory->PostMessage(&choose);
}


void
TDialogHistory::Refresh()
{
	if (sHistory != NULL)
		sHistory->PostMessage('sD=L');
}


// TDialogHistory::Init: the Options menu (MENU 230) in a menu bar; the
// list's font 9
void
TDialogHistory::Init()
{
	// TFloaterMgr::AddFloater: the document windows in its subset (B_FLOATING_
	// SUBSET_WINDOW_FEEL)
	for (int32 i = 0; BWindow* window = be_app->WindowAt(i); i++)
		if (dynamic_cast<TDocWindow*>(window) != NULL)
			AddToSubset(window);
	BMenuBar* bar = new BMenuBar("mMenuBar");
	fOptions = new TMenu(230);
	bar->AddItem(fOptions);
	if (BGroupLayout* layout = dynamic_cast<BGroupLayout*>(GetLayout()))
		layout->AddView(0, bar);
	if (BListView* list = HistoryList(Item(3))) {
		BFont font;
		list->GetFont(&font);
		font.SetSize(9);
		list->SetFont(&font);
		BMessage* select = new BMessage('APPL' + 3);
		select->AddInt32("dialog", 36);
		select->AddInt32("index", 3);
		select->AddBool("Select", true);
		list->SetSelectionMessage(select);
	}
	Update();
}


// TDialogHistory::UpdateContents
void
TDialogHistory::Update()
{
	std::vector<std::string> labels;
	size_t done = 0;
	bool show = false;
	TDocWindow* window = TDocWindow::Front();
	if (window != NULL && window->LockWithTimeout(100000) == B_OK) {
		window->HistoryState(labels, done, show);
		window->Unlock();
	}
	fDone = done;
	fCount = labels.size();
	fShow = show;
	BString count;
	count.SetToFormat(GobeResources::IndexString(5, 9).String(), int(labels.size()));
	SetTextItem(2, count.String());
	SetItemEnabled(0, done > 0);
	SetItemEnabled(1, done < labels.size());
	BListView* list = HistoryList(Item(3));
	if (list == NULL)
		return;
	BMessage* select = list->SelectionMessage() != NULL
		? new BMessage(*list->SelectionMessage()) : NULL;
	list->SetSelectionMessage(NULL);
	while (BListItem* item = list->RemoveItem(int32(0)))
		delete item;
	list->AddItem(new BStringItem(GobeResources::IndexString(5, 18).String()));
	for (const std::string& label : labels)
		list->AddItem(new BStringItem(label.c_str()));
	list->Select(int32(done), false);
	list->ScrollToSelection();
	list->SetSelectionMessage(select);
}


void
TDialogHistory::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case 'sD=L':
			Update();
			return;
		case 'Gchs':
			if (BListView* list = HistoryList(Item(3)))
				list->Select(message->GetInt32("row", 0));
			return;
		// TDialogWindow: a new document window in the subset
		case 'sE05':
		{
			BWindow* window;
			if (message->FindPointer("window", (void**)&window) == B_OK)
				AddToSubset(window);
			return;
		}
		// the Options menu (TDialogHistory::ItemHit 0xffff): to the front
		// window
		case 'sM2D':
		case 'sM2E':
		case 'sM2F':
			if (message->what == 'sM2F')
				fShow = !fShow;
			if (TDocWindow* window = TDocWindow::Front())
				window->PostMessage(message);
			return;
	}
	TDialog::MessageReceived(message);
}


// TDialogHistory::MenusBeginning: Clear History with steps, Show Undo
// Steps marked when on
void
TDialogHistory::MenusBeginning()
{
	TDialog::MenusBeginning();
	if (fOptions == NULL)
		return;
	if (BMenuItem* item = fOptions->FindItem('sM2E'))
		item->SetEnabled(fCount > 0);
	if (BMenuItem* item = fOptions->FindItem('sM2F'))
		item->SetMarked(fShow);
}


bool
TDialogHistory::ItemHit(int32 index, BMessage* message)
{
	TDocWindow* window = TDocWindow::Front();
	switch (index) {
		case 0:
		case 1:
			if (window != NULL)
				window->PostMessage(index == 0 ? 'sM10' : 'sM11');
			return true;
		case 3:
		{
			BListView* list = HistoryList(Item(3));
			int32 selected = list != NULL ? list->CurrentSelection() : -1;
			if (selected < 0 || window == NULL) {
				Update();
				return true;
			}
			// Option: the other way of showing the steps
			bool show = fShow;
			if (modifiers() & B_OPTION_KEY)
				show = !show;
			BMessage step(size_t(selected) < fDone ? 'sM10' : 'sM11');
			int32 count = size_t(selected) < fDone ? int32(fDone - size_t(selected))
				: int32(size_t(selected) - fDone);
			if (count == 0)
				return true;
			step.AddInt32(kValueString, count);
			step.AddBool("show", show);
			window->PostMessage(&step);
			return true;
		}
	}
	return true;
}


// #pragma mark - TDialogHistorySize


TDialogHistorySize::TDialogHistorySize(BLooper* window, int32 levels)
	:
	TDialog(75, 75, 'APPL', NULL),
	fWindow(window),
	fLevels(levels)
{
}


void
TDialogHistorySize::Init()
{
	SetTextItem(2, std::to_string(fLevels).c_str());
	MakeFocusItem(2);
}


bool
TDialogHistorySize::ItemHit(int32 index, BMessage* message)
{
	if (index == 0) {
		BString text = TextItem(2);
		char* end;
		long levels = strtol(text.String(), &end, 10);
		if (end == text.String() || *end != '\0' || levels < 1 || levels > 999) {
			BString alert;
			alert.SetToFormat(GobeResources::IndexString(6, 13).String(), 1, 999);
			(new BAlert("", alert.String(), "OK"))->Go(NULL);
			MakeFocusItem(2);
			return true;
		}
		BMessage set('sM2D');
		set.AddInt32(kValueString, int32(levels));
		fWindow->PostMessage(&set);
	}
	return TDialog::ItemHit(index, message);
}

} // namespace gobe
