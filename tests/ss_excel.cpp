// ss_excel: Excel workbooks read (ReadExcel) and opened as spreadsheets
// (ImportExcel). A BIFF8 workbook is built here in an OLE compound file:
// two worksheets with numbers (NUMBER, RK, MULRK), shared strings
// (LABELSST), a boolean and an error (BOOLERR), formulas (SUM over a range,
// a relative and an absolute reference, a function by number, a string
// result with its STRING record), a date in a date format, a column width.
// Expected values follow the BIFF8 and compound file layouts as Microsoft
// documents them and TImportExcel's conversions (AdjustTimeValue: 25569).
// Then every sample's spreadsheets written as a workbook (ExcelBytes) and
// read back: the same sheets, values and formulas.
// Arguments: the sample documents.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "Document.h"
#include "Editing.h"
#include "Excel.h"
#include "SSEval.h"

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


struct Bytes : std::vector<uint8_t> {
	void u8(uint8_t v) { push_back(v); }
	void u16(uint16_t v) { push_back(uint8_t(v)); push_back(uint8_t(v >> 8)); }
	void u32(uint32_t v) { u16(uint16_t(v)); u16(uint16_t(v >> 16)); }
	void f64(double v) { uint8_t b[8]; memcpy(b, &v, 8); insert(end(), b, b + 8); }
	void add(const std::vector<uint8_t>& b) { insert(end(), b.begin(), b.end()); }
};


static void
Record(Bytes& out, uint16_t type, const Bytes& data)
{
	out.u16(type);
	out.u16(uint16_t(data.size()));
	out.add(data);
}


// an unformatted BIFF8 string (8-bit characters)
static Bytes
String8(const std::string& s, bool longLength)
{
	Bytes b;
	if (longLength)
		b.u16(uint16_t(s.size()));
	else
		b.u8(uint8_t(s.size()));
	b.u8(0);
	b.insert(b.end(), s.begin(), s.end());
	return b;
}


// a compound file with one stream "Workbook" (at least 4096 bytes, so in
// normal sectors): the header, one FAT sector, one directory sector, the
// stream's sectors
static std::vector<uint8_t>
Compound(Bytes stream)
{
	while (stream.size() < 4096)
		stream.push_back(0);
	size_t sectors = (stream.size() + 511) / 512;
	stream.resize(sectors * 512, 0);
	Bytes file;
	const uint8_t signature[8] = {0xd0, 0xcf, 0x11, 0xe0, 0xa1, 0xb1, 0x1a, 0xe1};
	file.insert(file.end(), signature, signature + 8);
	file.resize(0x18, 0);
	file.u16(0x3e);			// minor version
	file.u16(3);			// major version
	file.u16(0xfffe);		// byte order
	file.u16(9);			// 512-byte sectors
	file.u16(6);			// 64-byte mini sectors
	file.resize(0x2c, 0);
	file.u32(1);			// FAT sectors
	file.u32(1);			// the directory's first sector
	file.u32(0);
	file.u32(4096);			// mini stream cutoff
	file.u32(0xfffffffe);	// no mini FAT
	file.u32(0);
	file.u32(0xfffffffe);	// no DIFAT sectors
	file.u32(0);
	file.u32(0);			// DIFAT[0]: the FAT in sector 0
	while (file.size() < 512)
		file.u32(0xffffffff);
	// sector 0: the FAT (0 FAT, 1 directory, 2.. the stream)
	Bytes fat;
	fat.u32(0xfffffffd);
	fat.u32(0xfffffffe);
	for (size_t i = 0; i < sectors; i++)
		fat.u32(i + 1 < sectors ? uint32_t(3 + i) : 0xfffffffe);
	while (fat.size() < 512)
		fat.u32(0xffffffff);
	file.add(fat);
	// sector 1: the directory (root, the stream)
	auto entry = [](const std::string& name, uint8_t type, uint32_t start, uint32_t size,
			uint32_t child) {
		Bytes e;
		for (char c : name)
			e.u16(uint16_t(c));
		e.u16(0);
		e.resize(64, 0);
		e.u16(uint16_t((name.size() + 1) * 2));
		e.u8(type);
		e.u8(1);
		e.u32(0xffffffff);
		e.u32(0xffffffff);
		e.u32(child);
		e.resize(0x74, 0);
		e.u32(start);
		e.u32(size);
		e.u32(0);
		return e;
	};
	Bytes directory;
	directory.add(entry("Root Entry", 5, 0xfffffffe, 0, 1));
	directory.add(entry("Workbook", 2, 2, uint32_t(stream.size()), 0xffffffff));
	directory.resize(512, 0);
	file.add(directory);
	file.add(stream);
	return file;
}


