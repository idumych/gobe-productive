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
	if (prev.text.empty() || prev.text.back() != '\n')
		return false;	// a page or column break: keep it
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

} // namespace gobe
