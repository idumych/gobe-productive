// SYLK: the records Gobe Productive's translator reads and writes (see
// Sylk.h). Drafted by the local model, rewritten to the header.
#include "Sylk.h"

#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>

namespace gobe {

namespace {

struct Field {
	char letter;
	std::string value;	// ";;" already turned into ';'
};

// a record's type and fields: "C;X1;Y2;K\"a;;b\"" -> "C", X 1, Y 2, K "a;b"
void
SplitRecord(const std::string& line, std::string& type, std::vector<Field>& fields)
{
	fields.clear();
	size_t at = line.find(';');
	type = line.substr(0, at);
	while (at != std::string::npos && at + 1 < line.size()) {
		at++;	// the ';' before the field
		Field field{line[at++], ""};
		while (at < line.size()) {
			if (line[at] == ';') {
				if (at + 1 < line.size() && line[at + 1] == ';') {
					field.value += ';';
					at += 2;
					continue;
				}
				break;
			}
			field.value += line[at++];
		}
		fields.push_back(field);
		if (at >= line.size())
			break;
	}
}


bool
ParseInt(const std::string& text, long& value)
{
	if (text.empty())
		return false;
	char* end;
	errno = 0;
	value = strtol(text.c_str(), &end, 10);
	return errno == 0 && end != text.c_str() && *end == '\0';
}


const char* const kErrors[] = {
	"#DIV/0!", "#N/A", "#VALUE!", "#REF!", "#NAME?", "#NUM!", "#NULL!"
};


// a K field's value into the cell; false if it isn't one
bool
ParseValue(const std::string& text, SylkCell& cell)
{
	if (!text.empty() && text[0] == '"') {
		if (text.size() < 2 || text.back() != '"')
			return false;
		cell.kind = SylkCell::kString;
		cell.text = text.substr(1, text.size() - 2);
		return true;
	}
	if (text == "TRUE" || text == "FALSE") {
		cell.kind = SylkCell::kBool;
		cell.number = text == "TRUE" ? 1 : 0;
		return true;
	}
	for (const char* error : kErrors) {
		if (text == error) {
			cell.kind = SylkCell::kError;
			cell.text = text;
			return true;
		}
	}
	if (text.empty())
		return false;
	char* end;
	double number = strtod(text.c_str(), &end);
	if (*end != '\0')
		return false;
	cell.kind = SylkCell::kNumber;
	cell.number = number;
	return true;
}


std::string
Escaped(const std::string& text)
{
	std::string out;
	for (char c : text) {
		out += c;
		if (c == ';')
			out += ';';
	}
	return out;
}


std::string
NumberText(double value)
{
	char text[32];
	snprintf(text, sizeof text, "%.15g", value);
	if (strtod(text, NULL) != value)
		snprintf(text, sizeof text, "%.17g", value);
	return text;
}

// a P record's format with SYLK's escapes taken out
std::string
Unescaped(const std::string& format)
{
	std::string out;
	for (size_t i = 0; i < format.size(); i++) {
		if (format[i] == '\\' && i + 1 < format.size())
			i++;
		out += format[i];
	}
	return out;
}


// TNumberFormatsTable::StyleToNumberFormat's escapes (';' is doubled when
// the record is written)
std::string
EscapedFormat(const std::string& format)
{
	std::string out;
	for (char c : format) {
		if (c == ':' || c == '-' || c == '/' || c == '(' || c == ')')
			out += '\\';
		out += c;
	}
	return out;
}


// convert_number_format: 'G' general, 'F' fixed with its decimals, '%',
// 'E' exponential, 'C' currency (the original counted the decimals into
// the pointer, leaving 0; counted here)
void
FormatKind(const std::string& format, char& kind, int& decimals)
{
	kind = 'G';
	decimals = 0;
	size_t dot = format.find('.');
	if (dot != std::string::npos)
		for (size_t i = dot + 1; i < format.size() && isdigit((unsigned char)format[i]); i++)
			decimals++;
	if (decimals != 0)
		kind = 'F';
	if (format.find('%') != std::string::npos)
		kind = '%';
	if (format.find("E+") != std::string::npos)
		kind = 'E';
	if (format.find('$') != std::string::npos)
		kind = 'C';
}

} // namespace


const std::vector<std::string>&
DefaultSylkFormats()
{
	// write_default_format's P records, unescaped
	static const std::vector<std::string> kFormats = {
		"General", "0", "0.00", "#,##0", "#,##0.00",
		"\"$\"#,##0 ;(\"$\"#,##0)", "\"$\"#,##0.00 ;(\"$\"#,##0.00)",
		"0%", "0.00%", "0.00E+00", "m/d/yy", "d-mmm-yy", "d-mmm", "mmm-yy",
		"h:mm AM/PM", "h:mm:ss AM/PM", "h:mm", "h:mm:ss", "hh:mm AM/PM", "hh:mm:ss AM/PM",
		"h:mm", "h:mm:ss", "m/d/yy h:mm", "m-dd-yy", "m-dd",
		"\"$\"#,##0 ;[Red](\"$\"#,##0)", "\"$\"#,##0.00 ;[Red](\"$\"#,##0.00)",
		"mmm d, yyyy", "mmmm d, yyyy", "ddd,\\ mmm d, yyyy", "dddd, mmmm d, yyyy",
		"d, mmmm yyyy"
	};
	return kFormats;
}


bool
ReadSylk(const std::string& data, SylkSheet& sheet, std::string& error)
{
	sheet = SylkSheet();
	long x = 0, y = 0;	// the current position, from 1 (0: none yet)
	std::map<std::pair<uint16_t, uint16_t>, int> cellFormats;
	bool first = true;
	size_t at = 0;
	for (int lineNumber = 1; at < data.size(); lineNumber++) {
		// lines end in "\r\n", "\n" or (old Macintosh files) "\r"
		size_t end = data.find_first_of("\r\n", at);
		if (end == std::string::npos)
			end = data.size();
		std::string line = data.substr(at, end - at);
		at = end + (data.compare(end, 2, "\r\n") == 0 ? 2 : 1);
		if (line.empty())
			continue;

		std::string type;
		std::vector<Field> fields;
		SplitRecord(line, type, fields);
		std::string where = "line " + std::to_string(lineNumber) + ": ";
		if (first) {
			if (type != "ID") {
				error = "no ID record";
				return false;
			}
			first = false;
			continue;
		}
		if (type == "E")
			break;
		if (type == "B") {
			for (const Field& field : fields) {
				long value;
				if (field.letter != 'X' && field.letter != 'Y')
					continue;
				if (!ParseInt(field.value, value) || value < 0 || value > 65535) {
					error = where + "bad B record";
					return false;
				}
				(field.letter == 'Y' ? sheet.rows : sheet.columns) = uint16_t(value);
			}
		} else if (type == "C") {
			SylkCell cell;
			bool hasValue = false;
			for (const Field& field : fields) {
				long value;
				if (field.letter == 'X' || field.letter == 'Y') {
					if (!ParseInt(field.value, value) || value < 1 || value > 65536) {
						error = where + "bad cell position";
						return false;
					}
					(field.letter == 'X' ? x : y) = value;
				} else if (field.letter == 'K' && !field.value.empty()) {
					// (an empty K: no value)
					if (!ParseValue(field.value, cell)) {
						error = where + "bad value";
						return false;
					}
					hasValue = true;
				} else if (field.letter == 'E')
					cell.formula = field.value;
			}
			if (x == 0 || y == 0) {
				error = where + "cell without a position";
				return false;
			}
			if (!hasValue)
				cell.kind = SylkCell::kEmpty;
			cell.row = uint16_t(y - 1);
			cell.column = uint16_t(x - 1);
			sheet.cells.push_back(cell);
		} else if (type == "P") {
			// a number format (P), or a font (F, E, L: not kept)
			for (const Field& field : fields)
				if (field.letter == 'P')
					sheet.formats.push_back(Unescaped(field.value));
		} else if (type == "F") {
			// the cell's format: its position (it moves the current one too)
			long format = -1;
			for (const Field& field : fields) {
				long value;
				if ((field.letter == 'X' || field.letter == 'Y') && ParseInt(field.value, value)
						&& value >= 1 && value <= 65536)
					(field.letter == 'X' ? x : y) = value;
				else if (field.letter == 'P' && ParseInt(field.value, value) && value >= 0)
					format = value;
			}
			if (format >= 0 && x > 0 && y > 0)
				cellFormats[{uint16_t(y - 1), uint16_t(x - 1)}] = int(format);
			for (const Field& field : fields) {
				if (field.letter != 'W')
					continue;
				long from, to, width;
				char extra;
				if (sscanf(field.value.c_str(), "%ld %ld %ld %c", &from, &to, &width, &extra) != 3
						|| from < 1 || to < from || to > 65536 || width < 0) {
					error = where + "bad column width";
					return false;
				}
				for (long column = from; column <= to; column++)
					sheet.columnWidths[uint16_t(column - 1)] = int(width);
			}
		}
		// other records: ignored
	}
	if (first) {
		error = "no ID record";
		return false;
	}
	for (SylkCell& cell : sheet.cells) {
		auto f = cellFormats.find({cell.row, cell.column});
		if (f != cellFormats.end() && f->second < int(sheet.formats.size()))
			cell.format = f->second;
	}
	return true;
}


std::string
WriteSylk(const SylkSheet& sheet)
{
	// "ID;PGOBE": SYLK's P field names the program
	std::string out = "ID;PGOBE\r\n";
	for (const std::string& format : sheet.formats)
		out += "P;P" + Escaped(EscapedFormat(format)) + "\r\n";
	if (sheet.rows != 0 || sheet.columns != 0)
		out += "B;Y" + std::to_string(sheet.rows) + ";X" + std::to_string(sheet.columns) + "\r\n";
	for (const auto& width : sheet.columnWidths) {
		std::string column = std::to_string(width.first + 1);
		out += "F;W" + column + " " + column + " " + std::to_string(width.second) + "\r\n";
	}
	for (const SylkCell& cell : sheet.cells) {
		if (cell.format >= 0 && size_t(cell.format) < sheet.formats.size()) {
			// write_cell_format: the format's index and kind (alignment G)
			char kind;
			int decimals;
			FormatKind(sheet.formats[size_t(cell.format)], kind, decimals);
			out += "F;P" + std::to_string(cell.format) + ";F" + kind + std::to_string(decimals)
				+ "G;X" + std::to_string(cell.column + 1) + ";Y" + std::to_string(cell.row + 1)
				+ "\r\n";
		}
		out += "C;X" + std::to_string(cell.column + 1) + ";Y" + std::to_string(cell.row + 1);
		switch (cell.kind) {
			case SylkCell::kNumber:
				out += ";K" + NumberText(cell.number);
				break;
			case SylkCell::kString:
				out += ";K\"" + Escaped(cell.text) + "\"";
				break;
			case SylkCell::kBool:
				out += cell.number != 0 ? ";KTRUE" : ";KFALSE";
				break;
			case SylkCell::kError:
				out += ";K" + cell.text;
				break;
			case SylkCell::kEmpty:
				break;
		}
		if (!cell.formula.empty())
			out += ";E" + Escaped(cell.formula);
		out += "\r\n";
	}
	out += "E\r\n";
	return out;
}

} // namespace gobe


