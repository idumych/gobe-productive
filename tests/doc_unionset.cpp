// doc_unionset: the documents' styles keep their properties in
// TPropsTable's order (by type, tabs by position), and each style's stored
// union set (props2) is TStyle::UpdateUnionSet of its parents; styles the
// port makes while editing follow both. Arguments: the sample documents.
#include <cstdio>
#include <string>

#include "Document.h"
#include "Editing.h"
#include "Styles.h"

using namespace gobe;

int
main(int argc, char** argv)
{
	int styles = 0, sortedOwn = 0, sortedUnion = 0, unionOk = 0, edited = 0, editedOk = 0;
	for (int i = 1; i < argc; i++) {
		Document doc;
		std::string error;
		if (!LoadDocument(argv[i], doc, error))
			continue;
		for (size_t s = 0; s < doc.styles.styles.size(); s++) {
			const Style& style = doc.styles.styles[s];
			styles++;
			sortedOwn += PropsSorted(doc.styles, style.props);
			sortedUnion += PropsSorted(doc.styles, style.props2);
			bool same = StyleUnionSet(doc.styles, style) == style.props2;
			unionOk += same;
			if (!same && unionOk + 5 > styles)
				printf("%s: style %zu union set differs\n", argv[i], s);
		}
		// edit: every word processing paragraph's first character bold and
		// a tab stop; the new styles must be consistent too
		size_t before = doc.styles.styles.size();
		for (auto& [index, wp] : doc.wordParts) {
			for (size_t k = 0; k < wp.paragraphs.size(); k++) {
				if (wp.paragraphs[k].text.size() < 2)
					continue;
				ApplyCharacterProperty(doc, index, {k, 0}, {k, 1}, TextFaceProperty(2));
				TabStop tab;
				tab.position = 100;
				ApplyParagraphProperty(doc, index, k, k, TabProperty(tab));
			}
		}
		for (size_t s = before; s < doc.styles.styles.size(); s++) {
			const Style& style = doc.styles.styles[s];
			edited++;
			editedOk += PropsSorted(doc.styles, style.props) && PropsSorted(doc.styles, style.props2)
				&& StyleUnionSet(doc.styles, style) == style.props2;
		}
	}
	printf("styles %d: own sorted %d, union sorted %d, union matches %d; edited styles %d ok %d\n",
		styles, sortedOwn, sortedUnion, unionOk, edited, editedOk);
	bool ok = styles > 0 && sortedOwn == styles && sortedUnion == styles && unionOk == styles
		&& edited > 0 && editedOk == edited;
	printf("unionset checks %s, failed %d\n", ok ? "ok" : "FAIL", ok ? 0 : 1);
	return ok ? 0 : 1;
}
