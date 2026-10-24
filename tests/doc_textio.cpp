// doc_textio: plain text and RTF import/export (TextImport.h, RTF.h), the
// SheetMgr reader, InsertSpecialChar. Arguments: the sample documents.
//
// Expected values were worked out by hand from the original's code
// (decomp/TextTranslator/TTextToBlockStream.c, _global.c DetectConversion,
// decomp/RTFTranslator/TImportRTF.c, TExportRTF.c) or come from python3
// (commands in the comments); WPTranslateTable is the 32 bytes at
// 0x300780 in the original's binary.
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "DocArchive.h"
#include "Document.h"
#include "Editing.h"
#include "RTF.h"
#include "Styles.h"
#include "TextImport.h"
#include "WPPart.h"

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


static std::string
Printable(const std::string& s)
{
	std::string out;
	for (unsigned char c : s) {
		if (c == '\n')
			out += "\\n";
		else if (c < 0x20 || c >= 0x7f) {
			char b[8];
			snprintf(b, sizeof b, "\\x%02x", c);
			out += b;
		} else
			out += char(c);
	}
	return out;
}


static std::vector<std::string>
Paragraphs(const Document& doc)
{
	std::vector<std::string> out;
	if (doc.wordParts.empty())
		return out;
	for (const WPParagraph& p : doc.wordParts[0].second.paragraphs)
		out.push_back(p.text);
	return out;
}


static void
CheckParagraphs(const Document& doc, const std::vector<std::string>& expected,
	const std::string& what)
{
	std::vector<std::string> got = Paragraphs(doc);
	bool same = got == expected;
	if (!same) {
		std::string g, e;
		for (const std::string& s : got)
			g += "[" + Printable(s) + "]";
		for (const std::string& s : expected)
			e += "[" + Printable(s) + "]";
		Check(false, what + ": paragraphs " + g + " expected " + e);
	} else
		Check(true, what);
}


static TextStyle
StyleAt(const Document& doc, size_t paragraph, size_t offset)
{
	const WPParagraph& p = doc.wordParts[0].second.paragraphs[paragraph];
	uint32_t style = p.parStyle;
	for (const WPStyleRun& r : p.runs)
		if (r.position <= offset)
			style = r.style;
	return ResolveTextStyle(doc.styles, style);
}


static TextStyle
ParagraphStyle(const Document& doc, size_t paragraph)
{
	return ResolveTextStyle(doc.styles, doc.wordParts[0].second.paragraphs[paragraph].parStyle);
}


static FontCatalog
TestFonts()
{
	FontCatalog fonts;
	fonts.fonts = {
		{"Dutch801 Rm BT", "Roman", 0}, {"Dutch801 Rm BT", "Bold", 0},
		{"Swis721 BT", "Roman", 0}, {"Swis721 BT", "Bold", 0}, {"Swis721 BT", "Italic", 0},
		{"Courier10 BT", "Roman", 0},
	};
	fonts.plainFamily = "Dutch801 Rm BT";
	fonts.plainStyle = "Roman";
	return fonts;
}


static const Color kBlack = {0, 0, 0, 255}, kRed = {255, 0, 0, 255}, kBlue = {0, 0, 255, 255};


