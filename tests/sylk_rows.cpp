// sylk_rows: SYLK's font record and row heights (TExportSYLK: "P;F<name>;
// M<size x 20>", "F;M<height x 20>;R<row>"; TImportSYLK::rtd_P, rtd_F:
// M / 20 in whole points, with D the default height). Written by hand
// and read; a new spreadsheet with two rows resized saved and opened.
// Arguments: none (documents given are ignored).
#include <cstdio>
#include <string>

#include "Document.h"
#include "Editing.h"
#include "SSSylk.h"
#include "Sylk.h"

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


int
main()
{
	SylkSheet sheet;
	sheet.fonts.push_back({"Swis721 BT", 10});
	sheet.rowHeights[0] = 20;
	sheet.rowHeights[4] = 31.5f;
	SylkCell cell;
	cell.kind = SylkCell::kNumber;
	cell.number = 1;
	sheet.cells.push_back(cell);
	std::string text = WriteSylk(sheet);
	Check(text.find("P;FSwis721 BT;M200\r\n") != std::string::npos, "the font record:\n" + text);
	Check(text.find("F;M400;R1\r\nF;M630;R5\r\n") != std::string::npos, "the row heights:\n" + text);
	Check(text.find("P;FSwis721") < text.find("C;"), "the font before the cells");

	// read back: M / 20 in whole points (630 -> 31)
	SylkSheet back;
	std::string error;
	Check(ReadSylk(text, back, error), "read " + error);
	Check(back.fonts.size() == 1 && back.fonts[0].name == "Swis721 BT" && back.fonts[0].size == 10,
		"the font read back");
	Check(back.rowHeights.size() == 2 && back.rowHeights[0] == 20 && back.rowHeights[4] == 31,
		"the heights read back (31.5 as 31)");

	// another program's: a default height (F;D...;M) and Excel's fonts
	Check(ReadSylk("ID;PWXL;N;E\r\nP;FArial;M200\r\nP;EArial;M240;SB\r\nF;DG0G8;M255\r\n"
		"F;M300;R3\r\nC;Y1;X1;K1\r\nE\r\n", back, error), "read Excel's " + error);
	Check(back.defaultRowHeight == 12 && back.rowHeights.size() == 1 && back.rowHeights[2] == 15,
		"the default height 255 / 20 = 12, row 3 15");
	Check(back.fonts.size() == 2 && back.fonts[1].size == 12, "two fonts");

	// a spreadsheet: rows 2 and 4 resized, saved and opened
	Document doc;
	Check(CreateBlankSpreadsheet(doc, error), "a new spreadsheet " + error);
	size_t part = doc.spreadsheetParts.empty() ? 0 : doc.spreadsheetParts[0].first;
	SSPart* ss = doc.EditableSpreadsheetPart(part);
	Check(ss != nullptr, "its part");
	if (ss != nullptr) {
		SetLineSize(*ss, false, 1, 1, 30);
		SetLineSize(*ss, false, 3, 3, 9);
		SylkSheet out = SpreadsheetToSylk(doc, part);
		Check(out.rowHeights.size() == 2 && out.rowHeights[1] == 30 && out.rowHeights[3] == 9,
			"the heights exported");
		Check(out.fonts.size() == 1 && !out.fonts[0].name.empty() && out.fonts[0].size > 0,
			"the default font exported");
		std::string path = "/tmp/sylk_rows_test.slk";
		Check(ExportSylk(doc, part, path, error), "export " + error);
		Document opened;
		Check(ImportSylk(path, opened, error), "import " + error);
		const SSPart* in = opened.spreadsheetParts.empty() ? nullptr
			: opened.SpreadsheetPart(opened.spreadsheetParts[0].first);
		Check(in != nullptr && in->RowHeight(1) == 30 && in->RowHeight(3) == 9
			&& in->RowHeight(2) == in->defaultHeight, "the heights opened");
	}
	printf("sylk_rows: ok %d, failed %d\n", sOk, sFailed);
	return sFailed ? 1 : 0;
}
