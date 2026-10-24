// doc_measure: TMeasure's text in each unit (STR# 7: in cm mm pc pt li) and
// reading it back. Expected values from python3 with the same float
// factors (TMeasure::GetText / SetValue), e.g.
//   f=lambda x: struct.unpack('f',struct.pack('f',x))[0]
//   '%.12g' % f(f(100)*f(0.03527785))  -> 3.52778506279
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "Measure.h"

using namespace gobe;

static int sOk = 0, sFailed = 0;

static void
Check(bool ok, const std::string& what)
{
	if (ok)
		sOk++;
	else {
		sFailed++;
		printf("FAIL %s\n", what.c_str());
	}
}


int
main()
{
	const std::vector<std::string> units = {"in", "cm", "mm", "pc", "pt", "li"};
	struct { float points; int unit; int flags; const char* text; } texts[] = {
		{72, 0, kMeasureWithUnit | kMeasureTwoPlaces, "1 in"},
		{72, 0, 0, "1"},
		{90, 0, kMeasureTwoPlaces, "1.25"},
		{72, 1, kMeasureWithUnit | kMeasureTwoPlaces, "2.54 cm"},
		{72, 2, kMeasureTwoPlaces, "25.4"},
		{36, 3, kMeasureWithUnit | kMeasureTwoPlaces, "3 pc"},
		{13.5f, 4, kMeasureWithUnit | kMeasureTwoPlaces, "13.5 pt"},
		{32, 5, kMeasureWithUnit | kMeasureTwoPlaces, "2 li"},
		{100, 1, 0, "3.52778506279"},
	};
	for (const auto& t : texts) {
		std::string text = MeasureText(t.points, t.unit, t.flags, units);
		Check(text == t.text, std::to_string(t.points) + " -> \"" + text + "\", expected \""
			+ t.text + "\"");
	}
	struct { const char* text; int unit; bool lines; bool negative; bool ok; float points; }
		parses[] = {
		{"1.5", 0, false, false, true, 108},
		{"2.54 cm", 0, false, false, true, 71.9998550415f},
		{"25mm", 0, false, false, true, 70.8659973145f},
		{"6 pt", 0, false, false, true, 6},
		{"2 li", 4, false, false, true, 2},	// lines not allowed: points
		{"2 li", 4, true, false, true, 32},
		{"-1", 0, false, false, false, 0},
		{"-1", 0, false, true, true, -72},
		{"abc", 0, false, false, false, 0},
	};
	for (const auto& p : parses) {
		float points = 0;
		bool ok = ParseMeasure(p.text, p.unit, units, p.lines, p.negative, points);
		Check(ok == p.ok && (!ok || std::fabs(points - p.points) < 1e-4f),
			std::string("\"") + p.text + "\" -> " + (ok ? std::to_string(points) : "no"));
	}
	printf("measure ok %d, failed %d\n", sOk, sFailed);
	return sFailed > 0 ? 1 : 0;
}
