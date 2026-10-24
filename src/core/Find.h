// Document > Find/Change (TFindReplace, TTextAccessor, TDialogFindChange's
// conversions): the application's find and replace strings and options,
// searching the shown text parts a paragraph (a block) at a time.
#ifndef GOBE_FIND_H
#define GOBE_FIND_H

#include <cstdint>
#include <string>
#include <vector>

namespace gobe {

struct Document;
struct WPPart;

// ToUpperUTF8: ASCII and some two-byte letters upper case (as the
// original maps them)
void ToUpperUTF8(std::string& text);
// CharsMatchUTF8(pattern, text, caseSensitive, true): the characters
// match, the text's upper-cased unless `caseSensitive`
bool CharsMatchUTF8(const char* pattern, const char* text, bool caseSensitive);

// TFindReplace
struct FindReplace {
	std::string find, replace;
	bool caseSensitive = false, wholeWord = false, wrap = true;

	// SetFindString, SetCaseSensative (resetPattern)
	void SetFind(const std::string& text);
	void SetCaseSensitive(bool on);
	const std::string& Pattern() const { return fPattern; }

	// TFindReplace::find: in `text` [0, length), the first match (its start,
	// before 0 when it began in the last block: `continued`), else false
	// (`continued` and `matched` say a match is under way at the end).
	// `before` is the character before `text`.
	bool FindInBlock(const char* text, size_t length, char before, bool& continued,
		size_t& matched, long& at) const;

private:
	std::string fPattern;
};

// TTextAccessor over the shown text parts (a paragraph a block)
class TextAccessor {
public:
	TextAccessor(const Document& doc);

	// StartFromTop
	bool StartFromTop();
	// StartFromPosition(wrap, ...): from `position` in `part` (false if it
	// isn't a shown text part); with `wrap` the blocks go on from the first
	// part's after the last, ending with the start's block up to it
	bool StartFromPosition(size_t part, uint32_t position, bool wrap);
	// NextBlock: false at the end; `continues` if the block follows the
	// last in its part
	bool NextBlock(bool& continues);

	// the block: its part, paragraph, first byte and length
	size_t Part() const;
	size_t Paragraph() const { return fParagraph; }
	size_t Offset() const { return fOffset; }
	size_t Length() const { return fLength; }
	const char* Text() const;
	// the character before the block (0 before a part's first)
	char Before() const;
	// the block's start in its part (PartPosition)
	uint32_t Position() const;

private:
	const WPPart* CurrentPart() const;

	const Document* fDoc;
	std::vector<size_t> fParts;
	bool fStarted = false;		// +0x28
	bool fFromPosition = false;	// +0x29
	bool fWrap = false;			// +0x1c
	bool fAtStartPart = false;	// +0x38
	bool fDone = false;			// +0x39
	bool fInPart = false;		// +0xc: a part's blocks under way
	size_t fPartIndex = 0;		// +0x10
	long fBlock = -1;			// +0x18
	size_t fStartPartIndex = 0;	// +0x2c
	long fStartBlock = 0;		// +0x30
	size_t fStartLength = 0;	// +0x34
	size_t fParagraph = 0, fOffset = 0, fLength = 0;
};

// a match: its part and its start and end in the part's text
struct FoundText {
	size_t part = 0;
	uint32_t start = 0, end = 0;
};

// TFindReplace::DoFind: from `position` in text part `part` (the
// selection's end), or the top if `fromPosition` is false or the part
// isn't text; false if there's no match
bool FindNext(const Document& doc, const FindReplace& find, bool fromPosition, size_t part,
	uint32_t position, FoundText& found);
// TFindReplace::DoReplaceAll: every match from the top replaced (each
// search going on after the last replacement); the number replaced, the
// last one's text in `last`
int ReplaceEvery(Document& doc, const FindReplace& find, FoundText& last);
// TWPTextIterator::ReplaceText: [start, end) of a text part replaced by
// `text`; where it went
bool ReplacePartText(Document& doc, size_t part, uint32_t start, uint32_t end,
	const std::string& text, FoundText& replaced);

// TDialogFindChange::convertToDisplay, convertFromDisplay: the special
// characters as \t, \l (line break), \n (paragraph), \f (frame break), \p
// (page), \m (multi-use, found only), and "/" as \/
std::string FindToDisplay(const std::string& text);
std::string FindFromDisplay(const std::string& text, bool replaceField);
// TDialogFindChange::viewContainsSpecials
bool FindHasSpecials(const std::string& text);

} // namespace gobe

#endif
