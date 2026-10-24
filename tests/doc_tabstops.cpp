// doc_tabstops: one PTab property per tab stop (TPropParTab::Replaces: a
// new tab replaces only the one at its position), RemoveParagraphTab, and
// SpaB/SpaA's "in lines" flag. Arguments: the sample documents.
#include <cstdio>
#include <string>
#include <vector>

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


static std::string
Describe(const std::vector<TabStop>& tabs)
{
	std::string s;
	for (const TabStop& t : tabs)
		s += "(" + std::to_string(int(t.position)) + " " + std::to_string(t.type) + ")";
	return s;
}


static TabStop
Tab(float position, int type)
{
	TabStop t;
	t.position = position;
	t.type = type;
	return t;
}


int
main(int argc, char** argv)
{
	// a blank document: tabs added one by one
	Document doc;
	std::string error;
	CreateBlankDocument(doc, error);
	size_t part = doc.wordParts[0].first;
	auto tabs = [&]() {
		return ResolveTextStyle(doc.styles, doc.wordParts[0].second.paragraphs[0].parStyle).tabs;
	};
	ApplyParagraphProperty(doc, part, 0, 0, TabProperty(Tab(144, 2)));
	ApplyParagraphProperty(doc, part, 0, 0, TabProperty(Tab(72, 0)));
	std::vector<TabStop> t = tabs();
	Check(t.size() == 2 && t[0].position == 72 && t[0].type == 0 && t[1].position == 144
		&& t[1].type == 2, "two tabs " + Describe(t));
	ApplyParagraphProperty(doc, part, 0, 0, TabProperty(Tab(72, 1)));
	t = tabs();
	Check(t.size() == 2 && t[0].position == 72 && t[0].type == 1 && t[1].type == 2,
		"same position replaces " + Describe(t));
	Check(RemoveParagraphTab(doc, part, 0, 0, 144), "remove returns true");
	t = tabs();
	Check(t.size() == 1 && t[0].position == 72, "removed " + Describe(t));
	Check(!RemoveParagraphTab(doc, part, 0, 0, 500), "removing a missing tab changes nothing");
	// every PTab property holds one tab
	bool single = true;
	for (const Property& p : doc.styles.properties) {
		TextStyle s;
		if (p.type == Sig("PTab"))
			single = single && ApplyProperty(p, s) && s.tabs.size() == 1;
	}
	Check(single, "one tab per PTab property");
	// saved and read back
	std::string path = "/tmp/doc_tabstops.gobe";
	Check(doc.Save(path, error), "save " + error);
	t = tabs();
	Check(t.size() == 1 && t[0].position == 72 && t[0].type == 1, "after save " + Describe(t));

	// spacing: points when the flag is clear
	ApplyParagraphProperty(doc, part, 0, 0, SpacingProperty(Sig("SpaB"), 6, false));
	ApplyParagraphProperty(doc, part, 0, 0, SpacingProperty(Sig("SpaA"), 0.5f));
	TextStyle s = ResolveTextStyle(doc.styles, doc.wordParts[0].second.paragraphs[0].parStyle);
	Check(s.spaceBefore == 6 && !s.spaceBeforeLines && s.spaceAfter == 0.5f && s.spaceAfterLines,
		"spacing in points and lines");

	// the samples: a new tab keeps the paragraph's tabs
	int paragraphs = 0;
	for (int i = 1; i < argc; i++) {
		Document d;
		if (!LoadDocument(argv[i], d, error))
			continue;
		for (auto& [index, wp] : d.wordParts) {
			for (size_t k = 0; k < wp.paragraphs.size(); k++) {
				std::vector<TabStop> before
					= ResolveTextStyle(d.styles, wp.paragraphs[k].parStyle).tabs;
				if (before.empty())
					continue;
				ApplyParagraphProperty(d, index, k, k, TabProperty(Tab(999, 2)));
				std::vector<TabStop> after
					= ResolveTextStyle(d.styles, d.WordPart(index)->paragraphs[k].parStyle).tabs;
				bool kept = after.size() == before.size() + 1;
				for (size_t j = 0; kept && j < before.size(); j++)
					kept = after[j].position == before[j].position
						&& after[j].type == before[j].type;
				Check(kept, std::string(argv[i]) + " paragraph " + std::to_string(k)
					+ ": " + Describe(before) + " -> " + Describe(after));
				paragraphs++;
				break;
			}
		}
	}
	Check(paragraphs > 0, "sample paragraphs with tabs: " + std::to_string(paragraphs));
	printf("tabstops ok %d, failed %d\n", sOk, sFailed);
	return sFailed > 0 ? 1 : 0;
}
