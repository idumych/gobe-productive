// Gobe Productive's resource formats, parsed into plain structs. Each parser
// follows the original loader in the decompiled code (named per function);
// see NOTES.md "Resources" for the formats.
#ifndef GOBE_FORMATS_H
#define GOBE_FORMATS_H

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "ResourceFile.h"

namespace gobe {

// MENU: MMenuData::MMenuData + the item loop at 0x1be780 (TMenu).
struct MenuItemData {
	bool separator = false;
	bool enabled = true;
	std::string label;
	int32_t submenu = 0;		// MENU id, 0 = none
	uint32_t command = 0;		// BMessage what
	char shortcut = 0;
	uint32_t modifiers = 0;
};

struct MenuData {
	std::string title;
	uint32_t flags = 0;
	uint32_t extra = 0;			// present when flags & 2
	uint32_t valueMessage = 0;	// non-zero: every item sends this with "value"
	std::vector<MenuItemData> items;
};

bool ParseMenu(const std::vector<uint8_t>& data, MenuData& menu);

// STR#: GetIndString.
std::vector<std::string> ParseStringList(const std::vector<uint8_t>& data);

// DIAL: TDialog::TDialog.
struct DialogData {
	std::string title;
	uint32_t look = 0;
	uint32_t feel = 0;
	uint32_t flags = 0;
	bool menuBar = false;
};

bool ParseDialog(const std::vector<uint8_t>& data, DialogData& dialog);

// Item: TItemHandler::TItemHandler (0x2a6510). A tree of controls.
struct ItemData {
	uint32_t type = 0;			// 'BUTT', 'GRUP', ...
	uint32_t layout[2] = {0, 0};	// passed to every Engineer (meaning TBD)
	std::string label;
	int32_t index = -1;			// command offset (dialog base code + index);
								// -1 for groups
	bool enabled = true;

	// per type (unused fields stay default)
	bool isDefault = false;		// BUTT
	int32_t width = 0;			// BUTT
	bool vertical = false;		// GRUP/FGRP
	int32_t spacing = 0;		// GRUP/FGRP
	uint32_t insets[2] = {0, 0};	// FGRP
	uint32_t menu = 0;			// POPM: MENU id, or 0xb fonts, 0x79 arrows
	std::string text;			// EDIT initial text
	std::vector<std::string> strings;	// BSLI
	std::vector<int32_t> values;		// BSLI, EDIT, TEXT, LIST, ICON, IBUT, ...
	uint32_t align = 0;			// STAT/SPRf/TEXT: 0 left, 1 right, 2 center
	bool bold = false;			// STAT/SPRf

	std::vector<ItemData> children;	// GBOX/GRUP/FGRP
};

struct ItemList {
	bool twoLayoutValues = false;
	std::vector<ItemData> items;
	bool unknownType = false;	// parsing stopped at a type the loader skips
};

bool ParseItems(const std::vector<uint8_t>& data, ItemList& list);

bool IsGroupType(uint32_t type);

} // namespace gobe

#endif
