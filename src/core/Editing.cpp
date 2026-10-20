#include "Editing.h"

#include <algorithm>
#include <map>
#include <set>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include "Document.h"
#include "SSEval.h"

namespace gobe {

// #pragma mark - text

namespace {

bool
IsTerminator(char c)
{
	return c == '\n' || c == '\r' || c == 0x02 || c == 0x05 || c == 0x0c;
}


// global text offset of a paragraph start
uint32_t
ParagraphStart(const WPPart& part, size_t paragraph)
{
	uint32_t start = 0;
	for (size_t i = 0; i < paragraph && i < part.paragraphs.size(); i++)
		start += uint32_t(part.paragraphs[i].text.size());
	return start;
}


void
ShiftSections(WPPart& part, uint32_t from, int64_t delta)
{
	for (WPSection& s : part.sections)
		if (s.start > from)
			s.start = uint32_t(std::max<int64_t>(from, int64_t(s.start) + delta));
}

} // namespace


size_t
EditableLength(const WPParagraph& p)
{
	size_t n = p.text.size();
	if (n > 0 && IsTerminator(p.text[n - 1]))
		n--;
	return n;
}


size_t
NextCharacter(const std::string& text, size_t offset)
{
	if (offset >= text.size())
		return text.size();
	offset++;
	while (offset < text.size() && (uint8_t(text[offset]) & 0xc0) == 0x80)
		offset++;
	return offset;
}


size_t
PreviousCharacter(const std::string& text, size_t offset)
{
	if (offset == 0)
		return 0;
	offset--;
	while (offset > 0 && (uint8_t(text[offset]) & 0xc0) == 0x80)
		offset--;
	return offset;
}


TextPosition
InsertText(WPPart& part, TextPosition at, const std::string& text)
{
	if (at.paragraph >= part.paragraphs.size() || text.empty())
		return at;
	WPParagraph& p = part.paragraphs[at.paragraph];
	at.offset = std::min(at.offset, EditableLength(p));
	p.text.insert(at.offset, text);
	uint32_t n = uint32_t(text.size());
	// text typed at a run boundary takes the style before it
	for (WPStyleRun& r : p.runs)
		if (r.position > at.offset || (r.position == at.offset && at.offset > 0))
			r.position += n;
	for (WPMiscInfo& m : p.misc)
		if (m.position >= at.offset)
			m.position += n;
	uint32_t global = ParagraphStart(part, at.paragraph) + uint32_t(at.offset);
	ShiftSections(part, global, n);
	part.edits.emplace_back(global, int64_t(n));
	at.offset += n;
	return at;
}


bool
DeleteText(WPPart& part, TextPosition from, size_t to)
{
	if (from.paragraph >= part.paragraphs.size())
		return false;
	WPParagraph& p = part.paragraphs[from.paragraph];
	to = std::min(to, EditableLength(p));
	if (from.offset >= to)
		return false;
	for (const WPMiscInfo& m : p.misc)
		if (m.position >= from.offset && m.position < to)
			return false;
	uint32_t n = uint32_t(to - from.offset);
	p.text.erase(from.offset, n);
	for (WPStyleRun& r : p.runs) {
		if (r.position >= to)
			r.position -= n;
		else if (r.position > from.offset)
			r.position = uint32_t(from.offset);
	}
	// runs that became empty: keep the last of those at the same position
	for (size_t i = 0; i + 1 < p.runs.size();) {
		if (p.runs[i].position == p.runs[i + 1].position && p.runs.size() > 1)
			p.runs.erase(p.runs.begin() + i);
		else
			i++;
	}
	for (WPMiscInfo& m : p.misc)
		if (m.position >= to)
			m.position -= n;
	uint32_t global = ParagraphStart(part, from.paragraph) + uint32_t(from.offset);
	ShiftSections(part, global, -int64_t(n));
	part.edits.emplace_back(global, -int64_t(n));
	return true;
}


TextPosition
SplitParagraph(WPPart& part, TextPosition at)
{
	if (at.paragraph >= part.paragraphs.size())
		return at;
	WPParagraph& p = part.paragraphs[at.paragraph];
	at.offset = std::min(at.offset, EditableLength(p));
	WPParagraph next;
	next.value12 = p.value12;
	next.parStyle = p.parStyle;
	next.text = p.text.substr(at.offset);
	p.text = p.text.substr(0, at.offset) + "\n";
	// the run in effect at the split continues in the new paragraph
	uint32_t current = p.runs.empty() ? 0xffffffff : p.runs[0].style;
	std::vector<WPStyleRun> keep;
	for (const WPStyleRun& r : p.runs) {
		if (r.position <= at.offset) {
			// the style of the text before the split (not of the terminator)
			if (r.position < at.offset || at.offset == 0)
				current = r.style;
			if (r.position < at.offset || at.offset == 0)
				keep.push_back(r);
			else
				next.runs.push_back({0, r.style});
		} else
			next.runs.push_back({uint32_t(r.position - at.offset), r.style});
	}
	// typed text at the start of the new paragraph continues the style
	if (!p.runs.empty()) {
		if (!next.runs.empty() && next.runs[0].position == 0 && next.text.size() <= 1)
			next.runs[0].style = current;
		else if (next.runs.empty() || next.runs[0].position != 0)
			next.runs.insert(next.runs.begin(), {0, current});
	}
	if (keep.empty() && !p.runs.empty())
		keep.push_back({0, p.runs[0].style});
	p.runs = keep;
	std::vector<WPMiscInfo> stay;
	for (WPMiscInfo& m : p.misc) {
		if (m.position >= at.offset) {
			m.position -= uint32_t(at.offset);
			next.misc.push_back(std::move(m));
		} else
			stay.push_back(std::move(m));
	}
	p.misc = std::move(stay);
	// one more character (the new terminator): sections after it move by one
	uint32_t global = ParagraphStart(part, at.paragraph) + uint32_t(at.offset);
	ShiftSections(part, global, 1);
	part.edits.emplace_back(global, 1);
	part.paragraphs.insert(part.paragraphs.begin() + at.paragraph + 1, std::move(next));
	return {at.paragraph + 1, 0};
}


bool
JoinWithPrevious(WPPart& part, size_t paragraph, TextPosition& joined)
{
	if (paragraph == 0 || paragraph >= part.paragraphs.size())
		return false;
	WPParagraph& prev = part.paragraphs[paragraph - 1];
	WPParagraph& p = part.paragraphs[paragraph];
	// page, column and section breaks go like any character, as in the
	// original (TParagraph::DeleteText; sections keep their own starts)
	if (prev.text.empty() || !IsTerminator(prev.text.back()))
		return false;
	size_t base = prev.text.size() - 1;
	uint32_t globalBase = ParagraphStart(part, paragraph - 1) + uint32_t(base);
	prev.text.erase(base);
	prev.text += p.text;
	for (const WPStyleRun& r : p.runs) {
		if (r.position == 0 && !prev.runs.empty() && prev.runs.back().style == r.style)
			continue;
		prev.runs.push_back({uint32_t(r.position + base), r.style});
	}
	for (WPMiscInfo& m : p.misc) {
		m.position += uint32_t(base);
		prev.misc.push_back(std::move(m));
	}
	part.paragraphs.erase(part.paragraphs.begin() + paragraph);
	ShiftSections(part, globalBase, -1);
	part.edits.emplace_back(globalBase, -1);
	joined = {paragraph - 1, base};
	return true;
}


// Extracts plain text between two positions, handling paragraph boundaries and clamping.
std::string
RangeText(const WPPart& part, TextPosition from, TextPosition to)
{
	if (from.paragraph > to.paragraph || (from.paragraph == to.paragraph && from.offset > to.offset))
	{
		auto tmp = from; from = to; to = tmp;
	}
	const auto N = part.paragraphs.size();
	if (N == 0) return {};
	size_t f_p = std::min(from.paragraph, N - 1);
	size_t t_p = std::min(to.paragraph, N - 1);
	size_t f_o = std::min(from.offset, EditableLength(part.paragraphs[f_p]));
	size_t t_o = std::min(to.offset, EditableLength(part.paragraphs[t_p]));
	if (f_p == t_p)
		return part.paragraphs[f_p].text.substr(f_o, t_o - f_o);
	std::string res;
	for (size_t i = f_p; i <= t_p; ++i)
	{
		const auto& p = part.paragraphs[i];
		size_t start = (i == f_p) ? f_o : 0;
		size_t end = (i == t_p) ? t_o : EditableLength(p);
		res += p.text.substr(start, end - start);
		if (i < t_p) res += '\n';
	}
	return res;
}


// Deletes [from, to) across paragraphs; false (nothing changed) if it would
// remove a misc-info anchor or a break
bool
DeleteRange(WPPart& part, TextPosition from, TextPosition to, TextPosition& result)
{
	if (from.paragraph > to.paragraph
			|| (from.paragraph == to.paragraph && from.offset > to.offset))
		std::swap(from, to);
	const size_t count = part.paragraphs.size();
	if (from.paragraph >= count || to.paragraph >= count)
		return false;
	from.offset = std::min(from.offset, EditableLength(part.paragraphs[from.paragraph]));
	to.offset = std::min(to.offset, EditableLength(part.paragraphs[to.paragraph]));
	result = from;
	if (from.paragraph == to.paragraph && from.offset == to.offset)
		return true;

	// check everything first
	for (size_t i = from.paragraph; i <= to.paragraph; i++) {
		const WPParagraph& p = part.paragraphs[i];
		if (i < to.paragraph && (p.text.empty() || !IsTerminator(p.text.back())))
			return false;
		size_t a = i == from.paragraph ? from.offset : 0;
		size_t b = i == to.paragraph ? to.offset : EditableLength(p);
		for (const WPMiscInfo& m : p.misc)
			if (m.position >= a && m.position < b)
				return false;
	}

	if (from.paragraph == to.paragraph)
		return DeleteText(part, from, to.offset);
	for (size_t i = to.paragraph; i > from.paragraph; i--) {
		size_t b = i == to.paragraph ? to.offset : EditableLength(part.paragraphs[i]);
		DeleteText(part, {i, 0}, b);
	}
	DeleteText(part, from, EditableLength(part.paragraphs[from.paragraph]));
	TextPosition joined;
	for (size_t i = to.paragraph; i > from.paragraph; i--)
		if (!JoinWithPrevious(part, i, joined))
			return false;
	return true;
}


// Inserts text that may contain line breaks, splitting paragraphs at them
TextPosition
InsertMultiline(WPPart& part, TextPosition at, const std::string& text)
{
	std::string piece;
	for (size_t i = 0; i <= text.size(); i++) {
		char c = i < text.size() ? text[i] : '\n';
		if (c == '\r' && i + 1 < text.size() && text[i + 1] == '\n')
			continue;
		if (c == '\n' || c == '\r') {
			at = InsertText(part, at, piece);
			piece.clear();
			if (i < text.size())
				at = SplitParagraph(part, at);
		} else
			piece += c;
	}
	return at;
}


namespace {

// 0: word character, 1: blank, 2: punctuation (a run by itself)
int
CharacterKind(char c)
{
	unsigned char u = (unsigned char)c;
	if (u >= 0x80 || isalnum(u) || u == '_')
		return 0;
	return u == ' ' || u == '\t' ? 1 : 2;
}

} // namespace


void
WordRange(const std::string& text, size_t offset, size_t& from, size_t& to)
{
	from = to = 0;
	if (text.empty())
		return;
	// the character at offset, or the one before at the end
	size_t start = offset >= text.size() ? PreviousCharacter(text, text.size()) : offset;
	int kind = CharacterKind(text[start]);
	from = start;
	to = NextCharacter(text, start);
	if (kind == 2)
		return;
	while (from > 0) {
		size_t previous = PreviousCharacter(text, from);
		if (CharacterKind(text[previous]) != kind)
			break;
		from = previous;
	}
	while (to < text.size() && CharacterKind(text[to]) == kind)
		to = NextCharacter(text, to);
}


// #pragma mark - styles

namespace {

bool
SameStyle(const Style& a, const Style& b)
{
	return a.id == b.id && a.value8 == b.value8 && a.valueC == b.valueC
		&& a.value10 == b.value10 && a.name == b.name && a.props == b.props
		&& a.props2 == b.props2 && a.substyles.empty() && b.substyles.empty()
		&& std::equal(a.links, a.links + 3, b.links);
}


uint32_t
AddProperty(StyleMgr& styles, const Property& property)
{
	for (size_t i = 0; i < styles.properties.size(); i++)
		if (styles.properties[i].type == property.type
			&& styles.properties[i].data == property.data)
			return uint32_t(i);
	styles.properties.push_back(property);
	return uint32_t(styles.properties.size() - 1);
}


void
Retain(StyleMgr& styles, uint32_t style)
{
	if (style < styles.styles.size())
		styles.styles[style].value4++;
}


void
Release(StyleMgr& styles, uint32_t style)
{
	if (style < styles.styles.size() && styles.styles[style].value4 > 1)
		styles.styles[style].value4--;
}


// the run in effect at `offset`
size_t
RunAt(const WPParagraph& p, size_t offset)
{
	size_t run = 0;
	for (size_t i = 0; i < p.runs.size(); i++)
		if (p.runs[i].position <= offset)
			run = i;
	return run;
}

} // namespace


uint32_t
StyleWithProperty(StyleMgr& styles, uint32_t base, const Property& property)
{
	if (base >= styles.styles.size())
		return base;
	uint32_t index = AddProperty(styles, property);
	Style style;
	if (styles.styles[base].name.empty()) {
		// a copy with the property replaced or added
		style = styles.styles[base];
		bool replaced = false;
		for (uint32_t& p : style.props)
			if (p < styles.properties.size() && styles.properties[p].type == property.type) {
				p = index;
				replaced = true;
			}
		if (!replaced) {
			style.props.push_back(index);
			std::sort(style.props.begin(), style.props.end());
		}
	} else {
		// a named style: an unnamed child holding the property
		style = styles.styles[base];
		style.name.clear();
		style.value8 = 32;
		style.links[0] = base;
		style.props = {index};
	}
	style.substyles.clear();
	for (size_t i = 0; i < styles.styles.size(); i++)
		if (SameStyle(styles.styles[i], style))
			return uint32_t(i);
	style.value4 = 1;
	Retain(styles, style.links[0]);
	styles.styles.push_back(style);
	return uint32_t(styles.styles.size() - 1);
}


bool
ApplyCharacterProperty(Document& doc, size_t part, TextPosition from, TextPosition to,
	const Property& property)
{
	if (to.paragraph < from.paragraph
			|| (to.paragraph == from.paragraph && to.offset < from.offset))
		std::swap(from, to);
	WPPart* wp = doc.EditableWordPart(part);
	if (wp == nullptr || from.paragraph >= wp->paragraphs.size())
		return false;
	to.paragraph = std::min(to.paragraph, wp->paragraphs.size() - 1);
	StyleMgr& styles = doc.styles;
	bool changed = false;
	for (size_t k = from.paragraph; k <= to.paragraph; k++) {
		WPParagraph& p = wp->paragraphs[k];
		if (p.runs.empty())
			continue;
		size_t length = EditableLength(p);
		size_t a = k == from.paragraph ? std::min(from.offset, length) : 0;
		size_t b = k == to.paragraph ? std::min(to.offset, length) : length;
		if (a >= b && !(a == 0 && length == 0))
			continue;
		// runs starting at a and b
		for (size_t at : {a, b}) {
			if (at >= length && at != 0)
				continue;
			size_t r = RunAt(p, at);
			if (p.runs[r].position != at) {
				p.runs.insert(p.runs.begin() + r + 1, {uint32_t(at), p.runs[r].style});
				Retain(styles, p.runs[r].style);
			}
		}
		for (WPStyleRun& run : p.runs) {
			if (run.position < a || (run.position >= b && b < length))
				continue;
			uint32_t style = StyleWithProperty(styles, run.style, property);
			if (style != run.style) {
				Retain(styles, style);
				Release(styles, run.style);
				run.style = style;
				changed = true;
			}
		}
		// neighbours alike: one run
		for (size_t i = 1; i < p.runs.size();) {
			if (p.runs[i].style == p.runs[i - 1].style) {
				Release(styles, p.runs[i].style);
				p.runs.erase(p.runs.begin() + i);
			} else
				i++;
		}
	}
	if (changed)
		doc.stylesChanged = true;
	return changed;
}


bool
ApplyParagraphProperty(Document& doc, size_t part, size_t first, size_t last,
	const Property& property)
{
	WPPart* wp = doc.EditableWordPart(part);
	if (wp == nullptr || first >= wp->paragraphs.size())
		return false;
	if (last < first)
		std::swap(first, last);
	last = std::min(last, wp->paragraphs.size() - 1);
	bool changed = false;
	for (size_t k = first; k <= last; k++) {
		WPParagraph& p = wp->paragraphs[k];
		uint32_t style = StyleWithProperty(doc.styles, p.parStyle, property);
		if (style != p.parStyle) {
			Retain(doc.styles, style);
			Release(doc.styles, p.parStyle);
			p.parStyle = style;
			changed = true;
		}
	}
	if (changed)
		doc.stylesChanged = true;
	return changed;
}


// #pragma mark - rows and columns

namespace {

// where row/column `t` goes; -1 if it's deleted
struct LineMap {
	int at, count;		// count < 0: deleted [at, at - count)

