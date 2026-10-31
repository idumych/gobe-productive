// Tables and pictures made in a document's text (HTML import's <TABLE>
// and <IMG>): a table part (TTablePart, kind 'TBLE') with its text part
// and a frame per cell, or an image part (TPartIM, 'BITS'), each shown
// in a frame inline in a word processing part's text (a FRAM special).
#ifndef GOBE_INLINE_PARTS_H
#define GOBE_INLINE_PARTS_H

#include <cstdint>
#include <string>
#include <vector>

#include "Editing.h"
#include "IMPart.h"

namespace gobe {

struct Document;

// a new table: its columns' widths and rows' heights (points), its cell
// inset (TTable +0x20); each row has a cell per column, `cells[r][c]`
// its text (UTF-8, '\n' between its paragraphs)
struct NewTable {
	std::vector<float> columns;
	std::vector<float> rows;
	float inset = 1;
	std::vector<std::vector<std::string>> cells;
};

// A table part shown in `container` (a word processing part), and its
// text part with a frame per cell, as the original's documents have
// them: the table's frame in a portal of the table in the container
// (TTableFrame), the cells' frames (TWPFrame, inset 2 and 1 points) in
// a portal of the text part in the table, each showing its cell's
// paragraphs. New parts and selections are registered as AddSheet does
// them. Returns the table part (`textPart` its text), -1 on failure.
int AddTablePart(Document& doc, size_t container, const NewTable& table, size_t& textPart,
	std::string& error);

// The first paragraph of cell (row, column) in the table's text part;
// -1 if there is no such cell.
int CellParagraph(const Document& doc, size_t table, size_t row, size_t column);

// The cells' frames' text ranges set again from their paragraphs (after
// their text changed): `paragraphs[i]` cell i's (row by row) paragraph
// count.
void SetCellRanges(Document& doc, size_t table, const std::vector<size_t>& paragraphs);

// An image part (TPartIM: the bitmap, no floating selection, the colors
// black and white) shown in `container` at its own size. Returns the
// part, -1 on failure.
int AddImagePart(Document& doc, size_t container, const ArchiveBitmap& bitmap,
	std::string& error);

struct Frame;

// The frame a frame pointer names (TOutputArchive::WriteFramePtr: the
// part, its portal's id (TPortal +0x20), the frame's (TFrame +0x38, its
// second value)); NULL if there's none.
const Frame* FrameAt(const Document& doc, const uint32_t pointer[3]);

// The part's frame in `container` (its first) shown inline in the text
// at `at`: a FRAM special (the 0x04 character, TMiscFrameInfo) pointing
// at it. Returns the position after it.
TextPosition InsertInlineFrame(Document& doc, size_t container, TextPosition at, size_t part);

} // namespace gobe

#endif
