// Check property decoding over every sample: count properties per type that
// decode exactly, and print the resolved style of the first runs of text.
#include <cstdio>
#include <map>

#include "Document.h"
#include "ResourceFile.h"
#include "Styles.h"

using namespace gobe;

int main(int argc, char** argv)
{
	std::map<std::string, std::pair<int, int>> stats;	// type -> ok, failed
	for (int i = 1; i < argc; i++) {
		Document doc;
		std::string error;
		if (!LoadDocument(argv[i], doc, error)) {
			printf("%s: %s\n", argv[i], error.c_str());
			continue;
		}
		for (const Property& p : doc.styles.properties) {
			TextStyle s;
			auto& st = stats[FourCCString(p.type)];
			if (ApplyProperty(p, s))
				st.first++;
			else
				st.second++;
		}
		if (i == 1) {
			for (auto& [index, wp] : doc.wordParts) {
				for (auto& par : wp.paragraphs) {
					for (auto& run : par.runs) {
						TextStyle s = ResolveTextStyle(doc.styles, run.style);
						printf("  run style %u: %s / %s %.0fpt face %u color #%02x%02x%02x align %d\n",
							run.style, s.family.c_str(), s.face.c_str(), s.size, s.faceFlags,
							s.color.r, s.color.g, s.color.b, s.alignment);
					}
					break;
				}
			}
		}
	}
	for (auto& [type, st] : stats)
		printf("%-5s decoded %4d  not decoded %4d\n", type.c_str(), st.first, st.second);
}
