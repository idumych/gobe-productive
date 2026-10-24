// Fields in word processing text: a misc character (0x04) whose paragraph
// info says what it shows (TMiscInfo::Text).
#ifndef GOBE_FIELDS_H
#define GOBE_FIELDS_H

#include <string>

#include "WPPart.h"

namespace gobe {

class SSBook;
struct SSValue;

// TEvalResult as saved, as a value
SSValue StoredValue(const EvalResult& result);

// What a field shows: a formula field its formula's value in its number
// format, its formula, or both (TMiscFormulaInfo::Text; the value saved
// with it if it is static, else evaluated again as the original does on
// opening), a page number field the number of the page it is on
// (TMiscPageInfo::GetPageStr: `page` from 0, -1 for none: "#"). Other
// kinds show nothing.
std::string FieldText(SSBook& book, size_t part, const WPMiscInfo& field, int page);

// a value as TEvalResult saves it
EvalResult ResultOf(const SSValue& value);

// TMiscFormulaInfo::TMiscFormulaInfo(TPart*, BMessage*): a formula field
// for text part `part` (`formula` with its "="), evaluated, with its
// number format, the command that makes it ('sM53' date, 'sM54' time:
// "=Now()"; 'sM55' formula) and its flags; the names it uses get links to
// the part (added to the document if new). False, with the parser's
// message, if the formula doesn't parse.
struct Document;
bool FormulaField(Document& doc, SSBook& book, size_t part, const std::string& formula,
	const std::string& format, uint32_t command, bool isStatic, bool displayResult,
	bool displayFormula, WPMiscInfo& field, std::string& error);

} // namespace gobe

#endif
