// Verify WriteStyleMgr reproduces the exact bytes ReadStyleMgr reads: for each
// document, find the original "StyleMgr" section bytes from the TOC, compare
// with WriteStyleMgr(doc.styles), then read the written bytes back and check
// style/property counts match.
#include <cstdio>
#include <fstream>
#include <iterator>

#include "DocArchive.h"
#include "DocSections.h"
#include "Document.h"

using namespace gobe;

int main(int argc, char** argv)
{
	int ok = 0, differ = 0;

	for (int i = 1; i < argc; i++) {
		const char* path = argv[i];

		// Load the full document (parses styles)
		Document doc;
		std::string error;
		if (!LoadDocument(path, doc, error)) {
			printf("%s: %s\n", path, error.c_str());
			differ++;
			continue;
		}

		// Find the StyleMgr section in the TOC and extract its original bytes
		const TocEntry* entry = nullptr;
		for (const TocEntry& e : doc.file.toc) {
			if (e.name == "StyleMgr") {
				entry = &e;
				break;
			}
		}
		if (!entry) {
			printf("%s: no StyleMgr section\n", path);
			differ++;
			continue;
		}
		size_t secEnd = (static_cast<int>(entry - &doc.file.toc[0]) + 1 < (int)doc.file.toc.size())
			? doc.file.toc[entry - &doc.file.toc[0] + 1].offset
			: doc.file.tocOffset;
		size_t secSize = secEnd - entry->offset;
		if (secSize == 0) {
			printf("%s: empty StyleMgr section\n", path);
			differ++;
			continue;
		}
		const uint8_t* original = doc.bytes.data() + entry->offset;

		// Write the styles back
		std::vector<uint8_t> written = WriteStyleMgr(doc.styles);
		size_t writtenSize = written.size();

		if (writtenSize != secSize) {
			printf("%s: size differ %zu vs %zu\n", path, writtenSize, secSize);
			differ++;
			continue;
		}

		// Byte-by-byte compare
		size_t diffOffset = writtenSize;
		for (size_t k = 0; k < writtenSize; k++) {
			if (written[k] != original[k]) {
				diffOffset = k;
				break;
			}
		}
		if (diffOffset < writtenSize) {
			printf("%s: differ at offset %zu (%zu vs %zu)\n", path, diffOffset,
				writtenSize, secSize);
			// Print hex diff for the first 32 bytes
			size_t start = diffOffset > 32 ? diffOffset - 32 : 0;
			size_t end = diffOffset + 32 < writtenSize ? diffOffset + 32 : writtenSize;
			printf("  original: ");
			for (size_t k = start; k < end; k++)
				printf("%02x", original[k]);
			printf("\n  written:  ");
			for (size_t k = start; k < end; k++)
				printf("%02x", written[k]);
			printf("\n");
			differ++;
			continue;
		}

		// Read written bytes back and verify counts match
		DocArchive archive(std::move(written));
		StyleMgr roundtrip;
		try {
			ReadStyleMgr(archive, roundtrip);
		} catch (const ArchiveError& x) {
			printf("%s: roundtrip read error: %s\n", path, x.what());
			differ++;
			continue;
		}
		if (roundtrip.styles.size() != doc.styles.styles.size()
			|| roundtrip.properties.size() != doc.styles.properties.size()) {
			printf("%s: roundtrip counts differ (%zu styles, %zu props vs %zu, %zu)\n",
				path, roundtrip.styles.size(), roundtrip.properties.size(),
				doc.styles.styles.size(), doc.styles.properties.size());
			differ++;
			continue;
		}

		ok++;
	}

	printf("stylemgr sections ok %d, differ %d\n", ok, differ);
	return differ ? 1 : 0;
}
