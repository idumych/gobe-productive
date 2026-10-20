// Check the TabsProperty encoder: for every PTab property in the documents,
// re-encode the decoded tabs and verify byte-for-byte equality. Also verify
// that adding a tab through ApplyParagraphProperty + TabsProperty and resolving
// the paragraph's style shows it.
#include <cstdio>
#include <cstring>
#include <algorithm>

#include "Document.h"
#include "Editing.h"
#include "Styles.h"

using namespace gobe;

int main(int argc, char** argv)
{
	int ok = 0, failed = 0;
	int total_props = 0;

	for (int i = 1; i < argc; i++) {
		Document doc;
		std::string error;
		if (!LoadDocument(argv[i], doc, error)) {
			printf("%s: %s\n", argv[i], error.c_str());
			continue;
		}

		// Check every PTab property: decode it, re-encode, compare bytes
		for (const Property& p : doc.styles.properties) {
			if (p.type != Sig("PTab"))
				continue;
			total_props++;

			// Decode into a temporary style to extract the tabs
			TextStyle s;
			if (!ApplyProperty(p, s)) {
				// PTab should always decode
				failed++;
				continue;
			}

			// Re-encode using TabsProperty
			Property enc = TabsProperty(s.tabs);

			// Compare the payloads
			if (p.data.size() == enc.data.size()
				&& memcmp(p.data.data(), enc.data.data(), p.data.size()) == 0)
				ok++;
			else
				failed++;
		}

		// Test: add a tab via ApplyParagraphProperty + TabsProperty and verify
		// the paragraph's style reflects it
		if (i == 1 && !doc.wordParts.empty()) {
			auto it = doc.wordParts.begin();
			WPPart* wp = &it->second;
			if (!wp->paragraphs.empty()) {
				size_t par = 0;
				uint32_t oldStyle = wp->paragraphs[par].parStyle;
				TextStyle oldStyleResolved = ResolveTextStyle(doc.styles, oldStyle);

				// Create a tab at position 144 (2 inches)
				TabStop tab;
				tab.position = 144;
				tab.type = 2;	// right tab
				tab.fill = 0;
				tab.alignChar = '.';
				tab.fillChar = '.';

				std::vector<TabStop> new_tabs = oldStyleResolved.tabs;
				new_tabs.push_back(tab);
				std::sort(new_tabs.begin(), new_tabs.end(),
					[](const TabStop& a, const TabStop& b) {
						return a.position < b.position;
					});

				Property tabProp = TabsProperty(new_tabs);
				ApplyParagraphProperty(doc, it->first, par, par, tabProp);

				// Check that the paragraph now has the new tab
				uint32_t newStyle = wp->paragraphs[par].parStyle;
				TextStyle newStyleResolved = ResolveTextStyle(doc.styles, newStyle);

				bool found = false;
				for (const TabStop& t : newStyleResolved.tabs) {
					if (t.position == 144 && t.type == 2) {
						found = true;
						break;
					}
				}
				if (!found) {
					printf("  tab insert test failed: tab not found in resolved style\n");
					failed++;
				} else {
					printf("  tab insert test ok\n");
				}
			}
		}
	}

	printf("tabs ok %d, failed %d\n", ok, failed);
	return failed ? 1 : 0;
}
