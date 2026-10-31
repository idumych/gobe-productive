// ss_table: text tables opened as spreadsheets (ImportTable, the Text
// translator's TABT, COMA and SEMI formats): cells at the delimiters and
// LFs (CR LF and CR too), runs of them skipping columns and rows, numbers
// and formulas typed, a formula that doesn't parse kept as text, the
// delimiter as TableDelimiter picks it. Expected cells worked out by hand
// from GetNextCell.
// Arguments: none.
#include <cstdio>
#include <string>

#include "Document.h"
#include "Editing.h"
#include "SSEval.h"
#include "TextImport.h"

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


static bool
Import(const std::string& text, Document& doc, char delimiter = 0)
{
	std::string path = "/tmp/ss_table_test.txt";
	FILE* f = fopen(path.c_str(), "wb");
	fwrite(text.data(), 1, text.size(), f);
	fclose(f);
	std::string error;
	bool ok = ImportTable(path, doc, error, delimiter);
	remove(path.c_str());
	if (!ok)
		printf("  ImportTable: %s\n", error.c_str());
	return ok;
}


// what cell (row, column) shows, as text
static std::string
Shown(const Document& doc, uint16_t row, uint16_t column)
{
	const SSPart& ss = *doc.SpreadsheetPart(size_t(doc.MainPart()));
	SSBook book(doc);
	SSEvaluator e(ss, &book, doc.MainPart());
	SSValue v = e.Value(row, column);
	switch (v.type) {
		case SSValue::kNumber: {
			char text[32];
			snprintf(text, sizeof text, "%g", v.number);
			return text;
		}
		case SSValue::kString:
			return "'" + v.text;
		case SSValue::kNone:
			return "";
		default:
			return "?";
	}
}


int
main()
{
	Check(TableDelimiter("a\tb\tc\n1\t2\t3\n") == '\t', "tabs");
	Check(TableDelimiter("a,b,c\n1,2,3\n") == ',', "commas");
	Check(TableDelimiter("a;b;c\n1;2;3\n") == ';', "semicolons");
	Check(TableDelimiter("a;b,c;d\n") == ';', "more semicolons than commas");

	{
		// row 0: Name, Qty, Price; row 1: Pens, 3, 1.5, =B2*C2; row 3 (two
		// LFs): a cell after an empty one (two tabs) in column 2
		Document doc;
		Check(Import("Name\tQty\tPrice\nPens\t3\t1.5\t=B2*C2\n\nx\t\tlast\n", doc), "tab table");
		Check(Shown(doc, 0, 0) == "'Name" && Shown(doc, 0, 2) == "'Price", "header row");
		Check(Shown(doc, 1, 1) == "3" && Shown(doc, 1, 2) == "1.5", "numbers");
		Check(Shown(doc, 1, 3) == "4.5", "formula =B2*C2: " + Shown(doc, 1, 3));
		Check(Shown(doc, 2, 0) == "", "an empty row");
		Check(Shown(doc, 3, 0) == "'x" && Shown(doc, 3, 1) == "" && Shown(doc, 3, 2) == "'last",
			"two tabs skip a column");
		const SSPart& ss = *doc.SpreadsheetPart(size_t(doc.MainPart()));
		Check(ss.cells.size() == 9, "nine cells (3 + 4 + 2): " + std::to_string(ss.cells.size()));
		Check(doc.name.compare(0, 8, "untitled") == 0, "an untitled spreadsheet");
	}
	{
		// commas, CR LF line ends, the delimiter detected
		Document doc;
		Check(Import("a,b\r\n1,2\r\n=A2+B2,TRUE\r\n", doc), "comma table");
		Check(Shown(doc, 0, 1) == "'b" && Shown(doc, 1, 0) == "1" && Shown(doc, 2, 0) == "3",
			"commas, CR LF: " + Shown(doc, 2, 0));
	}
	{
		// semicolons, lone CR line ends; a leading delimiter starts at
		// column 1; a formula that doesn't parse stays its text
		Document doc;
		Check(Import(";x;y\r1;2;=SUM(\r", doc), "semicolon table");
		Check(Shown(doc, 0, 0) == "" && Shown(doc, 0, 1) == "'x" && Shown(doc, 0, 2) == "'y",
			"a leading delimiter");
		Check(Shown(doc, 1, 2) == "'=SUM(", "bad formula kept as text: " + Shown(doc, 1, 2));
	}
	{
		// the delimiter given: commas inside a tab table are text
		Document doc;
		Check(Import("1,5\t2\n", doc, '\t'), "tab table with commas");
		Check(Shown(doc, 0, 0) == "'1,5" && Shown(doc, 0, 1) == "2", "comma kept in the text");
		// and saved and reloaded
		std::string error;
		Check(doc.Save("/tmp/ss_table_saved.gobe", error), "save " + error);
		Document back;
		Check(LoadDocument("/tmp/ss_table_saved.gobe", back, error), "reload " + error);
		Check(Shown(back, 0, 1) == "2", "reloaded");
		remove("/tmp/ss_table_saved.gobe");
	}
	printf("ss_table: ok %d, failed %d\n", sOk, sFailed);
	return sFailed != 0 || sOk == 0;
}
