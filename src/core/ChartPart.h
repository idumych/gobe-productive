// The chart part (Parts/Chart: TChartPart, a TPartGR subclass): the graphics
// part data, then 'cht+', TDataAccessor ('daa+' ... 'daa-': the linked data
// range, series and category names and the data cached as rows of floats),
// the chart type and the chart (legend, axes, 3D renderer: not read yet).
#ifndef GOBE_CHART_PART_H
#define GOBE_CHART_PART_H

#include <cstdint>
#include <functional>
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
// `mode` is the data's preference (TDataAccessor +0x40: 0 and 1 by the
// range's shape, 2 series in rows, 3 in columns; CalcIsRowSeries), and
// `seriesInRows` gets what it came to (+0x4d), `label` the data's label
// (+0x14: the corner cell's text when it has both names, "No Data to
// Chart", STR# 1 string 16, without values).
ChartData ChartDataOf(const Document& doc, size_t part, const SSRange& range,
	uint32_t mode = 0, bool* seriesInRows = nullptr, std::string* label = nullptr);

// the cells chart part `chart` shows (its TLink: the spreadsheet part and
// its reference's range); false if it has none
bool ChartCells(const Document& doc, size_t chart, size_t& sheet, SSRange& range);
// TDataAccessor's +0x4d and +0x14 for chart part `chart`, from its cells
bool ChartSeriesInRows(const Document& doc, size_t chart);
std::string ChartLabel(const Document& doc, size_t chart);

// THideShowPieceCmd: the chart's piece of `kind` ('Lgnd', 'Titl') shown
// or hidden (TChartPiece +0x30 bit 0); a title it doesn't have made first
// (TChartPart::InsertNewPiece, TChartFrame::FixupPiecePositions: the
// data's label, as wide as `measure` says plus 4, centered). False if
// nothing changed.
typedef std::function<void(const std::string& text, uint32_t style, float& width,
	float& height)> TextMeasure;
bool ShowChartPiece(Document& doc, size_t chart, uint32_t kind, bool show,
	const TextMeasure& measure);

// TToggleSeriesOrderCmd: the data taken the other way (+0x40 2 or 3) and
// read again from the cells (TDataAccessor::DataChanged), the chart's
// series following (their names; the styles they had, new ones as
// MakeChart makes them). False if the chart has no cells.
bool ToggleChartSeriesOrder(Document& doc, size_t chart);

// Make Chart (the PartBar's button and the Options menu: 's00T' with Kind
// 'chrt', TDocWindow::DoInsertFrame): a new chart part (TChartPart::
// TChartPart(TDoc*): a 2D bar chart, T2DXYAxisChart kind 0, its pieces,
// styles, axes ranged by T2DXYAxisChart::FindMinMax) of `range`'s values,
// linked to the cells (an unnamed reference in the spreadsheet, a TLink),
// framed in the sheet's drawing (400 by 200, TChartPart::DefaultFrameRect)
// in the middle of what the window shows (`center`, the drawing's point
// there; without it, right of the last column with anything in it, level
// with the cells), which gets a FRAM shape for it. Its pieces where the
// original puts them: the legend at 5, 5, as wide as its widest series name
// (`measure`) and 21 more (TLegendPiece::FixSize), the plot 12 right of the
// legend as it was before it had series and 12 from the frame's right, 6
// from its top and bottom (TChartFrame::SetDefaultRenderPiecePos), the data's
// label as a title (TChartFrame::FixupPiecePositions: centered, 4 down, its
// text's size and 4 more). The chart part's index, or -1 with `error`.
int MakeChart(Document& doc, size_t part, const SSRange& range, std::string& error,
	const std::function<void(const std::string&, uint32_t, float&, float&)>& measure = nullptr,
	const float* center = nullptr);

} // namespace gobe

#endif
