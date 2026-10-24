#include "TMenu.h"

#include <stdio.h>

#include "GobeResources.h"

namespace gobe {

TMenuItem::TMenuItem(const char* label, BMessage* message, char shortcut,
	uint32 modifiers)
	:
	BMenuItem(label, message, shortcut, modifiers)
{
}


static MenuData
LoadMenuData(int32 id, const ResourceFile& resources)
{
	MenuData data;
	const ResourceFile::Resource* r = resources.Find(FourCC("MENU"), id);
	if (r == NULL || !ParseMenu(r->data, data))
		fprintf(stderr, "TMenu: MENU %d missing or malformed\n", (int)id);
	return data;
}


TMenu::TMenu(int32 id, bool loadItems)
	:
	TMenu(id, GobeResources::File(), loadItems)
{
}


TMenu::TMenu(int32 id, const ResourceFile& resources, bool loadItems)
	:
	BMenu(LoadMenuData(id, resources).title.c_str(), B_ITEMS_IN_COLUMN),
	fID(id),
	fResources(&resources),
	fData(LoadMenuData(id, resources))
{
	if (loadItems)
		LoadMenuItems();
}


void
TMenu::LoadMenuItems()
{
	for (const MenuItemData& item : fData.items) {
		if (item.separator) {
			AddSeparatorItem();
			continue;
		}
		if (item.submenu == 0) {
			NewMenuItem(item.enabled, item.label.c_str(), item.command,
				item.shortcut, item.modifiers);
			continue;
		}
		// a submenu: its own MENU resource; the item's command, if any,
		// becomes the superitem's message
		TMenu* sub = new TMenu(item.submenu, *fResources, true);
		AddItem(sub);
		if (item.command != 0 && sub->Superitem() != NULL)
			sub->Superitem()->SetMessage(new BMessage(item.command));
	}
}


void
TMenu::NewMenuItem(bool enabled, const char* label, uint32 command,
	char shortcut, uint32 modifiers)
{
	BMessage* message;
	if (fData.valueMessage == 0) {
		message = new BMessage(command);
	} else {
		// value menu: one message, the item identified by value (or name)
		message = new BMessage(fData.valueMessage);
		if (command == 0xffffffff)
			message->AddString("name", label);
		else
			message->AddInt32(kValueString, (int32)command);
	}
	TMenuItem* item = new TMenuItem(label, message, shortcut, modifiers);
	if (!enabled)
		item->SetEnabled(false);
	AddItem(item);
}


BMenuItem*
TMenu::FindValue(uint32 value) const
{
	for (int32 i = 0; i < CountItems(); i++) {
		BMenuItem* item = ItemAt(i);
		BMessage* message = item->Message();
		if (message != NULL && (uint32)message->FindInt32(kValueString) == value)
			return item;
	}
	return NULL;
}

} // namespace gobe
