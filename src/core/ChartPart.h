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
#include "SSPart.h"

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

struct Document;

// TDataAccessor::DataChanged for `range` of spreadsheet part `part`:
// categories down the rows unless the range is wider than tall (series
// across), a first row or column more than half not numbers taken as the
// names (the first category's labels name the series, the first series'
// the categories), the values cached series-major (0 for what isn't a
// number); counts as TDataAccessor::Write writes them.
ChartData ChartDataOf(const Document& doc, size_t part, const SSRange& range);

// Make Chart (the PartBar's button and the Options menu: 's00T' with Kind
// 'chrt', TDocWindow::DoInsertFrame): a new chart part (TChartPart::
// TChartPart(TDoc*): a 2D bar chart, T2DXYAxisChart kind 0, its pieces,
// styles, axes ranged by T2DXYAxisChart::FindMinMax) of `range`'s values,
// linked to the cells (an unnamed reference in the spreadsheet, a TLink),
// framed in the sheet's drawing right of the last column with anything in
// it, level with the cells (320 x 200: the port's place and size),
// which gets a FRAM shape for it. The chart part's index, or -1 with
// `error`.
int MakeChart(Document& doc, size_t part, const SSRange& range, std::string& error);

} // namespace gobe

#endif
