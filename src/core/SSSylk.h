// A spreadsheet part as SYLK, as Save As writes it through the SYLK
// translator (TExportSYLK, from the translator's TTranslatorPart_SS).
#ifndef GOBE_SS_SYLK_H
#define GOBE_SS_SYLK_H

#include <string>

#include "Sylk.h"

namespace gobe {

struct Document;

// TExportSYLK::DoTranslate: the bounds of the cells in use (B), the
// columns whose width isn't the default (F;W, pixels / 7.45 as
// characters), then each cell with a value or a formula (C): its value
// as calculated (a time as days since 1970 plus 24106, as write_cell
// does), its formula in R1C1 (FormulaToSylk). Cells with neither are left
// out.
SylkSheet SpreadsheetToSylk(const Document& doc, size_t part);

// Writes part `part` (a spreadsheet) to `path` as SYLK text.
bool ExportSylk(const Document& doc, size_t part, const std::string& path, std::string& error);

// TImportSYLK::DoTranslate into part `part` (a spreadsheet): each cell's
// formula (FormulaFromSylk) or else its value as the cell's (text stays
// text, errors stay errors), the column widths (characters * 7, as
// rtd_F has it), the
// cells in use grown to the B record's bounds; the dependencies updated.
// A formula the parser doesn't take leaves the cell its value; their
// number goes in `badFormulas`.
bool SylkToSpreadsheet(Document& doc, size_t part, const SylkSheet& sheet, int& badFormulas);

// Opens a SYLK file as a new spreadsheet document (CreateBlankSpreadsheet,
// then SylkToSpreadsheet). `notice` (if given) receives a message when
// some formulas couldn't be read.
bool ImportSylk(const std::string& path, Document& doc, std::string& error,
	std::string* notice = nullptr);

} // namespace gobe

#endif
