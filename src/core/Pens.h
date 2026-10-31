// Pens (TPen and its subclasses TNormalPen, TDotPen, TDashPen, THashPen,
// TUnknownPen; TPenMgr): the application's (PENS resources) and a
// document's own ("PenMgr"), and a shape's pen type (TPropPenType 'PEN ').
#ifndef GOBE_PENS_H
#define GOBE_PENS_H

#include <cstdint>
#include <string>
#include <vector>

#include "Styles.h"

namespace gobe {

class DocArchive;
class DocWriter;

// A pen's kind: 0 a normal pen (NORM), 1 dots (PDOT: the gap, filled or
// hollow), 2 dashes (DASH: the dash and the gap), 3 hash marks (HASH: the
// gap); -1 another kind (TUnknownPen: its signature and data kept); its
// name, whether it prints
struct PenType {
	int kind = 0;
	std::string name;
	bool print = true;
	float dash = 0, gap = 0;
	bool filled = false;
	uint32_t unknownKind = 0;
	std::vector<uint8_t> unknownData;
};

// TPen::operator==: the same name, printing, kind and numbers
bool SamePen(const PenType& a, const PenType& b);

// a 'PENS' resource (the application's pens, TPenMgr::loadDefaultPens):
// the kind, the name (C string), printable, then the kind's numbers
// (u32s, little-endian); false if it isn't one
bool ParsePenResource(const std::vector<uint8_t>& data, PenType& pen);

// TPen::Write: the kind's signature, the data's size, then the data
// (TPen::WritePenData: the name, printable; and the subclass's)
void WritePen(DocWriter& w, const PenType& pen);
void ReadPen(DocArchive& a, PenType& pen);		// throws ArchiveError

// the 'PEN ' property of a pen (TPropPenType::WritePropData: present,
// then TPen::Write) and back
Property PenTypeProperty(const PenType& pen);
bool ReadPenType(const Property& property, PenType& pen);

// A document's pens ("PenMgr", TPenMgr): +0x14 the application's pens
// are used (a document's always), +0x15 it has its own (the
// application's copied on the first change: copyAppPens), the pens
struct PenMgr {
	bool shared = true;
	bool own = false;
	std::vector<PenType> pens;
};
// TPenMgr(TInputArchive&): 'PENM', the size, the two flags, the count,
// the pens (TPenFactory::ReadPen), 'PenM'
void ReadPenMgr(DocArchive& a, PenMgr& pens);	// throws ArchiveError
std::vector<uint8_t> WritePenMgr(const PenMgr& pens);

// the pens a document offers (TPenMgr::GetPen, GetPenCount): its own,
// else the application's
const std::vector<PenType>& DocumentPens(const PenMgr& pens, const std::vector<PenType>& app);
// TPenMgr::AddPen, ReplacePen, DeletePen (the application's copied first;
// a pen the same as one there added under a new name: NewPenName, the
// name with 1, 2, ... after it); false if `index` is out of range
void AddPen(PenMgr& pens, const std::vector<PenType>& app, PenType pen);
bool ReplacePen(PenMgr& pens, const std::vector<PenType>& app, size_t index, const PenType& pen);
bool DeletePen(PenMgr& pens, const std::vector<PenType>& app, size_t index);
std::string NewPenName(const std::vector<PenType>& pens, const std::string& name);

} // namespace gobe

#endif
