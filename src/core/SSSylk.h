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

} // namespace gobe

#endif
