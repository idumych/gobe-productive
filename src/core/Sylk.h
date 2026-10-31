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

// TExportSYLK::convertToSYLKformula: a Gobe formula of the cell at `row`,
// `column` (from 0) as SYLK's E field: the leading '=' dropped, ".." as
// ':', each cell reference in R1C1 form. A reference is a whole word (no
// letter, digit, '_' or '.' just before or after it): an optional '$', one
// or two capital letters (A = column 0 ... Z = 25, AA = 26, AB = 27 ...),
// an optional '$', then digits (the row, from 1). An absolute part
// (with '$') is written with its number from 1 ("R5", "C2"), a relative
// one as the offset from the formula's cell in brackets ("R[-1]",
// "C[2]"), or as "R" / "C" alone when the offset is 0. Text in double
// quotes is left as it is.
//   "=A1+$B$2" at row 1, column 1 -> "R[-1]C[-1]+R2C2"
//   "=SUM(B1..B3)" at row 3, column 1 -> "SUM(R[-3]C:R[-1]C)"
std::string FormulaToSylk(const std::string& formula, uint16_t row, uint16_t column);

// TImportSYLK::rtd_C_field_E: the reverse: '=' put first, ':' as "..",
// each R1C1 reference (a whole word: 'R', digits or a bracketed signed
// offset or nothing, 'C', the same) as an A1 one, '$' before its
// absolute parts. Text in double quotes is left as it is, and so is a
// reference that would land before row 1 or column A.
//   "R[-1]C[-1]+R2C2" at row 1, column 1 -> "=A1+$B$2"
std::string FormulaFromSylk(const std::string& formula, uint16_t row, uint16_t column);

} // namespace gobe

#endif
