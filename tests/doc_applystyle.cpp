// doc_applystyle: Format bar > Styles with a paragraph style
// (ApplyParagraphStyle): in every document's text parts, each named
// paragraph style applied to the first paragraph not yet in it. The
// paragraph's style is then unnamed with the named one as its parent and
// resolves to the named style with the paragraph's own properties on top
// (TStyleID::SetParentStyle keeps them); the other paragraphs keep their
// styles; applying it again changes nothing; it survives saving and
// loading.
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


int
main(int argc, char** argv)
{
	int applied = 0;
	for (int i = 1; i < argc; i++) {
		std::string name = argv[i];
		name = name.substr(name.rfind('/') + 1);
		Document doc;
		std::string error;
		if (!LoadDocument(argv[i], doc, error))
			continue;
		for (size_t part = 0; part < doc.parts.parts.size(); part++) {
			if (doc.WordPart(part) == nullptr)
				continue;
			for (size_t named = 0; named < doc.styles.styles.size(); named++) {
				// (a copy: applying adds styles)
				const Style n = doc.styles.styles[named];
				if (n.name.empty() || n.value10 != Sig("Para"))
					continue;
				const WPPart* wp = doc.WordPart(part);
				size_t target = wp->paragraphs.size();
				for (size_t p = 0; p < wp->paragraphs.size() && target == wp->paragraphs.size(); p++) {
					uint32_t s = wp->paragraphs[p].parStyle;
					if (s != named && !(s < doc.styles.styles.size()
							&& doc.styles.styles[s].name.empty() && doc.styles.styles[s].links[0] == named))
						target = p;
				}
				if (target == wp->paragraphs.size())
					continue;
				std::string what = name + " part " + std::to_string(part) + " par "
					+ std::to_string(target) + " \"" + n.name + "\"";

				// expected: the named style, then the paragraph's own
				// properties (those of its unnamed style)
				uint32_t old = wp->paragraphs[target].parStyle;
				TextStyle expected = ResolveTextStyle(doc.styles, uint32_t(named));
				if (old < doc.styles.styles.size() && doc.styles.styles[old].name.empty())
					for (uint32_t p : doc.styles.styles[old].props)
						ApplyProperty(doc.styles.properties[p], expected);
				std::vector<uint32_t> before;
				for (const WPParagraph& p : wp->paragraphs)
					before.push_back(p.parStyle);

				Check(ApplyParagraphStyle(doc, part, target, target, uint32_t(named)),
					what + ": applied");
				applied++;
				wp = doc.WordPart(part);
				uint32_t now = wp->paragraphs[target].parStyle;
				Check(now < doc.styles.styles.size() && doc.styles.styles[now].name.empty()
					&& doc.styles.styles[now].links[0] == named, what + ": an unnamed child");
				Check(SameText(ResolveTextStyle(doc.styles, now), expected),
					what + ": resolves to the style and the paragraph's own properties");
				bool others = true;
				for (size_t p = 0; p < wp->paragraphs.size(); p++)
					if (p != target && wp->paragraphs[p].parStyle != before[p])
						others = false;
				Check(others, what + ": other paragraphs unchanged");
				Check(!ApplyParagraphStyle(doc, part, target, target, uint32_t(named)),
					what + ": again changes nothing");

				std::string path = "/tmp/doc_applystyle.gobe";
				Document reloaded;
				Check(doc.Save(path, error) && LoadDocument(path, reloaded, error),
					what + ": saves and loads (" + error + ")");
				const WPPart* rp = reloaded.WordPart(part);
				if (rp != nullptr && target < rp->paragraphs.size()) {
					uint32_t s = rp->paragraphs[target].parStyle;
					bool parent = s < reloaded.styles.styles.size()
						&& reloaded.styles.styles[s].links[0] < reloaded.styles.styles.size()
						&& reloaded.styles.styles[reloaded.styles.styles[s].links[0]].name == n.name;
					Check(parent, what + ": reloaded, based on the style");
					Check(SameText(ResolveTextStyle(reloaded.styles, s), expected),
						what + ": reloaded, resolves the same");
				} else
					Check(false, what + ": reloaded, the paragraph is there");
				// later styles are tried on the document as changed so far
				doc = Document();
				LoadDocument(path, doc, error);
			}
		}
	}
	printf("applystyle: styles applied %d, ok %d, failed %d\n", applied, sOk, sFailed);
	return sFailed > 0;
}
