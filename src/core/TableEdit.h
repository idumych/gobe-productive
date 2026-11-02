// Editing a table (Parts/Table: TTable, TRow, TCell and the Table menu's
// commands), on a copy of the table as TTable holds it: rows of cells, each
// cell with its location, rectangle, spans, its text frame and the text that
// frame shows. The operations follow TTable's functions one for one; the
// table is then put back into the document (the table part's record, its
// text part's paragraphs in frame order, the cells' frames).
#ifndef GOBE_TABLE_EDIT_H
#define GOBE_TABLE_EDIT_H

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "PartTail.h"
#include "WPPart.h"

namespace gobe {

struct Document;

// TCell
struct TableEditCell {
	int row = 0, column = 0;		// TLocation (+0x4, +0x8)
	float x = 0, y = 0;				// +0xc
	float width = 0, height = 0;	// +0x14
	int rowSpan = 1, columnSpan = 1;	// +0x1c, +0x20
	bool ignore = false;			// +0x2d (ClearIgnoreOnRecalc)
	int id = -1;					// its text and frame (TableEdit::texts)
};

// TRow: its height (+0x14), the height it was given (+0x18), its cells
struct TableEditRow {
	float height = 17, height2 = 17;
	std::vector<TableEditCell> cells;
};

// a cell's frame and text: the paragraphs it shows, the last ended by the
// cell break (0x05)
struct TableEditText {
	Frame frame;
	bool made = false;				// a new frame (TRow::InsertCell)
	std::vector<WPParagraph> paragraphs;
};

// a table selection (TTablePart::CreateTableSelection(left, right, from,
// to)): in rows `from` to `to`, the cells between `left` and `right`
// (TRow::AddToSelection)
struct TableSelection {
	float left = 0, right = 0;
	int from = 0, to = -1;
	bool Empty() const { return to < from; }
};

struct TableEdit {
	float size[2] = {0, 0};			// TTable +0x18
	float gutter = 1;				// +0x20 (Cell Gutter…)
	std::vector<TableEditRow> rows;
	std::vector<float> columnEnds;	// +0x24 (RecalcColumnEndTable)
	std::vector<TableEditText> texts;	// by cell id
	std::vector<int> frameOrder;	// the cells' ids in the portal's order
	std::vector<WPParagraph> trailer;	// the text after the cells'
	// TCell::MinimumHeight's text: how high cell `id`'s text is laid out
	// in its frame (none given: one 12-point line, 15)
	std::function<float(int id)> textHeight;

	// TTable::GetCellPtr
	TableEditCell* Cell(int row, int column);
	const TableEditCell* Cell(int row, int column) const;
	// TTable::FindCell: the cell `x`, `y` is in (its rectangle grown by 2
	// with `grow`), NULL if none
	TableEditCell* FindCell(float x, float y, bool grow = false);
	TableEditCell* CellWithId(int id);
	// TTable::InRowResizeArea: the row whose bottom edge `x`, `y` is within
	// 2 of (the last row of the cell there), -1 if none
	int InRowResizeArea(float x, float y);
	// TTable::InColumnResizeArea: the column edge `x`, `y` is within 2 of
	// (the cell there's left, but the table's, or its right), -1 if none
	float InColumnResizeArea(float x, float y);
	// TTable::FindPreviousColumnStart: the rightmost cell left before
	// `columnEnd`, -1 if none
	float FindPreviousColumnStart(float columnEnd) const;
	// TTable::RowTop, FindRow
	float RowTop(int row) const;
	int FindRow(float y) const;
	// TRow::FindColumn
	int FindColumn(int row, float x, bool atEnd) const;
	// TTable::FindPreviousCell: the cell before (row, column) in the rows'
	// order, NULL if none
	TableEditCell* FindPreviousCell(int row, int column);
	// TTable::RecalcSize, FixLocations, AdjustPositions, RecalcColumnEndTable
	void RecalcSize();
	void FixLocations();
	void AdjustPositions(int fromRow, float dy);
	void RecalcColumnEndTable();
	// TCell::MinimumHeight, TRow::MinimumTextHeight
	float MinimumHeight(const TableEditCell& cell) const;
	float MinimumTextHeight(int row) const;
	// TTable::CalcSpan(TRect, ...): the rows and columns the rectangle
	// (left, top, right, bottom) holds whole
	void CalcSpan(const float rect[4], int& rowFrom, int& rowTo, int& columnFrom,
		int& columnTo, bool recalc);
	// TTable::CalcSpan(TLocation, ...): a cell's row and column spans
	void CalcSpan(int row, int column, int& rowSpan, int& columnSpan, bool recalc);
	// TTable::CalcAllSpans
	void CalcAllSpans();
	// TTable::AdjustSpanningCells: from `x` to `right` along the row at
	// `y`, the cells reaching down past it grow by `dy` and `rows` rows
	void AdjustSpanningCells(float x, float y, float right, float dy, int rows);
	// TTable::ChangeRowHeight, ChangeColumnWidth, RecutAll
	void ChangeRowHeight(int row, float height, float height2, bool adjust);
	void ChangeColumnWidth(float columnEnd, float delta);
	void RecutAll(bool propagate, bool changed);
	// TTable::ScaleToSize (TTableFrame::SetSize, its frame resized): the
	// columns and rows scaled to make the table `width` by `height`, no
	// column narrower than 20 (what that adds moves the rest right), no cell
	// lower than 17 a row it spans
	void ScaleToSize(float width, float height);
	// TTable::RemoveEmptyRows
	void RemoveEmptyRows();
	// TCell::Grow (TTablePart::GrowChildFrame, its frame grown to its
	// text): the cell's last row as high as the cell's text needs, no less
	// than the height that row was given; false if nothing changed
	bool Grow(const TableEditCell& cell);

