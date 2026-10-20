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

	// the cached value of series s at category c (data is series-major)
	float Value(size_t s, size_t c) const;
	size_t Categories() const { return data.values[0]; }
	size_t SeriesCount() const { return data.values[1]; }
};

// `a` is positioned at the part record; reads through the legend.
void ReadChartPart(DocArchive& a, ChartPart& out);

} // namespace gobe

#endif
