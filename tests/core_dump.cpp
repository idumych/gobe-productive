// Dump every MENU, DIAL, Item and STR# of an original Gobe binary in a
// canonical text form; tests/check_core.sh compares it with tools/gobersrc.py.
#include <cstdio>
#include <string>

#include "Formats.h"

using namespace gobe;

// like gobersrc.fourcc: text only if all four bytes are printable
static std::string Code(uint32_t c)
{
	for (int shift = 0; shift < 32; shift += 8) {
		int ch = (c >> shift) & 0xff;
		if (ch < 32 || ch > 126)
			return std::to_string(c);
	}
	return FourCCString(c);
}

static void DumpItems(const std::vector<ItemData>& items, int depth)
{
	for (const ItemData& it : items) {
		printf("%*s%s index=%d label=[%s]\n", depth * 2, "", Code(it.type).c_str(),
			it.index, it.label.c_str());
		DumpItems(it.children, depth + 1);
	}
}

int main(int argc, char** argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: core_dump <original binary>\n");
		return 2;
	}
	ResourceFile file;
	if (!file.Load(argv[1])) {
		fprintf(stderr, "%s\n", file.Error().c_str());
		return 1;
	}
	int failures = 0;
	for (auto* r : file.All(FourCC("MENU"))) {
		MenuData m;
		if (!ParseMenu(r->data, m))
			failures++;
		printf("MENU %d title=[%s]\n", r->id, m.title.c_str());
		for (auto& i : m.items) {
			if (i.separator)
				printf("  ---\n");
			else
				printf("  [%s] cmd=%s sub=%d\n", i.label.c_str(), Code(i.command).c_str(),
					i.submenu);
		}
	}
	for (auto* r : file.All(FourCC("DIAL"))) {
		DialogData d;
		if (!ParseDialog(r->data, d))
			failures++;
		printf("DIAL %d title=[%s] look=%u feel=%u\n", r->id, d.title.c_str(), d.look, d.feel);
	}
	for (auto* r : file.All(FourCC("Item"))) {
		ItemList l;
		if (!ParseItems(r->data, l))
			failures++;
		printf("Item %d\n", r->id);
		DumpItems(l.items, 1);
	}
	for (auto* r : file.All(FourCC("STR#"))) {
		auto list = ParseStringList(r->data);
		printf("STR# %d count=%zu\n", r->id, list.size());
	}
	fprintf(stderr, "parse failures: %d\n", failures);
	return failures ? 1 : 0;
}
