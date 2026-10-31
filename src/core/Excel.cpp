// Excel import: an OLE compound file reader ([MS-CFB]) and TImportExcel's
// BIFF records (ExcelTranslator/TImportExcel.c, TBiffReader.c,
// TExcelParser.c for formulas).
#include "Excel.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>

#include "Document.h"
#include "Editing.h"
#include "Names.h"
#include "SSEval.h"
#include "SSPart.h"
#include "Formula.h"
#include "SSStyle.h"

namespace gobe {

namespace {

uint16_t
U16(const std::vector<uint8_t>& d, size_t at)
{
	return at + 2 <= d.size() ? uint16_t(d[at] | d[at + 1] << 8) : 0;
}


uint32_t
U32(const std::vector<uint8_t>& d, size_t at)
{
	return at + 4 <= d.size() ? uint32_t(d[at] | d[at + 1] << 8 | d[at + 2] << 16
		| uint32_t(d[at + 3]) << 24) : 0;
}


double
F64(const std::vector<uint8_t>& d, size_t at)
{
	double v = 0;
	if (at + 8 <= d.size())
		memcpy(&v, &d[at], 8);	// (little-endian hosts)
	return v;
}


const uint32_t kEndOfChain = 0xfffffffe, kFree = 0xffffffff;

} // namespace


bool
CompoundFile::Open(const std::vector<uint8_t>& data, std::string& error)
{
	static const uint8_t kSignature[8] = {0xd0, 0xcf, 0x11, 0xe0, 0xa1, 0xb1, 0x1a, 0xe1};
	if (data.size() < 512 || memcmp(data.data(), kSignature, 8) != 0) {
		error = "not an OLE compound file";
		return false;
	}
	fData = &data;
	fSectorSize = 1u << U16(data, 0x1e);
	fMiniSectorSize = 1u << U16(data, 0x20);
	fMiniCutoff = U32(data, 0x38);
	if (fSectorSize < 128 || fSectorSize > 65536) {
		error = "bad sector size";
		return false;
	}
	auto sector = [&](uint32_t n) -> size_t { return size_t(n + 1) * fSectorSize; };
	// the FAT's sectors: the header's DIFAT, then the DIFAT chain
	std::vector<uint32_t> fatSectors;
	for (int i = 0; i < 109; i++) {
		uint32_t s = U32(data, 0x4c + size_t(i) * 4);
		if (s != kFree)
			fatSectors.push_back(s);
	}
	uint32_t difat = U32(data, 0x44);
	for (uint32_t k = 0; difat != kEndOfChain && difat != kFree && k < 10000; k++) {
		size_t at = sector(difat);
		for (uint32_t i = 0; i + 1 < fSectorSize / 4; i++) {
			uint32_t s = U32(data, at + i * 4);
			if (s != kFree)
				fatSectors.push_back(s);
		}
		difat = U32(data, at + fSectorSize - 4);
	}
	for (uint32_t s : fatSectors) {
		size_t at = sector(s);
		if (at + fSectorSize > data.size())
			break;
		for (uint32_t i = 0; i < fSectorSize / 4; i++)
			fFAT.push_back(U32(data, at + i * 4));
	}
	// the directory
	std::vector<uint8_t> directory = Chain(U32(data, 0x30), UINT64_MAX, false);
	for (size_t at = 0; at + 128 <= directory.size(); at += 128) {
		Entry e;
		uint16_t length = U16(directory, at + 0x40);
		for (size_t i = 0; i + 2 <= length && i < 64; i += 2) {
			uint16_t c = U16(directory, at + i);
			if (c == 0)
				break;
			e.name += c < 0x80 ? char(c) : '?';
		}
		e.type = directory[at + 0x42];
		e.start = U32(directory, at + 0x74);
		e.size = U32(directory, at + 0x78);
		fEntries.push_back(e);
	}
	if (fEntries.empty() || fEntries[0].type != 5) {
		error = "no root entry";
		return false;
	}
	// the mini stream (the root's) and its FAT
	std::vector<uint8_t> miniFAT = Chain(U32(data, 0x3c), UINT64_MAX, false);
	for (size_t i = 0; i + 4 <= miniFAT.size(); i += 4)
		fMiniFAT.push_back(U32(miniFAT, i));
	fMiniStream = Chain(fEntries[0].start, fEntries[0].size, false);
	return true;
}


std::vector<uint8_t>
CompoundFile::Chain(uint32_t first, uint64_t size, bool mini) const
{
	std::vector<uint8_t> out;
	const std::vector<uint32_t>& fat = mini ? fMiniFAT : fFAT;
	uint32_t unit = mini ? fMiniSectorSize : fSectorSize;
	for (uint32_t s = first, k = 0; s != kEndOfChain && s != kFree && s < fat.size()
			&& out.size() < size && k < 1000000; s = fat[s], k++) {
		if (mini) {
			size_t at = size_t(s) * unit;
			if (at + unit > fMiniStream.size())
				break;
			out.insert(out.end(), fMiniStream.begin() + long(at), fMiniStream.begin() + long(at + unit));
		} else {
			size_t at = size_t(s + 1) * unit;
			if (at + unit > fData->size())
				break;
			out.insert(out.end(), fData->begin() + long(at), fData->begin() + long(at + unit));
		}
	}
	if (size != UINT64_MAX && out.size() > size)
		out.resize(size_t(size));
	return out;
}


bool
CompoundFile::HasStream(const std::string& name) const
{
	for (const Entry& e : fEntries)
		if (e.type == 2 && e.name == name)
			return true;
	return false;
}


bool
CompoundFile::Stream(const std::string& name, std::vector<uint8_t>& out) const
{
	for (const Entry& e : fEntries)
		if (e.type == 2 && e.name == name) {
			out = Chain(e.start, e.size, e.size < fMiniCutoff);
			return true;
		}
	return false;
}


namespace {

// the translator's FunctionNames and FunctionArgs (Excel's function
// numbers; the names Gobe Productive gives them, the fixed argument counts,
// 255 for a variable count): those it knows
struct ExcelFunction {
	int index;
	const char* name;
	int arguments;
};
const ExcelFunction kFunctions[] = {
	{0, "COUNT", 255}, {1, "IF", 255}, {2, "ISNA", 1}, {3, "ISERROR", 1}, {4, "SUM", 255},
	{5, "AVERAGE", 255}, {6, "MIN", 255}, {7, "MAX", 255}, {8, "ROW", 255}, {9, "COL", 255},
	{10, "NA", 0}, {11, "NPV", 255}, {12, "STDEV", 255}, {13, "DOLLAR", 255}, {14, "FIXED", 255},
	{15, "SIN", 1}, {16, "COS", 1}, {17, "TAN", 1}, {18, "ATAN", 1}, {19, "PI", 0},
	{20, "SQRT", 1}, {21, "EXP", 1}, {22, "LN", 1}, {23, "LOG10", 1}, {24, "ABS", 1},
	{25, "INT", 1}, {26, "SIGN", 1}, {27, "ROUND", 2}, {28, "LOOKUP", 255}, {29, "INDEX", 255},
	{30, "REPT", 2}, {31, "MID", 3}, {32, "LEN", 1}, {33, "VALUE", 1}, {34, "TRUE", 0},
	{35, "FALSE", 0}, {36, "AND", 255}, {37, "OR", 255}, {38, "NOT", 1}, {39, "MOD", 2},
	{40, "DCOUNT", 3}, {41, "DSUM", 3}, {42, "DAVERAGE", 3}, {43, "DMIN", 3}, {44, "DMAX", 3},
	{45, "DSTDEV", 3}, {46, "VAR", 255}, {47, "DVAR", 3}, {48, "TEXT", 2}, {49, "LINEST", 255},
	{50, "TREND", 255}, {51, "LOGEST", 255}, {52, "GROWTH", 255}, {56, "PV", 255},
	{57, "FV", 255}, {58, "NPER", 255}, {59, "PMT", 255}, {60, "RATE", 255}, {61, "MIRR", 3},
	{62, "IRR", 255}, {63, "RAND", 0}, {64, "MATCH", 255}, {65, "DATE", 3}, {66, "TIME", 3},
	{67, "DAY", 1}, {68, "MONTH", 1}, {69, "YEAR", 1}, {70, "WEEKDAY", 2}, {71, "HOUR", 1},
	{72, "MINUTE", 1}, {73, "SECOND", 1}, {74, "NOW", 0}, {75, "AREAS", 1}, {76, "ROWS", 1},
	{77, "COLUMNS", 1}, {78, "OFFSET", 255}, {82, "SEARCH", 255}, {83, "TRANSPOSE", 1},
	{86, "TYPE", 1}, {97, "ATAN2", 2}, {98, "ASIN", 1}, {99, "ACOS", 1}, {100, "CHOOSE", 255},
	{101, "HLOOKUP", 255}, {102, "VLOOKUP", 255}, {105, "ISREF", 1}, {109, "LOG", 255},
	{111, "CHAR", 1}, {112, "LOWER", 1}, {113, "UPPER", 1}, {114, "PROPER", 1},
	{115, "LEFT", 255}, {116, "RIGHT", 255}, {117, "EXACT", 2}, {118, "TRIM", 1},
	{119, "REPLACE", 4}, {120, "SUBSTITUTE", 255}, {121, "CODE", 1}, {124, "FIND", 255},
	{125, "CELL", 2}, {126, "ISERR", 1}, {127, "ISTEXT", 1}, {128, "ISNUMBER", 1},
	{129, "ISBLANK", 1}, {130, "T", 1}, {131, "N", 1}, {140, "DATEVALUE", 1},
	{141, "TIMEVALUE", 1}, {142, "SLN", 3}, {143, "SYD", 4}, {144, "DDB", 255},
	{149, "REGISTER", 255}, {150, "CALL", 255}, {162, "CLEAN", 1}, {163, "MDETERM", 1},
	{164, "MINVERSE", 1}, {165, "MMULT", 2}, {167, "IPMT", 255}, {168, "PPMT", 255},
	{169, "COUNTA", 255}, {183, "PRODUCT", 255}, {184, "FACT", 1}, {189, "DPRODUCT", 3},
	{190, "ISNONTEXT", 1}, {193, "STDEVP", 255}, {194, "VARP", 255}, {195, "DSTDEVP", 3},
	{196, "DVARP", 3}, {197, "TRUNC", 255}, {198, "ISLOGICAL", 1}, {199, "DCOUNTA", 3},
	{212, "ROUNDUP", 2}, {213, "ROUNDDOWN", 2}, {214, "ASC", 1}, {216, "RANK", 255},
	{219, "ADDRESS", 255}, {220, "DAYS360", 255}, {221, "TODAY", 0}, {222, "VDB", 255},
	{227, "MEDIAN", 255}, {228, "SUMPRODUCT", 255}, {229, "SINH", 1}, {230, "COSH", 1},
	{231, "TANH", 1}, {232, "ASINH", 1}, {233, "ACOSH", 1}, {234, "ATANH", 1}, {235, "DGET", 3},
	{244, "INFO", 1}, {247, "DB", 255}, {252, "FREQUENCY", 2}, {267, "REGISTERID", 255},
	{269, "AVEDEV", 255}, {270, "BETADIST", 255}, {271, "GAMMALN", 1}, {272, "BETAINV", 255},
	{273, "BINOMDIST", 4}, {274, "CHIDIST", 2}, {275, "CHIINV", 2}, {276, "COMBIN", 2},
	{277, "CONFIDENCE", 3}, {278, "CRITBINOM", 3}, {279, "EVEN", 1}, {280, "EXPONDIST", 3},
	{281, "FDIST", 3}, {282, "FINV", 3}, {283, "FISHER", 1}, {284, "FISHERINV", 1},
	{285, "FLOOR", 2}, {286, "GAMMADIST", 4}, {287, "GAMMAINV", 3}, {288, "CEILING", 2},
	{289, "HYPGEOMDIST", 4}, {290, "LOGNORMDIST", 3}, {291, "LOGINV", 3},
	{292, "NEGBINOMDIST", 3}, {293, "NORMDIST", 4}, {294, "NORMSDIST", 1}, {295, "NORMINV", 3},
	{296, "NORMSINV", 1}, {297, "STANDARDIZE", 3}, {298, "ODD", 1}, {299, "PERMUT", 2},
	{300, "POISSON", 3}, {301, "TDIST", 3}, {302, "WEIBULL", 4}, {303, "SUMXMY2", 2},
	{304, "SUMX2MY2", 2}, {305, "SUMX2PY2", 2}, {306, "CHITEST", 2}, {307, "CORREL", 2},
	{308, "COVAR", 2}, {309, "FORECAST", 3}, {310, "FTEST", 2}, {311, "INTERCEPT", 2},
	{312, "PEARSON", 2}, {313, "RSQ", 2}, {314, "STEYX", 2}, {315, "SLOPE", 2}, {316, "TTEST", 4},
	{317, "PROB", 255}, {318, "DEVSQ", 255}, {319, "GEOMEAN", 255}, {320, "HARMEAN", 255},
	{321, "SUMSQ", 255}, {322, "KURT", 255}, {323, "SKEW", 255}, {324, "ZTEST", 255},
	{325, "LARGE", 2}, {326, "SMALL", 2}, {327, "QUARTILE", 2}, {328, "PERCENTILE", 2},
	{329, "PERCENTRANK", 255}, {330, "MODE", 255}, {331, "TRIMMEAN", 2}, {332, "TINV", 2},
	{336, "CONCAT", 255}, {337, "POWER", 2}, {342, "RADIANS", 1}, {343, "DEGREES", 1},
	{344, "SUBTOTAL", 2}, {345, "SUMIF", 255}, {346, "COUNTIF", 2}, {347, "COUNTBLANK", 1},
	{354, "ROMAN", 255}
};


const ExcelFunction*
Function(int index)
{
	for (const ExcelFunction& f : kFunctions)
		if (f.index == index)
			return &f;
	return nullptr;
}


std::string
ColumnName(int column)
{
	std::string s;
	for (column++; column > 0; column = (column - 1) / 26)
		s.insert(s.begin(), char('A' + (column - 1) % 26));
	return s;
}


// a reference as Gobe writes it ('$' before absolute parts)
std::string
RefName(int row, int column, bool rowRelative, bool columnRelative)
{
	return std::string(columnRelative ? "" : "$") + ColumnName(column) + (rowRelative ? "" : "$")
		+ std::to_string(row + 1);
}


const char* const kErrorNames[] = {"#NULL!", "#DIV/0!", "#VALUE!", "#REF!", "#NAME?", "#NUM!",
	"#N/A"};
const uint8_t kErrorCodes[] = {0x00, 0x07, 0x0f, 0x17, 0x1d, 0x24, 0x2a};


std::string
ErrorName(uint8_t code)
{
	for (size_t i = 0; i < sizeof kErrorCodes; i++)
		if (kErrorCodes[i] == code)
			return kErrorNames[i];
	return "#VALUE!";
}


// a BIFF string: BIFF8 a length, flags (bit 0: 16-bit characters; 8: rich
// text runs; 4: Far East data) then the characters; BIFF5 a length and
// bytes (Latin-1 here)
std::string
BiffString(const std::vector<uint8_t>& d, size_t& at, bool biff8, bool longLength)
{
	size_t length = longLength ? U16(d, at) : (at < d.size() ? d[at] : 0);
	at += longLength ? 2 : 1;
	std::string out;
	auto append = [&](uint32_t c) {
		if (c < 0x80)
			out += char(c);
		else if (c < 0x800) {
			out += char(0xc0 | (c >> 6));
			out += char(0x80 | (c & 0x3f));
		} else {
			out += char(0xe0 | (c >> 12));
			out += char(0x80 | ((c >> 6) & 0x3f));
			out += char(0x80 | (c & 0x3f));
		}
	};
	if (!biff8) {
		for (size_t i = 0; i < length && at < d.size(); i++)
			append(d[at++]);
		return out;
	}
	uint8_t flags = at < d.size() ? d[at] : 0;
	at++;
	uint16_t runs = 0;
	uint32_t farEast = 0;
	if (flags & 8) {
		runs = U16(d, at);
		at += 2;
	}
	if (flags & 4) {
		farEast = U32(d, at);
		at += 4;
	}
	for (size_t i = 0; i < length && at < d.size(); i++) {
		if (flags & 1) {
			append(U16(d, at));
			at += 2;
		} else
			append(d[at++]);
	}
	at += size_t(runs) * 4 + farEast;
	return out;
}


// Excel's built-in number formats (by FORMAT index) that a file needn't
// write, in Gobe's syntax
std::string
BuiltinFormat(uint16_t index)
{
	switch (index) {
		case 1: return "0";
		case 2: return "0.00";
		case 3: return "#,##0";
		case 4: return "#,##0.00";
		case 5: return "$#,##0;($#,##0)";
		case 6: return "$#,##0;[Red]($#,##0)";
		case 7: return "$#,##0.00;($#,##0.00)";
		case 8: return "$#,##0.00;[Red]($#,##0.00)";
		case 9: return "0%";
		case 10: return "0.00%";
		case 11: return "0.00E+00";
		case 14: return "m/d/yy";
		case 15: return "d-mmm-yy";
		case 16: return "d-mmm";
		case 17: return "mmm-yy";
		case 18: return "h:mm AM/PM";
		case 19: return "h:mm:ss AM/PM";
		case 20: return "h:mm";
		case 21: return "h:mm:ss";
		case 22: return "m/d/yy h:mm";
		case 37: return "#,##0;(#,##0)";
		case 38: return "#,##0;[Red](#,##0)";
		case 39: return "#,##0.00;(#,##0.00)";
		case 40: return "#,##0.00;[Red](#,##0.00)";
		case 45: return "mm:ss";
		case 46: return "h:mm:ss";
		case 47: return "mm:ss";
		case 48: return "##0.0E+0";
		default: return "";
	}
}


// a format string as Excel writes it, as Gobe's: quotes and backslashes
// of literal characters taken out where Gobe doesn't need them
std::string
GobeFormat(const std::string& excel)
{
	std::string out;
	for (size_t i = 0; i < excel.size(); i++) {
		if (excel[i] == '\\' && i + 1 < excel.size()) {
			out += excel[++i];
			continue;
		}
		out += excel[i];
	}
	return out == "General" ? "" : out;
}


// TExcelParser: a formula's tokens (BIFF8's or BIFF5's) as Gobe's text; ""
// if one can't be (an unknown function, a name, another sheet)
std::string
FormulaText(const std::vector<uint8_t>& d, size_t at, size_t length, int row, int column, bool biff8,
	const std::vector<std::string>& names)
{
	std::vector<std::string> stack;
	size_t end = std::min(d.size(), at + length);
	auto binary = [&](const char* op) {
		if (stack.size() < 2)
			return false;
		std::string b = stack.back();
		stack.pop_back();
		stack.back() += op + b;
		return true;
	};
	auto ref = [&](size_t& p, bool relativeToCell, int& r, int& c, bool& rr, bool& cr) {
		if (biff8) {
			uint16_t rowField = U16(d, p), colField = U16(d, p + 2);
			p += 4;
			rr = (colField & 0x8000) != 0;
			cr = (colField & 0x4000) != 0;
			r = rowField;
			c = colField & 0xff;
			if (relativeToCell) {
				if (rr)
					r = (row + int16_t(rowField)) & 0xffff;
				if (cr)
					c = (column + int8_t(colField & 0xff)) & 0xff;
			}
		} else {
			uint16_t rowField = U16(d, p);
			uint8_t colField = d[p + 2];
			p += 3;
			rr = (rowField & 0x8000) != 0;
			cr = (rowField & 0x4000) != 0;
			r = rowField & 0x3fff;
			c = colField;
			if (relativeToCell) {
				if (rr)
					r = (row + int16_t(rowField << 2) / 4) & 0x3fff;
				if (cr)
					c = (column + int8_t(colField)) & 0xff;
			}
		}
	};
	size_t p = at;
	while (p < end) {
		uint8_t token = d[p++];
		uint8_t base = token >= 0x20 ? uint8_t((token & 0x1f) | 0x20) : token;
		switch (token) {
			case 0x03: if (!binary("+")) return ""; continue;
			case 0x04: if (!binary("-")) return ""; continue;
			case 0x05: if (!binary("*")) return ""; continue;
			case 0x06: if (!binary("/")) return ""; continue;
			case 0x07: if (!binary("^")) return ""; continue;
			case 0x08: if (!binary("&")) return ""; continue;
			case 0x09: if (!binary("<")) return ""; continue;
			case 0x0a: if (!binary("<=")) return ""; continue;
			case 0x0b: if (!binary("=")) return ""; continue;
			case 0x0c: if (!binary(">=")) return ""; continue;
			case 0x0d: if (!binary(">")) return ""; continue;
			case 0x0e: if (!binary("<>")) return ""; continue;
			case 0x12:		// unary plus
				continue;
			case 0x13:
				if (stack.empty())
					return "";
				stack.back() = "-" + stack.back();
				continue;
			case 0x14:
				if (stack.empty())
					return "";
				stack.back() += "%";
				continue;
			case 0x15:
				if (stack.empty())
					return "";
				stack.back() = "(" + stack.back() + ")";
				continue;
			case 0x16:
				stack.push_back("");
				continue;
			case 0x17: {
				size_t q = p;
				std::string s = BiffString(d, q, biff8, false);
				p = q;
				stack.push_back("\"" + s + "\"");
				continue;
			}
			case 0x19: {
				// tAttr: tAttrSum is SUM(x); the others (if, choose, skip,
				// space, volatile) only steer evaluation
				uint8_t kind = d[p];
				p++;
				if (kind & 0x04) {		// tAttrChoose: a jump table
					uint16_t count = U16(d, p);
					p += 2 + (size_t(count) + 1) * 2;
					continue;
				}
				p += 2;
				if (kind & 0x10) {
					if (stack.empty())
						return "";
					stack.back() = "SUM(" + stack.back() + ")";
				}
				continue;
			}
			case 0x1c:
				stack.push_back(ErrorName(d[p++]));
				continue;
			case 0x1d:
				stack.push_back(d[p++] ? "TRUE" : "FALSE");
				continue;
			case 0x1e:
				stack.push_back(std::to_string(U16(d, p)));
				p += 2;
				continue;
			case 0x1f: {
				char text[32];
				snprintf(text, sizeof text, "%.15g", F64(d, p));
				p += 8;
				stack.push_back(text);
				continue;
			}
			default:
				break;
		}
		switch (base) {
			case 0x21:		// tFunc: a fixed argument count
			case 0x22: {	// tFuncVar
				int count = -1;
				if (base == 0x22)
					count = d[p++] & 0x7f;
				int index = biff8 || base == 0x22 ? U16(d, p) & 0x7fff : U16(d, p);
				p += 2;
				const ExcelFunction* f = Function(index);
				if (f == nullptr)
					return "";
				if (count < 0)
					count = f->arguments == 255 ? 0 : f->arguments;
				if (int(stack.size()) < count)
					return "";
				std::string args;
				for (size_t i = stack.size() - size_t(count); i < stack.size(); i++)
					args += (i == stack.size() - size_t(count) ? "" : ",") + stack[i];
				stack.resize(stack.size() - size_t(count));
				stack.push_back(std::string(f->name) + "(" + args + ")");
				continue;
			}
			case 0x24:		// tRef
			case 0x2c: {	// tRefN (shared formulas: relative to the cell)
				int r, c;
				bool rr, cr;
				ref(p, base == 0x2c, r, c, rr, cr);
				stack.push_back(RefName(r, c, rr, cr));
				continue;
			}
			case 0x25:		// tArea
			case 0x2d: {	// tAreaN
				int r1, r2, c1, c2;
				bool rr1, cr1, rr2, cr2;
				if (biff8) {
					uint16_t row1 = U16(d, p), row2 = U16(d, p + 2);
					uint16_t col1 = U16(d, p + 4), col2 = U16(d, p + 6);
					p += 8;
					rr1 = col1 & 0x8000;
					cr1 = col1 & 0x4000;
					rr2 = col2 & 0x8000;
					cr2 = col2 & 0x4000;
					r1 = row1;
					r2 = row2;
					c1 = col1 & 0xff;
					c2 = col2 & 0xff;
					if (base == 0x2d) {
						if (rr1) r1 = (row + int16_t(row1)) & 0xffff;
						if (rr2) r2 = (row + int16_t(row2)) & 0xffff;
						if (cr1) c1 = (column + int8_t(col1 & 0xff)) & 0xff;
						if (cr2) c2 = (column + int8_t(col2 & 0xff)) & 0xff;
					}
				} else {
					uint16_t row1 = U16(d, p), row2 = U16(d, p + 2);
					uint8_t col1 = d[p + 4], col2 = d[p + 5];
					p += 6;
					rr1 = row1 & 0x8000;
					cr1 = row1 & 0x4000;
					rr2 = row2 & 0x8000;
					cr2 = row2 & 0x4000;
					r1 = row1 & 0x3fff;
					r2 = row2 & 0x3fff;
					c1 = col1;
					c2 = col2;
				}
				stack.push_back(RefName(r1, c1, rr1, cr1) + ".." + RefName(r2, c2, rr2, cr2));
				continue;
			}
			case 0x23: {	// tName: a defined name (from 1) Gobe has too
				uint16_t index = U16(d, p);
				p += biff8 ? 4 : 14;
				if (index == 0 || index > names.size() || names[index - 1].empty())
					return "";
				stack.push_back(names[index - 1]);
				continue;
			}
			case 0x39:		// tNameX
			case 0x3a:		// tRef3d
			case 0x3b:		// tArea3d
			case 0x2a:		// tRefErr
			case 0x2b:		// tAreaErr
			case 0x26: case 0x27: case 0x28: case 0x29:	// memory tokens
			case 0x20:		// tArray
			default:
				return "";
		}
	}
	if (stack.size() != 1)
		return "";
	return "=" + stack[0];
}

} // namespace


bool
ReadExcel(const std::vector<uint8_t>& file, ExcelBook& book, std::string& error)
{
	CompoundFile ole;
	if (!ole.Open(file, error))
		return false;
	std::vector<uint8_t> stream;
	if (!ole.Stream("Workbook", stream) && !ole.Stream("Book", stream)) {
		error = "no workbook in the file";
		return false;
	}
	// the records, CONTINUE records joined to the one before (but SST's
	// strings split across them are read as the record says)
	struct Record {
		uint16_t type;
		std::vector<uint8_t> data;
		size_t offset;
	};
	std::vector<Record> records;
	for (size_t at = 0; at + 4 <= stream.size();) {
		uint16_t type = U16(stream, at), length = U16(stream, at + 2);
		size_t start = at;
		at += 4;
		std::vector<uint8_t> data(stream.begin() + long(std::min(at, stream.size())),
			stream.begin() + long(std::min(at + length, stream.size())));
		at += length;
		if (type == 0x3c && !records.empty()) {
			// a CONTINUE; in an SST a string's characters restart with a flags
			// byte, which BiffString can't see across the join: kept apart
			if (records.back().type == 0xfc)
				records.push_back({0x3c, data, start});
			else
				records.back().data.insert(records.back().data.end(), data.begin(), data.end());
			continue;
		}
		records.push_back({type, data, start});
	}
	if (records.empty() || (records[0].type != 0x809 && records[0].type != 0x409
			&& records[0].type != 0x209 && records[0].type != 0x009)) {
		error = "no BOF record";
		return false;
	}
	book.version = U16(records[0].data, 0);
	bool biff8 = book.version == 0x0600;
	if (book.version != 0x0600 && book.version != 0x0500) {
		error = "only Excel 5 and later workbooks are read";
		return false;
	}
	// the globals
	std::vector<std::string> sst;
	std::map<uint16_t, std::string> formats;
	std::vector<uint16_t> xfFormat;
	std::vector<uint16_t> externSheets;
	struct RawName {
		std::string name;
		std::vector<uint8_t> definition;
	};
	std::vector<RawName> rawNames;
	struct SheetAt {
		std::string name;
		uint32_t offset;
		bool worksheet;
	};
	std::vector<SheetAt> sheets;
	size_t r = 1;
	for (; r < records.size() && records[r].type != 0x0a; r++) {
		const Record& rec = records[r];
		const std::vector<uint8_t>& d = rec.data;
		size_t at = 0;
		switch (rec.type) {
			case 0x22:		// DATEMODE
				book.date1904 = U16(d, 0) != 0;
				break;
			case 0x85: {	// BOUNDSHEET
				at = 6;
				std::string name = BiffString(d, at, biff8, false);
				sheets.push_back({name, U32(d, 0), (d.size() > 5 ? d[5] : 0) == 0});
				break;
			}
			case 0x41e: {	// FORMAT
				at = 2;
				formats[U16(d, 0)] = GobeFormat(BiffString(d, at, biff8, biff8));
				break;
			}
			case 0xe0:		// XF: its FORMAT's index
				xfFormat.push_back(U16(d, 2));
				break;
			case 0x17:		// EXTERNSHEET (BIFF8): the sheet each XTI names
				if (biff8)
					for (size_t i = 0, n = U16(d, 0); i < n && 2 + i * 6 + 6 <= d.size(); i++)
						externSheets.push_back(U16(d, 2 + i * 6 + 2));
				break;
			case 0x18: {	// NAME (BIFF8): its text and definition
				if (!biff8) {
					rawNames.push_back({"", {}});
					break;
				}
				uint16_t options = U16(d, 0);
				size_t nameLength = d.size() > 3 ? d[3] : 0;
				size_t formulaLength = U16(d, 4);
				at = 14;
				uint8_t flags = at < d.size() ? d[at++] : 0;
				std::string name;
				for (size_t i = 0; i < nameLength && at < d.size(); i++) {
					uint16_t ch = (flags & 1) ? U16(d, at) : d[at];
					at += (flags & 1) ? 2 : 1;
					name += ch < 0x80 ? char(ch) : '?';
				}
				// (built-in names, Print_Area and the like, aren't kept)
				if (options & 0x20)
					name.clear();
				rawNames.push_back({name, std::vector<uint8_t>(d.begin() + long(std::min(at,
					d.size())), d.begin() + long(std::min(at + formulaLength, d.size())))});
				break;
			}
			case 0xfc: {	// SST, with its CONTINUE records
				uint32_t count = U32(d, 4);
				std::vector<uint8_t> all = d;
				std::vector<size_t> breaks;
				for (size_t k = r + 1; k < records.size() && records[k].type == 0x3c; k++) {
					breaks.push_back(all.size());
					all.insert(all.end(), records[k].data.begin(), records[k].data.end());
				}
				at = 8;
				for (uint32_t i = 0; i < count && at < all.size(); i++) {
					// a string whose characters go on in a CONTINUE gets a new
					// flags byte there
					size_t length = U16(all, at);
					size_t q = at + 2;
					uint8_t flags = all[q++];
					uint16_t runs = 0;
					uint32_t farEast = 0;
					if (flags & 8) {
						runs = U16(all, q);
						q += 2;
					}
					if (flags & 4) {
						farEast = U32(all, q);
						q += 4;
					}
					std::string s;
					for (size_t c = 0; c < length && q < all.size(); c++) {
						for (size_t b : breaks)
							if (q == b) {
								flags = (flags & ~1) | (all[q] & 1);
								q++;
							}
						uint32_t ch;
						if (flags & 1) {
							ch = U16(all, q);
							q += 2;
						} else
							ch = all[q++];
						if (ch < 0x80)
							s += char(ch);
						else if (ch < 0x800) {
							s += char(0xc0 | (ch >> 6));
							s += char(0x80 | (ch & 0x3f));
						} else {
							s += char(0xe0 | (ch >> 12));
							s += char(0x80 | ((ch >> 6) & 0x3f));
							s += char(0x80 | (ch & 0x3f));
						}
					}
					q += size_t(runs) * 4 + farEast;
					sst.push_back(s);
					at = q;
				}
				break;
			}
			default:
				break;
		}
	}
	// the names that are one range of a worksheet (tRef3d or tArea3d)
	std::vector<size_t> worksheetIndex(sheets.size(), size_t(-1));
	for (size_t i = 0, n = 0; i < sheets.size(); i++)
		if (sheets[i].worksheet)
			worksheetIndex[i] = n++;
	std::vector<std::string> nameTexts;
	for (const RawName& raw : rawNames) {
		const std::vector<uint8_t>& t = raw.definition;
		std::string text;
		if (!raw.name.empty() && !t.empty()) {
			uint8_t token = uint8_t((t[0] & 0x1f) | 0x20);
			uint16_t xti = U16(t, 1);
			size_t tab = xti < externSheets.size() ? externSheets[xti] : size_t(-1);
			if ((token == 0x3a && t.size() >= 7) || (token == 0x3b && t.size() >= 11)) {
				ExcelName n;
				n.name = raw.name;
				n.row1 = U16(t, 3);
				if (token == 0x3a) {
					n.row2 = n.row1;
					n.column1 = n.column2 = U16(t, 5) & 0xff;
				} else {
					n.row2 = U16(t, 5);
					n.column1 = U16(t, 7) & 0xff;
					n.column2 = U16(t, 9) & 0xff;
				}
				if (tab < worksheetIndex.size() && worksheetIndex[tab] != size_t(-1)) {
					n.sheet = worksheetIndex[tab];
					book.names.push_back(n);
					text = raw.name;
				}
			}
		}
		nameTexts.push_back(text);
	}
	auto formatOf = [&](uint16_t xf) -> std::string {
		if (xf >= xfFormat.size())
			return "";
		uint16_t index = xfFormat[xf];
		auto f = formats.find(index);
		return f != formats.end() ? f->second : BuiltinFormat(index);
	};
	// AdjustTimeValue: a number in a date or time format as Gobe's days
	auto value = [&](double v, const std::string& format) {
		if (format.empty() || !IsDateNumberFormat(format))
			return v;
		// (TExcelFormatsTable::IsDateFormat's time-only formats: hours or
		// seconds, no days or years)
		bool timeOnly = format.find_first_of("dDyY") == std::string::npos
			&& format.find_first_of("hHsS") != std::string::npos;
		if (timeOnly)
			v -= std::floor(v);
		if (v <= -1 || v >= 1)
			v -= book.date1904 ? 24107 : 25569;
		return v;
	};
	// each worksheet from its BOF
	for (const SheetAt& at : sheets) {
		if (!at.worksheet)
			continue;
		ExcelSheet sheet;
		sheet.name = at.name;
		size_t k = 0;
		while (k < records.size() && records[k].offset != at.offset)
			k++;
		if (k == records.size())
			continue;
		ExcelCell* lastFormula = nullptr;
		std::map<std::pair<uint16_t, uint16_t>, std::vector<uint8_t>> shared;
		for (k++; k < records.size() && records[k].type != 0x0a; k++) {
			const std::vector<uint8_t>& d = records[k].data;
			uint16_t rowAt = U16(d, 0), colAt = U16(d, 2), xf = U16(d, 4);
			ExcelCell cell;
			cell.row = rowAt;
			cell.column = colAt;
			cell.format = formatOf(xf);
			switch (records[k].type) {
				case 0x203:		// NUMBER
					cell.kind = ExcelCell::kNumber;
					cell.number = value(F64(d, 6), cell.format);
					break;
				case 0x27e: {	// RK
					uint32_t rk = U32(d, 6);
					double v;
					if (rk & 2)
						v = double(int32_t(rk) >> 2);
					else {
						uint64_t bits = uint64_t(rk & 0xfffffffc) << 32;
						memcpy(&v, &bits, 8);
					}
					if (rk & 1)
						v /= 100;
					cell.kind = ExcelCell::kNumber;
					cell.number = value(v, cell.format);
					break;
				}
				case 0xbd: {	// MULRK
					uint16_t last = U16(d, d.size() - 2);
					for (uint16_t c = colAt, i = 0; c <= last && size_t(4 + i * 6 + 6) <= d.size(); c++, i++) {
						ExcelCell m;
						m.row = rowAt;
						m.column = c;
						m.format = formatOf(U16(d, 4 + size_t(i) * 6));
						uint32_t rk = U32(d, 6 + size_t(i) * 6);
						double v;
						if (rk & 2)
							v = double(int32_t(rk) >> 2);
						else {
							uint64_t bits = uint64_t(rk & 0xfffffffc) << 32;
							memcpy(&v, &bits, 8);
						}
						if (rk & 1)
							v /= 100;
						m.kind = ExcelCell::kNumber;
						m.number = value(v, m.format);
						sheet.cells.push_back(m);
					}
					continue;
				}
				case 0x204:		// LABEL
				case 0xd6: {	// RSTRING
					size_t q = 6;
					cell.kind = ExcelCell::kString;
					cell.text = BiffString(d, q, biff8, true);
					break;
				}
				case 0xfd: {	// LABELSST
					uint32_t i = U32(d, 6);
					cell.kind = ExcelCell::kString;
					cell.text = i < sst.size() ? sst[i] : "";
					break;
				}
				case 0x205:		// BOOLERR
					if (d.size() > 7 && d[7]) {
						cell.kind = ExcelCell::kError;
						cell.text = ErrorName(d[6]);
					} else {
						cell.kind = ExcelCell::kBool;
						cell.number = d.size() > 6 ? d[6] : 0;
					}
					break;
				case 0x06: {	// FORMULA: its value, then its tokens
					if (U16(d, 12) == 0xffff) {
						uint8_t kind = d[6];
						if (kind == 1) {
							cell.kind = ExcelCell::kBool;
							cell.number = d[8];
						} else if (kind == 2) {
							cell.kind = ExcelCell::kError;
							cell.text = ErrorName(d[8]);
						} else if (kind == 0)
							cell.kind = ExcelCell::kString;	// the STRING record's
						else
							cell.kind = ExcelCell::kBlank;
					} else {
						cell.kind = ExcelCell::kNumber;
						cell.number = value(F64(d, 6), cell.format);
					}
					// (BIFF5 and BIFF8 alike: the tokens' size at 20, the tokens at 22)
					size_t length = U16(d, 20), tokens = 22;
					std::vector<uint8_t> formula(d.begin() + long(std::min(tokens, d.size())),
						d.begin() + long(std::min(tokens + length, d.size())));
					// tExp: the shared formula's (SHRFMLA, after this record)
					if (!formula.empty() && formula[0] == 0x01 && formula.size() >= 5) {
						uint16_t sr = U16(formula, 1), sc = biff8 ? U16(formula, 3) : formula[3];
						for (size_t s = k + 1; s < records.size() && s < k + 3; s++)
							if (records[s].type == 0x4bc) {
								const std::vector<uint8_t>& sd = records[s].data;
								size_t off = biff8 ? 8 : 8;
								size_t len = U16(sd, off);
								shared[{sr, sc}] = std::vector<uint8_t>(sd.begin()
									+ long(std::min(off + 2, sd.size())),
									sd.begin() + long(std::min(off + 2 + len, sd.size())));
							}
						auto found = shared.find({sr, sc});
						if (found != shared.end())
							formula = found->second;
					}
					cell.formula = FormulaText(formula, 0, formula.size(), rowAt, colAt, biff8,
						nameTexts);
					cell.badFormula = cell.formula.empty();
					sheet.cells.push_back(cell);
					lastFormula = cell.kind == ExcelCell::kString ? &sheet.cells.back() : nullptr;
					continue;
				}
				case 0x207:		// STRING: the formula before's text
					if (lastFormula != nullptr) {
						size_t q = 0;
						lastFormula->text = BiffString(d, q, biff8, true);
						lastFormula = nullptr;
					}
					continue;
				case 0x7d: {	// COLINFO: 1/256 of a character's width
					uint16_t first = U16(d, 0), last = U16(d, 2), width = U16(d, 4);
					for (uint32_t c = first; c <= last && c < 256; c++)
						sheet.columnWidths[uint16_t(c)] = float(width) / 256.0f * 7.0f;
					continue;
				}
				case 0x200:		// DIMENSIONS
					if (biff8) {
						sheet.rows = uint16_t(std::min<uint32_t>(U32(d, 4), 65535));
						sheet.columns = U16(d, 10);
					} else {
						sheet.rows = U16(d, 2);
						sheet.columns = U16(d, 6);
					}
					continue;
				default:
					continue;
			}
			sheet.cells.push_back(cell);
		}
		book.sheets.push_back(std::move(sheet));
	}
	if (book.sheets.empty()) {
		error = "no worksheets";
		return false;
	}
	return true;
}


bool
ImportExcel(const std::string& path, Document& doc, std::string& error, std::string* notice)
{
	std::ifstream in(path, std::ios::binary);
	if (!in) {
		error = "cannot open " + path;
		return false;
	}
	std::vector<uint8_t> file((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	ExcelBook book;
	if (!ReadExcel(file, book, error))
		return false;
	if (!CreateBlankSpreadsheet(doc, error))
		return false;
	int bad = 0;
	// the sheets first, then the names (their formulas need them), then
	// the cells
	std::vector<size_t> partOf;
	for (size_t s = 0; s < book.sheets.size(); s++) {
		const ExcelSheet& sheet = book.sheets[s];
		size_t index = 0;
		if (s > 0) {
			int added = doc.AddSheet(Sig("SPRD"), sheet.name, error);
			if (added < 0)
				return false;
			index = size_t(added);
		} else if (!sheet.name.empty()) {
			doc.sheets.sheets[0].name = sheet.name;
			doc.sheetsChanged = true;
		}
		partOf.push_back(doc.sheets.sheets[index].mainPart);
	}
	for (const ExcelName& name : book.names)
		if (name.sheet < partOf.size() && ValidName(name.name)) {
			SSRange range{{name.row1, name.column1}, {name.row2, name.column2}};
			NameCellSelection(doc, partOf[name.sheet], name.name, {range}, range.from);
		}
	for (size_t s = 0; s < book.sheets.size(); s++) {
		const ExcelSheet& sheet = book.sheets[s];
		size_t part = partOf[s];
		SSPart* ss = doc.EditableSpreadsheetPart(part);
		if (ss == nullptr)
			continue;
		auto isName = [&](const std::string& name) {
			return doc.NamedSelection(name, int(part)) != nullptr;
		};
		std::string ignored;
		for (const ExcelCell& cell : sheet.cells) {
			if (cell.kind == ExcelCell::kBlank && cell.formula.empty())
				continue;
			if (!cell.format.empty())
				ApplyCellProperty(doc, part, cell.row, cell.column, cell.row, cell.column,
					NumberFormatProperty(cell.format));
			ss = doc.EditableSpreadsheetPart(part);
			if (!cell.formula.empty() && SetCellInput(*ss, cell.row, cell.column, cell.formula, isName,
					ignored))
				continue;
			if (!cell.formula.empty() || cell.badFormula)
				bad++;
			switch (cell.kind) {
				case ExcelCell::kNumber: {
					char text[32];
					snprintf(text, sizeof text, "%.17g", cell.number);
					SetCellInput(*ss, cell.row, cell.column, text, nullptr, ignored);
					break;
				}
				case ExcelCell::kBool:
					SetCellInput(*ss, cell.row, cell.column, cell.number != 0 ? "TRUE" : "FALSE",
						nullptr, ignored);
					break;
				case ExcelCell::kString:
				case ExcelCell::kError:
					// text as text, whatever it looks like
					if (cell.text.empty())
						break;
					SetCellInput(*ss, cell.row, cell.column, "x", nullptr, ignored);
					for (SSCell& c : ss->cells)
						if (c.id.a == cell.row && c.id.b == cell.column) {
							if (cell.kind == ExcelCell::kString) {
								c.kind = Sig("Strn");
								c.text = cell.text;
							} else {
								int code = 0;
								for (int e = 1; e < 64; e++)
									if (ErrorText(e) == cell.text)
										code = e;
								c.kind = Sig("Err#");
								c.value = uint32_t(code);
								c.text.clear();
							}
						}
					break;
				case ExcelCell::kBlank:
					break;
			}
		}
		for (const auto& width : sheet.columnWidths)
			if (width.second > 0)
				SetLineSize(*ss, true, width.first, width.first, width.second);
		if (sheet.rows > ss->used.to.a + 1)
			ss->used.to.a = uint16_t(sheet.rows - 1);
		if (sheet.columns > ss->used.to.b + 1)
			ss->used.to.b = uint16_t(std::min<int>(sheet.columns - 1, 255));
		UpdateDependencies(doc, part);
	}
	if (bad > 0 && notice != nullptr)
		*notice = std::to_string(bad) + " formulas couldn't be read; their cells have their values.";
	return true;
}

} // namespace gobe


// #pragma mark - export

namespace gobe {

std::vector<uint8_t>
MakeCompoundFile(const std::vector<std::pair<std::string, std::vector<uint8_t>>>& streams)
{
	const uint32_t kSector = 512, kMini = 64, kCutoff = 4096;
	// the mini stream (small streams, 64-byte sectors) and its FAT
	std::vector<uint8_t> mini;
	std::vector<uint32_t> miniFAT;
	std::vector<uint32_t> starts(streams.size());
	for (size_t i = 0; i < streams.size(); i++) {
		const std::vector<uint8_t>& data = streams[i].second;
		if (data.size() >= kCutoff)
			continue;
		uint32_t first = uint32_t(mini.size() / kMini);
		uint32_t count = uint32_t((data.size() + kMini - 1) / kMini);
		starts[i] = count == 0 ? kEndOfChain : first;
		for (uint32_t k = 0; k < count; k++)
			miniFAT.push_back(k + 1 < count ? first + k + 1 : kEndOfChain);
		mini.insert(mini.end(), data.begin(), data.end());
		mini.resize(size_t(first + count) * kMini, 0);
	}
	// the sectors after the header: big streams, the mini stream, the mini
	// FAT, the directory, then the FAT (whose own size settles last)
	std::vector<std::vector<uint8_t>> blocks;	// each sector-aligned
	std::vector<uint32_t> blockStart;
	uint32_t next = 0;
	auto place = [&](std::vector<uint8_t> data) {
		uint32_t count = uint32_t((data.size() + kSector - 1) / kSector);
		data.resize(size_t(count) * kSector, 0);
		blockStart.push_back(count == 0 ? kEndOfChain : next);
		next += count;
		blocks.push_back(std::move(data));
		return blockStart.back();
	};
	for (size_t i = 0; i < streams.size(); i++)
		if (streams[i].second.size() >= kCutoff)
			starts[i] = place(streams[i].second);
	uint32_t miniStart = place(mini);
	std::vector<uint8_t> miniFATBytes;
	for (uint32_t v : miniFAT)
		for (int b = 0; b < 4; b++)
			miniFATBytes.push_back(uint8_t(v >> (8 * b)));
	uint32_t miniFATStart = place(miniFATBytes);
	uint32_t miniFATSectors = uint32_t((miniFATBytes.size() + kSector - 1) / kSector);
	// the directory: the root, then the streams as a chain of right siblings
	std::vector<uint8_t> directory;
	auto entry = [&](const std::string& name, uint8_t type, uint32_t start, uint32_t size,
			uint32_t right, uint32_t child) {
		std::vector<uint8_t> e(128, 0);
		for (size_t i = 0; i < name.size() && i < 31; i++)
			e[i * 2] = uint8_t(name[i]);
		uint16_t length = uint16_t((std::min<size_t>(name.size(), 31) + 1) * 2);
		e[0x40] = uint8_t(length);
		e[0x41] = uint8_t(length >> 8);
		e[0x42] = type;
		e[0x43] = 1;			// black
		auto u32at = [&](size_t at, uint32_t v) {
			for (int b = 0; b < 4; b++)
				e[at + size_t(b)] = uint8_t(v >> (8 * b));
		};
		u32at(0x44, kFree);		// left sibling
		u32at(0x48, right);
		u32at(0x4c, child);
		u32at(0x74, start);
		u32at(0x78, size);
		directory.insert(directory.end(), e.begin(), e.end());
	};
	entry("Root Entry", 5, mini.empty() ? kEndOfChain : miniStart, uint32_t(mini.size()), kFree,
		streams.empty() ? kFree : 1);
	for (size_t i = 0; i < streams.size(); i++)
		entry(streams[i].first, 2, starts[i], uint32_t(streams[i].second.size()),
			i + 1 < streams.size() ? uint32_t(i + 2) : kFree, kFree);
	uint32_t directoryStart = place(directory);
	// the FAT: every chain, then the FAT's own sectors
	uint32_t dataSectors = next;
	uint32_t fatSectors = 1;
	while ((dataSectors + fatSectors) * 4 > fatSectors * kSector)
		fatSectors++;
	std::vector<uint32_t> fat(size_t(fatSectors) * kSector / 4, kFree);
	for (size_t b = 0; b < blocks.size(); b++) {
		uint32_t count = uint32_t(blocks[b].size() / kSector);
		for (uint32_t k = 0; k < count; k++)
			fat[blockStart[b] + k] = k + 1 < count ? blockStart[b] + k + 1 : kEndOfChain;
	}
	for (uint32_t k = 0; k < fatSectors; k++)
		fat[dataSectors + k] = 0xfffffffd;		// FATSECT
	// the header
	std::vector<uint8_t> out(512, 0);
	static const uint8_t kSignature[8] = {0xd0, 0xcf, 0x11, 0xe0, 0xa1, 0xb1, 0x1a, 0xe1};
	memcpy(out.data(), kSignature, 8);
	auto u16at = [&](size_t at, uint16_t v) { out[at] = uint8_t(v); out[at + 1] = uint8_t(v >> 8); };
	auto u32at = [&](size_t at, uint32_t v) {
		for (int b = 0; b < 4; b++)
			out[at + size_t(b)] = uint8_t(v >> (8 * b));
	};
	u16at(0x18, 0x3e);
	u16at(0x1a, 3);
	u16at(0x1c, 0xfffe);
	u16at(0x1e, 9);
	u16at(0x20, 6);
	u32at(0x2c, fatSectors);
	u32at(0x30, directoryStart);
	u32at(0x38, kCutoff);
	u32at(0x3c, miniFAT.empty() ? kEndOfChain : miniFATStart);
	u32at(0x40, miniFAT.empty() ? 0 : miniFATSectors);
	u32at(0x44, kEndOfChain);
	for (int i = 0; i < 109; i++)
		u32at(0x4c + size_t(i) * 4, uint32_t(i) < fatSectors ? dataSectors + uint32_t(i) : kFree);
	for (const std::vector<uint8_t>& block : blocks)
		out.insert(out.end(), block.begin(), block.end());
	for (uint32_t v : fat)
		for (int b = 0; b < 4; b++)
			out.push_back(uint8_t(v >> (8 * b)));
	return out;
}


namespace {

struct BiffOut {
	std::vector<uint8_t> data;
	void u8(uint8_t v) { data.push_back(v); }
	void u16(uint16_t v) { u8(uint8_t(v)); u8(uint8_t(v >> 8)); }
	void u32(uint32_t v) { u16(uint16_t(v)); u16(uint16_t(v >> 16)); }
	void f64(double v) { uint8_t b[8]; memcpy(b, &v, 8); data.insert(data.end(), b, b + 8); }
	void bytes(const std::vector<uint8_t>& b) { data.insert(data.end(), b.begin(), b.end()); }
};


void
Record(BiffOut& out, uint16_t type, const std::vector<uint8_t>& body)
{
	// (records over 8224 bytes go on in CONTINUE records)
	size_t at = 0;
	do {
		size_t n = std::min<size_t>(body.size() - at, 8224);
		out.u16(at == 0 ? type : 0x3c);
		out.u16(uint16_t(n));
		out.data.insert(out.data.end(), body.begin() + long(at), body.begin() + long(at + n));
		at += n;
	} while (at < body.size());
}


// UTF-8 as UTF-16 code units
std::vector<uint16_t>
Units(const std::string& text)
{
	std::vector<uint16_t> out;
	for (size_t i = 0; i < text.size();) {
		unsigned char c = (unsigned char)text[i];
		uint32_t code = c;
		int length = c >= 0xf0 ? 4 : c >= 0xe0 ? 3 : c >= 0xc0 ? 2 : 1;
		if (length > 1)
			code = c & (0x3f >> (length - 1));
		for (int k = 1; k < length && i + size_t(k) < text.size(); k++)
			code = code << 6 | ((unsigned char)text[i + size_t(k)] & 0x3f);
		i += size_t(length);
		if (code >= 0x10000) {
			code -= 0x10000;
			out.push_back(uint16_t(0xd800 | (code >> 10)));
			out.push_back(uint16_t(0xdc00 | (code & 0x3ff)));
		} else
			out.push_back(uint16_t(code));
	}
	return out;
}


// a BIFF8 string: its length (8 or 16 bits), 1 (16-bit characters), them
void
UnicodeString(BiffOut& out, const std::string& text, bool longLength)
{
	std::vector<uint16_t> units = Units(text);
	if (longLength)
		out.u16(uint16_t(std::min<size_t>(units.size(), 65535)));
	else
		out.u8(uint8_t(std::min<size_t>(units.size(), 255)));
	out.u8(1);
	for (size_t i = 0; i < units.size() && i < (longLength ? 65535u : 255u); i++)
		out.u16(units[i]);
}


// TExportExcel::Write_FORMULA_Record's tokens: Gobe's (operator first) as
// Excel's (operands first); false for what Excel can't take
bool
ExcelTokens(const std::vector<FormulaToken>& t, size_t& i, CellID at, BiffOut& out)
{
	if (i >= t.size())
		return false;
	const FormulaToken& k = t[i++];
	static const uint8_t kBinary[12] = {0x0a, 0x0c, 0x0e, 0x09, 0x0d, 0x0b, 0x08, 0x03, 0x04,
		0x05, 0x06, 0x07};
	if (k.kind < 0x0c) {
		if (!ExcelTokens(t, i, at, out) || !ExcelTokens(t, i, at, out))
			return false;
		out.u8(kBinary[k.kind]);
		return true;
	}
	auto ref = [&](const CellRef& r, uint16_t& row, uint16_t& column) {
		CellID id;
		// (CellRef: flags 1 invalid, 2 the row absolute, 4 the column)
		if (r.flags & 1)
			return false;
		int rr = (r.flags & 2) ? int16_t(r.cell.a) : at.a + int16_t(r.cell.a);
		int cc = (r.flags & 4) ? int16_t(r.cell.b) : at.b + int16_t(r.cell.b);
		if (rr < 0 || cc < 0 || cc > 255)
			return false;
		(void)id;
		row = uint16_t(rr);
		column = uint16_t(cc | ((r.flags & 2) ? 0 : 0x8000) | ((r.flags & 4) ? 0 : 0x4000));
		return true;
	};
	switch (k.kind) {
		case 0x0c:
			if (!ExcelTokens(t, i, at, out))
				return false;
			out.u8(0x14);
			return true;
		case 0x0d:
			if (!ExcelTokens(t, i, at, out))
				return false;
			out.u8(0x13);
			return true;
		case 0x0e:
			if (!ExcelTokens(t, i, at, out))
				return false;
			if (i < t.size() && t[i].kind == 0x0f)
				i++;
			out.u8(0x15);
			return true;
		case 0x11: {
			std::string s = k.strings.empty() ? "" : k.strings[0];
			out.u8(0x17);
			UnicodeString(out, s, false);
			return true;
		}
		case 0x12:
			out.u8(0x1d);
			out.u8(k.value ? 1 : 0);
			return true;
		case 0x13:
		case 0x14:
		case 0x15:
		case 0x16:
			if (k.number >= 0 && k.number <= 65535 && k.number == std::floor(k.number)) {
				out.u8(0x1e);
				out.u16(uint16_t(k.number));
			} else {
				out.u8(0x1f);
				out.f64(k.number);
			}
			return true;
		case 0x17: {
			std::string name = ErrorText(int(k.value));
			uint8_t code = 0x0f;
			for (size_t e = 0; e < sizeof kErrorCodes; e++)
				if (name == kErrorNames[e])
					code = kErrorCodes[e];
			out.u8(0x1c);
			out.u8(code);
			return true;
		}
		case 0x18:	// a date: Excel's 1904 count (DATEMODE 1)
			out.u8(0x1f);
			out.f64(double(k.value) / 86400.0 + 24107);
			return true;
		case 0x19: {
			uint16_t row, column;
			if (k.refs.empty() || !ref(k.refs[0], row, column))
				return false;
			out.u8(0x44);		// tRefV
			out.u16(row);
			out.u16(column);
			return true;
		}
		case 0x1a: {
			uint16_t r1, c1, r2, c2;
			if (k.refs.size() < 2 || !ref(k.refs[0], r1, c1) || !ref(k.refs[1], r2, c2))
				return false;
			out.u8(0x25);		// tAreaR
			out.u16(r1);
			out.u16(r2);
			out.u16(c1);
			out.u16(c2);
			return true;
		}
		case 0x1c: {
			std::string name = k.strings.empty() ? "" : k.strings[0];
			for (char& c : name)
				c = char(toupper((unsigned char)c));
			const ExcelFunction* f = nullptr;
			for (const ExcelFunction& g : kFunctions)
				if (name == g.name)
					f = &g;
			if (f == nullptr)
				return false;
			for (uint32_t n = 0; n < k.value; n++)
				if (!ExcelTokens(t, i, at, out))
					return false;
			if (f->arguments != 255 && uint32_t(f->arguments) == k.value) {
				out.u8(0x41);	// tFuncV
				out.u16(uint16_t(f->index));
			} else {
				out.u8(0x42);	// tFuncVarV
				out.u8(uint8_t(k.value));
				out.u16(uint16_t(f->index));
			}
			return true;
		}
		default:
			return false;
	}
}

} // namespace


std::vector<uint8_t>
ExcelBytes(const Document& doc)
{
	// the spreadsheet sheets (TExportExcel::CountSSSheets)
	std::vector<size_t> parts;
	std::vector<std::string> names;
	for (size_t s = 0; s < doc.SheetCount(); s++)
		if (doc.SpreadsheetPart(doc.sheets.sheets[s].mainPart) != nullptr) {
			parts.push_back(doc.sheets.sheets[s].mainPart);
			names.push_back(doc.sheets.sheets[s].name);
		}
	// what the globals hold: the strings (SST), the number formats
	std::vector<std::string> strings;
	std::map<std::string, uint32_t> stringIndex;
	std::vector<std::string> formats;				// custom, from FORMAT index 164
	struct Cell {
		uint16_t row, column;
		SSValue value;
		std::vector<uint8_t> tokens;
		bool formula = false;
		uint16_t xf = 15;
	};
	std::vector<std::vector<Cell>> sheetCells(parts.size());
	std::vector<std::map<uint16_t, float>> widths(parts.size());
	std::vector<SSRange> used(parts.size());
	std::vector<std::string> xfFormats;			// a cell XF per format, from XF 16
	SSBook book(doc);
	for (size_t s = 0; s < parts.size(); s++) {
		const SSPart& ss = *doc.SpreadsheetPart(parts[s]);
		SSEvaluator eval(ss, &book, int(parts[s]));
		used[s] = ss.used;
		for (const SSHeader& column : ss.columns)
			if (column.index < 256 && column.cell.number != ss.defaultWidth && column.cell.number > 0)
				widths[s][uint16_t(column.index)] = float(column.cell.number);
		for (const SSCell& cell : ss.cells) {
			if (cell.id.b == 0xffff || cell.id.b > 255)
				continue;
			Cell out;
			out.row = cell.id.a;
			out.column = cell.id.b;
			out.value = eval.Value(cell.id.a, cell.id.b);
			if (cell.kind == Sig("Fmla")) {
				BiffOut tokens;
				size_t i = 0;
				bool ok = ExcelTokens(cell.formula, i, cell.id, tokens);
				while (i < cell.formula.size() && cell.formula[i].kind == 0x1d)
					i++;	// (the formula's end)
				if (ok && i == cell.formula.size()) {
					out.tokens = tokens.data;
					out.formula = true;
				}
			}
			if (out.value.type == SSValue::kNone && !out.formula)
				continue;
			// the format: a cell XF of its own
			std::vector<uint32_t> stack = {ss.style};
			if (const SSHeader* h = ss.Column(cell.id.b))
				stack.push_back(h->cell.style);
			if (const SSHeader* h = ss.Row(cell.id.a))
				stack.push_back(h->cell.style);
			stack.push_back(cell.style);
			std::string format = ResolveCellStyle(doc.styles, stack).format;
			if (!format.empty() && format != "General") {
				auto x = std::find(xfFormats.begin(), xfFormats.end(), format);
				out.xf = uint16_t(16 + (x - xfFormats.begin()));
				if (x == xfFormats.end())
					xfFormats.push_back(format);
			}
			if (out.value.type == SSValue::kString && !out.formula) {
				auto found = stringIndex.find(out.value.text);
				if (found == stringIndex.end()) {
					stringIndex[out.value.text] = uint32_t(strings.size());
					strings.push_back(out.value.text);
				}
			}
			sheetCells[s].push_back(out);
		}
		std::stable_sort(sheetCells[s].begin(), sheetCells[s].end(), [](const Cell& a, const Cell& b) {
			return a.row != b.row ? a.row < b.row : a.column < b.column;
		});
	}
	uint32_t totalStrings = 0;
	for (const auto& cells : sheetCells)
		for (const Cell& c : cells)
			totalStrings += c.value.type == SSValue::kString && !c.formula;

	BiffOut out;
	auto bof = [&](uint16_t type) {
		BiffOut b;
		b.u16(0x0600);
		b.u16(type);
		b.u16(0x0dbb);
		b.u16(0x07cc);
		b.u32(0);
		b.u32(6);
		Record(out, 0x809, b.data);
	};
	bof(0x0005);
	{
		BiffOut b;
		b.u16(1200);		// CODEPAGE: UTF-16
		Record(out, 0x42, b.data);
	}
	{
		// WINDOW1 as Write_Global_Records writes it
		BiffOut b;
		for (uint16_t v : {uint16_t(0x370), uint16_t(0x118), uint16_t(0x2bd4), uint16_t(0x1d60),
				uint16_t(0x38), uint16_t(0), uint16_t(0), uint16_t(1), uint16_t(600)})
			b.u16(v);
		Record(out, 0x3d, b.data);
	}
	{
		BiffOut b;
		b.u16(1);			// DATEMODE: 1904
		Record(out, 0x22, b.data);
	}
	for (int f = 0; f < 4; f++) {
		// FONT: Arial 10 points (Excel needs four; the fifth is index 5)
		BiffOut b;
		b.u16(200);
		b.u16(0);
		b.u16(0x7fff);
		b.u16(400);
		b.u16(0);
		b.u8(0);
		b.u8(0);
		b.u8(0);
		b.u8(0);
		UnicodeString(b, "Arial", false);
		Record(out, 0x31, b.data);
	}
	// FORMAT for each format of the cells (from 164)
	std::vector<uint16_t> formatIndex;
	for (const std::string& format : xfFormats) {
		uint16_t index = 0;
		for (uint16_t builtin = 1; builtin < 50 && index == 0; builtin++)
			if (BuiltinFormat(builtin) == format)
				index = builtin;
		if (index == 0) {
			index = uint16_t(164 + formats.size());
			formats.push_back(format);
			BiffOut b;
			b.u16(index);
			UnicodeString(b, format, true);
			Record(out, 0x41e, b.data);
		}
		formatIndex.push_back(index);
	}
	// XF: 15 style XFs (Normal and the built-in styles), the default cell
	// XF (15), a cell XF per format (16 on)
	auto xf = [&](uint16_t format, bool style) {
		BiffOut b;
		b.u16(0);
		b.u16(format);
		b.u16(style ? 0xfff5 : 0x0001);
		b.u8(0x20);			// general, bottom
		b.u8(0);
		b.u8(0);
		b.u8(style ? 0 : 0xf8);
		b.u32(0);
		b.u32(0);
		b.u16(0x20c0);
		Record(out, 0xe0, b.data);
	};
	for (int i = 0; i < 15; i++)
		xf(0, true);
	xf(0, false);
	for (uint16_t index : formatIndex)
		xf(index, false);
	{
		BiffOut b;			// STYLE: Normal on XF 0
		b.u16(0x8000);
		b.u8(0);
		b.u8(0xff);
		Record(out, 0x293, b.data);
	}
	std::vector<size_t> boundAt;
	for (const std::string& name : names) {
		BiffOut b;
		b.u32(0);			// (the sheet's BOF, patched)
		b.u8(0);
		b.u8(0);
		UnicodeString(b, name.empty() ? "Sheet" : name, false);
		boundAt.push_back(out.data.size() + 4);
		Record(out, 0x85, b.data);
	}
	{
		// SST: whole strings per record (a string that doesn't fit starts the
		// next CONTINUE)
		std::vector<std::vector<uint8_t>> pieces;
		BiffOut piece;
		piece.u32(totalStrings);
		piece.u32(uint32_t(strings.size()));
		for (const std::string& s : strings) {
			BiffOut one;
			UnicodeString(one, s.size() > 8000 ? s.substr(0, 8000) : s, true);
			if (piece.data.size() + one.data.size() > 8224) {
				pieces.push_back(piece.data);
				piece.data.clear();
			}
			piece.bytes(one.data);
		}
		pieces.push_back(piece.data);
		for (size_t k = 0; k < pieces.size(); k++) {
			out.u16(k == 0 ? 0xfc : 0x3c);
			out.u16(uint16_t(pieces[k].size()));
			out.bytes(pieces[k]);
		}
	}
	Record(out, 0x0a, {});
	// the sheets
	for (size_t s = 0; s < parts.size(); s++) {
		uint32_t at = uint32_t(out.data.size());
		memcpy(&out.data[boundAt[s]], &at, 4);
		bof(0x0010);
		{
			BiffOut b;		// DIMENSIONS
			uint32_t rows = 0;
			uint16_t columns = 0;
			for (const Cell& c : sheetCells[s]) {
				rows = std::max<uint32_t>(rows, uint32_t(c.row) + 1);
				columns = std::max<uint16_t>(columns, uint16_t(c.column + 1));
			}
			b.u32(0);
			b.u32(rows);
			b.u16(0);
			b.u16(columns);
			b.u16(0);
			Record(out, 0x200, b.data);
		}
		for (const auto& width : widths[s]) {
			BiffOut b;		// COLINFO: 1/256 of a character, 7 points each
			b.u16(width.first);
			b.u16(width.first);
			b.u16(uint16_t(std::min(65535.0f, width.second / 7.0f * 256.0f)));
			b.u16(15);
			b.u16(0);
			b.u16(0);
			Record(out, 0x7d, b.data);
		}
		for (const Cell& c : sheetCells[s]) {
			BiffOut b;
			b.u16(c.row);
			b.u16(c.column);
			b.u16(c.xf);
			double number = 0;
			bool isNumber = c.value.GetNum(number) && (c.value.type == SSValue::kNumber
				|| c.value.type == SSValue::kTime);
			if (c.value.type == SSValue::kTime || (isNumber && c.xf >= 16
					&& IsDateNumberFormat(xfFormats[c.xf - 16])))
				number += 24107;	// Gobe's days in Excel's 1904 count
			if (c.formula) {
				// FORMULA: the value (a string after it in STRING), always
				// calculated again
				if (c.value.type == SSValue::kString) {
					b.u8(0);
					for (int k = 0; k < 5; k++)
						b.u8(0);
					b.u16(0xffff);
				} else if (c.value.type == SSValue::kBool || c.value.type == SSValue::kError) {
					b.u8(c.value.type == SSValue::kBool ? 1 : 2);
					b.u8(0);
					uint8_t v = uint8_t(c.value.number != 0);
					if (c.value.type == SSValue::kError) {
						v = 0x0f;
						std::string name = ErrorText(c.value.error);
						for (size_t e = 0; e < sizeof kErrorCodes; e++)
							if (name == kErrorNames[e])
								v = kErrorCodes[e];
					}
					b.u8(v);
					b.u8(0);
					b.u8(0);
					b.u8(0);
					b.u16(0xffff);
				} else
					b.f64(number);
				b.u16(0x0002);
				b.u32(0);
				b.u16(uint16_t(c.tokens.size()));
				b.bytes(c.tokens);
				Record(out, 0x06, b.data);
				if (c.value.type == SSValue::kString) {
					BiffOut str;
					UnicodeString(str, c.value.text, true);
					Record(out, 0x207, str.data);
				}
				continue;
			}
			switch (c.value.type) {
				case SSValue::kString:
					b.u32(stringIndex[c.value.text]);
					Record(out, 0xfd, b.data);
					break;
				case SSValue::kBool:
				case SSValue::kError: {
					uint8_t v = uint8_t(c.value.number != 0);
					if (c.value.type == SSValue::kError) {
						v = 0x0f;
						std::string name = ErrorText(c.value.error);
						for (size_t e = 0; e < sizeof kErrorCodes; e++)
							if (name == kErrorNames[e])
								v = kErrorCodes[e];
					}
					b.u8(v);
					b.u8(c.value.type == SSValue::kError ? 1 : 0);
					Record(out, 0x205, b.data);
					break;
				}
				default:
					b.f64(number);
					Record(out, 0x203, b.data);
					break;
			}
		}
		{
			BiffOut b;		// WINDOW2: grid, headings, zeros; the first sheet selected
			b.u16(uint16_t(0x06b6 | (s == 0 ? 0x0600 : 0)));
			b.u16(0);
			b.u16(0);
			b.u16(64);
			b.u16(0);
			b.u16(0);
			b.u16(0);
			b.u32(0);
			Record(out, 0x23e, b.data);
		}
		Record(out, 0x0a, {});
	}
	return MakeCompoundFile({{"Workbook", out.data}});
}


bool
ExportExcel(const Document& doc, const std::string& path, std::string& error)
{
	std::vector<uint8_t> bytes = ExcelBytes(doc);
	std::ofstream file(path, std::ios::binary | std::ios::trunc);
	if (!file) {
		error = "cannot write " + path;
		return false;
	}
	file.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
	return bool(file);
}

} // namespace gobe
