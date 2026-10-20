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

// What a cell's editor starts with: its formula or its value as typed.
std::string CellInput(const SSPart& part, uint16_t row, uint16_t column);

} // namespace gobe

#endif
