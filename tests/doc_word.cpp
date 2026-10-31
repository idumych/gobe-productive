// doc_word: Word 97 documents read (ReadWord) and opened (ImportWord). A
// document is built here as [MS-DOC] lays it out: the FIB, the text in one
// compressed (Windows-1252) piece, a CHPX FKP (bold, italic, a size, a
// font, a color) and a PAPX FKP (centered, indents), the font table; a
// field whose code is dropped and result kept, a line break, a paragraph
// mark, a table cell mark.
// Arguments: none (sample paths, if given, are ignored).
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "Document.h"
#include "Excel.h"
#include "HTMLImport.h"
#include "PNG.h"
#include "Styles.h"
#include "TablePart.h"
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


static void
Put16(std::vector<uint8_t>& d, size_t at, uint16_t v)
{
	if (d.size() < at + 2)
		d.resize(at + 2, 0);
	d[at] = uint8_t(v);
	d[at + 1] = uint8_t(v >> 8);
}


static void
Put32(std::vector<uint8_t>& d, size_t at, uint32_t v)
{
	Put16(d, at, uint16_t(v));
	Put16(d, at + 2, uint16_t(v >> 16));
}


int
main()
{
	// the text: "Hello bold world" / "Caf\xe9 {field code}result\x0bnext" / "a\x07b"
	std::string text = "Hello bold world\r";
	size_t boldFrom = 6, boldTo = 10;
	text += "Caf\xe9 \x13 PAGE \x14" "7\x15\x0bnext\r";
	size_t secondFrom = 17;
	text += "a\x07" "b\r";
	uint32_t textAt = 0x800;
	std::vector<uint8_t> word(0x800, 0);
	Put16(word, 0, 0xa5ec);			// wIdent
	Put16(word, 2, 0xc1);			// nFib: Word 97
	Put16(word, 0x0a, 0x0200);		// fWhichTblStm: 1Table
	Put32(word, 0x4c, uint32_t(text.size()));	// ccpText
	word.insert(word.end(), text.begin(), text.end());
	word.resize(0xa00, 0);
	// a CHPX FKP at page 5 (0xa00): runs [text, bold), [bold, boldTo),
	// [boldTo, second), [second, end)
	size_t chpxPage = 0xa00;
	word.resize(chpxPage + 512, 0);
	uint32_t runs[5] = {textAt, textAt + uint32_t(boldFrom), textAt + uint32_t(boldTo),
		textAt + uint32_t(secondFrom), textAt + uint32_t(text.size())};
	for (int k = 0; k < 5; k++)
		Put32(word, chpxPage + size_t(k) * 4, runs[k]);
	// the CHPXs near the page's end (offsets in words)
	std::vector<uint8_t> bold = {0x35, 0x08, 0x01, 0x36, 0x08, 0x01};	// bold, italic
	std::vector<uint8_t> second = {0x43, 0x4a, 28, 0, 0x4f, 0x4a, 1, 0, 0x42, 0x2a, 6};	// 14 pt, font 1, red
	size_t boldAt = 0x1c0, secondAt = 0x1d0;
	word[chpxPage + boldAt] = uint8_t(bold.size());
	memcpy(&word[chpxPage + boldAt + 1], bold.data(), bold.size());
	word[chpxPage + secondAt] = uint8_t(second.size());
	memcpy(&word[chpxPage + secondAt + 1], second.data(), second.size());
	word[chpxPage + 5 * 4 + 0] = 0;
	word[chpxPage + 5 * 4 + 1] = uint8_t(boldAt / 2);
	word[chpxPage + 5 * 4 + 2] = 0;
	word[chpxPage + 5 * 4 + 3] = uint8_t(secondAt / 2);
	word[chpxPage + 511] = 4;
	// a PAPX FKP at page 6: the first paragraph centered, the second with
	// a 0.5 inch left indent and a first line 0.25 inch further in
	size_t papxPage = 0xc00;
	word.resize(papxPage + 512, 0);
	uint32_t paras[4] = {textAt, textAt + uint32_t(secondFrom),
		textAt + uint32_t(text.find("a\x07")), textAt + uint32_t(text.size())};
	for (int k = 0; k < 4; k++)
		Put32(word, papxPage + size_t(k) * 4, paras[k]);
	std::vector<uint8_t> centered = {0, 0, 0x03, 0x24, 1};				// istd 0, sprmPJc 1
	std::vector<uint8_t> indented = {0, 0, 0x0f, 0x84, 0xd0, 0x02, 0x11, 0x84, 0x68, 0x01};
	size_t centeredAt = 0x1a0, indentedAt = 0x1b0;
	auto papx = [&](size_t at, const std::vector<uint8_t>& p) {
		// PapxInFkp: an odd size as cb (2 cb - 1 bytes), an even one as 0
		// and cb' (2 cb' bytes); the istd and sprms after
		size_t q = papxPage + at;
		if (p.size() % 2) {
			word[q++] = uint8_t((p.size() + 1) / 2);
		} else {
			word[q++] = 0;
			word[q++] = uint8_t(p.size() / 2);
		}
		memcpy(&word[q], p.data(), p.size());
	};
	papx(centeredAt, centered);
	papx(indentedAt, indented);
	size_t bx = papxPage + 4 * 4;
	word[bx] = uint8_t(centeredAt / 2);
	word[bx + 13] = uint8_t(indentedAt / 2);
	word[bx + 26] = 0;
	word[papxPage + 511] = 3;

	// the table stream: the CLX, the bin tables, the font table
	std::vector<uint8_t> table;
	size_t clxAt = table.size();
	table.push_back(2);				// Pcdt
	Put32(table, table.size(), 4 + 4 + 8);
	Put32(table, table.size(), 0);
	Put32(table, table.size(), uint32_t(text.size()));
	Put16(table, table.size(), 0);
	Put32(table, table.size(), (textAt * 2) | 0x40000000);	// compressed
	Put16(table, table.size(), 0);
	uint32_t clxLength = uint32_t(table.size() - clxAt);
	size_t chpxAt = table.size();
	Put32(table, table.size(), textAt);
	Put32(table, table.size(), textAt + uint32_t(text.size()));
	Put32(table, table.size(), uint32_t(chpxPage / 512));
	size_t papxAt = table.size();
	Put32(table, table.size(), textAt);
	Put32(table, table.size(), textAt + uint32_t(text.size()));
	Put32(table, table.size(), uint32_t(papxPage / 512));
	size_t fontsAt = table.size();
	Put16(table, table.size(), 2);
	Put16(table, table.size(), 0);
	for (const char* name : {"Times New Roman", "Arial"}) {
		std::vector<uint8_t> ffn(40, 0);
		for (const char* c = name; *c; c++) {
			ffn.push_back(uint8_t(*c));
			ffn.push_back(0);
		}
		ffn.push_back(0);
		ffn.push_back(0);
		ffn[0] = uint8_t(ffn.size() - 1);
		table.insert(table.end(), ffn.begin(), ffn.end());
	}
	Put32(word, 0x1a2, uint32_t(clxAt));
	Put32(word, 0x1a6, clxLength);
	Put32(word, 0xfa, uint32_t(chpxAt));
	Put32(word, 0xfe, 12);
	Put32(word, 0x102, uint32_t(papxAt));
	Put32(word, 0x106, 12);
	Put32(word, 0x112, uint32_t(fontsAt));
	Put32(word, 0x116, uint32_t(table.size() - fontsAt));

	std::vector<uint8_t> file = MakeCompoundFile({{"WordDocument", word}, {"1Table", table}});
	std::vector<WordParagraph> ps;
	std::string error;
	Check(ReadWord(file, ps, error), "read " + error);
	Check(ps.size() == 3, "three paragraphs: " + std::to_string(ps.size()));
	if (ps.size() == 3) {
		std::string first;
		for (const WordRun& r : ps[0].runs)
			first += r.text;
		Check(first == "Hello bold world", "the first paragraph: " + first);
		Check(ps[0].runs.size() == 3 && ps[0].runs[1].text == "bold" && ps[0].runs[1].bold
			&& ps[0].runs[1].italic && !ps[0].runs[0].bold, "bold italic in the middle");
		Check(ps[0].alignment == 1, "centered");
		std::string second;
		for (const WordRun& r : ps[1].runs)
			second += r.text;
		Check(second == "Caf\xc3\xa9 7\x01next", "a field's result, a line break: " + second);
		Check(!ps[1].runs.empty() && ps[1].runs[0].size == 14 && ps[1].runs[0].font == "Arial"
			&& ps[1].runs[0].hasColor && ps[1].runs[0].r == 255 && ps[1].runs[0].g == 0,
			"14 points, Arial, red");
		Check(ps[1].left == 36 && ps[1].first == 54, "indents: " + std::to_string(ps[1].left) + ", "
			+ std::to_string(ps[1].first));
		std::string third;
		for (const WordRun& r : ps[2].runs)
			third += r.text;
		Check(third == "a\tb", "a cell mark as a tab: " + third);
	}
	// opened as a document
	std::string path = "/tmp/doc_word_test.doc";
	FILE* f = fopen(path.c_str(), "wb");
	fwrite(file.data(), 1, file.size(), f);
	fclose(f);
	Document doc;
	Check(ImportWord(path, doc, error), "import " + error);
	const WPPart* wp = doc.WordPart(size_t(doc.MainPart()));
	Check(wp != nullptr && wp->paragraphs.size() == 3 && wp->paragraphs[0].text.compare(0, 16,
		"Hello bold world") == 0, "the text in the document");
	if (wp != nullptr && wp->paragraphs.size() == 3) {
		const WPParagraph& p = wp->paragraphs[0];
		uint32_t style = p.parStyle;
		for (const WPStyleRun& r : p.runs)
			if (r.position <= 7)
				style = r.style;
		Check(ResolveTextStyle(doc.styles, style).faceFlags & 2, "bold in the document");
		Check(ResolveTextStyle(doc.styles, p.parStyle).alignment == 1
			|| ResolveTextStyle(doc.styles, style).alignment == 1, "centered in the document");
	}
	remove(path.c_str());

	// Save As Word of a document with a table and a picture (opened from
	// HTML): a Word table (each cell's paragraph ending in its mark, in
	// the table, each row's end mark with its cells' widths) and an inline
	// PNG picture of the frame's size; read back as such; antiword shows
	// the rows; opened again, a table part and an image part
	{
		std::vector<uint8_t> rgba;
		for (int y = 0; y < 5; y++)
			for (int x = 0; x < 8; x++) {
				rgba.push_back(uint8_t(x * 30));
				rgba.push_back(uint8_t(y * 50));
				rgba.push_back(200);
				rgba.push_back(255);
			}
		std::vector<uint8_t> png = EncodePNG(8, 5, rgba);
		std::string folder = "/tmp";
		std::ofstream("/tmp/doc_word_dot.png", std::ios::binary)
			.write(reinterpret_cast<const char*>(png.data()), std::streamsize(png.size()));
		Document tabled;
		std::string importError;
		Check(ImportHTMLText("<HTML><BODY><P>Before <IMG SRC=\"doc_word_dot.png\"> after</P>"
			"<TABLE BORDER=1><TR><TD WIDTH=100>Name<TD WIDTH=60>Count"
			"<TR><TD>Apples<TD>12</TABLE><P>After</P></BODY></HTML>", tabled, importError, folder),
			"a table and a picture opened " + importError);
		Check(tabled.tableParts.size() == 1 && tabled.imageParts.size() == 1,
			"the HTML's table and picture");
		std::vector<uint8_t> bytes = WordBytes(tabled);
		std::vector<WordParagraph> written;
		std::string readError;
		Check(ReadWord(bytes, written, readError), "the Word file read " + readError);
		std::vector<std::string> cells;
		std::vector<std::vector<float>> widths;
		const WordRun* picture = nullptr;
		std::string after;
		for (const WordParagraph& p : written) {
			std::string text;
			for (const WordRun& r : p.runs) {
				text += r.text;
				if (!r.picture.empty())
					picture = &r;
			}
			if (p.cellEnd && p.inTable)
				cells.push_back(text);
			if (p.rowEnd && p.inTable)
				widths.push_back(p.cellWidths);
			if (!p.inTable)
				after += text + "|";
		}
		Check(cells == std::vector<std::string>{"Name", "Count", "Apples", "12"},
			"four cells, each ending in its mark: " + std::to_string(cells.size()));
		Check(widths.size() == 2 && widths[0].size() == 2 && widths[0][0] > 50
			&& widths[0][1] > 20 && widths[0][0] > widths[0][1], "two rows' end marks, their widths");
		Check(picture != nullptr && picture->picture == png && !picture->jpeg
			&& std::fabs(picture->pictureWidth - 8) < 0.1f
			&& std::fabs(picture->pictureHeight - 5) < 0.1f, "the picture, its PNG and size");
		Check(after.find("Before  after|") != std::string::npos
			&& after.find("After|") != std::string::npos, "the text around them: " + after);
		std::string path = "/tmp/doc_word_table.doc";
		std::ofstream(path, std::ios::binary)
			.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
		// antiword: the rows on lines of their own, their cells between bars
		if (FILE* pipe = popen(("antiword " + path + " 2>&1").c_str(), "r")) {
			std::string out;
			char line[512];
			while (fgets(line, sizeof line, pipe) != nullptr)
				out += line;
			pclose(pipe);
			size_t name = out.find("Name"), apples = out.find("Apples");
			Check(name != std::string::npos && apples != std::string::npos
				&& out.find("Count", name) < out.find('\n', name)
				&& out.find("12", apples) < out.find('\n', apples)
				&& out.find('|', name) < out.find("Count", name), "antiword's rows:\n" + out);
		}
		Document back;
		std::string openError;
		Check(ImportWord(path, back, openError) && back.tableParts.size() == 1
			&& back.imageParts.size() == 1, "opened again: a table and a picture " + openError);
		if (back.tableParts.size() == 1) {
			const TablePart& t = back.tableParts[0].second;
			Check(t.rows.size() == 2 && t.rows[0].cells.size() == 2
				&& std::fabs(t.rows[0].cells[0].size[0] - widths[0][0]) < 0.5f,
				"its rows, cells and widths");
		}
		if (back.imageParts.size() == 1)
			Check(back.imageParts[0].second.image.Width() == 8
				&& back.imageParts[0].second.image.Height() == 5, "its picture's size");
		remove(path.c_str());
		remove("/tmp/doc_word_dot.png");
	}
	printf("doc_word: ok %d, failed %d\n", sOk, sFailed);
	return sFailed != 0 || sOk == 0;
}
