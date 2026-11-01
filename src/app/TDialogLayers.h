// Document > Views & Layers… (TDialogLayers, DIAL/Item 203, a floater):
// a menu bar of View (MENU 220: New View, Delete View, Rename View…, then
// the drawing's views, the shown one marked) and Layer (MENU 221: New
// Layer, Delete Layer, Rename Layer…); the shown view's name (0) over four
// icons (1-4: visible, selectable, locked, printable) and the view's
// layers (5, TLayerListView) with a check per flag, the active layer
// selected. Everything goes to the front document window as TPartGR's
// commands ('sM4F' to 'sM4N'). Rename… opens TDialogLayerName (DIAL 31).
#ifndef GOBE_TDIALOG_LAYERS_H
#define GOBE_TDIALOG_LAYERS_H

#include <string>
#include <vector>

#include "TDialog.h"

class BListView;

namespace gobe {

class TMenu;

// what the floater shows of the front document's drawing
struct LayersState {
	std::vector<std::string> views;
	uint32 shownView = 0;
	struct Row {
		std::string name;
		bool flags[5];
	};
	std::vector<Row> rows;		// the shown view's layer infos
	size_t layers = 0;
	size_t activeLayer = 0;
};

class TDialogLayers : public TDialog {
public:
	TDialogLayers();
	virtual ~TDialogLayers();

	virtual void MessageReceived(BMessage* message);

	static TDialogLayers* Open();
	// TFloaterMgr::UpdateAllContents
	static void Refresh();

protected:
	virtual void Init();
	virtual bool ItemHit(int32 index, BMessage* message);

private:
	void Update();
	BListView* List() const;
	// to the front document window, with "index" when it's 0 or more
	void Send(uint32 what, int32 index = -1);

	TMenu* fViews = NULL;
	TMenu* fLayers = NULL;
	LayersState fState;
};

// TDialogLayerName (a TDialogGenericGetString: DIAL/Item 31, the title,
// "Name:" and the name): OK sends `window` `what` with "index" and "name"
class TDialogLayerName : public TDialog {
public:
	TDialogLayerName(BLooper* window, uint32 what, int32 index, const char* title,
		const char* name);

protected:
	virtual void Init();
	virtual bool ItemHit(int32 index, BMessage* message);

private:
	BLooper* fWindow;
	uint32 fWhat;
	int32 fIndex;
	BString fTitle;
	BString fName;
};

} // namespace gobe

#endif
