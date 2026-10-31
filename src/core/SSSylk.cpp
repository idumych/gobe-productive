// SYLK export of a spreadsheet part: TExportSYLK (SYLKTranslator/
// TExportSYLK.c): write_header, write_cell.
#include "SSSylk.h"

#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "Document.h"
#include "Editing.h"
#include "SSEval.h"

namespace gobe {

namespace {

// SYLK's error values; ErrString's others (#ARG!, #USER!, ...) aren't
// among them
bool
SylkError(const std::string& text)
{
	static const char* const kErrors[] = {
		"#DIV/0!", "#N/A", "#VALUE!", "#REF!", "#NAME?", "#NUM!", "#NULL!"
	};
	for (const char* error : kErrors)
		if (text == error)
			return true;
	return false;
}

} // namespace


SylkSheet
SpreadsheetToSylk(const Document& doc, size_t part)
{
	SylkSheet sheet;
	const SSPart* ss = doc.SpreadsheetPart(part);
	if (ss == nullptr)
		return sheet;
	SSBook book(doc);
	SSEvaluator eval(*ss, &book, int(part));
	bool any = false;
	for (const SSCell& cell : ss->cells) {
		if (cell.id.b == 0xffff)
			continue;
		SylkCell out;
		out.row = cell.id.a;
		out.column = cell.id.b;
		if (cell.kind == Sig("Fmla") || cell.kind == Sig("FmEr")) {
			std::string input = CellInput(*ss, cell.id.a, cell.id.b);
			out.formula = FormulaToSylk(input, cell.id.a, cell.id.b);
		}
		SSValue v = eval.Value(cell.id.a, cell.id.b);
		switch (v.type) {
			case SSValue::kNumber:
				out.kind = SylkCell::kNumber;
				out.number = v.number;
				break;
			case SSValue::kTime:
				out.kind = SylkCell::kNumber;
				out.number = v.time / 86400.0 + 24106;
				break;
			case SSValue::kBool:
				out.kind = SylkCell::kBool;
				out.number = v.number != 0;
				break;
			case SSValue::kString:
				out.kind = SylkCell::kString;
				out.text = v.text;
				break;
			case SSValue::kError:
				out.kind = SylkCell::kError;
				out.text = ErrorText(v.error);
				if (!SylkError(out.text))
					out.text = "#VALUE!";
				break;
			default:
				break;
		}
		if (out.kind == SylkCell::kEmpty && out.formula.empty())
			continue;
		sheet.cells.push_back(out);
		any = true;
	}
	if (any) {
		sheet.rows = uint16_t(ss->used.to.a + 1);
		sheet.columns = uint16_t(ss->used.to.b + 1);
	}
	for (const SSHeader& column : ss->columns)
		if (column.index < 0xffff && column.cell.number != ss->defaultWidth)
			sheet.columnWidths[uint16_t(column.index)] = int(std::lround(column.cell.number / 7.45));
	return sheet;
}


bool
SylkToSpreadsheet(Document& doc, size_t part, const SylkSheet& sheet, int& badFormulas)
{
	badFormulas = 0;
	SSPart* ss = doc.EditableSpreadsheetPart(part);
	if (ss == nullptr)
		return false;
	auto isName = [&](const std::string& name) {
		return doc.NamedSelection(name, int(part)) != nullptr;
	};
	std::string error;
	// a cell typed as something, then given the kind and contents SetCellInput
	// wouldn't make from text (text that reads as a number, an error)
	auto set = [&](uint16_t row, uint16_t column, uint32_t kind, const std::string& text,
			uint32_t value) {
		SetCellInput(*ss, row, column, "x", nullptr, error);
		for (SSCell& c : ss->cells)
			if (c.id.a == row && c.id.b == column) {
				c.kind = kind;
				c.text = text;
				c.value = value;
			}
	};
	for (const SylkCell& cell : sheet.cells) {
		uint16_t row = cell.row, column = cell.column;
		if (!cell.formula.empty()) {
			std::string formula = FormulaFromSylk(cell.formula, row, column);
			if (SetCellInput(*ss, row, column, formula, isName, error))
				continue;
			badFormulas++;
		}
		switch (cell.kind) {
			case SylkCell::kNumber: {
				char text[32];
				snprintf(text, sizeof text, "%.17g", cell.number);
				SetCellInput(*ss, row, column, text, nullptr, error);
				break;
			}
			case SylkCell::kBool:
				SetCellInput(*ss, row, column, cell.number != 0 ? "TRUE" : "FALSE", nullptr, error);
				break;
			case SylkCell::kString:
				if (cell.text.empty())
					break;
				set(row, column, Sig("Strn"), cell.text, 0);
				break;
			case SylkCell::kError: {
				int code = 0;
				for (int e = 1; e < 64; e++)
					if (ErrorText(e) == cell.text) {
						code = e;
						break;
					}
				if (code != 0)
					set(row, column, Sig("Err#"), "", uint32_t(code));
				break;
			}
			case SylkCell::kEmpty:
				break;
		}
	}
	// TImportSYLK::rtd_F: characters * 7 (the export divides by 7.45); a
	// width of 0 leaves the default (SetLineSize's 0)
	for (const auto& width : sheet.columnWidths)
		if (width.second > 0)
			SetLineSize(*ss, true, width.first, width.first, float(width.second * 7));
	if (sheet.rows > ss->used.to.a + 1)
		ss->used.to.a = uint16_t(sheet.rows - 1);
	if (sheet.columns > ss->used.to.b + 1)
		ss->used.to.b = uint16_t(sheet.columns - 1);
	for (const SylkCell& cell : sheet.cells) {
		if (cell.row > ss->used.to.a)
			ss->used.to.a = cell.row;
		if (cell.column > ss->used.to.b)
			ss->used.to.b = cell.column;
	}
	UpdateDependencies(doc, part);
	return true;
}


bool
ImportSylk(const std::string& path, Document& doc, std::string& error, std::string* notice)
{
	FILE* file = fopen(path.c_str(), "rb");
	if (file == nullptr) {
		error = strerror(errno);
		return false;
	}
	std::string data;
	char buffer[65536];
	size_t length;
	while ((length = fread(buffer, 1, sizeof buffer, file)) > 0)
		data.append(buffer, length);
	fclose(file);
	SylkSheet sheet;
	if (!ReadSylk(data, sheet, error))
		return false;
	if (!CreateBlankSpreadsheet(doc, error))
		return false;
	int bad = 0;
	if (!SylkToSpreadsheet(doc, size_t(doc.MainPart()), sheet, bad)) {
		error = "no spreadsheet";
		return false;
	}
	if (bad > 0 && notice != nullptr)
		*notice = std::to_string(bad) + " formulas couldn't be read; their cells have their values.";
	return true;
}


bool
ExportSylk(const Document& doc, size_t part, const std::string& path, std::string& error)
{
	if (doc.SpreadsheetPart(part) == nullptr) {
		error = "not a spreadsheet";
		return false;
	}
	std::string text = WriteSylk(SpreadsheetToSylk(doc, part));
	FILE* file = fopen(path.c_str(), "wb");
	if (file == nullptr) {
		error = strerror(errno);
		return false;
	}
	bool ok = fwrite(text.data(), 1, text.size(), file) == text.size();
	ok = fclose(file) == 0 && ok;
	if (!ok)
		error = "write error";
	return ok;
}

} // namespace gobe