	// the selection's cells (TRow::AddToSelection), in the rows' order
	std::vector<const TableEditCell*> Selected(const TableSelection& selection) const;
	// TTablePart::CreateTableSelection(TLocation, TLocation)
	TableSelection SelectionOf(int row1, int column1, int row2, int column2) const;
	// TTable::SelectRows(long, long), SelectColumns
	TableSelection SelectRows(int from, int to) const;
	TableSelection SelectColumns(float left, float right) const;
	// TTable::CalcSpanningRect: the rectangle round the selected cells
	void SpanningRect(const TableSelection& selection, float rect[4]) const;
	// TTable::SelectionIsRectangular: no cell in that rectangle left out;
	// `count` the cells in it
	bool SelectionIsRectangular(const TableSelection& selection, int& count) const;
	// TTable::CalcSpan(TWPSelection*, ...)
	void SelectionSpan(const TableSelection& selection, int& rows, int& columns);
	// TTablePart::GetVirtualColumn: the cell's left and the first column
	// end after it
	void VirtualColumn(int row, int column, float& left, float& right);

	// TRow::InsertCell: a cell (row, column; -1 the end) of `width` by
	// `height` at `x`, `y`, its frame and text (a cell break, styled as the
	// text there) after cell `afterId`'s (-1: first), its frame styled as
	// cell `like`'s; its id
	int InsertCell(int row, int column, float x, float y, float width, float height,
		int afterId, int like);
	// the id before `id` in the frames' order, -1 if it's first
	int FrameBefore(int id) const;
	// the cell's text and frame gone (the cell itself stays)
	void DropText(int id);

	// TTable::InsertRow: a row before `row` (`row` the count: at the end)
	void InsertRow(int row);
	// TTable::InsertColumn: a column at `left` as wide as the cells ending
	// at `right` (the narrowest), or `right` - `left`
	void InsertColumn(float left, float right);
	// TTable::DeleteRow and its text (TDeleteRowCommand)
	void DeleteRow(int row);
	// TTable::DeleteColumn and its cells' text (TDeleteColumnCommand)
	void DeleteColumn(float left, float right);
	// TTable::MergeCells: the selected cells' text joined in the first's
	// (TWPPart::FilterCellBreaks), that cell the rectangle round them
	void MergeCells(const TableSelection& selection);
	// TTable::SplitCell: the cell as `columns` across by `rows` down;
	// returns the selection of what it was split into
	TableSelection SplitCell(int row, int column, int columns, int rows);
	// TDistributeRowsCommand, TDistributeColumnsCommand
	void DistributeRows(int from, int to);
	void DistributeColumns(float left, float right);
	// TTable::SetCellGutter, then RecutAll (TTableOptionsCommand)
	void SetCellGutter(float gutter);
};

// the table part `table` of `doc` as a TableEdit; false if it isn't one
bool LoadTableEdit(const Document& doc, size_t table, TableEdit& edit);
// `edit` put back: the table part (rows, size, gutter; its frames as large
// as it), the text part's paragraphs in frame order and the cells' frames
// (numbered in that order, inset by the gutter + 1 across and the gutter
// down, their text ranges)
bool StoreTableEdit(Document& doc, size_t table, const TableEdit& edit, std::string& error);

// the first paragraph of cell `id`'s text in the text part as stored
size_t TableEditParagraph(const TableEdit& edit, int id);

} // namespace gobe

#endif
