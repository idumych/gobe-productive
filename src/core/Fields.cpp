// Fields in word processing text.
#include "Fields.h"

#include "Document.h"
#include "DocWriter.h"
#include "Editing.h"
#include "SSEval.h"
#include "SSStyle.h"

namespace gobe {

SSValue
StoredValue(const EvalResult& result)
{
	switch (result.type) {
		case Sig("Bool"):
			return SSValue::Bool(result.number != 0);
		case Sig("Lng#"):
		case Sig("Flt#"):
		case Sig("Dbl#"):
			return SSValue::Number(result.number);
		case Sig("Strn"):
			return SSValue::String(result.text);
		case Sig("Time"):
			return SSValue::Time(result.value);
		case Sig("Err#"):
			return SSValue::Error(int(result.value));
		default:
			return SSValue();
	}
}


std::string
FieldText(SSBook& book, size_t part, const WPMiscInfo& field, int page)
{
	if (field.kind == Sig("PAGE"))
		return page < 0 ? "#" : std::to_string(page + 1);
	if (field.kind != Sig("FORM"))
		return "";
	const Expression& e = field.expression;
	std::string text;
	if (field.displayFormula)
		text = e.text;
	if (field.displayResult) {
		// "formula = value", without the formula's "="
		if (field.displayFormula)
			text = (text.empty() ? text : text.substr(1)) + " = ";
		SSValue value = field.isStatic ? StoredValue(e.result)
			: book.EvaluateField(e.formula, int(part));
		text += FormatCellValue(value, e.format);
	}
	return text;
}

EvalResult
ResultOf(const SSValue& value)
{
	EvalResult r;
	switch (value.type) {
		case SSValue::kNumber:
			r.type = Sig("Dbl#");
			r.number = value.number;
			break;
		case SSValue::kBool:
			r.type = Sig("Bool");
			r.number = value.number;
			break;
		case SSValue::kString:
			r.type = Sig("Strn");
			r.text = value.text;
			break;
		case SSValue::kTime:
			r.type = Sig("Time");
			r.value = uint32_t(value.time);
			break;
		case SSValue::kError:
			r.type = Sig("Err#");
			r.value = uint32_t(value.error);
			break;
		default:
			break;
	}
	return r;
}


bool
FormulaField(Document& doc, SSBook& book, size_t part, const std::string& formula, const std::string& format,
	uint32_t command, bool isStatic, bool displayResult, bool displayFormula, WPMiscInfo& field,
	std::string& error)
{
	field = WPMiscInfo{};
	field.kind = Sig("FORM");
	Expression& e = field.expression;
	e.text = formula;
	e.format = format;
	std::string body = !formula.empty() && formula[0] == '=' ? formula.substr(1) : formula;
	auto isName = [&](const std::string& name) {
		int p;
		SSRange range;
		return book.ResolveName(name, int(part), p, range);
	};
	if (!ParseFormula(body, CellID(), isName, e.formula, error))
		return false;
	e.result = ResultOf(book.EvaluateField(e.formula, int(part)));
	// TExpression: a link for each name, in the order they come (a name
	// used twice is listed twice)
	for (const FormulaToken& t : e.formula) {
		size_t source, reference;
		if (t.kind != 0x1b || t.strings.size() < 2
				|| !doc.FindName(t.strings[1], int(part), source, reference))
			continue;
		size_t links = doc.links.links.size();
		uint32_t link = LinkFor(doc, source, reference, part);
		doc.linksChanged |= doc.links.links.size() != links;
		e.links.push_back(link);
	}
	field.text = formula;
	field.command = command;
	field.isStatic = isStatic;
	field.displayResult = displayResult;
	field.displayFormula = displayFormula;
	// TMiscFormulaInfo::Write: the expression, the command, the flags
	DocWriter w;
	WriteExpression(w, e);
	w.UInt32(command);
	w.Bool(isStatic);
	w.Bool(displayResult);
	w.Bool(displayFormula);
	field.raw = w.Data();
	return true;
}

} // namespace gobe
