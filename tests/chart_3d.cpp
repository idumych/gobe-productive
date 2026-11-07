// chart_3d: the 3D charts' model (T3DXYAxisChart, T3DPieChart, T3DRenderer).
// A linked 2D chart changed to a 3D bar, pie and 3D area chart and back: its
// body (axes with their depth, explosions), camera and flags as
// TChangeChartTypeCmd::Perform leaves them, written and read back; the
// camera's moves; a slice's arc. The expected camera bits are the original's
// (TChangeChartTypeCmd::Perform), the arc's from python3 in the comments.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "ChartPart.h"
#include "Document.h"

using namespace gobe;

static int failures = 0, checks = 0;

static void
Check(bool ok, const char* what)
{
	checks++;
	if (!ok) {
		printf("FAIL: %s\n", what);
		failures++;
	}
}


static bool
Near(float a, float b, float within = 1e-4f)
{
	return std::fabs(a - b) <= within;
}


int
main(int argc, char** argv)
{
	// a slice: the whole circle has 4 quarters of 13 steps (and the last point)
	std::vector<std::array<float, 2>> whole = ChartArcPoints(3500, 0, 360);
	Check(whole.size() >= 50 && whole.size() <= 56, "the whole circle's points");
	for (const auto& p : whole)
		Check(Near(std::hypot(p[0], p[1]), 3500, 60), "each point on the circle");
	std::vector<std::array<float, 2>> quarter = ChartArcPoints(3500, 0, 90);
	Check(quarter.size() >= 13 && quarter.size() <= 15, "a quarter's points");
	Check(Near(quarter.front()[0], -3500, 1) && Near(quarter.front()[1], 0, 1),
		"a slice starts at the left");
	Check(Near(quarter.back()[0], 0, 80) && Near(quarter.back()[1], -3500, 80),
		"a quarter ends at the bottom");

	// the camera's moves: from (1, 0, 0) looking at the origin, up y
	ChartCamera camera;
	camera.position[0] = 1;
	float distance = ChartCameraDistance(camera);
	Check(Near(distance, 1), "distance");
	ChartCamera moved = camera;
	MoveChartCamera(moved, 0, 100, 0, distance);	// (dx - dy) * 0.005 toward what it looks at
	Check(Near(moved.position[0], 0.5f), "dolly");
	moved = camera;
	MoveChartCamera(moved, 1, 100, 0, distance);	// 0.5 along view x up: (0, 0, -1) * ...
	Check(Near(moved.position[2], moved.look[2]) && !Near(moved.position[2], 0),
		"pan takes what it looks at along");
	moved = camera;
	MoveChartCamera(moved, 2, 100, 0, distance);	// around, the same distance from the origin
	Check(Near(ChartCameraDistance(moved), distance), "orbit keeps the distance");
	moved = camera;
	Check(ChartCameraKey(moved, 0x1d) && Near(moved.position[0], 1.02f), "right arrow");
	Check(ChartCameraKey(moved, 'z') && Near(moved.light[2], 0.52f), "z moves the light");
	Check(!ChartCameraKey(moved, 'q'), "other keys left alone");

	int charts = 0;
	for (int i = 1; i < argc; i++) {
		Document doc;
		std::string error;
		if (!LoadDocument(argv[i], doc, error))
			continue;
		for (auto& entry : doc.chartParts) {
			size_t part = entry.first;
			if (doc.Chart(part)->SeriesCount() == 0)
				continue;
			charts++;
			Check(ChangeChartType(doc, part, 0, 1), "to a 3D bar chart");
			const ChartPart* chart = doc.Chart(part);
			Check(chart->chartType == Sig("BAR3") && chart->chartFlags == 3, "a BAR3's type, flags");
			Check(Near(chart->axisDepth[1], float(chart->SeriesCount())), "as deep as its series");
			ChartCamera at;
			if (ReadChartCamera(*chart, at)) {
				uint32_t bits;
				memcpy(&bits, &at.position[0], 4);
				Check(bits == 0x3f79a7c1, "the camera a 2D chart gets");
			}
			Check(ChangeChartType(doc, part, 2, 3), "to a 3D area chart");
			chart = doc.Chart(part);
			Check(chart->chartType == Sig("ARE3"), "an ARE3");
			Check(ChangeChartType(doc, part, 1, 0), "to a pie");
			chart = doc.Chart(part);
			Check(chart->chartType == Sig("PIE3") && chart->chartFlags == 5, "a PIE3's type, flags");
			Check(chart->explosions.size() == chart->SeriesCount(), "an explosion for each series");
			if (ReadChartCamera(*chart, at)) {
				uint32_t bits;
				memcpy(&bits, &at.position[0], 4);
				Check(bits == 0x3b9f6230, "the pie's camera");
			}
			// saved and read again as the type it is
			std::string path = std::string(getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp")
				+ "/chart_3d.gobe";
			if (doc.Save(path, error)) {
				Document again;
				Check(LoadDocument(path, again, error), "saved pie reads back");
				const ChartPart* back = again.Chart(part);
				Check(back != nullptr && back->chartType == Sig("PIE3")
					&& back->explosions.size() == chart->explosions.size(), "pie after saving");
			}
			Check(ChangeChartType(doc, part, 0, 0), "back to 2D bars");
			chart = doc.Chart(part);
			Check(chart->chartType == Sig("2DSM") && !chart->sets.empty(), "2D bars again");
			break;
		}
	}
	Check(charts > 0, "charts in the documents");
	printf("chart_3d: %d checks, %d charts, %d failures\n", checks, charts, failures);
	return failures == 0 ? 0 : 1;
}
