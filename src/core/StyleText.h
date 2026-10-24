// What Format > Edit Styles… shows of a style's properties: each property's
// name (TProperty::GetTypeName) and value (GetDataStr), from the strings
// the caller looks up (STR# 5, 7, 11, 13, 1304, 1305).
#ifndef GOBE_STYLETEXT_H
#define GOBE_STYLETEXT_H

#include <cstdint>
#include <functional>
#include <string>

#include "DocSections.h"
#include "Editing.h"

namespace gobe {

// string `index` (from 0) of STR# `id`
typedef std::function<std::string(int32_t id, int index)> StringLookup;

// TProperty::GetTypeName ("" for the add-on parts' properties)
std::string PropertyTypeName(const Property& property, const StringLookup& strings);
// TProperty::GetDataStr; lengths in `measureUnit` (the Measurement Units
// preference)
std::string PropertyDataText(const Property& property, const StringLookup& strings,
	int measureUnit);
// TPropertyListItem: an ink property draws its ink instead of a value
bool PropertyIsInk(const Property& property);

// TStyleManager::GetActiveStyle for the text from `from` to `to` of a text
// part (a caret: the run there; else each run of the selection, with its
// paragraph's style under it)
struct SelectionStyles {
	// the last style applied to all of it, else the last named style all
	// of it is based on ("Default" if one applied to only part of it), with
	// "+" if the text has more than that style
	std::string active;
	// TAppliedStylesTable: the named styles applied (an unnamed style's
	// links[1]), and whether only to part of the text
	std::vector<std::pair<std::string, bool>> applied;
	// TStyleManager::GetIncrementalProps: the unnamed styles' own
	// properties, in the style table's order
	std::vector<uint32_t> incremental;
};
SelectionStyles TextSelectionStyles(const StyleMgr& styles, const WPPart& part,
	TextPosition from, TextPosition to);

} // namespace gobe

#endif
