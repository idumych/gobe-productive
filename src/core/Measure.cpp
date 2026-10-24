// TMeasure: lengths in units.
#include "Measure.h"

#include <cstdio>
#include <cstdlib>

namespace gobe {

namespace {

// TMeasure::SetValue: points per unit
const float kPointsPer[6] = {72.0f, 28.3464f, 2.83464f, 12.0f, 1.0f, 16.0f};
// TMeasure::GetText: units per point
const float kPerPoint[6] = {0.013888889f, 0.03527785f, 0.3527785f, 0.083333336f, 1.0f,
	0.0625f};


// TruncateNumericString: trailing zeros after a '.', and the '.'
std::string
Truncate(std::string s)
{
	if (s.find('.') == std::string::npos)
		return s;
	while (!s.empty() && s.back() == '0')
		s.pop_back();
	if (!s.empty() && s.back() == '.')
		s.pop_back();
	return s;
}

} // namespace


std::string
MeasureText(float points, int unit, int flags, const std::vector<std::string>& unitNames)
{
	float value = unit >= 0 && unit < 6 ? points * kPerPoint[unit] : points;
	char buffer[64];
	// TString::SetNum(float, 0 | 1): "%.12g" or "%.2f", truncated
	snprintf(buffer, sizeof buffer, (flags & kMeasureTwoPlaces) ? "%.2f" : "%.12g",
		double(value));
	std::string text = Truncate(buffer);
	if ((flags & kMeasureWithUnit) && unit >= 0 && size_t(unit) < unitNames.size())
		text += " " + unitNames[unit];
	return text;
}


bool
ParseMeasure(const std::string& text, int unit, const std::vector<std::string>& unitNames,
	bool withLines, bool negative, float& points)
{
	std::string number = text;
	size_t count = withLines ? 6 : 5;
	for (size_t i = 0; i < count && i < unitNames.size(); i++) {
		size_t at = number.find(unitNames[i]);
		if (at == std::string::npos)
			continue;
		if (at > 0 && number[at - 1] == ' ')
			at--;
		number = number.substr(0, at);
		unit = int(i);
		break;
	}
	char* end;
	float value = strtof(number.c_str(), &end);
	if (end == number.c_str() || (value < 0 && !negative))
		return false;
	points = unit >= 0 && unit < 6 ? value * kPointsPer[unit] : value;
	return true;
}

} // namespace gobe