namespace gobe {

// The formula conversions (TExportSYLK::convertToSYLKformula, TImportSYLK::
// rtd_C_field_E), on whole words only.
namespace {

bool
WordChar(char c)
{
	return isalnum((unsigned char)c) || c == '_' || c == '.';
}


// a name's character just before `at` (a '.' of a ".." range doesn't count)
bool
WordBefore(const std::string& text, size_t at)
{
	return at > 0 && WordChar(text[at - 1]) && !(text[at - 1] == '.' && at > 1
		&& text[at - 2] == '.');
}


// a name's character at `at`
bool
WordAt(const std::string& text, size_t at)
{
	return at < text.size() && WordChar(text[at]) && !(text[at] == '.' && at + 1 < text.size()
		&& text[at + 1] == '.');
}


bool
Capital(char c)
{
	return c >= 'A' && c <= 'Z';
}


bool
Digit(char c)
{
	return c >= '0' && c <= '9';
}


// an A1 reference at `at` (a word on its own): its length, the row and
// column (from 0) and which parts are absolute; 0 if there isn't one
size_t
ParseA1(const std::string& text, size_t at, long& row, long& column, bool& rowAbsolute,
	bool& columnAbsolute)
{
	if (WordBefore(text, at))
		return 0;
	size_t i = at;
	columnAbsolute = i < text.size() && text[i] == '$';
	if (columnAbsolute)
		i++;
	size_t letters = 0;
	column = 0;
	while (i < text.size() && Capital(text[i]) && letters < 3) {
		column = column * 26 + (text[i] - 'A' + 1);
		i++;
		letters++;
	}
	if (letters == 0 || letters > 2)
		return 0;
	column--;
	rowAbsolute = i < text.size() && text[i] == '$';
	if (rowAbsolute)
		i++;
	size_t digits = i;
	row = 0;
	while (i < text.size() && Digit(text[i]) && row < 1000000)
		row = row * 10 + (text[i++] - '0');
	if (i == digits || row == 0 || WordAt(text, i))
		return 0;
	row--;
	return i - at;
}


// an R1C1 part ("R", "R5", "R[-2]") at `at` after its letter: the end, the
// value (a number from 1 or an offset) and whether it's absolute; npos if
// malformed
size_t
ParseR1C1Part(const std::string& text, size_t at, long& value, bool& absolute)
{
	value = 0;
	absolute = false;
	if (at < text.size() && text[at] == '[') {
		size_t i = at + 1;
		bool negative = i < text.size() && text[i] == '-';
		if (i < text.size() && (text[i] == '-' || text[i] == '+'))
			i++;
		size_t digits = i;
		while (i < text.size() && Digit(text[i]) && value < 1000000)
			value = value * 10 + (text[i++] - '0');
		if (i == digits || i >= text.size() || text[i] != ']')
			return std::string::npos;
		if (negative)
			value = -value;
		return i + 1;
	}
	size_t i = at;
	while (i < text.size() && Digit(text[i]) && value < 1000000)
		value = value * 10 + (text[i++] - '0');
	absolute = i > at;
	if (absolute && value == 0)
		return std::string::npos;
	return i;
}


std::string
ColumnLetters(long column)
{
	std::string letters;
	for (column++; column > 0; column = (column - 1) / 26)
		letters.insert(letters.begin(), char('A' + (column - 1) % 26));
	return letters;
}

} // namespace


std::string
FormulaToSylk(const std::string& formula, uint16_t row, uint16_t column)
{
	std::string out;
	size_t at = !formula.empty() && formula[0] == '=' ? 1 : 0;
	bool quoted = false;
	while (at < formula.size()) {
		char c = formula[at];
		if (c == '"')
			quoted = !quoted;
		long r, col;
		bool rowAbsolute, columnAbsolute;
		size_t length;
		if (quoted || c == '"') {
			out += c;
			at++;
		} else if (c == '.' && at + 1 < formula.size() && formula[at + 1] == '.') {
			out += ':';
			at += 2;
		} else if ((length = ParseA1(formula, at, r, col, rowAbsolute, columnAbsolute)) != 0) {
			auto part = [&](char letter, long value, long base, bool absolute) {
				out += letter;
				if (absolute)
					out += std::to_string(value + 1);
				else if (value != base)
					out += "[" + std::to_string(value - base) + "]";
			};
			part('R', r, row, rowAbsolute);
			part('C', col, column, columnAbsolute);
			at += length;
		} else {
			out += c;
			at++;
		}
	}
	return out;
}


std::string
FormulaFromSylk(const std::string& formula, uint16_t row, uint16_t column)
{
	std::string out = "=";
	size_t at = 0;
	bool quoted = false;
	while (at < formula.size()) {
		char c = formula[at];
		if (c == '"')
			quoted = !quoted;
		if (quoted || c == '"') {
			out += c;
			at++;
			continue;
		}
		if (c == ':') {
			out += "..";
			at++;
			continue;
		}
		if (c == 'R' && !WordBefore(formula, at)) {
			long r, col;
			bool rowAbsolute, columnAbsolute;
			size_t end = ParseR1C1Part(formula, at + 1, r, rowAbsolute);
			if (end != std::string::npos && end < formula.size() && formula[end] == 'C') {
				end = ParseR1C1Part(formula, end + 1, col, columnAbsolute);
				if (end != std::string::npos && !WordAt(formula, end)) {
					long targetRow = rowAbsolute ? r - 1 : row + r;
					long targetColumn = columnAbsolute ? col - 1 : column + col;
					if (targetRow >= 0 && targetColumn >= 0 && targetRow < 65536
							&& targetColumn < 702) {
						out += (columnAbsolute ? "$" : "") + ColumnLetters(targetColumn)
							+ (rowAbsolute ? "$" : "") + std::to_string(targetRow + 1);
						at = end;
						continue;
					}
				}
			}
		}
		out += c;
		at++;
	}
	return out;
}

} // namespace gobe
