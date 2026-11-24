// WriteFile (DocWriter.cpp): a save writes a new file and renames it over
// the old one, keeping the old file's permissions, extended attributes
// (Cosmoe's file attributes) and a link to it.
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <sys/stat.h>
#ifdef __linux__
#include <sys/xattr.h>
#endif
#include <unistd.h>
#include "DocWriter.h"

using namespace gobe;
namespace fs = std::filesystem;

static int ok = 0, failed = 0;

static void
Check(bool condition, const char* what)
{
	if (condition)
		ok++;
	else {
		failed++;
		printf("FAIL %s\n", what);
	}
}

static std::string
Contents(const fs::path& path)
{
	std::ifstream in(path, std::ios::binary);
	return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

int
main()
{
	char pattern[] = "/tmp/gobe-write-file-XXXXXX";
	if (mkdtemp(pattern) == nullptr)
		return 1;
	fs::path dir = pattern, file = dir / "doc";
	std::ofstream(file) << "old";
	chmod(file.c_str(), 0640);
#ifdef __linux__	// (Cosmoe's attributes; on Haiku the save copies BFS attributes)
	bool xattrs = setxattr(file.c_str(), "user.test.keep", "kept", 4, 0) == 0;
#endif

	std::string error;
	std::vector<uint8_t> data = {'n', 'e', 'w'};
	Check(WriteFile(file.string(), data, error), "WriteFile succeeds");
	Check(Contents(file) == "new", "the new contents are in place");
	struct stat st;
	Check(stat(file.c_str(), &st) == 0 && (st.st_mode & 0777) == 0640, "permissions kept");
#ifdef __linux__
	if (xattrs) {
		char value[16] = "";
		ssize_t n = getxattr(file.c_str(), "user.test.keep", value, sizeof value);
		Check(n == 4 && std::string(value, 4) == "kept", "extended attribute kept");
	} else
		printf("note: no user xattrs on %s: the attribute check is skipped\n", pattern);
#endif
	Check(!fs::exists(dir / "doc.saving"), "no temporary file left");

	// through a link: the file it points to is replaced, the link stays
	fs::create_symlink("doc", dir / "link");
	data = {'v', '2'};
	Check(WriteFile((dir / "link").string(), data, error), "WriteFile through a link");
	Check(fs::is_symlink(dir / "link") && Contents(file) == "v2", "the link kept, its file written");

	fs::remove_all(dir);
	printf("write_file: ok %d, failed %d\n", ok, failed);
	return failed != 0;
}
