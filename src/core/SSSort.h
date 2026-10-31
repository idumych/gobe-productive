// Sort Cells (TSSSortCommand, TSSSortTable, decomp/Spreadsheet): the rows
// (vertical) or columns of a block reordered by the values of up to three
// keys, each a column (row) as long as the block.
#ifndef GOBE_SS_SORT_H
#define GOBE_SS_SORT_H

#include <string>
#include <vector>

#include "SSPart.h"

namespace gobe {

struct Document;

// TSSSortKey: the cells whose values decide the order (row i of the key
// for row i of the block), ascending or descending
struct SortKey {
	SSRange range;
	bool ascending = true;
};

// TSSSortCommand::Perform: the rows (columns) of `range` of part `part`
// put in the order QuickSort gives with TSSSortTable::Compare, cells with
// their formats moving and their borders staying (SwapCells). False,
// nothing changed, without keys or if the range isn't as long as the
// first key.
bool SortCells(Document& doc, size_t part, const SSRange& range, bool vertical,
	const std::vector<SortKey>& keys);

// TRange::Name: "A2..B5" ("A2" for one cell)
std::string RangeName(const SSRange& range);
// TRange::Set: "A2..B5" (both cells, no '$'; letters in either case,
// spaces before each cell); false otherwise
bool ParseRangeName(const std::string& text, SSRange& range);

} // namespace gobe

#endif