static void
TestTextImport()
{
	Document doc;
	std::string error;

	// UTF-8, LF; FF ends a paragraph and stays
	Check(ImportText("tests/fixtures/lines.txt", doc, error), "import lines.txt " + error);
	CheckParagraphs(doc, {"First line\n", "Second\twith tab\n", "Caf\xc3\xa9 cr\xc3\xa8me\n", "\n",
		"After empty\f", "Page two"}, "lines.txt paragraphs");
	// FF goes out as LF (WPTranslateTable[12] = 0x0a)
	Check(ExportedText(doc, doc.wordParts[0].first)
		== "First line\nSecond\twith tab\nCaf\xc3\xa9 cr\xc3\xa8me\n\nAfter empty\nPage two",
		"lines.txt exported");

	// a CR LF file is ISO 8859-1 (DetectConversion returns 0 at the first CR)
	Check(ImportText("tests/fixtures/latin1_crlf.txt", doc, error), "import latin1_crlf.txt");
	CheckParagraphs(doc, {"Caf\xc3\xa9\n", "line2\n", ""}, "latin1_crlf.txt paragraphs");

	// ... even when it's UTF-8: python3 -c "print('Café'.encode().decode('latin-1').encode())"
	Check(ImportText("tests/fixtures/utf8_crlf.txt", doc, error), "import utf8_crlf.txt");
	CheckParagraphs(doc, {"Caf\xc3\x83\xc2\xa9\n", "x"}, "utf8_crlf.txt paragraphs");

	// a lone CR: Mac Roman (0x8e = é: python3 -c "print(b'\x8e'.decode('mac_roman'))")
	Check(ImportText("tests/fixtures/mac.txt", doc, error), "import mac.txt");
	CheckParagraphs(doc, {"a\n", "b\xc3\xa9"}, "mac.txt paragraphs");

	// control characters but tab, LF and FF become spaces; 0x7f stays
	Check(ImportText("tests/fixtures/controls.txt", doc, error), "import controls.txt");
	CheckParagraphs(doc, {"a b c\x7f" "d e"}, "controls.txt paragraphs");

	// a big-endian UTF-16 BOM: UCS-2, the BOM stays as U+FEFF
	Check(ImportText("tests/fixtures/utf16be.txt", doc, error), "import utf16be.txt");
	CheckParagraphs(doc, {"\xef\xbb\xbfHi\xe2\x82\xac"}, "utf16be.txt paragraphs");

	// StyledEdit attributes on a UTF-8 file: the alignment (BTextView
	// center 2 -> Gobe 1) for every paragraph, the runs' font, size, color
	TextAttributes attributes;
	attributes.hasAlignment = true;
	attributes.alignment = 2;
	attributes.runs = {{0, "Swis721 BT", "Bold", 14, kRed}, {6, "Dutch801 Rm BT", "Roman", 10, kBlue}};
	Check(ImportText("tests/fixtures/lines.txt", doc, error, &attributes), "import with attributes");
	bool centered = true;
	for (size_t i = 0; i < Paragraphs(doc).size(); i++)
		centered = centered && ParagraphStyle(doc, i).alignment == 1;
	Check(centered, "attributes: every paragraph centered");
	TextStyle t = StyleAt(doc, 0, 0);
	Check(t.family == "Swis721 BT" && t.face == "Bold" && t.size == 14 && t.color == kRed,
		"attributes: first run");
	t = StyleAt(doc, 0, 6);
	Check(t.family == "Dutch801 Rm BT" && t.face == "Roman" && t.size == 10 && t.color == kBlue,
		"attributes: second run");
	t = StyleAt(doc, 4, 3);
	Check(t.size == 10 && t.color == kBlue, "attributes: second run to the end");

	// written back: the alignment and the two runs (one style for the rest)
	TextAttributes out;
	FontCatalog fonts = TestFonts();
	ExportedText(doc, doc.wordParts[0].first, &out, &fonts);
	Check(out.hasAlignment && out.alignment == 2, "attributes out: alignment");
	Check(out.runs.size() == 2, "attributes out: 2 runs, got " + std::to_string(out.runs.size()));
	if (out.runs.size() == 2) {
		Check(out.runs[0].offset == 0 && out.runs[0].family == "Swis721 BT"
			&& out.runs[0].style == "Bold" && out.runs[0].size == 14 && out.runs[0].color == kRed,
			"attributes out: first run");
		Check(out.runs[1].offset == 6 && out.runs[1].family == "Dutch801 Rm BT"
			&& out.runs[1].style == "Roman" && out.runs[1].size == 10
			&& out.runs[1].color == kBlue, "attributes out: second run");
	}

	// the attributes don't count when the text isn't UTF-8
	Check(ImportText("tests/fixtures/latin1_crlf.txt", doc, error, &attributes),
		"import latin1 with attributes");
	Check(ParagraphStyle(doc, 0).alignment == 0 && StyleAt(doc, 0, 0).color != kRed,
		"attributes ignored for ISO 8859-1");
}


