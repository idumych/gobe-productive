// Readers for the sections of a Gobe document, in file order (the order of
// TDoc::TDoc(TInputArchive&, TString) / TDoc::Write). Each follows the
// original constructor or Read method named in its comment; the table of
// contents lets tests check that a reader consumes exactly its section.
#ifndef GOBE_DOC_SECTIONS_H
#define GOBE_DOC_SECTIONS_H

#include <cstdint>
#include <array>
#include <string>
#include <vector>

#include "DocArchive.h"

namespace gobe {

// "VersionMgr": TVersionMgr::TVersionMgr(TInputArchive&)
struct VersionMgr {
	std::vector<DocVersion> versions;
};
void ReadVersionMgr(DocArchive& a, VersionMgr& out);

// "PartFactory Table": TPartMgr::ReadFactoryTable
struct PartFactoryEntry {
	uint32_t kind;			// part factory kind ('WORD', 'SPRD', ...)
	uint32_t id;			// id the parts refer to
	std::string name;		// "Word Processing", ...
};
void ReadPartFactoryTable(DocArchive& a, std::vector<PartFactoryEntry>& out);

// "Document Misc": in TDoc::TDoc after the factory table
struct DocumentMisc {
	uint32_t blockCount = 0;	// number of 0x208-byte blocks allocated
	uint32_t value70 = 0;		// TDoc +0x70
	std::vector<std::vector<uint8_t>> messages;	// 5 flattened BMessages
};
void ReadDocumentMisc(DocArchive& a, DocumentMisc& out);

// Properties (TPropertyManager::Read): each is a type code and a payload
// whose size is stored (checked against all samples), so unknown types are
// kept as raw bytes. Text/paragraph attributes: 'TSiz' size, 'TFon' font,
// 'TCol' color, 'PAlg' alignment, 'PLef'/'PInd' indents, ...
struct Property {
	uint32_t type;
	std::vector<uint8_t> data;
};
void ReadPropertyManager(DocArchive& a, std::vector<Property>& out);

// TPropsTable::Read: indexes into the property list
std::vector<uint32_t> ReadPropsTable(DocArchive& a);

// TStyle::TStyle(TStyleManager*, TInputArchive&)
struct Style {
	uint32_t id = 0;
	uint32_t value4 = 0;
	uint32_t value8 = 0;
	uint16_t valueC = 0;
	uint32_t value10 = 0;
	std::string name;
	uint32_t links[3] = {0, 0, 0};	// TStylePtr ids (+0x14, +0x18, +0x1c)
	std::vector<uint32_t> props;		// +0x28
	std::vector<uint32_t> props2;		// +0x50
	std::vector<std::pair<uint16_t, Style>> substyles;
};
void ReadStyle(DocArchive& a, Style& out);

// "StyleMgr": TStyleManager::TStyleManager(TInputArchive&)
struct StyleMgr {
	std::vector<Property> properties;
	std::vector<std::pair<uint32_t, std::string>> names;	// 'STYP' table
	std::vector<Style> styles;
	uint32_t afterSection = 0;		// TDoc +0x6c, stored after StyM in the section
};
void ReadStyleMgr(DocArchive& a, StyleMgr& out);
std::vector<uint8_t> WriteStyleMgr(const StyleMgr& styles);

// "PartMgr": TPartMgr::ReadParts / ReadOnePart. 'PARM', a u32, the part
// count, an index of (kind, record size) per part, the parts, 'ParM'. Each
// part record is its size (u32, counting itself), then 'PART', the factory kind, the part's
// own data (TPart subclass Read), its reference table, its portals
// ('PORT' ... 'PorT') and 'ParT' -- so parts can be walked generically and
// parsed by kind.
struct PartRecord {
	uint32_t kind;				// 'WORD', 'GRFX', 'SPRD', 'PAGE', 'PROT', ...
	size_t offset;				// of the record (after its size)
	std::vector<uint8_t> data;	// the record: 'PART', kind, ... 'ParT'
};
struct PartMgr {
	uint32_t value14 = 0;
	std::vector<PartRecord> parts;
};
void ReadPartMgr(DocArchive& a, PartMgr& out);

// "SelectionMgr" (TSelectionMgr::Read): 'SELM', a count, then per selection
// a record size (u32) and the part factory kind whose ReadSelection reads
// the rest
// (TSelection: part, u32; spreadsheet: + u32, cell, cells list, bool; word
// processing: text ranges + bool (+ bool from major version 2); graphics:
// shape ids; image: a rectangle or a bitmap), 'SelM'.
struct Selection {
	uint32_t id;				// position in the list (TSelPtr index)
	uint32_t kind;
	uint32_t part = 0xffffffff;
	uint32_t value = 0;
	// spreadsheet: the active cell and the ranges ((row, column) pairs)
	uint16_t cell[2] = {};
	std::vector<std::array<uint16_t, 4>> ranges;
	size_t cellAt = 0;			// where cell[] is in the file (the ranges' count follows)
	// word processing: text ranges (start, end), and where they are in the
	// file (to clamp them when the text gets shorter)
	std::vector<std::pair<uint32_t, uint32_t>> textRanges;
	size_t textRangesAt = 0;
};
struct SelectionMgr {
	std::vector<Selection> selections;
	const Selection* Find(uint32_t id) const;
};
void ReadSelectionMgr(DocArchive& a, SelectionMgr& out);

// "LinksMgr" (TLinkTable::Read): 'LINK', a bool, a count of TLink, 'LinK'.
// A link is a named reference into a source part, used by formulas.
struct Link {
	uint32_t value20 = 0;
	bool flag1 = false;
	uint32_t sourcePart = 0xffffffff;	// +0x0c TPart* (part index)
	uint32_t dependentPart = 0xffffffff;	// +0x14
	std::string name;
	uint32_t value1c = 0;
	uint32_t reference = 0xffffffff;	// index into the source part's REFT
};
struct LinksMgr {
	bool value0 = false;
	std::vector<Link> links;
};
void ReadLinksMgr(DocArchive& a, LinksMgr& out);
std::vector<uint8_t> WriteLinksMgr(const LinksMgr& links);

// "SetMgr" (unparseable): 'SETM', 3 zero u32s (ink set counts), 'SetM'.
std::vector<uint8_t> WriteSetMgr();

// "PenMgr" (unparseable): 'PENM', u8 flag, bool, pen count, pen entries,
// double-passthrough, same data, 'PenM'. For blank: count 0.
std::vector<uint8_t> WritePenMgr();

// "SheetMgr" (TSheetMgr::TSheetMgr(TInputArchive&)): TSheet (name, two
// u32s), +0x24 (TExportTranslation::DoExport's sheet count), a count, then each TLeafSheet: TSheet,
// its main part, graphics part, header and footer parts (part indexes,
// 0xffffffff for none), two u32s, two bools (+0x2c: a frame opened as a
// sheet), the paper rectangle and the printable rectangle (TLeafSheet::
// PaperRect, PrintableRect), four bools, the unit and ruler divisions, and
// the flattened print settings BMessage (u32 size, bytes).
struct LeafSheet {
	std::string name;
	uint32_t value4 = 0, value8 = 0;
	uint32_t mainPart = 0xffffffff;		// +0x14
	uint32_t graphicsPart = 0xffffffff;	// +0x18
	uint32_t headerPart = 0xffffffff;	// +0x1c
	uint32_t footerPart = 0xffffffff;	// +0x20
	uint32_t value24 = 0, value28 = 0;
	bool frameSheet = false;			// +0x2c
	bool value2d = false;
	float paper[4] = {0, 0, 612, 792};		// +0x34
	float printable[4] = {72, 72, 540, 720};	// +0x44
	bool flags[4] = {};					// +0x54..+0x57
	uint32_t unit = 0, divisions = 0;	// +0x58, +0x5c
	std::vector<uint8_t> printSettings;	// +0x30, flattened BMessage
};
struct SheetMgr {
	std::string name;
	uint32_t value4 = 0, value8 = 0;
	uint32_t sheetCount = 0;			// +0x24 (the number of sheets in the documents)
	std::vector<LeafSheet> sheets;
};
void ReadSheetMgr(DocArchive& a, SheetMgr& out);
std::vector<uint8_t> WriteSheetMgr(const SheetMgr& mgr);

// "CommandMgr" (unparseable): 'CMDM', count, per-command 'TCMD' blocks, 'CmdM'.
std::vector<uint8_t> WriteCommandMgr();

// "HistoryMgr" (unparseable): TOrderTable::Write (no explicit sig, writes
// count), then two u32s. For blank: count 0, two zero u32s.
std::vector<uint8_t> WriteHistoryMgr();

// "WindowMgr" (unparseable): TDocWindow::Write per window (doc pointer,
// internal TOC, window name). For blank: 0 windows.
std::vector<uint8_t> WriteWindowMgr();

// PartFactory Table write helper.
std::vector<uint8_t> WriteFactoryTable(
	const std::vector<PartFactoryEntry>& factories);

// Document Misc write helper.
std::vector<uint8_t> WriteDocumentMisc(const DocumentMisc& misc);

// SelectionMgr write helper.
std::vector<uint8_t> WriteSelectionMgr(const SelectionMgr& mgr);

// PartMgr write helper — writes from PartRecord data (for creating new docs).
std::vector<uint8_t> WritePartMgrFromRecords(const PartMgr& mgr);

// One section reader per TOC name; returns false if there is none (yet).
bool ReadSection(DocArchive& a, const std::string& name, std::string& summary);

} // namespace gobe

#endif