	int operator()(int t) const
	{
		if (t < at)
			return t;
		if (count > 0)
			return t + count;
		return t < at - count ? -1 : t + count;
	}
	// a range's ends: [from, to] -> new [from, to], false if nothing is left
	bool Range(int& from, int& to) const
	{
		int f = (*this)(from), e = (*this)(to);
		if (f < 0)
			f = at;				// first row after the deletion
		if (e < 0)
			e = at - 1;			// last row before it
		if (e < f)
			return false;
		from = f;
		to = e;
		return true;
	}
};


void
MapRange(SSRange& r, const LineMap& map, bool columns, bool& valid)
{
	int from = columns ? r.from.b : r.from.a;
	int to = columns ? r.to.b : r.to.a;
	valid = map.Range(from, to);
	if (!valid)
		return;
	(columns ? r.from.b : r.from.a) = uint16_t(from);
	(columns ? r.to.b : r.to.a) = uint16_t(to);
}

} // namespace


bool
InsertRowsColumns(Document& doc, size_t part, bool columns, uint16_t at, int count)
{
	SSPart* ss = doc.EditableSpreadsheetPart(part);
	if (ss == nullptr || count == 0)
		return false;
	LineMap map{at, count};
	auto line = [&](const CellID& id) -> int { return columns ? id.b : id.a; };
	auto setLine = [&](CellID& id, int v) { (columns ? id.b : id.a) = uint16_t(v); };

	// formulas, with their references resolved against the old positions
	// and stored against the new ones
	std::vector<SSCell> cells;
	for (SSCell& cell : ss->cells) {
		int moved = map(line(cell.id));
		if (moved < 0)
			continue;
		CellID oldAt = cell.id, newAt = cell.id;
		setLine(newAt, moved);
		const uint16_t absolute = columns ? 4 : 2;
		for (FormulaToken& token : cell.formula) {
			bool range = token.refs.size() == 2;
			int ends[2];
			bool live = true;
			for (size_t k = 0; k < token.refs.size(); k++) {
				CellRef& r = token.refs[k];
				if (r.flags & 1) {
					live = false;
					continue;
				}
				int16_t stored = int16_t(columns ? r.cell.b : r.cell.a);
				ends[k % 2] = (r.flags & absolute) ? stored : line(oldAt) + stored;
			}
			if (!live || token.refs.empty())
				continue;
			bool valid = true;
			if (range)
				valid = map.Range(ends[0], ends[1]);
			else {
				ends[0] = map(ends[0]);
				valid = ends[0] >= 0;
			}
			for (size_t k = 0; k < token.refs.size(); k++) {
				CellRef& r = token.refs[k];
				if (!valid) {
					r.flags |= 1;
					continue;
				}
				int target = ends[k % 2];
				int stored = (r.flags & absolute) ? target : target - line(newAt);
				(columns ? r.cell.b : r.cell.a) = uint16_t(int16_t(stored));
			}
		}
		cell.id = newAt;
		cells.push_back(std::move(cell));
	}
	ss->cells = std::move(cells);

	// sizes of rows/columns
	std::vector<SSHeader>& headers = columns ? ss->columns : ss->rows;
	std::vector<SSHeader> kept;
	for (SSHeader& h : headers) {
		int moved = map(int(h.index));
		if (moved < 0)
			continue;
		h.index = uint32_t(moved);
		kept.push_back(h);
	}
	headers = std::move(kept);

	// ranges kept with the part
	bool valid;
	SSRange used = ss->used;
	MapRange(used, map, columns, valid);
	if (valid)
		ss->used = used;
	std::vector<SSRange> selection;
	for (SSRange r : ss->selection) {
		MapRange(r, map, columns, valid);
		if (valid)
			selection.push_back(r);
	}
	if (!selection.empty())
		ss->selection = std::move(selection);
	std::vector<SSFill> fills;
	for (SSFill& fill : ss->fills) {
		MapRange(fill.range, map, columns, valid);
		if (!valid)
			continue;
		std::vector<SSRange> fillCells;
		for (SSRange r : fill.cells) {
			bool ok;
			MapRange(r, map, columns, ok);
			if (ok)
				fillCells.push_back(r);
		}
		fill.cells = std::move(fillCells);
		fills.push_back(std::move(fill));
	}
	ss->fills = std::move(fills);

	// the calculation order: moved cells, deleted ones left out
	std::vector<CellID> dependents;
	for (CellID id : ss->dependents) {
		int moved = map(line(id));
		if (moved < 0)
			continue;
		setLine(id, moved);
		dependents.push_back(id);
	}
	ss->dependents = std::move(dependents);

	// named ranges and selections of this part ((row, column) pairs); their
	// number stays the same so they can be written in place
	for (Selection& sel : doc.selections.selections) {
		if (sel.kind != Sig("SPRD") || sel.part != part)
			continue;
		int c = columns ? sel.cell[1] : sel.cell[0];
		c = map(c);
		(columns ? sel.cell[1] : sel.cell[0]) = uint16_t(c < 0 ? at : c);
		for (auto& r : sel.ranges) {
			int from = columns ? r[1] : r[0], to = columns ? r[3] : r[2];
			if (!map.Range(from, to))
				from = to = std::min<int>(at, 0xfffe);	// nothing left: one row
			(columns ? r[1] : r[0]) = uint16_t(from);
			(columns ? r[3] : r[2]) = uint16_t(to);
		}
		doc.selectionsChanged = true;
	}

	UpdateDependencies(doc, part);
	return true;
}


// #pragma mark - formulas

namespace {

class Parser {
public:
	Parser(const std::string& text, CellID at,
			const std::function<bool(const std::string&)>& isName)
		: fText(text), fAt(at), fIsName(isName) {}