static void
TestRTFImport()
{
	FontCatalog fonts = TestFonts();
	Document doc;
	std::string error, warning;
	Check(ImportRTF("tests/fixtures/styles.rtf", doc, error, fonts, &warning),
		"import styles.rtf " + error);
	Check(warning.empty(), "styles.rtf: no warning");
	// the header, \info and {\*\unknown} groups, the field's instruction
	// and result aren't text; \page breaks the paragraph; the PAGE field
	// adds nothing (no page number fields yet)
	CheckParagraphs(doc, {
		"This is bold text and italic text in a larger font.\n",
		"This paragraph is centered.\n",
		"Red colored text here.\n",
		"Right Dutch and  under\n",
		"Caf\xc3\xa9 \xe2\x82\xac {x}\\ page  end\f",
		"After break\n",
		""}, "styles.rtf paragraphs");
	if (Paragraphs(doc).size() != 7)
		return;

	// \f0 "Helvetica" \fswiss: the swiss family's default font; \f1
	// "Courier" \fnil: be_plain_font; \f2 by its name
	TextStyle t = StyleAt(doc, 0, 0);
	Check(t.family == "Swis721 BT" && t.face == "Roman" && t.size == 12 && t.faceFlags == 1
		&& t.color == kBlack, "rtf: plain text");
	Check(StyleAt(doc, 0, 8).faceFlags == 2, "rtf: bold");
	Check(StyleAt(doc, 0, 13).faceFlags == 1, "rtf: \\b0");
	Check(StyleAt(doc, 0, 22).faceFlags == 4, "rtf: italic");
	t = StyleAt(doc, 0, 37);
	Check(t.family == "Dutch801 Rm BT" && t.face == "Roman" && t.size == 18, "rtf: \\f1\\fs36");
	t = StyleAt(doc, 2, 0);
	Check(t.color == kRed && t.size == 12 && t.family == "Dutch801 Rm BT", "rtf: \\cf1");
	Check(StyleAt(doc, 2, 17).color == kBlack, "rtf: \\cf0");
	t = StyleAt(doc, 3, 6);
	Check(t.family == "Dutch801 Rm BT" && t.face == "Bold", "rtf: font by name");
	Check(StyleAt(doc, 3, 11).family == "Swis721 BT", "rtf: \\f0 again");
	Check(StyleAt(doc, 3, 17).faceFlags == 8, "rtf: \\ul");

	Check(ParagraphStyle(doc, 0).alignment == 0, "rtf: \\ql");
	Check(ParagraphStyle(doc, 1).alignment == 1, "rtf: \\qc is 1");
	t = ParagraphStyle(doc, 3);
	// \fi-360 comes out as 360 (the sample reader's double negation):
	// first line (720 - 360) / 20 = 18
	Check(t.alignment == 2 && t.leftIndent == 36 && t.rightIndent == 72 && t.firstIndent == 18,
		"rtf: \\qr\\li720\\fi-360\\ri1440 (got align " + std::to_string(t.alignment) + " left "
		+ std::to_string(t.leftIndent) + " right " + std::to_string(t.rightIndent)
		+ " first " + std::to_string(t.firstIndent) + ")");

	// unmatched braces: imported, with the original's warning
	warning.clear();
	Check(ImportRTF("tests/fixtures/unmatched.rtf", doc, error, fonts, &warning),
		"import unmatched.rtf");
	Check(warning.find("unmatched braces") != std::string::npos, "unmatched.rtf: warning");
	CheckParagraphs(doc, {"Hello bold"}, "unmatched.rtf paragraphs");
	Check(StyleAt(doc, 0, 6).faceFlags == 2 && StyleAt(doc, 0, 0).faceFlags == 1,
		"unmatched.rtf: bold");

	// \mac: \' bytes are Mac Roman
	Check(ImportRTF("tests/fixtures/mac.rtf", doc, error, fonts), "import mac.rtf");
	CheckParagraphs(doc, {"Caf\xc3\xa9\n", ""}, "mac.rtf paragraphs");
}


static std::string
ExpectedHeader()
{
	// TExportRTF::WriteHeader, TFontTableRTF, TColorTable::WriteColorTable
	return "{\\rtf1\\ansi\\deflang1033\\deflangfe1041\\deff0\n"
		"{\\fonttbl{\\f0\\froman \\fcharset0 Dutch801 Rm BT Roman;}"
		"{\\f1\\froman \\fcharset0 Dutch801 Rm BT Bold;}"
		"{\\f2\\fswiss \\fcharset0 Swis721 BT Roman;}"
		"{\\f3\\fswiss \\fcharset0 Swis721 BT Bold;}"
		"{\\f4\\fswiss \\fcharset0 Swis721 BT Italic;}"
		"{\\f5\\fnil \\fcharset0 Courier10 BT Roman;}}\n"
		"{\\colortbl;\\red0\\green0\\blue0;\\red0\\green0\\blue255;\\red0\\green255\\blue255;"
		"\\red0\\green255\\blue0;\\red255\\green0\\blue255;\\red255\\green0\\blue0;"
		"\\red255\\green255\\blue0;\\red255\\green255\\blue255;\\red0\\green0\\blue128;"
		"\\red0\\green128\\blue128;\\red0\\green128\\blue0;\\red128\\green0\\blue128;"
		"\\red128\\green0\\blue0;\\red128\\green128\\blue0;\\red128\\green128\\blue128;"
		"\\red192\\green192\\blue192;}\n"
		"\\paperw12240\\paperh15840\\margt1440\\margb1440\\margl1440\\margr1440"
		"\\widowctrl\\ftnbj\\ftnrestart\\ftnstart1\\pgnstart1"
		"\\deftab720\\linemod0\\linex0\\cols1\\colsx0\\sectd\\pard\\plain\\uc1\n";
}


