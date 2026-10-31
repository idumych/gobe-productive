// SYLK (Symbolic Link) spreadsheet format reader and writer.
//
// Import parses SYLK text files: ID record first, then records separated by
// LF or CRLF. C records set cell values with optional X/Y coordinates, F;W
// sets column widths, E ends the file (ignored after).
//
// Export writes SYLK text with ID;PGOBE, column widths (if any), and cell
// records, ending with E.
#ifndef GOBE_SYLK_H
#define GOBE_SYLK_H

#include <cstdint>
#include <string>
#include <vector>

#include "DocArchive.h"

namespace gobe {

// A single cell in a SYLK sheet.
struct SylkCell {
	uint16_t column = 0;	// 0-based
	uint16_t row = 0;		// 0-based
	std::string text;		// cell input (formula, text, or number)
};

// A sheet: all cells and column widths.
struct SylkSheet {
	std::vector<SylkCell> cells;
	// F;W records: each is {first (1-based), last (1-based), width}
	struct ColumnWidth {
		uint16_t first, last;
		float width;
	};
	std::vector<ColumnWidth> colWidths;
};

// Reads SYLK text from `data` into `sheet`. Returns false when there is no
// ID record as the first line, or the input is completely empty.
bool ReadSylk(const std::string& data, SylkSheet& sheet, std::string& error);

// Writes `sheet` to SYLK text.
std::string WriteSylk(const SylkSheet& sheet);

} // namespace gobe

#endif
