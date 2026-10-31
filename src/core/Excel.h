// Excel workbooks (BIFF5 and BIFF8 in an OLE compound file) opened as
// spreadsheets, as the Excel translator reads them (ExcelTranslator/
// TImportExcel.c, TBiffReader, TExcelParser).
#ifndef GOBE_EXCEL_H
#define GOBE_EXCEL_H

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace gobe {

struct Document;

// An OLE compound file ([MS-CFB]): its streams by name.
class CompoundFile {
public:
	bool Open(const std::vector<uint8_t>& data, std::string& error);
	// the stream's bytes; false if there's none of that name
	bool Stream(const std::string& name, std::vector<uint8_t>& out) const;

private:
	std::vector<uint8_t> Chain(uint32_t first, uint64_t size, bool mini) const;

	const std::vector<uint8_t>* fData = nullptr;
	uint32_t fSectorSize = 512, fMiniSectorSize = 64, fMiniCutoff = 4096;
	std::vector<uint32_t> fFAT, fMiniFAT;
	std::vector<uint8_t> fMiniStream;
	struct Entry {
		std::string name;
		uint8_t type;
		uint32_t start;
		uint64_t size;
	};
	std::vector<Entry> fEntries;
};

struct ExcelCell {
	uint16_t row = 0, column = 0;
	enum Kind { kNumber, kString, kBool, kError, kBlank } kind = kBlank;
	double number = 0;			// kNumber (a date or time already in Gobe's days); kBool 0/1
	std::string text;			// kString; kError: "#DIV/0!" etc.
	std::string formula;		// as typed in Gobe ("=SUM(A1..A3)"), "" if none or not convertible
	bool badFormula = false;	// a formula that couldn't be converted (its value kept)
	std::string format;			// the cell's number format (its XF's), "" for General
};

struct ExcelSheet {
	std::string name;
	std::vector<ExcelCell> cells;
	std::map<uint16_t, float> columnWidths;		// column -> width in points
	uint16_t rows = 0, columns = 0;				// DIMENSIONS: one past the last
};

// a defined name (NAME) for a range of one of the workbook's worksheets
struct ExcelName {
	std::string name;
	size_t sheet = 0;			// index into ExcelBook::sheets
	uint16_t row1 = 0, column1 = 0, row2 = 0, column2 = 0;
};

struct ExcelBook {
	int version = 0;			// the BOF's: 0x0500 BIFF5, 0x0600 BIFF8
	bool date1904 = false;		// DATEMODE
	std::vector<ExcelSheet> sheets;
	std::vector<ExcelName> names;
};

// TImportExcel::DoTranslate's reading: the workbook globals (BOUNDSHEET,
// SST, FORMAT, XF, DATEMODE, CODEPAGE), then each worksheet's cells
// (NUMBER, RK, MULRK, LABEL, LABELSST, BOOLERR, FORMULA and STRING, BLANK
// and MULBLANK skipped), COLINFO widths and DIMENSIONS. Formulas are
// turned from their tokens into Gobe's text (TExcelParser: the function
// names as the translator's FunctionNames has them, ranges "A1..B2",
// defined names by name); defined names (NAME, BIFF8) that are a range of
// a worksheet (tRef3d, tArea3d through EXTERNSHEET) are kept; numbers in
// date and time formats become Gobe's days (AdjustTimeValue).
bool ReadExcel(const std::vector<uint8_t>& file, ExcelBook& book, std::string& error);

// Opens a workbook as a new spreadsheet document: a sheet per worksheet
// (CreateBlankSpreadsheet, AddSheet), named as in the workbook, its
// defined names as named selections (CreateNamedReferences), the cells
// typed (formulas the parser refuses keep their values), their number
// formats, the column widths. `notice` gets a message when formulas
// couldn't be converted.
bool ImportExcel(const std::string& path, Document& doc, std::string& error,
	std::string* notice = nullptr);

// The workbook Save As writes in the "Excel" format (TExportExcel, BIFF8
// in an OLE compound file: the "Workbook" stream): the document's
// spreadsheet sheets (CountSSSheets), each cell's value and its formula
// (Gobe's tokens as Excel's; a formula with something Excel can't take,
// another part's reference or a name, is written as its value), the
// number formats (FORMAT and XF), the column widths (COLINFO), the 1904
// date system (DATEMODE 1, as Write_Global_Records sets it).
std::vector<uint8_t> ExcelBytes(const Document& doc);
bool ExportExcel(const Document& doc, const std::string& path, std::string& error);

// A compound file with the given streams (root storage only): streams
// under 4096 bytes in the mini stream, as [MS-CFB] has them.
std::vector<uint8_t> MakeCompoundFile(
	const std::vector<std::pair<std::string, std::vector<uint8_t>>>& streams);

} // namespace gobe

#endif