static void
TestRTFExport()
{
	FontCatalog fonts = TestFonts();
	Document blank;
	std::string error;
	CreateBlankDocument(blank, error);
	Check(ExportedRTF(blank, blank.wordParts[0].first, fonts) == ExpectedHeader() + "}",
		"rtf export: blank document");

	Document doc;
	ImportRTF("tests/fixtures/styles.rtf", doc, error, fonts);
	std::string rtf = ExportedRTF(doc, doc.wordParts[0].first, fonts);
	Check(rtf.compare(0, ExpectedHeader().size(), ExpectedHeader()) == 0, "rtf export: header");
	// own paragraph and character properties, runs as groups
	const char* fragments[] = {
		"\\pard\\ql\\li0\\fi0\\ri0{\\plain\\f2\\fs24\\cf0 This is }{\\plain\\f3\\fs24\\b\\cf0 bold }",
		"{\\plain\\f4\\fs24\\i\\cf0 italic }",
		"{\\plain\\f0\\fs36\\cf0 a larger font.\\par}\n",
		"\\pard\\qc\\li0\\fi0\\ri0{\\plain\\f0\\fs24\\cf0 This paragraph is centered.\\par}\n",
		"{\\plain\\f0\\fs24\\cf6 Red colored text }",
		// "Dutch801 Rm BT Bold" with a plain face: GetTextFont drops the
		// font style for a plain face, so the family's first font, \f0
		"\\pard\\qr\\li720\\fi-360\\ri1440{\\plain\\f0\\fs24\\cf0 Right }{\\plain\\f0\\fs24\\cf0 Dutch}",
		"{\\plain\\f2\\fs24\\ul\\cf0 under\\par}\n",
		// é as \'e9; € as \u with its UCS-2 bytes read little-endian:
		// python3 -c "import struct;print(struct.unpack('<h',bytes([0x20,0xac]))[0])"
		// (the page break is a special character: a run of its own)
		"\\pard\\ql{\\plain Caf\\'e9 \\u-21472? \\{x\\}\\\\ page  end}{\\plain \\par}\n",
		"After break\\par}\n}",
	};
	for (const char* f : fragments)
		Check(rtf.find(f) != std::string::npos, std::string("rtf export has ") + Printable(f));
	if (sFailed > 0 && getenv("TEXTIO_DUMP"))
		printf("%s\n", rtf.c_str());

	// read back: the same text and styles, but the page break is a
	// paragraph end and the \u value is the swapped one (U+53E0)
	std::string path = "/tmp/doc_textio_export.rtf";
	Check(ExportRTF(doc, doc.wordParts[0].first, path, error, fonts), "ExportRTF " + error);
	Document back;
	Check(ImportRTF(path, back, error, fonts), "re-import " + error);
	std::vector<std::string> expected = Paragraphs(doc);
	if (expected.size() == 7) {
		expected[4] = "Caf\xc3\xa9 \xe5\x8f\xa0 {x}\\ page  end\n";
		CheckParagraphs(back, expected, "rtf round trip paragraphs");
	}
	if (Paragraphs(back) == expected) {
		bool same = true;
		std::string where;
		for (size_t p = 0; p < 4; p++) {
			TextStyle a = ParagraphStyle(doc, p), b = ParagraphStyle(back, p);
			if (a.alignment != b.alignment || a.leftIndent != b.leftIndent
					|| a.firstIndent != b.firstIndent || a.rightIndent != b.rightIndent)
				same = false, where += " paragraph " + std::to_string(p);
			for (size_t o = 0; o + 1 < expected[p].size(); o++) {
				TextStyle x = StyleAt(doc, p, o), y = StyleAt(back, p, o);
				// "Dutch" comes back in the family's first font (above); bold
				// and italic text in the family's bold or italic font
				if (p == 3 && o >= 6 && o < 11)
					x.face = "Roman";
				if ((x.faceFlags & 6) != 0)
					x.face = y.face;
				if (x.family != y.family || x.face != y.face || x.size != y.size
						|| x.faceFlags != y.faceFlags || x.color != y.color) {
					same = false;
					where += " " + std::to_string(p) + ":" + std::to_string(o);
					break;
				}
			}
		}
		Check(same, "rtf round trip styles" + where);
	}
}


