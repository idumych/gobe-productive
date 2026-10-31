// doc_wordout: Save As Microsoft Word (WordFileBytes, WordBytes) read back
// with ReadWord. Paragraphs built here: faces, a size, a font and a color
// per run; alignment, indents and spacing; a line break; a page break;
// Windows-1252 text and text that needs UTF-16; 400 paragraphs, so the
// FKPs take several pages; the FIB's fields. Then every document given:
// its main text written and read back as WordParagraphsOf has it.
// Arguments: documents.
#include <cstdio>
#include <string>
#include <vector>

#include "Document.h"
#include "Excel.h"
#include "WPPart.h"
#include "Word.h"

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


static uint32_t
U32(const std::vector<uint8_t>& d, size_t at)
{
	return d[at] | d[at + 1] << 8 | d[at + 2] << 16 | uint32_t(d[at + 3]) << 24;
}


static WordRun
Run(const std::string& text, float size = 12, const std::string& font = "Times New Roman")
{
	WordRun r;
	r.text = text;
	r.size = size;
	r.font = font;
	r.hasColor = true;
	return r;
}


static bool
SameRun(const WordRun& a, const WordRun& b)
{
	return a.text == b.text && a.bold == b.bold && a.italic == b.italic
		&& a.underline == b.underline && a.strike == b.strike
		&& a.superscript == b.superscript && a.subscript == b.subscript && a.size == b.size
		&& a.font == b.font && a.r == b.r && a.g == b.g && a.b == b.b;
}


// the paragraphs read back are the ones written: text, runs, properties
static void
Compare(const std::vector<WordParagraph>& want, const std::vector<WordParagraph>& got,
	const std::string& what)
{
	Check(got.size() == want.size(), what + ": " + std::to_string(got.size())
		+ " paragraphs, not " + std::to_string(want.size()));
	for (size_t i = 0; i < want.size() && i < got.size(); i++) {
		const WordParagraph& w = want[i];
		const WordParagraph& g = got[i];
		std::string at = what + ", paragraph " + std::to_string(i);
		// (an empty paragraph's run has no text to read back)
		size_t runs = w.runs.size() == 1 && w.runs[0].text.empty() ? 0 : w.runs.size();
		bool same = g.runs.size() == runs;
		for (size_t k = 0; same && k < runs; k++)
			same = SameRun(w.runs[k], g.runs[k]);
		Check(same, at + ": the runs");
		Check(g.alignment == w.alignment && g.left == w.left && g.first == w.first
			&& g.right == w.right && g.before == w.before && g.after == w.after,
			at + ": alignment, indents, spacing");
		Check(g.pageBreak == w.pageBreak, at + ": the page break");
	}
}


