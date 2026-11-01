// The Status window (TStatusDialog, DIAL/Item 71): while a file is
// imported or exported, "<kind>: importing “<file>”" (STR# 2, 19) or
// "<kind>: exporting to “<file>”" (2, 21) over a bar (item 3), the
// translator format's name and MIME type (items 1 and 2) and Cancel
// (item 0). The work runs on the caller's thread and reports through
// Update(); the window draws on its own.
#ifndef GOBE_TSTATUSDIALOG_H
#define GOBE_TSTATUSDIALOG_H

#include <atomic>

#include "TDialog.h"

class BStatusBar;

namespace gobe {

class TStatusDialog : public TDialog {
public:
	TStatusDialog(const char* title, const char* formatName, const char* formatType);

	// TStatusDialog::UpdateStatus, from the working thread: the bar to
	// `fraction` of the percent range (shown at most every tenth of a
	// second, and always for 0); false once Cancel was pressed
	bool Update(float fraction);
	// TStatusDialog::SetPercentRange: the part of the bar a step fills
	void SetPercentRange(float start, float end);
	bool WasCancelled() const { return fCancelled; }
	// TStatusDialog::FinishBar, then the window closes
	void Finish();

	// the title: STR# 2, 19 (import) or 21 (export) with the main part's
	// kind ("Word Processing", "Spreadsheet") and the file's name
	static BString Title(bool import, const char* kind, const char* fileName);

protected:
	virtual void Init();
	virtual bool ItemHit(int32 index, BMessage* message);

private:
	BString fTitle;
	BString fFormatName;
	BString fFormatType;
	BStatusBar* fBar = NULL;
	float fStart = 0, fEnd = 1;
	bigtime_t fLastUpdate = 0;
	std::atomic<bool> fCancelled{false};
};

} // namespace gobe

#endif
