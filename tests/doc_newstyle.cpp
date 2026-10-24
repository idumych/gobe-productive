// doc_newstyle: Styles > New Style… (NewNamedStyle). In each document with
// text, a paragraph style made from the first paragraph's text ("Copy
// formatting from selection") based on "WP Default" resolves to what that
// text has; it is named, flagged 0x822 and the base's child; a second style
// of the same name isn't made; one based on None has no parent and none of
// the text's properties when not copying; both survive saving and loading.
// Arguments: the sample documents.
#include <cstdio>
#include <string>

#include "Document.h"
#include "Editing.h"
#include "Styles.h"

using namespace gobe;

static int sOk = 0, sFailed = 0;

static void
Check(bool ok, const std::string& what)
{
	if (ok)
		sOk++;
	else {
		sFailed++;
		printf("FAIL %s\n", what.c_str());
	}
}


static bool
SameText(const TextStyle& a, const TextStyle& b)
{
	return a.family == b.family && a.face == b.face && a.size == b.size
		&& a.faceFlags == b.faceFlags && a.color == b.color && a.alignment == b.alignment
		&& a.leftIndent == b.leftIndent && a.firstIndent == b.firstIndent
		&& a.rightIndent == b.rightIndent && a.spaceBefore == b.spaceBefore
		&& a.spaceAfter == b.spaceAfter && a.lineSpacing == b.lineSpacing
		&& a.lineSpacingMultiple == b.lineSpacingMultiple && a.tabs.size() == b.tabs.size();
}


static uint32_t
Named(const Document& doc, const std::string& name)
{
	for (size_t i = 0; i < doc.styles.styles.size(); i++)
		if (doc.styles.styles[i].name == name)
			return uint32_t(i);
	return kNoPart;
}


int
main(int argc, char** argv)
{
	int made = 0;
	for (int i = 1; i < argc; i++) {
		std::string name = argv[i];
		name = name.substr(name.rfind('/') + 1);
		Document doc;
		std::string error;
		if (!LoadDocument(argv[i], doc, error) || doc.MainPart() < 0)
			continue;
		const WPPart* wp = doc.WordPart(size_t(doc.MainPart()));
		if (wp == nullptr || wp->paragraphs.empty())
			continue;
		const WPParagraph& first = wp->paragraphs[0];
		uint32_t from = first.runs.empty() ? first.parStyle : first.runs[0].style;
		TextStyle expected = ResolveTextStyle(doc.styles, from);
		uint32_t base = Named(doc, "WP Default");
		size_t before = doc.styles.styles.size();

		uint32_t style = NewNamedStyle(doc, Sig("Para"), "Style 1", base, from);
		made++;
		Check(style == before, name + ": a new style at the end");
		if (style >= doc.styles.styles.size())
			continue;
		const Style& s = doc.styles.styles[style];
		Check(s.name == "Style 1" && s.value10 == Sig("Para") && s.value8 == 0x822,
			name + ": named, a paragraph style, flags 0x822");
		Check(s.links[0] == base, name + ": based on WP Default");
		Check(SameText(ResolveTextStyle(doc.styles, style), expected),
			name + ": resolves to the first paragraph's text");
		Check(NewNamedStyle(doc, Sig("Para"), "Style 1", base, from) == kNoPart,
			name + ": the name is taken");
		Check(NewNamedStyle(doc, Sig("Para"), "", base, from) == kNoPart,
			name + ": no empty name");

		uint32_t plain = NewNamedStyle(doc, Sig("Para"), "Style 2", kNoPart, kNoPart);
		Check(plain < doc.styles.styles.size() && doc.styles.styles[plain].links[0] == kNoPart
			&& doc.styles.styles[plain].props.empty(),
			name + ": based on None, not copying: no parent, no properties");

		// applied to the first paragraph
		ApplyParagraphStyle(doc, size_t(doc.MainPart()), 0, 0, style);

		std::string path = "/tmp/doc_newstyle.gobe";
		Document reloaded;
		Check(doc.Save(path, error) && LoadDocument(path, reloaded, error),
			name + ": saves and loads (" + error + ")");
		uint32_t again = Named(reloaded, "Style 1");
		Check(again != kNoPart && SameText(ResolveTextStyle(reloaded.styles, again), expected),
			name + ": reloaded, the style resolves the same");
		Check(Named(reloaded, "Style 2") != kNoPart, name + ": reloaded, the plain style is there");
		const WPPart* rp = reloaded.WordPart(size_t(reloaded.MainPart()));
		if (rp != nullptr && !rp->paragraphs.empty()) {
			uint32_t p = rp->paragraphs[0].parStyle;
			// (TStyle::SetParentStyle: applied, links[1])
			Check(p < reloaded.styles.styles.size() && reloaded.styles.styles[p].links[1] == again,
				name + ": reloaded, the first paragraph has it applied");
		}
	}
	printf("newstyle: styles made %d, ok %d, failed %d\n", made, sOk, sFailed);
	return sFailed > 0;
}
