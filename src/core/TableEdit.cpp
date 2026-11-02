#include "TableEdit.h"

#include <algorithm>
#include <cmath>

#include "DocWriter.h"
#include "Document.h"
#include "Editing.h"
#include "InlineParts.h"

namespace gobe {

namespace {

// the Table part's comparisons (Equal, LessThan, ...: within 0.1)
bool Eq(float a, float b) { return std::fabs(a - b) <= 0.1f; }
bool Lt(float a, float b) { return a + 0.1f < b; }
bool Gt(float a, float b) { return a > b + 0.1f; }
bool Ge(float a, float b) { return a >= b - 0.1f; }
bool Le(float a, float b) { return a <= b + 0.1f; }

const uint32_t kCellFrameFlags = 0x48;		// TTable::SetUpWPFrame

bool
EndsWithBreak(const WPParagraph& p)
{
	return !p.text.empty() && p.text.back() == '\x05';
}


// TWPPart::FilterCellBreaks on text joined from cells: an empty cell
// after another cell goes, the breaks left become line ends but the last
void
FilterCellBreaks(std::vector<WPParagraph>& paragraphs)
{
	for (int i = int(paragraphs.size()) - 2; i >= 0; i--) {
		if (EndsWithBreak(paragraphs[size_t(i)]) && paragraphs[size_t(i) + 1].text == "\x05") {
			// (its break deleted: it joins the next paragraph)
			WPPart part;
			part.paragraphs = paragraphs;
			TextPosition from = {size_t(i) + 1, 0}, to = {size_t(i) + 1, 1};
			TextPosition result;
			if (size_t(i) + 2 < paragraphs.size() && DeleteRange(part, from, to, result))
				paragraphs = part.paragraphs;
			else
				paragraphs.erase(paragraphs.begin() + i + 1);
		}
	}
	for (size_t i = 0; i + 1 < paragraphs.size(); i++)
		if (EndsWithBreak(paragraphs[i]))
			paragraphs[i].text.back() = '\n';
}

} // namespace


TableEditCell*
TableEdit::Cell(int row, int column)
{
	if (row < 0 || row >= int(rows.size()) || column < 0
			|| column >= int(rows[size_t(row)].cells.size()))
		return nullptr;
	return &rows[size_t(row)].cells[size_t(column)];
}


const TableEditCell*
TableEdit::Cell(int row, int column) const
{
	return const_cast<TableEdit*>(this)->Cell(row, column);
}


TableEditCell*
TableEdit::FindCell(float x, float y, bool grow)
{
	for (TableEditRow& row : rows)
		for (TableEditCell& c : row.cells) {
			float left = c.x, top = c.y, right = c.x + c.width, bottom = c.y + c.height;
			if (grow) {
				left -= 2;
				top -= 2;
				right += 2;
				bottom += 2;
			}
			if (x >= left && x <= right && y >= top && y <= bottom)
				return &c;
		}
	return nullptr;
}


TableEditCell*
TableEdit::CellWithId(int id)
{
	for (TableEditRow& row : rows)
		for (TableEditCell& c : row.cells)
			if (c.id == id)
				return &c;
	return nullptr;
}


float
TableEdit::RowTop(int row) const
{
	float top = 0;
	for (int r = 0; r < row && r < int(rows.size()); r++)
		top += rows[size_t(r)].height;
	return top;
}


int
TableEdit::FindRow(float y) const
{
	float bottom = 0;
	for (size_t r = 0; r < rows.size(); r++) {
		bottom += rows[r].height;
		if (Le(y, bottom))
			return int(r);
	}
	return int(rows.size()) - 1;
}


int
TableEdit::FindColumn(int row, float x, bool atEnd) const
{
	const std::vector<TableEditCell>& cells = rows[size_t(row)].cells;
	for (size_t i = 0; i < cells.size(); i++) {
		float right = cells[i].x + cells[i].width;
		if (atEnd && Le(x, right))
			return int(i);
		if (Lt(x, right))
			return int(i);
	}
	return int(cells.size());
}


TableEditCell*
TableEdit::FindPreviousCell(int row, int column)
{
	for (;;) {
		if (column - 1 >= 0)
			return Cell(row, column - 1);
		row--;
		if (row < 0)
			return nullptr;
		column = int(rows[size_t(row)].cells.size());
	}
}


void
TableEdit::RecalcSize()
{
	float width = 0, height = 0;
	for (const TableEditRow& row : rows) {
		height += row.height;
		float across = 0;
		for (const TableEditCell& c : row.cells)
			across += c.width;
		width = std::max(width, across);
	}
	size[0] = width;
	size[1] = height;
}


void
TableEdit::FixLocations()
{
	for (size_t r = 0; r < rows.size(); r++)
		for (size_t c = 0; c < rows[r].cells.size(); c++) {
			rows[r].cells[c].row = int(r);
			rows[r].cells[c].column = int(c);
		}
}


void
TableEdit::AdjustPositions(int fromRow, float dy)
{
	for (size_t r = size_t(std::max(0, fromRow)); r < rows.size(); r++)
		for (TableEditCell& c : rows[r].cells)
			c.y += dy;
}


void
TableEdit::RecalcColumnEndTable()
{
	columnEnds.clear();
	for (const TableEditRow& row : rows) {
		size_t at = 0;
		for (const TableEditCell& c : row.cells) {
			float end = c.x + c.width;
			while (at < columnEnds.size() && !Le(end, columnEnds[at]))
				at++;
			if (at < columnEnds.size() && Eq(columnEnds[at], end))
				continue;
			columnEnds.insert(columnEnds.begin() + long(at), end);
		}
	}
}


float
TableEdit::MinimumHeight(const TableEditCell& cell) const
{
	float text = textHeight ? textHeight(cell.id) : 15;
	return gutter + gutter + text;
}


float
TableEdit::MinimumTextHeight(int row) const
{
	float height = 0;
	for (const TableEditCell& c : rows[size_t(row)].cells)
		if (c.rowSpan == 1)
			height = std::max(height, MinimumHeight(c));
	return height;
}


void
TableEdit::CalcSpan(const float rect[4], int& rowFrom, int& rowTo, int& columnFrom,
	int& columnTo, bool recalc)
{
	if (recalc)
		RecalcColumnEndTable();
	int count = 0;
	rowFrom = -1;
	float top = 0;
	for (size_t r = 0; r < rows.size(); r++) {
		float height = rows[r].height;
		if (Ge(top, rect[1]) && Le(top + height, rect[3])) {
			if (rowFrom == -1)
				rowFrom = int(r);
			count++;
		}
		top += height;
	}
	int columns = 0;
	columnFrom = -1;
	float previous = 0;
	for (size_t i = 0; i < columnEnds.size(); i++) {
		float end = columnEnds[i];
		if (Ge(previous, rect[0]) && Le(end, rect[2])) {
			if (columnFrom == -1)
				columnFrom = int(i);
			columns++;
		}
		previous = end;
	}
	rowFrom = std::max(rowFrom, 0);
	columnFrom = std::max(columnFrom, 0);
	rowTo = count + rowFrom - 1;
	columnTo = columns + columnFrom - 1;
}


void
TableEdit::CalcSpan(int row, int column, int& rowSpan, int& columnSpan, bool recalc)
{
	const TableEditCell* c = Cell(row, column);
	if (c == nullptr) {
		rowSpan = columnSpan = 1;
		return;
	}
	float rect[4] = {c->x, c->y, c->x + c->width, c->y + c->height};
	int rowFrom, rowTo, columnFrom, columnTo;
	CalcSpan(rect, rowFrom, rowTo, columnFrom, columnTo, recalc);
	rowSpan = rowTo - rowFrom + 1;
	columnSpan = columnTo - columnFrom + 1;
}


void
TableEdit::CalcAllSpans()
{
	RecalcColumnEndTable();
	for (size_t r = 0; r < rows.size(); r++)
		for (size_t c = 0; c < rows[r].cells.size(); c++) {
			int rowSpan, columnSpan;
			CalcSpan(int(r), int(c), rowSpan, columnSpan, false);
			rows[r].cells[c].rowSpan = rowSpan;
			rows[r].cells[c].columnSpan = columnSpan;
		}
}


void
TableEdit::AdjustSpanningCells(float x, float y, float right, float dy, int rowCount)
{
	while (Lt(x, right)) {
		TableEditCell* c = FindCell(x + 1, y);
		if (c == nullptr || c->rowSpan == 1)
			return;
		float width = c->width;
		c->height += dy;
		c->rowSpan = int(std::nearbyint(float(rowCount) + float(c->rowSpan)));
		x += width;
	}
}


void
TableEdit::ChangeRowHeight(int row, float height, float height2, bool adjust)
{
	TableEditRow& r = rows[size_t(row)];
	height = std::max(height, MinimumTextHeight(row));
	height = std::max(height, 17.0f);
	height2 = std::max(height2, 17.0f);
	float delta = height - r.height;
	float top = RowTop(row);
	for (int above = 0; above < row; above++)
		for (TableEditCell& c : rows[size_t(above)].cells)
			if (Gt(c.y + c.height, top) && !c.ignore)
				c.height += delta;
	r.height = height;
	r.height2 = height2;
	for (TableEditCell& c : r.cells)
		if (!c.ignore)
			c.height += delta;
	if (adjust)
		AdjustPositions(row + 1, delta);
}


void
TableEdit::ChangeColumnWidth(float columnEnd, float delta)
{
	for (TableEditRow& row : rows)
		for (size_t i = 0; i < row.cells.size(); i++) {
			TableEditCell& c = row.cells[i];
			if (!Ge(c.x + c.width, columnEnd))
				continue;
			size_t from = i;
			if (Lt(c.x, columnEnd)) {
				// TCell::SetSize: no less than 20 by 17
				c.width = std::max(20.0f, c.width + delta);
				c.height = std::max(17.0f, c.height);
				from = i + 1;
			}
			for (size_t k = from; k < row.cells.size(); k++)
				row.cells[k].x += delta;
			break;
		}
	RecutAll(true, false);
}


void
TableEdit::RecutAll(bool propagate, bool changed)
{
	float shift = 0;
	for (size_t r = 0; r < rows.size(); r++) {
		float height = rows[r].height2;
		for (TableEditCell& c : rows[r].cells) {
			if (c.rowSpan == 1)
				height = std::max(height, MinimumHeight(c));
			if (!c.ignore)
				c.y += shift;
		}
		float difference = height - rows[r].height;
		if (!Eq(difference, 0)) {
			ChangeRowHeight(int(r), height, rows[r].height2, false);
			changed = true;
			shift += difference;
		}
	}
	for (size_t r = 0; r < rows.size(); r++)
		for (size_t i = 0; i < rows[r].cells.size(); i++) {
			const TableEditCell& c = rows[r].cells[i];
			if (c.rowSpan <= 1)
				continue;
			float total = 0;
			size_t k = r;
			for (; k < r + size_t(c.rowSpan) && k < rows.size(); k++)
				total += rows[k].height;
			float minimum = MinimumHeight(c);
			if (Gt(minimum, total)) {
				float height = (minimum - total) + rows[k - 1].height;
				ChangeRowHeight(int(k - 1), height, height, false);
				changed = true;
			}
		}
	(void)changed;
	if (shift != 0 || propagate)
		RecalcSize();
}


void
TableEdit::RemoveEmptyRows()
{
	bool removed = false;
	for (size_t r = 0; r < rows.size(); r++) {
		if (!rows[r].cells.empty())
			continue;
		removed = true;
		// TTable::RemoveEmptyRow: its height to the row above; cells
		// reaching into it a row shorter
		float height = rows[r].height;
		if (r != 0) {
			rows[r - 1].height += height;
			rows[r - 1].height2 += height;
		}
		for (size_t above = 0; above < r; above++)
			for (TableEditCell& c : rows[above].cells)
				if (int(r) < c.rowSpan + int(above))
					c.rowSpan--;
		rows.erase(rows.begin() + long(r));
		r--;
	}
	if (removed)
		CalcAllSpans();
}


std::vector<const TableEditCell*>
TableEdit::Selected(const TableSelection& selection) const
{
	std::vector<const TableEditCell*> cells;
	for (int r = std::max(0, selection.from); r <= selection.to && r < int(rows.size()); r++)
		for (const TableEditCell& c : rows[size_t(r)].cells)
			if (Gt(c.x + c.width, selection.left) && Lt(c.x, selection.right))
				cells.push_back(&c);
	return cells;
}


TableSelection
TableEdit::SelectionOf(int row1, int column1, int row2, int column2) const
{
	if (row2 < row1) {
		std::swap(row1, row2);
		std::swap(column1, column2);
	}
	TableSelection selection;
	const TableEditCell* a = Cell(row1, column1);
	const TableEditCell* b = Cell(row2, column2);
	if (a == nullptr || b == nullptr)
		return selection;
	selection.left = std::min(a->x, b->x);
	selection.right = std::max(a->x + a->width, b->x + b->width);
	selection.from = row1;
	selection.to = row2;
	return selection;
}


TableSelection
TableEdit::SelectRows(int from, int to) const
{
	int last = 0;
	for (int r = from; r <= to && r < int(rows.size()); r++)
		last = std::max(last, int(rows[size_t(r)].cells.size()) - 1);
	// (the last row's own last cell when it has fewer)
	if (to >= 0 && to < int(rows.size()))
		last = std::min(last, int(rows[size_t(to)].cells.size()) - 1);
	return SelectionOf(from, 0, to, last);
}


TableSelection
TableEdit::SelectColumns(float left, float right) const
{
	TableSelection selection;
	selection.left = left;
	selection.right = right;
	selection.from = 0;
	selection.to = int(rows.size()) - 1;
	return selection;
}


void
TableEdit::SpanningRect(const TableSelection& selection, float rect[4]) const
{
	rect[0] = size[0];
	rect[1] = size[1];
	rect[2] = 0;
	rect[3] = 0;
	for (const TableEditCell* c : Selected(selection)) {
		rect[0] = std::min(rect[0], c->x);
		rect[1] = std::min(rect[1], c->y);
		rect[2] = std::max(rect[2], c->x + c->width);
		rect[3] = std::max(rect[3], c->y + c->height);
	}
}


bool
TableEdit::SelectionIsRectangular(const TableSelection& selection, int& count) const
{
	count = 0;
	std::vector<const TableEditCell*> selected = Selected(selection);
	if (selected.empty())
		return false;
	float rect[4];
	SpanningRect(selection, rect);
	rect[0] += 1;
	rect[1] += 1;
	rect[2] -= 1;
	rect[3] -= 1;
	for (const TableEditRow& row : rows)
		for (const TableEditCell& c : row.cells) {
			bool meets = !(c.x > rect[2] || c.x + c.width < rect[0] || c.y > rect[3]
				|| c.y + c.height < rect[1]);
			if (!meets)
				continue;
			if (std::find(selected.begin(), selected.end(), &c) == selected.end())
				return false;
			count++;
		}
	return true;
}


void
TableEdit::SelectionSpan(const TableSelection& selection, int& rowCount, int& columnCount)
{
	float rect[4];
	SpanningRect(selection, rect);
	int rowFrom, rowTo, columnFrom, columnTo;
	CalcSpan(rect, rowFrom, rowTo, columnFrom, columnTo, true);
	rowCount = rowTo - rowFrom + 1;
	columnCount = columnTo - columnFrom + 1;
}


void
TableEdit::VirtualColumn(int row, int column, float& left, float& right)
{
	RecalcColumnEndTable();
	const TableEditCell* c = Cell(row, column);
	if (c == nullptr) {
		left = right = 0;
		return;
	}
	left = c->x;
	right = c->x + c->width;
	float previous = 0;
	for (float end : columnEnds) {
		if (left == previous)
			right = end;
		previous = end;
	}
}


int
TableEdit::FrameBefore(int id) const
{
	auto at = std::find(frameOrder.begin(), frameOrder.end(), id);
	if (at == frameOrder.end() || at == frameOrder.begin())
		return -1;
	return *(at - 1);
}


int
TableEdit::InsertCell(int row, int column, float x, float y, float width, float height,
	int afterId, int like)
{
	// its text: a cell break where it goes, styled as the text there (the
	// next cell's first paragraph, or what follows the cells)
	auto after = afterId < 0 ? frameOrder.begin()
		: std::find(frameOrder.begin(), frameOrder.end(), afterId) + 1;
	const WPParagraph* there = nullptr;
	for (auto it = after; it != frameOrder.end() && there == nullptr; ++it)
		if (!texts[size_t(*it)].paragraphs.empty())
			there = &texts[size_t(*it)].paragraphs.front();
	if (there == nullptr && !trailer.empty())
		there = &trailer.front();
	TableEditText text;
	WPParagraph p;
	if (there != nullptr) {
		p.parStyle = there->parStyle;
		p.value12 = there->value12;
		if (!there->runs.empty())
			p.runs.push_back({0, there->runs.front().style});
	}
	p.text = "\x05";
	text.paragraphs.push_back(p);
	if (like >= 0 && like < int(texts.size()))
		text.frame = texts[size_t(like)].frame;
	else if (!texts.empty())
		text.frame = texts.front().frame;
	text.frame.values[3] = kCellFrameFlags;
	text.made = true;
	int id = int(texts.size());
	texts.push_back(text);
	frameOrder.insert(after, id);

	TableEditCell cell;
	cell.x = x;
	cell.y = y;
	cell.width = width;
	cell.height = height;
	cell.id = id;
	cell.row = row;
	std::vector<TableEditCell>& cells = rows[size_t(row)].cells;
	if (column < 0 || column > int(cells.size()))
		column = int(cells.size());
	cell.column = column;
	cells.insert(cells.begin() + column, cell);
	return id;
}


void
TableEdit::DropText(int id)
{
	frameOrder.erase(std::remove(frameOrder.begin(), frameOrder.end(), id), frameOrder.end());
	if (id >= 0 && id < int(texts.size()))
		texts[size_t(id)].paragraphs.clear();
}


void
TableEdit::InsertRow(int row)
{
	if (rows.empty())
		return;
	int count = int(rows.size());
	int reference = row < count ? row : count - 1;
	int afterId;
	if (row < count)
		afterId = rows[size_t(row)].cells.empty() ? -1
			: FrameBefore(rows[size_t(row)].cells.front().id);
	else
		afterId = frameOrder.empty() ? -1 : frameOrder.back();
	TableEditRow made;
	made.height2 = rows[size_t(reference)].height2;
	made.height = rows[size_t(reference)].height;
	float y = RowTop(reference) + 1;
	// the reference row's cells as they are
	std::vector<TableEditCell> above = rows[size_t(reference)].cells;
	float referenceHeight = rows[size_t(reference)].height;
	int at = row < 0 ? count : std::min(row, count);
	rows.insert(rows.begin() + at, made);
	float end = 0;
	int column = 0;
	for (const TableEditCell& c : above) {
		if (Lt(end, c.x))
			AdjustSpanningCells(end, y, c.x, referenceHeight, 0);
		end = c.x + c.width;
		float cellY = row == count ? c.y + made.height : c.y;
		afterId = InsertCell(at, column++, c.x, cellY, c.width, made.height, afterId, c.id);
	}
	if (Lt(end, size[0]))
		AdjustSpanningCells(end, y, size[0], referenceHeight, 0);
	FixLocations();
	AdjustPositions(at + 1, made.height);
	RecalcSize();
}


void
TableEdit::InsertColumn(float left, float right)
{
	float width = right - left;
	for (size_t r = 0; r < rows.size(); r++) {
		if (rows[r].cells.empty())
			continue;
		int i = FindColumn(int(r), right, true);
		if (i == int(rows[r].cells.size()))
			i--;
		const TableEditCell& c = rows[r].cells[size_t(i)];
		if (Eq(c.x + c.width, right))
			width = std::min(width, c.width);
	}
	float top = 0;
	for (size_t r = 0; r < rows.size(); r++) {
		if (rows[r].cells.empty())
			continue;
		int i = FindColumn(int(r), left, false);
		std::vector<TableEditCell>& cells = rows[r].cells;
		bool make = true;
		int afterId = -1, like = -1;
		if (i == int(cells.size())) {
			const TableEditCell& last = cells.back();
			afterId = last.id;
			TableEditCell* found = FindCell(left + 1, top);
			like = found != nullptr ? found->id : last.id;
		} else if (!Lt(cells[size_t(i)].x, left)) {
			afterId = FrameBefore(cells[size_t(i)].id);
			like = cells[size_t(i)].id;
		} else {
			make = false;
			cells[size_t(i)].width += width;
			cells[size_t(i)].columnSpan++;
		}
		if (make)
			InsertCell(int(r), i, left, top, width, rows[r].height, afterId, like);
		for (size_t k = size_t(i) + 1; k < rows[r].cells.size(); k++)
			rows[r].cells[k].x += width;
		top += rows[r].height;
	}
	FixLocations();
	RecalcSize();
}


void
TableEdit::DeleteRow(int row)
{
	if (row < 0 || row >= int(rows.size()) || rows.size() < 2)
		return;
	// (TDeleteRowCommand: the row's cells' text first)
	for (const TableEditCell& c : rows[size_t(row)].cells)
		DropText(c.id);
	TableEditRow& r = rows[size_t(row)];
	if (r.cells.empty())
		return;
	float height = r.height;
	float y = r.cells.front().y + r.cells.front().height - 1;
	float end = 0;
	int next = row + 1;
	std::vector<TableEditCell> cells = r.cells;
	for (size_t i = 0; i < cells.size(); i++) {
		const TableEditCell& c = cells[i];
		if (Lt(end, c.x))
			AdjustSpanningCells(end, y, c.x, -height, 0);
		end = c.x + c.width;
		if (c.rowSpan > 1 && next < int(rows.size())) {
			int k = FindColumn(next, c.x, false);
			if (k != int(rows[size_t(next)].cells.size())) {
				// the rest of it in the next row
				int target = rows[size_t(next)].cells[size_t(k)].id;
				int id = InsertCell(next, int(i), c.x, height + c.y, c.width, c.height - height,
					FrameBefore(target), c.id);
				TableEditCell* made = CellWithId(id);
				made->rowSpan = c.rowSpan - 1;
				made->columnSpan = c.columnSpan;
			}
		}
	}
	if (Lt(end, size[0]))
		AdjustSpanningCells(end, y, size[0], -height, 0);
	rows.erase(rows.begin() + row);
	FixLocations();
	AdjustPositions(row, -height);
	RecalcSize();
}


void
TableEdit::DeleteColumn(float left, float right)
{
	bool several = false;
	for (const TableEditRow& row : rows)
		if (row.cells.size() > 1)
			several = true;
	if (!several)
		return;
	float width = right - left;
	for (int r = int(rows.size()) - 1; r >= 0; r--) {
		std::vector<TableEditCell>& cells = rows[size_t(r)].cells;
		size_t i = 0;
		while (i < cells.size()) {
			TableEditCell& c = cells[i];
			if (Ge(c.x, left) && Le(c.x + c.width, right)) {
				DropText(c.id);
				cells.erase(cells.begin() + long(i));
				continue;
			}
			if (Ge(c.x + c.width, right)) {
				if (!Ge(c.x, right))
					c.width -= width;
				else
					c.x -= width;
			}
			i++;
		}
	}
	FixLocations();
	RecalcSize();
}


void
TableEdit::MergeCells(const TableSelection& selection)
{
	std::vector<const TableEditCell*> selected = Selected(selection);
	if (selected.empty())
		return;
	float rect[4];
	SpanningRect(selection, rect);
	std::vector<int> ids;
	for (const TableEditCell* c : selected)
		ids.push_back(c->id);
	// the text: the selected cells' joined (in the text's order), put where
	// the first of them began, the others' gone
	std::vector<WPParagraph> joined;
	std::vector<WPParagraph> flat;
	size_t at = size_t(-1);
	for (int id : frameOrder) {
		const std::vector<WPParagraph>& text = texts[size_t(id)].paragraphs;
		if (std::find(ids.begin(), ids.end(), id) != ids.end()) {
			if (at == size_t(-1))
				at = flat.size();
			joined.insert(joined.end(), text.begin(), text.end());
		} else
			flat.insert(flat.end(), text.begin(), text.end());
	}
	FilterCellBreaks(joined);
	flat.insert(flat.begin() + long(at), joined.begin(), joined.end());
	// the cells: the one at the rectangle's top left takes it, the others go
	int survivor = -1;
	float top = 0;
	for (TableEditRow& row : rows) {
		if (Ge(top, rect[1]) && Le(top + row.height, rect[3])) {
			for (size_t i = 0; i < row.cells.size(); i++) {
				TableEditCell& c = row.cells[i];
				if (!Ge(c.x, rect[0]) || !Le(c.x + c.width, rect[2]))
					continue;
				if (Eq(c.x, rect[0]) && Eq(c.y, rect[1])) {
					c.width = std::max(20.0f, rect[2] - rect[0]);
					c.height = std::max(17.0f, rect[3] - rect[1]);
					survivor = c.id;
					continue;
				}
				frameOrder.erase(std::remove(frameOrder.begin(), frameOrder.end(), c.id),
					frameOrder.end());
				texts[size_t(c.id)].paragraphs.clear();
				row.cells.erase(row.cells.begin() + long(i));
				i--;
			}
		}
		top += row.height;
	}
	// the text flows through the frames left, each to its cell break
	size_t p = 0;
	for (int id : frameOrder) {
		std::vector<WPParagraph>& text = texts[size_t(id)].paragraphs;
		text.clear();
		while (p < flat.size()) {
			text.push_back(flat[p++]);
			if (EndsWithBreak(text.back()))
				break;
		}
	}
	if (p < flat.size())
		trailer.insert(trailer.begin(), flat.begin() + long(p), flat.end());
	if (TableEditCell* c = CellWithId(survivor)) {
		int rowSpan, columnSpan;
		CalcSpan(c->row, c->column, rowSpan, columnSpan, true);
		c->rowSpan = rowSpan;
		c->columnSpan = columnSpan;
	}
	RemoveEmptyRows();
	FixLocations();
}


TableSelection
TableEdit::SplitCell(int row, int column, int columns, int rowCount)
{
	TableSelection result;
	TableEditCell* cell = Cell(row, column);
	if (cell == nullptr || columns < 1 || rowCount < 1)
		return result;
	std::vector<float> tops;
	for (size_t r = 0; r < rows.size(); r++)
		tops.push_back(RowTop(int(r)));
	int id = cell->id;
	float rect[4] = {cell->x, cell->y, cell->x + cell->width, cell->y + cell->height};
	float x = cell->x, y = cell->y;
	float width = cell->width / float(columns);
	float remaining = rect[3] - rect[1];
	int last = row;
	while (last < int(rows.size()) - 1 && Lt(tops[size_t(last) + 1], rect[3]))
		last++;
	int r = row;
	for (int i = 0; i < rowCount; i++, r++) {
		float height;
		if (last < r) {
			TableEditRow made;
			height = remaining / float(rowCount - i);
			made.height = made.height2 = height;
			rows.insert(rows.begin() + std::min(r, int(rows.size())), made);
		} else {
			TableEditRow& existing = rows[size_t(r)];
			if (i == rowCount - 1)
				height = remaining;
			else if (r == last) {
				height = remaining / float(rowCount - i);
				existing.height = existing.height2 = height;
			} else
				height = existing.height;
		}
		int at = FindColumn(r, x + width, true);
		for (int j = 0; j < columns; j++) {
			if (i == 0 && j == 0) {
				TableEditCell* c = CellWithId(id);
				c->width = width;
				c->height = height;
				continue;
			}
			TableEditCell* previous = FindPreviousCell(r, at + j);
			InsertCell(r, at + j, x + float(j) * width, y, width, height,
				previous != nullptr ? previous->id : -1, id);
		}
		y += height;
		remaining -= height;
	}
	FixLocations();
	CalcAllSpans();
	// TTablePart::RectToCellLocations
	TableEditCell* first = FindCell(rect[0] + 1, rect[1] + 1);
	TableEditCell* end = FindCell(rect[2] - 1, rect[3] - 1);
	int firstRow = first ? first->row : 0, firstColumn = first ? first->column : 0;
	int endRow = end ? end->row : 0, endColumn = end ? end->column : 0;
	RecutAll(false, true);
	return SelectionOf(firstRow, firstColumn, endRow, endColumn);
}


void
TableEdit::DistributeRows(int from, int to)
{
	if (from < 0 || to >= int(rows.size()) || from > to)
		return;
	float total = 0, tallest = 0;
	for (int r = from; r <= to; r++) {
		total += rows[size_t(r)].height;
		tallest = std::max(tallest, MinimumTextHeight(r));
	}
	float height = std::max(total / float(to - from + 1), tallest);
	for (int r = from; r <= to; r++)
		ChangeRowHeight(r, height, height, true);
	RecalcSize();
}


void
TableEdit::DistributeColumns(float left, float right)
{
	RecalcColumnEndTable();
	int count = 0;
	for (float end : columnEnds)
		if (Gt(end, left) && Le(end, right))
			count++;
	if (count == 0)
		return;
	// (Round(x, 0))
	float width = std::floor((right - left) / float(count) + 0.5f);
	std::vector<float> ends = columnEnds;
	for (int i = int(ends.size()) - 1; i >= 0; i--) {
		float previous = i == 0 ? 0 : ends[size_t(i) - 1];
		float end = ends[size_t(i)];
		if (Gt(end, left) && Le(end, right))
			ChangeColumnWidth(end, width - (end - previous));
	}
}


void
TableEdit::SetCellGutter(float value)
{
	gutter = value;
	RecutAll(true, true);
}


bool
LoadTableEdit(const Document& doc, size_t table, TableEdit& edit)
{
	const TablePart* t = doc.Table(table);
	const WPPart* wp = t != nullptr ? doc.WordPart(t->textPart) : nullptr;
	if (wp == nullptr || t->textPart >= doc.tails.size())
		return false;
	const Portal* portal = nullptr;
	for (const Portal& p : doc.tails[t->textPart].portals)
		if (p.part == uint32_t(table))
			portal = &p;
	if (portal == nullptr)
		return false;
	edit = TableEdit();
	edit.size[0] = t->size[0];
	edit.size[1] = t->size[1];
	edit.gutter = t->value20;
	std::vector<uint32_t> starts;
	uint32_t at = 0;
	for (const WPParagraph& p : wp->paragraphs) {
		starts.push_back(at);
		at += uint32_t(p.text.size());
	}
	// the cells; their frames by number
	std::vector<std::pair<uint32_t, int>> byFrame;
	for (const TableRow& row : t->rows) {
		TableEditRow r;
		r.height = row.top;
		r.height2 = row.height;
		for (const TableCell& c : row.cells) {
			TableEditCell cell;
			cell.row = int(c.row);
			cell.column = int(c.column);
			cell.x = c.origin[0];
			cell.y = c.origin[1];
			cell.width = c.size[0];
			cell.height = c.size[1];
			cell.rowSpan = int(c.values[0]);
			cell.columnSpan = int(c.values[1]);
			cell.id = int(edit.texts.size());
			TableEditText text;
			for (const Frame& f : portal->frames)
				if (f.values[1] == c.frame[2])
					text.frame = f;
			edit.texts.push_back(text);
			byFrame.push_back({c.frame[2], cell.id});
			r.cells.push_back(cell);
		}
		edit.rows.push_back(r);
	}
	// the frames' order, each cell's paragraphs, what follows them
	uint32_t cellsEnd = 0;
	for (const Frame& f : portal->frames)
		for (const auto& [number, id] : byFrame)
			if (number == f.values[1]) {
				edit.frameOrder.push_back(id);
				std::vector<WPParagraph>& text = edit.texts[size_t(id)].paragraphs;
				for (size_t k = 0; k < starts.size(); k++)
					if (starts[k] >= f.textStart && starts[k] < f.textStart + f.textLength)
						text.push_back(wp->paragraphs[k]);
				cellsEnd = std::max(cellsEnd, f.textStart + f.textLength);
			}
	for (size_t k = 0; k < starts.size(); k++)
		if (starts[k] >= cellsEnd)
			edit.trailer.push_back(wp->paragraphs[k]);
	edit.RecalcColumnEndTable();
	return !edit.rows.empty();
}


bool
StoreTableEdit(Document& doc, size_t table, const TableEdit& edit, std::string& error)
{
	TablePart* t = nullptr;
	for (auto& [index, part] : doc.tableParts)
		if (index == table)
			t = &part;
	WPPart* wp = t != nullptr ? doc.EditableWordPart(t->textPart) : nullptr;
	if (wp == nullptr || t->textPart >= doc.tails.size()) {
		error = "not a table";
		return false;
	}
	Portal* portal = nullptr;
	for (Portal& p : doc.tails[t->textPart].portals)
		if (p.part == uint32_t(table))
			portal = &p;
	if (portal == nullptr) {
		error = "the table's cells have no frames";
		return false;
	}
	// the text: the cells' in the frames' order, then what follows
	std::vector<WPParagraph> paragraphs;
	std::vector<std::pair<uint32_t, uint32_t>> ranges(edit.texts.size(), {0, 0});
	std::vector<uint32_t> number(edit.texts.size(), 0);
	uint32_t at = 0;
	for (size_t k = 0; k < edit.frameOrder.size(); k++) {
		int id = edit.frameOrder[k];
		uint32_t length = 0;
		for (const WPParagraph& p : edit.texts[size_t(id)].paragraphs) {
			length += uint32_t(p.text.size());
			paragraphs.push_back(p);
		}
		ranges[size_t(id)] = {at, length};
		number[size_t(id)] = uint32_t(k);
		at += length;
	}
	std::vector<WPParagraph> trailer = edit.trailer;
	if (trailer.empty()) {
		WPParagraph end;
		if (!paragraphs.empty()) {
			end.parStyle = paragraphs.back().parStyle;
			if (!paragraphs.back().runs.empty())
				end.runs.push_back({0, paragraphs.back().runs.back().style});
		}
		trailer.push_back(end);
	}
	paragraphs.insert(paragraphs.end(), trailer.begin(), trailer.end());
	wp->paragraphs = paragraphs;
	wp->edits.clear();

	// the frames, in that order, inset by the gutter (TCell::AdjustSize)
	const float gutter = edit.gutter;
	std::vector<Frame> frames(edit.frameOrder.size());
	std::vector<TableRow> rows;
	for (const TableEditRow& r : edit.rows) {
		TableRow row;
		row.top = r.height;
		row.height = r.height2;
		for (const TableEditCell& c : r.cells) {
			TableCell cell;
			cell.row = uint32_t(c.row);
			cell.column = uint32_t(c.column);
			cell.origin[0] = c.x;
			cell.origin[1] = c.y;
			cell.size[0] = c.width;
			cell.size[1] = c.height;
			cell.values[0] = uint32_t(c.rowSpan);
			cell.values[1] = uint32_t(c.columnSpan);
			cell.frame[0] = t->textPart;
			cell.frame[1] = portal->values[0];
			cell.frame[2] = number[size_t(c.id)];
			row.cells.push_back(cell);
			Frame f = edit.texts[size_t(c.id)].frame;
			f.part = t->textPart;
			f.container = uint32_t(table);
			f.values[1] = number[size_t(c.id)];
			f.values[2] = number[size_t(c.id)];
			f.origin[0] = c.x + gutter + 1;
			f.origin[1] = c.y + gutter;
			f.extent[0] = std::max(1.0f, c.width - 2 * (gutter + 1));
			f.extent[1] = std::max(1.0f, c.height - 2 * gutter);
			f.textStart = ranges[size_t(c.id)].first;
			f.textLength = ranges[size_t(c.id)].second;
			f.textAt = 1;
			// TWPFrame +0x60: the gutter, the last of its fields
			if (f.raw.size() >= 24) {
				DocWriter g;
				g.Float(gutter);
				std::copy(g.Data().begin(), g.Data().end(), f.raw.end() - 4);
			}
			if (number[size_t(c.id)] < frames.size())
				frames[number[size_t(c.id)]] = f;
		}
		rows.push_back(row);
	}
	portal->frames = frames;
	portal->values[1] = uint32_t(frames.size());
	portal->values[2] = uint32_t(frames.size());
	t->rows = rows;
	t->size[0] = edit.size[0];
	t->size[1] = edit.size[1];
	t->value20 = gutter;
	// TTable::CalcAndPropagateSize: its frames as large as it
	if (table < doc.tails.size())
		for (Portal& p : doc.tails[table].portals)
			for (Frame& f : p.frames) {
				f.extent[0] = edit.size[0];
				f.extent[1] = edit.size[1];
			}
	doc.dirtyParts.insert(table);
	doc.dirtyTails.insert(table);
	doc.dirtyParts.insert(t->textPart);
	doc.dirtyTails.insert(t->textPart);
	doc.layoutRanges.insert(t->textPart);
	return true;
}


size_t
TableEditParagraph(const TableEdit& edit, int id)
{
	size_t paragraph = 0;
	for (int other : edit.frameOrder) {
		if (other == id)
			return paragraph;
		paragraph += edit.texts[size_t(other)].paragraphs.size();
	}
	return paragraph;
}

} // namespace gobe
