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

} // namespace gobe

#endif
