// Copying and pasting cells: TSSPasteCommand (decomp/Spreadsheet/
// TSSPasteCommand.c: ValidPaste, PerformCmd) with the flags its message
// sets (STR# 1504: 7 data, 30 formatting, 32 formulas).
#include "SSPaste.h"

#include <algorithm>
#include <map>
#include <memory>

#include "Document.h"
#include "Editing.h"

namespace gobe {

namespace {

// a cell holding a value (the value cell TFormulaCell keeps), its style kept
void
SetValue(SSCell& cell, const SSValue& value)
{
	cell.formula.clear();
	cell.text.clear();
	cell.text2.clear();
	cell.number = 0;
	cell.value = 0;
	switch (value.type) {
		case SSValue::kNumber:
			cell.kind = Sig("Dbl#");
			cell.number = value.number;
			break;
		case SSValue::kString:
			cell.kind = Sig("Strn");
			cell.text = value.text;
			break;
		case SSValue::kBool:
			cell.kind = Sig("Bool");
			cell.number = value.number;
			break;
		case SSValue::kTime:
			cell.kind = Sig("Time");
			cell.value = uint32_t(value.time);
			cell.number = double(cell.value);
			break;
		case SSValue::kError:
		case SSValue::kRange:
			cell.kind = Sig("Err#");
			cell.value = uint32_t(value.error);
			break;
		default:
			cell.kind = Sig("NONE");
			break;
	}
}

} // namespace


CopiedCells
CopyCells(const Document& doc, size_t partIndex, const SSRange& range, SSBook* book)
{
	CopiedCells copied;
	const SSPart* part = doc.SpreadsheetPart(partIndex);
	if (part == nullptr)
		return copied;
	copied.rows = uint16_t(range.to.a - range.from.a + 1);
	copied.columns = uint16_t(range.to.b - range.from.b + 1);
	copied.source = &doc;
	std::unique_ptr<SSBook> own;
	if (book == nullptr) {
		own.reset(new SSBook(doc));
		book = own.get();
	}
	SSEvaluator eval(*part, book, int(partIndex));
	for (const SSCell& cell : part->cells) {
		if (cell.id.a < range.from.a || cell.id.a > range.to.a || cell.id.b < range.from.b
				|| cell.id.b > range.to.b)
			continue;
		SSCell c = cell;
		copied.values.push_back(eval.Value(cell.id.a, cell.id.b));
		c.id = {uint16_t(cell.id.a - range.from.a), uint16_t(cell.id.b - range.from.b)};
		copied.cells.push_back(c);
	}
	return copied;
}


bool
PasteFits(const SSPart& part, CellID at, const CopiedCells& copied)
{
	return copied.rows > 0 && copied.columns > 0 && at.a >= part.used.from.a
		&& at.b >= part.used.from.b && at.a + copied.rows - 1 <= part.used.to.a
		&& at.b + copied.columns - 1 <= part.used.to.b;
}


bool
PasteCells(Document& doc, size_t partIndex, CellID at, const CopiedCells& copied, bool data,
	bool formulas, bool formatting)
{
	SSPart* part = doc.EditableSpreadsheetPart(partIndex);
	if (part == nullptr || !PasteFits(*part, at, copied))
		return false;
	formatting = formatting && copied.source == &doc;
	if (!data && !formatting)
		return false;
	auto key = [](CellID id) { return uint32_t(id.a) << 16 | id.b; };
	std::map<uint32_t, size_t> source;	// block position -> copied index
	for (size_t i = 0; i < copied.cells.size(); i++)
		source[key(copied.cells[i].id)] = i;
	std::map<uint32_t, size_t> target;	// sheet position -> part.cells index
	for (size_t i = 0; i < part->cells.size(); i++)
		target[key(part->cells[i].id)] = i;
	for (uint16_t r = 0; r < copied.rows; r++) {
		for (uint16_t c = 0; c < copied.columns; c++) {
			CellID to{uint16_t(at.a + r), uint16_t(at.b + c)};
			auto from = source.find(key({r, c}));
			const SSCell* s = from != source.end() ? &copied.cells[from->second] : nullptr;
			auto there = target.find(key(to));
			if (there == target.end()) {
				if (s == nullptr)
					continue;	// nothing here, nothing to paste
				SSCell blank;
				blank.id = to;
				blank.kind = Sig("NONE");
				part->cells.push_back(blank);
				there = target.emplace(key(to), part->cells.size() - 1).first;
			}
			SSCell& cell = part->cells[there->second];
			if (data) {
				if (s == nullptr || s->kind == Sig("NONE"))
					SetValue(cell, SSValue());
				else if (formulas || s->kind != Sig("Fmla")) {
					uint32_t style = cell.style;
					cell = *s;
					cell.id = to;
					cell.style = style;
					cell.flags &= ~uint16_t(7);
				} else
					SetValue(cell, copied.values[from->second]);
			}
			if (formatting)
				SetCellStyle(doc.styles, cell.style, s != nullptr ? s->style : 0xffffffff);
		}
	}
	// empty cells without a style go; the cells in column order, then row
	part->cells.erase(std::remove_if(part->cells.begin(), part->cells.end(),
		[](const SSCell& c) { return c.kind == Sig("NONE") && c.style == 0xffffffff; }),
		part->cells.end());
	std::stable_sort(part->cells.begin(), part->cells.end(), [](const SSCell& x, const SSCell& y) {
		return std::make_pair(x.id.b, x.id.a) < std::make_pair(y.id.b, y.id.a);
	});
	if (formatting)
		doc.stylesChanged = true;
	UpdateDependencies(doc, partIndex);
	return true;
}

} // namespace gobe
