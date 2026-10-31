// Ink sets (TInkSet, TInkSetManager): the application's (COLR, BPAT and
// GRAD resources), the ones in files (the original's "Ink Sets" folder,
// SETF archives) and a document's own ("SetMgr").
#ifndef GOBE_INK_SETS_H
#define GOBE_INK_SETS_H

#include <cstdint>
#include <string>
#include <vector>

#include "Styles.h"

namespace gobe {

class DocArchive;
class ResourceFile;

// TInkSet: kind 0 colors, 1 patterns, 2 gradients (+0x0); its name (+0x4),
// the menu's columns (+0x24) and cell size (+0x28), the gap between cells
// (+0x30: 2, 0 when cells are under 6) and its inks (TPenInk: a color, a
// pattern or a gradient, as ReadInk reads them)
enum {
	kColorSet = 0,
	kPatternSet = 1,
	kGradientSet = 2
};
struct InkSet {
	int kind = kColorSet;
	std::string name;
	int columns = 1;
	int cell = 10;			// a cell's size in the menu
	int gap = 2;
	std::vector<Ink> inks;
	// a file's archive header (98 bytes, TArchiveHeader) kept for writing
	// it back; empty: a new file's
	std::vector<uint8_t> header;
};

// A 'COLR' resource: columns, cell size and gap, then per color R, G, B
// (little-endian u32s). False if it isn't one.
bool ParseColorSet(const std::vector<uint8_t>& resource, InkSet& set);

// A 'BPAT' resource: columns and cell size, then per pattern its high and
// low colors (four little-endian u32s each, R, G, B, A) and its 8 pattern
// bytes (a u32 each); the data after the header a multiple of 64 bytes.
// False if it isn't.
bool ParsePatternSet(const std::vector<uint8_t>& resource, InkSet& set);

// A 'GRAD' resource: columns and cell size, then gradients until 'gEND':
// the type ('GDdr', 'GDrl'; little-endian u32s throughout), the number of
// colors, four colors, the angle, a rectangle (not used), then two floats
// as NUL-terminated text, a radial gradient's center. False if it's cut
// short.
bool ParseGradientSet(const std::vector<uint8_t>& resource, InkSet& set);

// TInkSet::Read/Write: 'INKS', the name, the count, columns, cell size,
// kind, gap, the inks (TPenInk::Write), 'InkS'.
void ReadInkSet(DocArchive& a, InkSet& set);		// throws ArchiveError
std::vector<uint8_t> InkSetBytes(const InkSet& set);

// An ink set file (TInkSetManager::WriteInkSet): the archive header (type
// 'SETF'), 2, 1, the kind, the set, 'END!'. Byte for byte what was read.
bool ReadInkSetFile(const std::vector<uint8_t>& file, InkSet& set, std::string& error);
std::vector<uint8_t> WriteInkSetFile(const InkSet& set);
// the same from and to a path
bool LoadInkSetFile(const std::string& path, InkSet& set, std::string& error);
bool SaveInkSetFile(const std::string& path, const InkSet& set, std::string& error);

// The sets there are to choose from (TInkSetManager::LoadAppInkSets,
// LoadInkSetsFromFolder): the application's ("Standard": COLR, BPAT and
// GRAD 0 of `resources`, if given), then the files in `folders` in order
// (the original's "Ink Sets" folder, the port's settings folder "Gobe
// Productive port/Ink Sets"; a missing folder or a file that isn't a set
// skipped), each folder's sorted by name. `path` is the file's ("" for
// the application's); without `load` a file's set has no inks.
struct InkSetEntry {
	InkSet set;
	std::string path;
};
std::vector<InkSetEntry> AvailableInkSets(const ResourceFile* resources,
	const std::vector<std::string>& folders, bool load = true);

// A document's own sets ("SetMgr", TInkSetManager::Read/Write): 'SETM',
// for colors, patterns and gradients a count and the sets, 'SetM'.
struct SetMgr {
	std::vector<InkSet> sets[3];
};
void ReadSetMgr(DocArchive& a, SetMgr& sets);		// throws ArchiveError
std::vector<uint8_t> WriteSetMgr(const SetMgr& sets);

} // namespace gobe

#endif
