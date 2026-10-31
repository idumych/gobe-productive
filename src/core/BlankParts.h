// Part records of new documents and sheets (BlankDocument.cpp), shared by
// File > New and New Sheet.
#ifndef GOBE_BLANK_PARTS_H
#define GOBE_BLANK_PARTS_H

#include <cstdint>
#include <vector>

#include "DocSections.h"

namespace gobe {

// the styles a spreadsheet part uses (indices into the StyleMgr)
struct SheetStyles {
	uint32_t ssDefault, grid, header, column, row, lock;
};

// TPartSSFactory::AddPartStyles and TSSDisplayManager's styles: those the
// manager doesn't have yet ("SS Default" based on `base`, "Default"),
// counted for one more spreadsheet part using them
SheetStyles AddSheetStyles(StyleMgr& styles, uint32_t base);

// a new spreadsheet part's record (TPartSS::TPartSS(TDoc*)): part id `id`,
// its selection, the styles; with a frame in the blank spreadsheet's browse
// root (part 2) when `framed`, else none (a sheet not shown)
std::vector<uint8_t> SheetPartRecord(uint32_t id, uint32_t selection, const SheetStyles& styles,
	bool framed, uint32_t frameStyle);
// the styles a word processing part uses: its paragraph's and its run's
struct WordStyles {
	uint32_t paragraph, run;
};
// TWPPartFactory::InitStyles ("WP Default", "Body", found or added) and
// TWPPart::TWPPart's own paragraph and run styles
WordStyles AddWordStyles(StyleMgr& styles, uint32_t base);
// a new word processing part's record (TWPPart::TWPPart(TDoc*): one empty
// paragraph, one section); with a frame on the blank document's page when
// `framed`, else none
std::vector<uint8_t> WordPartRecord(uint32_t id, uint32_t selection, uint32_t paragraphStyle,
	uint32_t runStyle, bool framed, uint32_t frameStyle);

// the sheet's drawing (TPartGR: "Layer 1", "View 1"), the same way
std::vector<uint8_t> SheetGraphicsRecord(uint32_t id, uint32_t selection, bool framed,
	uint32_t frameStyle);

} // namespace gobe

#endif
