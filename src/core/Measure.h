// TMeasure: a length in points shown and typed in a unit (TMeasureUnit:
// 0 in, 1 cm, 2 mm, 3 pc, 4 pt, 5 li; the names are STR# 7).
#ifndef GOBE_MEASURE_H
#define GOBE_MEASURE_H

#include <string>
#include <vector>

namespace gobe {

enum {
	kMeasureWithUnit = 1,	// append " <unit>"
	kMeasureTwoPlaces = 2	// "%.2f" (else "%.12g"); trailing zeros dropped
};

// TMeasure::GetText(unit, flags)
std::string MeasureText(float points, int unit, int flags,
	const std::vector<std::string>& unitNames);
// TMeasure::SetText: a number, optionally followed by a unit name (lines
// only if `withLines`); false if it isn't a number, or is negative unless
// `negative`
bool ParseMeasure(const std::string& text, int unit, const std::vector<std::string>& unitNames,
	bool withLines, bool negative, float& points);

} // namespace gobe

#endif
