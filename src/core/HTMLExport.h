// Word processing documents as HTML, as the HTML translator writes them
// (HTMLTranslator/TExportHTML.c).
#ifndef GOBE_HTML_EXPORT_H
#define GOBE_HTML_EXPORT_H

#include <cstdint>
#include <string>
#include <vector>

namespace gobe {

struct Document;

// TExportHTML::DoTranslate: "<HTML>", the head (the title: `title`, the
// file's name), the body (BGCOLOR from "Text Background"'s fill) with each
// word processing sheet's main text (WriteWord): a <P> per paragraph with
// its style's own alignment, the runs' own size, color and faces as <FONT>,
// <B>, <I>, <U>, <STRIKE>, <SUP>, <SUB>; separator lines as <HR>, fields as
// their text, line breaks as <BR>, an empty paragraph as &nbsp;; tables
// (in the text, and table sheets) as <TABLE> (WriteTable). Latin-1
// letters as their entities, other characters as numeric ones. Pictures
// (TExportHTML::writeBitmap: image parts shown in the text, as PNG) go
// to `images` as files for the "<title>.images" folder, with <IMG> in
// the text; none are written when it's null.
struct HTMLImage {
	std::string name;			// "image.0.png", ...
	std::vector<uint8_t> bytes;
};
std::string HTMLText(const Document& doc, const std::string& title,
	std::vector<HTMLImage>* images = nullptr);
// HTMLText to `path`, the pictures into "<title>.images" beside it.
bool ExportHTML(const Document& doc, const std::string& path, const std::string& title,
	std::string& error);

} // namespace gobe

#endif