static void
TestSamples(int argc, char** argv)
{
	// WPTranslateTable: 20 01 0a 0a 3f 20 20 20 20 09 0a 0a 0a 20 ... 20
	auto translate = [](unsigned char c) -> char {
		if (c >= 0x20)
			return char(c);
		static const char table[13] = {' ', 1, '\n', '\n', '?', ' ', ' ', ' ', ' ', '\t',
			'\n', '\n', '\n'};
		return c < 13 ? table[c] : ' ';
	};
	int exported = 0, sheets = 0;
	for (int i = 1; i < argc; i++) {
		Document doc;
		std::string error;
		if (!LoadDocument(argv[i], doc, error))
			continue;
		// SheetMgr: read and written back byte for byte
		for (size_t s = 0; s < doc.file.toc.size(); s++) {
			if (doc.file.toc[s].name != "SheetMgr")
				continue;
			size_t start = doc.file.toc[s].offset, end = doc.file.tocOffset;
			for (const TocEntry& e : doc.file.toc)
				if (e.offset > start && e.offset < end)
					end = e.offset;
			std::vector<uint8_t> written = WriteSheetMgr(doc.sheets);
			bool same = written.size() <= end - start
				&& std::equal(written.begin(), written.end(), doc.bytes.begin() + start);
			Check(same && !doc.sheets.sheets.empty(), std::string("SheetMgr round trip ") + argv[i]);
			sheets++;
		}
		int main = doc.MainPart();
		Check(main >= 0, std::string("main part ") + argv[i]);
		const WPPart* wp = main >= 0 ? doc.WordPart(size_t(main)) : nullptr;
		if (wp == nullptr) {
			Check(ExportedText(doc, size_t(std::max(main, 0))).empty(),
				std::string("no text from a non-text sheet ") + argv[i]);
			continue;
		}
		std::string expected;
		for (const WPParagraph& p : wp->paragraphs)
			for (unsigned char c : p.text)
				expected += translate(c);
		Check(ExportedText(doc, size_t(main)) == expected, std::string("text export ") + argv[i]);
		exported++;
		if (strstr(argv[i], "/Resume"))
			Check(expected.compare(0, 10, "Objective\n") == 0, "Resume starts with Objective");
	}
	Check(exported >= 3, "sample text exports: " + std::to_string(exported));
	Check(sheets == argc - 1, "SheetMgr in every document: " + std::to_string(sheets));

	// the blank document's sheet shows its WORD part
	Document blank;
	std::string error;
	CreateBlankDocument(blank, error);
	Check(blank.MainPart() >= 0 && blank.WordPart(size_t(blank.MainPart())) != nullptr,
		"blank document: main part is the WORD part");
}


static void
TestSpecialChars()
{
	Document doc;
	std::string error;
	CreateBlankDocument(doc, error);
	WPPart* wp = doc.EditableWordPart(doc.wordParts[0].first);
	InsertText(*wp, {0, 0}, "abcd");
	TextPosition at = InsertSpecialChar(*wp, {0, 2}, 0x0c);
	CheckParagraphs(doc, {"ab\f", "cd"}, "InsertSpecialChar page break");
	Check(at.paragraph == 1 && at.offset == 0, "InsertSpecialChar returns the next paragraph");
	InsertSpecialChar(*wp, {1, 1}, 0x01);
	CheckParagraphs(doc, {"ab\f", "c\x01" "d"}, "InsertSpecialChar line break");
	Check(doc.wordParts[0].second.paragraphs[1].runs.size() == 1,
		"split paragraph keeps a run");
}


int
main(int argc, char** argv)
{
	TestTextImport();
	TestRTFImport();
	TestRTFExport();
	TestSamples(argc, argv);
	TestSpecialChars();
	printf("textio checks ok %d, failed %d\n", sOk, sFailed);
	return sFailed > 0 ? 1 : 0;
}
