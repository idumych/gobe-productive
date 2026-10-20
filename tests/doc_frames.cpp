// Read every part's tail (references, portals, frames) in the given
// documents; print the frame tree of the first and stats for all.
#include <cstdio>
#include <map>

#include "Document.h"
#include "PartTail.h"
#include "ResourceFile.h"

using namespace gobe;

int main(int argc, char** argv)
{
	std::map<std::string, std::pair<int, int>> stats;	// kind -> tails ok, failed
	std::map<std::string, std::pair<int, int>> exact;	// kind -> frames exact, scanned
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
			std::string kind = FourCCString(rec.kind);
			PartTail tail;
			if (!ReadPartTail(rec.data, rec.kind, versions, tail)) {
				stats[kind].second++;
				continue;
			}
			stats[kind].first++;
			for (auto& portal : tail.portals)
				for (auto& f : portal.frames)
					(f.exact ? exact[kind].first : exact[kind].second)++;
			if (i == 1) {
				printf("part %zu %s: %zu refs, %zu portals\n", p, kind.c_str(),
					tail.refs.size(), tail.portals.size());
				for (auto& portal : tail.portals)
					for (auto& f : portal.frames)
						printf("    frame (%.0f,%.0f) %.0fx%.0f angle %.0f in part %d (%s)%s\n",
							f.origin[0], f.origin[1], f.extent[0], f.extent[1], f.angle,
							(int)f.container, f.container < doc.parts.parts.size()
								? FourCCString(doc.parts.parts[f.container].kind).c_str() : "-",
							f.exact ? "" : " [scanned]");
			}
		}
	}
	for (auto& [k, s] : stats)
		printf("%-5s tails ok %3d failed %3d  frames exact %3d scanned %3d\n", k.c_str(),
			s.first, s.second, exact[k].first, exact[k].second);
}
