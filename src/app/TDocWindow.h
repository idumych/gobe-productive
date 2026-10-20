// TDocWindow: the document window. For now: the base menu bar the original
// builds in TNormalDocWinHelper::InstallBaseMenus, a debug menu that opens any
// DIAL resource, and a status line showing the commands menus send (none are
// wired to document code yet).
#ifndef GOBE_TDOCWINDOW_H
#define GOBE_TDOCWINDOW_H

#include <MenuBar.h>
#include <Window.h>


#include <string>

#include "TDialog.h"

class BFilePanel;
class BHandler;
class BMenu;
class BMenuItem;
class BStringView;

namespace gobe {

struct Document;
class TPageView;

// The document window's menu bar ("Main Menu Bar" in the original).
class TMenuBar : public BMenuBar {
public:
	TMenuBar() : BMenuBar("Main Menu Bar") {}

	// test hook: open menu `index` as if clicked
	void OpenMenu(int32 index) { StartMenuBar(index, true, true); }
};

// TFontMenu: a font menu built dynamically from system font families.
class TFontMenu : public BMenu {
public:
	TFontMenu();
	// test hook: rebuild the list from system fonts
	void Rebuild();
	BMenuItem* CurrentFamily() const { return fCurrentFamily; }
	void SetCurrentFamily(BMenuItem* item) { fCurrentFamily = item; }

private:
	BMenuItem* fCurrentFamily = NULL;
};

// TFindDialog: the Find/Replace (DIAL 7) dialog, made functional.
// Controls: 0=Find text, 1=Replace text, 2=Ignore Case, 3=Entire Word,
// 4=Wrap, 5=Insert Special, 6=Find, 7=Replace, 8=Replace & Find,
// 9=Replace All.
class TFindDialog : public TDialog {
public:
	// `target` is the TDocWindow that owns this dialog.
	TFindDialog(int32 dialogId, int32 itemsId, uint32 command,
		BHandler* target, Document* doc, TPageView* view);
	virtual ~TFindDialog();

	virtual void MessageReceived(BMessage* message);

	// Set the dialog's values (called from TDocWindow before showing)
	void SetFindText(const char* text);
	void SetReplaceText(const char* text);
	void SetCaseSensitive(bool on);
	void SetEntireWord(bool on);
	void SetWrap(bool on);

private:
	void DoFind();
	void DoReplace();
	void DoReplaceAndFind();
	void DoReplaceAll();
	// Read current values from BTextControl and BCheckBox controls
	// in the dialog's view hierarchy.
	void ReadControls();

	Document* fDocument;
	TPageView* fView;
	std::string fFindText;
	std::string fReplaceText;
	bool fCaseSensitive = false;
	bool fEntireWord = false;
	bool fWrap = true;
};

struct Document;

class TDocWindow : public BWindow {
public:
	// document may be NULL (an empty "Untitled" window)
	TDocWindow(BRect frame, const char* title, Document* document = NULL);

	virtual void MessageReceived(BMessage* message);
	virtual bool QuitRequested();

	// Called from TPageView after text style changes to update marks.
	// `family` is the current font family (empty = default).
	void UpdateTextMarks(const char* family, uint32_t faceFlags,
		float textSize, int alignment);

private:
	TMenuBar* InstallBaseMenus();
	BMenu* MakeDialogsMenu();
	BMenu* MakeFontMenu();
	bool Save(const std::string& path);
	void UpdateTitle();

	BStringView* fStatus;
	Document* fDocument = NULL;
	class TPageView* fPages = NULL;
	BFilePanel* fSavePanel = NULL;
	bool fModified = false;
	bool fBackedUp = false;

	// text menus for menu mark tracking
	TFontMenu* fFontMenu = NULL;
	BMenu* fSizeMenu = NULL;
	BMenu* fFaceMenu = NULL;
	BMenu* fAlignmentMenu = NULL;

	// Spreadsheet menu (populated when a spreadsheet part is active)
};

} // namespace gobe

#endif
