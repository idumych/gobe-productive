// doc_findchange: Find/Change (TFindReplace, TTextAccessor). In each sample with
// text: a word of its main text found from the top where a plain search of
// the shown text parts' paragraphs finds it first; in another case when
// case is ignored, not when it isn't; from after its last occurrence found
// again only with Wrap; Replace All replacing as many as the plain search
// counts, leaving none. Made-up text: Entire Word passes "other" by,
// a match across a paragraph break (\n) starts in the first paragraph.
// The dialog's conversions follow TDialogFindChange (\t \l \n \f \p \m,
// "/" as \/); ToUpperUTF8 as the original maps letters (Ă to ā included).
// Arguments: the sample documents.
#include <cstdio>
#include <string>

#include "Document.h"
#include "Editing.h"
#include "Find.h"
#include "WordCount.h"

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


// the plain search: each shown text part's paragraphs in turn
static bool
PlainFind(const Document& doc, const std::string& word, size_t& part, uint32_t& start,
	int& count)
{
	bool found = false;
	count = 0;
	for (size_t p : ShownTextParts(doc)) {
		const WPPart* wp = doc.WordPart(p);
		uint32_t base = 0;
		for (const WPParagraph& paragraph : wp->paragraphs) {
			for (size_t at = paragraph.text.find(word); at != std::string::npos;
					at = paragraph.text.find(word, at + word.size())) {
				if (!found) {
					found = true;
					part = p;
					start = base + uint32_t(at);
				}
				count++;
			}
			base += uint32_t(paragraph.text.size());
		}
	}
	return found;
}


int
main(int argc, char** argv)
{
	// the conversions
	Check(FindToDisplay("a\tb\x01" "c\n\x02\f\x04/") == "a\\tb\\lc\\n\\f\\p\\m\\/",
		"specials shown");
	Check(FindFromDisplay("a\\tb\\lc\\n\\f\\p\\m\\/\\q", false) == "a\tb\x01" "c\n\x02\f\x04/\\q",
		"specials read back (\\q stays)");
	Check(FindFromDisplay("x\\m", true) == "x\\m", "\\m is the find field's only");
	Check(FindHasSpecials("a\tb") && !FindHasSpecials("ab"), "raw specials seen");
	std::string upper = "a\xc3\xa0\xc3\xbf\xc4\x82\xc3\xb7z";
	ToUpperUTF8(upper);
	Check(upper == "A\xc3\x80\xc5\xb8\xc4\x81\xc3\xb7Z", "ToUpperUTF8's mapping");
	Check(CharsMatchUTF8("\xc3\x80", "\xc3\xa0", false) && !CharsMatchUTF8("\xc3\x80", "\xc3\xa0", true),
		"\xc3\xa0 matches \xc3\x80 ignoring case only");

	int docs = 0;
	for (int i = 1; i < argc; i++) {
		std::string name = argv[i];
		name = name.substr(name.rfind('/') + 1);
		Document doc;
		std::string error;
		if (!LoadDocument(argv[i], doc, error) || doc.MainPart() < 0)
			continue;
		size_t main = size_t(doc.MainPart());
		WPPart* wp = doc.EditableWordPart(main);
		if (wp == nullptr || wp->paragraphs.empty())
			continue;
		// a word of five letters or more from the main text
		std::string word;
		for (const WPParagraph& p : wp->paragraphs) {
			size_t at = 0, length;
			bool digits;
			while (word.empty() && NextWordInBlock(p.text, at, length, digits)) {
				std::string w = p.text.substr(at, length);
				bool letters = length >= 5;
				for (char c : w)
					letters = letters && ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'));
				if (letters)
					word = w;
				at += length;
			}
			if (!word.empty())
				break;
		}
		if (word.empty())
			continue;
		docs++;
		size_t part;
		uint32_t start;
		int count;
		PlainFind(doc, word, part, start, count);

		FindReplace find;
		find.caseSensitive = true;
		find.SetFind(word);
		FoundText found;
		Check(FindNext(doc, find, false, 0, 0, found) && found.part == part && found.start == start
			&& found.end == start + word.size(), name + ": \"" + word + "\" found from the top ("
			+ std::to_string(found.part) + " " + std::to_string(found.start) + ", expected "
			+ std::to_string(part) + " " + std::to_string(start) + ")");

		// another case
		std::string other = word;
		for (char& c : other)
			c = c >= 'a' ? char(c - 0x20) : char(c + 0x20);
		size_t otherPart;
		uint32_t otherStart;
		int otherCount;
		bool otherExists = PlainFind(doc, other, otherPart, otherStart, otherCount);
		find.SetFind(other);
		Check(FindNext(doc, find, false, 0, 0, found) == otherExists,
			name + ": in another case, as case matters, as the text has it");
		find.SetCaseSensitive(false);
		Check(FindNext(doc, find, false, 0, 0, found) && found.part == part && found.start == start,
			name + ": in another case, case ignored");

		// after the last one: only with Wrap
		find.SetCaseSensitive(true);
		find.SetFind(word);
		const WPPart* last = doc.WordPart(ShownTextParts(doc).back());
		uint32_t end = PartPosition(*last, {last->paragraphs.size() - 1,
			last->paragraphs.back().text.size()});
		size_t lastPart = ShownTextParts(doc).back();
		uint32_t endInFirst = PartPosition(*doc.WordPart(part), {doc.WordPart(part)->paragraphs.size()
			- 1, doc.WordPart(part)->paragraphs.back().text.size()});
		find.wrap = true;
		Check(FindNext(doc, find, true, part, endInFirst, found) && found.start == start
			&& found.part == part, name + ": wrapped around from its part's end"
			+ (lastPart != part ? std::string(" (through the later parts)") : ""));
		if (lastPart == part) {
			find.wrap = false;
			Check(!FindNext(doc, find, true, part, end, found), name + ": not without Wrap");
		}

		// Replace All
		Document copy = doc;
		find.replace = "\x01REPLACED\x01";
		FoundText lastReplaced;
		int replaced = ReplaceEvery(copy, find, lastReplaced);
		size_t dummyPart;
		uint32_t dummyStart;
		int left;
		Check(replaced == count && !PlainFind(copy, word, dummyPart, dummyStart, left),
			name + ": Replace All replaced " + std::to_string(replaced) + " of "
			+ std::to_string(count));

		// Entire Word, a match across a paragraph break
		wp->paragraphs[0].text.insert(0, "other the ");
		FindReplace whole;
		whole.caseSensitive = true;
		whole.wholeWord = true;
		whole.SetFind("the");
		Check(FindNext(doc, whole, true, main, 0, found) && found.part == main && found.start == 6,
			name + ": entire word \"the\" at 6, not in \"other\" (" + std::to_string(found.start) + ")");
		whole.wholeWord = false;
		Check(FindNext(doc, whole, true, main, 0, found) && found.start == 1,
			name + ": \"the\" in \"other\" without Entire Word");
		if (wp->paragraphs.size() > 1 && !wp->paragraphs[1].text.empty()
				&& wp->paragraphs[0].text.back() == '\n') {
			std::string& first = wp->paragraphs[0].text;
			std::string across = first.substr(first.size() - 3) + wp->paragraphs[1].text.substr(0, 2);
			FindReplace span;
			span.caseSensitive = true;
			span.wrap = false;
			span.SetFind(across);
			Check(FindNext(doc, span, true, main, 0, found) && found.part == main
				&& found.start == first.size() - 3, name + ": a match across the paragraph break");
		}
	}
	printf("findchange: documents %d, ok %d, failed %d\n", docs, sOk, sFailed);
	return sFailed > 0;
}