int
main(int argc, char** argv)
{
	std::string error;
	// 1. faces, sizes, fonts, colors; paragraph properties; breaks
	std::vector<WordParagraph> ps(5);
	ps[0].runs = {Run("Hello "), Run("bold italic"), Run(" world")};
	ps[0].runs[1].bold = ps[0].runs[1].italic = true;
	ps[0].alignment = 1;
	WordRun red = Run("Red Arial", 14, "Arial");
	red.r = 255;
	ps[1].runs = {red, Run(" under"), Run(" strike"), Run("sup"), Run("sub")};
	ps[1].runs[1].underline = true;
	ps[1].runs[2].strike = true;
	ps[1].runs[3].superscript = true;
	ps[1].runs[4].subscript = true;
	ps[1].left = 36;
	ps[1].first = 54;
	ps[1].right = 18;
	ps[1].before = 6;
	ps[1].after = 12;
	ps[2].runs = {Run("Caf\xc3\xa9 \xe2\x80\x9cquoted\xe2\x80\x9d\x01next line", 10, "Courier 10 BT")};
	ps[2].pageBreak = true;
	ps[3].runs = {Run("")};		// an empty paragraph
	ps[3].alignment = 2;
	ps[4].runs = {Run("the end")};
	ps[4].alignment = 3;
	std::vector<uint8_t> file = WordFileBytes(ps);
	std::vector<WordParagraph> back;
	Check(ReadWord(file, back, error), "read: " + error);
	Compare(ps, back, "1252");

	// the FIB and the streams
	CompoundFile ole;
	std::vector<uint8_t> word, table;
	Check(ole.Open(file, error) && ole.Stream("WordDocument", word) && ole.Stream("1Table", table),
		"the WordDocument and 1Table streams");
	if (word.size() > 0x400) {
		Check(word[0] == 0xec && word[1] == 0xa5 && word[2] == 0xc1 && word[3] == 0,
			"wIdent, nFib 0xc1");
		Check((word[0x0b] & 0x02) != 0, "fWhichTblStm");
		// 5 paragraphs: 15 + 33 + 31 + 0 + 7 characters and their marks
		size_t text = 0;
		for (const WordParagraph& p : ps)
			for (const WordRun& r : p.runs)
				for (unsigned char c : r.text)
					text += (c & 0xc0) != 0x80;
		Check(U32(word, 0x4c) == text + ps.size(), "ccpText " + std::to_string(U32(word, 0x4c)));
		Check(U32(word, 0x40) == word.size(), "cbMac");
		Check(U32(word, 0x9a + 1 * 8 + 4) > 0, "a style sheet");
		Check(U32(word, 0x9a + 6 * 8 + 4) == 4 * 2 + 12, "one section");
		Check(U32(word, 0x9a + 31 * 8 + 4) == 500, "the DOP");
		Check(word[0x400] == 'H', "the text at 0x400");
		uint32_t pcd = U32(table, U32(word, 0x9a + 33 * 8) + 1 + 4 + 8 + 2);
		Check(pcd == (0x400 * 2 | 0x40000000), "a compressed piece");
	}

	// 2. a character Windows-1252 hasn't: UTF-16
	std::vector<WordParagraph> greek(1);
	greek[0].runs = {Run("\xce\xa9 = 1 \xe2\x82\xac")};		// Omega, euro
	file = WordFileBytes(greek);
	Check(ReadWord(file, back, error), "read UTF-16: " + error);
	Compare(greek, back, "UTF-16");
	if (ole.Open(file, error) && ole.Stream("WordDocument", word) && word.size() > 0x402)
		Check(word[0x400] == 0xa9 && word[0x401] == 0x03, "Omega as UTF-16");

	// 3. 400 paragraphs, 3 runs each: several FKP pages of each kind
	std::vector<WordParagraph> many(400);
	for (size_t i = 0; i < many.size(); i++) {
		many[i].runs = {Run("plain " + std::to_string(i)), Run(" bold"), Run(" plain")};
		many[i].runs[1].bold = true;
		many[i].left = float(i % 7) * 9;
		many[i].after = float(i % 3);
	}
	file = WordFileBytes(many);
	Check(ReadWord(file, back, error), "read 400: " + error);
	Compare(many, back, "400");
	if (ole.Open(file, error) && ole.Stream("WordDocument", word)) {
		uint32_t chpxBins = (U32(word, 0x9a + 12 * 8 + 4) - 4) / 8;
		uint32_t papxBins = (U32(word, 0x9a + 13 * 8 + 4) - 4) / 8;
		Check(chpxBins > 1 && papxBins > 1, "several FKP pages: " + std::to_string(chpxBins)
			+ " CHPX, " + std::to_string(papxBins) + " PAPX");
	}

	// 4. the documents: their main text written and read back
	size_t documents = 0;
	for (int i = 1; i < argc; i++) {
		Document doc;
		if (!LoadDocument(argv[i], doc, error))
			continue;
		int main = doc.MainPart();
		if (main < 0 || doc.WordPart(size_t(main)) == nullptr)
			continue;
		documents++;
		std::vector<WordParagraph> want = WordParagraphsOf(doc, size_t(main));
		Check(want.size() == doc.WordPart(size_t(main))->paragraphs.size(),
			std::string(argv[i]) + ": a paragraph for each");
		// what ReadWord gives for the fonts written under Windows names
		for (WordParagraph& p : want)
			for (WordRun& r : p.runs) {
				if (r.font == "Dutch801 Rm BT")
					r.font = "Times New Roman";
				else if (r.font == "Swis721 BT")
					r.font = "Arial";
				// (twips: what the reader gets back)
			}
		for (WordParagraph& p : want) {
			for (float* v : {&p.left, &p.first, &p.right, &p.before, &p.after})
				*v = float(int(*v * 20 + (*v < 0 ? -0.5f : 0.5f))) / 20.0f;
		}
		Check(ReadWord(WordBytes(doc), back, error), std::string(argv[i]) + ": read " + error);
		Compare(want, back, argv[i]);
	}

	printf("documents %zu\n", documents);
	printf("doc_wordout: ok %d, failed %d\n", sOk, sFailed);
	return sFailed ? 1 : 0;
}