	bool Parse(std::vector<FormulaToken>& out, std::string& error)
	{
		try {
			Comparison(out);
			Blanks();
			if (fPos != fText.size())
				throw std::string("unexpected '") + fText.substr(fPos, 8) + "'";
			out.push_back(Token(0x1d));
			return true;
		} catch (const std::string& e) {
			error = e;
			return false;
		}
	}

private:
	static FormulaToken Token(uint16_t kind)
	{
		FormulaToken t;
		t.kind = kind;
		return t;
	}

	void Blanks()
	{
		while (fPos < fText.size() && isspace((unsigned char)fText[fPos]))
			fPos++;
	}

	bool Take(const char* s)
	{
		Blanks();
		size_t n = strlen(s);
		if (fText.compare(fPos, n, s) == 0) {
			fPos += n;
			return true;
		}
		return false;
	}

	// binary operators are stored before their operands
	void Binary(std::vector<FormulaToken>& out, uint16_t op,
		std::vector<FormulaToken>& left, std::vector<FormulaToken>& right)
	{
		std::vector<FormulaToken> r;
		r.push_back(Token(op));
		r.insert(r.end(), left.begin(), left.end());
		r.insert(r.end(), right.begin(), right.end());
		out = std::move(r);
	}

	// <= >= <> < > = (0-5)
	void Comparison(std::vector<FormulaToken>& out)
	{
		std::vector<FormulaToken> left;
		Concat(left);
		for (;;) {
			int op = -1;
			if (Take("<="))
				op = 0;
			else if (Take(">="))
				op = 1;
			else if (Take("<>"))
				op = 2;
			else if (Take("<"))
				op = 3;
			else if (Take(">"))
				op = 4;
			else if (Take("="))
				op = 5;
			if (op < 0)
				break;
			std::vector<FormulaToken> right;
			Concat(right);
			Binary(left, uint16_t(op), left, right);
		}
		out.insert(out.end(), left.begin(), left.end());
	}

