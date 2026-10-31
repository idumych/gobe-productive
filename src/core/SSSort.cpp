// Sort Cells: TSSSortCommand::Perform and SwapCells (TSSSortCommand.c),
// TSSSortTable::Compare (TSSSortTable.c), the cells' SortRank and
// operator< (TNumberCell.c, TStringCell.c, TBooleanCell.c, TErrorCell.c,
// TDataCell.c: TDataCell::Compare) and QuickSort (Gobe Productive/
// _global.c).
#include "SSSort.h"

#include <algorithm>
#include <cstring>
#include <map>

#include "Document.h"
#include "Editing.h"
#include "SSEval.h"

namespace gobe {

namespace {

// TDataCell::SortRank: numbers (and times) 100, strings 200, booleans 300,
// errors 400, empty cells 32000
int
SortRank(const SSValue& v)
{
	switch (v.type) {
		case SSValue::kNumber:
		case SSValue::kTime:
			return 100;
		case SSValue::kString:
			return 200;
		case SSValue::kBool:
			return 300;
		case SSValue::kNone:
			return 32000;
		default:
			return 400;
	}
}


// the cells' operator< for two values of the same rank
bool
Less(const SSValue& a, const SSValue& b)
{
	switch (a.type) {
		case SSValue::kNumber:
		case SSValue::kTime: {
			double x = 0, y = 0;
			a.GetNum(x);
			b.GetNum(y);
			return x < y;
		}
		case SSValue::kString:
			return strcmp(a.text.c_str(), b.text.c_str()) < 0;
		case SSValue::kBool:
			return a.number < b.number;
		case SSValue::kNone:
			return false;
		default:
			return a.error < b.error;
	}
}


// TDataCell::Compare
int
CompareValues(const SSValue& a, const SSValue& b)
{
	int ra = SortRank(a), rb = SortRank(b);
	if (ra < rb)
		return -1;
	if (rb < ra)
		return 1;
	if (Less(a, b))
		return -1;
	return Less(b, a) ? 1 : 0;
}


struct SortTable {
	std::vector<SortKey> keys;
	bool vertical;
	SSEvaluator* eval;

