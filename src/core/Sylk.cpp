// SYLK: the records Gobe Productive's translator reads and writes (see
// Sylk.h). Drafted by the local model, rewritten to the header.
#include "Sylk.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

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

} // namespace


bool
ReadSylk(const std::string& data, SylkSheet& sheet, std::string& error)
{
	sheet = SylkSheet();
	long x = 0, y = 0;	// the current position, from 1 (0: none yet)
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
		} else if (type == "F") {
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
	return true;
}


std::string
WriteSylk(const SylkSheet& sheet)
{
	// "ID;PGOBE": SYLK's P field names the program
	std::string out = "ID;PGOBE\r\n";
	if (sheet.rows != 0 || sheet.columns != 0)
		out += "B;Y" + std::to_string(sheet.rows) + ";X" + std::to_string(sheet.columns) + "\r\n";
	for (const auto& width : sheet.columnWidths) {
		std::string column = std::to_string(width.first + 1);
		out += "F;W" + column + " " + column + " " + std::to_string(width.second) + "\r\n";
	}
	for (const SylkCell& cell : sheet.cells) {
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
