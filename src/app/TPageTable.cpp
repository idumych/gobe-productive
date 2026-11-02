// TPageView's tables (TTablePart, TTableClickHandler): a selection of cells
// dragged across them, the Table menu's commands (TTablePart::HandleMessage)
// and what they enable (TTablePart::FixMenus).
#include "TPageView.h"

#include <Alert.h>
#include <Beep.h>
#include <Window.h>

#include <algorithm>

#include "GobeResources.h"
#include "TTableDialogs.h"

namespace gobe {

bool
TPageView::LoadTable(size_t table, TableEdit& edit) const
{
	if (!LoadTableEdit(fDocument, table, edit))
		return false;
	// TCell::MinimumHeight: how high a cell's text is laid out
	const TablePart* t = fDocument.Table(table);
	size_t text = t->textPart;
	const TableEdit* model = &edit;
	edit.textHeight = [this, text, model](int id) -> float {
		size_t first = TableEditParagraph(*model, id);
		size_t count = model->texts[size_t(id)].paragraphs.size();
		float top = 0, bottom = 0;
		bool any = false;
		for (const Line& line : fLines) {
			if (line.part != text || line.paragraph < first || line.paragraph >= first + count)
				continue;
			if (!any || line.frame.top < top)
				top = line.frame.top;
			if (!any || line.frame.bottom + 1 > bottom)
				bottom = line.frame.bottom + 1;
			any = true;
		}
		return any ? (bottom - top) / fZoom : 15;
	};
	return true;
}


bool
TPageView::CaretCell(size_t& table, TableEdit& edit, int& row, int& column) const
{
	if (!fCaret.active)
		return false;
	int t = fDocument.TableOfText(fCaret.part);
	if (t < 0 || !LoadTable(size_t(t), edit))
		return false;
	table = size_t(t);
	for (const TableEditRow& r : edit.rows)
		for (const TableEditCell& c : r.cells) {
			size_t first = TableEditParagraph(edit, c.id);
			size_t count = edit.texts[size_t(c.id)].paragraphs.size();
			if (fCaret.paragraph >= first && fCaret.paragraph < first + std::max<size_t>(1, count)) {
				row = c.row;
				column = c.column;
				return true;
			}
		}
	return false;
}


bool
TPageView::InsertLocation(size_t& table, TableEdit& edit, int& row, int& column) const
{
	// TTablePart::GetInsertLocation: the first cell selected (in the text's
	// order), else the caret's
	if (!CaretCell(table, edit, row, column))
		return false;
	if (fTableCells.active && fTableCells.table == table) {
		std::vector<const TableEditCell*> selected = edit.Selected(fTableCells.selection);
		for (int id : edit.frameOrder)
			for (const TableEditCell* c : selected)
				if (c->id == id) {
					row = c->row;
					column = c->column;
					return true;
				}
	}
	return true;
}


void
TPageView::SetTableCells(size_t table, const TableEdit& edit, const TableSelection& selection)
{
	fTableCells.active = !selection.Empty() && !edit.Selected(selection).empty();
	fTableCells.table = table;
	fTableCells.selection = selection;
	// the caret at the first selected cell's text
	if (fTableCells.active) {
		std::vector<const TableEditCell*> selected = edit.Selected(selection);
		for (int id : edit.frameOrder)
			for (const TableEditCell* c : selected)
				if (c->id == id) {
					fCaret = {true, fDocument.Table(table)->textPart, TableEditParagraph(edit, id), 0};
					fAnchor = fCaret;
					return;
				}
	}
}


bool
TPageView::TableDragMoved(const Caret& at)
{
	// TTableClickHandler::TrackMouse: in the cell it began in, a text
	// selection; in another, the cells from that one to this
	if (!fTableDrag.active || at.part != fAnchor.part)
		return false;
	size_t table;
	TableEdit edit;
	int row, column;
	Caret saved = fCaret;
	fCaret = at;
	bool found = CaretCell(table, edit, row, column);
	fCaret = saved;
	if (!found)
		return false;
	if (row == fTableDrag.row && column == fTableDrag.column) {
		if (fTableCells.active) {
			fTableCells.active = false;
			fCaret = at;
			Invalidate();
		}
		return false;
	}
	TableSelection selection = edit.SelectionOf(fTableDrag.row, fTableDrag.column, row, column);
	fTableCells.active = true;
	fTableCells.table = table;
	fTableCells.selection = selection;
	Invalidate();
	return true;
}


void
TPageView::TableDragBegan()
{
	fTableCells.active = false;
	fTableDrag.active = false;
	size_t table;
	TableEdit edit;
	int row, column;
	if (CaretCell(table, edit, row, column)) {
		fTableDrag.active = true;
		fTableDrag.row = row;
		fTableDrag.column = column;
	}
}


void
TPageView::DrawTableSelection(float dx, float dy)
{
	if (!fTableCells.active || IsPrinting())
		return;
	TableEdit edit;
	const TablePart* t = fDocument.Table(fTableCells.table);
	if (t == NULL || !LoadTableEdit(fDocument, fTableCells.table, edit))
		return;
	std::vector<uint32_t> numbers;
	for (size_t k = 0; k < edit.frameOrder.size(); k++)
		for (const TableEditCell* c : edit.Selected(fTableCells.selection))
			if (c->id == edit.frameOrder[k])
				numbers.push_back(uint32_t(k));
	PushState();
	// (inverted, as the text selection)
	SetDrawingMode(B_OP_INVERT);
	for (const Document::PlacedFrame& f : fFrames) {
		if (f.part != t->textPart || f.frame == NULL || f.frame->container != fTableCells.table
				|| std::find(numbers.begin(), numbers.end(), f.frame->values[1]) == numbers.end())
			continue;
		FillRect(BRect(f.x + dx, f.y + dy, f.x + dx + f.width, f.y + dy + f.height));
	}
	PopState();
}


void
TPageView::TableMenuState(bool& merge, bool& split) const
{
	// TTablePart::FixMenus: Merge Cells with a rectangle of cells more than
	// one, Split Cell with a rectangle (or within a cell)
	merge = false;
	split = true;
	if (!fTableCells.active)
		return;
	TableEdit edit;
	if (!LoadTableEdit(fDocument, fTableCells.table, edit))
		return;
	int count = 0;
	bool rectangular = edit.SelectionIsRectangular(fTableCells.selection, count);
	merge = rectangular && count > 1;
	split = rectangular;
}


std::vector<int>
TPageView::TableCellFrames(size_t& table, TableEdit& edit) const
{
	// TTablePart::GetSelectedFrames: the selected cells', else the caret's
	std::vector<int> numbers;
	int row, column;
	if (!CaretCell(table, edit, row, column))
		return numbers;
	std::vector<const TableEditCell*> cells;
	if (fTableCells.active && fTableCells.table == table)
		cells = edit.Selected(fTableCells.selection);
	else if (const TableEditCell* c = edit.Cell(row, column))
		cells.push_back(c);
	for (size_t k = 0; k < edit.frameOrder.size(); k++)
		for (const TableEditCell* c : cells)
			if (c->id == edit.frameOrder[k])
				numbers.push_back(int(k));
	return numbers;
}


bool
TPageView::CaretCellFrameStyle(uint32_t& style) const
{
	// TTablePartStyleIterator: the frame of the cell the caret's in (the
	// first selected)
	size_t table;
	TableEdit edit;
	std::vector<int> numbers = TableCellFrames(table, edit);
	const TablePart* t = fDocument.Table(table);
	if (numbers.empty() || t == NULL)
		return false;
	for (const Portal& portal : fDocument.tails[t->textPart].portals)
		if (portal.part == table)
			for (const gobe::Frame& f : portal.frames)
				if (int(f.values[1]) == numbers.front()) {
					style = f.style;
					return true;
				}
	return false;
}


int
TPageView::TableVerticalAlignment() const
{
	// TVertAlignMenuElement::FixMenu: what the cells' frames all have (-1
	// mixed), Align Top with none
	size_t table;
	TableEdit edit;
	std::vector<int> numbers = TableCellFrames(table, edit);
	const TablePart* t = fDocument.Table(table);
	if (numbers.empty() || t == NULL)
		return 1;
	int seen = 0;
	for (const Portal& portal : fDocument.tails[t->textPart].portals) {
		if (portal.part != table)
			continue;
		for (const gobe::Frame& f : portal.frames) {
			if (std::find(numbers.begin(), numbers.end(), int(f.values[1])) == numbers.end())
				continue;
			int align = StyleVerticalAlignment(fDocument.styles, f.style);
			if (align == 0)
				align = 1;
			if (seen != 0 && seen != align)
				return -1;
			seen = align;
		}
	}
	return seen != 0 ? seen : 1;
}


// the Table part bar's vertical alignment ('sTBB' top, 'sTBC' center,
// 'sTBD' bottom; TTablePartFormatCommand, TPropVAlign): 'VAln' in the
// cells' frames' styles
void
TPageView::VerticallyAlign(int align)
{
	size_t table;
	TableEdit edit;
	std::vector<int> numbers = TableCellFrames(table, edit);
	const TablePart* t = fDocument.Table(table);
	if (numbers.empty() || t == NULL) {
		beep();
		return;
	}
	// (TFormatCommand::CommandName: "Set " and the property's name)
	std::string name = std::string(GobeResources::IndexString(5, 5).String())
		+ GobeResources::IndexString(11, 25).String();
	fUndo.BeginDocument(fDocument, name, CurrentViewState());
	bool changed = false;
	for (Portal& portal : fDocument.tails[t->textPart].portals) {
		if (portal.part != table)
			continue;
		for (gobe::Frame& f : portal.frames) {
			if (std::find(numbers.begin(), numbers.end(), int(f.values[1])) == numbers.end())
				continue;
			uint32_t style = StyleWithProperty(fDocument.styles, f.style, CellVAlignProperty(align));
			if (style != f.style) {
				f.style = style;
				changed = true;
			}
		}
	}
	if (!changed)
		return;
	fDocument.stylesChanged = true;
	fDocument.dirtyTails.insert(t->textPart);
	GeometryChanged();
	if (Window() != NULL)
		Window()->PostMessage(kMsgDocumentChanged);
	Invalidate();
}


// the cell's text, all of it but its break, selected (empty: the caret at
// its start)
void
TPageView::SelectCellText(size_t table, const TableEdit& edit, int id)
{
	size_t text = fDocument.Table(table)->textPart;
	size_t first = TableEditParagraph(edit, id);
	const std::vector<WPParagraph>& paragraphs = edit.texts[size_t(id)].paragraphs;
	size_t last = first + (paragraphs.empty() ? 0 : paragraphs.size() - 1);
	size_t end = paragraphs.empty() ? 0 : EditableLength(paragraphs.back());
	fAnchor = {true, text, first, 0};
	fCaret = {true, text, last, end};
	fTableCells.active = false;
}


// TTablePart::KeyDown: Tab to the next cell (a row more after the last),
// its text selected; Escape out of the table (TFrame::ActivateParent, the
// table's frame selected); Up and Down from a cell's first or last line to
// the cell above or below; a key typed on cells selected types over the
// first one's text (CheckForPare); Backspace and Delete empty them all.
// True if the key was the table's.
bool
TPageView::TableKey(const char* bytes, bool shift)
{
	size_t table;
	TableEdit edit;
	int row, column;
	if (!InsertLocation(table, edit, row, column))
		return false;
	const TablePart* t = fDocument.Table(table);
	char key = bytes[0];
	if (key == B_TAB && !shift) {
		const TableEditCell* next = NULL;
		if (fTableCells.active && fTableCells.table == table)
			next = edit.Cell(row, column);
		else {
			// TTable::FindNextCell
			int r = row, c = column + 1;
			while (r < int(edit.rows.size()) && c >= int(edit.rows[size_t(r)].cells.size())) {
				r++;
				c = 0;
			}
			if (r < int(edit.rows.size()))
				next = edit.Cell(r, c);
			else {
				// the last cell: Insert Row at the end (TInsertRowCommand)
				fUndo.BeginDocument(fDocument, "Insert Row", CurrentViewState());
				edit.InsertRow(int(edit.rows.size()));
				std::string error;
				if (!StoreTableEdit(fDocument, table, edit, error)) {
					beep();
					return true;
				}
				LoadTable(table, edit);
				next = edit.Cell(row + 1, 0);
				GeometryChanged();
				if (Window() != NULL)
					Window()->PostMessage(kMsgDocumentChanged);
			}
		}
		if (next != NULL)
			SelectCellText(table, edit, next->id);
		Invalidate();
		return true;
	}
	if (key == B_ESCAPE) {
		size_t container;
		TextPosition at;
		fTableCells.active = false;
		if (InlineSpecialOf(table, container, at)) {
			fAnchor = {true, container, at.paragraph, at.offset};
			fCaret = {true, container, at.paragraph, at.offset + 1};
		} else {
			fCaret.active = false;
			fAnchor.active = false;
			fFrame.active = true;
			fFrame.pointer[0] = uint32_t(table);
			fFrame.pointer[1] = fFrame.pointer[2] = 0;
		}
		Invalidate();
		return true;
	}
	if ((key == B_UP_ARROW || key == B_DOWN_ARROW) && shift && fTableCells.active)
		return true;
	if ((key == B_UP_ARROW || key == B_DOWN_ARROW) && !shift) {
		size_t index;
		float x;
		if (!CaretLine(fCaret, index, x))
			return false;
		const Line& line = fLines[index];
		// another line of the same cell: the text's own move
		bool up = key == B_UP_ARROW;
		for (size_t k = 0; k < fLines.size(); k++) {
			const Line& other = fLines[k];
			if (k == index || other.part != line.part || other.textFrame != line.textFrame)
				continue;
			if (up ? other.frame.top < line.frame.top : other.frame.top > line.frame.top)
				return false;
		}
		const TableEditCell* cell = edit.Cell(row, column);
		if (cell == NULL)
			return true;
		// TWPFrame::CharToPixel: where the caret is across its frame
		float pixel = std::max(1.0f, (x - line.textFrame.left) / fZoom);
		TableEditCell* target = edit.FindCell(cell->x + pixel,
			up ? cell->y - 1 : cell->y + cell->height + 1);
		if (target == NULL)
			return true;
		float across = std::max(0.0f, pixel - (target->x - cell->x));
		// its last line (Up) or first (Down), there
		size_t first = TableEditParagraph(edit, target->id);
		size_t last = first + edit.texts[size_t(target->id)].paragraphs.size() - 1;
		const Line* to = NULL;
		for (const Line& candidate : fLines) {
			if (candidate.part != t->textPart || candidate.paragraph < first
					|| candidate.paragraph > last)
				continue;
			if (to == NULL || (up ? candidate.frame.top > to->frame.top
					: candidate.frame.top < to->frame.top))
				to = &candidate;
		}
		fTableCells.active = false;
		Caret caret;
		if (to != NULL && HitText(BPoint(to->textFrame.left + across * fZoom,
				(to->frame.top + to->frame.bottom) / 2), caret) && caret.part == t->textPart) {
			fCaret = caret;
			fAnchor = caret;
		} else
			SelectCellText(table, edit, target->id);
		fAnchor = fCaret;
		Invalidate();
		return true;
	}
	if (!fTableCells.active || fTableCells.table != table)
		return false;
	if (key == B_BACKSPACE || key == B_DELETE) {
		// the selected cells' text gone, their breaks kept
		fUndo.BeginDocument(fDocument, key == B_BACKSPACE ? "Backspace" : "Delete",
			CurrentViewState());
		for (const TableEditCell* c : edit.Selected(fTableCells.selection)) {
			std::vector<WPParagraph>& text = edit.texts[size_t(c->id)].paragraphs;
			if (text.empty())
				continue;
			WPParagraph kept = text.front();
			kept.text = "\x05";
			kept.misc.clear();
			if (kept.runs.size() > 1)
				kept.runs.resize(1);
			text = {kept};
		}
		std::string error;
		if (!StoreTableEdit(fDocument, table, edit, error)) {
			beep();
			return true;
		}
		const TableEditCell* first = edit.Cell(row, column);
		fTableCells.active = false;
		if (first != NULL)
			fCaret = fAnchor = {true, t->textPart, TableEditParagraph(edit, first->id), 0};
		GeometryChanged();
		if (Window() != NULL)
			Window()->PostMessage(kMsgDocumentChanged);
		return true;
	}
	if ((unsigned char)key >= 0x20 && key != B_DELETE) {
		// (CheckForPare: the first cell's text, typed over)
		if (const TableEditCell* first = edit.Cell(row, column))
			SelectCellText(table, edit, first->id);
		return false;
	}
	return false;
}


bool
TPageView::TableCommand(BMessage* message)
{
	const char* name = NULL;
	switch (message->what) {
		case 'sTB0': name = "Merge Cells"; break;
		case 'sTB1': name = "Split Cell"; break;
		case 'sTB2': name = "Insert Row"; break;
		case 'sTB3': name = "Insert Column"; break;
		case 'sTB4': name = "Delete Row"; break;
		case 'sTB5': name = "Delete Column"; break;
		case 'sTB6': name = "Distribute Rows"; break;
		case 'sTB7': name = "Distribute Columns"; break;
		case 'sTB8': case 'sTB9': break;
		case 'sTBA': name = "Set Cell Gutter"; break;
		case 'sTBB': case 'sTBC': case 'sTBD':
			VerticallyAlign(message->what == 'sTBB' ? 1 : message->what == 'sTBC' ? 2 : 3);
			return true;
		default:
			return false;
	}
	size_t table;
	TableEdit edit;
	int row = 0, column = 0;
	if (!InsertLocation(table, edit, row, column)) {
		beep();
		return true;
	}
	bool cells = fTableCells.active && fTableCells.table == table;
	TableSelection selection = cells ? fTableCells.selection : TableSelection();
	const TableEditCell* at = edit.Cell(row, column);
	float origin[2] = {at != NULL ? at->x : 0, at != NULL ? at->y : 0};

	// what only selects, or only asks
	switch (message->what) {
		case 'sTB8': {
			// Select Row: the rows of what's selected, else the caret's
			int from = row, to = row;
			TextPosition a, b;
			if (cells || TextSelection(a, b)) {
				TableSelection current = cells ? selection
					: edit.SelectionOf(row, column, row, column);
				float rect[4];
				edit.SpanningRect(current, rect);
				int columnFrom, columnTo;
				edit.CalcSpan(rect, from, to, columnFrom, columnTo, true);
			}
			SetTableCells(table, edit, edit.SelectRows(from, to));
			Invalidate();
			return true;
		}
		case 'sTB9': {
			// Select Column: the columns of what's selected, else the
			// caret's cell's
			float rect[4] = {origin[0], 0, origin[0] + (at != NULL ? at->width : 0), 0};
			if (cells)
				edit.SpanningRect(selection, rect);
			SetTableCells(table, edit, edit.SelectColumns(rect[0], rect[2]));
			Invalidate();
			return true;
		}
		case 'sTB1':
			if (message->GetInt32("rows", 0) == 0 || message->GetInt32("columns", 0) == 0) {
				// Split Cell…: 2 by 1, or twice the selection's columns
				int rows = 1, columns = 2;
				if (cells) {
					edit.SelectionSpan(selection, rows, columns);
					columns *= 2;
				}
				if (Window() != NULL)
					(new TDialogSplitCell(Window(), columns, rows))->RunDialog();
				return true;
			}
			break;
		case 'sTBA':
			if (!message->HasInt32("gutter")) {
				if (Window() != NULL)
					(new TDialogCellGutter(Window(), edit.gutter))->RunDialog();
				return true;
			}
			break;
		case 'sTB0':
			if (!cells) {
				beep();
				return true;
			}
			break;
		case 'sTB4':
			// (TDeleteRowCommand: not the last row)
			if (edit.rows.size() < 2)
				return true;
			break;
		case 'sTB5': {
			bool several = false;
			for (const TableEditRow& r : edit.rows)
				several = several || r.cells.size() > 1;
			if (!several)
				return true;
			break;
		}
	}
	if (message->what == 'sTB1') {
		// TSplitCellsCommand: the cells of the selection merged first; no
		// cells smaller than 20 across or the gutter twice and one down
		int rows = message->GetInt32("rows", 1), columns = message->GetInt32("columns", 1);
		float rect[4] = {origin[0], origin[1], origin[0] + (at != NULL ? at->width : 0),
			origin[1] + (at != NULL ? at->height : 0)};
		if (cells)
			edit.SpanningRect(selection, rect);
		if ((rect[2] - rect[0]) / float(columns) < 20
				|| (rect[3] - rect[1]) / float(rows) < edit.gutter + edit.gutter + 1) {
			(new BAlert("", IndexString(GobeResources::Part("Table"), 200, 8).c_str(),
				"OK"))->Go(NULL);
			return true;
		}
	}

	fUndo.BeginDocument(fDocument, name, CurrentViewState());
	TableSelection result;
	bool select = false, fixCaret = false;
	switch (message->what) {
		case 'sTB2':
			edit.InsertRow(row);
			result = edit.SelectRows(row, row);
			select = true;
			break;
		case 'sTB3': {
			float left, right;
			edit.VirtualColumn(row, column, left, right);
			edit.InsertColumn(left, right);
			result = edit.SelectColumns(left, right);
			select = true;
			break;
		}
		case 'sTB4':
			edit.DeleteRow(row);
			fixCaret = true;
			break;
		case 'sTB5': {
			float left, right;
			edit.VirtualColumn(row, column, left, right);
			edit.DeleteColumn(left, right);
			fixCaret = true;
			break;
		}
		case 'sTB0': {
			edit.MergeCells(selection);
			// TMergeCellsCommand: the merged cell selected
			if (const TableEditCell* c = edit.FindCell(origin[0] + 1, origin[1] + 1))
				result = edit.SelectionOf(c->row, c->column, c->row, c->column);
			select = true;
			break;
		}
		case 'sTB1': {
			if (cells) {
				edit.MergeCells(selection);
				if (const TableEditCell* c = edit.FindCell(origin[0] + 1, origin[1] + 1)) {
					row = c->row;
					column = c->column;
				}
			}
			result = edit.SplitCell(row, column, message->GetInt32("columns", 1),
				message->GetInt32("rows", 1));
			select = true;
			break;
		}
		case 'sTB6': {
			// all the rows, or the selection's when it holds more than one
			int from = 0, to = int(edit.rows.size()) - 1;
			if (cells) {
				float rect[4];
				edit.SpanningRect(selection, rect);
				int rowFrom, rowTo, columnFrom, columnTo;
				edit.CalcSpan(rect, rowFrom, rowTo, columnFrom, columnTo, true);
				if (rowTo > rowFrom) {
					from = rowFrom;
					to = rowTo;
				}
			}
			edit.DistributeRows(from, to);
			break;
		}
		case 'sTB7': {
			float left = 0, right = edit.size[0];
			if (cells) {
				float rect[4];
				edit.SpanningRect(selection, rect);
				int rowFrom, rowTo, columnFrom, columnTo;
				edit.CalcSpan(rect, rowFrom, rowTo, columnFrom, columnTo, true);
				if (columnTo > columnFrom) {
					left = rect[0];
					right = rect[2];
				}
			}
			edit.DistributeColumns(left, right);
			break;
		}
		case 'sTBA':
			edit.SetCellGutter(float(message->GetInt32("gutter", 1)));
			break;
	}
	std::string error;
	if (!StoreTableEdit(fDocument, table, edit, error)) {
		fprintf(stderr, "table: %s\n", error.c_str());
		beep();
		return true;
	}
	size_t text = fDocument.Table(table)->textPart;
	if (select)
		SetTableCells(table, edit, result);
	else if (fixCaret) {
		// TTablePartCommand::FixInsertionPos: the start of the cell where
		// the caret's was, else the text's end
		fTableCells.active = false;
		TableEditCell* c = edit.FindCell(origin[0] + 1, origin[1] + 1);
		const WPPart* wp = fDocument.WordPart(text);
		if (c != NULL)
			fCaret = {true, text, TableEditParagraph(edit, c->id), 0};
		else if (wp != NULL && !wp->paragraphs.empty())
			fCaret = {true, text, wp->paragraphs.size() - 1, 0};
		fAnchor = fCaret;
	} else if (fCaret.part == text && fDocument.WordPart(text) != NULL
			&& fCaret.paragraph >= fDocument.WordPart(text)->paragraphs.size()) {
		fCaret = {true, text, 0, 0};
		fAnchor = fCaret;
	}
	GeometryChanged();
	if (Window() != NULL)
		Window()->PostMessage(kMsgDocumentChanged);
	Invalidate();
	return true;
}

} // namespace gobe
