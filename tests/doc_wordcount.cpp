// doc_wordcount: Word Count (TWordCountDialog::countBlock, countDoc,
// GetNextWordInBlock, HasCJKChars). Expected counts follow the original's
// rules by hand:
//   "Hello world.\n": Hello, world; the period and newline are passed over:
//     13 characters, 2 words
//   "e.g. dogs' tail": "e.g." takes its last period without counting it,
//     "dogs'" leaves its apostrophe to be counted again: 15, 3
//   "well-known 3-4 -x": hyphens join between word characters; "-x" is
//     "-" passed over and "x": 17, 3
//   "café niño": é and ñ are Latin-1 letters: 9, 2
//   "日本": not letters (2 characters passed over), CJK: 2 words
//   "× ÷": not letters: 3, 0
// Arguments: none.
#include <cstdio>
#include <string>

#include "Document.h"
#include "Editing.h"
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


static void
Expect(const std::string& text, int64_t characters, int64_t words)
{
	WordCount c;
	CountBlock(text, c);
	Check(c.characters == characters && c.words == words, "\"" + text + "\": "
		+ std::to_string(c.characters) + " characters, " + std::to_string(c.words) + " words");
}


int
main()
{
	Expect("Hello world.\n", 13, 2);
	Expect("e.g. dogs' tail", 15, 3);
	Expect("well-known 3-4 -x", 17, 3);
	Expect("caf\xc3\xa9 ni\xc3\xb1o", 9, 2);
	Expect("\xe6\x97\xa5\xe6\x9c\xac", 2, 2);
	Expect("\xc3\x97 \xc3\xb7", 3, 0);
	Expect("", 0, 0);

	// a document: every paragraph of the shown text parts, the pages
	Document doc;
	std::string error;
	CreateBlankDocument(doc, error);
	WPPart* wp = doc.EditableWordPart(0);
	TextPosition at = InsertText(*wp, {0, 0}, "One two");
	at = SplitParagraph(*wp, at);
	InsertText(*wp, at, "three");
	WordCount c = CountDocument(doc);
	// "One two\n" 8 characters, "three" 5
	Check(c.paragraphs == 2 && c.words == 3 && c.characters == 13 && c.pages == 1,
		"document: " + std::to_string(c.paragraphs) + " paragraphs, " + std::to_string(c.words)
		+ " words, " + std::to_string(c.characters) + " characters");
	// a header's text counts; once removed it doesn't
	doc.InsertHeaderFooter(0);
	InsertText(*doc.EditableWordPart(size_t(doc.HeaderPart(0))), {0, 0}, "Head");
	c = CountDocument(doc);
	Check(c.paragraphs == 3 && c.words == 4, "with a header");
	doc.RemoveHeaderFooter(0);
	c = CountDocument(doc);
	Check(c.paragraphs == 2 && c.words == 3, "the header removed");
	printf("wordcount ok %d, failed %d\n", sOk, sFailed);
	return sFailed > 0 ? 1 : 0;
}
