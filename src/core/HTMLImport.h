// HTML opened as a word processing document, as the HTML translator reads
// it (HTMLTranslator/TImportHTML.c and HTMLTree.c).
#ifndef GOBE_HTML_IMPORT_H
#define GOBE_HTML_IMPORT_H

#include <functional>
#include <string>

#include "IMPart.h"

namespace gobe {

struct Document;

// TImportHTML::DoTranslate and HTMLTree's walk over the elements, into a
// new word processing document (CreateBlankDocument). The text starts in
// Dutch801 Rm BT at 12 points (HTMLTree's initial state); each element
// changes the state for its contents (pushState / popState): B and STRONG
// bold, I, EM, CITE and VAR italic, U underline, STRIKE, SUB, SUP; TT,
// CODE, KBD, SAMP and PRE the fixed font (Courier10 BT, be_fixed_font on
// BeOS) at 12; H1-H6 a blank line first, bold, 24, 18, 14, 12, 10, 9
// points; BIG and SMALL a size up or down; FONT SIZE (1-7: 9, 10, 12, 14,
// 18, 24, 36, or +n/-n) and COLOR; A HREF the link color, underlined; P,
// DIV and headings ALIGN (center, right), CENTER; P, lists and HR start a
// new line, BR breaks the line (0x01); UL and OL indent, LI a bullet or
// number and a tab; HR a separator line; TD and TH a tab, TR a new line.
// Entities: ISOlat1table's names, &amp; &lt; &gt; &quot;, numeric and hex.
// HEAD, TITLE, SCRIPT and STYLE aren't text. TABLE makes a table (HTMLTree::
// BeginTable, TableAddRow, TableAddCell; TTableImport): a table part in an
// inline frame on a line of its own, a cell per TD or TH with its text
// (TH bold), its WIDTH and CELLPADDING (see InlineParts); IMG SRC a
// picture (an image part in an inline frame) when its file can be read
// (`readPicture`, else a PNG reader), else its ALT text. A relative SRC
// is from `folder`. Forms aren't made (their text comes through).
//
// reads a picture file into a B_RGBA32 bitmap; false if it can't
using PictureReader = std::function<bool(const std::string& path, ArchiveBitmap& bitmap)>;
bool ImportHTMLText(const std::string& html, Document& doc, std::string& error,
	const std::string& folder = "", const PictureReader& readPicture = {});
bool ImportHTML(const std::string& path, Document& doc, std::string& error,
	const PictureReader& readPicture = {});

} // namespace gobe

#endif
