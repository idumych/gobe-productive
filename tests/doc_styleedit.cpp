// Edit Styles' commands on the documents that have the text part's "Body"
// and "WP Default" styles: a new style renamed and rebased (users counts
// following), refused renames and bases, Clear Style, and a property set
// in "Body" reaching what comes from it.
#include <cstdio>
#include <string>

#include "Document.h"
#include "Editing.h"
#include "ResourceFile.h"
#include "StyleEdit.h"
#include "Styles.h"

using namespace gobe;

static int failures = 0;
static int checks = 0;

static void
Check(bool ok, const std::string& what)
{
	checks++;
	if (!ok) {
		printf("FAIL: %s\n", what.c_str());
		failures++;
	}
}


int
main(int argc, char** argv)
{
	int documents = 0;
	const uint32_t kNo = 0xffffffff;
	for (int i = 1; i < argc; i++) {
		Document doc;
		std::string error;
		if (!LoadDocument(argv[i], doc, error))
			continue;
		StyleMgr& m = doc.styles;
		uint32_t body = FindNamedStyle(m, "Body");
		uint32_t wpDefault = FindNamedStyle(m, "WP Default");
		if (body == kNo || wpDefault == kNo)
			continue;
		documents++;
		std::string name = std::string(argv[i]).substr(std::string(argv[i]).rfind('/') + 1);

		uint32_t mine = NewNamedStyle(doc, Sig("Para"), "Mine", body, kNoPart);
		Check(mine != kNoPart && FindNamedStyle(m, "Mine") == mine, name + ": the new style");
		// renamed
		Check(ModifyNamedStyle(doc, "Mine", "Mine 2", "Body"), name + ": rename");
		Check(FindNamedStyle(m, "Mine") == kNo && FindNamedStyle(m, "Mine 2") == mine,
			name + ": found by its new name only");
		Check(!ModifyNamedStyle(doc, "Mine 2", "Mine 2", "Body"), name + ": nothing to change");
		Check(!ModifyNamedStyle(doc, "Mine 2", "Body", "Body"), name + ": another's name");
		Check(!ModifyNamedStyle(doc, "Mine 2", "Mine 2", "No Such Style"),
			name + ": no such base");
		Check(!ModifyNamedStyle(doc, "No Such Style", "X", ""), name + ": no such style");
		// rebased on WP Default: Body has one user less, WP Default one more
		uint32_t bodyUsers = m.styles[body].value4, defaultUsers = m.styles[wpDefault].value4;
		Check(ModifyNamedStyle(doc, "Mine 2", "Mine 2", "WP Default"), name + ": rebase");
		Check(m.styles[mine].links[0] == wpDefault, name + ": based on WP Default");
		Check(m.styles[body].value4 == bodyUsers - 1
			&& m.styles[wpDefault].value4 == defaultUsers + 1, name + ": the users counts");
		// what it inherits: WP Default's (and its parent's) properties
		Check(m.styles[mine].props2 == StyleUnionSet(m, m.styles[mine]), name + ": props2");
		// WP Default can't come from a style that comes from it
		Check(!ModifyNamedStyle(doc, "WP Default", "WP Default", "Mine 2"),
			name + ": a base that comes from the style");
		// no base
		Check(ModifyNamedStyle(doc, "Mine 2", "Mine 2", "") && m.styles[mine].links[0] == kNo,
			name + ": no base");

		// Clear Style: not Body (styles come from it); the new one
		Check(!CanDeleteStyle(m, body) && !DeleteNamedStyle(doc, "Body"),
			name + ": Body can't be cleared");
		Check(DeleteNamedStyle(doc, "Mine 2") && FindNamedStyle(m, "Mine 2") == kNo
			&& (m.styles[mine].value8 & kStyleDeleted), name + ": cleared");
		// a new style may take the cleared one's name
		Check(NewNamedStyle(doc, Sig("Para"), "Mine 2", body, kNoPart) != kNoPart,
			name + ": the name again");

		// a property set in Body: its own; what comes from it inherits it
		Property size = TextSizeProperty(31);
		Check(SetStyleProperty(doc, body, size), name + ": Body's size set");
		Check(ResolveTextStyle(m, body).size == 31, name + ": Body is 31 points");
		Check(!SetStyleProperty(doc, body, size), name + ": the same again changes nothing");
		bool inherited = true;
		int children = 0;
		for (size_t s = 0; s < m.styles.size(); s++) {
			if (m.styles[s].links[0] != body)
				continue;
			children++;
			bool has = false;
			for (uint32_t p : m.styles[s].props2)
				has = has || (p < m.properties.size() && m.properties[p].type == size.type
					&& m.properties[p].data == size.data);
			inherited = inherited && has;
		}
		Check(children > 0 && inherited, name + ": Body's children inherit the size");
		// the size WP Default has: Body's own size goes (TStyle::Reduce)
		TextStyle base = ResolveTextStyle(m, wpDefault);
		Check(SetStyleProperty(doc, body, TextSizeProperty(base.size)),
			name + ": Body's size as WP Default's");
		bool own = false;
		for (uint32_t p : m.styles[body].props)
			own = own || (p < m.properties.size() && m.properties[p].type == size.type);
		Check(!own && ResolveTextStyle(m, body).size == base.size,
			name + ": Body has no size of its own");
	}
	if (documents == 0)
		Check(false, "no documents with Body and WP Default");
	printf(failures ? "%d failures\n" : "doc_styleedit: documents %d, ok %d\n",
		failures ? failures : documents, checks);
	return failures != 0;
}
