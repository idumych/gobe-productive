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
	// controls send `command + index` like the original. The resources
	// are the application's, or `resources` (a part's: TSSDialog's
	// SSResources).
	TDialog(int32 dialogId, int32 itemsId, uint32 command, BHandler* target,
		const ResourceFile* resources = NULL);

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

	// the controls by their item index (TDialog::SetTextItem, ...); with
	// `set`, those of the item set (ISET) of that index
	BView* Item(int32 index, int32 set = -1) const;
	void SetTextItem(int32 index, const char* text, int32 set = -1);
	BString TextItem(int32 index, int32 set = -1) const;
	void SetCheckBoxItem(int32 index, bool on, int32 set = -1);
	bool CheckBoxItem(int32 index, int32 set = -1) const;
	void MakeFocusItem(int32 index, int32 set = -1);
	// a control's label (BControl::SetLabel)
	void SetLabelItem(int32 index, const char* label, int32 set = -1);
	// TDialog::SetItemEnabled
	void SetItemEnabled(int32 index, bool on, int32 set = -1);
	// radio buttons (TDialog::SetRadioItem, GetRadioValue: which of the
	// radios in box `group` is on, from 0; -1 if none)
	void SetRadioItem(int32 index, bool on, int32 set = -1);
	int32 RadioValue(int32 group, int32 set = -1) const;
	// pop-up menus (TPopUpMenu::FindValue, the marked item's value; -1 if
	// none) and taking items off their end
	void SetPopUpItem(int32 index, int32 value, int32 set = -1);
	int32 PopUpValue(int32 index, int32 set = -1) const;
	void RemoveLastPopUpItems(int32 index, int32 count, int32 set = -1);
	// TItemSet::LoadItems: the item set `index` shows the controls of Item
	// resource `itemsId` (they send the set's command with "SubID", their
	// index)
	void LoadItemSet(int32 index, int32 itemsId);

private:
	void AddItems(BGroupLayout* layout, const std::vector<ItemData>& items, int32 group = -1);
	BView* MakeControl(const ItemData& item);
	BMessage* ControlMessage(const ItemData& item) const;

	const ResourceFile& Resources() const { return *fResources; }

	int32 fDialogId;
	const ResourceFile* fResources;
	uint32 fCommand;
	BHandler* fTarget;
	std::map<int32, BView*> fItems;
	std::map<int32, std::vector<int32>> fRadios;	// box index: its radios, in order
	// the item set's
	int32 fBuildingSet = -1;
	std::map<int32, BView*> fSetItems;
	std::map<int32, std::vector<int32>> fSetRadios;
};

} // namespace gobe

#endif
