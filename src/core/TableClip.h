// Copying and pasting a table's cells: Edit > Copy and Cut with cells
// selected (TTable::CopySelection: the selected cells as a table of their
// own on the clipboard) and Paste into a table (TTablePasteCommand,
// TTable::Paste, TTable::PasteCell: each copied cell's text and frame
// style given to a cell of the table, from the insert location on). The
// copy holds each cell's text and frame style by their properties, so it
// pastes into any document.
#ifndef GOBE_TABLE_CLIP_H
#define GOBE_TABLE_CLIP_H

#include <string>
#include <vector>

#include "Editing.h"
#include "TableEdit.h"

namespace gobe {

struct Document;

// a copied cell (TCell(TCell*, TTable*, true)): its size, its text (but
// its cell break), its frame style's properties, own and inherited
struct TableClipCell {
	float width = 100, height = 17;
	TextFragment text;
	std::vector<Property> frame;
};

// a row of the copy (TRow): as high as its highest cell
struct TableClipRow {
	float height = 0;
	std::vector<TableClipCell> cells;
};

struct TableClip {
	std::vector<TableClipRow> rows;
	bool Empty() const { return rows.empty(); }
};

// TTable::CopySelection: the selected cells of table part `table` (as
// `edit`, loaded from it, has them), a row of the copy for each row with
// cells selected, the cells side by side from its left
TableClip CopyTableCells(const Document& doc, size_t table, const TableEdit& edit,
	const TableSelection& selection);

// TTable::Paste: the copy's cells given to table `table`'s cells from
// (`row`, `column`) on, row for row and cell for cell, those past the
// table's last row or a row's last cell left out: each such cell's text
// becomes the copied cell's, its frame style the copied one's
// (TStyleManager::CopyStyle). False if nothing could be pasted.
bool PasteTableCells(Document& doc, size_t table, int row, int column, const TableClip& clip,
	std::string& error);

// The copy pasted into text that isn't a table's: a table of its own
// (TTablePart, as Insert Table makes one) as high and wide as its rows and
// cells, inline at `at` in word processing part `container`, with the
// cells' text and styles. Returns the new table part, -1 on failure.
int PasteTableClip(Document& doc, size_t container, TextPosition at, const TableClip& clip,
	std::string& error);

} // namespace gobe

#endif
