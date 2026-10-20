// Read every graphics part and check it ends where its tail begins.
#include <cstdio>
#include <map>

#include "Document.h"
#include "GRPart.h"
#include "PartTail.h"
#include "ResourceFile.h"

using namespace gobe;

int main(int argc, char** argv)
{
	int ok = 0, bad = 0;
	std::map<std::string, int> kinds;
	for (int i = 1; i < argc; i++) {
		Document doc;
		std::string error;
		if (!LoadDocument(argv[i], doc, error))
			continue;
		DocArchive versions({});
		versions.SetVersions(doc.file.header.version.values[0],
			doc.file.header.version.values[3], doc.file.header.version.values[2]);
		for (size_t p = 0; p < doc.parts.parts.size(); p++) {
			const PartRecord& rec = doc.parts.parts[p];
			if (rec.kind != Sig("GRFX"))
				continue;
			DocArchive pa(rec.data);
			pa.CopyVersions(versions);
			GRPart gr;
			try {
				ReadGRPart(pa, gr);
				if (doc.tailRead[p] && pa.Position() == doc.tails[p].offset) {
					ok++;
					for (auto& l : gr.layers)
						for (auto& s : l.shapes)
							kinds[FourCCString(s.kind)]++;
				} else {
					printf("%s part %zu: ends at %zu, tail at %zu\n", argv[i], p,
						pa.Position(), doc.tails[p].offset);
					bad++;
				}
			} catch (const ArchiveError& x) {
				printf("%s part %zu: %s at %zu\n", argv[i], p, x.what(), x.Position());
				bad++;
			}
		}
	}
	printf("graphics parts ok %d, failed %d; shapes:", ok, bad);
	for (auto& [k, n] : kinds)
		printf(" %s=%d", k.c_str(), n);
	printf("\n");
	return bad ? 1 : 0;
}
