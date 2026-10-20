#include "SSEval.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>
#include <ctime>
#include <functional>

namespace gobe {

namespace {

// ErrType values (index into STR# 20)
enum {
	kErrError = 0, kErrNA, kErrDiv0, kErrValue, kErrNum, kErrRef, kErrArg,
	kErrUser, kErrDate, kErrTime, kErrName, kErrFunc, kErrFormula, kErrNull
};

const char* kErrorText[] = {"#ERROR!", "#N/A!", "#DIV/0!", "#VALUE!", "#NUM!",
	"#REF!", "#ARG!", "#USER!", "#DATE!", "#TIME!", "#NAME!", "#FUNC!",
	"#FORMULA!", "#NULL!"};

// TCellRef::ActualValid limits
const int kMaxRow = 0x3fff;
const int kMaxColumn = 0x2be;

uint32_t
Key(int row, int column)
{
	return uint32_t(row) << 16 | uint16_t(column);
}


// TCellRef::ActualCell: flag 2 = absolute row, 4 = absolute column;
// otherwise relative to the formula's cell. Flag 1 = invalid.
bool
Resolve(const CellRef& r, CellID at, CellID& out)
{
	if (r.flags & 1)
		return false;
	int row = (r.flags & 2) ? int16_t(r.cell.a) : at.a + int16_t(r.cell.a);
	int col = (r.flags & 4) ? int16_t(r.cell.b) : at.b + int16_t(r.cell.b);
	if (row < 0 || row > kMaxRow || col < 0 || col > kMaxColumn)
		return false;
	out.a = row;
	out.b = col;
	return true;
}


// TString::Num: a number with optional sign, decimals, exponent; blanks
// around it allowed
bool
StringToNumber(const std::string& s, double& out)
{
	const char* p = s.c_str();
	while (isspace((unsigned char)*p))
		p++;
	if (!*p)
		return false;
	char* end;
	out = strtod(p, &end);
	if (end == p)
		return false;
	while (isspace((unsigned char)*end))
		end++;
	return *end == 0;
}


// TNumberFormatFactory::GeneralFormat, approximately: up to 10 significant
// digits, no exponent for ordinary magnitudes
std::string
GeneralNumber(double n)
{
	char buf[64];
	if (n == 0)
		return "0";
	double a = fabs(n);
	if (a >= 1e15 || a < 1e-9)
		snprintf(buf, sizeof buf, "%.6g", n);
	else {
		snprintf(buf, sizeof buf, "%.10g", n);
		if (strchr(buf, 'e'))
			snprintf(buf, sizeof buf, "%.10f", n);
		if (strchr(buf, '.')) {
			char* e = buf + strlen(buf) - 1;
			while (*e == '0')
				*e-- = 0;
			if (*e == '.')
				*e = 0;
		}
	}
	return buf;
}


// timegm (not on Haiku): seconds since 1970 for a UTC date; month and day
// may be out of range (DATE(2000,13,1) is January 2001)
int64_t
DateToSeconds(int64_t year, int64_t month, int64_t day)
{
	year += (month - 1) / 12;
	month = (month - 1) % 12 + 1;
	if (month <= 0) {
		month += 12;
		year--;
	}
	// days from civil (Howard Hinnant's algorithm)
	year -= month <= 2;
	int64_t era = (year >= 0 ? year : year - 399) / 400;
	int64_t yoe = year - era * 400;
	int64_t doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5;
	int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	int64_t days = era * 146097 + doe - 719468;
	return (days + day - 1) * 86400;
}


// ConvertDateTime: days since 1970 to UTC broken-down time
struct tm
DaysToTm(double days)
{
	time_t t = time_t(llround(days * 86400.0));
	struct tm out;
	gmtime_r(&t, &out);
	return out;
}

} // namespace


std::string
ErrorText(int error)
{
	if (error >= 0 && error < int(sizeof kErrorText / sizeof kErrorText[0]))
		return kErrorText[error];
	return kErrorText[0];
}


std::string
CellName(CellID id)
{
	std::string s;
	if (id.b > 25)
		s += char('A' + (id.b - 26) / 26);
	s += char('A' + id.b % 26);
	return s + std::to_string(id.a + 1);
}


bool
SSValue::GetNum(double& out) const
{
	switch (type) {
		case kNumber:
		case kBool:
			out = number;
			return true;
		case kNone:
			out = 0;
			return true;
		case kTime:
			out = time / 86400.0;
			return true;
		case kString:
			return StringToNumber(text, out);
		default:
			return false;
	}
}


bool
SSValue::GetBool(bool& out) const
{
	if (type != kBool && type != kNumber)
		return false;
	out = number != 0;
	return true;
}


bool
SSValue::GetString(std::string& out) const
{
	switch (type) {
		case kString:
			out = text;
			return true;
		case kNone:
			out.clear();
			return true;
		case kNumber:
			out = GeneralNumber(number);
			return true;
		case kBool:
			out = number != 0 ? "TRUE" : "FALSE";
			return true;
		case kTime: {
			time_t t = time_t(time);
			struct tm tm;
			gmtime_r(&t, &tm);
			char buf[64];
			asctime_r(&tm, buf);
			out = buf;
			if (!out.empty() && out.back() == '\n')
				out.pop_back();
			return true;
		}
		default:
			return false;
	}
}


// #pragma mark - unparse

namespace {

const char* kOperators[] = {"<=", ">=", "<>", "<", ">", "=", "&", "+", "-",
	"*", "/", "^", "%", "-", "(", ")"};

std::string
RefText(const CellRef& r, CellID at)
{
	CellID id;
	if (!Resolve(r, at, id))
		return "BADCELL";
	std::string s;
	if (r.flags & 4)
		s += '$';
	std::string name = CellName(id);
	size_t digits = name.find_first_of("0123456789");
	s += name.substr(0, digits);
	if (r.flags & 2)
		s += '$';
	return s + name.substr(digits);
}


std::string
Unparse(const std::vector<FormulaToken>& t, size_t& i, CellID at)
{
	if (i >= t.size())
		return "";
	const FormulaToken& k = t[i++];
	if (k.kind < 0xc) {
		std::string left = Unparse(t, i, at);
		return left + kOperators[k.kind] + Unparse(t, i, at);
	}
	switch (k.kind) {
		case 0xc:
			return Unparse(t, i, at) + "%";
		case 0xd:
			return "-" + Unparse(t, i, at);
		case 0xe: {
			std::string s = "(" + Unparse(t, i, at);
			if (i < t.size() && t[i].kind == 0xf)
				i++;
			return s + ")";
		}
		case 0x11:
			return "\"" + (k.strings.empty() ? "" : k.strings[0]) + "\"";
		case 0x12:
			return k.value ? "TRUE" : "FALSE";
		case 0x13:
		case 0x14:
			return std::to_string(llround(k.number));
		case 0x15:
		case 0x16:
			return GeneralNumber(k.number);
		case 0x17:
			return ErrorText(k.value);
		case 0x18: {
			time_t tt = k.value;
			struct tm tm;
			gmtime_r(&tt, &tm);
			char buf[64];
			strftime(buf, sizeof buf, "'%m/%d/%y %H:%M'", &tm);
			return buf;
		}
		case 0x19:
			return RefText(k.refs[0], at);
		case 0x1a:
			return RefText(k.refs[0], at) + ":" + RefText(k.refs[1], at);
		case 0x1b:
			return (k.strings[0].empty() ? "" : k.strings[0] + ":") + k.strings[1];
		case 0x1c: {
			std::string name = k.strings.empty() ? "" : k.strings[0];
			for (char& c : name)
				c = toupper((unsigned char)c);
			std::string s = name + "(";
			for (uint32_t n = 0; n < k.value; n++)
				s += (n ? "," : "") + Unparse(t, i, at);
			return s + ")";
		}
		case 0x1e:
		case 0x1f: {
			std::string s = k.strings[0];
			if (!k.strings[1].empty())
				s += (s.empty() ? "" : ":") + k.strings[1];
			s += "!" + RefText(k.refs[0], at);
			if (k.kind == 0x1f)
				s += ":" + RefText(k.refs[1], at);
			return s;
		}
	}
	return "";
}

} // namespace


std::string
FormulaText(const std::vector<FormulaToken>& formula, CellID at)
{
	size_t i = 0;
	return Unparse(formula, i, at);
}


// #pragma mark - SSEvaluator

struct SSEvaluator::Iter {
	const std::vector<FormulaToken>& tokens;
	size_t pos;
	CellID at;