	// TSSSortTable::Compare at key `level`: ties go to the next key, after
	// the last one to the cells' order; a descending key turns the result
	// round unless one of its cells is empty
	int Compare(long i, long j, size_t level) const
	{
		const SSRange& key = keys[level].range;
		CellID a = key.from, b = key.from;
		if (vertical) {
			a.a = uint16_t(a.a + i);
			b.a = uint16_t(b.a + j);
		} else {
			a.b = uint16_t(a.b + i);
			b.b = uint16_t(b.b + j);
		}
		SSValue va = eval->Value(a.a, a.b), vb = eval->Value(b.a, b.b);
		bool empty = va.type == SSValue::kNone || vb.type == SSValue::kNone;
		int result = CompareValues(va, vb);
		if (result == 0) {
			if (level + 1 < keys.size())
				result = Compare(i, j, level + 1);
			else if (a.a < b.a || (a.a == b.a && a.b < b.b))	// TCellID::operator<
				result = -1;
			else
				result = (b.a < a.a || (b.a == a.a && b.b < a.b)) ? 1 : 0;
		}
		if (!empty && !keys[level].ascending)
			result = -result;
		return result;
	}
};


// QuickSort: Wirth's non-recursive quicksort on the indices 0..size-1
std::vector<long>
QuickSort(const SortTable& table, long size)
{
	std::vector<long> index;
	for (long k = 0; k < size; k++)
		index.push_back(k);
	if (size < 2)
		return index;
	std::vector<std::pair<long, long>> stack{{0, size - 1}};
	while (!stack.empty()) {
		long l = stack.back().first, r = stack.back().second;
		stack.pop_back();
		do {
			long pivot = index[(l + r) >> 1];
			long i = l, j = r;
			do {
				while (table.Compare(index[i], pivot, 0) == -1)
					i++;
				while (table.Compare(pivot, index[j], 0) == -1)
					j--;
				if (j < i)
					break;
				std::swap(index[i], index[j]);
				i++;
				j--;
			} while (i <= j);
			if (j - l < r - i) {
				if (i < r)
					stack.push_back({i, r});
				r = j;
			} else {
				if (l < j)
					stack.push_back({l, j});
				l = i;
			}
		} while (l < r);
	}
	return index;
}

} // namespace


bool
SortCells(Document& doc, size_t partIndex, const SSRange& range, bool vertical,
	const std::vector<SortKey>& keys)
{
	SSPart* part = doc.EditableSpreadsheetPart(partIndex);
	if (part == nullptr || keys.empty())
		return false;
	// TSSSortTable::SortSize: the first key's length
	const SSRange& first = keys[0].range;
	long size = vertical ? first.to.a - first.from.a + 1 : first.to.b - first.from.b + 1;
	long length = vertical ? range.to.a - range.from.a + 1 : range.to.b - range.from.b + 1;
	if (length != size)
		return false;

	SSBook book(doc);
	SSEvaluator eval(*part, &book, int(partIndex));
	SortTable table{keys, vertical, &eval};
	std::vector<long> index = QuickSort(table, size);

	// SwapCells: line k of the range gets what line index[k] held; the
	// cells move with their styles, but the borders stay where they were
	std::map<uint32_t, SSCell> before;
	auto key = [](CellID id) { return uint32_t(id.a) << 16 | id.b; };
	auto inRange = [&](CellID id) {
		return range.from.a <= id.a && id.a <= range.to.a && range.from.b <= id.b
			&& id.b <= range.to.b;
	};
	std::vector<SSCell> kept;
	for (SSCell& cell : part->cells) {
		if (inRange(cell.id))
			before[key(cell.id)] = std::move(cell);
		else
			kept.push_back(std::move(cell));
	}
	part->cells = std::move(kept);
	for (long k = 0; k < size; k++) {
		long from = index[k];
		long across = vertical ? range.to.b - range.from.b + 1 : range.to.a - range.from.a + 1;
		for (long n = 0; n < across; n++) {
			CellID to = vertical
				? CellID{uint16_t(range.from.a + k), uint16_t(range.from.b + n)}
				: CellID{uint16_t(range.from.a + n), uint16_t(range.from.b + k)};
			CellID at = vertical
				? CellID{uint16_t(range.from.a + from), to.b}
				: CellID{to.a, uint16_t(range.from.b + from)};
			auto source = before.find(key(at));
			auto here = before.find(key(to));
			uint32_t hereStyle = here != before.end() ? here->second.style : 0xffffffff;
			if (source == before.end()) {
				// an empty place moved here: only the borders that were here
				uint32_t style = StyleWithBorders(doc.styles, 0xffffffff, hereStyle);
				if (style != 0xffffffff) {
					SSCell cell;
					cell.id = to;
					cell.kind = Sig("NONE");
					SetCellStyle(doc.styles, cell.style, style);
					part->cells.push_back(cell);
					doc.stylesChanged = true;
				}
				continue;
			}
			SSCell cell = source->second;
			cell.id = to;
			uint32_t style = StyleWithBorders(doc.styles, cell.style, hereStyle);
			if (style != cell.style) {
				SetCellStyle(doc.styles, cell.style, style);
				doc.stylesChanged = true;
			}
			part->cells.push_back(std::move(cell));
		}
	}
	// (the cells in column order, then row, as the data manager keeps them)
	std::stable_sort(part->cells.begin(), part->cells.end(), [](const SSCell& x, const SSCell& y) {
		return std::make_pair(x.id.b, x.id.a) < std::make_pair(y.id.b, y.id.a);
	});
	// the formulas of the range taken out of the calculation order and put
	// back at their new places (RemoveFormula, AddFormula)
	std::vector<CellID> dependents;
	for (CellID id : part->dependents)
		if (!inRange(id))
			dependents.push_back(id);
	part->dependents = std::move(dependents);
	UpdateDependencies(doc, partIndex);
	return true;
}


std::string
RangeName(const SSRange& range)
{
	std::string name = CellName(range.from);
	if (range.from.a != range.to.a || range.from.b != range.to.b)
		name += ".." + CellName(range.to);
	return name;
}


namespace {

// TCellID::Set: a cell's name at `text`, `text` moved past it
bool
ParseCellName(const char*& text, CellID& cell)
{
	while (*text == ' ')
		text++;
	auto upper = [](char c) { return c >= 'a' && c <= 'z' ? char(c - 32) : c; };
	char c = upper(*text);
	if (c < 'A' || c > 'Z')
		return false;
	int column = c - 'A';
	text++;
	c = upper(*text);
	if (c >= 'A' && c <= 'Z') {
		column = (column + 1) * 26 + c - 'A';
		text++;
	}
	int row = 0;
	while (*text >= '0' && *text <= '9')
		row = uint16_t(row * 10 + *text++ - '0');
	if (row == 0 || column >= 0x2bf || row - 1 >= 0x4000)
		return false;
	cell = {uint16_t(row - 1), uint16_t(column)};
	return true;
}

} // namespace


bool
ParseRangeName(const std::string& text, SSRange& range)
{
	const char* at = text.c_str();
	CellID from, to;
	if (!ParseCellName(at, from) || at[0] != '.' || at[1] != '.')
		return false;
	at += 2;
	if (!ParseCellName(at, to))
		return false;
	range = {from, to};
	return true;
}

} // namespace gobe
