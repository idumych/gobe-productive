// Moving cells (TSSMoveCommand in the original), the primitive Move Cells,
// Insert Cells and Delete Cells are built on: a block of cells goes to a new
// place with formulas following it. A reference into the block moves with
// it; a reference from a moved cell to a cell left behind keeps pointing
// there; a reference to a cell the block lands on becomes invalid (#REF!).
// Named ranges of the part follow the same rules.
#ifndef GOBE_SS_MOVE_H
#define GOBE_SS_MOVE_H

#include <vector>

#include "Formula.h"
#include "SSPart.h"

namespace gobe {

struct Document;

// TCellRef::ActualCell: the cell a reference in the formula of cell `at`
// points to (relative parts are offsets from `at`)
CellID ActualCell(const CellRef& ref, CellID at);

// TCellsList (a list of ranges, which may overlap)
// CellInList / RangeInList / ListInList: a cell or any cell of a range in it
bool CellInList(const std::vector<SSRange>& list, CellID cell);
bool RangeInList(const std::vector<SSRange>& list, const SSRange& range);
bool ListInList(const std::vector<SSRange>& list, const std::vector<SSRange>& other);
// ContainsRange: every cell of the range in it
bool ListContainsRange(const std::vector<SSRange>& list, const SSRange& range);
// RemoveRange: the ranges the range touches are taken out and the parts of
// them it doesn't cover added back (above, left, below, right)
void RemoveRange(std::vector<SSRange>& list, const SSRange& range);
// RemoveList: RemoveRange for each range of `removed`
void RemoveList(std::vector<SSRange>& list, const std::vector<SSRange>& removed);

// TRangeRef::Adjust: a range reference (refs[0], refs[1]) in the formula of
// cell `at` after `block` moved by rows, columns. A range inside the block
// moves with it; one whose first or last row (column) lies in the block
// when it moves up or down (left or right) stretches or shrinks. True if
// the reference changed.
bool AdjustRangeRef(CellRef& from, CellRef& to, const std::vector<SSRange>& block, int rows,
	int columns, CellID at);
// TFormula::MoveReferences: the formula of cell `at` after `block` moved by
// rows, columns (`at` is where the cell was). True if it changed.
bool MoveFormulaReferences(std::vector<FormulaToken>& formula,
	const std::vector<SSRange>& block, int rows, int columns, CellID at);
// TFormula::InvalidateReferences: references to `cells` (a range when
// either corner is in them) made invalid. True if any was.
bool InvalidateFormulaReferences(std::vector<FormulaToken>& formula,
	const std::vector<SSRange>& cells, CellID at);

// TSSMoveCommand::MoveCells: the cells of `block` (pinned to the sheet,
// part.used) moved by rows, columns, and the formulas of the part following
// them; the cells the block lands on outside itself are cleared first.
// False, with nothing changed, if the block would go above the first row or
// left of the first column. `changed` (if given) gets the formula cells
// whose references changed, then the block and where it lands.
bool MoveCells(SSPart& part, const std::vector<SSRange>& block, int rows, int columns,
	std::vector<SSRange>* changed = nullptr);

// TSSMoveCommand::PerformCmd: Move Cells of part `part` of the document:
// refused (locked) if a cell where the block lands is locked; otherwise the
// cells move (MoveCells; failed as there), then the part's named ranges
// follow (TSSMoveCommand::MoveReferences) and dependencies are brought up
// to date.
enum class MoveResult { moved, locked, failed };
MoveResult MoveCellsCommand(Document& doc, size_t part, const std::vector<SSRange>& block,
	int rows, int columns);

// TRange::Adjust: range `r` (a locked range) after `block` moved by rows,
// columns: inside the block it moves with it; one that the move doesn't
// leave alone along the other axis stretches by the move when the block's
// first row (column) lies in its rows (columns). True if it changed.
bool AdjustRange(SSRange& r, const SSRange& block, int rows, int columns);

// TSSInsertCommand and TSSDeleteCommand::PerformCmd, `range` of part
// `part`, a selection of cells, whole rows or whole columns (TSSSelection
// 'CELL', 'ROWS', 'COLS'). For cells, Insert Cells shifts the cells from
// the range to the sheet's last row (column) down (right) by the range's
// height (width), refused (false, nothing changed) if cells that aren't
// empty would go past it; Delete Cells shifts the cells below (right of)
// the range up (left) over it. Rows go down (up) and columns right (left);
// the sheet grows (TSSSetDocSizeCommand: refused past row 0x3ffe or column
// 0x2bd) or shrinks by them, and their sizes move with them (Offset/
// KillRowHeaders). The locks in the way are offset (TSSMoveCommand::
// OffsetLocks), formulas and named ranges follow (MoveCells,
// TSSMoveCommand::MoveReferences); references to deleted cells become
// #REF!. `vertical` is the dialog's "shiftVertical" (cells only).
enum class SelectionKind { cells, rows, columns };
bool InsertCells(Document& doc, size_t part, const SSRange& range, bool vertical,
	SelectionKind kind = SelectionKind::cells);
bool DeleteCells(Document& doc, size_t part, const SSRange& range, bool vertical,
	SelectionKind kind = SelectionKind::cells);

} // namespace gobe

#endif
