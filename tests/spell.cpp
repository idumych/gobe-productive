// spell: the SSCE engine (Ssce) with the original's US English lexicons.
// Its character tables equal the binary's (Latin1_CharClass at file offset
// 0x2ffe80, Latin1_ToBase at 0x2fff80, the phonetic classes at 0x2f9bf6);
// the compressed lexicon's segments decode to sorted words starting with
// their prefixes; common words are good, nonsense is misspelled; the
// change lexicon's lines ("Febuary" A "February", "abd" C "and") give their
// results and replacements, in the word's case; suggestions for typos hold
// the words meant; a doubled word and the first word's capital are found in
// a block; the user dictionary learns a word.
// Arguments: none (run from the project root or port/).
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "Ssce.h"

using namespace gobe;
using namespace gobe::ssce;

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
Suggests(const std::vector<std::pair<std::string, int>>& list, const std::string& word)
{
	for (const auto& s : list)
		if (s.first == word)
			return true;
	return false;
}


int
main()
{
	std::filesystem::path install;
	for (const char* root : {"original/installed", "../original/installed"}) {
		if (!std::filesystem::exists(root))
			continue;
		for (const auto& entry : std::filesystem::directory_iterator(root)) {
			std::filesystem::path p = entry.path() / "Gobe Productive 2.0";
			if (std::filesystem::exists(p / "Dictionaries"))
				install = p;
		}
	}
	if (install.empty()) {
		printf("spell: no original install, ok 0\n");
		return 1;
	}

	// the tables
	std::ifstream binary(install / "Gobe Productive", std::ios::binary);
	std::vector<uint8_t> image((std::istreambuf_iterator<char>(binary)),
		std::istreambuf_iterator<char>());
	int differ = 0;
	for (int c = 0; c < 256; c++)
		differ += image[0x2ffe80 + c] != CharClass(uint8_t(c))
			|| image[0x2fff80 + c] != ToBase(uint8_t(c))
			|| image[0x2f9bf6 + c] != PhoneticClass(uint8_t(c));
	Check(differ == 0, "the character tables equal the binary's (" + std::to_string(differ)
		+ " differ)");

	std::filesystem::path dir = install / "Dictionaries" / "USEnglish";
	Session session;
	std::string error;
	int main3 = session.OpenLex((dir / "USEnglish3").string(), error);
	int words2 = session.OpenLex((dir / "USEnglish2").string(), error);
	int changes1 = session.OpenLex((dir / "USEnglish1").string(), error);
	Check(main3 >= 0 && words2 >= 0 && changes1 >= 0, "the lexicons open (" + error + ")");
	auto* compressed = dynamic_cast<CompressedLexicon*>(session.Lex(main3));
	Check(compressed != nullptr, "USEnglish3 is compressed");
	Check(dynamic_cast<TextLexicon*>(session.Lex(changes1)) != nullptr, "USEnglish1 is text");

	// the segments
	size_t words = 0, unsorted = 0, prefixed = 0;
	for (size_t k = 0; compressed != nullptr && k < compressed->SegmentCount(); k++) {
		std::vector<std::string> list = compressed->SegmentWords(k);
		for (size_t i = 0; i < list.size(); i++) {
			words++;
			if (i > 0 && !(list[i - 1] < list[i]))
				unsorted++;
			if (i == 0 || list[i].compare(0, 3, list[0], 0, 3) != 0)
				prefixed += i > 0;
		}
	}
	Check(words > 50000, "the compressed lexicon has many words (" + std::to_string(words) + ")");
	Check(unsorted == 0, "each segment's words are sorted (" + std::to_string(unsorted) + " not)");
	Check(prefixed == 0, "each segment's words share its prefix");

	for (const char* good : {"the", "and", "apple", "receive", "example", "running", "February",
			"Wednesday", "I'm", "THE", "The", "a", "I", "dog's", "dogs'"})
		Check(session.CheckWord(good) == 0, std::string("\"") + good + "\" is good");
	for (const char* bad : {"qwzx", "exampel", "teh", "Febuary"})
		Check((session.CheckWord(bad) & (kMisspelled | kAutoChange | kConditionalChange)) != 0,
			std::string("\"") + bad + "\" isn't");

	std::string replacement;
	int r = session.CheckWord("Febuary", &replacement);
	Check(r == kAutoChange && replacement == "February",
		"\"Febuary\" changes to \"February\" (" + std::to_string(r) + " " + replacement + ")");
	r = session.CheckWord("absense", &replacement);
	Check(r == kAutoChange && replacement == "absence", "\"absense\": \"absence\" (" + replacement + ")");
	r = session.CheckWord("abd", &replacement);
	Check(r == kConditionalChange && replacement == "and",
		"\"abd\" conditionally \"and\" (" + std::to_string(r) + " " + replacement + ")");
	r = session.CheckWord("Abd", &replacement);
	Check(replacement == "And", "\"Abd\": \"And\" (" + replacement + ")");
	r = session.CheckWord("ABSENSE", &replacement);
	Check(replacement == "ABSENCE", "\"ABSENSE\": \"ABSENCE\" (" + replacement + ")");

	auto list = session.Suggest("exampel", 50, 16);
	Check(Suggests(list, "example"), "\"exampel\" suggests \"example\"");
	list = session.Suggest("recieve", 50, 16);
	Check(Suggests(list, "receive"), "\"recieve\" suggests \"receive\"");
	list = session.Suggest("Wendsday", 50, 16);
	Check(!list.empty() && list[0].first == "Wednesday",
		"\"Wendsday\" suggests \"Wednesday\" first (" + (list.empty() ? "" : list[0].first) + ")");
	list = session.Suggest("Speling", 50, 16);
	Check(Suggests(list, "Spelling"), "\"Speling\" suggests \"Spelling\" (in its case)");
	bool sorted = true;
	for (size_t i = 1; i < list.size(); i++)
		sorted = sorted && list[i - 1].second >= list[i].second;
	Check(sorted, "suggestions come best first");

	// a block: the doubled word, the first word's capital
	uint32_t options = session.options | kReportDoubledWord | kReportUncapped;
	Session strict;
	strict.OpenLex((dir / "USEnglish3").string(), error);
	strict.options = options;
	Block block("this is is a test, a test.", &strict.options);
	std::string word;
	r = strict.CheckBlock(block, word, &replacement);
	Check(r == kUncapped && word == "this" && replacement == "This",
		"the first word wants a capital (" + std::to_string(r) + " " + word + ")");
	block.NextWord();
	r = strict.CheckBlock(block, word, &replacement);
	Check((r & kDoubledWord) && word == "is", "\"is is\" is doubled (" + std::to_string(r) + " "
		+ word + ")");
	block.NextWord();
	r = strict.CheckBlock(block, word, &replacement);
	Check(r == kEndOfBlock, "\"test, a test\" isn't doubled (" + std::to_string(r) + " " + word + ")");

	// the user dictionary learns
	std::string user = "/tmp/spell_user";
	{
		std::ofstream f(user, std::ios::trunc);
		f << "BeOS\nGobe\n";
	}
	int u = session.OpenLex(user, error);
	auto* userLex = dynamic_cast<TextLexicon*>(session.Lex(u));
	Check(session.CheckWord("Gobe") == 0, "\"Gobe\" is in the user dictionary");
	Check(session.CheckWord("Zorblax") & kMisspelled, "\"Zorblax\" isn't yet");
	Check(userLex != nullptr && userLex->Add("Zorblax"), "learned");
	Check(session.CheckWord("Zorblax") == 0, "\"Zorblax\" is now");
	std::ifstream back(user);
	std::string text((std::istreambuf_iterator<char>(back)), std::istreambuf_iterator<char>());
	Check(text == "BeOS\nGobe\nZorblax\n", "the user dictionary is written back plain (" + text + ")");

	printf("spell: words %zu, ok %d, failed %d\n", words, sOk, sFailed);
	return sFailed > 0;
}
