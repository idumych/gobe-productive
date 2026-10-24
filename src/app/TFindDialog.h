// Document > Find/Change > Find/Replace… (TDialogFindChange, DIAL/Item 7)
// and the application's find and replace (TFindReplace, OurApp +0x1cc).
#ifndef GOBE_TFINDDIALOG_H
#define GOBE_TFINDDIALOG_H

#include "Find.h"
#include "TDialog.h"

class BTextView;

namespace gobe {

class TDocWindow;

// TFindReplace: one for every window (Ignore Case, Entire Word off, Wrap
// on at first)
FindReplace& AppFindReplace();

// TDialogFindChange: Find (0) and Replace (1), the special characters shown
// as \t, \l, ... (Insert Special, 5, puts them in the field last typed in);
// Ignore Case (2), Entire Word (3), Wrap (4) set at once; Find (6),
// Replace (7), Replace & Find (8) and Replace All (9, which closes it) send
// the front window Find/Change's commands. Its buttons follow the find
// text and the front window's selection. One at a time.
class TDialogFindChange : public TDialog {
public:
	TDialogFindChange();
	virtual ~TDialogFindChange();

	virtual void MessageReceived(BMessage* message);

	// TFindReplace +0x8: the dialog if it's open
	static TDialogFindChange* Open();

protected:
	virtual void Init();
	virtual bool ItemHit(int32 index, BMessage* message);

private:
	// TDialogFindChange::updateButtons
	void UpdateButtons();
	BTextView* FieldView(int32 index) const;

	BTextView* fLast = NULL;		// +0x4c
};

} // namespace gobe

#endif
