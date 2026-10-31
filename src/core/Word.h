// Word 97 documents (Word 8.0, an OLE compound file) opened as word
// processing documents, as the Word translator reads them (WORDTranslator/
// TImportWORD.c, TCHPXTbl, TPAPXTbl, TFKPTbl, TFontTblWORD).
#ifndef GOBE_WORD_H
#define GOBE_WORD_H

#include <cstdint>
#include <string>
#include <vector>

namespace gobe {

struct Document;

// a run of a paragraph's text with its character properties (the CHPX's
// sprms; "set" flags for those the run has)
struct WordRun {
	std::string text;			// UTF-8; 0x01 a line break
	bool bold = false, italic = false, underline = false, strike = false;
	bool superscript = false, subscript = false;
	float size = 0;				// points (0: the default)
	std::string font;			// "" for the default
	bool hasColor = false;
	uint8_t r = 0, g = 0, b = 0;
	// an inline picture (the 0x01 character with sprmCFSpec and
	// sprmCPicLocation: a PICF and its OfficeArt BLIP in the Data stream)
	// instead of text: the PNG (or, read, a JPEG: `jpeg`) and its size in
	// points
	std::vector<uint8_t> picture;
	bool jpeg = false;
	float pictureWidth = 0, pictureHeight = 0;
};

struct WordParagraph {
	std::vector<WordRun> runs;
	int alignment = 0;			// 0 left, 1 center, 2 right, 3 justify
	float left = 0, first = 0, right = 0;	// indents in points (first: absolute)
	float before = 0, after = 0;			// spacing in points
	bool pageBreak = false;		// ends with a page or section break
	// in a table (sprmPFInTable): the paragraph ends a cell (its mark
	// 0x07), or is a row's end mark (sprmPFTtp, the mark alone) with the
	// row's cells' widths in points and their borders (sprmTDefTable)
	bool inTable = false;
	bool cellEnd = false;
	bool rowEnd = false;
	std::vector<float> cellWidths;
	float border = 0;			// the cells' borders' width in points (0: none)
};

// TImportWORD: the FIB, the piece table (CLX) for the main text, the
// CHPX and PAPX FKPs for its character and paragraph properties, the
// font table (SttbfFfn). Paragraphs end at 0x0D (and at page and section
// breaks); 0x0B is a line break; fields keep their result; footnote marks
// don't come through as text. In a table (sprmPFInTable) a cell's last
// paragraph ends at its 0x07 mark (cellEnd) and a row's end mark is a
// paragraph of its own (rowEnd, the widths from sprmTDefTable); a cell
// mark outside a table is a tab. Inline pictures (PNG or JPEG BLIPs in
// the Data stream) are runs of their own.
bool ReadWord(const std::vector<uint8_t>& file, std::vector<WordParagraph>& paragraphs,
	std::string& error);

// Opens a Word 97 file as a new word processing document.
bool ImportWord(const std::string& path, Document& doc, std::string& error);

// The paragraphs of a word processing part as TExportWORD writes them:
// each run's style resolved into a WordRun (every run with its size, font
// and color), each paragraph's style into its WordParagraph; a line break
// 0x01, fields as their text, a picture (an image part's frame) a run of
// its own, a table's frame its cells' paragraphs and its rows' end marks
// (the paragraph split around it; a table in a cell as its text),
// separator lines and other frames left out.
std::vector<WordParagraph> WordParagraphsOf(const Document& doc, size_t part);

// A Word 97 file (TExportWORD): an OLE compound file with WordDocument
// and 1Table streams: the FIB (nFib 0xc1), the text at 0x400 as one
// piece (Windows-1252 when every character has a byte there, else
// UTF-16), the CHPX and PAPX FKPs and their bin tables, a style sheet
// with Normal, one section, the font table (BeOS's Dutch801 and Swis721
// as Times New Roman and Arial, as TFontTblWORD has them), the DOP;
// paragraphs end in 0x0D (0x0C after a page break), line breaks are 0x0B;
// table cells and rows end in 0x07 with sprmPFInTable (a row's end mark
// also sprmPFTtp and sprmTDefTable); a picture is 0x01 with sprmCFSpec and
// sprmCPicLocation, its PICF (MM_SHAPE) and an OfficeArt shape with its
// BSE and PNG BLIP in a Data stream.
std::vector<uint8_t> WordFileBytes(const std::vector<WordParagraph>& paragraphs);

// a table frame's cells as text (`pointer` a FrameRef's): a line break
// (0x01) before each row, tabs between the cells, a cell's line ends as
// spaces; empty for a frame of another part. (Save As RTF writes tables'
// text from it, Save As Word a table in a cell: the original writes RTF
// pictures of tables.)
std::string TableText(const Document& doc, const uint32_t pointer[3]);

// Save As "Microsoft Word": the document's main word processing part.
std::vector<uint8_t> WordBytes(const Document& doc);
bool ExportWord(const Document& doc, const std::string& path, std::string& error);

} // namespace gobe

#endif
