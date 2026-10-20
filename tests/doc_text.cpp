// Print the text of every word processing part in the given documents.
#include <cstdio>
#include <fstream>
#include <iterator>

#include "DocArchive.h"
#include "DocSections.h"
#include "WPPart.h"

using namespace gobe;

int main(int argc, char** argv)
{
	int bad = 0;
	for (int i = 1; i < argc; i++) {
		std::ifstream in(argv[i], std::ios::binary);
		std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)),
			std::istreambuf_iterator<char>());
		DocArchive a(data);
		DocFile file;
		std::string error;
		if (!ReadDocFile(a, file, error)) {
			printf("%s: %s\n", argv[i], error.c_str());
			continue;
		}
		for (const TocEntry& e : file.toc) {
			if (e.name != "PartMgr")
				continue;
			a.Seek(e.offset);
			PartMgr parts;
			ReadPartMgr(a, parts);
			printf("== %s\n", argv[i]);
			for (const PartRecord& p : parts.parts) {
				if (p.kind != Sig("WORD"))
					continue;
				DocArchive pa(p.data);
				pa.CopyVersions(a);
				WPPart wp;
				try {
					ReadWPPart(pa, wp);
					size_t rest = pa.Size() - pa.Position();
					bool tail = rest >= 4 && p.data[p.data.size() - 4] == 'P'
						&& p.data[p.data.size() - 3] == 'a';
					std::string all;
					for (auto& par : wp.paragraphs)
						all += par.text;
					for (char& c : all)
						if (c == '\r' || c == '\n')
							c = '|';
					printf("  WORD %zu paragraphs %zu sections, %zu bytes after wp-- %s: %.110s\n",
						wp.paragraphs.size(), wp.sections.size(), rest,
						tail ? "(ends ParT)" : "(NO ParT)", all.c_str());
				} catch (const ArchiveError& x) {
					printf("  WORD ERR %s at +%zu\n", x.what(), x.Position());
					bad++;
				}
			}
		}
	}
	return bad ? 1 : 0;
}
