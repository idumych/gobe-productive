// For each document given: read the header and table of contents, then run
// every section reader from its section's start and check it consumes
// exactly up to the next section. Prints one line per section:
//   ok   <name>  <size>  <summary>
//   LEN  <name>  consumed N of M
//   ERR  <name>  <error> at <offset>
//   --   <name>  (no reader yet)
#include <cstdio>
#include <fstream>
#include <iterator>

#include "DocArchive.h"
#include "DocSections.h"

using namespace gobe;

int main(int argc, char** argv)
{
	int bad = 0;
	for (int i = 1; i < argc; i++) {
		std::ifstream in(argv[i], std::ios::binary);
		std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)),
			std::istreambuf_iterator<char>());
		DocArchive a(std::move(data));
		DocFile file;
		std::string error;
		if (!ReadDocFile(a, file, error)) {
			printf("%s: %s\n", argv[i], error.c_str());
			bad++;
			continue;
		}
		printf("== %s (%zu sections)\n", argv[i], file.toc.size());
		for (size_t s = 0; s < file.toc.size(); s++) {
			const TocEntry& e = file.toc[s];
			size_t end = s + 1 < file.toc.size() ? file.toc[s + 1].offset : file.tocOffset;
			size_t size = end - e.offset;
			std::string summary;
			try {
				a.Seek(e.offset);
				if (!ReadSection(a, e.name, summary)) {
					printf("  --   %-18s %7zu\n", e.name.c_str(), size);
					continue;
				}
				size_t used = a.Position() - e.offset;
				if (used == size)
					printf("  ok   %-18s %7zu  %s\n", e.name.c_str(), size, summary.c_str());
				else {
					printf("  LEN  %-18s consumed %zu of %zu\n", e.name.c_str(), used, size);
					bad++;
				}
			} catch (const ArchiveError& x) {
				printf("  ERR  %-18s %s at %#zx (section +%zu)\n", e.name.c_str(), x.what(),
					x.Position(), x.Position() - e.offset);
				bad++;
			}
		}
	}
	return bad ? 1 : 0;
}
