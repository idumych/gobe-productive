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
	float border = 0;			// the table's pen (TTableImport: 0 transparent)
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


// A frame placed in a drawing (a graphics part) is one of its shapes:
// the frame gets the drawing's next shape id, and a FRAM shape pointing
// at it goes into `layer` (TPartGR::InsertChildFrame). False if
// `drawing` isn't a drawing or the frame isn't there.
bool AddFrameShape(Document& doc, size_t drawing, size_t layer, const uint32_t pointer[3]);

// The ToolBar's part tool and the Frame menu's parts (TPartGR::DragOut
// with a part kind, TDocWindow::DoInsertFrame): a new part of `kind`
// ('WORD' text, 'SPRD' a spreadsheet, 'GRFX' a drawing, 'TBLE' a 2 x 2
// table, 'BITS' a white picture) framed in drawing `drawing` at `rect`
// (left, top, right, bottom in the drawing), its FRAM shape in `layer`.
// Returns the part, -1 with `error`.
int AddDrawingFrame(Document& doc, size_t drawing, size_t layer, uint32_t kind,
	const float rect[4], std::string& error);

// Frame > Make Linked Frame (TDocWindow::MakeLinkedFrame): a text frame
// in a drawing gets a next frame in its portal, as big, right of it; the
// text flows on into it. `made` is its pointer.
bool AddLinkedFrame(Document& doc, const uint32_t pointer[3], uint32_t made[3],
	std::string& error);

// Frame > Clone Frame (TDocWindow::DoCloneFrame): the part shown again,
// in a portal of its own in the same drawing, 18 points down and right.
bool CloneFrame(Document& doc, const uint32_t pointer[3], uint32_t made[3], std::string& error);

// TFrameCommand: a frame's flags (TFrame +0x40) after Autogrow
// Vertically ('sM34', 8), Autogrow Horizontally ('sM35', 4) or
// Scrollbars ('sM36', 0x20): growing turns the scroll bars off, the
// scroll bars the growing.
uint32_t FrameCommandFlags(uint32_t flags, uint32_t command);

} // namespace gobe

#endif
