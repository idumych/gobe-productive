// Plain text in and out of word processing documents, as the original does
// it through the Gobe Text Translator (TTextToBlockStream, TBlockStreamToText)
// and TWPImport / TWPExport, with the translator's default settings
// (DefaultPrefs: conversion "Automatic", line type 0, character attributes
// on).
#ifndef GOBE_TEXT_IMPORT_H
#define GOBE_TEXT_IMPORT_H

#include <cstdint>
#include <string>
#include <vector>

#include "Styles.h"

namespace gobe {

struct Document;

// The installed fonts as the translators see them (TFontTable::
// loadAllBeFonts): every family's styles in the font server's order, and
// be_plain_font. The app fills it in; the core has no font server.
struct FontInfo {
	std::string family, style;
	int charset = 0;	// TFontBE: 0x80 when the font has the Japanese blocks
};
struct FontCatalog {
	std::vector<FontInfo> fonts;
	std::string plainFamily, plainStyle;	// be_plain_font

	// the font with that family and style, or -1
	int Find(const std::string& family, const std::string& style) const;
	// TFontTable +0x4: be_plain_font's index (0 when it isn't listed)
	int PlainIndex() const;
	// TTranslatorStyle::GetTextFont's face handling: the family's first
	// style whose name has "Bold" exactly when `bold` and "Italic" exactly
	// when `italic`; "" if there is none
	std::string StyleFor(const std::string& family, bool bold, bool italic) const;
};

// A BeOS text file's "styles" (BTextView::FlattenRunArray) and "alignment"
// attributes, as StyledEdit writes them; the app reads and writes the
// attributes, the core works with this.
struct TextRun {
	int32_t offset = 0;
	std::string family, style;
	float size = 12;
	Color color;
};
struct TextAttributes {
	bool hasAlignment = false;
	int32_t alignment = 0;		// BTextView: 0 left, 1 right, 2 center
	std::vector<TextRun> runs;
};

// The text conversions (UTF8.h) the translators use.
enum {
	kConversionISO1 = 0,		// B_ISO1_CONVERSION
	kConversionMacRoman = 10,	// B_MAC_ROMAN_CONVERSION
	kConversionSJIS = 11,		// B_SJIS_CONVERSION
	kConversionUnicode = 15,	// B_UNICODE_CONVERSION (UCS-2, big-endian)
	kConversionNone = -3		// UTF-8, nothing to convert
};

// DetectConversion (TextTranslator): kConversionNone for UTF-8 (a UTF-8
// BOM, or no invalid sequence before the first CR), kConversionUnicode for
// a big-endian UTF-16 BOM; at the first CR: ISO 8859-1 if a LF follows,
// Mac Roman if not; ISO 8859-1 for anything that isn't valid UTF-8.
int DetectConversion(const std::string& data);
// convert_to_utf8 / convert_from_utf8 for ISO 8859-1, Mac Roman and UCS-2
// (big-endian); characters that don't exist in the target become
// `substitute`. Other conversions go through the app's converter (below);
// without one, bytes >= 0x80 become `substitute`.
std::string ConvertToUTF8(int conversion, const std::string& data, char substitute);
std::string ConvertFromUTF8(int conversion, const std::string& utf8, char substitute);
// The app's convert_to_utf8 / convert_from_utf8 for the other conversions
// (Shift-JIS, ...): returns false if it can't convert.
typedef bool (*TextConverter)(int conversion, bool fromUTF8, const std::string& in,
	std::string& out, char substitute);
void SetTextConverter(TextConverter converter);

// Replaces the text of a blank word processing part: paragraphs end after
// LF, 0x02, FF and 0x05 (TWPPart::InsertStyledText), each styled like the
// part's first paragraph.
struct WPPart;
void SetPartText(WPPart& part, const std::string& text);
// The paragraph and offset of a text offset in the part.
struct TextPosition;
TextPosition PositionAt(const WPPart& part, size_t offset);

// TextTranslator Identify: whether a file whose MIME type is text/* is
// text (FUN_0000de90: UTF-8 or UTF-16 as detected, else more than ten
// times as many other bytes as control characters but tab, LF, FF and CR),
// and how much it looks like a table (FUN_0000df0c: delimiters per line;
// 0.5 and more open as a spreadsheet). `head` is the file's first 32 KB.
bool LooksLikeText(const std::string& head);
float TableLikeness(const std::string& head);

// Imports a text file into `doc` (made with CreateBlankDocument): the
// encoding detected, CR LF and lone CR turned into LF, control characters
// other than tab, LF and FF into spaces (TTextToBlockStream::FilterText),
// one paragraph per line (FF ends a paragraph as a page break). `attributes`
// (may be null) are the file's StyledEdit attributes, used when the text
// is UTF-8: the alignment for every paragraph, the runs' font, size and
// color for their text.
bool ImportText(const std::string& path, Document& doc, std::string& error,
	const TextAttributes* attributes = nullptr);

// The text of word processing part `part` as the original saves it: the
// paragraphs' text, control characters through WPTranslateTable (page,
// column and section breaks become LF, embedded frames and fields '?',
// other controls but tab and the line break 0x01 spaces), UTF-8. When
// `attributes` isn't null it receives the "alignment" and "styles" the
// original writes with the file (TBlockStreamToText::WriteStyleInformation);
// `fonts` (may be null) picks the bold/italic font styles for them.
std::string ExportedText(const Document& doc, size_t part,
	TextAttributes* attributes = nullptr, const FontCatalog* fonts = nullptr);
bool ExportText(const Document& doc, size_t part, const std::string& path,
	std::string& error, TextAttributes* attributes = nullptr,
	const FontCatalog* fonts = nullptr);

} // namespace gobe

#endif
