// rtf_tables: tables and pictures through RTF. A document with text, a
// 2 x 3 table (its columns' widths, a bold word in a cell, a cell of two
// paragraphs) and a picture is saved as RTF: the table is written as
// \trowd ... \cellx ... \intbl ... \cell ... \row, the picture as a PNG
// \pict. Opened again, the table is a table part again (its rows,
// columns, widths, cells' text and the bold word), the picture an image
// part of its size and pixels, the text around them as it was; a
// hand-written Word-style table (\trowd per row, borders) reads too.
// Arguments: none (documents given are ignored).
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>

#include "Document.h"
#include "Editing.h"
#include "HTMLImport.h"
#include "InlineParts.h"
#include "RTF.h"
#include "TablePart.h"

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


static FontCatalog
Fonts()
{
	FontCatalog fonts;
	// (the plain font not the styles' default: a paragraph left unstyled
	// shows)
	fonts.fonts = {{"Swis721 BT", "Roman", 0}, {"Swis721 BT", "Bold", 0},
		{"Dutch801 Rm BT", "Roman", 0}, {"Dutch801 Rm BT", "Bold", 0}};
	fonts.plainFamily = "Swis721 BT";
	fonts.plainStyle = "Roman";
	return fonts;
}


// the main text's tables and pictures: their parts, in order
static std::vector<size_t>
Framed(const Document& doc, uint32_t kind)
{
	std::vector<size_t> parts;
	const WPPart* wp = doc.WordPart(size_t(doc.MainPart()));
	for (const WPParagraph& p : wp->paragraphs)
		for (const WPMiscInfo& m : p.misc)
			if (m.kind == Sig("FRAM")) {
				const Frame* f = FrameAt(doc, m.frame);
				if (f != nullptr && doc.parts.parts[f->part].kind == kind)
					parts.push_back(f->part);
			}
	return parts;
}


// cell (r, c)'s text, its paragraph breaks as '|'
static std::string
CellText(const Document& doc, size_t table, size_t r, size_t c)
{
	const TablePart* t = doc.Table(table);
	const WPPart* text = t != nullptr ? doc.WordPart(t->textPart) : nullptr;
	if (text == nullptr || r >= t->rows.size() || c >= t->rows[r].cells.size())
		return "?";
	const Frame* f = FrameAt(doc, t->rows[r].cells[c].frame);
	std::string all;
	for (const WPParagraph& p : text->paragraphs)
		all += p.text;
	if (f == nullptr || f->textStart > all.size())
		return "?";
	std::string s = all.substr(f->textStart, f->textLength);
	while (!s.empty() && (s.back() == '\n' || s.back() == 0x05))
		s.pop_back();
	for (char& ch : s)
		if (ch == '\n')
			ch = '|';
	return s;
}


static std::string
MainText(const Document& doc)
{
	std::string all;
	for (const WPParagraph& p : doc.WordPart(size_t(doc.MainPart()))->paragraphs)
		all += p.text;
	return all;
}


