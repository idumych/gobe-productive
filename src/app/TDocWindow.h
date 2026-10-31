// TDocWindow: the document window. For now: the base menu bar the original
// builds in TNormalDocWinHelper::InstallBaseMenus, a debug menu that opens any
// DIAL resource, and a status line showing the commands menus send (none are
// wired to document code yet).
#ifndef GOBE_TDOCWINDOW_H
#define GOBE_TDOCWINDOW_H

#include <vector>

#include <MenuBar.h>
#include <Menu.h>
#include <PopUpMenu.h>
#include <CheckBox.h>
#include <TextControl.h>
#include <Window.h>

// BColorControl forward declaration
class BColorControl;
class BMenu;
class BMenuField;


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
// the Font menu's message (TFontMenu)
static const uint32 kMsgFontFamily = 'sf01';

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

struct Document;
struct StylesSnapshot;

class TDocWindow : public BWindow {
public:
	// document may be NULL (an empty "Untitled" window)
	TDocWindow(BRect frame, const char* title, Document* document = NULL);

	virtual ~TDocWindow();

	virtual void MessageReceived(BMessage* message);
	virtual bool QuitRequested();
	virtual void MenusBeginning();
	virtual void WindowActivated(bool active);

	// TApp::FrontDocWindow: the document window last active, or NULL
	static TDocWindow* Front();
	// the document's other windows hold this while they use it
	class BLocker& DocumentLock() const;
	// the active part has text selected (TFindReplace::hasTextSelection)
	bool HasTextSelection() const;
	// THistory: the steps' labels, how many are done, Show Undo Steps
	void HistoryState(std::vector<std::string>& labels, size_t& done, bool& show) const;
	// what Edit Styles shows: the document's styles, the active part's
	// kinds, the selection's
	void GetStylesSnapshot(StylesSnapshot& snapshot) const;

	// Called from TPageView after text style changes to update marks.
	// `family` is the current font family (empty = default).
	void UpdateTextMarks(const char* family, uint32_t faceFlags,
		float textSize, int alignment);

	virtual void DispatchMessage(BMessage* message, BHandler* handler);

private:
	TMenuBar* InstallBaseMenus();
	void InstallPartMenus();
	BMenu* MakeDialogsMenu();
	BMenu* MakeFontMenu();
	bool Save(const std::string& path);
	void UpdateTitle();
	// TFindReplace::DoMessage: Find/Change's commands
	void FindReplaceCommand(BMessage* message);
	// File > Page Setup… (TLeafSheet::PageSetup), Print… (TDocWindow::
	// Print, TPart::Print); the document's geometry changed (margins)
	bool PageSetup();
	void Print();
	void GeometryChanged();

	BStringView* fStatus;
	TMenuBar* fBar = NULL;
	std::vector<BMenu*> fPartMenus;	// the active part's (InstallPartMenus)
	bool fSheetMenus = false;
	Document* fDocument = NULL;
	class TPageView* fPages = NULL;
	class TWPRuler* fRuler = NULL;
	class TPartBar* fPartBar = NULL;
	class TFormatBar* fFormatBar = NULL;
	class TSpellBar* fSpellBar = NULL;
	class TEntryBar* fEntryBar = NULL;
	class TSheetBar* fSheetBar = NULL;
	class TToolBar* fToolBar = NULL;
	void ShowActiveRibbon();
	class TScaleSlider* fScaleSlider = NULL;
	class BFilePanel* fInsertPanel = NULL;	// File > Insert…
	bool fCloseAfterSave = false;	// Save As chosen in the close alert
public:
	// the application is quitting (the close alert asks "before quitting?")
	bool fAppQuitting = false;
private:
	BFilePanel* fSavePanel = NULL;
	BMenuField* fFormatField = NULL;	// the Save panel's "File Format"
	// the document's windows, history and saved state (New Window)
	struct TSharedDocument* fShared = NULL;
	uint32 fSeenGeneration = 0;		// the shared generation laid out for

	// text menus for menu mark tracking
	TFontMenu* fFontMenu = NULL;
	BMenu* fSizeMenu = NULL;
	BMenu* fFaceMenu = NULL;
	BMenu* fAlignmentMenu = NULL;

	// Spreadsheet menu (populated when a spreadsheet part is active)
};

} // namespace gobe

#endif