	void Concat(std::vector<FormulaToken>& out)
	{
		std::vector<FormulaToken> left;
		Sum(left);
		while (Take("&")) {
			std::vector<FormulaToken> right;
			Sum(right);
			Binary(left, 6, left, right);
		}
		out.insert(out.end(), left.begin(), left.end());
	}

	void Sum(std::vector<FormulaToken>& out)
	{
		std::vector<FormulaToken> left;
		Product(left);
		for (;;) {
			uint16_t op;
			if (Take("+"))
				op = 7;
			else if (Take("-"))
				op = 8;
			else
				break;
			std::vector<FormulaToken> right;
			Product(right);
			Binary(left, op, left, right);
		}
		out.insert(out.end(), left.begin(), left.end());
	}

	void Product(std::vector<FormulaToken>& out)
	{
		std::vector<FormulaToken> left;
		Power(left);
		for (;;) {
			uint16_t op;
			if (Take("*"))
				op = 9;
			else if (Take("/"))
				op = 10;
			else
				break;
			std::vector<FormulaToken> right;
			Power(right);
			Binary(left, op, left, right);
		}
		out.insert(out.end(), left.begin(), left.end());
	}

	void Power(std::vector<FormulaToken>& out)
	{
		std::vector<FormulaToken> left;
		Unary(left);
		while (Take("^")) {
			std::vector<FormulaToken> right;
			Unary(right);
			Binary(left, 11, left, right);
		}
		out.insert(out.end(), left.begin(), left.end());
	}

