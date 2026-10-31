// The Inks window on its pens (TDialogNewColor, DIAL/Item 65, with Pen
// Ink's pens: MajorType 1, MinorType 3; its TPenView's list of the front
// document's pens) and the pen dialog (TDialogEditPen, DIAL/Item 32: a
// new pen or one edited). The window's other kinds and inks (colours,
// patterns, gradients and their sets) and the pen view's width controls
// aren't ported.
#ifndef GOBE_TPENSWINDOW_H
#define GOBE_TPENSWINDOW_H

#include <string>

#include "Pens.h"
#include "TDialog.h"

class BListView;
class BMenu;

namespace gobe {

// to the front document window: a pen added (TDialogNewColor::DoAddPen,
// 'sfI5', "pen"), one replaced (DoReplacePen, 'sfI7', "ink index" and
// "pen"), one deleted (DoClear; the port's code, "ink index")
const uint32 kMsgAddPen = 'sfI5';
const uint32 kMsgReplacePen = 'sfI7';
const uint32 kMsgDeletePen = 'GpnD';

class TPensWindow : public TDialog {
public:
	TPensWindow();
	virtual ~TPensWindow();

	virtual void MessageReceived(BMessage* message);
	virtual void MenusBeginning();

	// 'sW00' (TApp: the floater shown, or brought to the front)
	static void Open();
	// the list made again from the front document's pens
	static void Refresh();
	// test hooks: "select N", "new", "edit", "clear", "apply" (posted to
	// the open window)
	static void Script(const std::string& command);

protected:
	virtual void Init();

private:
	void Update();
	BListView* List() const;
	void EditPen(bool make);

	std::vector<PenType> fPens;
	BMenu* fEdit = NULL;
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
