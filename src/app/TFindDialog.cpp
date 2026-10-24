// The Find dialog.
#include "TFindDialog.h"

#include <Button.h>
#include <CheckBox.h>
#include <TextControl.h>
#include <TextView.h>

#include "GobeResources.h"
#include "TDocWindow.h"

namespace gobe {

namespace {

TDialogFindChange* sOpen = NULL;

const uint32 kCommand = 'APPL';

} // namespace


FindReplace&
AppFindReplace()
{
	static FindReplace sFind;
	return sFind;
}


TDialogFindChange::TDialogFindChange()
	:
	TDialog(7, 7, kCommand, NULL)
{
	sOpen = this;
}


TDialogFindChange::~TDialogFindChange()
{
	if (sOpen == this)
		sOpen = NULL;
}


TDialogFindChange*
TDialogFindChange::Open()
{
	return sOpen;
}


BTextView*
TDialogFindChange::FieldView(int32 index) const
{
	BTextControl* edit = dynamic_cast<BTextControl*>(Item(index));
	return edit != NULL ? edit->TextView() : NULL;
}


// TDialogFindChange::Init: the options and strings as they are; the
// fields report each change
void
TDialogFindChange::Init()
{
	FindReplace& find = AppFindReplace();
	SetCheckBoxItem(2, !find.caseSensitive);
	SetCheckBoxItem(3, find.wholeWord);
	SetCheckBoxItem(4, find.wrap);
	for (int32 i = 0; i < 2; i++) {
		if (BTextControl* edit = dynamic_cast<BTextControl*>(Item(i))) {
			BMessage* modified = new BMessage(*edit->Message());
			modified->AddBool("isMod", true);
			edit->SetModificationMessage(modified);
		}
	}
	SetTextItem(0, FindToDisplay(find.find).c_str());
	SetTextItem(1, FindToDisplay(find.replace).c_str());
	UpdateButtons();
	MakeFocusItem(0);
	fLast = FieldView(0);
}


void
TDialogFindChange::MessageReceived(BMessage* message)
{
	// TFindReplace::DoMessage's 'sD=L': the buttons again
	if (message->what == 'sD=L') {
		UpdateButtons();
		return;
	}
	// Insert Special's items (MENU 240's value message)
	if (message->what == kCommand + 5 && !message->HasInt32("dialog")) {
		ItemHit(5, message);
		return;
	}
	TDialog::MessageReceived(message);
}


bool
TDialogFindChange::ItemHit(int32 index, BMessage* message)
{
	FindReplace& find = AppFindReplace();
	TDocWindow* window = TDocWindow::Front();
	switch (index) {
		case 0:
		case 1:
		{
			BTextView* view = FieldView(index);
			if (view == NULL)
				return true;
			fLast = view;
			// pasted or typed specials shown as escapes
			if (FindHasSpecials(view->Text()))
				view->SetText(FindToDisplay(view->Text()).c_str());
			std::string text = FindFromDisplay(view->Text(), index == 1);
			if (index == 0)
				find.SetFind(text);
			else
				find.replace = text;
			UpdateButtons();
			return true;
		}
		case 2:
			find.SetCaseSensitive(!CheckBoxItem(2));
			return true;
		case 3:
			find.wholeWord = CheckBoxItem(3);
			return true;
		case 4:
			find.wrap = CheckBoxItem(4);
			return true;
		case 5:
		{
			static const char kSpecials[] = "tlnfpm";
			int32 value = 0;
			message->FindInt32(kValueString, &value);
			if (value < 0 || value > 5)
				return true;
			char escape[3] = {'\\', kSpecials[value], 0};
			BTextView* view = dynamic_cast<BTextView*>(CurrentFocus());
			if (view != FieldView(0) && view != FieldView(1))
				view = fLast;
			if (view == NULL)
				return true;
			int32 from, to;
			view->GetSelection(&from, &to);
			if (from != to)
				view->Delete(from, to);
			view->Insert(from, escape, 2);
			view->Select(from + 2, from + 2);
			return true;
		}
		case 6:
		case 7:
		case 8:
		case 9:
		{
			static const uint32 kCommands[4] = {'sME1', 'sME5', 'sME3', 'sME4'};
			if (window != NULL) {
				window->PostMessage('sE02');
				window->PostMessage(kCommands[index - 6]);
			}
			if (index == 9)
				PostMessage(B_QUIT_REQUESTED);
			return true;
		}
	}
	return TDialog::ItemHit(index, message);
}


// Find with a find string; Replace All with a different replacement too;
// Replace with selected text; Replace & Find with both
void
TDialogFindChange::UpdateButtons()
{
	bool findText = TextItem(0).Length() > 0;
	bool different = TextItem(0) != TextItem(1);
	bool selection = false;
	if (TDocWindow* window = TDocWindow::Front()) {
		if (window->LockWithTimeout(100000) == B_OK) {
			selection = window->HasTextSelection();
			window->Unlock();
		}
	}
	auto enable = [&](int32 index, bool on) {
		if (BButton* button = dynamic_cast<BButton*>(Item(index)))
			button->SetEnabled(on);
	};
	enable(6, findText);
	enable(9, findText && different);
	enable(7, selection);
	enable(8, findText && selection);
}

} // namespace gobe
