// Document dialogs: Margins (TDialogFormatDocument), History
// (TDialogHistory, TDialogHistorySize).
#ifndef GOBE_TDOCUMENT_DIALOGS_H
#define GOBE_TDOCUMENT_DIALOGS_H

#include <string>
#include <vector>

#include "TDialog.h"

class BLooper;

namespace gobe {

struct Document;

// Document > Margins… (DIAL/Item 20, "Document Margins"): the first
// sheet's margins in its unit and "Show Page Guides"; OK sends the window
// 'sM22' with "Margins" (left, top, right, bottom), "MirrorFacingPages",
// "ShowMargins" and "ShowPageGuides".
class TDialogFormatDocument : public TDialog {
public:
	TDialogFormatDocument(BLooper* window, const Document& document);

protected:
	virtual void Init();
	virtual bool ItemHit(int32 index, BMessage* message);

private:
	bool ValidTextControl(int32 index, float& value);
	float Maximum(int32 index) const;

	BLooper* fWindow;
	float fMargins[4];		// left, top, right, bottom
	float fPaper[4];
	int fUnit;
	bool fMirror, fShowMargins, fShowGuides;
	std::vector<std::string> fUnitNames;
};

// Document > History… (DIAL/Item 36, a floater; Options, MENU 230):
// "START" (STR# 5, 18) and the front document's steps, the last done
// selected; "Commands: N" (STR# 5, 9); Undo and Redo; choosing a step
// undoes or redoes to it (shown step by step with Show Undo Steps,
// Option the other way). Options' commands go to the front window.
class TDialogHistory : public TDialog {
public:
	TDialogHistory();
	virtual ~TDialogHistory();

	virtual void MessageReceived(BMessage* message);
	virtual void MenusBeginning();

	// TDialogHistory::UpdateContents (the floater's, if it's open) with
	// the front document's steps
	static void Refresh();
	static TDialogHistory* Open();
	// test hook: row `row` chosen in the open floater's list
	static void Choose(int32 row);

protected:
	virtual void Init();
	virtual bool ItemHit(int32 index, BMessage* message);

private:
	void Update();

	size_t fDone = 0;
	size_t fCount = 0;
	bool fShow = false;
	class TMenu* fOptions = NULL;
};

// Options > Set Undo Levels… (DIAL/Item 75): the steps kept, 1 to 999
// (else STR# 6, 13); OK sends `window` 'sM2D' with them.
class TDialogHistorySize : public TDialog {
public:
	TDialogHistorySize(BLooper* window, int32 levels);

protected:
	virtual void Init();
	virtual bool ItemHit(int32 index, BMessage* message);

private:
	BLooper* fWindow;
	int32 fLevels;
};

} // namespace gobe

#endif