	void Unary(std::vector<FormulaToken>& out)
	{
		if (Take("-")) {
			out.push_back(Token(0xd));
			Unary(out);
			return;
		}
		if (Take("+")) {
			Unary(out);
			return;
		}
		std::vector<FormulaToken> operand;
		Primary(operand);
		while (Take("%")) {
			std::vector<FormulaToken> r = {Token(0xc)};
			r.insert(r.end(), operand.begin(), operand.end());
			operand = std::move(r);
		}
		out.insert(out.end(), operand.begin(), operand.end());
	}

	// a cell reference at fPos: [$]letters[$]digits
	bool CellRefAt(size_t& pos, CellRef& ref)
	{
		size_t p = pos;
		bool absColumn = false, absRow = false;
		if (p < fText.size() && fText[p] == '$') {
			absColumn = true;
			p++;
		}
		size_t letters = p;
		while (p < fText.size() && isalpha((unsigned char)fText[p]))
			p++;
		size_t nLetters = p - letters;
		if (nLetters < 1 || nLetters > 2)
			return false;
		if (p < fText.size() && fText[p] == '$') {
			absRow = true;
			p++;
		}
		size_t digits = p;
		while (p < fText.size() && isdigit((unsigned char)fText[p]))
			p++;
		if (p == digits || (p < fText.size() && (isalnum((unsigned char)fText[p])
				|| fText[p] == '_' || fText[p] == '(')))
			return false;
		int column = 0;
		for (size_t i = letters; i < letters + nLetters; i++)
			column = column * 26 + (toupper((unsigned char)fText[i]) - 'A' + 1);
		column--;
		int row = atoi(fText.c_str() + digits) - 1;
		if (row < 0 || row > 0x3fff || column > 0x2be)
			return false;
		ref.flags = (absRow ? 2 : 0) | (absColumn ? 4 : 0);
		ref.cell.a = uint16_t(absRow ? row : row - fAt.a);
		ref.cell.b = uint16_t(absColumn ? column : column - fAt.b);
		pos = p;
		return true;
	}

