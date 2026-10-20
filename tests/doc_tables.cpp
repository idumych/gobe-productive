// Read every table part and check it ends where its tail begins.
#include <cstdio>

#include "Document.h"
#include "TablePart.h"

using namespace gobe;

int main(int argc, char** argv)
{
	int ok = 0, bad = 0;
	size_t cells = 0;
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
			if (rec.kind != Sig("TBLE"))
				continue;
			DocArchive pa(rec.data);
			pa.CopyVersions(versions);
			TablePart tb;
			try {
				ReadTablePart(pa, tb);
				if (doc.tailRead[p] && pa.Position() == doc.tails[p].offset) {
					ok++;
					cells += 0; for (auto& r : tb.rows) cells += r.cells.size();
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
	printf("table parts ok %d, failed %d, cells %zu\n", ok, bad, cells);
	return bad ? 1 : 0;
}
