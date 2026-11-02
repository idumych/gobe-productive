// The Presentation part's dialogs (its resources, the TPR... dialogs): New
// Slide (DIAL/Item 11002), Duplicate Slide (11003), Slide Transition
// (11004, with the transitions' options in Item 100 to 102) and the slide's
// name (TDialogSlideName, the generic DIAL 31). Each sends the document
// window its 'PR..' command (TPartPR::HandleMessage).
#ifndef GOBE_TPRESENTATION_H
#define GOBE_TPRESENTATION_H

#include <String.h>

#include <string>
#include <vector>

#include "TDialog.h"

class BOutlineListView;

namespace gobe {

// the transitions (TPartPRFactory: Jump, Wipe, Venetian Blinds, Elevator
// Doors): their type (TSlide +0xc), the Item resource of their options (0
// none) and their default options
struct TransitionKind {
	const char* name;
	uint32 type;
	int32 items;
	uint32 defaultOptions;
};
extern const TransitionKind kTransitions[4];
// its index in kTransitions, -1 if unknown
int32 TransitionIndex(uint32 type);

// TDialogPRNewSlide: "untitled slide N" (STR# 11002, 0; N one more than
// the slides), at the selection (`selection`, the slide after the one
// chosen) or at the end (`count`), a new layer, the previous slide's
// background (not at the first slide); 'PR04'
class TDialogPRNewSlide : public TDialog {
public:
	TDialogPRNewSlide(BLooper* window, int32 selection, int32 count);

protected:
	virtual void Init();
	virtual bool ItemHit(int32 index, BMessage* message);

private:
	void FixControls();

	BLooper* fWindow;
	int32 fSelection, fCount;
};

// TDialogPRDupSlide: CopyName of the slide's name, its layers or copies of
// them, with a new layer or the background shared; 'PR0A'
class TDialogPRDupSlide : public TDialog {
public:
	TDialogPRDupSlide(BLooper* window, int32 slide, const char* name);

protected:
	virtual void Init();
	virtual bool ItemHit(int32 index, BMessage* message);

private:
	void FixControls();

	BLooper* fWindow;
	int32 fSlide;
	BString fName;
};

// TDialogPRTransitions: the transition (pop-up 2) and its options (item
// set 4); 'PR03' with "refcon" the type and "transition" the options
class TDialogPRTransitions : public TDialog {
public:
	TDialogPRTransitions(BLooper* window, int32 slide, uint32 type, uint32 options);

protected:
	virtual void Init();
	virtual bool ItemHit(int32 index, BMessage* message);

private:
	// TTransition::InitItems: the options shown
	void LoadOptions(int32 kind, uint32 options);
	// GetSettings
	uint32 Options() const;

	BLooper* fWindow;
	int32 fSlide;
	int32 fKind;
	uint32 fOptions;
};

// TDialogSlideName (DIAL 31): `title`, "Name:" and the name; `what` with
// "slide" and "name" to the window
class TDialogSlideName : public TDialog {
public:
	TDialogSlideName(BLooper* window, uint32 what, int32 slide, const char* title,
		const char* name, const char* label);

protected:
	virtual void Init();
	virtual bool ItemHit(int32 index, BMessage* message);

private:
	BLooper* fWindow;
	uint32 fWhat;
	int32 fSlide;
	BString fTitle, fName, fLabel;
};

// what the Presentation window shows of the front document window's
// presentation (TDocWindow::PresentationState)
struct PresentationState {
	bool presentation = false;
	struct SlideRow {
		std::string name;
		std::vector<std::string> layers;
		bool collapsed = false;
		uint32 transition = 0;
		uint32 options = 0;
	};
	std::vector<SlideRow> slides;
	std::vector<std::string> partLayers;	// the drawing's layers
	std::vector<std::vector<bool>> inSlide;	// [slide][part layer]
	int32 currentSlide = 0;
	int32 currentLayer = -1;
	bool autoStart = false, cursor = false;
};

class TMenu;

// TDialogPRNavigate (DIAL/Item 11001, a floater): Edit (MENU 11003) and
// Options (11004) menus, the slides with their layers under them (an
// outline list; each slide's transition and its layers behind the icons on
// its right, PresListItem), Start and Start Here
class TDialogPRNavigate : public TDialog {
public:
	TDialogPRNavigate();
	virtual ~TDialogPRNavigate();

	static TDialogPRNavigate* Open();
	// TPartPR::UpdateFloater
	static void Refresh();

	virtual void MessageReceived(BMessage* message);
	virtual void MenusBeginning();

protected:
	virtual void Init();
	virtual bool ItemHit(int32 index, BMessage* message);

private:
	friend class PresList;
	void Update();
	// the slide and layer of the list's selection (layer -1: a slide)
	bool Selection(int32& slide, int32& layer) const;
	void Send(BMessage* message);
	void FixButtons();
	// PresListItem::MouseDown: the transition menu or the layers menu
	void TransitionMenu(int32 slide, BPoint where);
	void LayersMenu(int32 slide, BPoint where);

	BOutlineListView* fList = NULL;
	TMenu* fEdit = NULL;
	TMenu* fOptions = NULL;
	PresentationState fState;
	bool fUpdating = false;
};

class TDocWindow;

// TPRWinHelper: the slide show (Start Presentation…, Start, Start Here):
// the slides filling the screen, black round them, the cursor hidden
// unless Options > Cursor; Home and Up the first slide, End and Down the
// last, Backspace, Page Up and Left the one before, Tab, Page Down, Right,
// Space and a click the next (with the slide's transition), Escape, Q and
// Command-period end it
void StartSlideShow(TDocWindow* window, struct Document* document, int32 slide);

} // namespace gobe

#endif
