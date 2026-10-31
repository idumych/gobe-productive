// Copying and pasting cells as cells (TSSPasteCommand with the original's
// private scrap, a TPartSS of the copied cells): formulas keep their
// relative references, so they refer to the cells around where they land;
// formats come along within a document; Paste Special pastes the values
// only, or the formats only.
#ifndef GOBE_SS_PASTE_H
#define GOBE_SS_PASTE_H

#include <string>
#include <vector>

#include "SSEval.h"
#include "SSPart.h"

namespace gobe {

struct Document;

struct CopiedCells {
	uint16_t rows = 0, columns = 0;	// the block's size (0: nothing copied)
	std::vector<SSCell> cells;		// ids from the block's top left cell
	std::vector<SSValue> values;	// each cell's value when it was copied
	std::string text;				// what went to the clipboard as text
	const Document* source = nullptr;	// formats paste only into it
};

// the cells of `range` of part `part` (values from `book`, or computed)
CopiedCells CopyCells(const Document& doc, size_t part, const SSRange& range,
	SSBook* book = nullptr);

// TSSPasteCommand::ValidPaste: whether the block fits in the sheet at `at`
bool PasteFits(const SSPart& part, CellID at, const CopiedCells& copied);

// TSSPasteCommand::PerformCmd: the block put at `at`: with `data`, each
// cell's contents (formulas as formulas, or with `formulas` false their
// values; a cell the block has empty is emptied); with `formatting`, each
// cell's style (only within the document copied from). False if it
// doesn't fit (PasteFits) or nothing was copied.
bool PasteCells(Document& doc, size_t part, CellID at, const CopiedCells& copied, bool data,
	bool formulas, bool formatting);

} // namespace gobe

#endif
