// SYLK (Symbolic Link) reader and writer tests.
// Tests the typed-cell interface: SylkCell::Kind, number, text, formula.
// Arguments: none.
#include <cmath>
#include <cstdio>
#include <string>

#include "Sylk.h"

using namespace gobe;

static int sOk = 0, sFailed = 0;

static void
Check(bool cond, const std::string& what)
{
	if (cond) {
		sOk++;
		return;
	}
	if (sFailed < 40)
		printf("FAIL sylk: %s\n", what.c_str());
	sFailed++;
}

static void
ExpectRead(bool expected, const std::string& data, const std::string& what)
{
	SylkSheet sheet;
	std::string error;
	bool result = ReadSylk(data, sheet, error);
	Check(result == expected, what);
}

static void
ExpectCells(int count, const SylkSheet& sheet, const std::string& what)
{
	Check(sheet.cells.size() == (size_t)count, what);
}

static void
ExpectCellPosition(const SylkSheet& sheet, int index, uint16_t col,
	uint16_t row, const std::string& what)
{
	if (index < 0 || index >= (int)sheet.cells.size()) {
		if (sFailed < 40)
			printf("FAIL sylk: %s (cell %d out of range, got %zu cells)\n",
				what.c_str(), index, sheet.cells.size());
		sFailed++;
		return;
	}
	const SylkCell& c = sheet.cells[index];
	Check(c.column == col && c.row == row, what);
}

static void
ExpectCellKind(const SylkSheet& sheet, int index, SylkCell::Kind kind,
	const std::string& what)
{
	if (index < 0 || index >= (int)sheet.cells.size()) {
		if (sFailed < 40)
			printf("FAIL sylk: %s (cell %d out of range)\n",
				what.c_str(), index);
		sFailed++;
		return;
	}
	Check(sheet.cells[index].kind == kind, what);
}

static void
ExpectCellNumber(const SylkSheet& sheet, int index, double expected,
	const std::string& what)
{
	if (index < 0 || index >= (int)sheet.cells.size()) return;
	Check(sheet.cells[index].number == expected, what);
}

static void
ExpectCellText(const SylkSheet& sheet, int index, const std::string& expected,
	const std::string& what)
{
	if (index < 0 || index >= (int)sheet.cells.size()) return;
	Check(sheet.cells[index].text == expected, what);
}

static void
ExpectCellFormula(const SylkSheet& sheet, int index,
	const std::string& expected, const std::string& what)
{
	if (index < 0 || index >= (int)sheet.cells.size()) return;
	Check(sheet.cells[index].formula == expected, what);
}

static void
ExpectMapSize(size_t expected, const std::map<uint16_t, int>& m,
	const std::string& what)
{
	Check(m.size() == expected, what);
}

static void
ExpectMapValue(uint16_t key, int value, const std::map<uint16_t, int>& m,
	const std::string& what)
{
	auto it = m.find(key);
	Check(it != m.end() && it->second == value, what);
}

static void
MakeNumberCell(SylkCell& c, uint16_t col, uint16_t row, double n)
{
	c.row = row; c.column = col;
	c.kind = SylkCell::kNumber; c.number = n;
}

static void
MakeStringCell(SylkCell& c, uint16_t col, uint16_t row, const std::string& t)
{
	c.row = row; c.column = col;
	c.kind = SylkCell::kString; c.text = t;
}

static void
MakeBoolCell(SylkCell& c, uint16_t col, uint16_t row, bool v)
{
	c.row = row; c.column = col;
	c.kind = SylkCell::kBool; c.number = v ? 1 : 0;
}

static void
MakeErrorCell(SylkCell& c, uint16_t col, uint16_t row, const std::string& t)
{
	c.row = row; c.column = col;
	c.kind = SylkCell::kError; c.text = t;
}

static void
MakeEmptyCell(SylkCell& c, uint16_t col, uint16_t row)
{
	c.row = row; c.column = col;
	c.kind = SylkCell::kEmpty;
}

// ---------------------------------------------------------------------------

