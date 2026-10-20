// TDialog: a window from a DIAL resource with controls from an Item resource
// (TDialog::TDialog, TDialogWindow, TItemHandler in the original).
//
// Layout: the original lays controls out with its own "Engineer" classes
// (ItemGroup, ButtonEngineer, ...). Until those are ported, GRUP/FGRP map to
// BGroupLayout and GBOX to a BBox around one, which keeps the structure.
#ifndef GOBE_TDIALOG_H
#define GOBE_TDIALOG_H

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

private:
	void AddItems(BGroupLayout* layout, const std::vector<ItemData>& items);
	BView* MakeControl(const ItemData& item);
	BMessage* ControlMessage(const ItemData& item) const;

	int32 fDialogId;
	uint32 fCommand;
	BHandler* fTarget;
};

} // namespace gobe

#endif
