// Read every image part and check it ends where its tail begins.
#include <cstdio>

#include "Document.h"
#include "IMPart.h"

using namespace gobe;

int main(int argc, char** argv)
{
	int ok = 0, bad = 0;
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
			if (rec.kind != Sig("BITS"))
				continue;
			DocArchive pa(rec.data);
			pa.CopyVersions(versions);
			IMPart im;
			try {
				ReadIMPart(pa, im);
				if (doc.tailRead[p] && pa.Position() == doc.tails[p].offset) {
					ok++;
					printf("%s part %zu: %dx%d color space %#x, %zu bytes\n", argv[i], p,
						im.image.Width(), im.image.Height(), im.image.colorSpace,
						im.image.bits.size());
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
	printf("image parts ok %d, failed %d\n", ok, bad);
	return bad ? 1 : 0;
}
