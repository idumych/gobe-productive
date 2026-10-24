// The word processing part's data (TWPPart::TWPPart(TInputArchive&)),
// parsed from its part record (see PartRecord in DocSections.h).
#ifndef GOBE_WP_PART_H
#define GOBE_WP_PART_H

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "DocArchive.h"
#include "Formula.h"

namespace gobe {

// TPart::TPart(TInputArchive&): the header every part starts with
struct PartHeader {
	std::vector<uint8_t> message;	// +0x58, flattened BMessage
	uint32_t values[4] = {};		// +0x38? +0x3c, +0x40, +0x34
	uint32_t selection = 0;			// TSelPtr index
};
void ReadPartHeader(DocArchive& a, PartHeader& out);

// TParagraph misc infos: frames, footnotes, formulas, line/page breaks
struct WPMiscInfo {
	uint32_t kind;				// 'FRAM', 'FMRK', 'FORM', 'LINE', 'FREF', 'PAGE'
	uint32_t position;			// TMiscInfo: offset in the paragraph
	uint32_t frame[3] = {};		// FRAM, FREF: ReadFramePtr
	std::string text;			// FREF text, FORM formula as typed
	std::vector<uint8_t> raw;	// the payload after kind and position (for writing)
	// FORM (TMiscFormulaInfo): the expression (formula, last result,
	// number format), the command that made it ('sM53' date, 'sM54' time,
	// 'sM55' formula) and whether it keeps its value, shows its result,
	// shows its formula
	Expression expression;
	uint32_t command = 0;
	bool isStatic = false, displayResult = true, displayFormula = false;
	// FREF (TMiscFootnoteRef): the note's frame is `frame`; an endnote, an
	// automatic number (else `text` is its mark) and that number
	bool endnote = false, autoNumber = true;
	uint32_t number = 0;
};

struct WPStyleRun {
	uint32_t position;
	uint32_t style;				// TStyleID: style index (0xffffffff = none)
};

// TParagraph::TParagraph(TInputArchive&, TWPPart*) + ReadText
struct WPParagraph {
	uint32_t start = 0;
	uint32_t length = 0;		// bytes of text
	uint16_t value12 = 0;
	uint32_t parStyle = 0;		// TParStyleID
	std::vector<WPStyleRun> runs;
	std::vector<WPMiscInfo> misc;
	std::string text;			// UTF-8
};

// TSection::TSection(TInputArchive&)
struct WPSection {
	uint32_t start = 0;
	uint16_t values[2] = {};
	bool flag = false;
	std::vector<std::array<float, 4>> columns;	// TColumnData rects
};

struct WPPart {
	PartHeader header;
	uint16_t version98 = 0;
	uint16_t version9a = 1;
	uint32_t value88 = 0;
	uint32_t style = 0;			// +0x90 TStyleID
	uint32_t value8c = 0;
	uint32_t value94 = 0;
	std::vector<WPParagraph> paragraphs;
	std::vector<WPSection> sections;
	// where 'par+' starts and 'wp--' ends in the part record: what's around
	// them is copied when writing
	size_t bodyStart = 0, bodyEnd = 0;
	// text edits since reading, in order: (text offset, characters inserted
	// (> 0) or deleted (< 0)); used to keep the frames' text ranges right
	std::vector<std::pair<uint32_t, int64_t>> edits;
};

// a text offset from before the edits to after them; a boundary exactly at
// an insertion stays before the inserted text unless `atEnd` (frame ends
// take text typed at them)
uint32_t MapTextOffset(const WPPart& part, uint32_t offset, bool atEnd);

// `a` is positioned at the part record ('PART', 'WORD', ...); reads up to
// and including 'wp--'.
void ReadWPPart(DocArchive& a, WPPart& out);

// The part record with the paragraphs, text and sections written from `part`
// (paragraph starts recomputed from the text lengths); `record` is the part
// record it was read from.
std::vector<uint8_t> WriteWPPart(const WPPart& part, const std::vector<uint8_t>& record);

} // namespace gobe

#endif
