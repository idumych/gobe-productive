// Moving cells: TSSMoveCommand (decomp/Spreadsheet/TSSMoveCommand.c) with
// TFormula::MoveReferences and InvalidateReferences (TFormula.c),
// TRangeRef::Adjust (TRangeRef.c), TCellRef (TCellRef.c) and the TCellsList
// operations it uses (TCellsList.c). Cell ids are (row a, column b), as
// TCellID's field_0x0 and field_0x2; arithmetic wraps at 16 bits as there.
#include "SSMove.h"

#include <algorithm>
#include <set>

#include "Document.h"
#include "Editing.h"

namespace gobe {

namespace {

// TCellRef flags
const uint16_t kInvalid = 1, kRowAbsolute = 2, kColumnAbsolute = 4;

// TRange::CellInRange
bool
InRange(const SSRange& r, CellID c)
{
	return r.from.a <= c.a && c.a <= r.to.a && r.from.b <= c.b && c.b <= r.to.b;
}


// TRange::Intersects
bool
Intersects(const SSRange& x, const SSRange& y)
{
	return !(y.to.a < x.from.a || x.to.a < y.from.a || y.to.b < x.from.b || x.to.b < y.from.b);
}


// TRange::Contains
bool
Contains(const SSRange& outer, const SSRange& inner)
{
	return InRange(outer, inner.from) && InRange(outer, inner.to);
}


// TRange::Pin
void
Pin(SSRange& r, const SSRange& to)
{
	r.from.a = std::min(std::max(r.from.a, to.from.a), to.to.a);
	r.from.b = std::min(std::max(r.from.b, to.from.b), to.to.b);
	r.to.a = std::min(std::max(r.to.a, to.from.a), to.to.a);
	r.to.b = std::min(std::max(r.to.b, to.from.b), to.to.b);
}


// TRange::Normalize
SSRange
Normalized(CellID a, CellID b)
{
	return {{std::min(a.a, b.a), std::min(a.b, b.b)}, {std::max(a.a, b.a), std::max(a.b, b.b)}};
}


// TCellsList::TCellsList(list, rows, columns): the list offset
std::vector<SSRange>
Offset(const std::vector<SSRange>& list, int rows, int columns)
{
	std::vector<SSRange> out = list;
	for (SSRange& r : out) {
		r.from.a = uint16_t(r.from.a + rows);
		r.from.b = uint16_t(r.from.b + columns);
		r.to.a = uint16_t(r.to.a + rows);
		r.to.b = uint16_t(r.to.b + columns);
	}
	return out;
}


// TCellRef::OffsetAbsolute / OffsetRelative: the absolute (relative) parts
void
OffsetAbsolute(CellRef& r, int rows, int columns)
{
	if (r.flags & kRowAbsolute)
		r.cell.a = uint16_t(r.cell.a + rows);
	if (r.flags & kColumnAbsolute)
		r.cell.b = uint16_t(r.cell.b + columns);
}


void
OffsetRelative(CellRef& r, int rows, int columns)
{
	if (!(r.flags & kRowAbsolute))
		r.cell.a = uint16_t(r.cell.a + rows);
	if (!(r.flags & kColumnAbsolute))
		r.cell.b = uint16_t(r.cell.b + columns);
}


// the cells a formula refers to (TFormulaCell's list at +0x18): its valid
// cell and range references
std::vector<SSRange>
FormulaCells(const std::vector<FormulaToken>& formula, CellID at)
{
	std::vector<SSRange> out;
	for (const FormulaToken& t : formula) {
		if (t.kind == 0x19 && t.refs.size() == 1 && !(t.refs[0].flags & kInvalid)) {
			CellID c = ActualCell(t.refs[0], at);
			out.push_back({c, c});
		} else if (t.kind == 0x1a && t.refs.size() == 2 && !(t.refs[0].flags & kInvalid)
				&& !(t.refs[1].flags & kInvalid))
			out.push_back(Normalized(ActualCell(t.refs[0], at), ActualCell(t.refs[1], at)));
	}
	return out;
}

} // namespace


CellID
ActualCell(const CellRef& ref, CellID at)
{
	CellID c;
	c.a = (ref.flags & kRowAbsolute) ? ref.cell.a : uint16_t(at.a + ref.cell.a);
	c.b = (ref.flags & kColumnAbsolute) ? ref.cell.b : uint16_t(at.b + ref.cell.b);
	return c;
}


bool
CellInList(const std::vector<SSRange>& list, CellID cell)
{
	for (const SSRange& r : list)
		if (InRange(r, cell))
			return true;
	return false;
}


bool
RangeInList(const std::vector<SSRange>& list, const SSRange& range)
{
	for (const SSRange& r : list)
		if (Intersects(r, range))
			return true;
	return false;
}


bool
ListInList(const std::vector<SSRange>& list, const std::vector<SSRange>& other)
{
	for (const SSRange& r : list)
		if (RangeInList(other, r))
			return true;
	return false;
}


bool
ListContainsRange(const std::vector<SSRange>& list, const SSRange& range)
{
	// the pieces of the range not yet found covered
	std::vector<SSRange> stack{range};
	while (!stack.empty()) {
		SSRange r = stack.back();
		stack.pop_back();
		bool found = false;
		for (const SSRange& l : list) {
			if (found)
				break;
			if (!Intersects(l, r))
				continue;
			found = true;
			if (r.from.a < l.from.a) {
				stack.push_back({r.from, {uint16_t(l.from.a - 1), r.to.b}});
				r.from.a = l.from.a;
			}
			if (r.from.b < l.from.b) {
				stack.push_back({r.from, {r.to.a, uint16_t(l.from.b - 1)}});
				r.from.b = l.from.b;
			}
			if (l.to.a < r.to.a) {
				stack.push_back({{uint16_t(l.to.a + 1), r.from.b}, r.to});
				r.to.a = l.to.a;
			}
			if (l.to.b < r.to.b)
				stack.push_back({{r.from.a, uint16_t(l.to.b + 1)}, r.to});
		}
		if (!found)
			return false;
	}
	return true;
}


void
RemoveRange(std::vector<SSRange>& list, const SSRange& range)
{
	// the ranges it touches out, those it doesn't contain stacked to come
	// back in pieces (last first)
	std::vector<SSRange> stack;
	for (size_t i = 0; i < list.size(); i++) {
		const SSRange& r = list[i];
		if (!Intersects(range, r))
			continue;
		if (!Contains(range, r))
			stack.push_back(r);
		list.erase(list.begin() + i);
		i--;
	}
	while (!stack.empty()) {
		SSRange r = stack.back();
		stack.pop_back();
		if (r.from.a < range.from.a) {		// above
			list.push_back({r.from, {uint16_t(range.from.a - 1), r.to.b}});
			r.from.a = range.from.a;
		}
		if (r.from.b < range.from.b) {		// left
			list.push_back({r.from, {r.to.a, uint16_t(range.from.b - 1)}});
			r.from.b = range.from.b;
		}
		if (range.to.a < r.to.a) {			// below
			list.push_back({{uint16_t(range.to.a + 1), r.from.b}, r.to});
			r.to.a = range.to.a;
		}
		if (range.to.b < r.to.b)			// right
			list.push_back({{r.from.a, uint16_t(range.to.b + 1)}, r.to});
	}
}


void
RemoveList(std::vector<SSRange>& list, const std::vector<SSRange>& removed)
{
	for (const SSRange& r : removed)
		RemoveRange(list, r);
}


bool
AdjustRangeRef(CellRef& from, CellRef& to, const std::vector<SSRange>& block, int rows,
	int columns, CellID at)
{
	SSRange actual = Normalized(ActualCell(from, at), ActualCell(to, at));
	bool formulaMoved = CellInList(block, at);
	bool contained = false, edge = false, intersects = false;
	SSRange hit{};		// the last block range it meets
	for (const SSRange& r : block) {
		if (!Intersects(r, actual))
			continue;
		intersects = true;
		hit = r;
		if (Contains(hit, actual)) {
			contained = true;
			break;
		}
		if (rows != 0 && columns != 0)
			break;
		CellID bottomLeft{actual.to.a, actual.from.b}, topRight{actual.from.a, actual.to.b};
		if (columns == 0) {
			// moving up or down: its first or last row in the block
			if ((InRange(hit, actual.from) && InRange(hit, topRight))
				|| (InRange(hit, actual.to) && InRange(hit, bottomLeft)))
				edge = true;
		} else {
			// left or right: its first or last column
			if ((InRange(hit, actual.from) && InRange(hit, bottomLeft))
				|| (InRange(hit, actual.to) && InRange(hit, topRight)))
				edge = true;
		}
		if (edge)
			break;
	}
	bool fromIn = InRange(hit, actual.from), toIn = InRange(hit, actual.to);
	if (!formulaMoved) {
		bool changed = false;
		if (contained || (edge && fromIn)) {
			OffsetAbsolute(from, rows, columns);
			OffsetRelative(from, rows, columns);
			changed = true;
		}
		if (contained || (edge && toIn)) {
			OffsetAbsolute(to, rows, columns);
			OffsetRelative(to, rows, columns);
			changed = true;
		}
		return changed;
	}
	if (contained) {
		OffsetAbsolute(from, rows, columns);
		OffsetAbsolute(to, rows, columns);
		return true;
	}
	if (intersects) {
		if (!edge)
			return false;
		if (fromIn)
			OffsetAbsolute(from, rows, columns);
		else
			OffsetRelative(from, -rows, -columns);
		if (toIn)
			OffsetAbsolute(to, rows, columns);
		else
			OffsetRelative(to, -rows, -columns);
		return true;
	}
	OffsetRelative(from, -rows, -columns);
	OffsetRelative(to, -rows, -columns);
	return true;
}


bool
MoveFormulaReferences(std::vector<FormulaToken>& formula, const std::vector<SSRange>& block,
	int rows, int columns, CellID at)
{
	bool changed = false;
	bool cellMoved = CellInList(block, at);
	for (FormulaToken& t : formula) {
		if (t.kind == 0x19 && t.refs.size() == 1 && !(t.refs[0].flags & kInvalid)) {
			CellRef& r = t.refs[0];
			bool targetMoved = CellInList(block, ActualCell(r, at));
			if (targetMoved && !cellMoved) {
				OffsetRelative(r, rows, columns);
				OffsetAbsolute(r, rows, columns);
			} else if (targetMoved)
				OffsetAbsolute(r, rows, columns);
			else if (cellMoved)
				OffsetRelative(r, -rows, -columns);
			else
				continue;
			changed = true;
		} else if (t.kind == 0x1a && t.refs.size() == 2 && !(t.refs[0].flags & kInvalid)
				&& !(t.refs[1].flags & kInvalid)) {
			if (AdjustRangeRef(t.refs[0], t.refs[1], block, rows, columns, at))
				changed = true;
		}
	}
	return changed;
}


bool
InvalidateFormulaReferences(std::vector<FormulaToken>& formula,
	const std::vector<SSRange>& cells, CellID at)
{
	bool changed = false;
	for (FormulaToken& t : formula) {
		if (t.kind == 0x19 && t.refs.size() == 1 && !(t.refs[0].flags & kInvalid)) {
			if (CellInList(cells, ActualCell(t.refs[0], at))) {
				t.refs[0].flags |= kInvalid;
				changed = true;
			}
		} else if (t.kind == 0x1a && t.refs.size() == 2 && !(t.refs[0].flags & kInvalid)
				&& !(t.refs[1].flags & kInvalid)) {
			SSRange r = Normalized(ActualCell(t.refs[0], at), ActualCell(t.refs[1], at));
			if (CellInList(cells, r.from) || CellInList(cells, r.to)) {
				t.refs[0].flags |= kInvalid;
				t.refs[1].flags |= kInvalid;
				changed = true;
			}
		}
	}
	return changed;
}


bool
MoveCells(SSPart& part, const std::vector<SSRange>& block, int rows, int columns,
	std::vector<SSRange>* changed)
{
	if (block.empty())
		return true;
	// TCellsList::Span; the original checks this after the formulas have
	// changed, here first so that a refused move changes nothing
	SSRange span = block[0];
	for (const SSRange& r : block) {
		span.from.a = std::min(span.from.a, r.from.a);
		span.from.b = std::min(span.from.b, r.from.b);
		span.to.a = std::max(span.to.a, r.to.a);
		span.to.b = std::max(span.to.b, r.to.b);
	}
	if ((rows < 0 && span.from.a < -rows) || (columns < 0 && span.from.b < -columns))
		return false;

	// where the block lands outside itself
	std::vector<SSRange> landing = Offset(block, rows, columns);
	RemoveList(landing, block);

	// formulas referring to the block or to where it lands, or in them
	for (SSCell& cell : part.cells) {
		if (cell.kind != Sig("Fmla"))
			continue;
		std::vector<SSRange> refers = FormulaCells(cell.formula, cell.id);
		bool moves = ListInList(refers, block) || CellInList(block, cell.id);
		bool lands = ListInList(refers, landing) || CellInList(landing, cell.id);
		if (!moves && !lands)
			continue;
		bool edited = false;
		if (lands)
			edited = InvalidateFormulaReferences(cell.formula, landing, cell.id);
		if (moves)
			edited = MoveFormulaReferences(cell.formula, block, rows, columns, cell.id);
		if (edited && changed != nullptr)
			changed->push_back({cell.id, cell.id});
	}

	// the block's cells taken out (pinned to the sheet), those where it
	// lands cleared, then the block's put in their new places
	auto takeOut = [&](SSRange r, std::vector<SSCell>* into) {
		if (r.from.a > part.used.to.a || r.from.b > part.used.to.b)
			return;
		Pin(r, part.used);
		for (size_t i = 0; i < part.cells.size(); i++) {
			if (!InRange(r, part.cells[i].id))
				continue;
			if (into != nullptr)
				into->push_back(std::move(part.cells[i]));
			part.cells.erase(part.cells.begin() + i);
			i--;
		}
	};
	std::vector<SSCell> moved;
	for (const SSRange& r : block)
		takeOut(r, &moved);
	std::vector<SSCell> cleared;
	for (const SSRange& r : landing)
		takeOut(r, &cleared);
	std::set<uint32_t> movedFrom, clearedAt;
	auto key = [](CellID id) { return uint32_t(id.a) << 16 | id.b; };
	for (SSCell& cell : moved) {
		movedFrom.insert(key(cell.id));
		cell.id.a = uint16_t(cell.id.a + rows);
		cell.id.b = uint16_t(cell.id.b + columns);
		auto at = std::find_if(part.cells.begin(), part.cells.end(),
			[&](const SSCell& c) { return c.id.a == cell.id.a && c.id.b == cell.id.b; });
		if (at != part.cells.end())
			*at = std::move(cell);
		else
			part.cells.push_back(std::move(cell));
	}
	for (const SSCell& cell : cleared)
		clearedAt.insert(key(cell.id));
	// (the cells in column order, then row, as the data manager keeps them)
	std::stable_sort(part.cells.begin(), part.cells.end(), [](const SSCell& x, const SSCell& y) {
		return std::make_pair(x.id.b, x.id.a) < std::make_pair(y.id.b, y.id.a);
	});
	// the calculation order keeps the moved formula cells, at their new
	// places, and loses the cleared ones
	std::vector<CellID> dependents;
	for (CellID id : part.dependents) {
		if (movedFrom.count(key(id))) {
			id.a = uint16_t(id.a + rows);
			id.b = uint16_t(id.b + columns);
		} else if (clearedAt.count(key(id)))
			continue;
		dependents.push_back(id);
	}
	part.dependents = std::move(dependents);

	if (changed != nullptr) {
		changed->insert(changed->end(), block.begin(), block.end());
		changed->insert(changed->end(), landing.begin(), landing.end());
	}
	return true;
}


namespace {

// TSSMoveCommand::MoveReferences: the part's named ranges after the block
// moved. Each range of a reference's selection, its end pinned to the
// sheet's last row and column, goes if the cells the block lands on cover
// it, else follows the block as an absolute range reference would
// (TRangeRef::Adjust, seen from no cell); the active cell is cleared if the
// block lands on it and moves if it was in the block. A changed reference
// left with no ranges is dead (TReference::SetDead).
void
MoveNamedReferences(Document& doc, size_t partIndex, const SSPart& part,
	const std::vector<SSRange>& block, int rows, int columns)
{
	if (partIndex >= doc.tails.size() || !doc.tailRead[partIndex])
		return;
	std::vector<SSRange> landing = Offset(block, rows, columns);
	RemoveList(landing, block);
	const CellID nowhere{0xffff, 0xffff};
	for (Reference& ref : doc.tails[partIndex].refs) {
		Selection* sel = nullptr;
		for (Selection& s : doc.selections.selections)
			if (s.id == ref.selection && s.kind == Sig("SPRD"))
				sel = &s;
		if (sel == nullptr)
			continue;
		bool changed = false;
		std::vector<std::array<uint16_t, 4>> ranges;
		for (const auto& stored : sel->ranges) {
			SSRange r{{stored[0], stored[1]}, {stored[2], stored[3]}};
			if (r.from.a <= part.used.to.a && part.used.to.a < r.to.a)
				r.to.a = part.used.to.a;
			if (r.from.b < part.used.to.b && part.used.to.b < r.to.b)
				r.to.b = part.used.to.b;
			if (ListContainsRange(landing, r)) {
				changed = true;
				continue;
			}
			CellRef from{r.from, kRowAbsolute | kColumnAbsolute};
			CellRef to{r.to, kRowAbsolute | kColumnAbsolute};
			if (AdjustRangeRef(from, to, block, rows, columns, nowhere)) {
				changed = true;
				r = Normalized(ActualCell(from, nowhere), ActualCell(to, nowhere));
			}
			ranges.push_back({r.from.a, r.from.b, r.to.a, r.to.b});
		}
		if (!changed)
			continue;
		// a new TSSSelection: no active cell unless the old one moved
		bool hasCell = false;
		uint16_t cell[2] = {0, 0};
		if (sel->hasCell) {
			CellID active{sel->cell[0], sel->cell[1]};
			if (!CellInList(landing, active) && CellInList(block, active)) {
				hasCell = true;
				cell[0] = uint16_t(active.a + rows);
				cell[1] = uint16_t(active.b + columns);
				// (TSSSelection::SetActiveCell adds it if it isn't in a range)
				SSRange one{{cell[0], cell[1]}, {cell[0], cell[1]}};
				std::vector<SSRange> list;
				for (const auto& x : ranges)
					list.push_back({{x[0], x[1]}, {x[2], x[3]}});
				if (!CellInList(list, one.from))
					ranges.push_back({cell[0], cell[1], cell[0], cell[1]});
			}
		}
		sel->ranges = std::move(ranges);
		sel->hasCell = hasCell;
		sel->cell[0] = cell[0];
		sel->cell[1] = cell[1];
		doc.selectionsChanged = true;
		if (!ref.flag1 && sel->ranges.empty()) {
			ref.flag1 = true;
			doc.dirtyTails.insert(partIndex);
		}
	}
}

} // namespace


MoveResult
MoveCellsCommand(Document& doc, size_t partIndex, const std::vector<SSRange>& block, int rows,
	int columns)
{
	SSPart* part = doc.EditableSpreadsheetPart(partIndex);
	if (part == nullptr)
		return MoveResult::failed;
	// TPartSS::IsLocked: any locked cell where the block lands
	if (ListInList(Offset(block, rows, columns), part->locked))
		return MoveResult::locked;
	if (!MoveCells(*part, block, rows, columns))
		return MoveResult::failed;
	// (TSSPainter::MoveFills isn't done: range fills aren't written back yet)
	MoveNamedReferences(doc, partIndex, *part, block, rows, columns);
	UpdateDependencies(doc, partIndex);
	return MoveResult::moved;
}


std::vector<CellID>
DependentCells(const SSPart& part, const std::vector<SSRange>& list)
{
	// the cells of the list (pinned to the sheet) that exist are dirty
	std::vector<SSRange> pinned = list;
	for (SSRange& r : pinned)
		Pin(r, part.used);
	std::set<uint32_t> dirty;
	auto key = [](CellID id) { return uint32_t(id.a) << 16 | id.b; };
	for (const SSCell& c : part.cells)
		if (CellInList(pinned, c.id))
			dirty.insert(key(c.id));
	// a formula referring to the list or to a dirty cell is dirty; in
	// calculation order, one pass reaches everything that follows
	auto containsDirty = [&](const std::vector<SSRange>& refers) {
		for (SSRange r : refers) {
			Pin(r, part.used);
			for (const SSCell& c : part.cells)
				if (InRange(r, c.id) && dirty.count(key(c.id)))
					return true;
		}
		return false;
	};
	for (CellID id : part.dependents) {
		const SSCell* c = part.Cell(id.a, id.b);
		if (c == nullptr || c->kind != Sig("Fmla") || dirty.count(key(id)))
			continue;
		std::vector<SSRange> refers = FormulaCells(c->formula, id);
		if (ListInList(list, refers) || containsDirty(refers))
			dirty.insert(key(id));
	}
	std::vector<CellID> out;
	for (CellID id : part.dependents)
		if (dirty.count(key(id)))
			out.push_back(id);
	return out;
}


bool
AdjustRange(SSRange& r, const SSRange& block, int rows, int columns)
{
	bool alongRows = columns != 0 || r.from.a < block.from.a || block.to.a < r.to.a;
	bool alongColumns = rows != 0 || r.from.b < block.from.b || block.to.b < r.to.b;
	if (Contains(block, r) || !alongRows || !alongColumns) {
		r = Offset({r}, rows, columns)[0];
		return true;
	}
	if ((columns == 0 && r.from.a <= block.from.a && block.from.a <= r.to.a)
			|| (rows == 0 && r.from.b <= block.from.b && block.from.b <= r.to.b)) {
		r.to.a = uint16_t(r.to.a + rows);
		r.to.b = uint16_t(r.to.b + columns);
		return true;
	}
	return false;
}


namespace {

// TSSMoveCommand::OffsetLocks: when the block meets a locked range, every
// locked range is adjusted (TRange::Adjust)
void
OffsetLocks(SSPart& part, const SSRange& block, int rows, int columns)
{
	if ((rows == 0 && columns == 0) || !RangeInList(part.locked, block))
		return;
	for (SSRange& r : part.locked)
		AdjustRange(r, block, rows, columns);
}


// TSSCommand::RangeEmpty: no cell of the range (pinned to the sheet) holds
// anything
bool
RangeEmpty(const SSPart& part, SSRange r)
{
	Pin(r, part.used);
	for (const SSCell& cell : part.cells)
		if (InRange(r, cell.id) && cell.kind != Sig("NONE"))
			return false;
	return true;
}


// the shift both commands end with: locks, cells, formulas, named ranges
bool
ShiftCells(Document& doc, size_t partIndex, SSPart& part, const SSRange& block, int rows,
	int columns)
{
	OffsetLocks(part, block, rows, columns);
	if (!MoveCells(part, {block}, rows, columns))
		return false;
	MoveNamedReferences(doc, partIndex, part, {block}, rows, columns);
	UpdateDependencies(doc, partIndex);
	return true;
}


// TSSMoveCommand::OffsetRowHeaders / OffsetColumnHeaders: the sizes of
// rows (columns) from..to move by `by`, those where they land lost;
// TSSDataCommand::KillRowHeaders / KillColumnHeaders: from..to lost (by 0)
void
OffsetHeaders(std::vector<SSHeader>& headers, int from, int to, int by)
{
	std::vector<SSHeader> kept;
	for (SSHeader& h : headers) {
		int i = int(h.index);
		bool moves = i >= from && i <= to;
		if (by == 0 ? moves : (!moves && i >= from + by && i <= to + by))
			continue;
		if (moves)
			h.index = uint32_t(i + by);
		kept.push_back(h);
	}
	std::sort(kept.begin(), kept.end(),
		[](const SSHeader& x, const SSHeader& y) { return x.index < y.index; });
	headers = std::move(kept);
}

} // namespace


bool
InsertCells(Document& doc, size_t partIndex, const SSRange& r, bool vertical,
	SelectionKind kind)
{
	SSPart* part = doc.EditableSpreadsheetPart(partIndex);
	if (part == nullptr)
		return false;
	if (kind != SelectionKind::cells)
		vertical = kind == SelectionKind::rows;
	const SSRange size = part->used;
	int rows = vertical ? r.to.a - r.from.a + 1 : 0;
	int columns = vertical ? 0 : r.to.b - r.from.b + 1;
	if (kind == SelectionKind::cells) {
		// the cells pushed past the sheet's edge must be empty
		SSRange edge = vertical
			? SSRange{{uint16_t(size.to.a - rows + 1), r.from.b}, {size.to.a, r.to.b}}
			: SSRange{{r.from.a, uint16_t(size.to.b - columns + 1)}, {r.to.a, size.to.b}};
		if (!RangeEmpty(*part, edge))
			return false;
	} else {
		// rows and columns make the sheet bigger
		if (size.to.a + rows > 0x3ffe || size.to.b + columns > 0x2bd)
			return false;
		part->used = {{0, 0}, {uint16_t(size.to.a + rows), uint16_t(size.to.b + columns)}};
	}
	// the cells from the range to the edge, less what goes past it
	SSRange block{r.from, r.from};
	if (vertical) {
		block.to.b = std::min(r.to.b, size.to.b);
		if (rows <= size.to.a)
			block.to.a = uint16_t(size.to.a - rows);
		block.to.a = std::max(block.to.a, r.from.a);
		if (kind == SelectionKind::rows) {
			block.to.a = size.to.a;
			OffsetHeaders(part->rows, r.from.a, size.to.a, rows);
		}
	} else {
		block.to.a = std::min(r.to.a, size.to.a);
		if (columns <= size.to.b)
			block.to.b = uint16_t(size.to.b - columns);
		block.to.b = std::max(block.to.b, r.from.b);
		if (kind == SelectionKind::columns) {
			block.to.b = size.to.b;
			OffsetHeaders(part->columns, r.from.b, size.to.b, columns);
		}
	}
	return ShiftCells(doc, partIndex, *part, block, rows, columns);
}


bool
DeleteCells(Document& doc, size_t partIndex, const SSRange& r, bool vertical,
	SelectionKind kind)
{
	SSPart* part = doc.EditableSpreadsheetPart(partIndex);
	if (part == nullptr)
		return false;
	if (kind != SelectionKind::cells)
		vertical = kind == SelectionKind::rows;
	const SSRange size = part->used;
	int height = r.to.a - r.from.a + 1, width = r.to.b - r.from.b + 1;
	// the cells past the range to the edge, at least as many as it has
	SSRange block;
	if (vertical) {
		block.from = {uint16_t(r.to.a + 1), r.from.b};
		block.to = {size.to.a, std::min(r.to.b, size.to.b)};
		if (size.to.a < block.from.a + height)
			block.to.a = uint16_t(block.from.a + height);
		block.to.a = std::max(block.to.a, block.from.a);
		if (kind == SelectionKind::rows) {
			OffsetHeaders(part->rows, r.from.a, r.to.a, 0);
			OffsetHeaders(part->rows, block.from.a, block.to.a, -height);
		}
	} else {
		block.from = {r.from.a, uint16_t(r.to.b + 1)};
		block.to = {std::min(r.to.a, size.to.a), size.to.b};
		if (size.to.b < block.from.b + width)
			block.to.b = uint16_t(block.from.b + width);
		block.to.b = std::max(block.to.b, block.from.b);
		if (kind == SelectionKind::columns) {
			OffsetHeaders(part->columns, r.from.b, r.to.b, 0);
			OffsetHeaders(part->columns, block.from.b, block.to.b, -width);
		}
	}
	int rows = vertical ? -height : 0, columns = vertical ? 0 : -width;
	if (!ShiftCells(doc, partIndex, *part, block, rows, columns))
		return false;
	// rows and columns make the sheet smaller
	if (kind != SelectionKind::cells)
		part->used = {{0, 0}, {uint16_t(size.to.a + rows), uint16_t(size.to.b + columns)}};
	return true;
}

} // namespace gobe
