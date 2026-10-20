// TDocWindow: the document window. For now: the base menu bar the original
// builds in TNormalDocWinHelper::InstallBaseMenus, a debug menu that opens any
// DIAL resource, and a status line showing the commands menus send (none are
// wired to document code yet).
#ifndef GOBE_TDOCWINDOW_H
#define GOBE_TDOCWINDOW_H

#include <MenuBar.h>
#include <Window.h>


#include <string>

class BFilePanel;
class BMenu;
class BMenuItem;
class BStringView;

namespace gobe {

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
