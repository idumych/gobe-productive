// The Inks window (TDialogNewColor, DIAL/Item 65): the fill, pen or text
// ink (its Kind menu), its colours, patterns and gradients (the active
// set's, TInkView; the Sets menu choosing the set) and the pen ink's pens
// (TPenView's list of the front document's pens); and the pen dialog
// (TDialogEditPen, DIAL/Item 32: a new pen or one edited). The ink and
// pen editors, the clipboard and the width controls are TInkEditors'.
#ifndef GOBE_TPENSWINDOW_H
#define GOBE_TPENSWINDOW_H

#include <functional>
#include <string>

#include "Pens.h"
#include "TDialog.h"
#include "TInkMenus.h"

class BListView;
class BMenu;
class BScrollView;
class BFilePanel;

namespace gobe {

// to the front document window: a pen added (TDialogNewColor::DoAddPen,
// 'sfI5', "pen"), one replaced (DoReplacePen, 'sfI7', "ink index" and
// "pen"), one deleted (DoClear; the port's code, "ink index")
const uint32 kMsgAddPen = 'sfI5';
const uint32 kMsgReplacePen = 'sfI7';
const uint32 kMsgDeletePen = 'GpnD';

// the inks' (TDialogNewColor): a new ink from its dialog ('sfI0', "ink
// name", "ink"), one replaced ('sfI2', "ink name", "ink index", "ink"),
// one applied ('sfI3')
const uint32 kMsgAddInk = 'sfI0';
const uint32 kMsgReplaceInk = 'sfI2';
const uint32 kMsgApplyInk = 'sfI3';
// to the front document window: one of its own sets changed ("kind",
// "ink name" its name before, "set": InkSetBytes; "add" for a new one)
const uint32 kMsgDocInkSet = 'GisD';

class TPensWindow : public TDialog {
public:
	TPensWindow(int32 major, int32 minor);
	virtual ~TPensWindow();

	virtual void MessageReceived(BMessage* message);
	virtual void MenusBeginning();

	// 'sW00' (TApp: the floater shown on the "MajorType" (0 fill, 1 pen, 2
	// text ink) and "MinorType" (0 colours, 1 patterns, 2 gradients, 3
	// pens), or brought to the front and switched to them)
	// (torn off: moved to `where`, its top left, screen; x < 0 leaves it)
	static void Open(int32 major = 1, int32 minor = 3, BPoint where = BPoint(-1, -1));
	// the list made again from the front document's pens and the sets
	static void Refresh();
	// test hooks: "kind N", "tab N", "set NAME", "select N", "new",
	// "edit", "clear", "cut", "copy", "paste", "apply", "show", "width
	// +|-|N", "editink 0|1|2", "editor COMMAND" (posted to the open window)
	static void Script(const std::string& command);

	int32 MajorType() const { return fMajor; }
	int32 MinorType() const { return fMinor; }
	// the selected ink (with the inks shown) and its set and index; the
	// selected pen (with the pens) and its index
	bool SelectedInk(std::string& set, int32& index, Ink& ink) const;
	bool SelectedPen(int32& index, PenType& pen) const;
	// ChangeMajorType, ChangeMinorType, SwitchToSet (the set made active)
	void ChangeMajorType(int32 major);
	void ChangeMinorType(int32 minor);
	void SwitchToSet(const std::string& name);

protected:
	virtual void Init();

private:
	void Update();
	void SetWindowTitle();
	BListView* List() const;
	void EditPen(bool make);
	// DoNewInk, DoEditInk (the ink editors' dialogs)
	void EditInk(bool make);
	// the File menu (DoNewSet, DoSave, DoSaveAs, DoRevert, DoConfigureSet)
	void FileCommand(BMessage* message);
	// Cut, Copy, Paste of inks and pens (InkClipboard): `run` false asks
	// whether it's on
	bool Clipboard(uint32 what, bool run);
	// a set changed: the application's or a file's (saved), or the front
	// document's (posted to it); `name` the set's name before
	void StoreSet(const InkSetRef& set, const std::string& name);
	// the front document's own sets
	static SetMgr FrontInkSets();

	int32 fMajor, fMinor;
	std::vector<PenType> fPens;
	InkSetRef fSet;
	SetMgr fDocSets;
	BFilePanel* fSavePanel = NULL;
	BMenu* fKind = NULL;
	BMenu* fFile = NULL;
	BMenu* fEdit = NULL;
	class TPenWidthView* fWidth = NULL;
	BMenu* fSets = NULL;
	BView* fKinds = NULL;
	class TInkSetView* fInks = NULL;
	BScrollView* fPenScroll = NULL;
};


class TDialogEditPen : public TDialog {
public:
	// a new pen (`pen` NULL: a dash pen, its name empty) or `pen` edited;
	// OK posts `reply` to `target` with the pen ("pen")
	TDialogEditPen(const PenType* pen, BLooper* target, const BMessage& reply);

	// test hook: "name TEXT", "type DASH|PDOT|HASH", "gap TEXT", "dash
	// TEXT", "filled 0|1", "print 0|1", "ok", "revert", "cancel" (to the
	// open dialog)
	static void Script(const std::string& command);

	virtual void MessageReceived(BMessage* message);

protected:
	virtual void Init();
	virtual bool ItemHit(int32 index, BMessage* message);

private:
	// TDialogEditPen::SetTypePopup, InitOptions, ValidData, CreateNewPen
	void SetTypePopup();
	void InitOptions();
	bool ValidData();
	void CreateNewPen();
	void Revert();

	bool fEditing;
	PenType fOriginal;
	BLooper* fTarget;
	BMessage fReply;
	// the pen's fields as the dialog has them (+0x54 name, +0x58 kind,
	// +0x5c printable, +0x60 gap, +0x64 dash, +0x68 filled)
	std::string fName;
	int fKind = 2;
	bool fPrint = true;
	float fGap = 4, fDash = 4;
	bool fFilled = true;
	PenType fPen;		// what the sample draws (+0x50)
	class TPenSampleView* fSample = NULL;
};

} // namespace gobe

#endif
