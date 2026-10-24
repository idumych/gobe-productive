// Spell checking (TSpellSession, TSpeller, TTextAccessor's walk for
// TSpellBar::checkSpelling) over the engine: the Dictionaries folder's
// lexicons, words in UTF-8.
#ifndef GOBE_SPELLING_H
#define GOBE_SPELLING_H

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "Ssce.h"

namespace gobe {

struct Document;

// the spelling preferences (TAppPrefs, PREF 42: "ignore mixed case",
// "ignore mixed digits", "ignore mixed all uppercase", "ignore
// capitalized", "suggest typo", "suggest phonetic", "search depth", "main
// dict", "user dict")
struct SpellPrefs {
	bool ignoreMixedCase = false, ignoreMixedDigits = false, ignoreAllCaps = false,
		ignoreCapitalized = false, suggestTypo = true, suggestPhonetic = true;
	int32_t depth = 1;			// 0 quick (20), 1 (50), 2 thorough (100)
	std::string mainDict = "USEnglish", userDict = "UserDictionary";
	// TAppPrefs::PrefLoad from the PREF resource (TRsrHelper: bools as
	// 4 bytes, C strings); false if it's short
	bool Read(const std::vector<uint8_t>& data);
};

class Speller {
public:
	// TSpeller::CheckWord's results
	enum {
		kGood = 0, kNoSession = 1, kQuestionable = 2, kUncapped = 3, kMixedCase = 4,
		kMixedDigits = 5, kAutoReplace = 6
	};

	// TSpellSession::StartSession: 0, or STR# 9's message (1 no Dictionaries
	// folder, 2 no session, 3 no main dictionary, 4 no user dictionary).
	// `languages` are STR# 18's: the first two split contractions, the
	// next two compound words.
	int Start(const std::string& dictionaries, const SpellPrefs& prefs,
		const std::vector<std::string>& languages);
	bool Started() const { return fSession != nullptr; }
	void Close();
	// TSpellSession::SetUserOptions: the preferences' ignore and suggest
	// options and search depth for the session going
	void SetUserOptions(const SpellPrefs& prefs);

	// TSpeller::CheckWord: 0 good, 2 questionable, 3 wants a capital, 4
	// mixed case, 5 letters and digits, 6 to be replaced by AutoReplacement()
	// (a replacement chosen before); 1 without a session
	int CheckWord(const std::string& word);
	const std::string& AutoReplacement() const { return fAuto; }
	// TSpeller::RebuildAltWordPopUp: the last word's replacements, best
	// first (the change lexicon's first; then suggestions scoring over 74
	// and within 30 of the best); none: the menu says "None"
	std::vector<std::string> Alternatives();
	// TSpeller::AddReplacementWordToTempLex: the last word changes from now
	// on to the `index`th word found for it (Alternatives' words, counting
	// a repeat of the change lexicon's it left out)
	bool AddReplacement(int index);
	// TSpellBar::DoSkip, DoLearn: the last word passes for the session (the
	// temporary lexicon), or always (the user dictionary)
	bool Skip();
	bool Learn();
	// SSCE_ClearLex on the temporary lexicon
	void ClearSkipped();

	ssce::Session* Session() const { return fSession.get(); }
	int Depth() const { return fDepth; }

private:
	std::unique_ptr<ssce::Session> fSession;
	int fTemp = -1, fUser = -1;
	int fDepth = 50;
	std::string fWord;					// the last word, Latin-1
	std::string fReplacement;			// the change lexicon's for it
	std::vector<std::string> fFound;	// its replacement and suggestions
	std::string fAuto;
};

// TSpellSession::BuildLexPopUps: the Dictionaries folder's language
// folders (whose first lexicon, the folder's name and 1, opens:
// isValidMainFolder) and user dictionaries (files that open as text
// lexicons: isValidLex), by name as the folder lists them
void DictionaryChoices(const std::string& dictionaries, std::vector<std::string>& languages,
	std::vector<std::string>& users);
// TSpellSession::CreateNewUserLex (TLex_Create): an empty text lexicon
// ("#LID" line only) in the folder; false if the name is empty, taken, or
// it can't be written
bool CreateUserDictionary(const std::string& dictionaries, const std::string& name);

// Latin-1 and UTF-8 (convert_from_utf8 B_ISO1_CONVERSION substitutes 0x1a
// for what Latin-1 lacks)
std::string Latin1FromUtf8(const std::string& text);
std::string Utf8FromLatin1(const std::string& text);

// where checking is: a word processing part's paragraph, a byte offset
struct SpellPosition {
	size_t part = 0, paragraph = 0, offset = 0;
};

// TTextAccessor::StartFromTop: the first shown text part's start (false if
// none)
bool SpellStartFromTop(const Document& doc, SpellPosition& at);
// TTextAccessor::StartFromPosition (TWPTextIterator): the position in its
// part, backed up to the word's break (IsWordBreakChar: a byte under '1');
// false if the part isn't shown text
bool SpellStartFromPosition(const Document& doc, SpellPosition& at);
// TSpellBar::checkSpelling's walk: from `at`, the first word that isn't
// good (`at` on it, its `length`; numbers aren't checked), through the
// paragraphs and the shown text parts after; its result, or 0 at the end
int FindMisspelling(const Document& doc, Speller& speller, SpellPosition& at,
	size_t& length);

} // namespace gobe

#endif