	void Primary(std::vector<FormulaToken>& out)
	{
		Blanks();
		if (fPos >= fText.size())
			throw std::string("the formula ends early");
		char c = fText[fPos];
		if (c == '(') {
			fPos++;
			out.push_back(Token(0xe));
			Comparison(out);
			if (!Take(")"))
				throw std::string("missing ')'");
			out.push_back(Token(0xf));
			return;
		}
		if (c == '"') {
			size_t end = fText.find('"', fPos + 1);
			if (end == std::string::npos)
				throw std::string("missing '\"'");
			FormulaToken t = Token(0x11);
			t.strings.push_back(fText.substr(fPos + 1, end - fPos - 1));
			fPos = end + 1;
			out.push_back(t);
			return;
		}
		if (isdigit((unsigned char)c) || c == '.') {
			char* end;
			double v = strtod(fText.c_str() + fPos, &end);
			std::string literal(fText.c_str() + fPos, size_t(end - (fText.c_str() + fPos)));
			fPos = end - fText.c_str();
			bool integer = literal.find_first_of(".eE") == std::string::npos;
			FormulaToken t = Token(integer && fabs(v) < 32768 ? 0x13
				: integer && fabs(v) < 2147483648.0 ? 0x14 : 0x15);
			t.number = v;
			out.push_back(t);
			return;
		}
		// a cell or range reference
		size_t p = fPos;
		CellRef from;
		if (CellRefAt(p, from)) {
			size_t q = p;
			while (q < fText.size() && isspace((unsigned char)fText[q]))
				q++;
			CellRef to;
			if (q < fText.size() && fText[q] == ':') {
				q++;
				while (q < fText.size() && isspace((unsigned char)fText[q]))
					q++;
				if (CellRefAt(q, to)) {
					FormulaToken t = Token(0x1a);
					t.refs = {from, to};
					fPos = q;
					out.push_back(t);
					return;
				}
			}
			FormulaToken t = Token(0x19);
			t.refs = {from};
			fPos = p;
			out.push_back(t);
			return;
		}
		// names: the longest known name here (names may contain spaces)
		for (size_t end = fText.size(); end > fPos; end--) {
			std::string name = fText.substr(fPos, end - fPos);
			while (!name.empty() && isspace((unsigned char)name.back()))
				name.pop_back();
			if (!name.empty() && fIsName && fIsName(name)) {
				FormulaToken t = Token(0x1b);
				t.value = 0;
				t.strings = {"", name};
				fPos += name.size();
				out.push_back(t);
				return;
			}
		}
		// a function call or TRUE/FALSE
		size_t start = fPos;
		while (fPos < fText.size() && (isalnum((unsigned char)fText[fPos]) || fText[fPos] == '_'))
			fPos++;
		std::string word = fText.substr(start, fPos - start);
		if (word.empty())
			throw std::string("unexpected '") + c + "'";
		std::string upper = word;
		for (char& ch : upper)
			ch = toupper((unsigned char)ch);
		if (!Take("(")) {
			if (upper == "TRUE" || upper == "FALSE") {
				FormulaToken t = Token(0x12);
				t.value = upper == "TRUE";
				out.push_back(t);
				return;
			}
			throw std::string("unknown name '") + word + "'";
		}
		FormulaToken call = Token(0x1c);
		call.strings.push_back(word);
		std::vector<FormulaToken> args;
		int argc = 0;
		if (!Take(")")) {
			do {
				Comparison(args);
				argc++;
			} while (Take(","));
			if (!Take(")"))
				throw std::string("missing ')' after the arguments of ") + word;
		}
		call.value = uint32_t(argc);
		out.push_back(call);
		out.insert(out.end(), args.begin(), args.end());
	}