	const FormulaToken* Next()
	{
		return pos < tokens.size() ? &tokens[pos++] : nullptr;
	}
};


SSEvaluator::SSEvaluator(const SSPart& part, SSBook* book, int index)
	:
	fPart(part),
	fBook(book),
	fIndex(index)
{
	for (const SSCell& c : part.cells)
		fCells[Key(c.id.a, c.id.b)] = &c;
}


SSValue
SSEvaluator::Value(uint16_t row, uint16_t column)
{
	uint32_t key = Key(row, column);
	auto cached = fCache.find(key);
	if (cached != fCache.end())
		return cached->second;
	auto found = fCells.find(key);
	if (found == fCells.end())
		return SSValue();
	const SSCell& c = *found->second;
	SSValue v;
	switch (c.kind) {
		case Sig("Bool"):
			v = SSValue::Bool(c.number != 0);
			break;
		case Sig("Lng#"):
		case Sig("Flt#"):
		case Sig("Dbl#"):
			v = SSValue::Number(c.number);
			break;
		case Sig("Time"):
			v = SSValue::Time(c.value);
			break;
		case Sig("Strn"):
			v = SSValue::String(c.text);
			break;
		case Sig("Err#"):
		case Sig("FmEr"):
			v = SSValue::Error(c.value);
			break;
		case Sig("Fmla"): {
			if (fBusy[key])		// circular reference
				return SSValue::Error(kErrRef);
			fBusy[key] = true;
			Iter it{c.formula, 0, c.id};
			v = Eval(it, true);
			fBusy[key] = false;
			break;
		}
		default:
			break;
	}
	fCache[key] = v;
	return v;
}


void
SSEvaluator::EvaluateAll()
{
	// fCells is ordered by (row, column)
	for (const auto& [key, cell] : fCells)
		if (cell->kind == Sig("Fmla"))
			Value(cell->id.a, cell->id.b);
}


SSValue
SSEvaluator::CellValue(int part, int row, int column)
{
	if (part < 0 || part == fIndex || !fBook)
		return Value(row, column);
	SSEvaluator* other = fBook->For(part);
	if (!other)
		return SSValue::Error(kErrRef);
	return other->Value(row, column);
}


SSBook::SSBook(const Document& doc)
	:
	fDoc(doc)
{
}


SSBook::~SSBook()
{
	for (auto& [part, e] : fEvaluators)
		delete e;
}


void
SSBook::Reset()
{
	for (auto& [part, e] : fEvaluators)
		delete e;
	fEvaluators.clear();
}


SSEvaluator*
SSBook::For(size_t part)
{
	auto found = fEvaluators.find(part);
	if (found != fEvaluators.end())
		return found->second;
	const SSPart* ss = fDoc.SpreadsheetPart(part);
	if (!ss)
		return nullptr;
	SSEvaluator* e = new SSEvaluator(*ss, this, int(part));
	fEvaluators[part] = e;
	e->EvaluateAll();
	return e;
}


bool
SSBook::ResolveName(const std::string& name, int fromPart, int& part, SSRange& range) const
{
	const Selection* s = fDoc.NamedSelection(name, fromPart);
	if (!s || s->kind != Sig("SPRD") || s->ranges.empty())
		return false;
	part = int(s->part);
	const auto& r = s->ranges[0];
	range.from = {r[0], r[1]};
	range.to = {r[2], r[3]};
	return true;
}


// TEvalResult::Reduce: a single-cell range, or the cell of a one-column
// (one-row) range in the formula's row (column), becomes that cell's value
SSValue
SSEvaluator::Reduce(const SSValue& v, CellID at)
{
	if (v.type != SSValue::kRange)
		return v;
	const CellID& from = v.range.from;
	const CellID& to = v.range.to;
	if (from.a == to.a && from.b == to.b)
		return CellValue(v.part, from.a, from.b);
	if (from.b == to.b && at.a >= from.a && at.a <= to.a)
		return CellValue(v.part, at.a, from.b);
	if (from.a == to.a && at.b >= from.b && at.b <= to.b)
		return CellValue(v.part, from.a, at.b);
	return SSValue::Error(kErrRef);
}


// TEvaluator::Skip: step over one operand
void
SSEvaluator::Skip(Iter& it)
{
	const FormulaToken* t = it.Next();
	if (!t)
		return;
	while (t && (t->kind == 0xe || t->kind == 0xf)) {
		t = it.Next();
		if (!t)
			return;
	}
	if (t->kind < 0xc) {
		Skip(it);
		Skip(it);
	} else if (t->kind == 0xc || t->kind == 0xd) {
		Skip(it);
	} else if (t->kind == 0x1c) {
		for (uint32_t i = 0; i < t->value; i++)
			Skip(it);
	}
}


// TEvaluator::DoEval
SSValue
SSEvaluator::Eval(Iter& it, bool reduce)
{
	const FormulaToken* t = it.Next();
	while (t && (t->kind == 0xe || t->kind == 0xf))
		t = it.Next();
	if (!t)
		return SSValue::Error(kErrFormula);
	if (t->kind < 0xe)
		return Operator(it, t->kind);
	switch (t->kind) {
		case 0x1c: {
			std::string name = t->strings.empty() ? "" : t->strings[0];
			for (char& c : name)
				c = toupper((unsigned char)c);
			return Function(it, name, int(t->value));
		}
		case 0x11:
			return SSValue::String(t->strings.empty() ? "" : t->strings[0]);
		case 0x12:
			return SSValue::Bool(t->value != 0);
		case 0x13:
		case 0x14:
		case 0x15:
		case 0x16:
			return SSValue::Number(t->number);
		case 0x18:
			return SSValue::Time(t->value);
		case 0x19: {
			CellID id;
			if (!Resolve(t->refs[0], it.at, id))
				return SSValue::Error(kErrRef);
			if (!reduce) {
				SSValue v;
				v.type = SSValue::kRange;
				v.range = {id, id};
				return v;
			}
			return Value(id.a, id.b);
		}
		case 0x1a: {
			CellID from, to;
			if (!Resolve(t->refs[0], it.at, from) || !Resolve(t->refs[1], it.at, to))
				return SSValue::Error(kErrRef);
			SSValue v;
			v.type = SSValue::kRange;
			v.range = {from, to};
			return reduce ? Reduce(v, it.at) : v;
		}
		case 0x1b: {	// a name: TDoc::FindLink
			SSValue v;
			v.type = SSValue::kRange;
			if (!fBook || t->strings.size() < 2
				|| !fBook->ResolveName(t->strings[1], fIndex, v.part, v.range))
				return SSValue::Error(kErrName);
			if (v.part == fIndex)
				v.part = -1;
			return reduce ? Reduce(v, it.at) : v;
		}
		case 0x17:	// an error constant
			return SSValue::Error(int(t->value));
		default:
			// sheet references (0x1e, 0x1f) aren't resolved yet
			return SSValue::Error(kErrRef);
	}
}


// TEvaluator::DoEvalOperator
SSValue
SSEvaluator::Operator(Iter& it, int op)
{
	SSValue left = Eval(it, true);
	SSValue right;
	if (op != 0xc && op != 0xd)
		right = Eval(it, true);
	if (left.type == SSValue::kError)
		return left;
	if (op != 0xc && op != 0xd && right.type == SSValue::kError && op >= 6)
		return right;

	if (op < 6) {
		if (left.type != right.type) {
			double a, b;
			std::string sa, sb;
			if (left.GetNum(a) && right.GetNum(b)) {
				left = SSValue::Number(a);
				right = SSValue::Number(b);
			} else if (left.GetString(sa) && right.GetString(sb)) {
				left = SSValue::String(sa);
				right = SSValue::String(sb);
			} else
				return SSValue::Error(kErrValue);
		}
		int cmp = 0;
		switch (left.type) {
			case SSValue::kNumber:
			case SSValue::kBool:
				cmp = left.number < right.number ? -1 : left.number > right.number;
				break;
			case SSValue::kString: {
				int c = strcmp(left.text.c_str(), right.text.c_str());
				cmp = c < 0 ? -1 : c > 0;
				break;
			}
			case SSValue::kTime:
				cmp = left.time < right.time ? -1 : left.time > right.time;
				break;
			case SSValue::kError:
				cmp = left.error < right.error ? -1 : left.error > right.error;
				break;
			default:
				break;
		}
		static const std::function<bool(int)> kCompare[] = {
			[](int c) { return c != 1; }, [](int c) { return c != -1; },
			[](int c) { return c != 0; }, [](int c) { return c == -1; },
			[](int c) { return c == 1; }, [](int c) { return c == 0; }};
		return SSValue::Bool(kCompare[op](cmp));
	}

	if (op == 6) {
		std::string a, b;
		if (!left.GetString(a) || !right.GetString(b))
			return SSValue::Error(kErrValue);
		return SSValue::String(a + b);
	}

	double a, b = 0;
	if (!left.GetNum(a))
		return SSValue::Error(kErrValue);
	if (op != 0xc && op != 0xd && !right.GetNum(b))
		return SSValue::Error(kErrValue);
	switch (op) {
		case 7: return SSValue::Number(a + b);
		case 8: return SSValue::Number(a - b);
		case 9: return SSValue::Number(a * b);
		case 10:
			if (b == 0)
				return SSValue::Error(kErrDiv0);
			return SSValue::Number(a / b);
		case 11: return SSValue::Number(pow(a, b));
		case 12: return SSValue::Number(a / 100.0);
		case 13: return SSValue::Number(-a);
	}
	return SSValue::Error(kErrError);
}


// TFunctionMgr::EvaluateFunction: the functions used by the sample
// documents and their relatives
SSValue
SSEvaluator::Function(Iter& it, const std::string& name, int argc)
{
	int used = 0;
	auto next = [&](bool reduce = true) {
		if (used >= argc)
			return SSValue::Error(kErrArg);
		used++;
		return Eval(it, reduce);
	};
	auto skip = [&]() {
		if (used < argc) {
			used++;
			Skip(it);
		}
	};
	auto finish = [&](SSValue v) {
		while (used < argc)
			skip();
		return v;
	};
	// iterate all values of all arguments, ranges expanded
	// (TIterFunction::Begin/Eval/End); f returns false to stop
	auto each = [&](const std::function<bool(const SSValue&)>& f) {
		while (used < argc) {
			SSValue v = next(false);
			if (v.type == SSValue::kRange) {
				const SSRange& r = v.range;
				int r0 = std::min(r.from.a, r.to.a), r1 = std::max(r.from.a, r.to.a);
				int c0 = std::min(r.from.b, r.to.b), c1 = std::max(r.from.b, r.to.b);
				for (int row = r0; row <= r1; row++)
					for (int col = c0; col <= c1; col++)
						if (!f(CellValue(v.part, row, col)))
							return;
			} else if (!f(v))
				return;
		}
	};
	auto numArg = [&](double& out) {
		SSValue v = next();
		return v.GetNum(out);
	};

	if (name == "SUM" || name == "AVERAGE" || name == "MIN" || name == "MAX"
		|| name == "COUNT") {
		double sum = 0, best = 0;
		int count = 0;
		SSValue error;
		each([&](const SSValue& v) {
			if (v.type == SSValue::kError) {
				error = v;
				return false;
			}
			if (v.type == SSValue::kNone || v.type == SSValue::kString)
				return true;
			double n;
			if (!v.GetNum(n))
				return true;
			if (count == 0 || (name == "MIN" ? n < best : n > best))
				best = n;
			sum += n;
			count++;
			return true;
		});
		if (error.type == SSValue::kError)
			return finish(error);
		if (name == "SUM")
			return finish(SSValue::Number(sum));
		if (name == "COUNT")
			return finish(SSValue::Number(count));
		if (name == "AVERAGE")
			return finish(count ? SSValue::Number(sum / count) : SSValue::Error(kErrDiv0));
		return finish(SSValue::Number(count ? best : 0));
	}
	if (name == "AND" || name == "OR") {
		bool isAnd = name == "AND";
		bool result = isAnd;
		SSValue error;
		each([&](const SSValue& v) {
			if (v.type == SSValue::kNone)
				return true;
			bool b;
			if (!v.GetBool(b)) {
				if (v.type == SSValue::kError) {
					error = v;
					return false;
				}
				return true;
			}
			if (b != isAnd) {
				result = b;
				return false;
			}
			return true;
		});
		if (error.type == SSValue::kError)
			return finish(error);
		return finish(SSValue::Bool(result));
	}
	if (name == "ISBLANK") {
		bool blank = true;
		each([&](const SSValue& v) {
			if (v.type != SSValue::kNone) {
				blank = false;
				return false;
			}
			return true;
		});
		return finish(SSValue::Bool(blank));
	}
	if (name == "CONCAT") {
		std::string s;
		bool ok = true;
		each([&](const SSValue& v) {
			std::string part;
			if (!v.GetString(part)) {
				ok = false;
				return false;
			}
			s += part;
			return true;
		});
		return finish(ok ? SSValue::String(s) : SSValue::Error(kErrValue));
	}
	if (name == "NOT") {
		SSValue v = next();
		bool b;
		if (!v.GetBool(b))
			return finish(v.type == SSValue::kError ? v : SSValue::Error(kErrValue));
		return finish(SSValue::Bool(!b));
	}
	if (name == "IF") {
		SSValue c = next();
		bool b;
		if (!c.GetBool(b))
			return finish(c.type == SSValue::kError ? c : SSValue::Error(kErrValue));
		if (!b)
			skip();
		if (used < argc) {
			SSValue v = next();
			return finish(v);
		}
		return finish(SSValue::Bool(false));
	}
	if (name == "CHOOSE") {
		double n;
		if (!numArg(n))
			return finish(SSValue::Error(kErrValue));
		int index = int(floor(n));
		if (index < 1 || index > argc - 1)
			return finish(SSValue::Error(kErrArg));
		for (int i = 1; i < index; i++)
			skip();
		return finish(next());
	}
	if (name == "ROUND") {
		double n, digits;
		if (!numArg(n) || !numArg(digits) || digits != floor(digits))
			return finish(SSValue::Error(kErrValue));
		double r;
		if (digits > 0) {
			double scale = pow(10.0, digits);
			r = floor(scale * n + 0.5) / scale;
		} else {
			double scale = pow(10.0, -ceil(digits));
			r = floor(n / scale + 0.5) * scale;
		}
		return finish(SSValue::Number(r));
	}
	if (name == "MOD") {
		double a, b;
		if (!numArg(a) || !numArg(b))
			return finish(SSValue::Error(kErrValue));
		if (b == 0)
			return finish(SSValue::Error(kErrDiv0));
		return finish(SSValue::Number(a - b * floor(a / b)));
	}
	if (name == "ABS" || name == "INT" || name == "SQRT") {
		double a;
		if (!numArg(a))
			return finish(SSValue::Error(kErrValue));
		if (name == "ABS")
			return finish(SSValue::Number(fabs(a)));
		if (name == "INT")
			return finish(SSValue::Number(floor(a)));
		if (a < 0)
			return finish(SSValue::Error(kErrNum));
		return finish(SSValue::Number(sqrt(a)));
	}
	if (name == "DATE") {
		double y, m, d;
		if (!numArg(y) || !numArg(m) || !numArg(d))
			return finish(SSValue::Error(kErrValue));
		return finish(SSValue::Time(DateToSeconds(llround(y), llround(m), llround(d))));
	}
	if (name == "YEAR" || name == "MONTH" || name == "DAY" || name == "WEEKDAY"
		|| name == "HOUR" || name == "MINUTE") {
		double n;
		if (!numArg(n))
			return finish(SSValue::Error(kErrValue));
		struct tm tm = DaysToTm(n);
		int r = name == "YEAR" ? tm.tm_year + 1900 : name == "MONTH" ? tm.tm_mon + 1
			: name == "DAY" ? tm.tm_mday : name == "WEEKDAY" ? tm.tm_wday
			: name == "HOUR" ? tm.tm_hour : tm.tm_min;
		return finish(SSValue::Number(r));
	}
	if (name == "MONTHNAME") {
		double n;
		if (!numArg(n) || n < 1 || n > 12)
			return finish(SSValue::Error(kErrDate));
		bool shortName = false;
		if (used < argc) {
			SSValue v = next();
			if (!v.GetBool(shortName))
				return finish(SSValue::Error(kErrValue));
		}
		struct tm tm = {};
		tm.tm_mon = int(llround(n)) - 1;
		char buf[32];
		strftime(buf, sizeof buf, shortName ? "%b" : "%B", &tm);
		return finish(SSValue::String(buf));
	}
	if (name == "NOW")
		return finish(SSValue::Time(int64_t(time(nullptr))));
	if (name == "RAND")
		return finish(SSValue::Number(rand() / (RAND_MAX + 1.0)));
	if (name == "REPT") {
		SSValue s = next();
		double n;
		std::string text;
		if (!s.GetString(text) || !numArg(n) || n > 1000)
			return finish(SSValue::Error(kErrValue));
		std::string r;
		for (int i = 0; i < int(llround(n)); i++)
			r += text;
		return finish(SSValue::String(r));
	}
	if (name == "PMT" || name == "PV") {
		double rate, periods, x, fv = 0, type = 0;
		if (!numArg(rate) || !numArg(periods) || !numArg(x))
			return finish(SSValue::Error(kErrValue));
		if (used < argc && !numArg(fv))
			return finish(SSValue::Error(kErrValue));
		if (used < argc && !numArg(type))
			return finish(SSValue::Error(kErrValue));
		if (name == "PMT") {
			double pv = -x;
			if (rate == 0)
				return finish(SSValue::Number((pv - fv) / periods));
			double f = pow(rate + 1.0, periods);
			return finish(SSValue::Number((f * pv - fv) * rate / (f - 1.0)));
		}
		double f = pow(rate + 1.0, periods);
		return finish(SSValue::Number(((f - 1.0) / rate * -x * (rate * type + 1.0) - fv) / f));
	}
	if (name == "VLOOKUP") {
		SSValue key = next();
		SSValue table = next(false);
		double column, mode = 1;
		if (table.type != SSValue::kRange || !numArg(column))
			return finish(SSValue::Error(kErrValue));
		const SSRange& r = table.range;
		int col = int(floor(column));
		if (col < 1 || col > r.to.b - r.from.b + 1)
			return finish(SSValue::Error(kErrRef));
		if (used < argc)
			numArg(mode);
		// the last row whose first column is <= key (mode > 0), or equal (0)
		int found = -1;
		double kn;
		bool numeric = key.GetNum(kn) && key.type != SSValue::kString;
		for (int row = r.from.a; row <= r.to.a; row++) {
			SSValue v = CellValue(table.part, row, r.from.b);
			int cmp;
			double vn;
			if (numeric && v.type != SSValue::kString && v.type != SSValue::kNone
				&& v.GetNum(vn))
				cmp = vn < kn ? -1 : vn > kn;
			else {
				std::string a, b;
				if (!v.GetString(a) || !key.GetString(b))
					continue;
				int c = strcasecmp(a.c_str(), b.c_str());
				cmp = c < 0 ? -1 : c > 0;
			}
			if (cmp == 0) {
				found = row;
				break;
			}
			if (mode > 0 && cmp < 0)
				found = row;
			else if (mode > 0 && cmp > 0)
				break;
		}
		if (found < 0)
			return finish(SSValue::Error(kErrNA));
		// TVLookupFunction: the column is counted from the key column
		// (1 = the column right of it)
		return finish(CellValue(table.part, found, r.from.b + col));
	}
	if (name == "ALERT") {
		// shows an alert when the condition is true (Functions/System
		// add-on); the cell shows the condition
		SSValue c = next();
		return finish(c);
	}
	return finish(SSValue::Error(kErrName));
}

} // namespace gobe
