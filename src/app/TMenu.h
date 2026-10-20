// TMenu: a BMenu built from a MENU resource (MMenuData + the item loop at
// 0x1be780 in the original). The original derives from both MMenuData and
// BMenu; here the parsed MenuData is a member.
#ifndef GOBE_TMENU_H
#define GOBE_TMENU_H

#include <Menu.h>
#include <MenuItem.h>

#include "Formats.h"

namespace gobe {

class TMenuItem : public BMenuItem {
public:
	TMenuItem(const char* label, BMessage* message, char shortcut,
		uint32 modifiers);
};


class TMenu : public BMenu {
public:
	// TMenu(unsigned long id, bool loadItems, BResources*, bool)
	TMenu(int32 id, bool loadItems = true);

	int32 ID() const { return fID; }
	const MenuData& Data() const { return fData; }

	// TMenu::LoadMenuItems
	void LoadMenuItems();

	// TMenu::FindValue / MMenuData::FindMenuValue
	BMenuItem* FindValue(uint32 value) const;

	// TMenu::NewMenuItem(char enabled, char* label, ulong command,
	//                    char shortcut, ulong modifiers)
	virtual void NewMenuItem(bool enabled, const char* label, uint32 command,
		char shortcut, uint32 modifiers);

private:
	int32 fID;
	MenuData fData;
};

} // namespace gobe

#endif
