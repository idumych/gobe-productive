// The number formats offered by the Number dialog (TNumberFormatFactory,
// TNumberFormatCategory, TNumberFormatGroup in the original): categories
// read from the '#NF#' resource 1 and its '#NFC' and '#FMT' resources, and
// format strings made from a category's types for a number of decimals and
// with or without thousands separators.
#ifndef GOBE_NUMBER_FORMATS_H
#define GOBE_NUMBER_FORMATS_H

#include <array>
#include <string>
#include <vector>

#include "ResourceFile.h"

namespace gobe {

// TNumberFormatGroup::FormatString when it has no formats
extern const char* const kGeneralNumFormatStr;	// "General"

struct NumberFormatCategory {
	std::string name;			// the '#NFC' resource's name (+0x4)
	std::string help;			// +0x8
	bool commas = false;		// +0x14: thousands separators can be chosen
	bool decimals = false;		// +0x15: the number of decimals can be chosen
	// its types (TNumberFormatGroup): the formats for positive and negative
	// numbers, zero and text ("" none), as their format strings
	std::vector<std::array<std::string, 4>> groups;
};

struct NumberFormatGroupInfo {	// TNumberFormatGroup +0x10..+0x13
	int category = -1;
	int type = 0;
	int decimals = 0;
	bool commas = false;
};

class NumberFormats {
public:
	// TNumberFormatFactory::LoadResource: the categories '#NF#' 1 lists, in
	// order (General first)
	bool Load(const ResourceFile& resources, std::string& error);

	const std::vector<NumberFormatCategory>& Categories() const { return fCategories; }

	// TNumberFormatCategory::GetFormatGroup and TNumberFormatGroup::
	// FormatString: the type's formats, parameterized when the category
	// takes decimals (or commas are wanted), joined with ';'
	std::string FormatString(int category, int type, int decimals, bool commas) const;

	// TNumberFormatFactory::FindFormat: the category, type, decimals and
	// commas that give `format` (or, failing that, the Number, Currency or
	// Percentage type it looks like); false if there are no categories
	bool Find(std::string format, NumberFormatGroupInfo& info) const;

private:
	bool FindInCategory(int category, const std::string& format,
		NumberFormatGroupInfo& info) const;

	std::vector<NumberFormatCategory> fCategories;
};

// TNumberFormat::Parameterize, the format string: `decimals` places after
// the point (0: none, nor the point) and thousands separators or not
std::string ParameterizeFormat(const std::string& format, int decimals, bool commas);

} // namespace gobe

#endif
