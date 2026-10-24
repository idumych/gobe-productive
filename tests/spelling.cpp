// spelling: the spell checking session (Speller: TSpellSession, TSpeller)
// and its walk through a document. The preferences read from PREF 42 are
// the ones tools/gobersrc.py shows (typo and phonetic suggestions, depth 1,
// "USEnglish", "UserDictionary"); a session starts on a copy of the
// original's Dictionaries, and fails without the user dictionary (STR# 9
// message 4) or the language folder (3); common words are good,
// "recieve" (USEnglish1: "recieve" A "receive") is questionable with
// "receive" first, "teh" ("teh" C "the") with "the"; a chosen replacement
// makes the word an automatic one; a skipped word passes until the
// session's skipped words are cleared; a learned one is written to the user
// dictionary; Latin-1 words convert. In each sample, misspellings put in
// its main text's first paragraph are found where they are, numbers and
// the user dictionary's words aren't, and a start inside a word backs up
// to the word.
// Arguments: the sample documents (the Dictionaries folder is beside
// their Samples folder).
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "Document.h"
#include "ResourceFile.h"
#include "Spelling.h"

using namespace gobe;
namespace fs = std::filesystem;

static int sOk = 0, sFailed = 0;

static void
Check(bool ok, const std::string& what)
{
	if (ok)
		sOk++;
	else {
		sFailed++;
		printf("FAIL %s\n", what.c_str());
	}
}


static bool
Has(const std::vector<std::string>& list, const std::string& word)
{
	for (const auto& s : list)
		if (s == word)
			return true;
	return false;
}


