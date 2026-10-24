// TDialog: a window from a DIAL resource with controls from an Item resource
// (TDialog::TDialog, TDialogWindow, TItemHandler in the original).
//
// Layout: the original lays controls out with its own "Engineer" classes
// (ItemGroup, ButtonEngineer, ...). Until those are ported, GRUP/FGRP map to
// BGroupLayout and GBOX to a BBox around one, which keeps the structure.
#ifndef GOBE_TDIALOG_H
#define GOBE_TDIALOG_H

#include <map>

#include <String.h>
#include <Window.h>

#include "Formats.h"

class BGroupLayout;
class BView;

namespace gobe {

class TDialog : public BWindow {
public:
	// TDialog(ulong dialogId, ulong itemsId, BResources*, ulong command):
	// controls send `command + index` like the original.
	TDialog(int32 dialogId, int32 itemsId, uint32 command, BHandler* target);

	virtual void MessageReceived(BMessage* message);

	static bool Exists(int32 dialogId);

	// TDialog::Run: Init, then show
	void RunDialog();

protected:
	// TDialog::Init: fill in the controls (the window is locked)
	virtual void Init() {}
	// TDialog::ItemHit: a control was used; true keeps the dialog open
	// after OK (a value didn't validate). The default closes on OK (0)
	// and Cancel (1).
	virtual bool ItemHit(int32 index, BMessage* message);

	// the controls by their item index (TDialog::SetTextItem, ...)
	BView* Item(int32 index) const;
	void SetTextItem(int32 index, const char* text);
	BString TextItem(int32 index) const;
	void SetCheckBoxItem(int32 index, bool on);
	bool CheckBoxItem(int32 index) const;
	void MakeFocusItem(int32 index);
	// a control's label (BControl::SetLabel)
	void SetLabelItem(int32 index, const char* label);
	// radio buttons (TDialog::SetRadioItem, GetRadioValue: which of the
	// radios in box `group` is on, from 0; -1 if none)
	void SetRadioItem(int32 index, bool on);
	int32 RadioValue(int32 group) const;

private:
	void AddItems(BGroupLayout* layout, const std::vector<ItemData>& items, int32 group = -1);
	BView* MakeControl(const ItemData& item);
	BMessage* ControlMessage(const ItemData& item) const;

	int32 fDialogId;
	uint32 fCommand;
	BHandler* fTarget;
	std::map<int32, BView*> fItems;
	std::map<int32, std::vector<int32>> fRadios;	// box index: its radios, in order
};

} // namespace gobe

#endif
