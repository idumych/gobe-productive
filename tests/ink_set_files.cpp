// ink_set_files: ink set files and a document's ink sets. Every file in
// the original's "Ink Sets" folder reads (an 'INKS' set of its kind with
// its inks) and writes back byte for byte; a set made anew is written
// with the original's header and reads back; AvailableInkSets lists the
// application's three "Standard" sets and the folder's by name. Each
// document's "SetMgr" reads and writes back byte for byte; a document
// given a set of its own keeps it through a save.
// Arguments: the documents (the ink sets are found beside them).
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>

#include "DocArchive.h"
#include "Document.h"
#include "InkSets.h"
#include "ResourceFile.h"

using namespace gobe;

static int sOk = 0, sFailed = 0;

static void
Check(bool ok, const std::string& what)
{
	if (ok)
		sOk++;
	else {
		sFailed++;
		printf("FAIL %s\n", what.c_str());
	}
}


static std::vector<uint8_t>
ReadFile(const std::string& path)
{
	std::ifstream in(path, std::ios::binary);
	return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), {});
}


int
main(int argc, char** argv)
{
	if (argc < 2) {
		printf("ink_set_files: no documents\n");
		return 1;
	}
	// the application's folder: the documents' Samples or Labels folder's
	std::string first = argv[1];
	std::string app = first.substr(0, first.rfind('/'));
	app = app.substr(0, app.rfind('/'));
	std::string folder = app + "/Ink Sets";
	const char* names[] = {"Clouds", "Dark", "Large", "Pinstripe", "Slate", "Stucco", "WebSafe"};
	for (const char* name : names) {
		std::vector<uint8_t> data = ReadFile(folder + "/" + name);
		InkSet set;
		std::string error;
		Check(ReadInkSetFile(data, set, error), std::string(name) + " read " + error);
		// (the names are the files' with a space after: "Web Safe ")
		Check(!set.name.empty() && !set.inks.empty() && set.kind >= 0 && set.kind <= 2,
			std::string(name) + ": its name, kind and inks");
		Check(WriteInkSetFile(set) == data, std::string(name) + " written back byte for byte");
	}
	// a set made anew
	InkSet made;
	made.kind = kPatternSet;
	made.name = "Made";
	made.columns = 4;
	made.cell = 16;
	Ink ink;
	ink.set = true;
	ink.color = {10, 20, 30, 255};
	ink.low = {200, 210, 220, 255};
	made.inks.push_back(ink);
	std::string path = "/tmp/ink_set_files_test";
	std::string error;
	Check(SaveInkSetFile(path, made, error), "a new set saved " + error);
	InkSet back;
	Check(LoadInkSetFile(path, back, error) && back.name == "Made" && back.kind == kPatternSet
		&& back.columns == 4 && back.inks.size() == 1 && back.inks[0].low.g == 210
		&& WriteInkSetFile(back) == WriteInkSetFile(made), "the new set read back " + error);

	// the sets to choose from
	ResourceFile resources;
	bool haveResources = resources.Load(app + "/Gobe Productive");
	Check(haveResources, "the application's resources");
	std::vector<InkSetEntry> all = AvailableInkSets(haveResources ? &resources : nullptr,
		{folder, "/nonexistent/Ink Sets"});
	Check(all.size() == 3 + 7 && all[0].set.name == "Standard" && all[0].set.kind == kColorSet
		&& all[1].set.kind == kPatternSet && all[2].set.kind == kGradientSet
		&& !all[0].set.inks.empty() && all[3].set.name == "Clouds " && all[9].set.name == "Web Safe "
		&& all[3].path == folder + "/Clouds", "the sets available");
	std::vector<InkSetEntry> names2 = AvailableInkSets(nullptr, {folder}, false);
	Check(names2.size() == 7 && names2[0].set.name == "Clouds " && names2[0].set.inks.empty()
		&& names2[0].set.kind == all[3].set.kind, "the sets' names without their inks");

	// the documents' own sets
	int documents = 0, own = 0;
	for (int i = 1; i < argc; i++) {
		std::vector<uint8_t> data = ReadFile(argv[i]);
		DocArchive a(data);
		DocFile file;
		if (!ReadDocFile(a, file, error))
			continue;
		for (const TocEntry& e : file.toc) {
			if (e.name != "SetMgr")
				continue;
			documents++;
			try {
				a.Seek(e.offset);
				SetMgr sets;
				ReadSetMgr(a, sets);
				std::vector<uint8_t> written = WriteSetMgr(sets);
				Check(std::equal(written.begin(), written.end(), data.begin() + e.offset)
					&& a.Position() == e.offset + written.size(),
					std::string(argv[i]) + ": SetMgr written back byte for byte");
				own += int(sets.sets[0].size() + sets.sets[1].size() + sets.sets[2].size());
			} catch (const ArchiveError& x) {
				Check(false, std::string(argv[i]) + ": SetMgr " + x.what());
			}
		}
	}
	Check(documents == argc - 1, "every document has a SetMgr");
	// a set of the document's own, saved
	Document doc;
	Check(LoadDocument(argv[1], doc, error), "a document " + error);
	doc.inkSets.sets[kPatternSet].push_back(made);
	doc.inkSetsChanged = true;
	Check(doc.Save("/tmp/ink_set_files_doc.gobe", error), "saved " + error);
	Document again;
	Check(LoadDocument("/tmp/ink_set_files_doc.gobe", again, error)
		&& again.inkSets.sets[kPatternSet].size() == 1
		&& again.inkSets.sets[kPatternSet][0].name == "Made", "its set read back " + error);

	printf("ink_set_files: documents %d (sets of their own %d), ok %d, failed %d\n", documents,
		own, sOk, sFailed);
	return sFailed ? 1 : 0;
}
