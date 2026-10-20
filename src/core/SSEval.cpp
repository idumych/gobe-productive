#include "SSEval.h"

#include "Editing.h"
#include "SSStyle.h"

#include <algorithm>
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


// GetTimezoneOffset: returns the offset from UTC in seconds.
// Positive for timezones east of UTC.
int
GetTimezoneOffset()
{
	time_t now = time(nullptr);
	struct tm local;
	localtime_r(&now, &local);
	return int(local.tm_gmtoff);
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
	if (name == "TRUNC") {
		double a;
		if (!numArg(a))
			return finish(SSValue::Error(kErrValue));
		return finish(SSValue::Number(trunc(a)));
	}
	if (name == "SIGN") {
		double a;
		if (!numArg(a))
			return finish(SSValue::Error(kErrValue));
		if (a > 0)
			return finish(SSValue::Number(1.0));
		if (a < 0)
			return finish(SSValue::Number(-1.0));
		return finish(SSValue::Number(0.0));
	}
	if (name == "QUOTIENT") {
		double a, b;
		if (!numArg(a) || !numArg(b))
			return finish(SSValue::Error(kErrValue));
		if (b == 0)
			return finish(SSValue::Error(kErrDiv0));
		return finish(SSValue::Number(trunc(a / b)));
	}
	if (name == "LOG10") {
		double a;
		if (!numArg(a))
			return finish(SSValue::Error(kErrValue));
		if (a <= 0)
			return finish(SSValue::Error(kErrNum));
		return finish(SSValue::Number(log10(a)));
	}
	if (name == "LOG") {
		double val, base;
		if (!numArg(val))
			return finish(SSValue::Error(kErrValue));
		base = 10.0;
		if (used < argc) {
			if (!numArg(base))
				return finish(SSValue::Error(kErrValue));
		}
		if (val <= 0 || base <= 0)
			return finish(SSValue::Error(kErrNum));
		return finish(SSValue::Number(log(val) / log(base)));
	}
	if (name == "LN") {
		double a;
		if (!numArg(a))
			return finish(SSValue::Error(kErrValue));
		if (a <= 0)
			return finish(SSValue::Error(kErrNum));
		return finish(SSValue::Number(log(a)));
	}
	if (name == "FRAC") {
		double a;
		if (!numArg(a))
			return finish(SSValue::Error(kErrValue));
		return finish(SSValue::Number(a - trunc(a)));
	}
	if (name == "FACT") {
		double a;
		if (!numArg(a))
			return finish(SSValue::Error(kErrValue));
		if (a < 0 || a != trunc(a))
			return finish(SSValue::Error(kErrNum));
		int n = (int)a;
		double r = 1.0;
		for (int i = 2; i <= n; i++)
			r *= i;
		return finish(SSValue::Number(r));
	}
	if (name == "EXP") {
		double a;
		if (!numArg(a))
			return finish(SSValue::Error(kErrValue));
		return finish(SSValue::Number(exp(a)));
	}
	if (name == "COMBIN") {
		double n, k;
		if (!numArg(n) || !numArg(k))
			return finish(SSValue::Error(kErrValue));
		n = trunc(n);
		k = trunc(k);
		if (n < 0 || k < 0 || n < k)
			return finish(SSValue::Error(kErrNum));
		if (k == 0 || n == k)
			return finish(SSValue::Number(1.0));
		if (k > n - k)
			k = n - k;
		double r = 1.0;
		for (int i = 0; i < (int)k; i++) {
			r *= (n - i);
			r /= (i + 1);
		}
		return finish(SSValue::Number(r));
	}
	if (name == "ATAN2") {
		double x, y;
		if (!numArg(x) || !numArg(y))
			return finish(SSValue::Error(kErrValue));
		return finish(SSValue::Number(atan2(y, x)));
	}
	if (name == "RADIANS") {
		double a;
		if (!numArg(a))
			return finish(SSValue::Error(kErrValue));
		return finish(SSValue::Number(a * M_PI / 180.0));
	}
	if (name == "DEGREES") {
		double a;
		if (!numArg(a))
			return finish(SSValue::Error(kErrValue));
		return finish(SSValue::Number(a * 180.0 / M_PI));
	}
	if (name == "PI") {
		return finish(SSValue::Number(M_PI));
	}
	if (name == "ATANH") {
		double a;
		if (!numArg(a))
			return finish(SSValue::Error(kErrValue));
		if (fabs(a) >= 1.0)
			return finish(SSValue::Error(kErrNum));
		return finish(SSValue::Number(0.5 * log((1.0 + a) / (1.0 - a))));
	}
	if (name == "ASINH") {
		double a;
		if (!numArg(a))
			return finish(SSValue::Error(kErrValue));
		return finish(SSValue::Number(log(a + sqrt(a * a + 1.0))));
	}
	if (name == "ACOSH") {
		double a;
		if (!numArg(a))
			return finish(SSValue::Error(kErrValue));
		if (a < 1.0)
			return finish(SSValue::Error(kErrNum));
		return finish(SSValue::Number(log(a + sqrt(a * a - 1.0))));
	}
	if (name == "TANH") {
		double a;
		if (!numArg(a))
			return finish(SSValue::Error(kErrValue));
		return finish(SSValue::Number(tanh(a)));
	}
	if (name == "SINH") {
		double a;
		if (!numArg(a))
			return finish(SSValue::Error(kErrValue));
		return finish(SSValue::Number(sinh(a)));
	}
	if (name == "COSH") {
		double a;
		if (!numArg(a))
			return finish(SSValue::Error(kErrValue));
		return finish(SSValue::Number(cosh(a)));
	}
	if (name == "ATAN") {
		double a;
		if (!numArg(a))
			return finish(SSValue::Error(kErrValue));
		return finish(SSValue::Number(atan(a)));
	}
	if (name == "ASIN") {
		double a;
		if (!numArg(a))
			return finish(SSValue::Error(kErrValue));
		if (a < -1.0 || a > 1.0)
			return finish(SSValue::Error(kErrNum));
		return finish(SSValue::Number(asin(a)));
	}
	if (name == "ACOS") {
		double a;
		if (!numArg(a))
			return finish(SSValue::Error(kErrValue));
		if (a < -1.0 || a > 1.0)
			return finish(SSValue::Error(kErrNum));
		return finish(SSValue::Number(acos(a)));
	}
	if (name == "TAN") {
		double a;
		if (!numArg(a))
			return finish(SSValue::Error(kErrValue));
		return finish(SSValue::Number(tan(a)));
	}
	if (name == "SIN") {
		double a;
		if (!numArg(a))
			return finish(SSValue::Error(kErrValue));
		return finish(SSValue::Number(sin(a)));
	}
	if (name == "COS") {
		double a;
		if (!numArg(a))
			return finish(SSValue::Error(kErrValue));
		return finish(SSValue::Number(cos(a)));
	}
	if (name == "PRODUCT") {
		double prod = 1.0;
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
			prod *= n;
			return true;
		});
		if (error.type == SSValue::kError)
			return finish(error);
		return finish(SSValue::Number(prod));
	}
	if (name == "STDEV") {
		// sample standard deviation
		double sum = 0, sum2 = 0;
		int count = 0;
		bool textSeen = false;
		SSValue error;
		each([&](const SSValue& v) {
			if (v.type == SSValue::kError) {
				error = v;
				return false;
			}
			if (v.type == SSValue::kNone)
				return true;
			if (v.type == SSValue::kString) {
				textSeen = true;
				return false;
			}
			double n;
			if (!v.GetNum(n))
				return true;
			sum += n;
			sum2 += n * n;
			count++;
			return true;
		});
		if (error.type == SSValue::kError)
			return finish(error);
		if (textSeen)
			return finish(SSValue::Error(kErrValue));
		if (count < 2)
			return finish(SSValue::Error(kErrDiv0));
		double variance = sum2 / (count - 1) - (sum * sum) / ((count - 1) * count);
		if (variance < 0)
			variance = 0;
		return finish(SSValue::Number(sqrt(variance)));
	}
	if (name == "VAR") {
		// sample variance
		double sum = 0, sum2 = 0;
		int count = 0;
		bool textSeen = false;
		SSValue error;
		each([&](const SSValue& v) {
			if (v.type == SSValue::kError) {
				error = v;
				return false;
			}
			if (v.type == SSValue::kNone)
				return true;
			if (v.type == SSValue::kString) {
				textSeen = true;
				return false;
			}
			double n;
			if (!v.GetNum(n))
				return true;
			sum += n;
			sum2 += n * n;
			count++;
			return true;
		});
		if (error.type == SSValue::kError)
			return finish(error);
		if (textSeen)
			return finish(SSValue::Error(kErrValue));
		if (count < 2)
			return finish(SSValue::Error(kErrDiv0));
		return finish(SSValue::Number(sum2 / (count - 1) - (sum * sum) / ((count - 1) * count)));
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
	if (name == "TIME") {
		double h, m, s;
		if (!numArg(h) || !numArg(m) || !numArg(s))
			return finish(SSValue::Error(kErrValue));
		return finish(SSValue::Time(
			llround(floor(h) * 3600 + floor(m) * 60 + floor(s))));
	}
	if (name == "SECOND") {
		double n;
		if (!numArg(n))
			return finish(SSValue::Error(kErrValue));
		struct tm tm = DaysToTm(n);
		return finish(SSValue::Number(tm.tm_sec));
	}
	if (name == "DAYOFYEAR") {
		double n;
		if (!numArg(n))
			return finish(SSValue::Error(kErrValue));
		struct tm tm = DaysToTm(n);
		return finish(SSValue::Number(tm.tm_yday + 1));
	}
	if (name == "DAYOFWEEK") {
		double n;
		if (!numArg(n))
			return finish(SSValue::Error(kErrValue));
		struct tm tm = DaysToTm(n);
		return finish(SSValue::Number(tm.tm_wday));
	}
	if (name == "DAYNAME") {
		double dn;
		if (!numArg(dn) || dn < 0 || dn > 6)
			return finish(SSValue::Error(kErrDate));
		bool shortName = false;
		if (used < argc) {
			SSValue v = next();
			if (!v.GetBool(shortName))
				return finish(SSValue::Error(kErrValue));
		}
		static const char* kDays[] = {
			"Sunday", "Monday", "Tuesday", "Wednesday",
			"Thursday", "Friday", "Saturday"};
		std::string day = kDays[int(llround(dn))];
		if (shortName && day.size() > 3)
			day = day.substr(0, 3);
		return finish(SSValue::String(day));
	}
	if (name == "WEEKOFYEAR") {
		double n;
		if (!numArg(n))
			return finish(SSValue::Error(kErrValue));
		struct tm tm = DaysToTm(n);
		// Sunday-based week numbering (like strftime %U).
		// First Sunday starts week 1; days before are week 0.
		int week = (tm.tm_yday + 6 - tm.tm_wday) / 7;
		return finish(SSValue::Number(week));
	}
	if (name == "TIMEZONE") {
		int offset = GetTimezoneOffset();
		return finish(SSValue::Number(offset));
	}
	if (name == "LOCALTIME") {
		int64_t utc_seconds;
		if (used < argc) {
			double n;
			if (!numArg(n))
				return finish(SSValue::Error(kErrValue));
			utc_seconds = llround(n * 86400.0);
		} else {
			utc_seconds = int64_t(time(nullptr));
		}
		int offset = 0;
		if (used < argc) {
			double od;
			if (!numArg(od))
				return finish(SSValue::Error(kErrValue));
			offset = int(llround(od));
		} else {
			offset = GetTimezoneOffset();
		}
		return finish(SSValue::Time(utc_seconds + offset));
	}
	if (name == "DATETOTEXT") {
		double n;
		if (!numArg(n))
			return finish(SSValue::Error(kErrValue));
		std::string fmt = "m/d/yy";
		if (used < argc) {
			SSValue v = next();
			std::string s;
			if (!v.GetString(s))
				return finish(SSValue::Error(kErrValue));
			fmt = s;
		}
		int64_t seconds = llround(n * 86400.0);
		SSValue dateVal = SSValue::Time(seconds);
		return finish(SSValue::String(FormatCellValue(dateVal, fmt)));
	}
	if (name == "TIMETOTEXT") {
		double n;
		if (!numArg(n))
			return finish(SSValue::Error(kErrValue));
		std::string fmt = "h:mm:ss";
		if (used < argc) {
			SSValue v = next();
			std::string s;
			if (!v.GetString(s))
				return finish(SSValue::Error(kErrValue));
			fmt = s;
		}
		int64_t seconds = llround(n * 86400.0);
		int64_t tod = seconds % 86400;
		if (tod < 0)
			tod += 86400;
		SSValue timeVal = SSValue::Time(tod);
		return finish(SSValue::String(FormatCellValue(timeVal, fmt)));
	}
	if (name == "TEXTTOTIME") {
		SSValue v = next();
		std::string s;
		if (!v.GetString(s))
			return finish(SSValue::Error(kErrValue));
		// Trim whitespace
		size_t start = 0, end = s.size();
		while (start < end && isspace((unsigned char)s[start])) start++;
		while (end > start && isspace((unsigned char)s[end - 1])) end--;
		std::string t = s.substr(start, end - start);
		if (t.empty())
			return finish(SSValue::Error(kErrValue));
		int hours = 0, minutes = 0, seconds = 0;
		bool isPM = false;
		if (t.size() >= 2) {
			char a = tolower((unsigned char)t[t.size() - 2]);
			char b = tolower((unsigned char)t[t.size() - 1]);
			if (a == 'a' && b == 'm') {
				isPM = false;
				t = t.substr(0, t.size() - 2);
			} else if (a == 'p' && b == 'm') {
				isPM = true;
				t = t.substr(0, t.size() - 2);
			}
		}
		while (!t.empty() && isspace((unsigned char)t.back()))
			t.pop_back();
		if (t.empty())
			return finish(SSValue::Error(kErrValue));
		int parsed;
		size_t p2 = t.find(':');
		if (p2 == std::string::npos)
			return finish(SSValue::Error(kErrValue));
		size_t p3 = t.find(':', p2 + 1);
		if (p3 != std::string::npos) {
			parsed = sscanf(t.c_str(), "%d:%d:%d", &hours, &minutes, &seconds);
			if (parsed != 3)
				return finish(SSValue::Error(kErrValue));
		} else {
			parsed = sscanf(t.c_str(), "%d:%d", &hours, &minutes);
			if (parsed != 2)
				return finish(SSValue::Error(kErrValue));
			seconds = 0;
		}
		if (isPM && hours != 12)
			hours += 12;
		if (!isPM && hours == 12)
			hours = 0;
		if (hours < 0 || hours > 23 || minutes < 0 || minutes > 59
				|| seconds < 0 || seconds > 59)
			return finish(SSValue::Error(kErrValue));
		return finish(SSValue::Time(
			int64_t(hours * 3600 + minutes * 60 + seconds)));
	}
	if (name == "TEXTTODATE") {
		SSValue v = next();
		std::string s;
		if (!v.GetString(s))
			return finish(SSValue::Error(kErrValue));
		size_t start = 0, end = s.size();
		while (start < end && isspace((unsigned char)s[start])) start++;
		while (end > start && isspace((unsigned char)s[end - 1])) end--;
		std::string t = s.substr(start, end - start);
		int year = 0, month = 0, day = 0;
		int parsed;
		// Try ISO format: YYYY-MM-DD
		parsed = sscanf(t.c_str(), "%d-%d-%d", &year, &month, &day);
		if (parsed == 3 && month >= 1 && month <= 12
				&& day >= 1 && day <= 31) {
			if (year < 100)
				year += 1900;
			return finish(SSValue::Time(DateToSeconds(year, month, day)));
		}
		// Try US format: MM/DD/YYYY or M/D/YY
		parsed = sscanf(t.c_str(), "%d/%d/%d", &month, &day, &year);
		if (parsed == 3 && month >= 1 && month <= 12
				&& day >= 1 && day <= 31) {
			if (year < 100)
				year += 1900;
			return finish(SSValue::Time(DateToSeconds(year, month, day)));
		}
		return finish(SSValue::Error(kErrValue));
	}
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

	// --- batch 2: text functions ---

	// UTF-8 helpers: count chars, find char by index, left/right/mid,
	// replace at char positions, find char by index (case-sensitive),
	// find char by index (case-insensitive)
	std::function<int(const std::string&)> utf8CharLen =
		[](const std::string& s) -> int {
		int n = 0;
		size_t i = 0;
		while (i < s.size()) {
			n++;
			i = NextCharacter(s, i);
		}
		return n;
	};
	std::function<std::string(const std::string&, int)> utf8Left =
		[](const std::string& s, int n) -> std::string {
		if (n <= 0) return "";
		std::string out;
		size_t i = 0, count = 0;
		while (i < s.size() && count < size_t(n)) {
			i = NextCharacter(s, i);
			count++;
		}
		return s.substr(0, i);
	};
	std::function<std::string(const std::string&, int)> utf8Right =
		[&](const std::string& s, int n) -> std::string {
		if (n <= 0) return "";
		int total = utf8CharLen(s);
		if (n >= total) return s;
		int skip = total - n;
		size_t i = 0;
		for (int c = 0; c < skip; c++)
			i = NextCharacter(s, i);
		return s.substr(i);
	};
	std::function<std::string(const std::string&, int, int)> utf8Mid =
		[&](const std::string& s, int start, int n) -> std::string {
		if (start < 1 || n < 0) return "";
		size_t i = 0;
		for (int c = 1; c < start && i < s.size(); c++)
			i = NextCharacter(s, i);
		if (i >= s.size() && start > 1) return "";
		if (n <= 0) return "";
		size_t end = i;
		for (int c = 0; c < n && end < s.size(); c++)
			end = NextCharacter(s, end);
		return s.substr(i, end - i);
	};
	std::function<void(std::string&, int, int, const std::string&)> utf8Replace =
		[&](std::string& s, int start, int n, const std::string& repl) {
		if (start < 1) return;
		size_t byteStart = 0;
		for (int c = 1; c < start && byteStart < s.size(); c++)
			byteStart = NextCharacter(s, byteStart);
		size_t byteEnd = byteStart;
		for (int c = 0; c < n && byteEnd < s.size(); c++)
			byteEnd = NextCharacter(s, byteEnd);
		s.replace(byteStart, byteEnd - byteStart, repl);
	};
	std::function<int(const std::string&, const std::string&, int)> utf8Find =
		[&](const std::string& source, const std::string& target,
		int start) -> int {
		if (start < 1 || target.empty()) return 0;
		size_t pos = 0;
		int charPos = 1;
		for (int c = 1; c < start && pos < source.size(); c++) {
			pos = NextCharacter(source, pos);
			charPos++;
		}
		while (pos < source.size()) {
			size_t nextPos = NextCharacter(source, pos);
			if (source.substr(pos, nextPos - pos) == target)
				return charPos;
			pos = nextPos;
			charPos++;
		}
		return 0;
	};
	std::function<int(const std::string&, const std::string&, int)> utf8FindNoCase =
		[&](const std::string& source, const std::string& target,
		int start) -> int {
		if (start < 1) return 0;
		size_t pos = 0;
		int charPos = 1;
		for (int c = 1; c < start && pos < source.size(); c++) {
			pos = NextCharacter(source, pos);
			charPos++;
		}
		std::string tgtUp = target;
		for (char& c : tgtUp)
			c = toupper((unsigned char)c);
		while (pos < source.size()) {
			size_t nextPos = NextCharacter(source, pos);
			std::string chunk = source.substr(pos, nextPos - pos);
			std::string chunkUp = chunk;
			for (char& c : chunkUp)
				c = toupper((unsigned char)c);
			if (chunkUp == tgtUp)
				return charPos;
			pos = nextPos;
			charPos++;
		}
		return 0;
	};
	std::function<int(const std::string&, const std::string&, int&)> utf8FindAll =
		[&](const std::string& source, const std::string& target,
		int& start) -> int {
		if (target.empty()) return 0;
		int pos = -1;
		for (int s = start; s < utf8CharLen(source); s++) {
			std::string sub = utf8Mid(source, s + 1, utf8CharLen(target));
			if (sub == target) {
				pos = s;
				start = s + 1;
				break;
			}
		}
		return pos;
	};

	if (name == "VALUE") {
		double n;
		SSValue v = next();
		if (!v.GetNum(n))
			return finish(SSValue::Error(kErrValue));
		return finish(SSValue::Number(n));
	}
	// UTF-8-aware case conversion helpers
	auto encode_utf8 = [](uint32_t cp, std::string& out) {
		if (cp < 0x80) {
			out += static_cast<char>(cp);
		} else if (cp < 0x800) {
			out += static_cast<char>(0xC0 | (cp >> 6));
			out += static_cast<char>(0x80 | (cp & 0x3F));
		} else if (cp < 0x10000) {
			out += static_cast<char>(0xE0 | (cp >> 12));
			out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
			out += static_cast<char>(0x80 | (cp & 0x3F));
		} else {
			out += static_cast<char>(0xF0 | (cp >> 18));
			out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
			out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
			out += static_cast<char>(0x80 | (cp & 0x3F));
		}
	};
	auto decode_utf8 = [](const std::string& s, size_t& i, uint32_t& cp) -> bool {
		unsigned char c = static_cast<unsigned char>(s[i]);
		if (c < 0x80) {
			cp = c;
			i += 1;
			return true;
		} else if (c < 0xE0) {
			cp = ((c & 0x1F) << 6) | (static_cast<unsigned char>(s[i + 1]) & 0x3F);
			i += 2;
			return true;
		} else if (c < 0xF0) {
			cp = ((c & 0x0F) << 12) | ((static_cast<unsigned char>(s[i + 1]) & 0x3F) << 6)
				| (static_cast<unsigned char>(s[i + 2]) & 0x3F);
			i += 3;
			return true;
		} else {
			cp = ((c & 0x07) << 18) | ((static_cast<unsigned char>(s[i + 1]) & 0x3F) << 12)
				| ((static_cast<unsigned char>(s[i + 2]) & 0x3F) << 6)
				| (static_cast<unsigned char>(s[i + 3]) & 0x3F);
			i += 4;
			return true;
		}
	};
	auto toupper_utf8 = [&](const std::string& s) -> std::string {
		std::string out;
		size_t i = 0;
		while (i < s.size()) {
			uint32_t cp;
			if (!decode_utf8(s, i, cp))
				break;
			if (cp < 0x80) {
				out += static_cast<char>(toupper(cp));
			} else {
				// Basic Latin uppercase: lowercase (0xE0-0xFF) to uppercase (0xC0-0xFF)
				if (cp >= 0xE0 && cp <= 0xFF)
					cp -= 32;
				encode_utf8(cp, out);
			}
		}
		return out;
	};
	auto tolower_utf8 = [&](const std::string& s) -> std::string {
		std::string out;
		size_t i = 0;
		while (i < s.size()) {
			uint32_t cp;
			if (!decode_utf8(s, i, cp))
				break;
			if (cp < 0x80) {
				out += static_cast<char>(tolower(cp));
			} else {
				// Basic Latin lowercase: uppercase (0xC0-0xDF) to lowercase (0xE0-0xFF)
				if (cp >= 0xC0 && cp <= 0xDF)
					cp += 32;
				encode_utf8(cp, out);
			}
		}
		return out;
	};

	if (name == "UPPER") {
		SSValue v = next();
		std::string s;
		if (!v.GetString(s))
			return finish(SSValue::String(""));
		return finish(SSValue::String(toupper_utf8(s)));
	}
	if (name == "LOWER") {
		SSValue v = next();
		std::string s;
		if (!v.GetString(s))
			return finish(SSValue::String(""));
		return finish(SSValue::String(tolower_utf8(s)));
	}
	if (name == "TRIM") {
		SSValue v = next();
		std::string s;
		if (!v.GetString(s))
			return finish(SSValue::String(""));
		s.erase(std::remove_if(s.begin(), s.end(), [](unsigned char c){ return c == ' '; }), s.end());
		return finish(SSValue::String(s));
	}
	if (name == "T") {
		SSValue v = next();
		std::string s;
		if (!v.GetString(s))
			return finish(SSValue::String(""));
		return finish(SSValue::String(s));
	}
	if (name == "EXACT") {
		SSValue a = next();
		SSValue b = next();
		std::string sa, sb;
		if (!a.GetString(sa) || !b.GetString(sb))
			return finish(SSValue::Bool(false));
		return finish(SSValue::Bool(sa == sb));
	}
	if (name == "SUBSTITUTE") {
		SSValue srcVal = next();
		SSValue oldVal = next();
		SSValue newVal = next();
		std::string source, oldText, newText;
		if (!srcVal.GetString(source) || !oldVal.GetString(oldText)
				|| !newVal.GetString(newText))
			return finish(SSValue::String(""));
		int instance = 0;  // 0 = replace all
		if (used < argc) {
			double d;
			if (!numArg(d))
				return finish(SSValue::Error(kErrValue));
			instance = static_cast<int>(floor(d));
			if (instance < 1)
				return finish(SSValue::Error(kErrValue));
		}
		int pos = -1, start = 0;
		if (instance <= 0) {
			// replace all occurrences
			while (true) {
				pos = utf8FindAll(source, oldText, start);
				if (pos < 0)
					break;
				size_t bytePos = 0;
				for (int c = 0; c < pos && bytePos < source.size(); c++)
					bytePos = NextCharacter(source, bytePos);
				size_t byteEnd = bytePos;
				for (int c = 0; c < utf8CharLen(oldText) && byteEnd < source.size(); c++)
					byteEnd = NextCharacter(source, byteEnd);
				source.replace(bytePos, byteEnd - bytePos, newText);
			}
		} else {
			for (int i = 0; i < instance; i++) {
				pos = utf8FindAll(source, oldText, start);
				if (pos < 0)
					break;
				size_t bytePos = 0;
				for (int c = 0; c < pos && bytePos < source.size(); c++)
					bytePos = NextCharacter(source, bytePos);
				size_t byteEnd = bytePos;
				for (int c = 0; c < utf8CharLen(oldText) && byteEnd < source.size(); c++)
					byteEnd = NextCharacter(source, byteEnd);
				source.replace(bytePos, byteEnd - bytePos, newText);
			}
		}
		return finish(SSValue::String(source));
	}
	if (name == "SEARCH") {
		SSValue findVal = next();
		SSValue srcVal = next();
		std::string findStr, source;
		if (!findVal.GetString(findStr) || !srcVal.GetString(source))
			return finish(SSValue::Error(kErrValue));
		int start = 1;
		if (used < argc) {
			double d;
			if (!numArg(d))
				return finish(SSValue::Error(kErrValue));
			start = static_cast<int>(floor(d));
			if (start < 1)
				return finish(SSValue::Error(kErrValue));
		}
		int charPos = utf8FindNoCase(source, findStr, start);
		if (charPos <= 0)
			return finish(SSValue::Number(0.0));
		return finish(SSValue::Number(static_cast<double>(charPos)));
	}
	if (name == "FIND") {
		SSValue findVal = next();
		SSValue srcVal = next();
		std::string findStr, source;
		if (!findVal.GetString(findStr) || !srcVal.GetString(source))
			return finish(SSValue::Error(kErrValue));
		int start = 1;
		if (used < argc) {
			double d;
			if (!numArg(d))
				return finish(SSValue::Error(kErrValue));
			start = static_cast<int>(floor(d));
			if (start < 1)
				return finish(SSValue::Error(kErrValue));
		}
		int charPos = utf8Find(source, findStr, start);
		if (charPos <= 0)
			return finish(SSValue::Number(0.0));
		return finish(SSValue::Number(static_cast<double>(charPos)));
	}
	if (name == "RIGHT") {
		SSValue sVal = next();
		double nD;
		std::string s;
		if (!sVal.GetString(s) || !numArg(nD))
			return finish(SSValue::Error(kErrValue));
		int n = static_cast<int>(floor(nD));
		if (n < 1)
			return finish(SSValue::Error(kErrValue));
		return finish(SSValue::String(utf8Right(s, n)));
	}
	if (name == "LEFT") {
		SSValue sVal = next();
		double nD;
		std::string s;
		if (!sVal.GetString(s) || !numArg(nD))
			return finish(SSValue::Error(kErrValue));
		int n = static_cast<int>(floor(nD));
		if (n < 1)
			return finish(SSValue::Error(kErrValue));
		return finish(SSValue::String(utf8Left(s, n)));
	}
	if (name == "MID") {
		SSValue sVal = next();
		double startD, nD;
		std::string s;
		if (!sVal.GetString(s) || !numArg(startD) || !numArg(nD))
			return finish(SSValue::Error(kErrValue));
		int start = static_cast<int>(floor(startD));
		int n = static_cast<int>(floor(nD));
		if (start < 1)
			return finish(SSValue::Error(kErrValue));
		return finish(SSValue::String(utf8Mid(s, start, n)));
	}
	if (name == "REPLACE") {
		SSValue srcVal = next();
		double startD, nD;
		std::string src, repl;
		if (!srcVal.GetString(src) || !numArg(startD) || !numArg(nD)
				|| !next().GetString(repl))
			return finish(SSValue::Error(kErrValue));
		int start = static_cast<int>(floor(startD));
		int n = static_cast<int>(floor(nD));
		if (start < 1)
			return finish(SSValue::Error(kErrValue));
		utf8Replace(src, start, n, repl);
		return finish(SSValue::String(src));
	}
	if (name == "PROPER") {
		SSValue v = next();
		std::string s;
		if (!v.GetString(s))
			return finish(SSValue::String(""));
		bool upperNext = true;
		for (char& c : s) {
			unsigned char uc = static_cast<unsigned char>(c);
			if ((uc & 0xc0) == 0x80) {
				// continuation byte of multichar
				c = tolower(uc);
				upperNext = false;
			} else if (uc < 0x80) {
				if (upperNext) {
					c = toupper(uc);
				} else {
					c = tolower(uc);
				}
				upperNext = !isalnum(uc);
			}
		}
		return finish(SSValue::String(s));
	}
	if (name == "LEN") {
		SSValue v = next();
		std::string s;
		if (!v.GetString(s))
			return finish(SSValue::Number(0.0));
		return finish(SSValue::Number(utf8CharLen(s)));
	}
	if (name == "CHAR") {
		double d;
		if (!numArg(d))
			return finish(SSValue::Error(kErrValue));
		int val = static_cast<int>(floor(d));
		if (val < 0 || val > 127)
			return finish(SSValue::String(""));
		char c = static_cast<char>(val);
		return finish(SSValue::String(std::string(1, c)));
	}
	if (name == "CODE") {
		SSValue v = next();
		std::string s;
		if (!v.GetString(s) || s.empty())
			return finish(SSValue::Error(kErrValue));
		unsigned char c = static_cast<unsigned char>(s[0]);
		return finish(SSValue::Number(static_cast<double>(c)));
	}
	if (name == "CLEAN") {
		SSValue v = next();
		std::string s;
		if (!v.GetString(s))
			return finish(SSValue::String(""));
		std::string out;
		for (size_t i = 0; i < s.size(); ) {
			size_t nextPos = NextCharacter(s, i);
			size_t len = nextPos - i;
			unsigned char c = static_cast<unsigned char>(s[i]);
			if (len == 1 && (c < 32 || c == 127)) {
				out += ' ';
			} else {
				out += s.substr(i, len);
			}
			i = nextPos;
		}
		return finish(SSValue::String(out));
	}
	if (name == "TEXT") {
		SSValue numV = next();
		double n;
		if (!numV.GetNum(n))
			return finish(SSValue::Error(kErrValue));
		SSValue fmtV = next();
		std::string fmt;
		if (!fmtV.GetString(fmt))
			return finish(SSValue::Error(kErrValue));
		return finish(SSValue::String(FormatCellValue(SSValue::Number(n), fmt)));
	}
	if (name == "FIXED") {
		double n, decimalsD;
		if (!numArg(n) || !numArg(decimalsD))
			return finish(SSValue::Error(kErrValue));
		int decimals = static_cast<int>(floor(decimalsD));
		if (decimals < 0 || decimals > 10)
			return finish(SSValue::Error(kErrValue));
		bool noCommas = false;
		if (used < argc) {
			double d;
			if (!numArg(d))
				return finish(SSValue::Error(kErrValue));
			noCommas = (d != 0);
		}
		std::string fmt;
		if (decimals == 0) {
			fmt = noCommas ? "#" : "#,###";
		} else {
			fmt = noCommas ? "#." : "#,###.";
			for (int i = 0; i < decimals; i++)
				fmt += "0";
		}
		// Build format pattern: positive;negative
		std::string pattern = fmt + ";(";
		for (int i = 0; i < decimals; i++)
			pattern += "0";
		pattern += ")";
		return finish(SSValue::String(FormatCellValue(SSValue::Number(n), pattern)));
	}
	if (name == "DOLLAR") {
		double n;
		if (!numArg(n))
			return finish(SSValue::Error(kErrValue));
		double decimalsD = 2;  // default 2 decimals
		if (used < argc) {
			if (!numArg(decimalsD))
				return finish(SSValue::Error(kErrValue));
		}
		int decimals = static_cast<int>(floor(decimalsD));
		if (decimals < 0 || decimals > 10)
			return finish(SSValue::Error(kErrValue));
		// Round to the specified number of decimals
		double multiplier = pow(10.0, decimals);
		n = round(n * multiplier) / multiplier;
		std::string fmt = "$#,###";
		if (decimals > 0)
			fmt += ".";
		for (int i = 0; i < decimals; i++)
			fmt += "0";
		std::string pattern = fmt + ";(";
		for (int i = 0; i < decimals; i++)
			pattern += "0";
		pattern += ")";
		return finish(SSValue::String(FormatCellValue(SSValue::Number(n), pattern)));
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
	// ====== RATE ======
	if (name == "RATE") {
		double pv, fv, periods;
		if (!numArg(pv) || !numArg(fv) || !numArg(periods))
			return finish(SSValue::Error(kErrValue));
		if (periods == 0)
			return finish(SSValue::Error(kErrValue));
		if (pv == 0 || fv == 0 || pv * fv < 0)
			return finish(SSValue::Error(kErrValue));
		// rate = (fv/pv)^(1/periods) - 1
		double ratio = fv / pv;
		if (ratio <= 0)
			return finish(SSValue::Error(kErrValue));
		return finish(SSValue::Number(pow(ratio, 1.0 / periods) - 1.0));
	}
	// ====== NPV ======
	if (name == "NPV") {
		double rate;
		if (!numArg(rate))
			return finish(SSValue::Error(kErrValue));
		double sum = 0;
		int i = 0;
		each([&](const SSValue& v) {
			if (v.type == SSValue::kError) {
				return false;
			}
			if (v.type == SSValue::kNone || v.type == SSValue::kString)
				return true;
			double n;
			if (!v.GetNum(n))
				return true;
			sum += n / pow(rate + 1.0, i + 1);
			i++;
			return true;
		});
		return finish(SSValue::Number(sum));
	}
	// ====== MIRR ======
	if (name == "MIRR") {
		double finance_rate, reinvest_rate;
		if (!numArg(finance_rate) || !numArg(reinvest_rate))
			return finish(SSValue::Error(kErrValue));
		std::vector<double> cash_flows;
		each([&](const SSValue& v) {
			if (v.type == SSValue::kError) {
				return false;
			}
			if (v.type == SSValue::kNone || v.type == SSValue::kString)
				return true;
			double n;
			if (!v.GetNum(n))
				return true;
			cash_flows.push_back(n);
			return true;
		});
		if (cash_flows.empty())
			return finish(SSValue::Error(kErrValue));
		// Check there's at least one positive and one negative
		bool has_pos = false, has_neg = false;
		for (auto cf : cash_flows) {
			if (cf > 0) has_pos = true;
			if (cf < 0) has_neg = true;
		}
		if (!has_pos || !has_neg)
			return finish(SSValue::Error(kErrValue));
		// FV of positive cash flows at reinvest_rate
		// PV of negative cash flows at finance_rate
		double fv_pos = 0, pv_neg = 0;
		for (int i = 0; i < (int)cash_flows.size(); i++) {
			if (cash_flows[i] > 0)
				fv_pos += cash_flows[i] * pow(reinvest_rate + 1.0, (int)cash_flows.size() - 1 - i);
			if (cash_flows[i] < 0)
				pv_neg += cash_flows[i] / pow(finance_rate + 1.0, i);
		}
		if (pv_neg >= 0 || fv_pos <= 0)
			return finish(SSValue::Error(kErrValue));
		int n = (int)cash_flows.size();
		return finish(SSValue::Number(pow(fv_pos / -pv_neg, 1.0 / (n - 1)) - 1.0));
	}
	// ====== IRR ======
	if (name == "IRR") {
		std::vector<double> cash_flows;
		each([&](const SSValue& v) {
			if (v.type == SSValue::kError) {
				return false;
			}
			if (v.type == SSValue::kNone || v.type == SSValue::kString)
				return true;
			double n;
			if (!v.GetNum(n))
				return true;
			cash_flows.push_back(n);
			return true;
		});
		if (cash_flows.empty())
			return finish(SSValue::Error(kErrValue));
		// Check sign change
		bool has_pos = false, has_neg = false;
		for (auto cf : cash_flows) {
			if (cf > 0) has_pos = true;
			if (cf < 0) has_neg = true;
		}
		if (!has_pos || !has_neg)
			return finish(SSValue::Error(kErrValue));
		// Newton's method
		double estimate = 0.1;
		for (int iter = 0; iter < 100; iter++) {
			double npv = 0, dnpv = 0;
			for (int i = 0; i < (int)cash_flows.size(); i++) {
				double d = pow(1.0 + estimate, i + 1);
				npv += cash_flows[i] / d;
				dnpv -= (double)i * cash_flows[i] / (d * (1.0 + estimate));
			}
			if (fabs(dnpv) < 1e-15)
				break;
			double new_estimate = estimate - npv / dnpv;
			if (fabs(new_estimate - estimate) < 1e-10) {
				estimate = new_estimate;
				break;
			}
			estimate = new_estimate;
		}
		return finish(SSValue::Number(estimate));
	}
	// ====== NPER ======
	if (name == "NPER") {
		double rate, pmt, pv, fv = 0, atEnd = 0;
		if (!numArg(rate) || !numArg(pmt) || !numArg(pv))
			return finish(SSValue::Error(kErrValue));
		if (used < argc && !numArg(fv))
			return finish(SSValue::Error(kErrValue));
		if (used < argc && !numArg(atEnd))
			return finish(SSValue::Error(kErrValue));
		// Negate pv internally (per decompiled)
		pv = -pv;
		if (rate == 0.0)
			return finish(SSValue::Number((pv - fv) / pmt));
		double d = (1.0 - rate * fv / pmt) / (1.0 - rate * pv / pmt);
		if (d <= 0.0)
			return finish(SSValue::Error(kErrValue));
		double log_rate = log(rate + 1.0);
		if (fabs(log_rate) < 1e-15)
			return finish(SSValue::Error(kErrValue));
		return finish(SSValue::Number(log(d) / log_rate));
	}
	// ====== FV ======
	if (name == "FV") {
		double rate, periods, payment, pv = 0, atEnd = 0;
		if (!numArg(rate) || !numArg(periods) || !numArg(payment))
			return finish(SSValue::Error(kErrValue));
		if (used < argc && !numArg(pv))
			return finish(SSValue::Error(kErrValue));
		if (used < argc && !numArg(atEnd))
			return finish(SSValue::Error(kErrValue));
		double f = pow(rate + 1.0, periods);
		return finish(SSValue::Number(-pv * f - ((f - 1.0) / rate) * (rate * atEnd + 1.0) * payment));
	}
	// ====== DDB ======
	if (name == "DDB") {
		double cost, salvage, life, period, factor = 2.0;
		if (!numArg(cost) || !numArg(salvage) || !numArg(life) || !numArg(period))
			return finish(SSValue::Error(kErrValue));
		if (cost < 0 || life < 0 || period < 0 || period > life)
			return finish(SSValue::Error(kErrValue));
		if (used < argc && !numArg(factor))
			return finish(SSValue::Error(kErrValue));
		double accumulated = 0;
		double dep = 0;
		for (int i = 1; i <= (int)period; i++) {
			if (i == (int)period && period == life) {
				// Last period: force to salvage
				dep = cost - accumulated - salvage;
			} else {
				dep = (cost - accumulated) * factor / life;
			}
			accumulated += dep;
		}
		return finish(SSValue::Number(dep));
	}
	// ====== DB ======
	if (name == "DB") {
		double cost, salvage, life, period, month = 12.0;
		if (!numArg(cost) || !numArg(salvage) || !numArg(life) || !numArg(period))
			return finish(SSValue::Error(kErrValue));
		if (cost <= 0 || life <= 0 || period <= 0 || period > life)
			return finish(SSValue::Error(kErrValue));
		if (period == life && cost == salvage) {
			// If cost equals salvage, no depreciation
			return finish(SSValue::Number(0.0));
		}
		if (used < argc && !numArg(month))
			return finish(SSValue::Error(kErrValue));
		if (month < 1 || month > 12)
			return finish(SSValue::Error(kErrValue));
		// rate = (salvage/cost)^(1/life), r = 1 - rate, rounded to 3 decimals
		double fixed_rate = pow(salvage / cost, 1.0 / life);
		double r = 1.0 - fixed_rate;
		// Round to 3 decimals
		r = round(r * 1000.0) / 1000.0;
		double accumulated = 0;
		double dep = 0;
		for (int i = 1; i <= (int)period; i++) {
			if (i == 1) {
				// First period: cost * r * month / 12
				dep = cost * r * month / 12.0;
			} else {
				// Subsequent: (cost - accumulated) * r
				dep = (cost - accumulated) * r;
			}
			accumulated += dep;
		}
		return finish(SSValue::Number(dep));
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
		// (1 = the column right of it; HLOOKUP: the row below it)
		return finish(CellValue(table.part, found, r.from.b + col));
	}
	if (name == "INDEX") {
		SSValue rng = next(false);
		double rd, cd;
		if (rng.type != SSValue::kRange || !numArg(rd) || !numArg(cd))
			return finish(SSValue::Error(kErrValue));
		const SSRange& r = rng.range;
		int rowOff = int(floor(rd));
		int colOff = int(floor(cd));
		if (rowOff < 1 || rowOff > r.to.a - r.from.a + 1
			|| colOff < 1 || colOff > r.to.b - r.from.b + 1)
			return finish(SSValue::Error(kErrRef));
		return finish(CellValue(rng.part, r.from.a + rowOff - 1,
		                             r.from.b + colOff - 1));
	}
	if (name == "MATCH") {
		SSValue key = next();
		SSValue rng = next(false);
		double modeD = 0;
		if (rng.type != SSValue::kRange)
			return finish(SSValue::Error(kErrValue));
		if (used < argc)
			numArg(modeD);
		const SSRange& r = rng.range;
		bool approx = modeD != 0;
		int cols = r.to.b - r.from.b + 1;
		int pos = 0;
		double kn;
		bool numeric = key.GetNum(kn) && key.type != SSValue::kString;
		int idx = 0;
		for (int row = r.from.a; row <= r.to.a; row++) {
			for (int col = r.from.b; col <= r.to.b; col++) {
				SSValue v = CellValue(rng.part, row, col);
				int cmp = 0;
				if (numeric && v.type != SSValue::kString
					&& v.type != SSValue::kNone) {
					double vn;
					if (v.GetNum(vn)) {
						cmp = vn < kn ? -1 : vn > kn;
					}
				} else {
					std::string a, b;
					if (!v.GetString(a) || !key.GetString(b)) {
						idx++;
						continue;
					}
					int c2 = strcasecmp(a.c_str(), b.c_str());
					cmp = c2 < 0 ? -1 : c2 > 0;
				}
				if (cmp == 0) {
					pos = idx + 1;
					break;
				}
				if (approx && cmp > 0)
					break;
				if (approx && cmp < 0)
					pos = idx + 1;
				idx++;
			}
			if (!approx && pos > 0)
				break;
		}
		if (pos < 1)
			return finish(SSValue::Error(kErrNA));
		return finish(SSValue::Number(double(pos)));
	}
	if (name == "LOOKUP") {
		SSValue key = next();
		SSValue searchRng = next(false);
		SSValue resultRng = next(false);
		double modeD = 1;
		if (searchRng.type != SSValue::kRange)
			return finish(SSValue::Error(kErrValue));
		numArg(modeD);
		const SSRange& sr = searchRng.range;
		const SSRange& rr = resultRng.range;
		bool approx = modeD != 0;
		int found = -1;
		double kn;
		bool numeric = key.GetNum(kn) && key.type != SSValue::kString;
		int idx = 0;
		for (int row = sr.from.a; row <= sr.to.a; row++) {
			for (int col = sr.from.b; col <= sr.to.b; col++) {
				SSValue v = CellValue(searchRng.part, row, col);
				int cmp = 0;
				double vn;
				if (numeric && v.type != SSValue::kString
					&& v.type != SSValue::kNone && v.GetNum(vn)) {
					cmp = vn < kn ? -1 : vn > kn;
				} else {
					std::string a, b;
					if (!v.GetString(a) || !key.GetString(b)) {
						idx++;
						continue;
					}
					int c2 = strcasecmp(a.c_str(), b.c_str());
					cmp = c2 < 0 ? -1 : c2 > 0;
				}
				if (cmp == 0) {
					found = idx;
					break;
				}
				if (approx) {
					if (cmp < 0)
						found = idx;
					if (cmp > 0)
						break;
				}
				idx++;
			}
			if (!approx && found >= 0)
				break;
		}
		if (found < 0)
			return finish(SSValue::Error(kErrNA));
		int rowOff = found / (sr.to.b - sr.from.b + 1);
		return finish(CellValue(resultRng.part, rr.from.a + rowOff,
		                             rr.from.b + found % (sr.to.b - sr.from.b + 1)));
	}
	if (name == "HLOOKUP") {
		SSValue key = next();
		SSValue table = next(false);
		double rowOffD, modeD = 1;
		if (table.type != SSValue::kRange || !numArg(rowOffD))
			return finish(SSValue::Error(kErrValue));
		const SSRange& r = table.range;
		int rowOff = int(floor(rowOffD));
		if (rowOff < 1 || rowOff > r.to.a - r.from.a + 1)
			return finish(SSValue::Error(kErrRef));
		if (used < argc)
			numArg(modeD);
		bool approx = modeD != 0;
		int found = -1;
		int lastLess = -1;
		double kn;
		bool numeric = key.GetNum(kn) && key.type != SSValue::kString;
		for (int col = r.from.b; col <= r.to.b; col++) {
			SSValue v = CellValue(table.part, r.from.a, col);
			int cmp;
			double vn;
			if (numeric && v.type != SSValue::kString
				&& v.type != SSValue::kNone && v.GetNum(vn))
				cmp = vn < kn ? -1 : vn > kn;
			else {
				std::string a, b;
				if (!v.GetString(a) || !key.GetString(b))
					continue;
				int c2 = strcasecmp(a.c_str(), b.c_str());
				cmp = c2 < 0 ? -1 : c2 > 0;
			}
			if (cmp == 0) {
				found = col;
				break;
			}
			if (approx) {
				if (cmp < 0)
					lastLess = col;
				else if (cmp > 0) {
					if (lastLess >= 0)
						found = lastLess;
					break;
				}
			}
		}
		if (found < 0) {
			if (approx && lastLess >= 0)
				found = lastLess;
			else
				return finish(SSValue::Error(kErrNA));
		}
		return finish(CellValue(table.part, r.from.a + rowOff, found));
	}
	if (name == "TYPE") {
		SSValue v = next();
		int t;
		switch (v.type) {
			case SSValue::kNone:    t = 1; break;
			case SSValue::kString:
				if (v.text.empty())
					t = 1;
				else
					t = 6;
				break;
			case SSValue::kBool:    t = 2; break;
			case SSValue::kNumber:  t = 3; break;
			case SSValue::kTime:    t = 4; break;
			case SSValue::kError:   t = 5; break;
			default:                t = 6; break;
		}
		return finish(SSValue::Number(double(t)));
	}
	if (name == "NA") {
		return finish(SSValue::Error(kErrNA));
	}
	if (name == "ERROR") {
		return finish(SSValue::Error(kErrUser));
	}
	if (name == "COL") {
		SSValue v = next(false);
		if (v.type == SSValue::kRange) {
			const SSRange& r = v.range;
			return finish(SSValue::Number(double(r.from.b + 1)));
		}
		return finish(SSValue::Number(double(it.at.b + 1)));
	}
	if (name == "ROW") {
		SSValue v = next(false);
		if (v.type == SSValue::kRange) {
			const SSRange& r = v.range;
			return finish(SSValue::Number(double(r.from.a + 1)));
		}
		return finish(SSValue::Number(double(it.at.a + 1)));
	}
	if (name == "ISNUMBER") {
		SSValue v = next();
		double n;
		return finish(SSValue::Bool(v.GetNum(n)));
	}
	if (name == "ISNA") {
		SSValue v = next();
		return finish(SSValue::Bool(v.type == SSValue::kError
		                       && v.error == kErrNA));
	}
	if (name == "ISLOGICAL") {
		SSValue v = next();
		return finish(SSValue::Bool(v.type == SSValue::kBool));
	}
	if (name == "ISTEXT") {
		SSValue v = next();
		return finish(SSValue::Bool(v.type == SSValue::kString));
	}
	if (name == "ISERR") {
		SSValue v = next();
		return finish(SSValue::Bool(v.type == SSValue::kError
		                       && v.error != kErrNA));
	}
	if (name == "ISERROR") {
		SSValue v = next();
		return finish(SSValue::Bool(v.type == SSValue::kError));
	}
	if (name == "ISDATE") {
		SSValue v = next();
		return finish(SSValue::Bool(v.type == SSValue::kTime));
	}
	if (name == "SUMIF") {
		SSValue rangeV = next(false);
		SSValue critV = next();
		SSValue sumRangeV = next(false);
		if (rangeV.type != SSValue::kRange)
			return finish(SSValue::Error(kErrValue));
		const SSRange& range = rangeV.range;
		int rangePart = rangeV.part;
		int sumPart = rangePart;
		const SSRange& sumR = sumRangeV.type == SSValue::kRange
			? sumRangeV.range : range;
		if (sumRangeV.type == SSValue::kRange)
			sumPart = sumRangeV.part;
		// Parse criteria string
		std::string crit;
		critV.GetString(crit);
		char op = 0;
		double val = 0;
		if (!crit.empty() && (crit[0] == '>' || crit[0] == '<' || crit[0] == '='
		                        || crit[0] == '!')) {
			op = crit[0];
			if (crit.size() > 1 && crit[1] == '=') {
				op = '=';
				if (!StringToNumber(crit.substr(2), val))
					return finish(SSValue::Error(kErrValue));
			} else {
				if (!StringToNumber(crit.substr(1), val))
					return finish(SSValue::Error(kErrValue));
			}
		} else {
			val = 0;
			if (StringToNumber(crit, val)) {
				op = '=';
			} else {
				op = 't';
			}
		}
		double sum = 0;
		for (int r = range.from.a; r <= range.to.a; r++) {
			for (int c = range.from.b; c <= range.to.b; c++) {
				SSValue v = CellValue(rangePart, r, c);
				if (v.type == SSValue::kError)
					return finish(v);
				int sr = r;
				int sc = c - range.from.b + sumR.from.b;
				if (v.type == SSValue::kNone || v.type == SSValue::kString) {
					if (op == 't') {
						std::string s;
						v.GetString(s);
						if (s == crit)
							sum += CellValue(sumPart, sr, sc).number;
					}
					continue;
				}
				double n;
				if (!v.GetNum(n)) continue;
				bool match = false;
				switch (op) {
					case '>': match = n > val; break;
					case '<': match = n < val; break;
					case '=': match = n == val; break;
					case 't': match = true; break;
				}
				if (match)
					sum += CellValue(sumPart, sr, sc).number;
			}
		}
		return finish(SSValue::Number(sum));
	}
	if (name == "COUNTIF") {
		SSValue rangeV = next(false);
		SSValue critV = next();
		if (rangeV.type != SSValue::kRange)
			return finish(SSValue::Error(kErrValue));
		const SSRange& range = rangeV.range;
		std::string crit;
		critV.GetString(crit);
		char op = 0;
		double val = 0;
		if (!crit.empty() && (crit[0] == '>' || crit[0] == '<' || crit[0] == '='
		                        || crit[0] == '!')) {
			op = crit[0];
			if (crit.size() > 1 && crit[1] == '=') {
				op = '=';
				if (!StringToNumber(crit.substr(2), val))
					return finish(SSValue::Error(kErrValue));
			} else {
				if (!StringToNumber(crit.substr(1), val))
					return finish(SSValue::Error(kErrValue));
			}
		} else {
			val = 0;
			if (StringToNumber(crit, val)) {
				op = '=';
			} else {
				op = 't';
			}
		}
		int count = 0;
		for (int r = range.from.a; r <= range.to.a; r++) {
			for (int c = range.from.b; c <= range.to.b; c++) {
				SSValue v = CellValue(rangeV.part, r, c);
				if (v.type == SSValue::kError)
					return finish(v);
				if (v.type == SSValue::kNone || v.type == SSValue::kString) {
					if (op == 't') {
						std::string s;
						v.GetString(s);
						if (s == crit)
							count++;
					}
					continue;
				}
				double n;
				if (!v.GetNum(n)) continue;
				bool match = false;
				switch (op) {
					case '>': match = n > val; break;
					case '<': match = n < val; break;
					case '=': match = n == val; break;
					case 't': match = true; break;
				}
				if (match)
					count++;
			}
		}
		return finish(SSValue::Number(count));
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
