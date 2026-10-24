// Getting the original Gobe Productive from what a user can still have: the
// CD image (ISO 9660, with Joliet or Rock Ridge names), its "Install Gobe
// Productive" installer (a BeOS ELF stub with a SoftwareValet package, the
// format Haiku's PackageInstaller reads), a bare .pkg, or the files copied
// from the CD or an old installation. BeOS can't run the installer on any
// current system, so the port unpacks it itself. Nothing is changed in the
// source; the files go to a folder of the port's own.
#ifndef GOBE_ORIGINAL_IMPORT_H
#define GOBE_ORIGINAL_IMPORT_H

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace gobe {

// A SoftwareValet package item (FilI file, FldI folder, LnkI link, ScrI
// script) and where its data is.
struct ValetItem {
	enum Kind { kFile, kFolder, kLink, kScript } kind = kFile;
	std::string path;			// "Gobe Productive 2.0b2/Gobe Productive 2.0/..."
	uint64_t offset = 0;		// OffT: of its data block, from the package start
	uint64_t size = 0;			// OrgS: the file's size
	uint32_t mode = 0;
	std::string link;			// LnkI: the target
};

// Finds the package in `data` (an installer or a .pkg: "AlB\x1a" followed by
// the 'PhIn' tag) and lists its items. False, with `error`, if there is none
// or it doesn't parse.
bool ReadValetPackage(const std::vector<uint8_t>& data, size_t& base,
	std::vector<ValetItem>& items, std::string& error);
// The file's bytes (its FiDa sections, zlib-compressed); its BeOS
// attributes (FBeA) are skipped.
bool ReadValetItem(const std::vector<uint8_t>& data, size_t base, const ValetItem& item,
	std::vector<uint8_t>& out, std::string& error);

// An ISO 9660 image's files: paths with Joliet names if it has them, else
// Rock Ridge's, else the plain names (";1" dropped).
struct IsoFile {
	std::string path;			// "/GOBE/Install Gobe Productive"
	uint64_t offset = 0;		// in the image
	uint64_t size = 0;
	bool folder = false;
};
bool ReadIsoImage(const std::string& image, std::vector<IsoFile>& files, std::string& error);
// Whether `image` holds a BeOS file system (BFS) instead (not readable here).
bool IsBfsImage(const std::string& image);

// Whether these bytes are the original program: an x86 ELF with BeOS
// resources holding its application signature.
bool IsOriginalProgram(const std::vector<uint8_t>& data);

// Imports the original from `source` (an ISO, the installer, a .pkg, the
// "Gobe Productive" program or a folder holding any of these) into
// `destination` (made if needed): the "Gobe Productive 2.0" folder with the
// program, its parts, dictionaries, samples and the rest. `program` gets
// the program's path. `progress` (optional) hears each file written.
bool ImportOriginal(const std::string& source, const std::string& destination,
	std::string& program, std::string& error,
	const std::function<void(const std::string&)>& progress = nullptr);

} // namespace gobe

#endif