int
main()
{
	std::string err;
	SylkSheet sheet;

	// ================================================================
	//  READ ERRORS
	// ================================================================

	ExpectRead(false, "", "empty input");
	ExpectRead(false, "C;X1;Y1;K\"hello\"", "no ID record");
	ExpectRead(false, "C\r\n", "just C without ID");

	// ================================================================
	//  BASIC ID RECORD
	// ================================================================

	SylkSheet s0;
	ReadSylk("ID;PGOBE\r\n", s0, err);
	ExpectCells(0, s0, "ID only has no cells");

	ReadSylk("ID;PGOBE\r\nE\r\n", s0, err);
	ExpectCells(0, s0, "ID+E has no cells");

	// ================================================================
	//  C RECORD: X, Y, K
	// ================================================================

	ReadSylk("ID;PGOBE\r\nC;X1;Y1;K100\r\n", s0, err);
	ExpectCells(1, s0, "single C record");
	ExpectCellPosition(s0, 0, 0, 0, "X1;Y1 -> col 0 row 0");
	ExpectCellKind(s0, 0, SylkCell::kNumber, "K100 is kNumber");
	ExpectCellNumber(s0, 0, 100, "K100 number value");

	ReadSylk("ID;PGOBE\r\nC;X5;Y3;K\"hello\"\r\n", s0, err);
	ExpectCells(1, s0, "C at X5;Y3");
	ExpectCellPosition(s0, 0, 4, 2, "X5;Y3 -> col 4 row 2");

	// ================================================================
	//  C RECORDS SHARE X/Y
	// ================================================================

	// a C record is one cell: of two K fields the last counts
	ReadSylk("ID;PGOBE\r\nC;X1;Y1;K1;K2\r\n", s0, err);
	ExpectCells(1, s0, "C record is one cell");
	ExpectCellPosition(s0, 0, 0, 0, "the cell at X1;Y1");
	ExpectCellNumber(s0, 0, 2, "the last K value");

	ReadSylk("ID;PGOBE\r\nC;X1;Y1;K10\r\nC;K20\r\n", s0, err);
	ExpectCells(2, s0, "C reuses previous X/Y");
	ExpectCellPosition(s0, 1, 0, 0, "second C reuses X1;Y1");

	ReadSylk("ID;PGOBE\r\nC;X1;Y1;K10\r\nC;X2;K20\r\n", s0, err);
	ExpectCells(2, s0, "C reuses Y but changes X");
	ExpectCellPosition(s0, 1, 1, 0, "C;X2 reuses Y1");

	// ================================================================
	//  MULTIPLE CELLS AT DIFFERENT POSITIONS
	// ================================================================

	ReadSylk("ID;PGOBE\r\nC;X1;Y1;K\"a\"\r\nC;X2;Y1;K\"b\"\r\nC;X1;Y2;K\"c\"\r\n",
		s0, err);
	ExpectCells(3, s0, "three cells at different positions");
	ExpectCellPosition(s0, 0, 0, 0, "cell 0 position");
	ExpectCellPosition(s0, 1, 1, 0, "cell 1 position");
	ExpectCellPosition(s0, 2, 0, 1, "cell 2 position");
	ExpectCellText(s0, 0, "a", "cell 0 text");
	ExpectCellText(s0, 1, "b", "cell 1 text");
	ExpectCellText(s0, 2, "c", "cell 2 text");

	// ================================================================
	//  C RECORD: ONLY X (reuses previous Y)
	// ================================================================

	ReadSylk("ID;PGOBE\r\nC;X1;Y1;K\"a\"\r\nC;X2;K\"a2\"\r\n", s0, err);
	ExpectCells(2, s0, "C with only X reuses Y");
	ExpectCellPosition(s0, 0, 0, 0, "first at X1;Y1");
	ExpectCellPosition(s0, 1, 1, 0, "second reuses Y1");

	// ================================================================
	//  C RECORD: ONLY Y (reuses previous X)
	// ================================================================

	ReadSylk("ID;PGOBE\r\nC;X1;Y1;K\"a\"\r\nC;Y2;K\"b\"\r\n", s0, err);
	ExpectCells(2, s0, "C with only Y reuses X");
	ExpectCellPosition(s0, 0, 0, 0, "first at X1;Y1");
	ExpectCellPosition(s0, 1, 0, 1, "second reuses X1, Y2");

	// ================================================================
	//  K FIELD: TRUE and FALSE
	// ================================================================

	ReadSylk("ID;PGOBE\r\nC;X1;Y1;KTRUE\r\n", s0, err);
	ExpectCells(1, s0, "TRUE boolean cell");
	ExpectCellKind(s0, 0, SylkCell::kBool, "KTRUE is kBool");
	ExpectCellNumber(s0, 0, 1, "TRUE number is 1");

	ReadSylk("ID;PGOBE\r\nC;X1;Y1;KFALSE\r\n", s0, err);
	ExpectCells(1, s0, "FALSE boolean cell");
	ExpectCellKind(s0, 0, SylkCell::kBool, "KFALSE is kBool");
	ExpectCellNumber(s0, 0, 0, "FALSE number is 0");

	// ================================================================
	//  K FIELD: ERROR CODES
	// ================================================================

	ReadSylk("ID;PGOBE\r\nC;X1;Y1;K#DIV/0!\r\n", s0, err);
	ExpectCells(1, s0, "division error");
	ExpectCellKind(s0, 0, SylkCell::kError, "K#DIV/0! is kError");
	ExpectCellText(s0, 0, "#DIV/0!", "#DIV/0! text");

	ReadSylk("ID;PGOBE\r\nC;X1;Y1;K#N/A\r\n", s0, err);
	ExpectCells(1, s0, "not available error");
	ExpectCellKind(s0, 0, SylkCell::kError, "K#N/A is kError");
	ExpectCellText(s0, 0, "#N/A", "#N/A text");

	ReadSylk("ID;PGOBE\r\nC;X1;Y1;K#VALUE!\r\n", s0, err);
	ExpectCells(1, s0, "value error");
	ExpectCellKind(s0, 0, SylkCell::kError, "K#VALUE! is kError");
	ExpectCellText(s0, 0, "#VALUE!", "#VALUE! text");

	ReadSylk("ID;PGOBE\r\nC;X1;Y1;K#REF!\r\n", s0, err);
	ExpectCells(1, s0, "ref error");
	ExpectCellKind(s0, 0, SylkCell::kError, "K#REF! is kError");
	ExpectCellText(s0, 0, "#REF!", "#REF! text");

	// ================================================================
	//  K FIELD: QUOTED STRINGS
	// ================================================================

	ReadSylk("ID;PGOBE\r\nC;X1;Y1;K\"hello world\"\r\n", s0, err);
	ExpectCells(1, s0, "quoted string");
	ExpectCellKind(s0, 0, SylkCell::kString, "quoted is kString");
	ExpectCellText(s0, 0, "hello world", "quoted string text");

	// the text is what lies between the first and the last quote
	ReadSylk("ID;PGOBE\r\nC;X1;Y1;K\"say \"hi\"\"\r\n", s0, err);
	ExpectCells(1, s0, "quote inside text");
	ExpectCellKind(s0, 0, SylkCell::kString, "quote inside text is kString");
	ExpectCellText(s0, 0, "say \"hi\"", "quote inside text kept");

	ReadSylk("ID;PGOBE\r\nC;X1;Y1;K\"a;;b\"\r\n", s0, err);
	ExpectCells(1, s0, ";; in quoted string");
	ExpectCellKind(s0, 0, SylkCell::kString, ";; string is kString");
	ExpectCellText(s0, 0, "a;b", ";; decodes to ;");

	ReadSylk("ID;PGOBE\r\nC;X1;Y1;K\"a;;b;;c\"\r\n", s0, err);
	ExpectCells(1, s0, "multiple escaped semicolons");
	ExpectCellText(s0, 0, "a;b;c", "multiple ;; decoded");

	// ================================================================
	//  K FIELD: NUMBERS
	// ================================================================

	ReadSylk("ID;PGOBE\r\nC;X1;Y1;K3.14159\r\n", s0, err);
	ExpectCells(1, s0, "float number");
	ExpectCellKind(s0, 0, SylkCell::kNumber, "3.14159 is kNumber");
	ExpectCellNumber(s0, 0, 3.14159, "float value");

	ReadSylk("ID;PGOBE\r\nC;X1;Y1;K-42\r\n", s0, err);
	ExpectCells(1, s0, "negative integer");
	ExpectCellKind(s0, 0, SylkCell::kNumber, "-42 is kNumber");
	ExpectCellNumber(s0, 0, -42, "negative value");

	ReadSylk("ID;PGOBE\r\nC;X1;Y1;K1e10\r\n", s0, err);
	ExpectCells(1, s0, "scientific notation");
	ExpectCellKind(s0, 0, SylkCell::kNumber, "1e10 is kNumber");
	ExpectCellNumber(s0, 0, 1e10, "1e10 value");

	ReadSylk("ID;PGOBE\r\nC;X1;Y1;K1.23E-4\r\n", s0, err);
	ExpectCells(1, s0, "small scientific notation");
	ExpectCellKind(s0, 0, SylkCell::kNumber, "1.23E-4 is kNumber");

	ReadSylk("ID;PGOBE\r\nC;X1;Y1;K0.1\r\n", s0, err);
	ExpectCells(1, s0, "0.1 number");
	ExpectCellKind(s0, 0, SylkCell::kNumber, "0.1 is kNumber");

	// ================================================================
	//  K FIELD: EMPTY
	// ================================================================

	ReadSylk("ID;PGOBE\r\nC;X1;Y1;K\r\n", s0, err);
	ExpectCells(1, s0, "empty K");
	ExpectCellKind(s0, 0, SylkCell::kEmpty, "empty K is kEmpty");

	// ================================================================
	//  K FIELD: FORMULA WITH VALUE
	// ================================================================

	ReadSylk("ID;PGOBE\r\nC;X1;Y1;K10;E=SUM(A1)\r\n", s0, err);
	ExpectCells(1, s0, "formula with value");
	ExpectCellKind(s0, 0, SylkCell::kNumber, "formula cell is kNumber");
	ExpectCellNumber(s0, 0, 10, "formula cell number");
	ExpectCellFormula(s0, 0, "=SUM(A1)", "formula text");

	// ================================================================
	//  K FIELD: FORMULA WITHOUT VALUE
	// ================================================================

	ReadSylk("ID;PGOBE\r\nC;X1;Y1;E=SUM(A1)\r\n", s0, err);
	ExpectCells(1, s0, "formula without value");
	ExpectCellKind(s0, 0, SylkCell::kEmpty, "formula-only is kEmpty");
	ExpectCellFormula(s0, 0, "=SUM(A1)", "formula text");

	// ================================================================
	//  B RECORD
	// ================================================================

	ReadSylk("ID;PGOBE\r\nB;Y10;X20\r\n", s0, err);
	ExpectCells(0, s0, "B record no cells");
	Check(s0.rows == 10, "B Y=10");
	Check(s0.columns == 20, "B X=20");

	// ================================================================
	//  F;W COLUMN WIDTHS
	// ================================================================

	ReadSylk("ID;PGOBE\r\nF;W1 3 12\r\n", s0, err);
	ExpectMapSize(3, s0.columnWidths, "width range 1..3 maps 3 cols");
	ExpectMapValue(0, 12, s0.columnWidths, "col 0 width 12");
	ExpectMapValue(1, 12, s0.columnWidths, "col 1 width 12");
	ExpectMapValue(2, 12, s0.columnWidths, "col 2 width 12");

	ReadSylk("ID;PGOBE\r\nF;W1 3 12;W5 7 8\r\n", s0, err);
	ExpectMapSize(6, s0.columnWidths, "two F;W ranges");
	ExpectMapValue(0, 12, s0.columnWidths, "first range width 12");
	ExpectMapValue(4, 8, s0.columnWidths, "second range start col 4");
	ExpectMapValue(6, 8, s0.columnWidths, "second range end col 6");

	// ================================================================
	//  E: END OF STREAM
	// ================================================================

	ReadSylk("ID;PGOBE\r\nC;X1;Y1;K\"hello\"\r\nE\r\nC;X2;Y2;K\"world\"\r\n",
		s0, err);
	ExpectCells(1, s0, "E stops parsing");
	ExpectCellText(s0, 0, "hello", "cell before E");

	// ================================================================
	//  UNKNOWN RECORDS IGNORED
	// ================================================================

	ReadSylk("ID;PGOBE\r\nP;SOME\r\nC;X1;Y1;K\"val\"\r\nQ;MORE\r\n",
		s0, err);
	ExpectCells(1, s0, "unknown records ignored");
	ExpectCellText(s0, 0, "val", "C record after unknown records");

	// ================================================================
	//  LINE ENDINGS
	// ================================================================

	ReadSylk("ID;PGOBE\nC;X1;Y1;K\"lf\"\n", s0, err);
	ExpectCells(1, s0, "LF line endings");
	ExpectCellText(s0, 0, "lf", "LF parsed");

	ReadSylk("ID;PGOBE\r\nC;X1;Y1;K\"a\"\nC;X2;Y2;K\"b\"\r\n", s0, err);
	ExpectCells(2, s0, "mixed CRLF and LF");
	ExpectCellText(s0, 0, "a", "first with CRLF");
	ExpectCellText(s0, 1, "b", "second with LF");

	ReadSylk("ID;PGOBE\rC;X1;Y1;K\"a\"\rC;X2;Y2;K\"b\"", s0, err);
	ExpectCells(2, s0, "CR-only line endings");
	ExpectCellText(s0, 0, "a", "first with CR");
	ExpectCellText(s0, 1, "b", "second with CR");

	// ================================================================
	//  WRITE: BASIC
	// ================================================================

	SylkSheet emptySheet;
	std::string sylk = WriteSylk(emptySheet);
	Check(sylk.find("ID;PGOBE") == 0, "write starts with ID;PGOBE");
	Check(sylk.find("E\r\n") != std::string::npos, "write ends with E");

	// ================================================================
	//  WRITE: NUMBER CELL
	// ================================================================

	SylkCell c;
	SylkSheet w1;
	MakeNumberCell(c, 0, 0, 42);
	w1.cells.push_back(c);
	sylk = WriteSylk(w1);
	Check(sylk.find("C;X1;Y1;K42") != std::string::npos,
		"write number cell");

	// ================================================================
	//  WRITE: STRING CELL
	// ================================================================

	SylkSheet w2;
	MakeStringCell(c, 0, 0, "hello");
	w2.cells.push_back(c);
	sylk = WriteSylk(w2);
	Check(sylk.find(";K\"hello\"") != std::string::npos,
		"write string cell");

	// ================================================================
	//  WRITE: BOOLEAN CELLS
	// ================================================================

	SylkSheet w3;
	MakeBoolCell(c, 0, 0, true);
	w3.cells.push_back(c);
	sylk = WriteSylk(w3);
	Check(sylk.find(";KTRUE") != std::string::npos,
		"write TRUE boolean");

	SylkSheet w4;
	MakeBoolCell(c, 0, 0, false);
	w4.cells.push_back(c);
	sylk = WriteSylk(w4);
	Check(sylk.find(";KFALSE") != std::string::npos,
		"write FALSE boolean");

	// ================================================================
	//  WRITE: ERROR CELL
	// ================================================================

	SylkSheet w5;
	MakeErrorCell(c, 0, 0, "#DIV/0!");
	w5.cells.push_back(c);
	sylk = WriteSylk(w5);
	Check(sylk.find(";K#DIV/0!") != std::string::npos,
		"write error cell");

	// ================================================================
	//  WRITE: FORMULA WITH VALUE
	// ================================================================

	SylkSheet w6;
	c.kind = SylkCell::kNumber; c.number = 10;
	c.row = 0; c.column = 0;
	c.formula = "=SUM(A1:A3)";
	w6.cells.push_back(c);
	sylk = WriteSylk(w6);
	Check(sylk.find("C;X1;Y1;K10;E=SUM(A1:A3)") != std::string::npos,
		"write formula with value");

	// ================================================================
	//  WRITE: FORMULA WITHOUT VALUE
	// ================================================================

	SylkSheet w7;
	MakeEmptyCell(c, 0, 0);
	c.formula = "=A1+B1";
	w7.cells.push_back(c);
	sylk = WriteSylk(w7);
	Check(sylk.find("C;X1;Y1;E=A1+B1") != std::string::npos,
		"write formula without value");

	// ================================================================
	//  WRITE: COLUMN WIDTHS
	// ================================================================

	SylkSheet w8;
	w8.columnWidths[0] = 12;
	w8.columnWidths[2] = 8;
	w8.cells.push_back(c);
	sylk = WriteSylk(w8);
	Check(sylk.find("F;W1 1 12") != std::string::npos,
		"write column width col 0");
	Check(sylk.find("F;W3 3 8") != std::string::npos,
		"write column width col 2");

	// ================================================================
	//  WRITE: LARGE POSITION
	// ================================================================

	SylkSheet w9;
	MakeNumberCell(c, 99, 25, 1);
	w9.cells.push_back(c);
	sylk = WriteSylk(w9);
	Check(sylk.find("C;X100;Y26;K1") != std::string::npos,
		"write large column and row");

	// ================================================================
	//  WRITE: SEMICOLON IN STRING
	// ================================================================

	SylkSheet w10;
	MakeStringCell(c, 0, 0, "a;b");
	w10.cells.push_back(c);
	sylk = WriteSylk(w10);
	Check(sylk.find("\"a;;b\"") != std::string::npos,
		"write semicolon in string");

	// ================================================================
	//  WRITE: QUOTES IN STRING
	// ================================================================

	SylkSheet w11;
	c = SylkCell();
	MakeStringCell(c, 0, 0, "say \"hi\"");
	w11.cells.push_back(c);
	sylk = WriteSylk(w11);
	Check(sylk.find("K\"say \"hi\"\"\r\n") != std::string::npos,
		"write quotes in string");

	// ================================================================
	//  WRITE: B RECORD (rows/columns nonzero)
	// ================================================================

	SylkSheet w12;
	w12.rows = 10;
	w12.columns = 20;
	MakeNumberCell(c, 0, 0, 1);
	w12.cells.push_back(c);
	sylk = WriteSylk(w12);
	Check(sylk.find("B;Y10;X20") != std::string::npos,
		"write B record");

	// ================================================================
	//  WRITE: NO B RECORD (rows and columns both zero)
	// ================================================================

	SylkSheet w13;
	MakeNumberCell(c, 0, 0, 1);
	w13.cells.push_back(c);
	sylk = WriteSylk(w13);
	Check(sylk.find("B;") == std::string::npos,
		"no B record when rows=0 and columns=0");

	// ================================================================
	//  ROUND-TRIP: NUMBER
	// ================================================================

	SylkSheet rt1;
	MakeNumberCell(c, 0, 0, 12345);
	rt1.cells.push_back(c);
	sylk = WriteSylk(rt1);
	SylkSheet rt1out;
	ReadSylk(sylk, rt1out, err);
	ExpectCells(1, rt1out, "round-trip number count");
	ExpectCellKind(rt1out, 0, SylkCell::kNumber, "round-trip kNumber");
	ExpectCellNumber(rt1out, 0, 12345, "round-trip number value");

	// ================================================================
	//  ROUND-TRIP: STRING
	// ================================================================

	SylkSheet rt2;
	MakeStringCell(c, 0, 0, "hello world");
	rt2.cells.push_back(c);
	sylk = WriteSylk(rt2);
	SylkSheet rt2out;
	ReadSylk(sylk, rt2out, err);
	ExpectCells(1, rt2out, "round-trip string count");
	ExpectCellKind(rt2out, 0, SylkCell::kString, "round-trip kString");
	ExpectCellText(rt2out, 0, "hello world", "round-trip string text");

	// ================================================================
	//  ROUND-TRIP: BOOLEAN
	// ================================================================

	SylkSheet rt3;
	MakeBoolCell(c, 0, 0, true);
	rt3.cells.push_back(c);
	sylk = WriteSylk(rt3);
	SylkSheet rt3out;
	ReadSylk(sylk, rt3out, err);
	ExpectCells(1, rt3out, "round-trip TRUE count");
	ExpectCellKind(rt3out, 0, SylkCell::kBool, "round-trip kBool");
	ExpectCellNumber(rt3out, 0, 1, "round-trip TRUE number");

	SylkSheet rt3b;
	MakeBoolCell(c, 0, 0, false);
	rt3b.cells.push_back(c);
	sylk = WriteSylk(rt3b);
	SylkSheet rt3bout;
	ReadSylk(sylk, rt3bout, err);
	ExpectCellKind(rt3bout, 0, SylkCell::kBool, "round-trip kBool FALSE");
	ExpectCellNumber(rt3bout, 0, 0, "round-trip FALSE number");

	// ================================================================
	//  ROUND-TRIP: ERROR
	// ================================================================

	SylkSheet rt4;
	MakeErrorCell(c, 0, 0, "#DIV/0!");
	rt4.cells.push_back(c);
	sylk = WriteSylk(rt4);
	SylkSheet rt4out;
	ReadSylk(sylk, rt4out, err);
	ExpectCells(1, rt4out, "round-trip error count");
	ExpectCellKind(rt4out, 0, SylkCell::kError, "round-trip kError");
	ExpectCellText(rt4out, 0, "#DIV/0!", "round-trip error text");

	// ================================================================
	//  ROUND-TRIP: STRING WITH ;;
	// ================================================================

	SylkSheet rt5;
	MakeStringCell(c, 0, 0, "a;b;c");
	rt5.cells.push_back(c);
	sylk = WriteSylk(rt5);
	SylkSheet rt5out;
	ReadSylk(sylk, rt5out, err);
	ExpectCellKind(rt5out, 0, SylkCell::kString, "round-trip ;; string");
	ExpectCellText(rt5out, 0, "a;b;c", "round-trip ;; string text");

	// ================================================================
	//  ROUND-TRIP: STRING WITH QUOTES
	// ================================================================

	SylkSheet rt6;
	MakeStringCell(c, 0, 0, "say \"hi\"");
	rt6.cells.push_back(c);
	sylk = WriteSylk(rt6);
	SylkSheet rt6out;
	ReadSylk(sylk, rt6out, err);
	ExpectCellKind(rt6out, 0, SylkCell::kString, "round-trip quoted string");
	ExpectCellText(rt6out, 0, "say \"hi\"",
		"round-trip quoted string text");

	// ================================================================
	//  ROUND-TRIP: COLUMN WIDTHS
	// ================================================================

	SylkSheet rt7;
	rt7.columnWidths[0] = 10;
	rt7.columnWidths[1] = 15;
	rt7.columnWidths[5] = 5;
	MakeNumberCell(c, 0, 0, 1);
	rt7.cells.push_back(c);
	sylk = WriteSylk(rt7);
	SylkSheet rt7out;
	ReadSylk(sylk, rt7out, err);
	ExpectMapSize(3, rt7out.columnWidths, "round-trip width count");
	ExpectMapValue(0, 10, rt7out.columnWidths, "round-trip width col 0");
	ExpectMapValue(1, 15, rt7out.columnWidths, "round-trip width col 1");
	ExpectMapValue(5, 5, rt7out.columnWidths, "round-trip width col 5");

	// ================================================================
	//  ROUND-TRIP: FORMULA WITH VALUE
	// ================================================================

	SylkSheet rt8;
	c.kind = SylkCell::kNumber; c.number = 42;
	c.row = 0; c.column = 0;
	c.formula = "=A1+B1";
	rt8.cells.push_back(c);
	sylk = WriteSylk(rt8);
	SylkSheet rt8out;
	ReadSylk(sylk, rt8out, err);
	ExpectCells(1, rt8out, "round-trip formula-with-value count");
	ExpectCellKind(rt8out, 0, SylkCell::kNumber, "round-trip formula kNumber");
	ExpectCellNumber(rt8out, 0, 42, "round-trip formula number");
	ExpectCellFormula(rt8out, 0, "=A1+B1", "round-trip formula text");

	// ================================================================
	//  ROUND-TRIP: FORMULA WITHOUT VALUE
	// ================================================================

	SylkSheet rt9;
	MakeEmptyCell(c, 0, 0);
	c.formula = "=SUM(C1:C10)";
	rt9.cells.push_back(c);
	sylk = WriteSylk(rt9);
	SylkSheet rt9out;
	ReadSylk(sylk, rt9out, err);
	ExpectCells(1, rt9out, "round-trip formula-without-value count");
	ExpectCellKind(rt9out, 0, SylkCell::kEmpty, "round-trip formula kEmpty");
	ExpectCellFormula(rt9out, 0, "=SUM(C1:C10)",
		"round-trip formula text");

	// ================================================================
	//  ROUND-TRIP: POSITION AND WIDTH COMBINED
	// ================================================================

	SylkSheet rt10;
	rt10.columnWidths[0] = 12;
	rt10.columnWidths[1] = 8;
	MakeNumberCell(c, 1, 2, 100);
	rt10.cells.push_back(c);
	sylk = WriteSylk(rt10);
	SylkSheet rt10out;
	ReadSylk(sylk, rt10out, err);
	ExpectCellPosition(rt10out, 0, 1, 2, "round-trip position");
	ExpectCellNumber(rt10out, 0, 100, "round-trip number");
	ExpectMapValue(0, 12, rt10out.columnWidths, "round-trip width col 0");
	ExpectMapValue(1, 8, rt10out.columnWidths, "round-trip width col 1");

	// ================================================================
	//  NUMBER PRECISION ROUND-TRIP
	// ================================================================

	SylkSheet rt11;
	MakeNumberCell(c, 0, 0, 3.14159265358979);
	rt11.cells.push_back(c);
	sylk = WriteSylk(rt11);
	SylkSheet rt11out;
	ReadSylk(sylk, rt11out, err);
	ExpectCells(1, rt11out, "precision round-trip count");
	double val = rt11out.cells[0].number;
	Check(val == 3.14159265358979, "round-trip precision maintained");

	// ================================================================
	//  200-CELL ROUND-TRIP
	// ================================================================

	SylkSheet bigIn;
	double values[] = {0.1, 1e-300, 123456789012345, -2.5};
	const int nVals = 4;

	for (int i = 0; i < 200; ++i) {
		int vIdx = i % nVals;
		MakeNumberCell(c, i % 20, i / 20, values[vIdx]);
		bigIn.cells.push_back(c);
	}
	// Add some string cells interspersed
	bigIn.cells.push_back(c);
	MakeStringCell(c, 0, 20, "row20col0");
	bigIn.cells.push_back(c);
	MakeStringCell(c, 1, 20, "row20col1 with ; and \"");
	bigIn.cells.push_back(c);
	bigIn.columnWidths[0] = 15;
	bigIn.columnWidths[1] = 10;
	bigIn.rows = 22;
	bigIn.columns = 21;

	sylk = WriteSylk(bigIn);
	SylkSheet bigOut;
	ReadSylk(sylk, bigOut, err);
	ExpectCells(203, bigOut, "200-cell round-trip count");
	ExpectCellKind(bigOut, 0, SylkCell::kNumber, "200-cell[0] kNumber");
	ExpectCellNumber(bigOut, 0, 0.1, "200-cell[0] value 0.1");
	ExpectCellKind(bigOut, 1, SylkCell::kNumber, "200-cell[1] kNumber");
	ExpectCellNumber(bigOut, 1, 1e-300, "200-cell[1] value 1e-300");
	ExpectCellKind(bigOut, 2, SylkCell::kNumber, "200-cell[2] kNumber");
	ExpectCellNumber(bigOut, 2, 123456789012345, "200-cell[2] value big int");
	ExpectCellKind(bigOut, 3, SylkCell::kNumber, "200-cell[3] kNumber");
	ExpectCellNumber(bigOut, 3, -2.5, "200-cell[3] value -2.5");
	// Check some string cells at end
	ExpectCellKind(bigOut, 201, SylkCell::kString, "string cell kString");
	ExpectCellText(bigOut, 201, "row20col0", "string cell text");
	ExpectCellKind(bigOut, 202, SylkCell::kString, "string with special chars");
	ExpectCellText(bigOut, 202, "row20col1 with ; and \"",
		"string with ; and \" text");
	ExpectMapSize(2, bigOut.columnWidths, "200-cell width count");
	ExpectMapValue(0, 15, bigOut.columnWidths, "200-cell width col 0");
	ExpectMapValue(1, 10, bigOut.columnWidths, "200-cell width col 1");
	Check(bigOut.rows == 22, "200-cell B Y round-trip");
	Check(bigOut.columns == 21, "200-cell B X round-trip");

	// ================================================================
	//  X AND Y ALWAYS WRITTEN; ';' IN FORMULAS; MALFORMED RECORDS
	// ================================================================

	{
		SylkSheet row;
		SylkCell a, b;
		a.kind = b.kind = SylkCell::kNumber;
		a.number = 1;
		b.column = 1;
		b.number = 2;
		b.formula = "IF(R1C1>0;1;2)";
		row.cells = {a, b};
		std::string text = WriteSylk(row);
		Check(text == "ID;PGOBE\r\nC;X1;Y1;K1\r\nC;X2;Y1;K2;EIF(R1C1>0;;1;;2)\r\nE\r\n",
			"X and Y on every C record, ';' in a formula as ';;'");
		SylkSheet back;
		std::string error;
		Check(ReadSylk(text, back, error) && back.cells.size() == 2
			&& back.cells[1].formula == "IF(R1C1>0;1;2)", "formula with ';' read back");
	}
	for (double number : {0.1 + 0.2, 1.0 / 3, 2.0 / 3 * 1e-200}) {
		// numbers that need 17 digits to read back the same
		SylkSheet one, back;
		SylkCell cell;
		cell.kind = SylkCell::kNumber;
		cell.number = number;
		one.cells.push_back(cell);
		std::string error;
		Check(ReadSylk(WriteSylk(one), back, error) && back.cells.size() == 1
			&& back.cells[0].number == number, "17-digit number round trip");
	}
	{
		// number formats: P records (escapes out), F records' P on the cell
		// at their position
		SylkSheet f;
		std::string error;
		Check(ReadSylk("ID;PGOBE\r\nP;PGeneral\r\nP;Pm\\/d\\/yy\r\nP;P\"$\"#,##0\\ ;;\\(\"$\"#,##0\\)\r\n"
			"F;P1;FG0G;X2;Y3\r\nC;K34545\r\nC;X1;Y1;K2\r\nF;P2;FC0G;Y1\r\nE\r\n", f, error),
			"formats read " + error);
		Check(f.formats.size() == 3 && f.formats[1] == "m/d/yy"
			&& f.formats[2] == "\"$\"#,##0 ;(\"$\"#,##0)", "P records unescaped");
		Check(f.cells.size() == 2 && f.cells[0].row == 2 && f.cells[0].column == 1
			&& f.cells[0].format == 1, "an F record's position is the C record's");
		Check(f.cells.size() == 2 && f.cells[1].format == 2, "an F record after its cell");
		// written: P records escaped, F before the cell; read back the same
		std::string text = WriteSylk(f);
		Check(text.find("P;Pm\\/d\\/yy\r\n") != std::string::npos
			&& text.find("P;P\"$\"#,##0 ;;\\(\"$\"#,##0\\)\r\n") != std::string::npos,
			"P records escaped");
		Check(text.find("F;P1;FG0G;X2;Y3\r\nC;X2;Y3;K34545") != std::string::npos,
			"an F record before its cell");
		Check(text.find("F;P2;FC0G;X1;Y1\r\nC;X1;Y1;K2") != std::string::npos, "currency 'C'");
		SylkSheet back;
		Check(ReadSylk(text, back, error) && back.formats == f.formats && back.cells.size() == 2
			&& back.cells[0].format == 1 && back.cells[1].format == 2, "formats read back");
		Check(DefaultSylkFormats().size() == 32 && DefaultSylkFormats()[10] == "m/d/yy",
			"write_default_format's 32");
	}
	ExpectRead(false, "ID;PGOBE\r\nC;X1;Y1;K1x\r\n", "a number with junk fails");
	ExpectRead(false, "ID;PGOBE\r\nC;X1;Y1;K\"open\r\n", "an unclosed string fails");
	ExpectRead(false, "ID;PGOBE\r\nC;X1;Y1;Kword\r\n", "an unquoted word fails");
	ExpectRead(false, "ID;PGOBE\r\nC;K1\r\n", "a first C record without X and Y fails");
	ExpectRead(false, "ID;PGOBE\r\nC;X0;Y1;K1\r\n", "X0 fails");
	ExpectRead(false, "ID;PGOBE\r\nF;W0 2 9\r\n", "F;W from column 0 fails");
	ExpectRead(false, "ID;PGOBE\r\nF;W3 2 9\r\n", "F;W backwards fails");
	ExpectRead(false, "ID;PGOBE\r\nF;Wx\r\n", "F;W not numbers fails");
	ExpectRead(false, "", "empty input fails");

	// ================================================================
	//  PRINT RESULT
	// ================================================================

	printf("sylk: ok %d, failed %d\n", sOk, sFailed);
	return sFailed > 0 || sOk == 0;
}
