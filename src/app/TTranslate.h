// The app's side of the Text and RTF translators: the installed fonts,
// the text encodings, a text file's StyledEdit attributes, what kind of
// file is being opened (TApp::RefsReceived, the translators' Identify) and
// the Save panel's "File Format" popup (SavePanel).
#ifndef GOBE_TTRANSLATE_H
#define GOBE_TTRANSLATE_H

#include <string>

#include <String.h>

#include "TextImport.h"

class BFilePanel;
class BMenuField;

namespace gobe {

struct Document;

// TFontTable::loadAllBeFonts: every installed font, be_plain_font
const FontCatalog& InstalledFonts();

// convert_to_utf8 / convert_from_utf8 for the core (SetTextConverter)
void InstallTextConverter();

enum FileKind {
	kGobeDocument,	// IsGobeDoc / the document MIME type
	kRTFFile,		// RTFTranslator Identify: starts with "{\rtf"
	kTextFile,		// TextTranslator Identify: a text/* file that is text
	kTableFile,		// ... that looks like a table (a spreadsheet import)
	kSylkFile,		// SYLKTranslator: text/sylk, or text starting with an ID record
	kHTMLFile,		// HTMLTranslator: text/html, or text with an <HTML> tag
	kExcelFile,		// ExcelTranslator: an OLE compound file with a Workbook or Book
	kWordFile,		// WORDTranslator: an OLE compound file with a WordDocument stream
	kUnknownFile
};
FileKind IdentifyFile(const char* path);

// Opens a file the translators read as a new untitled word processing
// document (a spreadsheet for SYLK) named "<file name> (converted)" (TImportTranslation::
// PrepareDocument). `warning` receives a message to show, if any.
bool ImportFile(const char* path, FileKind kind, Document& doc, BString& error,
	BString& warning);

// The formats Save As offers (SavePanel::SetFormats): the document's own
// format first, then the translators' formats for the document's main
// part, by name.
enum SaveFormat {
	kSaveGobe,
	kSavePlainText,		// TextTranslator 'TEXT', text/plain
	kSaveRTF,			// RTFTranslator 'RTF ', text/rtf
	kSaveHTML,			// HTMLTranslator 'HTML', text/html
	kSaveSYLK,			// SYLKTranslator, text/sylk (spreadsheets)
	kSaveTabText,		// TextTranslator 'TABT', text/tab-separated-values
	kSaveCommaText,		// 'COMA', text/x-comma-separated-values
	kSaveSemicolonText,	// 'SEMI', text/x-semicolon-separated-values
	kSaveExcel			// ExcelTranslator 'XLS8', application/vnd.ms-excel
};

// Adds the "File Format" popup to a save panel (SavePanel::SavePanel);
// returns the menu field. `textFormats`: whether the document's main part
// is word processing (the HTML, Plain Text and RTF formats are offered);
// `sheetFormats`: a spreadsheet (Excel 97, SYLK and the delimited texts).
BMenuField* AddFormatField(BFilePanel* panel, bool textFormats, bool sheetFormats = false);
// The format chosen in the field (kSaveGobe if none).
SaveFormat ChosenFormat(BMenuField* field);
// SavePanel::FixSuffix: the file name's suffix for that format.
void FixSuffix(BFilePanel* panel, SaveFormat format);

// Writes the document's main part in `format` (TExportTranslation with the
// translator; text gets the StyledEdit attributes and the file type).
bool ExportFile(const Document& doc, const char* path, SaveFormat format, BString& error);

} // namespace gobe

#endif
