// SYLK (Symbolic Link) spreadsheet format reader and writer.
#include "Sylk.h"

#include <cfloat>
#include <cstdlib>
#include <sstream>

namespace gobe {

namespace {

// Escape a SYLK value: double quotes around strings that contain
// special characters, semicolons escaped as ;;, and CR/LF escaped.
std::string EscapeSYLK(const std::string& s, bool isString)
{
	if (isString || s.empty()) {
		bool needsQuote = false;
		for (char c : s) {
			if (c == ';' || c == '"' || c == '\\' || c == '\r' || c == '\n') {
				needsQuote = true;
				break;
			}
		}
		if (needsQuote) {
			std::string out = "\"";
			for (char c : s) {
				if (c == '"')
					out += "\"\"";
				else if (c == ';')
					out += ";;";
				else if (c == '\\')
					out += "\\\\";
				else
					out += c;
			}
			out += '"';
			return out;
		}
		return s;
	}
	return s;
}


// Unescape a SYLK value: handle ;; -> ;, "" -> ", \\\\ -> \\, etc.
std::string UnescapeSYLK(const std::string& s, bool& isQuoted)
{
	isQuoted = false;
	if (s.empty())
		return s;

	// Check for quoted string
	if (s[0] == '"') {
		isQuoted = true;
		std::string out;
		for (size_t i = 1; i < s.size(); i++) {
			if (s[i] == '"') {
				if (i + 1 < s.size() && s[i + 1] == '"') {
					out += '"';
					i++;
				} else {
					break;  // end of quoted string
				}
			} else {
				out += s[i];
			}
		}
		return out;
	}

	// Unescape ;; -> ;
	std::string out;
	for (size_t i = 0; i < s.size(); i++) {
		if (s[i] == ';' && i + 1 < s.size() && s[i + 1] == ';') {
			out += ';';
			i++;
		} else {
			out += s[i];
		}
	}
	return out;
}


// Parse a SYLK record line into type and fields.
// Fields are pairs of (letter, value) separated by ';'.
// ;; in values represents literal ;.
struct SylkRecord {
	std::string type;
	struct Field {
		char letter;
		std::string value;
	};
	std::vector<Field> fields;
};

SylkRecord ParseRecord(const std::string& line)
{
	SylkRecord rec;
	size_t semi = line.find(';');
	if (semi == std::string::npos) {
		rec.type = line;
		return rec;
	}
	rec.type = line.substr(0, semi);

	// Parse fields
	const std::string& rest = line.substr(semi + 1);
	size_t pos = 0;
	while (pos < rest.size()) {
		// Skip leading semicolons
		while (pos < rest.size() && rest[pos] == ';')
			pos++;
		if (pos >= rest.size())
			break;

		// Field letter
		char letter = rest[pos++];
		if (pos >= rest.size()) {
			rec.fields.push_back({letter, ""});
			break;
		}

		// Field value (until next ';')
		size_t start = pos;
		while (pos < rest.size() && rest[pos] != ';') {
			// Handle ;; escape
			if (rest[pos] == ';' && pos + 1 < rest.size() && rest[pos + 1] == ';') {
				pos += 2;
			} else {
				pos++;
			}
		}
		rec.fields.push_back({letter, rest.substr(start, pos - start)});
	}

	return rec;
}


// Parse a single SYLK field value into the cell's text representation.
std::string ParseKField(const std::string& raw)
{
	// Check for quoted string
	if (!raw.empty() && raw[0] == '"') {
		// Find end of quoted string
		std::string result;
		for (size_t i = 1; i < raw.size(); i++) {
			if (raw[i] == '"') {
				if (i + 1 < raw.size() && raw[i + 1] == '"') {
					result += '"';
					i++;
				} else {
					break;
				}
			} else {
				result += raw[i];
			}
		}
		// Unescape ;;
		std::string out;
		for (size_t i = 0; i < result.size(); i++) {
			if (result[i] == ';' && i + 1 < result.size() && result[i + 1] == ';') {
				out += ';';
				i++;
			} else {
				out += result[i];
			}
		}
		return out;
	}

	// Unescape ;; first
	std::string unescaped;
	for (size_t i = 0; i < raw.size(); i++) {
		if (raw[i] == ';' && i + 1 < raw.size() && raw[i + 1] == ';') {
			unescaped += ';';
			i++;
		} else {
			unescaped += raw[i];
		}
	}

	// TRUE / FALSE
	if (unescaped == "TRUE")
		return "TRUE()";
	if (unescaped == "FALSE")
		return "FALSE()";

	// Error codes
	if (unescaped.find("#") == 0)
		return unescaped;

	// Plain number — leave as-is
	return unescaped;
}


// Format a number for SYLK output using %.15g, falling back to %.17g
std::string FormatNumber(double value)
{
	char buf[64];
	snprintf(buf, sizeof(buf), "%.15g", value);
	std::string s(buf);

	// Verify round-trip
	double rt = strtod(s.c_str(), nullptr);
	if (rt != value) {
		snprintf(buf, sizeof(buf), "%.17g", value);
		s = buf;
	}
	return s;
}


// Parse column width from F;W field value.
// Format: "first last width" (columns 1-based)
bool ParseColumnWidth(const std::string& raw, uint16_t& first, uint16_t& last, float& width)
{
	std::istringstream iss(raw);
	int f, l;
	if (!(iss >> f >> l >> width))
		return false;
	first = uint16_t(f);
	last = uint16_t(l);
	return first <= last && width > 0;
}

} // namespace


bool
ReadSylk(const std::string& data, SylkSheet& sheet, std::string& error)
{
	if (data.empty()) {
		error = "empty input";
		return false;
	}

	sheet.cells.clear();
	sheet.colWidths.clear();

	// Split into lines (handle \r\n, \r, \n)
	std::vector<std::string> lines;
	{
		std::string line;
		for (size_t i = 0; i < data.size(); i++) {
			if (data[i] == '\r') {
				if (i + 1 < data.size() && data[i + 1] == '\n')
					i++;
				lines.push_back(line);
				line.clear();
			} else if (data[i] == '\n') {
				lines.push_back(line);
				line.clear();
			} else {
				line += data[i];
			}
		}
		if (!line.empty())
			lines.push_back(line);
	}

	if (lines.empty()) {
		error = "no lines";
		return false;
	}

	// First record must be ID
	SylkRecord first = ParseRecord(lines[0]);
	if (first.type != "ID") {
		error = "first record is not ID";
		return false;
	}

	// Parse records
	int curX = 0, curY = 0;  // current position (1-based)
	bool endOfStream = false;

	for (size_t i = 0; i < lines.size(); i++) {
		const std::string& line = lines[i];
		if (line.empty())
			continue;

		SylkRecord rec = ParseRecord(line);
		if (rec.type.empty())
			continue;

		// E record = end of file, ignore anything after
		if (rec.type == "E") {
			endOfStream = true;
			break;
		}
		if (endOfStream)
			break;

		// C record: cell data
		if (rec.type == "C") {
			int x = curX, y = curY;
			for (const auto& f : rec.fields) {
				if (f.letter == 'X') {
					x = std::atoi(f.value.c_str());
				} else if (f.letter == 'Y') {
					y = std::atoi(f.value.c_str());
				} else if (f.letter == 'K') {
					if (x > 0 && y > 0) {
						// Convert to 0-based
						SylkCell cell;
						cell.column = uint16_t(x - 1);
						cell.row = uint16_t(y - 1);
						cell.text = ParseKField(f.value);
						sheet.cells.push_back(cell);
					}
				}
			}
			curX = x;
			curY = y;
			continue;
		}

		// F;W record: column widths
		if (rec.type == "F") {
			for (const auto& f : rec.fields) {
				if (f.letter == 'W' && !f.value.empty()) {
					uint16_t first, last;
					float width;
					if (ParseColumnWidth(f.value, first, last, width)) {
						SylkSheet::ColumnWidth cw;
						cw.first = first;
						cw.last = last;
						cw.width = width;
						sheet.colWidths.push_back(cw);
					}
				}
			}
			continue;
		}

		// Other records: ignored
	}

	return true;
}


std::string
WriteSylk(const SylkSheet& sheet)
{
	std::ostringstream out;

	// ID record
	out << "ID;PGOBE\r\n";

	// F;W records for column widths
	for (const auto& cw : sheet.colWidths) {
		out << "F;W" << ";" << cw.first << " " << cw.last << " " << cw.width << "\r\n";
	}

	// C records for cells
	// Track current position to emit X/Y only when they change
	int curX = 0, curY = 0;

	for (const auto& cell : sheet.cells) {
		// X/Y are 1-based in SYLK
		int x = int(cell.column) + 1;
		int y = int(cell.row) + 1;

		out << "C";

		// Only emit X if it differs from current
		if (x != curX) {
			out << ";X" << x;
			curX = x;
		}

		// Only emit Y if it differs from current
		if (y != curY) {
			out << ";Y" << y;
			curY = y;
		}

		// K field: the cell value
		const std::string& val = cell.text;
		if (val.empty()) {
			// Empty cell - still need K
			out << ";K";
		} else if (val == "TRUE()" || val == "FALSE()") {
			// Boolean
			out << ";K" << (val == "TRUE()" ? "TRUE" : "FALSE");
		} else if (val.size() > 0 && val[0] == '#') {
			// Error
			out << ";K" << val;
		} else {
			// Try as number
			char* end = nullptr;
			double num = strtod(val.c_str(), &end);
			bool isNumber = (end != nullptr && end != val.c_str() && *end == '\0'
				&& num >= -DBL_MAX && num <= DBL_MAX);

			if (isNumber) {
				out << ";K" << FormatNumber(num);
			} else {
				// It's a string or formula — quote it
				bool needsQuote = false;
				for (char c : val) {
					if (c == ';' || c == '"' || c == '\\') {
						needsQuote = true;
						break;
					}
				}
				if (needsQuote) {
					out << ";K\"";
					for (char c : val) {
						if (c == '"')
							out << "\"\"";
						else if (c == ';')
							out << ";;";
						else if (c == '\\')
							out << "\\\\";
						else
							out << c;
					}
					out << '"';
				} else {
					out << ";K" << val;
				}
			}
		}
		out << "\r\n";
	}

	// E record
	out << "E\r\n";

	return out.str();
}

} // namespace gobe
