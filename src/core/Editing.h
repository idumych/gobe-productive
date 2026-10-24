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
// Deletes [from, to) within one paragraph (fields in it with it); false if
// that would remove a paragraph info's anchor (an embedded frame, a
// footnote).
bool DeleteText(WPPart& part, TextPosition from, size_t to);
// Enter: splits the paragraph; returns the start of the new one.
TextPosition SplitParagraph(WPPart& part, TextPosition at);
// Backspace at the start of a paragraph: joins it to the previous one;
// returns where the two meet.
bool JoinWithPrevious(WPPart& part, size_t paragraph, TextPosition& joined);

// The plain text of a range ('\n' between paragraphs).
std::string RangeText(const WPPart& part, TextPosition from, TextPosition to);
// Deletes a range that may span paragraphs; false, with nothing changed, if
// it would remove an anchor (frame, footnote). Breaks (page,
// column, section) are deleted like other characters.
// `result` is where the range started.
bool DeleteRange(WPPart& part, TextPosition from, TextPosition to, TextPosition& result);
// InsertText for text with line breaks; returns the position after it.
TextPosition InsertMultiline(WPPart& part, TextPosition at, const std::string& text);
// TWPPart::InsertSpecialChar: 0x01 (line break, sM57) goes into the text;
// 0x02 (column break, sM58) and 0x0c (page break, sM59) end the paragraph
// like Enter, with that character as its terminator. Returns the position
// after it.
TextPosition InsertSpecialChar(WPPart& part, TextPosition at, char c);
// the link from reference `reference` of part `sourcePart` to part
// `dependentPart` (TDoc::FindLink, AddLink): the existing one's index, or
// a new one added to the document's LinksMgr
struct Document;
uint32_t LinkFor(Document& doc, size_t sourcePart, size_t reference, size_t dependentPart);
// a paragraph info that is a field (formula, page number, separator line):
// it goes when its character is deleted
bool IsField(const WPMiscInfo& info);
// TWPPart::InsertMiscCharPrim: a field's character (0x04) at `at`, with its
// paragraph info; a separator line (TMiscLineInfo) starts a paragraph, so
// the paragraph is split first unless `at` is its start. Returns the
// position after it.
TextPosition InsertField(WPPart& part, TextPosition at, WPMiscInfo field);

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

// TStyle::UpdateUnionSet: the properties a style inherits (its parents'
// inherited and own ones, later replacing earlier), sorted as TPropsTable
// keeps them (by type, tabs by position). Styles store it (props2) and the
// original uses it as stored.
std::vector<uint32_t> StyleUnionSet(const StyleMgr& styles, const Style& style);
// A piece of styled text (copied, or a file inserted): its text, '\n'
// between paragraphs, and each run's character properties (CharacterFormat).
// Field and note characters (0x04) are left out (their paragraph infos
// aren't copied).
struct TextFragment {
	struct Run {
		size_t paragraph, from, to;		// in the fragment
		std::vector<Property> properties;
	};
	std::string text;
	std::vector<Run> runs;
	// each paragraph's paragraph properties (given to the pasted paragraphs
	// that start in the fragment: all but the first)
	std::vector<std::vector<Property>> paragraphs;
};
TextFragment CopyFragment(const Document& doc, size_t part, TextPosition from, TextPosition to);
// Inserts a fragment at `at`, each run getting its properties; returns the
// position after it.
TextPosition PasteFragment(Document& doc, size_t part, TextPosition at,
	const TextFragment& fragment);
// Edit > Copy Format in text (TPart::CopyFormat, StripParagraphProperties):
// a style's properties, its own and inherited, but the paragraph ones
std::vector<Property> CharacterFormat(const StyleMgr& styles, uint32_t style);
// the paragraph properties of a style, its own and inherited
std::vector<Property> ParagraphFormat(const StyleMgr& styles, uint32_t style);
// TPropsTable's order: by type, tabs by position.
void SortProps(const StyleMgr& styles, std::vector<uint32_t>& props);
// Whether a property list is in TPropsTable's order.
bool PropsSorted(const StyleMgr& styles, const std::vector<uint32_t>& props);

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
// Removes the paragraphs' own tab stop at `position` (the PTab property of
// their unnamed style); tabs of named styles stay. False if nothing changed.
bool RemoveParagraphTab(Document& doc, size_t part, size_t first, size_t last,
	float position);

