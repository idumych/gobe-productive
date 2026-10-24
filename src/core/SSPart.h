// The spreadsheet part's data (TPartSS::TPartSS(TInputArchive&)): the used
// range, the default style, the locked cells, the cells with column and row
// header cells (sizes), the dependency table and the display settings.
#ifndef GOBE_SS_PART_H
#define GOBE_SS_PART_H

#include <cstdint>
#include <string>
#include <vector>

#include "DocArchive.h"
#include "Formula.h"
#include "WPPart.h"

namespace gobe {

// TRange::Read: two cell ids (TCellID: field_0x0, field_0x2)
struct SSRange {
	CellID from;
	CellID to;
};

// TDataCell and subclasses (TSSDataManager::ReadCell). Every cell starts
// with a u16 and a TStyleID.
struct SSCell {
	CellID id;
	uint32_t kind = 0;			// 'NONE', 'Bool', 'Err#', 'Lng#', 'Flt#',
								// 'Dbl#', 'Strn', 'Time', 'Fmla', 'FmEr', 'Head'
	uint16_t flags = 0;			// TDataCell +0x0c
	uint32_t style = 0xffffffff;	// TStyleID
	double number = 0;			// Bool, Lng#, Flt#, Dbl#, Time, Head (size)
	uint32_t value = 0;			// Err# code (also FmEr), Lng#, Time
	std::string text;			// Strn; FmEr: the formula as typed
	std::string text2;			// FmEr
	uint32_t extra[2] = {};		// FmEr
	std::vector<FormulaToken> formula;	// Fmla
};

// THeaderCell for a column or row: index and its cell (number = size)
struct SSHeader {
	uint32_t index;
	SSCell cell;
};

// TSSFillInfo
struct SSFill {
	std::vector<SSRange> cells;
	uint32_t style;
	SSRange range;
	float rect[4];
};

struct SSPart {
	PartHeader header;
	SSRange used;				// +0x60
	uint32_t style = 0xffffffff;	// +0x68
	std::vector<SSRange> locked;	// +0x6c TCellsList: LockedCells
	bool autoCalculate = false;	// +0x80 (Options > Auto Calculate)
	std::vector<SSHeader> columns;
	std::vector<SSHeader> rows;
	std::vector<SSCell> cells;
	// TSSDepManager ('depm')
	std::vector<CellID> dependents;	// the formula cells, in calculation order
	// TLinkDepInfo: a link (TLink*, index into LinksMgr) and the cells
	// whose formulas use it
	struct LinkDep {
		uint32_t link = 0;
		std::vector<SSRange> cells;
		bool flag = false;
		uint32_t value = 0;
	};
	std::vector<LinkDep> linkDepInfo;
	uint32_t linkDeps = 0;
	// TSSDisplayManager ('disp')
	float defaultHeight = 16;	// +0x0 DefRowHeight
	float defaultWidth = 72;	// +0x4 DefColumnWidth
	// +0x08 grid, +0x0c header, +0x10 column, +0x14 row, +0x18 locked
	uint32_t displayStyles[5] = {};
	// TSSDisplayOptions 'dopt': show grid, locked cells, formulas, column
	// headers, row headers, circular references
	bool options[6] = {};
	std::vector<SSFill> fills;	// TSSFillManager
	uint32_t frame[3] = {};		// format > 0x15: ReadFramePtr

	// 'ss +' to the end of 'rowi', the 'data' block and the 'depm' block
	// in the part record (all written from the parsed form)
	size_t headerStart = 0;
	size_t dataStart = 0, dataEnd = 0;
	size_t depmStart = 0, depmEnd = 0;
	CellID dataEndID;			// the terminating cell id (column 0xffff)

	const SSCell* Cell(uint16_t a, uint16_t b) const;
	// TSSDisplayManager::ColumnWidth / RowHeight
	float ColumnWidth(uint16_t column) const;
	float RowHeight(uint16_t row) const;
	const SSHeader* Column(uint16_t column) const;
	const SSHeader* Row(uint16_t row) const;
};

// `a` is positioned at the part record; reads up to and including 'ss -'.
void ReadSSPart(DocArchive& a, SSPart& out);

// The part record with the cells written from `part` (in their order);
// everything else is copied from `record`, the record it was read from.
std::vector<uint8_t> WriteSSPart(const SSPart& part, const std::vector<uint8_t>& record);

} // namespace gobe

#endif
