// Fields in word processing text.
#include "Fields.h"

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

} // namespace gobe
