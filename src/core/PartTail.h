// The tail every part record ends with (TPartMgr::ReadOnePart after the
// part's own data): its reference table, its portals with their frames, and
// 'ParT'. Frames are where parts are placed: a part's portal says which
// part it is shown in (Portal::part, e.g. a page part), and its frames are
// the rectangles there (a word processing part's text flows through its
// frames in order). Part pointers are indexes into the part list.
#ifndef GOBE_PART_TAIL_H
#define GOBE_PART_TAIL_H

#include <cstdint>
#include <string>
#include <vector>

#include "DocArchive.h"

namespace gobe {

constexpr uint32_t kNoPart = 0xffffffff;

// TReference::Read
struct Reference {
	uint32_t value14 = 0;
	bool flag0 = false, flag1 = false;
	std::string name;
	uint32_t selection = 0xffffffff;
	uint32_t valueC = 0, value10 = 0;
};

// TFrame::TFrame(TInputArchive&): TShape (TTransObject: a float, origin,
// size; a style; three u32) + the part shown, the containing part, four u32;
// then the owning part's frame class adds its fields
struct Frame {
	float angle = 0;			// TTransObject +0x0
	float origin[2] = {0, 0};	// +0x4
	float extent[2] = {0, 0};	// +0xc: width, height
	uint32_t style = 0;
	uint32_t shape[3] = {};
	uint32_t part = kNoPart;		// +0x2c: the part this frame shows
	uint32_t container = kNoPart;	// +0x30: the part it is placed in
	uint32_t values[4] = {};
	// TSSFrame: the cells shown (TRange: rows/columns from, to) and their
	// rectangle in sheet coordinates; TFrameIM/TFrameGR: the image/drawing
	// rectangle shown
	// TWPFrame +0x4c/+0x50: the first character the frame shows and how
	// many; `textAt` is where they are in the part record
	uint32_t textStart = 0, textLength = 0;
	size_t textAt = 0;
	uint16_t cells[4] = {};
	float sheetRect[4] = {0, 0, -1, -1};
	bool exact = true;			// false: the subclass fields were skipped by scanning
	// TPageFrame: the frames on the page (TFrame pointers: part, portal,
	// frame; 0xffffffff for none): the header, the footer, the sheet's
	// drawing, then the text columns
	struct Pointer {
		uint32_t part = kNoPart, portal = kNoPart, frame = kNoPart;
		bool operator==(const Pointer& o) const
			{ return part == o.part && portal == o.portal && frame == o.frame; }
	};
	std::vector<Pointer> children;
	// the frame class's fields as read (after the four values, up to
	// 'FraM'), written back by WritePartTail with textStart/textLength
	// (TWPFrame) and children (TPageFrame) updated
	std::vector<uint8_t> raw;
};

// TPortal::TPortal(TInputArchive&, TPart*): part, three u32, the frames;
// subclasses (TWPPortal, TPagePortal, ...) add fields
struct Portal {
	uint32_t part = kNoPart;	// the part this portal is shown in (none: root)
	uint32_t values[3] = {};
	std::vector<Frame> frames;
	uint32_t view = 0;			// TGRPortal: the graphics view shown
	bool exact = true;
	std::vector<uint8_t> raw;	// the portal class's fields (after the frames)
};

struct PartTail {
	size_t offset = 0;			// in the part record
	std::vector<Reference> refs;
	uint32_t refNext = 0;		// TRefTable +0x0: the next reference's id
	bool refsChanged = false;	// refs written from the parsed form
	std::vector<Portal> portals;
	std::vector<uint8_t> refsRaw;	// 'REFT' ... 'RefT' as read
};

// Finds and reads the tail of a part record (PartRecord::data). The part's
// own data isn't needed: candidate 'REFT' positions are tried from the end
// until one parses exactly to 'ParT'.
bool ReadPartTail(const std::vector<uint8_t>& record, uint32_t kind,
	const DocArchive& versions, PartTail& out);
// The tail as the original writes it (the references, the portals and their
// frames, 'ParT'); a tail read unchanged comes back byte for byte.
std::vector<uint8_t> WritePartTail(const PartTail& tail, uint32_t kind);

} // namespace gobe

#endif
