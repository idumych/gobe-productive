// The spelling engine Gobe Productive links in (Wintertree's SSCE: SSCE_*,
// CLex_*, TLex_*, Block*, WordComparator_*, Lang_Phonetic*), for the
// original's own lexicons: compressed ones (magic 0x57000501: segments of
// front-coded words with suffix tables) and text ones ("#LID n", then a
// word per line, or a word, a tab, an action and a replacement). Words are
// Latin-1 strings of up to 63 characters.
#ifndef GOBE_SSCE_H
#define GOBE_SSCE_H

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace gobe {
namespace ssce {

// SSCE_SetOption's options (the session starts with 0x18680)
enum {
	kIgnoreCappedWord = 0x1,
	kIgnoreMixedCase = 0x2,
	kIgnoreMixedDigits = 0x4,
	kIgnoreAllCaps = 0x8,
	kReportUncapped = 0x10,
	kReportMixedCase = 0x20,
	kReportMixedDigits = 0x40,
	kReportSpelling = 0x80,
	kReportDoubledWord = 0x100,
	kCaseSensitive = 0x200,
	kSplitHyphenated = 0x400,
	kSplitContracted = 0x800,
	kSplitWords = 0x1000,
	kSuggestSplitWords = 0x2000,
	kSuggestPhonetic = 0x4000,
	kSuggestTypographical = 0x8000,
	kStripPossessives = 0x10000
};

// SSCE_CheckWord's results (bits; 0 is a good word)
enum {
	kMisspelled = 0x1,
	kAutoChange = 0x2,
	kConditionalChange = 0x4,
	kUncapped = 0x8,
	kMixedCase = 0x10,
	kMixedDigits = 0x20,
	kEndOfBlock = 0x40,
	kDoubledWord = 0x80
};

// Latin1_CharClass bits
enum {
	kAlpha = 0x1, kDigit = 0x2, kPunct = 0x4, kSpace = 0x8, kLower = 0x10, kUpper = 0x20,
	kVowel = 0x40
};
uint8_t CharClass(uint8_t c);
uint8_t ToBase(uint8_t c);			// Latin1_ToBase: the plain lower case letter
uint8_t PhoneticClass(uint8_t c);	// Lang_PhoneticCharEquiv's table

// WordComparator_Typographical and WordComparator_EnglishPhonetic: how
// alike two words are, 0 to 100
int TypographicalScore(const std::string& word, const std::string& other);
int PhoneticScore(const std::string& word, const std::string& other);
// Lang_PhoneticSig
std::string PhoneticSignature(const std::string& word);

// SuggestionSet: the best `capacity` words by score, highest first
struct SuggestionSet {
	explicit SuggestionSet(size_t capacity) : capacity(capacity) {}
	void Add(const std::string& word, int score);
	size_t capacity;
	std::vector<std::pair<std::string, int>> entries;
};

class Lexicon {
public:
	virtual ~Lexicon() {}
	// CLex_Contains, TLex_Contains: the word's action ('i' a good word, 'e'
	// excluded, 'A' change, 'C' conditionally change; with `replacement`),
	// or -3 when it isn't there
	virtual int Contains(const std::string& word, bool caseSensitive,
		std::string& replacement) const = 0;
	virtual void Suggest(const std::string& word, int depth,
		int (*compare)(const std::string&, const std::string&), SuggestionSet& set) const = 0;
	uint16_t language = 0x616d;		// 'am'
};

// CLex: the compressed lexicon, read whole
class CompressedLexicon : public Lexicon {
public:
	// CLex_Open: false (and `error`) if it isn't one
	bool Open(const std::vector<uint8_t>& data, std::string& error);
	virtual int Contains(const std::string& word, bool caseSensitive,
		std::string& replacement) const;
	virtual void Suggest(const std::string& word, int depth,
		int (*compare)(const std::string&, const std::string&), SuggestionSet& set) const;

	struct Segment {
		uint8_t prefix[3];
		uint32_t offset, size;
	};
	size_t SegmentCount() const { return fSegments.size(); }
	// the segment's words (FUN_002cc3a0's decoding)
	std::vector<std::string> SegmentWords(size_t index) const;

private:
	const Segment* Find(const uint8_t prefix[3]) const;
	bool ScanSegment(const Segment& segment, const std::string& word, int mode) const;

	std::vector<uint8_t> fData;
	uint32_t fDataStart = 0;
	uint16_t fCharSet = 1;
	std::vector<std::string> fSimpleSuffixes, fExtendedSuffixes;
	std::vector<Segment> fSegments;		// sorted by prefix
};

// TLex: the text lexicon (kept sorted; a user dictionary is a plain list)
class TextLexicon : public Lexicon {
public:
	bool Open(const std::string& path, std::string& error);
	// the same, from text (no file to write back)
	void Read(const std::string& text);
	virtual int Contains(const std::string& word, bool caseSensitive,
		std::string& replacement) const;
	virtual void Suggest(const std::string& word, int depth,
		int (*compare)(const std::string&, const std::string&), SuggestionSet& set) const;
	// TLex_Add, TLex_Delete: the word (an action and replacement), written
	// back to the file; false if writing failed
	bool Add(const std::string& word, char action = 'i', const std::string& replacement = "");
	bool Delete(const std::string& word);
	// the file's text as FUN_002cfd1c writes it
	std::string Text() const;

	struct Entry {
		std::string word;
		char action;
		std::string replacement;
	};
	const std::vector<Entry>& Entries() const { return fEntries; }

private:
	void Insert(const std::string& word, char action, const std::string& replacement);
	bool Write() const;

	std::vector<Entry> fEntries;
	std::string fPath;
	bool fPlain = false;		// no "#LID" line
};

// the words of a text (BlockOpen, BlockGetWord, BlockNextWord, ...)
class Block {
public:
	Block(const std::string& text, const uint32_t* options);
	// the word at the cursor, or false at the end
	bool GetWord(std::string& word) const;
	bool NextWord();
	bool IsFirstWord() const { return fFirst; }
	bool IsDoubledWord(bool caseSensitive) const;
	uint32_t Cursor() const { return fCursor; }
	void SetCursor(uint32_t cursor);
	// BlockReplaceWord: the word at the cursor replaced
	bool ReplaceWord(const std::string& word);
	const std::string& Text() const { return fText; }

private:
	uint8_t At(uint32_t i) const { return i < fText.size() ? uint8_t(fText[i]) : 0; }
	// BlockGetWord from `cursor`; the character after the word
	bool WordAt(uint32_t cursor, std::string& word, uint8_t* after = nullptr) const;

	std::string fText;
	const uint32_t* fOptions;
	uint32_t fCursor = 0;
	uint32_t fPrevious;		// the last word's start (+0x14)
	bool fFirst = true;
};

class Session {
public:
	// SSCE_OpenLex: the lexicon file (compressed or text); its index, or
	// a negative error
	int OpenLex(const std::string& path, std::string& error);
	int AddLex(std::unique_ptr<Lexicon> lexicon);
	Lexicon* Lex(int index) const;

	// SSCE_CheckWord (FUN_002c8c00) with the session's options
	int CheckWord(const std::string& word, std::string* replacement = nullptr) const;
	int CheckWord(uint32_t options, const std::string& word, std::string* replacement) const;
	// SSCE_CheckBlock: from the block's cursor, the first word that isn't
	// good (its result, and the cursor on it), or kEndOfBlock
	int CheckBlock(Block& block, std::string& word, std::string* replacement) const;
	// SSCE_Suggest: up to `count` words like it, best first, with scores
	std::vector<std::pair<std::string, int>> Suggest(const std::string& word, int depth,
		size_t count) const;

	uint32_t options = 0x18680;
	uint16_t language = 0x616d;

private:
	int Lookup(uint32_t options, const std::string& word, std::string* replacement) const;
	int LexContains(uint32_t options, const Lexicon& lex, const std::string& word,
		std::string* replacement) const;

	std::vector<std::unique_ptr<Lexicon>> fLexicons;
};

} // namespace ssce
} // namespace gobe

#endif
