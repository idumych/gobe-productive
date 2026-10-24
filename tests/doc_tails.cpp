// doc_tails: every part tail (references, portals, frames) written back by
// WritePartTail is byte for byte what was read, and page frames list the
// frames on them. Arguments: the sample documents.
#include <cstdio>
#include <string>

#include "Document.h"
#include "PartTail.h"

using namespace gobe;

int
main(int argc, char** argv)
{
	int tails = 0, same = 0, pages = 0, pagesOk = 0;
	for (int i = 1; i < argc; i++) {
		Document doc;
		std::string error;
		if (!LoadDocument(argv[i], doc, error))
			continue;
		for (size_t p = 0; p < doc.parts.parts.size(); p++) {
			if (!doc.tailRead[p])
				continue;
			const std::vector<uint8_t>& record = doc.parts.parts[p].data;
			std::vector<uint8_t> written = WritePartTail(doc.tails[p], doc.parts.parts[p].kind);
			std::vector<uint8_t> original(record.begin() + doc.tails[p].offset, record.end());
			tails++;
			if (written == original)
				same++;
			else
				printf("%s part %zu: tail differs (%zu vs %zu bytes)\n", argv[i], p,
					written.size(), original.size());
			if (doc.parts.parts[p].kind != Sig("PAGE"))
				continue;
			// each page frame's children: header, footer, drawing, columns;
			// every pointer names a frame that exists
			for (const Frame& page : doc.tails[p].portals[0].frames) {
				pages++;
				bool ok = page.children.size() >= 3;
				for (const Frame::Pointer& c : page.children) {
					if (c.part == kNoPart)
						continue;
					ok = ok && c.part < doc.tails.size() && doc.tailRead[c.part]
						&& c.portal < doc.tails[c.part].portals.size()
						&& c.frame < doc.tails[c.part].portals[c.portal].frames.size();
				}
				pagesOk += ok;
				if (!ok)
					printf("%s: page frame children don't resolve\n", argv[i]);
			}
		}
	}
	printf("tails %d, written identically %d; page frames %d, children resolved %d\n",
		tails, same, pages, pagesOk);
	bool ok = tails > 0 && same == tails && pages > 0 && pagesOk == pages;
	printf("tails checks %s, differ %d\n", ok ? "ok" : "FAIL", tails - same + pages - pagesOk);
	return ok ? 0 : 1;
}
