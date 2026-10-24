// RTF in and out of word processing documents, as the original does it
// through the Gobe RTF translator (TImportRTF, a version of Microsoft's
// sample RTF reader, and TExportRTF) and TWPImport / TWPExport.
//
// Import reads the keywords in TImportRTF's symbol table (RTFSymbolTable,
// 160 entries): character properties (\f, \fs, \cf, \b, \i, \ul and its
// variants, \uldb, \strike, \super, \sub, \plain), paragraph properties
// (\li, \ri, \fi, \ql, \qc, \qr, \qj, \sl, \sb, \sa, tab stops, \pard),
// \par, \tab, quotes, \bullet, \page, \sect, the font and color tables,
// \', \u, \bin, the character sets (\ansi, \mac, \pc, \pca) and PAGE
// fields; everything else is skipped. Not ported yet (stand-ins): headers
// and footers (read but dropped), pictures (dropped, as when no translator
// can read them) and page numbers (dropped instead of a page number field).
//
// Export writes the original's fixed header, its font table (every
// installed font), its 16-color table, and per text run the paragraph's
// own alignment and indents and the run style's own font, size, face and
// color.
#ifndef GOBE_RTF_H
#define GOBE_RTF_H

#include <string>

#include "TextImport.h"

namespace gobe {

struct Document;

// Imports an RTF file into `doc` (made with CreateBlankDocument). `fonts`
// resolves the font table's names. Unmatched braces don't stop the import;
// `warning` (may be null) then receives the original's alert text.
bool ImportRTF(const std::string& path, Document& doc, std::string& error,
	const FontCatalog& fonts, std::string* warning = nullptr);

// Word processing part `part` as the original writes it.
std::string ExportedRTF(const Document& doc, size_t part, const FontCatalog& fonts);
bool ExportRTF(const Document& doc, size_t part, const std::string& path,
	std::string& error, const FontCatalog& fonts);

} // namespace gobe

#endif
