// Tear-off palettes: the FormatBar's menus dragged off into floaters at the
// drop point (TApp::MessageReceived 'sW02', 'sW04', 'sW05', 'sW06',
// 'sW07'; TFloaterMgr keeps one of each, moved to the new drop point when
// torn off again):
//   Styles (TDialogStyles, DIAL/Item 9): the active part's named styles
//     by kind, the selection's marked; choosing one applies it; New
//     (New Style…) and Edit (Edit Styles…)
//   Text Formatting (TDialogText, 64): the font and size, a size slider
//     with A- and A+, and the faces
//   Scale (TDialogScale, 72): the zoom as a slider
//   Named Selections (TDialogNames, 81): the document's names; choosing
//     one selects it, a double click edits the names
//   Transparency (TDialogTransparency, 90): the shapes' opacity, with
//     buttons a percent less or more
// They work on the front document window and follow it (RefreshTearOffs).
#ifndef GOBE_TTEAROFFS_H
#define GOBE_TTEAROFFS_H

#include <Point.h>

#include <string>
#include <vector>

#include "TStylesEdit.h"

namespace gobe {

// what the floaters show of the front document window, taken in its thread
// (TDocWindow::TearOffState)
struct TearOffState {
	bool document = false;
	std::vector<std::string> names;		// TSelectionMgr::GetNamedSelections
	bool shapes = false;				// shapes selected
	int32 opacity = 100;				// theirs (the first's), in percent
	bool zoomable = false;
	float zoom = 1;
	bool text = false;					// text selected or a caret
	std::string family;
	float size = 12;
	uint32 faces = 0;
	StylesSnapshot styles;
};

// TApp::MessageReceived: the floater for tear-off command `what` at
// `where` (its top left, screen), opened or moved there
void OpenTearOff(uint32 what, BPoint where);
// TFloaterMgr::UpdateAllContents: the open floaters again
void RefreshTearOffs();
// the open floaters (a new document window joins their subsets)
std::vector<class BWindow*> TearOffWindows();
// test hook: "<command> <args>" for the floater open for tear-off `what`
// ("dump" prints what it shows; "pick N" chooses list row N; "invoke N"
// double-clicks it; "slider V" moves its slider; "button I" presses item
// I)
void TearOffScript(uint32 what, const std::string& command);

} // namespace gobe

#endif
