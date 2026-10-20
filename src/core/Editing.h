// Editing operations on the parsed parts, kept consistent with what the
// writers save: text in word processing parts (style runs, paragraph infos
// and section starts follow the text), and spreadsheet cell input (numbers,
// text, booleans, dates and formulas parsed into Gobe's token format).
#ifndef GOBE_EDITING_H
#define GOBE_EDITING_H

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "DocSections.h"
#include "Formula.h"
#include "SSPart.h"
#include "WPPart.h"

namespace gobe {

// A position in a word processing part: paragraph and byte offset in its
// text (UTF-8; always at a character boundary).
struct TextPosition {
	size_t paragraph = 0;
	size_t offset = 0;
};

// the paragraph's text without its terminator ('\n' or a break character)
size_t EditableLength(const WPParagraph& p);
size_t NextCharacter(const std::string& text, size_t offset);
size_t PreviousCharacter(const std::string& text, size_t offset);

// Inserts UTF-8 text (no line breaks); returns the position after it.
TextPosition InsertText(WPPart& part, TextPosition at, const std::string& text);
// Deletes [from, to) within one paragraph; false if that would remove a
// paragraph info's anchor (an embedded frame, a formula field, ...).
bool DeleteText(WPPart& part, TextPosition from, size_t to);
// Enter: splits the paragraph; returns the start of the new one.
TextPosition SplitParagraph(WPPart& part, TextPosition at);
// Backspace at the start of a paragraph: joins it to the previous one;
// returns where the two meet.
bool JoinWithPrevious(WPPart& part, size_t paragraph, TextPosition& joined);

// The plain text of a range ('\n' between paragraphs).
std::string RangeText(const WPPart& part, TextPosition from, TextPosition to);
// Deletes a range that may span paragraphs; false, with nothing changed, if
// it would remove an anchor (frame, formula field, ...). Breaks (page,
// column, section) are deleted like other characters.
// `result` is where the range started.
bool DeleteRange(WPPart& part, TextPosition from, TextPosition to, TextPosition& result);
// InsertText for text with line breaks; returns the position after it.
TextPosition InsertMultiline(WPPart& part, TextPosition at, const std::string& text);

// Double click: [from, to) is the word, blank run or punctuation character
// at `offset` (the one before it at the end of the text).
void WordRange(const std::string& text, size_t offset, size_t& from, size_t& to);

// TParser: parses a formula ("=..." without the "="), relative to the
// formula's cell; `isName` tells known names (named ranges) apart. Returns
// false with an error message on a syntax error.
bool ParseFormula(const std::string& text, CellID at,
	const std::function<bool(const std::string&)>& isName,
	std::vector<FormulaToken>& tokens, std::string& error);

// Sets what the user typed into a cell: "" clears it (keeping its style),
// "=..." is a formula, numbers, TRUE/FALSE, m/d/yyyy dates, anything else
// text. Returns false (and an error) for a formula that doesn't parse.
bool SetCellInput(SSPart& part, uint16_t row, uint16_t column, const std::string& input,
	const std::function<bool(const std::string&)>& isName, std::string& error);

// Brings a spreadsheet part's dependency tables up to date after its
// formulas changed (TSSDepManager): `dependents` holds every formula cell,
// precedents first (the original order kept where it is valid); one
// TLinkDepInfo per link to this part lists the cells whose formulas use
// the link's name, with their count. Links for names this part didn't use
// before are added to the document's LinksMgr. Returns true if the links
// changed.
struct Document;
bool UpdateDependencies(Document& doc, size_t part);

// Text styles. A run's or paragraph's style is an index into the
// StyleMgr; unnamed run styles are children of unnamed paragraph styles,
// which are children of named ones ("Body"). A style's value4 counts its
// users (runs, paragraphs, child styles) plus one for the manager, and is
// kept up to date here.

// The style like `base` but with `property` set (replacing one of the
// same type): an existing identical unnamed style, or a new one (added
// with no users yet).
uint32_t StyleWithProperty(StyleMgr& styles, uint32_t base, const Property& property);
// Character properties (TFon, TSiz, TFac, TCol, ...) for the text in
// [from, to), which may span paragraphs; runs are split at the ends and
// merged again where neighbours end up alike. False if nothing changed.
bool ApplyCharacterProperty(Document& doc, size_t part, TextPosition from, TextPosition to,
	const Property& property);
// Paragraph properties (PAlg, PLef, PInd, PRig, SpaB, SpaA, LSpa, PTab)
// for paragraphs first..last.
bool ApplyParagraphProperty(Document& doc, size_t part, size_t first, size_t last,
	const Property& property);

// Inserts `count` empty rows (or columns) before row (column) `at` of a
// spreadsheet part, or deletes `count` of them from `at` if count < 0.
// Formulas follow: references to moved cells move (both in this part's
// formulas and in its named ranges); a range grows when rows are inserted
// inside it and shrinks when some of its rows are deleted; a reference to a
// deleted cell, or a range deleted entirely, becomes invalid (#REF!).
// Rows/column sizes, fills, the used range and the selection move along.
// Dependencies are brought up to date. False if nothing changed.
bool InsertRowsColumns(Document& doc, size_t part, bool columns, uint16_t at, int count);

// What a cell's editor starts with: its formula or its value as typed.
std::string CellInput(const SSPart& part, uint16_t row, uint16_t column);

// A block of cells as tab-separated text, one line per row: each cell as
// CellInput gives it (formulas as typed, "=..."). Rows [row0, row1],
// columns [column0, column1], inclusive; no trailing newline.
std::string RangeInput(const SSPart& part, uint16_t row0, uint16_t column0,
	uint16_t row1, uint16_t column1);
// Clears the cells of a block (SetCellInput with ""); true if any changed.
bool ClearRange(SSPart& part, uint16_t row0, uint16_t column0,
	uint16_t row1, uint16_t column1);
// Pastes tab-separated text (lines separated by \n, \r\n or \r; one
// trailing line break ignored) with its top left cell at (row, column).
// Formulas are pasted as typed (no reference adjustment). Stops at the
// first cell whose input doesn't parse: returns false with `error`
// naming that cell's text; cells before it stay pasted. `rows` and
// `columns` receive the pasted block's size.
bool PasteInput(SSPart& part, uint16_t row, uint16_t column, const std::string& text,
	const std::function<bool(const std::string&)>& isName, std::string& error,
	uint16_t& rows, uint16_t& columns);

} // namespace gobe

#endif
