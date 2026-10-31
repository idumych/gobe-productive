// Word processing documents as HTML, as the HTML translator writes them
// (HTMLTranslator/TExportHTML.c).
#ifndef GOBE_HTML_EXPORT_H
#define GOBE_HTML_EXPORT_H

#include <string>

namespace gobe {

struct Document;

// TExportHTML::DoTranslate: "<HTML>", the head (the title: `title`, the
// file's name), the body (BGCOLOR from "Text Background"'s fill) with each
// word processing sheet's main text (WriteWord): a <P> per paragraph with
// its style's own alignment, the runs' own size, color and faces as <FONT>,
// <B>, <I>, <U>, <STRIKE>, <SUP>, <SUB>; separator lines as <HR>, fields as
// their text, line breaks as <BR>, an empty paragraph as &nbsp;. Latin-1
// letters as their entities, other characters as numeric ones. Pictures
// (TExportHTML::writeBitmap) and tables aren't written.
std::string HTMLText(const Document& doc, const std::string& title);
bool ExportHTML(const Document& doc, const std::string& path, const std::string& title,
	std::string& error);

} // namespace gobe

#endif