int
main(int argc, char** argv)
{
	// the workbook globals
	Bytes book;
	Bytes bof;
	bof.u16(0x0600);
	bof.u16(0x0005);
	bof.u32(0);
	bof.u32(0);
	bof.u32(0);
	Record(book, 0x809, bof);
	Bytes format;
	format.u16(164);
	format.add(String8("0.0", true));
	Record(book, 0x41e, format);
	// XF 0: General, XF 1: "0.0" (164), XF 2: m/d/yy (14)
	for (uint16_t f : {uint16_t(0), uint16_t(164), uint16_t(14)}) {
		Bytes xf;
		xf.u16(0);
		xf.u16(f);
		xf.resize(20, 0);
		Record(book, 0xe0, xf);
	}
	Bytes sst;
	sst.u32(4);
	sst.u32(4);
	for (const char* s : {"Item", "Qty", "Caf\xe9"})
		sst.add(String8(s, true));
	// a 16-bit string: "\u03a9 ok"
	sst.u16(4);
	sst.u8(1);
	for (uint16_t ch : {uint16_t(0x3a9), uint16_t(' '), uint16_t('o'), uint16_t('k')})
		sst.u16(ch);
	Record(book, 0xfc, sst);
	// the sheets' BOUNDSHEET records (their offsets patched once known)
	size_t sheet1At = book.size() + 4, sheet2At;
	Bytes bound;
	bound.u32(0);
	bound.u8(0);
	bound.u8(0);
	bound.add(String8("Prices", false));
	Record(book, 0x85, bound);
	sheet2At = book.size() + 4;
	Bytes bound2 = bound;
	bound2.resize(6);
	bound2.add(String8("Notes", false));
	Record(book, 0x85, bound2);
	// a defined name "Rates" for Prices!B2:B3 (EXTERNSHEET's XTI 0 is the
	// first sheet; NAME's definition a tArea3d)
	Bytes extern_;
	extern_.u16(1);
	extern_.u16(0);
	extern_.u16(0);
	extern_.u16(0);
	Record(book, 0x17, extern_);
	Bytes name;
	name.u16(0);			// options
	name.u8(0);				// keyboard shortcut
	name.u8(5);				// the name's length
	name.u16(11);			// the definition's
	name.u16(0);
	name.u16(0);			// (a global name)
	name.u8(0);
	name.u8(0);
	name.u8(0);
	name.u8(0);
	name.u8(0);				// flags: 8-bit characters
	for (char ch : std::string("Rates"))
		name.u8(uint8_t(ch));
	name.u8(0x3b);			// tArea3d
	name.u16(0);
	name.u16(1);
	name.u16(2);
	name.u16(1);
	name.u16(1);
	Record(book, 0x18, name);
	Record(book, 0x0a, Bytes());
	// sheet 1
	uint32_t first = uint32_t(book.size());
	Record(book, 0x809, bof);
	Bytes dims;
	dims.u32(0);
	dims.u32(5);
	dims.u16(0);
	dims.u16(4);
	dims.u16(0);
	Record(book, 0x200, dims);
	Bytes colinfo;
	colinfo.u16(0);
	colinfo.u16(0);
	colinfo.u16(20 * 256);		// 20 characters
	colinfo.u16(0);
	colinfo.u16(0);
	colinfo.u16(0);
	Record(book, 0x7d, colinfo);
	auto labelsst = [&](uint16_t r, uint16_t c, uint32_t i) {
		Bytes b;
		b.u16(r);
		b.u16(c);
		b.u16(0);
		b.u32(i);
		Record(book, 0xfd, b);
	};
	labelsst(0, 0, 0);
	labelsst(0, 1, 1);
	Bytes number;
	number.u16(1);
	number.u16(1);
	number.u16(1);				// "0.0"
	number.f64(2.5);
	Record(book, 0x203, number);
	Bytes rk;					// 3 (an integer RK: 3 << 2 | 2)
	rk.u16(2);
	rk.u16(1);
	rk.u16(0);
	rk.u32(3u << 2 | 2);
	Record(book, 0x27e, rk);
	Bytes mulrk;				// row 3, columns 1-2: 10 and 0.25 (25 / 100)
	mulrk.u16(3);
	mulrk.u16(1);
	mulrk.u16(0);
	mulrk.u32(10u << 2 | 2);
	mulrk.u16(0);
	mulrk.u32(25u << 2 | 3);
	mulrk.u16(2);
	Record(book, 0xbd, mulrk);
	// B5: =SUM(B2:B3) (tArea, tFuncVar SUM with 1 argument)
	auto formula = [&](uint16_t r, uint16_t c, uint16_t xf, double value, bool text,
			const Bytes& tokens) {
		Bytes b;
		b.u16(r);
		b.u16(c);
		b.u16(xf);
		if (text) {
			b.u8(0);
			b.resize(b.size() + 5, 0);
			b.u16(0xffff);
		} else
			b.f64(value);
		b.u16(0);
		b.u32(0);
		b.u16(uint16_t(tokens.size()));
		b.add(tokens);
		Record(book, 0x06, b);
	};
	Bytes sum;
	sum.u8(0x25);
	sum.u16(1);
	sum.u16(2);
	sum.u16(1 | 0xc000);
	sum.u16(1 | 0xc000);
	sum.u8(0x22);
	sum.u8(1);
	sum.u16(4);
	formula(4, 1, 0, 5.5, false, sum);
	// C5: =B2*$B$3+ROUND(1.5,0) (tRef relative, tRef absolute, tFunc ROUND)
	Bytes mix;
	mix.u8(0x24);
	mix.u16(1);
	mix.u16(1 | 0xc000);
	mix.u8(0x24);
	mix.u16(2);
	mix.u16(1);
	mix.u8(0x05);
	mix.u8(0x1f);
	mix.f64(1.5);
	mix.u8(0x1e);
	mix.u16(0);
	mix.u8(0x21);
	mix.u16(27);
	mix.u8(0x03);
	formula(4, 2, 0, 9.5, false, mix);
	// D5: ="ab"&"cd", a string result in its STRING record
	Bytes concat;
	concat.u8(0x17);
	concat.add(String8("ab", false));
	concat.u8(0x17);
	concat.add(String8("cd", false));
	concat.u8(0x08);
	formula(4, 3, 0, 0, true, concat);
	Record(book, 0x207, String8("abcd", true));
	// F5: =SUM(Rates) (tName 1, tFuncVar SUM)
	Bytes named;
	named.u8(0x43);
	named.u16(1);
	named.u16(0);
	named.u8(0x22);
	named.u8(1);
	named.u16(4);
	formula(4, 5, 0, 5.5, false, named);
	Bytes boolean;
	boolean.u16(1);
	boolean.u16(3);
	boolean.u16(0);
	boolean.u8(1);
	boolean.u8(0);
	Record(book, 0x205, boolean);
	Bytes err;
	err.u16(2);
	err.u16(3);
	err.u16(0);
	err.u8(0x07);
	err.u8(1);
	Record(book, 0x205, err);
	Bytes date;					// 36373 is 1999-07-31 in Excel's count
	date.u16(1);
	date.u16(4);
	date.u16(2);
	date.f64(36373);
	Record(book, 0x203, date);
	Record(book, 0x0a, Bytes());
	// sheet 2: one string
	uint32_t second = uint32_t(book.size());
	Record(book, 0x809, bof);
	labelsst(0, 0, 2);
	labelsst(1, 0, 3);
	Record(book, 0x0a, Bytes());
	memcpy(&book[sheet1At], &first, 4);
	memcpy(&book[sheet2At], &second, 4);
	// (padding: an unknown record so the stream needs normal sectors)
	Bytes pad;
	pad.resize(4200, 0);
	Record(book, 0x1fff, pad);

	std::vector<uint8_t> file = Compound(book);
	ExcelBook read;
	std::string error;
	Check(ReadExcel(file, read, error), "read " + error);
	Check(read.version == 0x0600 && read.sheets.size() == 2, "BIFF8, two worksheets");
	if (read.sheets.size() == 2) {
		Check(read.sheets[0].name == "Prices" && read.sheets[1].name == "Notes", "sheet names");
		auto find = [&](int s, int r, int c) -> const ExcelCell* {
			for (const ExcelCell& cell : read.sheets[size_t(s)].cells)
				if (cell.row == r && cell.column == c)
					return &cell;
			return nullptr;
		};
		const ExcelCell* c;
		Check((c = find(0, 0, 0)) && c->kind == ExcelCell::kString && c->text == "Item", "A1 Item");
		Check((c = find(0, 1, 1)) && c->number == 2.5 && c->format == "0.0", "B2 2.5 in 0.0");
		Check((c = find(0, 2, 1)) && c->number == 3, "B3 an RK integer");
		Check((c = find(0, 3, 1)) && c->number == 10 && (c = find(0, 3, 2)) && c->number == 0.25,
			"MULRK 10 and 0.25");
		c = find(0, 4, 1);
		Check(c && c->formula == "=SUM(B2..B3)", "=SUM(B2..B3): "
			+ (c ? c->formula : std::string("none")));
		c = find(0, 4, 2);
		Check(c && c->formula == "=B2*$B$3+ROUND(1.5,0)", "a mix: "
			+ (c ? c->formula : std::string("none")));
		Check((c = find(0, 4, 3)) && c->formula == "=\"ab\"&\"cd\"" && c->text == "abcd",
			"a string formula and its STRING");
		Check((c = find(0, 1, 3)) && c->kind == ExcelCell::kBool && c->number == 1, "TRUE");
		Check((c = find(0, 2, 3)) && c->kind == ExcelCell::kError && c->text == "#DIV/0!",
			"#DIV/0!");
		Check((c = find(0, 1, 4)) && c->format == "m/d/yy" && c->number == 36373 - 25569,
			"a date in Gobe's days");
		Check(read.sheets[0].columnWidths.count(0) && read.sheets[0].columnWidths.at(0) == 140,
			"column A 20 characters (140 points)");
		c = find(0, 4, 5);
		Check(c && c->formula == "=SUM(Rates)", "a defined name: " + (c ? c->formula : std::string()));
		Check(read.names.size() == 1 && read.names[0].name == "Rates" && read.names[0].sheet == 0
			&& read.names[0].row1 == 1 && read.names[0].row2 == 2 && read.names[0].column1 == 1,
			"the name's range");
		Check((c = find(1, 0, 0)) && c->text == "Caf\xc3\xa9", "Latin-1 text as UTF-8");
		Check((c = find(1, 1, 0)) && c->text == "\xce\xa9 ok", "16-bit text as UTF-8");
	}
	// opened as a document
	std::string path = "/tmp/ss_excel_test.xls";
	FILE* f = fopen(path.c_str(), "wb");
	fwrite(file.data(), 1, file.size(), f);
	fclose(f);
	Document doc;
	std::string notice;
	Check(ImportExcel(path, doc, error, &notice), "import " + error);
	Check(doc.SheetCount() == 2 && doc.sheets.sheets[0].name == "Prices"
		&& doc.sheets.sheets[1].name == "Notes", "two sheets, their names");
	const SSPart* ss = doc.SpreadsheetPart(doc.sheets.sheets[0].mainPart);
	if (ss != nullptr) {
		SSBook book2(doc);
		SSEvaluator e(*ss, &book2, int(doc.sheets.sheets[0].mainPart));
		Check(e.Value(4, 1).number == 5.5, "SUM evaluated: " + std::to_string(e.Value(4, 1).number));
		Check(e.Value(4, 2).number == 2.5 * 3 + 2, "the mix evaluated");
		Check(e.Value(4, 3).text == "abcd", "the string formula");
		Check(e.Value(4, 5).number == 5.5 && doc.NamedSelection("Rates") != nullptr,
			"=SUM(Rates) through the named selection");
		Check(CellInput(*ss, 4, 1) == "=SUM(B2..B3)", "the formula as typed");
		Check(e.Value(2, 3).type == SSValue::kError, "the error");
		Check(std::fabs(ss->ColumnWidth(0) - 140) < 0.01, "column A's width");
	}
	remove(path.c_str());

	// the samples written and read back
	int documents = 0, cells = 0, formulas = 0;
	for (int i = 1; i < argc; i++) {
		Document sample;
		if (!LoadDocument(argv[i], sample, error))
			continue;
		std::vector<size_t> parts;
		for (size_t s = 0; s < sample.SheetCount(); s++)
			if (sample.SpreadsheetPart(sample.sheets.sheets[s].mainPart) != nullptr)
				parts.push_back(sample.sheets.sheets[s].mainPart);
		if (parts.empty())
			continue;
		documents++;
		std::string name = argv[i];
		ExcelBook back;
		Check(ReadExcel(ExcelBytes(sample), back, error), name + ": read back " + error);
		Check(back.sheets.size() == parts.size(), name + ": a worksheet per spreadsheet");
		if (back.sheets.size() != parts.size())
			continue;
		SSBook sampleBook(sample), again(sample);
		for (size_t s = 0; s < parts.size(); s++) {
			const SSPart& ss = *sample.SpreadsheetPart(parts[s]);
			SSEvaluator e(ss, &sampleBook, int(parts[s])), e2(ss, &again, int(parts[s]));
			std::map<std::pair<int, int>, const ExcelCell*> byCell;
			for (const ExcelCell& c : back.sheets[s].cells)
				byCell[{c.row, c.column}] = &c;
			for (const SSCell& cell : ss.cells) {
				if (cell.id.b == 0xffff || cell.id.b > 255)
					continue;
				SSValue v = e.Value(cell.id.a, cell.id.b);
				auto found = byCell.find({cell.id.a, cell.id.b});
				std::string where = name + " " + CellName(cell.id);
				// (nothing to write: an empty cell, a formula with no value
				// Excel can't take; a value that changes each time, RAND's)
				SSValue v2 = e2.Value(cell.id.a, cell.id.b);
				if (v.type == SSValue::kNone || v2.number != v.number || v2.text != v.text)
					continue;
				if (found == byCell.end()) {
					Check(false, where + ": missing");
					continue;
				}
				const ExcelCell& c = *found->second;
				cells++;
				if (v.type == SSValue::kString)
					Check(c.text == v.text, where + ": text");
				else if (v.type == SSValue::kNumber || v.type == SSValue::kTime) {
					double n = 0;
					v.GetNum(n);
					// (a time-only format keeps the time of day: AdjustTimeValue)
					bool timeOnly = c.format.find_first_of("dDyY") == std::string::npos
						&& c.format.find_first_of("hHsS") != std::string::npos;
					if (timeOnly)
						n -= std::floor(n);
					Check(c.kind == ExcelCell::kNumber && std::fabs(c.number - n) < 1e-6,
						where + ": number " + std::to_string(n) + " / " + std::to_string(c.number));
				}
				if (!c.formula.empty()) {
					formulas++;
					Check(c.formula == CellInput(ss, cell.id.a, cell.id.b), where + ": formula "
						+ CellInput(ss, cell.id.a, cell.id.b) + " / " + c.formula);
				}
			}
		}
	}
	Check(documents > 5 && formulas > 1000, "samples written: " + std::to_string(documents) + ", "
		+ std::to_string(cells) + " cells, " + std::to_string(formulas) + " formulas");
	printf("ss_excel: ok %d, failed %d\n", sOk, sFailed);
	return sFailed != 0 || sOk == 0;
}
