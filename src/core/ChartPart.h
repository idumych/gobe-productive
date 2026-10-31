// The chart part (Parts/Chart: TChartPart, a TPartGR subclass): the graphics
// part data, then 'cht+', TDataAccessor ('daa+' ... 'daa-': the linked data
// range, series and category names and the data cached as rows of floats),
// the chart type and the chart (legend, axes, 3D renderer: not read yet).
#ifndef GOBE_CHART_PART_H
#define GOBE_CHART_PART_H

#include <cstdint>
#include <string>
#include <vector>

#include "GRPart.h"

namespace gobe {

struct ChartData {
	uint32_t link = 0xffffffff;		// TLink*: the data's source
	uint32_t values[6] = {};
	std::vector<std::string> names1;	// +0x18
	std::vector<std::string> names2;	// +0x2c
	uint32_t value40 = 0, value44 = 0;
	bool flag4c = false;
	std::vector<std::vector<float>> table;	// TZTable: rows of values
};

// TAxis::Write: 'axi+', seven values (the range, steps and their kinds,
// as written), the axis style, two flags (labels, ...), 'axi-'
struct ChartAxis {
	uint32_t values[7] = {};
	uint32_t style = 0;
	bool flags[2] = {true, true};
};

// a T2DXYAxisChart series set: its kind (T2DBarSeries 0, T2DLineSeries 1,
// T2DAreaSeries 2, T2DStackedAreaSeries 3) and its series (indices)
struct ChartSeriesSet {
	uint32_t kind = 0;
	std::vector<uint32_t> series;
};

// TChartLegendSeries: 'ser+' name, shown, style 'ser-'
struct ChartSeries {
	std::string name;
	bool shown = true;
	uint32_t style = 0xffffffff;
};

struct ChartPart {
	GRPart graphics;
	ChartData data;
	uint32_t chartType = 0;			// NewChart's kind: 'BAR3', '2DLM', ...
	// TChart::Read: 'cht+', legend ('lgn+' rect, series, 'lgn-'), ...
	float legendRect[4] = {0, 0, -1, -1};
	std::vector<ChartSeries> series;
	// the rest of TChart::Write: a u32, the surface labels (kept as
	// written), the flags (field 0x20)
	uint32_t chartValue8 = 0;
	std::vector<uint8_t> surfaceLabels;	// the table: its count and items
	uint32_t chartFlags = 0;
	// T2DXYAxisChart ('2DSM'): the horizontal and vertical axes, the kind
	// of series new sets get (field 0x78), the series sets
	ChartAxis axes[2];
	uint32_t seriesKind = 0;
	std::vector<ChartSeriesSet> sets;
	// other chart types: what their Write adds after TChart's, as written
	std::vector<uint8_t> chartBody;
	// TChartPart::Write's last two u32 (format >= 0x11)
	uint32_t partValues[2] = {};
	// where 'cht+' (after the graphics) starts and the closing 'cht-' ends
	size_t sectionStart = 0, sectionEnd = 0;

	// the cached value of series s at category c (data is series-major)
	float Value(size_t s, size_t c) const;
	size_t Categories() const { return data.values[0]; }
	size_t SeriesCount() const { return data.values[1]; }
};

// `a` is positioned at the part record; reads the graphics and the chart
// up to the part's closing 'cht-'.
void ReadChartPart(DocArchive& a, ChartPart& out);

// TChartPart::Write after TPartGR::Write: 'cht+', the TDataAccessor, the
// chart type and the chart (TChart, then a 2DSM chart's axes and series
// sets or another type's body as read), the two u32, 'cht-'.
std::vector<uint8_t> WriteChartSection(const ChartPart& part, int fileFormat);

} // namespace gobe

#endif
