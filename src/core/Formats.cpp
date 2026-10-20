#include "Formats.h"

#include "RsrReader.h"

namespace gobe {

bool
ParseMenu(const std::vector<uint8_t>& data, MenuData& menu)
{
	RsrReader r(data);
	menu.title = r.String();
	menu.flags = r.UInt32();
	if (menu.flags & 2)
		menu.extra = r.UInt32();
	menu.valueMessage = r.UInt32();

	while (r.More()) {
		MenuItemData item;
		uint32_t separator = r.UInt32();
		item.enabled = (r.UInt32() & 0xff) != 0;
		if (separator & 0xff) {
			item.separator = true;
			menu.items.push_back(item);
			continue;
		}
		item.label = r.String();
		item.submenu = r.Int32();
		item.command = r.UInt32();
		item.shortcut = char(r.UInt32() & 0xff);
		item.modifiers = r.UInt32();
		r.UInt32();	// unused
		menu.items.push_back(item);
	}
	return !r.Overrun();
}


std::vector<std::string>
ParseStringList(const std::vector<uint8_t>& data)
{
	std::vector<std::string> list;
	RsrReader r(data);
	while (r.More())
		list.push_back(r.String());
	return list;
}


bool
ParseDialog(const std::vector<uint8_t>& data, DialogData& dialog)
{
	RsrReader r(data);
	dialog.title = r.String();
	dialog.look = r.UInt32();
	dialog.feel = r.UInt32();
	dialog.flags = r.UInt32();
	dialog.menuBar = r.Bool();
	return !r.Overrun();
}


bool
IsGroupType(uint32_t type)
{
	return type == FourCC("GRUP") || type == FourCC("FGRP") || type == FourCC("GBOX");
}


bool
ParseItems(const std::vector<uint8_t>& data, ItemList& list)
{
	RsrReader r(data);
	list.twoLayoutValues = r.Bool();

	// open groups; only the top vector is ever appended to, so pointers to
	// the vectors below stay valid
	std::vector<std::vector<ItemData>*> stack{&list.items};
	int32_t index = 0;

	while (r.More()) {
		uint32_t type = r.UInt32();
		if (type == 0) {
			if (stack.size() > 1)
				stack.pop_back();
			continue;
		}
		if (type == FourCC("ENDR"))
			break;

		ItemData it;
		it.type = type;
		it.layout[0] = r.UInt32();
		it.layout[1] = list.twoLayoutValues ? r.UInt32() : it.layout[0];
		bool noLabel = type == FourCC("GRUP") || type == FourCC("FGRP")
			|| type == FourCC("LIST") || type == FourCC("OLST")
			|| type == FourCC("CLST");
		if (!noLabel)
			it.label = r.String();

		bool group = false;
		switch (type) {
			case FourCC("IBUT"):
				for (int i = 0; i < 5; i++)
					it.values.push_back(r.Int32());
				it.enabled = r.Bool();
				break;
			case FourCC("BSLI"):
				for (int i = 0; i < 3; i++)
					it.strings.push_back(r.String());
				for (int i = 0; i < 4; i++)
					it.values.push_back(r.Int32());
				it.enabled = r.Bool();
				break;
			case FourCC("256P"):
			case FourCC("PIKR"):
			case FourCC("ISET"):
				break;
			case FourCC("BCOL"):
				it.values.push_back(r.Int32());	// layout (cells)
				it.values.push_back(r.Int32());	// cell size
				break;
			case FourCC("BUTT"):
				it.isDefault = r.Bool();
				it.width = r.Int32();
				it.enabled = r.Bool();
				break;
			case FourCC("CHEC"):
			case FourCC("TCHK"):
			case FourCC("RADI"):
				it.enabled = r.Bool();
				break;
			case FourCC("CUST"):
				for (int i = 0; i < 5; i++)
					it.values.push_back(r.Int32());
				break;
			case FourCC("EDIT"):
				it.text = r.String();
				it.values.push_back(r.Int32());
				it.values.push_back(r.Int32());
				break;
			case FourCC("GBOX"):
				group = true;
				break;
			case FourCC("GRUP"):
			case FourCC("FGRP"):
			{
				uint32_t flags = r.UInt32();
				r.UInt32();
				it.vertical = (flags & 1) != 0;
				it.spacing = r.Int32();
				if (type == FourCC("FGRP")) {
					it.insets[0] = r.UInt32();
					it.insets[1] = r.UInt32();
				}
				group = true;
				break;
			}
			case FourCC("POPM"):
				it.menu = r.UInt32();
				it.enabled = r.Bool();
				break;
			case FourCC("LIST"):
			case FourCC("OLST"):
			case FourCC("CLST"):
				for (int i = 0; i < 5; i++)
					it.values.push_back(r.Int32());
				break;
			case FourCC("ICON"):
			case FourCC("USER"):
				it.values.push_back(r.Int32());
				it.values.push_back(r.Int32());
				break;
			case FourCC("STAT"):
			case FourCC("SPRf"):
				it.align = r.UInt32();
				it.bold = r.Bool();
				break;
			case FourCC("TEXT"):
				for (int i = 0; i < 5; i++)
					it.values.push_back(r.Int32());
				it.align = uint32_t(it.values[4]);
				break;
			default:
				// TItemHandler skips the payload of unknown types, which
				// desynchronizes the stream; stop here
				list.unknownType = true;
				stack.back()->push_back(it);
				return false;
		}

		// GRUP/FGRP take no command code; everything else (GBOX too) does
		if (type == FourCC("GRUP") || type == FourCC("FGRP"))
			it.index = -1;
		else
			it.index = index++;

		stack.back()->push_back(it);
		if (group)
			stack.push_back(&stack.back()->back().children);
	}
	return !r.Overrun();
}

} // namespace gobe
