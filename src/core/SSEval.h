// Spreadsheet formula evaluation (TEvaluator, TEvalResult and the built-in
// functions of the original). Formula cells don't store their values in the
// document, so everything shown is computed here.
//
// Formulas are token lists in prefix order: an operator token (0-13) is
// followed by its operands, 0x0e/0x0f are parentheses, 0x1c is a function
// call (argument count + name) followed by its arguments.
#ifndef GOBE_SS_EVAL_H
#define GOBE_SS_EVAL_H

#include <cstdint>
#include <map>
#include <string>

#include "Document.h"
#include "SSPart.h"

namespace gobe {

// TEvalResult, reduced (no references left except ranges)
struct SSValue {
	enum Type { kNone, kNumber, kBool, kString, kTime, kError, kRange };
	Type type = kNone;
	double number = 0;		// kNumber, kBool (0/1)
	int64_t time = 0;		// kTime: seconds since 1970 (UTC)
	int error = 0;			// kError: ErrType
	std::string text;		// kString
	SSRange range;			// kRange (absolute)
	int part = -1;			// kRange: the part (-1: the formula's own)

	static SSValue Number(double n) { SSValue v; v.type = kNumber; v.number = n; return v; }
	static SSValue Bool(bool b) { SSValue v; v.type = kBool; v.number = b; return v; }
	static SSValue String(std::string s) { SSValue v; v.type = kString; v.text = std::move(s); return v; }
	static SSValue Error(int e) { SSValue v; v.type = kError; v.error = e; return v; }
	static SSValue Time(int64_t t) { SSValue v; v.type = kTime; v.time = t; return v; }

	// TEvalResult::GetNum / GetBool / GetString (unformatted)
	bool GetNum(double& out) const;
	bool GetBool(bool& out) const;
	bool GetString(std::string& out) const;
};

// ErrString: "#DIV/0!" etc.
std::string ErrorText(int error);
// TParser::UnParse: the formula as text, references relative to `at`
std::string FormulaText(const std::vector<FormulaToken>& formula, CellID at);
// TCellID::Name
std::string CellName(CellID id);

class SSBook;

class SSEvaluator {
public:
	// `book` (optional) resolves names and other parts' cells
	SSEvaluator(const SSPart& part, SSBook* book = nullptr, int index = -1);

	// the value of a cell (evaluating its formula, cached)
	SSValue Value(uint16_t row, uint16_t column);
	// evaluates every formula, top to bottom, so chains of references to
	// the cells above don't recurse deeply
	void EvaluateAll();

private:
	struct Iter;
	SSValue Eval(Iter& it, bool reduce);
	SSValue Reduce(const SSValue& v, CellID at);
	void Skip(Iter& it);
	SSValue Operator(Iter& it, int op);
	SSValue Function(Iter& it, const std::string& name, int argc);
	SSValue CellValue(int part, int row, int column);

	const SSPart& fPart;
	SSBook* fBook;
	int fIndex;
	std::map<uint32_t, const SSCell*> fCells;
	std::map<uint32_t, SSValue> fCache;
	std::map<uint32_t, bool> fBusy;
};

// The spreadsheet parts of a document, evaluated together: formulas can
// name ranges in other parts.
class SSBook {
public:
	explicit SSBook(const Document& doc);
	~SSBook();

	SSEvaluator* For(size_t part);
	// after cells changed: evaluate everything again
	void Reset();
	bool ResolveName(const std::string& name, int fromPart, int& part, SSRange& range) const;

private:
	const Document& fDoc;
	std::map<size_t, SSEvaluator*> fEvaluators;
};

} // namespace gobe

#endif