int
main()
{
	FontCatalog fonts = Fonts();
	Document doc;
	std::string error;
	Check(ImportHTMLText("<HTML><BODY><P>Before</P><TABLE BORDER=1>"
		"<TR><TD WIDTH=100>Name<TD WIDTH=60>Count<TD WIDTH=80>Note"
		"<TR><TD>Apples<TD><B>12</B><TD><P>red<P>sweet</TABLE><P>After</P></BODY></HTML>",
		doc, error), "the document made " + error);
	// a 3 x 2 picture after "After"
	ArchiveBitmap bitmap;
	bitmap.colorSpace = 0x2008;
	bitmap.bytesPerRow = 12;
	bitmap.bounds[2] = 2;
	bitmap.bounds[3] = 1;
	for (int i = 0; i < 6; i++) {
		uint8_t pixel[4] = {uint8_t(i * 40), uint8_t(255 - i * 30), uint8_t(i * 7), 255};
		bitmap.bits.insert(bitmap.bits.end(), pixel, pixel + 4);
	}
	size_t main = size_t(doc.MainPart());
	int picture = AddImagePart(doc, main, bitmap, error);
	Check(picture >= 0, "a picture " + error);
	const WPPart* wp = doc.WordPart(main);
	size_t last = wp->paragraphs.size() - 1;
	while (last > 0 && wp->paragraphs[last].text.find("After") == std::string::npos)
		last--;
	InsertInlineFrame(doc, main, {last, 5}, size_t(picture));
	std::vector<size_t> tables = Framed(doc, Sig("TBLE"));
	Check(tables.size() == 1 && Framed(doc, Sig("BITS")).size() == 1, "a table and a picture");
	if (tables.empty())
		return 1;

	// saved
	std::string path = "/tmp/rtf_tables_test.rtf";
	Check(ExportRTF(doc, main, path, error, fonts), "saved as RTF " + error);
	std::string rtf = ExportedRTF(doc, main, fonts);
	Check(rtf.find("\\trowd") != std::string::npos && rtf.find("\\cellx2000") != std::string::npos
		&& rtf.find("\\cellx3200") != std::string::npos && rtf.find("\\intbl") != std::string::npos
		&& rtf.find("Count\\cell") != std::string::npos && rtf.find("\\row") != std::string::npos
		&& rtf.find("\\clbrdrt\\brdrs") != std::string::npos, "the table written as an RTF table");
	Check(rtf.find("\\pict\\pngblip") != std::string::npos, "the picture written as PNG");

	// opened again
	Document back;
	Check(ImportRTF(path, back, error, fonts), "opened again " + error);
	std::vector<size_t> backTables = Framed(back, Sig("TBLE"));
	std::vector<size_t> backPictures = Framed(back, Sig("BITS"));
	Check(backTables.size() == 1, "a table read back");
	Check(backPictures.size() == 1, "a picture read back");
	if (backTables.size() == 1) {
		const TablePart* t = back.Table(backTables[0]);
		Check(t != nullptr && t->rows.size() == 2 && t->rows[0].cells.size() == 3,
			"its rows and columns");
		if (t != nullptr && t->rows.size() == 2 && t->rows[0].cells.size() == 3) {
			const TablePart* before = doc.Table(tables[0]);
			bool widths = true;
			for (size_t c = 0; c < 3; c++)
				widths = widths && std::fabs(t->rows[0].cells[c].size[0]
					- before->rows[0].cells[c].size[0]) < 0.5f;
			Check(widths, "its columns' widths");
			Check(CellText(back, backTables[0], 0, 0) == "Name"
				&& CellText(back, backTables[0], 0, 2) == "Note"
				&& CellText(back, backTables[0], 1, 1) == "12"
				&& CellText(back, backTables[0], 1, 2) == "red|sweet",
				"its cells' text: " + CellText(back, backTables[0], 1, 2));
			// the bold 12
			const WPPart* text = back.WordPart(t->textPart);
			const Frame* f = FrameAt(back, t->rows[1].cells[1].frame);
			bool bold = false;
			size_t at = 0;
			for (const WPParagraph& p : text->paragraphs) {
				if (f != nullptr && at <= f->textStart && f->textStart < at + p.text.size())
					for (const WPStyleRun& r : p.runs)
						bold |= (ResolveTextStyle(back.styles, r.style).faceFlags & 2) != 0;
				at += p.text.size();
			}
			Check(bold, "the bold word");
			// the last paragraph of a cell styled as the rest ("sweet" as
			// "red")
			const Frame* note = FrameAt(back, t->rows[1].cells[2].frame);
			uint32_t styles[2] = {0xffffffff, 0xfffffffe};
			size_t k = 0;
			at = 0;
			for (const WPParagraph& p : text->paragraphs) {
				if (note != nullptr && note->textStart <= at && at < note->textStart
						+ note->textLength && k < 2 && !p.runs.empty())
					styles[k++] = p.runs[0].style;
				at += p.text.size();
			}
			Check(k == 2 && ResolveTextStyle(back.styles, styles[0]).family
				== ResolveTextStyle(back.styles, styles[1]).family
				&& ResolveTextStyle(back.styles, styles[1]).family == "Swis721 BT",
				"a cell's paragraphs styled alike");
		}
	}
	if (backPictures.size() == 1) {
		const IMPart* im = back.ImagePart(backPictures[0]);
		bool same = im != nullptr && im->image.Width() == 3 && im->image.Height() == 2;
		for (int y = 0; same && y < 2; y++)
			for (int x = 0; x < 3; x++)
				for (int k = 0; k < 4; k++)
					same = same && im->image.bits[y * im->image.bytesPerRow + x * 4 + k]
						== bitmap.bits[y * 12 + x * 4 + k];
		Check(same, "the picture's size and pixels");
	}
	std::string before = MainText(back);
	Check(before.compare(0, 7, "Before\n") == 0 && before.find("After") != std::string::npos
		&& before.find("Name") == std::string::npos, "the text around them: "
		+ before.substr(0, 40));

	// a table as Word writes it: borders, \trowd per row, \pard after
	{
		std::string word = "{\\rtf1\\ansi{\\fonttbl{\\f0 Times;}}\\pard Top\\par\n"
			"\\trowd\\trgaph108\\clbrdrt\\brdrs\\cellx1440\\clbrdrt\\brdrs\\cellx4320\n"
			"\\pard\\intbl A1\\cell B1\\cell\\row\n"
			"\\trowd\\trgaph108\\cellx1440\\cellx4320\n"
			"\\pard\\intbl A2\\cell B2\\cell\\row\n"
			"\\pard Bottom\\par}";
		std::ofstream("/tmp/rtf_tables_word.rtf") << word;
		Document w;
		Check(ImportRTF("/tmp/rtf_tables_word.rtf", w, error, fonts), "Word's table " + error);
		std::vector<size_t> wt = Framed(w, Sig("TBLE"));
		const TablePart* t = wt.size() == 1 ? w.Table(wt[0]) : nullptr;
		Check(t != nullptr && t->rows.size() == 2 && t->rows[1].cells.size() == 2
			&& std::fabs(t->rows[0].cells[1].size[0] - 144) < 0.5f
			&& CellText(w, wt[0], 1, 1) == "B2", "Word's table read");
		std::string text = MainText(w);
		Check(text.compare(0, 4, "Top\n") == 0 && text.find("Bottom") != std::string::npos
			&& text.find("A1") == std::string::npos, "the text around Word's table");
	}

	printf("rtf_tables: ok %d, failed %d\n", sOk, sFailed);
	return sFailed ? 1 : 0;
}