	const std::string& fText;
	CellID fAt;
	const std::function<bool(const std::string&)>& fIsName;
	size_t fPos = 0;
};

} // namespace


bool
ParseFormula(const std::string& text, CellID at,
	const std::function<bool(const std::string&)>& isName,
	std::vector<FormulaToken>& tokens, std::string& error)
{
	tokens.clear();
	Parser parser(text, at, isName);
	return parser.Parse(tokens, error);
}


// #pragma mark - cells

namespace {

bool
ParseDate(const std::string& s, int64_t& seconds)
{
	int m, d, y;
	char tail;
	if (sscanf(s.c_str(), "%d/%d/%d%c", &m, &d, &y, &tail) != 3)
		return false;
	if (m < 1 || m > 12 || d < 1 || d > 31)
		return false;
	if (y < 100)
		y += y < 70 ? 2000 : 1900;
	// days from civil
	int yy = y - (m <= 2);
	int era = (yy >= 0 ? yy : yy - 399) / 400;
	int yoe = yy - era * 400;
	int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
	int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	seconds = (int64_t(era) * 146097 + doe - 719468) * 86400;
	return true;
}

} // namespace


bool
SetCellInput(SSPart& part, uint16_t row, uint16_t column, const std::string& input,
	const std::function<bool(const std::string&)>& isName, std::string& error)
{
	// cells are kept column by column, as TSparseArray writes them
	auto it = std::find_if(part.cells.begin(), part.cells.end(),
		[&](const SSCell& c) { return c.id.a == row && c.id.b == column; });
	SSCell cell;
	if (it != part.cells.end())
		cell = *it;
	cell.id = {row, column};
	std::string text = input;	// kept as typed (text cells may end in spaces)

	SSCell value = cell;
	value.formula.clear();
	value.text.clear();
	value.text2.clear();
	value.number = 0;
	value.value = 0;
	double number;
	char* end;
	int64_t seconds;
	if (text.empty()) {
		value.kind = Sig("NONE");
	} else if (text[0] == '=') {
		value.kind = Sig("Fmla");
		if (!ParseFormula(text.substr(1), cell.id, isName, value.formula, error))
			return false;
	} else if ((number = strtod(text.c_str(), &end)), *end == 0 && end != text.c_str()) {
		if (number == floor(number) && fabs(number) < 2147483648.0
				&& text.find_first_of(".eE") == std::string::npos) {
			value.kind = Sig("Lng#");
			value.value = uint32_t(int32_t(number));
		} else
			value.kind = Sig("Dbl#");
		value.number = number;
	} else if (text == "TRUE" || text == "true" || text == "FALSE" || text == "false") {
		value.kind = Sig("Bool");
		value.number = text[0] == 'T' || text[0] == 't';
	} else if (ParseDate(text, seconds)) {
		value.kind = Sig("Time");
		value.value = uint32_t(seconds);
		value.number = double(seconds);
	} else {
		value.kind = Sig("Strn");
		value.text = text;
	}

	if (it != part.cells.end()) {
		if (value.kind == Sig("NONE") && value.style == 0xffffffff)
			part.cells.erase(it);
		else
			*it = value;
		return true;
	}
	if (value.kind == Sig("NONE"))
		return true;
	auto at = std::find_if(part.cells.begin(), part.cells.end(), [&](const SSCell& c) {
		return std::make_pair(c.id.b, c.id.a) > std::make_pair(column, row);
	});
	part.cells.insert(at, value);
	// the used range grows to include the cell
	part.used.to.a = std::max(part.used.to.a, row);
	part.used.to.b = std::max(part.used.to.b, column);
	return true;
}


bool
UpdateDependencies(Document& doc, size_t partIndex)
{
	SSPart* part = nullptr;
	for (auto& [index, ss] : doc.spreadsheetParts)
		if (index == partIndex)
			part = &ss;
	if (part == nullptr)
		return false;

	// the formula cells and the same-part cells each one refers to
	std::map<uint32_t, const SSCell*> formulas;
	auto key = [](int row, int column) { return uint32_t(row) << 16 | uint16_t(column); };
	for (const SSCell& c : part->cells)
		if (c.kind == Sig("Fmla"))
			formulas[key(c.id.a, c.id.b)] = &c;
	auto resolve = [](const CellRef& r, CellID at, int& row, int& column) {
		if (r.flags & 1)
			return false;
		row = (r.flags & 2) ? int16_t(r.cell.a) : at.a + int16_t(r.cell.a);
		column = (r.flags & 4) ? int16_t(r.cell.b) : at.b + int16_t(r.cell.b);
		return true;
	};
	auto precedents = [&](const SSCell& c) {
		std::vector<uint32_t> out;
		for (const FormulaToken& t : c.formula) {
			int r0, c0, r1, c1;
			if (t.kind == 0x19 && resolve(t.refs[0], c.id, r0, c0)) {
				if (formulas.count(key(r0, c0)))
					out.push_back(key(r0, c0));
			} else if (t.kind == 0x1a && resolve(t.refs[0], c.id, r0, c0)
				&& resolve(t.refs[1], c.id, r1, c1)) {
				for (auto& [k, f] : formulas) {
					int row = k >> 16, column = int16_t(k & 0xffff);
					if (row >= std::min(r0, r1) && row <= std::max(r0, r1)
						&& column >= std::min(c0, c1) && column <= std::max(c0, c1))
						out.push_back(k);
				}
			}
		}
		return out;
	};

	// TSSDepManager::FixFormulaCells: a depth-first walk in list order
	std::vector<uint32_t> order;
	std::set<uint32_t> seen;
	for (const CellID& id : part->dependents)
		if (formulas.count(key(id.a, id.b)) && seen.insert(key(id.a, id.b)).second)
			order.push_back(key(id.a, id.b));
	for (auto& [k, f] : formulas)
		if (seen.insert(k).second)
			order.push_back(k);
	std::vector<CellID> sorted;
	std::set<uint32_t> done, visiting;
	std::function<void(uint32_t)> visit = [&](uint32_t k) {
		if (done.count(k) || visiting.count(k))
			return;	// circular references keep their place
		visiting.insert(k);
		for (uint32_t p : precedents(*formulas[k]))
			if (p != k)
				visit(p);
		visiting.erase(k);
		done.insert(k);
		sorted.push_back({uint16_t(k >> 16), uint16_t(k & 0xffff)});
	};
	for (uint32_t k : order)
		visit(k);
	part->dependents = std::move(sorted);

	// links: the names the formulas use, by link
	std::map<uint32_t, std::vector<CellID>> users;	// link -> cells (in cell order)
	bool linksChanged = false;
	for (const SSCell& c : part->cells) {
		if (c.kind != Sig("Fmla"))
			continue;
		std::set<uint32_t> linked;
		for (const FormulaToken& t : c.formula) {
			if (t.kind != 0x1b || t.strings.size() < 2)
				continue;
			// the reference of that name and the link from it to this part
			size_t sourcePart, sourceRef;
			if (!doc.FindName(t.strings[1], int(partIndex), sourcePart, sourceRef))
				continue;
			int source = int(sourcePart), reference = int(sourceRef);
			int link = -1;
			for (size_t l = 0; l < doc.links.links.size(); l++) {
				const Link& k = doc.links.links[l];
				if (k.sourcePart == uint32_t(source) && k.reference == uint32_t(reference)
					&& k.dependentPart == partIndex)
					link = int(l);
			}
			if (link < 0) {
				Link k;
				k.value20 = 1;
				k.sourcePart = uint32_t(source);
				k.dependentPart = uint32_t(partIndex);
				k.reference = uint32_t(reference);
				k.value1c = uint32_t(doc.links.links.size());
				link = int(doc.links.links.size());
				doc.links.links.push_back(k);
				linksChanged = true;
			}
			if (linked.insert(uint32_t(link)).second)
				users[uint32_t(link)].push_back(c.id);
		}
	}
	std::vector<SSPart::LinkDep> deps;
	std::set<uint32_t> present;
	for (const SSPart::LinkDep& old : part->linkDepInfo) {
		auto it = users.find(old.link);
		if (it == users.end())
			continue;	// no formula uses it any more
		SSPart::LinkDep d = old;
		// keep the original order of the cells still using it, new ones after
		std::vector<SSRange> cells;
		std::set<uint32_t> keep;
		for (const CellID& id : it->second)
			keep.insert(key(id.a, id.b));
		std::set<uint32_t> listed;
		for (const SSRange& r : old.cells)
			for (int row = r.from.a; row <= r.to.a; row++)
				for (int column = r.from.b; column <= r.to.b; column++)
					if (keep.count(key(row, column)) && listed.insert(key(row, column)).second)
						cells.push_back({{uint16_t(row), uint16_t(column)},
							{uint16_t(row), uint16_t(column)}});
		for (const CellID& id : it->second)
			if (listed.insert(key(id.a, id.b)).second)
				cells.push_back({id, id});
		// a list naming the same cells stays exactly as it was (the original
		// keeps ranges and repeats)
		std::set<uint32_t> oldSet;
		for (const SSRange& r : old.cells)
			for (int row = r.from.a; row <= r.to.a; row++)
				for (int column = r.from.b; column <= r.to.b; column++)
					oldSet.insert(key(row, column));
		if (oldSet != keep) {
			d.cells = std::move(cells);
			d.value = uint32_t(listed.size());
		}
		deps.push_back(std::move(d));
		present.insert(old.link);
	}
	for (auto& [link, cells] : users) {
		if (present.count(link))
			continue;
		SSPart::LinkDep d;
		d.link = link;
		for (const CellID& id : cells)
			d.cells.push_back({id, id});
		d.value = uint32_t(cells.size());
		deps.push_back(std::move(d));
	}
	part->linkDepInfo = std::move(deps);
	part->linkDeps = uint32_t(part->linkDepInfo.size());
	return linksChanged;
}


std::string
CellInput(const SSPart& part, uint16_t row, uint16_t column)
{
	const SSCell* c = part.Cell(row, column);
	if (c == nullptr)
		return "";
	char buf[64];
	switch (c->kind) {
		case Sig("Fmla"):
			return "=" + FormulaText(c->formula, c->id);
		case Sig("Strn"):
			return c->text;
		case Sig("Lng#"):
			return std::to_string(int32_t(c->value));
		case Sig("Flt#"):
		case Sig("Dbl#"):
			snprintf(buf, sizeof buf, "%.15g", c->number);
			return buf;
		case Sig("Bool"):
			return c->number != 0 ? "TRUE" : "FALSE";
		case Sig("Time"): {
			time_t t = time_t(c->value);
			struct tm tm;
			gmtime_r(&t, &tm);
			snprintf(buf, sizeof buf, "%d/%d/%d", tm.tm_mon + 1, tm.tm_mday,
				tm.tm_year + 1900);
			return buf;
		}
		case Sig("FmEr"):
			return c->text;
		default:
			return "";
	}
}


std::string
RangeInput(const SSPart& part, uint16_t row0, uint16_t column0,
	uint16_t row1, uint16_t column1)
{
	std::string s;
	for (uint16_t r = row0; r <= row1; r++) {
		if (r > row0)
			s += "\n";
		for (uint16_t c = column0; c <= column1; c++) {
			if (c > column0)
				s += "\t";
			s += CellInput(part, r, c);
		}
	}
	return s;
}


bool
ClearRange(SSPart& part, uint16_t row0, uint16_t column0,
	uint16_t row1, uint16_t column1)
{
	bool changed = false;
	for (uint16_t r = row0; r <= row1; r++) {
		for (uint16_t c = column0; c <= column1; c++) {
			std::string input = CellInput(part, r, c);
			if (!input.empty()) {
				std::string error;
				SetCellInput(part, r, c, "", nullptr, error);
				changed = true;
			}
		}
	}
	return changed;
}


bool
PasteInput(SSPart& part, uint16_t row, uint16_t column, const std::string& text,
	const std::function<bool(const std::string&)>& isName, std::string& error,
	uint16_t& rows, uint16_t& columns)
{
	// split into rows (the \n of \r\n ends the line)
	std::vector<std::string> lines;
	std::string cur;
	for (size_t i = 0; i <= text.size(); i++) {
		char c = i < text.size() ? text[i] : '\n';
		if (c == '\r' && i + 1 < text.size() && text[i + 1] == '\n')
			continue;
		if (c == '\n' || c == '\r') {
			lines.push_back(cur);
			cur.clear();
		} else
			cur += c;
	}
	// ignore trailing line break (trailing empty line)
	if (!lines.empty() && lines.back().empty())
		lines.pop_back();

	rows = uint16_t(lines.size());
	// determine columns from the row with the most tabs
	uint16_t maxCols = 0;
	for (const auto& line : lines) {
		uint16_t cols = 1;
		for (char c : line)
			if (c == '\t')
				cols++;
		if (cols > maxCols)
			maxCols = cols;
	}
	columns = maxCols;

	// paste cell by cell, stop at first parse failure
	for (uint16_t r = 0; r < rows; r++) {
		const std::string& line = lines[r];
		size_t start = 0;
		for (uint16_t c = 0; c < columns; c++) {
			// short rows: the cells after their last one are cleared
			std::string cellText;
			if (start <= line.size()) {
				size_t end = std::min(line.find('\t', start), line.size());
				cellText = line.substr(start, end - start);
				start = end + 1;
			}
			if (!SetCellInput(part, row + r, column + c, cellText, isName, error))
				return false;
		}
	}
	return true;
}

} // namespace gobe
