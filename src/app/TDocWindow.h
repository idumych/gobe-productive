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
class BStringView;

namespace gobe {

// The document window's menu bar ("Main Menu Bar" in the original).
class TMenuBar : public BMenuBar {
public:
	TMenuBar() : BMenuBar("Main Menu Bar") {}

	// test hook: open menu `index` as if clicked
	void OpenMenu(int32 index) { StartMenuBar(index, true, true); }
};

struct Document;

class TDocWindow : public BWindow {
public:
	// document may be NULL (an empty "Untitled" window)
	TDocWindow(BRect frame, const char* title, Document* document = NULL);

	virtual void MessageReceived(BMessage* message);
	virtual bool QuitRequested();

private:
	TMenuBar* InstallBaseMenus();
	BMenu* MakeDialogsMenu();
	bool Save(const std::string& path);
	void UpdateTitle();

	BStringView* fStatus;
	Document* fDocument = NULL;
	class TPageView* fPages = NULL;
	BFilePanel* fSavePanel = NULL;
	bool fModified = false;
	bool fBackedUp = false;
};

} // namespace gobe

#endif