// Inserts `count` empty rows (or columns) before row (column) `at` of a
// spreadsheet part, or deletes `count` of them from `at` if count < 0.
// Formulas follow: references to moved cells move (both in this part's
// formulas and in its named ranges); a range grows when rows are inserted
// inside it and shrinks when some of its rows are deleted; a reference to a
// deleted cell, or a range deleted entirely, becomes invalid (#REF!).
// Rows/column sizes, fills, the used range and the selection move along.
// Dependencies are brought up to date. False if nothing changed.
bool InsertRowsColumns(Document& doc, size_t part, bool columns, uint16_t at, int count);

// Sets the width of column `first` through `last` to `size` (points), or the
// height of row `first` through `last` to `size` (points) when `columns` is
// false.  Returns true if the header was created, modified or deleted (size
// == 0).  Sizes of 0 are stored as 0 (the view treats them as "use default").
bool SetLineSize(SSPart& part, bool columns, uint16_t first, uint16_t last, float size);

// Cell formats: sets `property` (a number format 'SSNm', fill 'FCol',
// alignment 'HAln'/'VAln', wrap 'ClWp', text properties, ...) in the style
// of every cell of the block, adding empty cells where there are none.
// Border properties, which a style holds once per side, aren't handled.
// False if nothing changed.
bool ApplyCellProperty(Document& doc, size_t part, uint16_t row0, uint16_t column0,
	uint16_t row1, uint16_t column1, const Property& property);

// What a cell's editor starts with: its formula or its value as typed.
std::string CellInput(const SSPart& part, uint16_t row, uint16_t column);

// Find options and results. `text` is the search string (empty = use last
// search). `caseSensitive` toggles ASCII case sensitivity; `entireWord`
// requires the match to be surrounded by non-word characters (or paragraph
// boundaries). `wrap` means search continues from the start when the end is
// reached. `paragraph` and `offset` are set to the position of the found
// match (or the paragraph after the last search if not found).
struct FindOptions {
	std::string text;
	bool caseSensitive = false;
	bool entireWord = false;
	bool wrap = false;
	size_t paragraph = 0;
	size_t offset = 0;
};

// Find the next occurrence of `options.text` in the part starting at
// `options.paragraph`/`options.offset`. If found, sets `options.paragraph`
// and `options.offset` to the match position and returns true. If not found
// and `wrap` is true, restarts from the beginning. Returns false if the
// text is empty or there really is nothing to find.
// Matches never span paragraphs.
bool FindText(WPPart& part, FindOptions& options);

// Replace all non-overlapping occurrences of `options.text` with `replacement`
// in the part starting from the first paragraph. Returns the number of
// replacements made. `options` fields (caseSensitive, entireWord) are used.
// Uses DeleteRange + InsertText to handle styles and anchors correctly.
uint32_t ReplaceAll(WPPart& part, const std::string& replacement,
	const FindOptions& options);

// Alternative find-and-replace API for the Find/Replace dialog and scripts.
// `FindText` searches from `from` (before it when backwards); sets `start`
// and `end` to the match byte offsets within the paragraph. Returns false
// when there is no match. Case-insensitive uses ASCII case folding.
struct FindDialogOptions {
	bool caseSensitive = false;
	bool wholeWord = false;		// use WordRange's word characters
	bool backwards = false;
	bool wrap = true;
};
bool FindText(const WPPart& part, TextPosition from, const std::string& query,
	const FindDialogOptions& options, TextPosition& start, TextPosition& end);
// Replaces every match with `replacement` (through DeleteRange and
// InsertText, so styles and anchors are handled as there; a match that
// DeleteRange refuses is skipped). Returns the number replaced.
int ReplaceAll(WPPart& part, const std::string& query, const std::string& replacement,
	const FindDialogOptions& options);

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

// Fill Down / Fill Right: copies the first row (column) of the block
// into the rest of it — value, formula and style. Relative references
// are stored as offsets from their cell, so copied formulas follow.
// False if nothing changed.
bool FillRange(SSPart& part, uint16_t row0, uint16_t column0, uint16_t row1,
	uint16_t column1, bool right);

} // namespace gobe

#endif
