// Writing Tools' Thesaurus… and Dictionary…: the files they read
// (TThesaurus, TDictionary, DictIndex). Gobe Productive used BeOS's
// /boot/optional/goodies/thesaurus and dictionary; the port's are made
// from WordNet by tools/wordnet_to_gobe.py, in the same formats.
#ifndef GOBE_LEXICON_H
#define GOBE_LEXICON_H

#include <cstdint>
#include <string>
#include <vector>

namespace gobe {

// a part of speech ("noun") and its words (TThesEntry)
struct ThesaurusPart {
	std::string name;
	std::vector<std::string> words;
};

// TThesaurus::Lookup: the line "word:..." (a binary search: the lines are
// sorted by strcmp), its parts ("-noun-") and words (',' between them);
// none: TThesaurus::Manufacture, the lines that list `word`, each line's
// headword under the part it's listed in. Empty if the file can't be
// read or nothing lists the word.
std::vector<ThesaurusPart> ThesaurusLookup(const std::string& path, const std::string& word);

// TDictionary and DictIndex: entries "@headword" (with '.' between
// syllables) and their text up to the next '@', sorted ignoring case.
class Dictionary {
public:
	// false if `path` can't be read
	bool Open(const std::string& path);
	size_t Count() const { return fEntries.size(); }
	// TDictionary::Lookup(char const*): the first entry whose headword
	// (its dots left out, case ignored) starts with `word`; Count() if
	// none
	size_t Find(const std::string& word) const;
	// the entry's headword (dots left out) and its text after the
	// headword's line
	std::string Headword(size_t entry) const;
	std::string Text(size_t entry) const;

private:
	std::string fData;
	std::vector<size_t> fEntries;	// where each '@' is
};

// the text's runs between backslashes (TDialogDictionary::FixFont: shown
// in another font): the text with the backslashes taken out and the
// runs' (start, end) in it
std::string DictionaryRuns(const std::string& text,
	std::vector<std::pair<size_t, size_t>>& runs);

} // namespace gobe

#endif
