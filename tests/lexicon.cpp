// lexicon: Thesaurus… and Dictionary…'s files (TThesaurus, TDictionary),
// in the formats tools/wordnet_to_gobe.py writes, built here: a thesaurus
// looked up by headword (the binary search) and by a word only listed
// (Manufacture); a dictionary's entries found by a word's start, case
// ignored, with dotted headwords, and its \part\ runs.
// Arguments: none (documents given are ignored).
#include <cstdio>
#include <fstream>
#include <string>

#include "Lexicon.h"

using namespace gobe;

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


int
main()
{
	const char* thesaurus = "/tmp/lexicon_test_thesaurus";
	std::ofstream(thesaurus) << "auto:-noun-,car,automobile\n"
		"big:-adjective-,large,great\n"
		"car:-noun-,auto,automobile,machine,-verb-,drive\n"
		"glad:-adjective-,happy\n"
		"large:-adjective-,big\n"
		"zebra:-noun-,horse\n";
	std::vector<ThesaurusPart> car = ThesaurusLookup(thesaurus, "car");
	Check(car.size() == 2 && car[0].name == "noun" && car[0].words.size() == 3
		&& car[0].words[2] == "machine" && car[1].name == "verb" && car[1].words[0] == "drive",
		"car: three nouns, a verb");
	Check(ThesaurusLookup(thesaurus, "zebra").size() == 1, "the last line");
	Check(ThesaurusLookup(thesaurus, "auto").size() == 1, "the first line");
	// "happy" is no headword: the lines listing it (Manufacture)
	std::vector<ThesaurusPart> happy = ThesaurusLookup(thesaurus, "happy");
	Check(happy.size() == 1 && happy[0].name == "adjective" && happy[0].words.size() == 1
		&& happy[0].words[0] == "glad", "happy: listed by glad");
	std::vector<ThesaurusPart> machine = ThesaurusLookup(thesaurus, "machine");
	Check(machine.size() == 1 && machine[0].words.size() == 1 && machine[0].words[0] == "car",
		"machine: listed by car");
	Check(ThesaurusLookup(thesaurus, "nothing").empty(), "nothing");
	Check(ThesaurusLookup("/tmp/no such thesaurus", "car").empty(), "no file");

	const char* dictionary = "/tmp/lexicon_test_dictionary";
	std::ofstream(dictionary) << "@aard.vark\n\\noun\\\n  1. an animal\n\n"
		"@Car\n\\noun\\\n  1. a vehicle\n\\verb\\\n  1. to ride\n\n"
		"@car.bon\n\\noun\\\n  1. an element\n\n"
		"@zoo\n\\noun\\\n  1. animals\n";
	Dictionary d;
	Check(d.Open(dictionary) && d.Count() == 4, "four entries");
	size_t at = d.Find("car");
	Check(at == 1 && d.Headword(at) == "Car", "car: Car, case ignored");
	Check(d.Find("carb") == 2 && d.Headword(2) == "carbon", "carb: carbon, its dots left out");
	Check(d.Find("AARDVARK") == 0, "aardvark");
	Check(d.Find("zoology") == d.Count() && d.Find("b") == d.Count(), "none");
	std::vector<std::pair<size_t, size_t>> runs;
	std::string shown = DictionaryRuns(d.Text(1), runs);
	Check(shown == "noun\n  1. a vehicle\nverb\n  1. to ride", "the text: " + shown);
	Check(runs.size() == 2 && runs[0] == std::make_pair(size_t(0), size_t(4))
		&& shown.substr(runs[1].first, runs[1].second - runs[1].first) == "verb", "the runs");
	Check(!d.Open("/tmp/no such dictionary"), "no file");

	printf("lexicon: ok %d, failed %d\n", sOk, sFailed);
	return sFailed ? 1 : 0;
}
