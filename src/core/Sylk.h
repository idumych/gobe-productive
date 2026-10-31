// SYLK: the records Gobe Productive's translator reads and writes
// (ID, B, C, F, E), cells only: values, formulas (as their text) and
// column widths.
#ifndef GOBE_SYLK_H
#define GOBE_SYLK_H

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace gobe {

struct SylkCell {
	uint16_t row = 0, column = 0;	// from 0 (SYLK's Y and X are from 1)
	enum Kind { kNumber, kString, kBool, kError, kEmpty } kind = kEmpty;
	double number = 0;				// kNumber; kBool: 0 or 1
	std::string text;				// kString: the text; kError: "#DIV/0!" etc.
	std::string formula;			// the E field as written (R1C1), "" if none
};

struct SylkSheet {
	std::vector<SylkCell> cells;	// in the order read
	std::map<uint16_t, int> columnWidths;	// column (from 0) -> width in characters
	uint16_t rows = 0, columns = 0;	// the B record's Y and X (0 if none)
};

// Reads SYLK text. False with an error if it doesn't start with an ID
// record or a record is malformed.
bool ReadSylk(const std::string& data, SylkSheet& sheet, std::string& error);
// Writes the sheet as SYLK: ID, B, the F width records, a C record per
// cell, E. Lines end in "\r\n" as SYLK files do.
std::string WriteSylk(const SylkSheet& sheet);

} // namespace gobe

#endif
