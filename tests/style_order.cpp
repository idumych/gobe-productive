// SortStyles: the original's style-table order (by type, then named-first
// in name order, then unnamed in original order); links and afterSection
// remapped through the returned old->new map; stable for equal elements.
#include <cstdio>
#include <cstring>
#include <vector>

#include "DocArchive.h"
#include "DocSections.h"
#include "StyleEdit.h"

using namespace gobe;
namespace {

const uint32_t kNone = 0xffffffff;

static int failures = 0;
static int checks = 0;

static void
Check(bool cond, const char* what)
{
	checks++;
	if (!cond) {
		printf("FAIL: %s\n", what);
		failures++;
	}
}

static void
CheckEq(uint32_t got, uint32_t expected, const char* what)
{
	checks++;
	if (got != expected) {
		printf("FAIL: %s\n  got:      %u\n  expected: %u\n", what, got, expected);
		failures++;
	}
}

} // namespace

int
main(int argc, char** argv)
{
	(void)argc;
	(void)argv;

	/* Build a StyleMgr by hand with these styles in this order
	 * (type, name, links[0]):
	 *  0 Para "WP Default" parent 3
	 *  1 Basi "" parent 3
	 *  2 GRFX "GR Default" parent 3
	 *  3 Basi "Default" none
	 *  4 Basi "Text Background" none
	 *  5 Para "" parent 0
	 *  6 Basi "" parent 2
	 *  7 Para "Body" parent 0
	 * afterSection = 6
	 *
	 * Expected order after SortStyles (old indexes):
	 * Basi named (name asc): 3 "Default", 4 "Text Background"
	 * Basi unnamed (original): 1, 6
	 * GRFX (only one): 2
	 * Para named (name asc): 7 "Body", 0 "WP Default"
	 * Para unnamed: 5
	 * => 3, 4, 1, 6, 2, 7, 0, 5
	 *
	 * Links remap (old -> new): 3->0, 4->1, 1->2, 6->3, 2->4, 7->5, 0->6, 5->7
	 * New "WP Default" at 6: links[0] = old 3 -> new 0
	 * New "Body" at 5: links[0] = old 0 -> new 6
	 * Unnamed Para at 7: links[0] = old 0 -> new 6
	 * Unnamed Basi at 3: links[0] = old 6 -> new 3
	 */

	StyleMgr m;
	m.properties.clear();
	m.names.clear();

	auto make_style = [&](const char* name, uint32_t type, uint32_t links0,
		uint32_t links1 = kNone, uint32_t links2 = kNone) {
		Style s;
		s.name = name ? name : "";
		s.value10 = type;
		s.links[0] = links0;
		s.links[1] = links1;
		s.links[2] = links2;
		m.styles.push_back(s);
	};

	// Old order (indexes 0..7):
	make_style("WP Default", Sig("Para"), 3);               // 0
	make_style("", Sig("Basi"), 3);                          // 1
	make_style("GR Default", Sig("GRFX"), 3);                // 2
	make_style("Default", Sig("Basi"), kNone);               // 3
	make_style("Text Background", Sig("Basi"), kNone);       // 4
	make_style("", Sig("Para"), 0);                          // 5
	make_style("", Sig("Basi"), 2);                          // 6
	make_style("Body", Sig("Para"), 0);                      // 7

	m.afterSection = 6;

	// --- Run SortStyles ---
	std::vector<uint32_t> map = SortStyles(m);

	/* Check the returned map */
	CheckEq(map[3], 0, "map[3] == 0 (Default -> new 0)");
	CheckEq(map[4], 1, "map[4] == 1 (Text Background -> new 1)");
	CheckEq(map[1], 2, "map[1] == 2 (unnamed Basi #1 -> new 2)");
	CheckEq(map[6], 3, "map[6] == 3 (unnamed Basi #2 -> new 3)");
	CheckEq(map[2], 4, "map[2] == 4 (GR Default -> new 4)");
	CheckEq(map[7], 5, "map[7] == 5 (Body -> new 5)");
	CheckEq(map[0], 6, "map[0] == 6 (WP Default -> new 6)");
	CheckEq(map[5], 7, "map[5] == 7 (unnamed Para -> new 7)");

	/* Check the names in the new order */
	Check(m.styles[0].name == "Default", "new[0] name == Default");
	Check(m.styles[1].name == "Text Background", "new[1] name == Text Background");
	Check(m.styles[2].name == "", "new[2] name is unnamed Basi");
	Check(m.styles[3].name == "", "new[3] name is unnamed Basi");
	Check(m.styles[4].name == "GR Default", "new[4] name == GR Default");
	Check(m.styles[5].name == "Body", "new[5] name == Body");
	Check(m.styles[6].name == "WP Default", "new[6] name == WP Default");
	Check(m.styles[7].name == "", "new[7] name is unnamed Para");

	/* Check links follow the remap */
	/* New "Default" at 0: old links[0] = kNone -> stays kNone */
	CheckEq(m.styles[0].links[0], kNone, "Default links[0] stays kNone");
	/* New "Text Background" at 1: old links[0] = kNone -> stays */
	CheckEq(m.styles[1].links[0], kNone, "Text Background links[0] stays kNone");
	/* New unnamed Basi (was old 1) at 2: old links[0] = 3 -> new 0 */
	CheckEq(m.styles[2].links[0], 0, "unnamed Basi(1) links[0] remaps 3->0");
	/* New unnamed Basi (was old 6) at 3: old links[0] = 2 -> new 4 */
	CheckEq(m.styles[3].links[0], 4, "unnamed Basi(6) links[0] remaps 2->4");
	/* New "GR Default" at 4: old links[0] = 3 -> new 0 */
	CheckEq(m.styles[4].links[0], 0, "GR Default links[0] remaps 3->0");
	/* New "Body" at 5: old links[0] = 0 -> new 6 */
	CheckEq(m.styles[5].links[0], 6, "Body links[0] remaps 0->6");
	/* New "WP Default" at 6: old links[0] = 3 -> new 0 */
	CheckEq(m.styles[6].links[0], 0, "WP Default links[0] remaps 3->0");
	/* New unnamed Para at 7: old links[0] = 0 -> new 6 */
	CheckEq(m.styles[7].links[0], 6, "unnamed Para links[0] remaps 0->6");

	/* Check afterSection == remap(old 6) == 3 */
	CheckEq(m.afterSection, 3, "afterSection == 3");

	/* A style whose link is 0xffffffff keeps it */
	Check(m.styles[0].links[0] == kNone, "kNone stays kNone (not remapped)");

	/* Sorting an already-sorted table returns the identity map */
	{
		StyleMgr sortedMgr;
		sortedMgr.properties.clear();
		sortedMgr.names.clear();

		// Already sorted: Basi named asc, Basi unnamed, Para named asc, Para unnamed
		auto add = [&](const char* name, uint32_t type, uint32_t links0) {
			Style s;
			s.name = name ? name : "";
			s.value10 = type;
			s.links[0] = links0;
			sortedMgr.styles.push_back(s);
		};
		add("A", Sig("Basi"), kNone);
		add("B", Sig("Basi"), 0);
		add("", Sig("Basi"), 0);
		add("Z", Sig("Para"), kNone);
		add("", Sig("Para"), 0);

		std::vector<uint32_t> idMap = SortStyles(sortedMgr);
		for (size_t i = 0; i < idMap.size(); i++)
			CheckEq(idMap[i], uint32_t(i), "identity map for already sorted");
	}

	printf("style_order ok %d, failed %d\n", checks, failures);
	return failures != 0;
}
