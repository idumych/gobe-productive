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
// several ranges (a selection of more than one), each as long as the first
// key, their lines put in the same order
bool SortCells(Document& doc, size_t part, const std::vector<SSRange>& ranges,
	bool vertical, const std::vector<SortKey>& keys);

// TCellsList::Name / Set: the ranges' names joined by ","; false if one
// isn't a range (one cell alone is taken as a range of it)
std::string RangeListName(const std::vector<SSRange>& ranges);
bool ParseRangeList(const std::string& text, std::vector<SSRange>& ranges);
// TRange::Name: "A2..B5" ("A2" for one cell)
std::string RangeName(const SSRange& range);
// TRange::Set: "A2..B5" (both cells, no '$'; letters in either case,
// spaces before each cell); false otherwise
bool ParseRangeName(const std::string& text, SSRange& range);

} // namespace gobe

#endif
