// Saving: an unchanged document saved through SaveDocument (the PartMgr
// section rebuilt from its records) must come out byte for byte the same.
#include <cstdio>

#include "DocWriter.h"
#include "Document.h"
#include "WPPart.h"
#include "Editing.h"

using namespace gobe;

int main(int argc, char** argv)
{
	int ok = 0, bad = 0;
	for (int i = 1; i < argc; i++) {
		Document doc;
		std::string error;
		if (!LoadDocument(argv[i], doc, error))
			continue;
		DocChanges changes;
		// force the PartMgr rewrite: every record "replaced" by itself, word
		// processing parts written from what was read
		for (size_t p = 0; p < doc.parts.parts.size(); p++)
			changes.parts[p] = doc.parts.parts[p].data;
		changes.sections["LinksMgr"] = WriteLinksMgr(doc.links);
		// the dependency tables, rebuilt, must come out as they were
		for (const auto& [p, ss] : doc.spreadsheetParts) {
			SSPart before = ss;
			if (UpdateDependencies(doc, p))
				printf("%s: part %zu: links added\n", argv[i], p);
			const SSPart& after = *doc.SpreadsheetPart(p);
			bool same = before.dependents.size() == after.dependents.size();
			for (size_t k = 0; same && k < after.dependents.size(); k++)
				same = before.dependents[k].a == after.dependents[k].a
					&& before.dependents[k].b == after.dependents[k].b;
			if (!same)
				printf("%s: part %zu: calculation order rebuilt differently\n", argv[i], p);
		}
		for (const auto& [p, ss] : doc.spreadsheetParts) {
			changes.parts[p] = WriteSSPart(ss, doc.parts.parts[p].data);
			if (changes.parts[p] != doc.parts.parts[p].data)
				printf("%s: SPRD part %zu written differently\n", argv[i], p);
		}
		for (const auto& [p, wp] : doc.wordParts) {
			changes.parts[p] = WriteWPPart(wp, doc.parts.parts[p].data);
			if (changes.parts[p] != doc.parts.parts[p].data)
				printf("%s: WORD part %zu written differently\n", argv[i], p);
		}
		std::vector<uint8_t> saved = SaveDocument(doc, changes);
		if (saved == doc.bytes)
			ok++;
		else {
			size_t at = 0;
			while (at < saved.size() && at < doc.bytes.size() && saved[at] == doc.bytes[at])
				at++;
			printf("%s: differs at %zu (sizes %zu, %zu)\n", argv[i], at, saved.size(),
				doc.bytes.size());
			bad++;
		}
	}
	printf("saved identically %d, differ %d\n", ok, bad);
	return bad ? 1 : 0;
}