int
main(int argc, char** argv)
{
	if (argc < 2) {
		printf("spelling: no documents, ok 0\n");
		return 1;
	}
	fs::path install = fs::path(argv[1]).parent_path().parent_path();
	if (!fs::exists(install / "Dictionaries"))
		install = fs::path(argv[argc - 1]).parent_path().parent_path();
	ResourceFile resources;
	Check(resources.Load((install / "Gobe Productive").string()), "the binary's resources load");

	SpellPrefs prefs;
	const ResourceFile::Resource* pref = resources.Find(FourCC("PREF"), 42);
	Check(pref != nullptr && prefs.Read(pref->data), "PREF 42 reads");
	Check(!prefs.ignoreMixedCase && !prefs.ignoreMixedDigits && !prefs.ignoreAllCaps
		&& !prefs.ignoreCapitalized && prefs.suggestTypo && prefs.suggestPhonetic
		&& prefs.depth == 1 && prefs.mainDict == "USEnglish" && prefs.userDict == "UserDictionary",
		"PREF 42: typo and phonetic, depth 1, USEnglish, UserDictionary (" + prefs.mainDict
		+ ", " + prefs.userDict + ")");
	std::vector<std::string> languages;
	for (int i = 0; i < 4; i++)
		languages.push_back(IndexString(resources, 18, i));

	// a copy, as learning writes to the user dictionary
	fs::path dicts = fs::temp_directory_path() / "spelling_dicts";
	fs::remove_all(dicts);
	fs::create_directories(dicts);
	{
		Speller none;
		Check(none.Start(dicts.string(), prefs, languages) == 4, "no user dictionary: message 4");
	}
	fs::copy_file(install / "Dictionaries" / "UserDictionary", dicts / "UserDictionary");
	{
		Speller none;
		Check(none.Start(dicts.string(), prefs, languages) == 3, "no USEnglish folder: message 3");
		Check(none.CheckWord("the") == Speller::kNoSession, "no session: 1");
	}
	fs::copy(install / "Dictionaries" / "USEnglish", dicts / "USEnglish");

	Speller speller;
	Check(speller.Start(dicts.string(), prefs, languages) == 0, "the session starts");
	Check(speller.Depth() == 50, "depth 1 searches 50");
	Check(speller.Session() != nullptr && (speller.Session()->options & ssce::kSuggestSplitWords)
		&& !(speller.Session()->options & ssce::kSplitWords)
		&& !(speller.Session()->options & ssce::kSplitContracted),
		"English: split words suggested, not split");
	for (const char* good : {"the", "February", "Gobe", "BeOS", "running"})
		Check(speller.CheckWord(good) == Speller::kGood, std::string(good) + " is good");

	Check(speller.CheckWord("recieve") == Speller::kQuestionable, "recieve is questionable");
	std::vector<std::string> alternatives = speller.Alternatives();
	Check(!alternatives.empty() && alternatives[0] == "receive",
		"recieve: receive first (" + (alternatives.empty() ? "" : alternatives[0]) + ")");
	Check(alternatives.size() <= 8, "at most 8 alternatives");
	size_t repeats = 0;
	for (const auto& s : alternatives)
		repeats += s == "receive";
	Check(repeats == 1, "receive once");
	Check(speller.CheckWord("teh") == Speller::kQuestionable, "teh is questionable");
	alternatives = speller.Alternatives();
	Check(!alternatives.empty() && alternatives[0] == "the", "teh: the first");
	Check(speller.CheckWord("exampel") == Speller::kQuestionable, "exampel is questionable");
	alternatives = speller.Alternatives();
	Check(Has(alternatives, "example"), "exampel: example suggested");
	Check(speller.CheckWord("qwzxqwzx") == Speller::kQuestionable
		&& speller.Alternatives().empty(), "qwzxqwzx: nothing like it (None)");

	// a replacement chosen: from then on automatic
	speller.CheckWord("recieve");
	speller.Alternatives();
	Check(speller.AddReplacement(0), "the replacement is chosen");
	Check(speller.CheckWord("recieve") == Speller::kAutoReplace
		&& speller.AutoReplacement() == "receive", "recieve is now replaced by receive");

	// skipped, then cleared
	Check(speller.CheckWord("Zorblax") == Speller::kQuestionable, "Zorblax is questionable");
	Check(speller.Skip(), "skipped");
	Check(speller.CheckWord("Zorblax") == Speller::kGood, "Zorblax passes");
	speller.ClearSkipped();
	Check(speller.CheckWord("Zorblax") == Speller::kQuestionable, "cleared: questionable again");
	Check(speller.CheckWord("recieve") == Speller::kQuestionable, "cleared: recieve not automatic");

	// learned
	speller.CheckWord("Frobnitzer");
	Check(speller.Learn(), "learned");
	Check(speller.CheckWord("Frobnitzer") == Speller::kGood, "Frobnitzer is good");
	std::ifstream user(dicts / "UserDictionary");
	std::string text((std::istreambuf_iterator<char>(user)), std::istreambuf_iterator<char>());
	Check(text.find("\nFrobnitzer\n") != std::string::npos, "the user dictionary has it");

	// Latin-1
	Check(Latin1FromUtf8("caf\xc3\xa9") == "caf\xe9", "UTF-8 to Latin-1");
	Check(Latin1FromUtf8("\xe2\x82\xac") == "\x1a", "no euro in Latin-1: 0x1a");
	Check(Utf8FromLatin1("Jo\xebl") == "Jo\xc3\xabl", "Latin-1 to UTF-8");
	Check(speller.CheckWord("Jo\xc3\xabl") == Speller::kGood, "Jo\xc3\xabl (the user dictionary's)");

	// the walk
	int docs = 0;
	for (int i = 1; i < argc; i++) {
		std::string name = fs::path(argv[i]).filename().string();
		Document doc;
		std::string error;
		if (!LoadDocument(argv[i], doc, error) || doc.MainPart() < 0)
			continue;
		size_t main = size_t(doc.MainPart());
		WPPart* wp = doc.EditableWordPart(main);
		if (wp == nullptr || wp->paragraphs.empty())
			continue;
		docs++;
		std::string& first = wp->paragraphs[0].text;
		first.insert(0, "12345 Gobe qwzxqwzx then xqzw ");
		SpellPosition at = {main, 0, 0};
		Check(SpellStartFromPosition(doc, at) && at.offset == 0, name + ": starts at 0");
		size_t length = 0;
		int result = FindMisspelling(doc, speller, at, length);
		Check(result == Speller::kQuestionable && at.part == main && at.paragraph == 0
			&& at.offset == 11 && length == 8, name + ": qwzxqwzx found at 11 (" + std::to_string(result)
			+ " " + std::to_string(at.offset) + ")");
		at.offset += length;
		result = FindMisspelling(doc, speller, at, length);
		Check(result == Speller::kQuestionable && at.paragraph == 0 && at.offset == 25
			&& length == 4, name + ": then xqzw at 25");
		// inside "qwzxqwzx": back to its start's break (the space before)
		at = {main, 0, 14};
		Check(SpellStartFromPosition(doc, at) && at.offset == 10, name + ": inside a word: back to "
			"the space before it (" + std::to_string(at.offset) + ")");
		// from the top: the first misspelling is no later than this one
		SpellPosition top;
		Check(SpellStartFromTop(doc, top), name + ": a start from the top");
		result = FindMisspelling(doc, speller, top, length);
		Check(result != 0 && (top.part < main || (top.part == main && top.paragraph == 0
			&& top.offset <= 11)), name + ": from the top, found by then");
		// every word found from the top isn't good, and they come in order
		SpellPosition walk = top;
		size_t found = 0;
		bool ordered = true, bad = true;
		while (result != 0 && found < 2000) {
			found++;
			const WPPart* p = doc.WordPart(walk.part);
			std::string word = p->paragraphs[walk.paragraph].text.substr(walk.offset, length);
			bad = bad && speller.CheckWord(word) != Speller::kGood;
			SpellPosition before = walk;
			walk.offset += length;
			result = FindMisspelling(doc, speller, walk, length);
			if (result != 0)
				ordered = ordered && (walk.part > before.part || (walk.part == before.part
					&& (walk.paragraph > before.paragraph || (walk.paragraph == before.paragraph
					&& walk.offset >= before.offset + 1))));
		}
		Check(bad && ordered, name + ": " + std::to_string(found) + " found, in order, none good");
	}
	fs::remove_all(dicts);
	printf("spelling: documents %d, ok %d, failed %d\n", docs, sOk, sFailed);
	return sFailed > 0;
}
