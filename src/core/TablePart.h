// The table part (Parts/Table: TTablePart). Cells are rectangles whose text
// is a frame of a word processing part (the table's text part).
#ifndef GOBE_TABLE_PART_H
#define GOBE_TABLE_PART_H

#include <cstdint>
#include <vector>

#include "DocArchive.h"
#include "WPPart.h"

namespace gobe {

// TCell::TCell(TInputArchive&, TTable*)
struct TableCell {
	uint32_t row = 0, column = 0;	// TLocation
	float origin[2] = {0, 0};		// in the table
	float size[2] = {0, 0};
	uint32_t values[2] = {};
	uint32_t frame[3] = {};			// ReadFramePtr: the cell's text frame
};

// TRow::TRow(TInputArchive&, TTable*)
struct TableRow {
	float top = 0, height = 0;
	std::vector<TableCell> cells;
};

struct TablePart {
	PartHeader header;
	float size[2] = {0, 0};			// TTable +0x18
	float value20 = 1;				// format >= 0x1d
	std::vector<TableRow> rows;
	uint32_t textPart = 0xffffffff;	// TPart*: the word processing part
	uint32_t textPortal = 0;
	// where TTable's data is in the part record (after TPart's header, up
	// to the tail) and the file format it was read in
	size_t bodyStart = 0, bodyEnd = 0;
	int format = 0;
};

// `a` is positioned at the part record; reads the table and its text part.
void ReadTablePart(DocArchive& a, TablePart& out);

// The part record with TTable::Write's data (TTable: the size, the gutter,
// TRow::Write's rows, TCell::Write's cells, the text part and portal) in
// place of what was read, the header and tail as they are
std::vector<uint8_t> WriteTableRecord(const TablePart& table, const std::vector<uint8_t>& record);

} // namespace gobe

#endif
