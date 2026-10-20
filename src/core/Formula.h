// Spreadsheet expressions as stored in documents: used by spreadsheet cells
// and by formula fields in word processing text (TMiscFormulaInfo).
#ifndef GOBE_FORMULA_H
#define GOBE_FORMULA_H

#include <cstdint>
#include <string>
#include <vector>

#include "DocArchive.h"

namespace gobe {

// TCellID::Read: two u16
struct CellID {
	uint16_t a = 0;
	uint16_t b = 0;
};

// TCellRef::Read: a cell id and flags (absolute/relative)
struct CellRef {
	CellID cell;
	uint16_t flags = 0;
};

CellID ReadCellID(DocArchive& a);
CellRef ReadCellRef(DocArchive& a);

// TEvalResult::Read: the cached value
struct EvalResult {
	uint32_t type = 0;		// 'Err#', 'Bool', 'Dbl#', 'Strn', 'Time', ...
	double number = 0;		// Bool, Dbl#
	uint32_t value = 0;		// Err# code, Time
	std::string text;		// Strn
};
EvalResult ReadEvalResult(DocArchive& a);

// TFormula::TFormula(TInputArchive&): tokens (u16 kind + payload) up to and
// including the end token 0x1d
struct FormulaToken {
	uint16_t kind;
	double number = 0;
	uint32_t value = 0;
	std::vector<std::string> strings;
	std::vector<CellRef> refs;
};
std::vector<FormulaToken> ReadFormula(DocArchive& a);
// TFormula::Write: the inverse
class DocWriter;
void WriteFormula(DocWriter& w, const std::vector<FormulaToken>& tokens);

// TExpression::Read: 'EXPR' ... 'ExpR'
struct Expression {
	CellID cell;
	std::string text;		// +0x04
	uint32_t valueC = 0;
	uint32_t value10 = 0;
	std::string source;		// +0x14: the formula as typed
	EvalResult result;
	std::string format;
	std::vector<FormulaToken> formula;
	std::vector<uint32_t> links;
};
void ReadExpression(DocArchive& a, Expression& out);

} // namespace gobe

#endif
