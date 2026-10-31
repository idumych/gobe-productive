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
