// ImportOriginal on the original's own download: the "Install Gobe
// Productive" installer itself, a folder holding it, and an ISO 9660 image
// (plain names, made here) holding it. The files it writes must equal those
// tools/unpkg.py (an independent Python reader) extracted into
// original/installed; the importer must refuse a file that is none of these.
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "OriginalImport.h"

using namespace gobe;
namespace fs = std::filesystem;

static int failures = 0, checks = 0;

static void
Check(bool ok, const std::string& what)
{
	checks++;
	if (!ok) {
		printf("FAIL: %s\n", what.c_str());
		failures++;
	}
}


static std::vector<uint8_t>
Bytes(const fs::path& path)
{
	std::ifstream in(path, std::ios::binary);
	return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)),
		std::istreambuf_iterator<char>());
}


static void
Put32Both(std::vector<uint8_t>& d, size_t at, uint32_t v)
{
	for (int i = 0; i < 4; i++) {
		d[at + i] = uint8_t(v >> (8 * i));
		d[at + 7 - i] = uint8_t(v >> (8 * i));
	}
}


// a directory record (ECMA-119 9.1)
static std::vector<uint8_t>
Record(uint32_t lba, uint32_t size, bool folder, const std::string& name)
{
	size_t length = 33 + name.size() + (name.size() % 2 == 0 ? 1 : 0);
	std::vector<uint8_t> r(length, 0);
	r[0] = uint8_t(length);
	Put32Both(r, 2, lba);
	Put32Both(r, 10, size);
	r[25] = folder ? 2 : 0;
	r[28] = 1;
	r[32] = uint8_t(name.size());
	memcpy(&r[33], name.data(), name.size());
	return r;
}


// an image with /GOBE/INSTALL.;1 holding `file`
static void
MakeIso(const fs::path& path, const std::vector<uint8_t>& file)
{
	const uint32_t rootLba = 18, gobeLba = 19, fileLba = 20;
	uint32_t fileSectors = uint32_t((file.size() + 2047) / 2048);
	std::vector<uint8_t> image(size_t(fileLba + fileSectors) * 2048, 0);
	uint8_t* pvd = &image[16 * 2048];
	pvd[0] = 1;
	memcpy(pvd + 1, "CD001", 5);
	pvd[6] = 1;
	std::vector<uint8_t> root = Record(rootLba, 2048, true, std::string(1, '\0'));
	memcpy(pvd + 156, root.data(), 34);
	uint8_t* end = &image[17 * 2048];
	end[0] = 255;
	memcpy(end + 1, "CD001", 5);
	size_t at = rootLba * 2048;
	for (const std::vector<uint8_t>& r : {Record(rootLba, 2048, true, std::string(1, '\0')),
			Record(rootLba, 2048, true, std::string(1, '\1')),
			Record(gobeLba, 2048, true, "GOBE")}) {
		memcpy(&image[at], r.data(), r.size());
		at += r.size();
	}
	at = gobeLba * 2048;
	for (const std::vector<uint8_t>& r : {Record(gobeLba, 2048, true, std::string(1, '\0')),
			Record(rootLba, 2048, true, std::string(1, '\1')),
			Record(fileLba, uint32_t(file.size()), false, "INSTALL.;1")}) {
		memcpy(&image[at], r.data(), r.size());
		at += r.size();
	}
	memcpy(&image[fileLba * 2048], file.data(), file.size());
	std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(image.data()),
		std::streamsize(image.size()));
}


int
main(int argc, char** argv)
{
	fs::path original;
	for (const char* root : {"original", "../original"})
		if (fs::exists(fs::path(root) / "Gobe-x86" / "Install Gobe Productive"))
			original = root;
	if (original.empty()) {
		printf("import_original: no original download, ok 0\n");
		return 1;
	}
	fs::path installer = original / "Gobe-x86" / "Install Gobe Productive";
	fs::path expected = original / "installed" / "Gobe Productive 2.0b2" / "Gobe Productive 2.0";
	fs::path scratch = fs::temp_directory_path() / "gobe-import-test";
	fs::remove_all(scratch);
	fs::create_directories(scratch);

	auto compare = [&](const std::string& program, const std::string& what) {
		Check(fs::path(program).filename() == "Gobe Productive", what + ": the program found");
		fs::path folder = fs::path(program).parent_path();
		int same = 0, files = 0;
		for (auto& e : fs::recursive_directory_iterator(expected)) {
			if (!e.is_regular_file() || e.path().extension() == ".json")
				continue;
			files++;
			fs::path mine = folder / fs::relative(e.path(), expected);
			if (fs::exists(mine) && Bytes(mine) == Bytes(e.path()))
				same++;
			else if (same + 20 > files)
				printf("  differs: %s\n", fs::relative(e.path(), expected).c_str());
		}
		Check(files > 100 && same == files, what + ": " + std::to_string(same) + " of "
			+ std::to_string(files) + " files as unpkg.py extracted them");
	};

	// the installer
	std::string program, error;
	int written = 0;
	Check(ImportOriginal(installer.string(), (scratch / "a").string(), program, error,
		[&](const std::string&) { written++; }), "from the installer (" + error + ")");
	Check(written > 100, "progress for each file");
	compare(program, "installer");
	Check(IsOriginalProgram(Bytes(program)), "the program is the original");

	// a folder holding it
	fs::create_directories(scratch / "cd" / "Gobe");
	fs::copy_file(installer, scratch / "cd" / "Gobe" / "Install Gobe Productive");
	Check(ImportOriginal((scratch / "cd").string(), (scratch / "b").string(), program, error),
		"from a folder (" + error + ")");
	compare(program, "folder");

	// an ISO image holding it
	MakeIso(scratch / "gobe.iso", Bytes(installer));
	std::vector<IsoFile> files;
	Check(ReadIsoImage((scratch / "gobe.iso").string(), files, error) && files.size() == 2
		&& files[1].path == "/GOBE/INSTALL" && files[1].size == fs::file_size(installer),
		"the ISO's files read");
	Check(ImportOriginal((scratch / "gobe.iso").string(), (scratch / "c").string(), program,
		error), "from the ISO (" + error + ")");
	compare(program, "ISO");

	// the program copied out of an installation
	Check(ImportOriginal(program, (scratch / "d").string(), program, error)
		&& fs::exists(program), "from the program itself (" + error + ")");

	// something else
	std::ofstream(scratch / "notes.txt") << "hello";
	Check(!ImportOriginal((scratch / "notes.txt").string(), (scratch / "e").string(), program,
		error) && !error.empty(), "a text file refused");

	fs::remove_all(scratch);
	printf(failures ? "%d failures\n" : "import_original: ok %d\n", failures ? failures : checks);
	return failures != 0;
}
