#include "NumberFormats.h"

#include <cstring>

namespace gobe {

const char* const kGeneralNumFormatStr = "General";


static uint32_t
LittleUInt32(const std::vector<uint8_t>& data, size_t& at)
{
	if (at + 4 > data.size()) {
		at = data.size();
		return 0;
	}
	uint32_t value = uint32_t(data[at]) | uint32_t(data[at + 1]) << 8
		| uint32_t(data[at + 2]) << 16 | uint32_t(data[at + 3]) << 24;
	at += 4;
	return value;
}


// TRsrHelper::GetString: NUL-terminated
static std::string
CString(const std::vector<uint8_t>& data, size_t& at)
{
	std::string s;
	while (at < data.size() && data[at] != 0)
		s += char(data[at++]);
	at++;
	return s;
}


bool
NumberFormats::Load(const ResourceFile& resources, std::string& error)
{
	fCategories.clear();
	const ResourceFile::Resource* list = resources.Find(FourCC("#NF#"), 1);
	if (list == nullptr) {
		error = "no '#NF#' resource 1";
		return false;
	}
	// all but the last entry
	size_t count = list->data.size() / 4;
	size_t at = 0;
	for (size_t i = 0; i + 1 < count; i++) {
		int32_t id = int32_t(LittleUInt32(list->data, at));
		const ResourceFile::Resource* r = resources.Find(FourCC("#NFC"), id);
		if (r == nullptr) {
			error = "no '#NFC' resource " + std::to_string(id);
			return false;
		}
		// TNumberFormatCategory::LoadResource
		NumberFormatCategory category;
		const std::vector<uint8_t>& d = r->data;
		size_t p = 0;
		category.commas = LittleUInt32(d, p) != 0;
		category.decimals = LittleUInt32(d, p) != 0;
		uint32_t groups = LittleUInt32(d, p);
		for (uint32_t g = 0; g < groups && p < d.size(); g++) {
			std::array<std::string, 4> formats;
			for (int k = 0; k < 4; k++) {
				int32_t format = int32_t(LittleUInt32(d, p));
				if (format == 0)
					continue;
				// TNumberFormatCategory::LoadFormat: its string first
				const ResourceFile::Resource* f = resources.Find(FourCC("#FMT"), format);
				if (f == nullptr) {
					error = "no '#FMT' resource " + std::to_string(format);
					return false;
				}
				size_t q = 0;
				formats[k] = CString(f->data, q);
			}
			// (TNumberFormat::Negate makes a negative format for a type
			// without one; every type of the original's categories has one)
			category.groups.push_back(formats);
		}
		LittleUInt32(d, p);
		LittleUInt32(d, p);
		category.help = CString(d, p);
		category.name = r->name;
		fCategories.push_back(std::move(category));
	}
	return true;
}


std::string
ParameterizeFormat(const std::string& format, int decimals, bool commas)
{
	std::string s = format;
	auto digit = [&](size_t i) { return i < s.size() && (s[i] == '0' || s[i] == '#'); };
	bool dropPoint = false;
	size_t point = s.find('.');
	if (decimals == 0) {
		if (point != std::string::npos) {
			size_t n = 0;
			while (digit(point + 1 + n))
				n++;
			s.erase(point + 1, n);
			dropPoint = true;
		}
	} else {
		std::string unit = "0";
		if (point == std::string::npos) {
			point = s.size();
			s += '.';
		} else if (point + 1 < s.size()) {
			if (s[point + 1] == '#')
				unit = "#";
			size_t n = 0;
			while (digit(point + 1 + n))
				n++;
			s.erase(point + 1, n);
		}
		std::string places;
		for (int i = 0; i < decimals; i++)
			places += unit;
		s.insert(point + 1, places);
	}

	size_t comma = s.find(',');
	if (commas) {
		if (comma == std::string::npos) {
			// the digits before the point become "#,###"
			size_t last = 0;
			for (size_t i = 0; i < s.size() && s[i] != '.'; i++)
				if (s[i] == '0' || s[i] == '#')
					last = i;
			size_t first = 0;
			if (last != 0) {
				size_t i = last, previous;
				do {
					previous = i;
					i--;
				} while ((i != 0 && s[i] == '0') || s[i] == '#');
				first = digit(i) ? i : previous;
			}
			s.erase(first, last - first + 1);
			s.insert(first, "#,###");
		}
	} else if (comma != std::string::npos) {
		size_t at = s.find("#,###");
		if (at == std::string::npos)
			at = comma;
		s.erase(at, 5);
		s.insert(at, "#");
	}
	if (dropPoint) {
		size_t at = s.find('.');
		if (at != std::string::npos)
			s.erase(at, 1);
	}
	return s;
}


std::string
NumberFormats::FormatString(int categoryIndex, int type, int decimals, bool commas) const
{
	if (categoryIndex < 0 || categoryIndex >= int(fCategories.size()))
		return kGeneralNumFormatStr;
	const NumberFormatCategory& category = fCategories[categoryIndex];
	if (type < 0 || type >= int(category.groups.size()))
		return kGeneralNumFormatStr;
	std::array<std::string, 4> formats = category.groups[type];
	if (category.decimals || commas) {
		// TNumberFormatGroup::Instantiate
		for (std::string& f : formats)
			if (!f.empty())
				f = ParameterizeFormat(f, decimals, commas);
	}
	const std::string& positive = formats[0];
	const std::string& negative = formats[1];
	const std::string& zero = formats[2];
	const std::string& text = formats[3];
	if (positive.empty() && negative.empty() && zero.empty() && text.empty())
		return kGeneralNumFormatStr;
	std::string s = positive;
	if (!negative.empty() || !zero.empty() || !text.empty())
		s += ";";
	s += negative;
	if (!zero.empty() || !text.empty())
		s += ";";
	s += zero;
	if (!text.empty())
		s += ";" + text;
	return s;
}


// TNumberFormatCategory::FindFormat
bool
NumberFormats::FindInCategory(int categoryIndex, const std::string& format,
	NumberFormatGroupInfo& info) const
{
	const NumberFormatCategory& category = fCategories[categoryIndex];
	bool sections = format.find(';') != std::string::npos;
	bool commas = category.commas && format.find(',') != std::string::npos;
	int decimals = 0;
	size_t point = format.find('.');
	if (category.decimals && point != std::string::npos)
		for (size_t i = point + 1; i < format.size() && (format[i] == '0' || format[i] == '#');
				i++)
			decimals++;
	for (int type = 0; type < int(category.groups.size()); type++) {
		std::string candidate = FormatString(categoryIndex, type, decimals, commas);
		size_t semicolon = candidate.find(';');
		if (!sections && semicolon != std::string::npos)
			candidate.resize(semicolon);
		if (candidate == format) {
			info.category = categoryIndex;
			info.type = type;
			info.decimals = category.decimals || commas ? decimals : 0;
			info.commas = commas;
			return true;
		}
	}
	return false;
}


bool
NumberFormats::Find(std::string format, NumberFormatGroupInfo& info) const
{
	if (fCategories.empty())
		return false;
	size_t at = format.find("AM/PM");
	if (at != std::string::npos)
		format.erase(at, 3);
	while ((at = format.find('\\')) != std::string::npos)
		format.erase(at, 1);
	for (int c = 0; c < int(fCategories.size()); c++)
		if (FindInCategory(c, format, info))
			return true;

	// what it looks like: Currency (category 2), Percentage (5) or Number (1)
	bool dollar = format.find('$') != std::string::npos;
	bool commas = format.find(',') != std::string::npos;
	bool parenthesis = format.find('(') != std::string::npos;
	bool red = format.find("Red") != std::string::npos;
	int decimals = 0;
	size_t point = format.find('.');
	if (point != std::string::npos)
		for (size_t i = point + 1; i < format.size() && (format[i] == '0' || format[i] == '#');
				i++)
			decimals++;
	int category, type;
	if (dollar) {
		category = 2;
		type = parenthesis ? (red ? 7 : 6) : (red ? 5 : 4);
	} else if (format.find('%') != std::string::npos) {
		category = 5;
		type = 0;
	} else {
		category = 1;
		type = parenthesis ? (red ? 4 : 3) : (red ? 2 : 0);
	}
	if (category >= int(fCategories.size()))
		return false;
	const NumberFormatCategory& c = fCategories[category];
	info.category = category;
	info.type = type;
	info.decimals = c.decimals || commas ? decimals : 0;
	info.commas = commas;
	return true;
}

} // namespace gobe
