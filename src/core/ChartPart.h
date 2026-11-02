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

#include "DocSections.h"
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
	// T2DXYChart ('2DXY'): the axes above (T2DXAxis, T2DVerticalAxis), its
	// minor type (scatter 0, line 1) and how many series it gave their
	// symbols (field 0x7c)
	uint32_t xyMinor = 0;
	uint32_t xyStyled = 0;
	// other chart types: what their Write adds after TChart's, as written
	std::vector<uint8_t> chartBody;
	// TChartPart::Write's last two u32 (format >= 0x11)
	uint32_t partValues[2] = {};
	// where 'cht+' (after the graphics) starts and the closing 'cht-' ends
	size_t sectionStart = 0, sectionEnd = 0;

	// the cached value of physical series s (a column or row of the
	// cells) at category c (data is series-major)
	float Value(size_t s, size_t c) const;
	size_t Categories() const { return data.values[0]; }
	// TDataAccessor::CalcLogicalSeries: the chart's series by its data's
	// format (+0x44): each a physical series (0), an x and a y one (1:
	// T2DXYChart), or y ones sharing the first's x (2)
	size_t SeriesCount() const;
	// GetPhysicalSeries: the physical series of series s's value `which`
	// (0 x, 1 y)
	size_t PhysicalSeries(size_t s, int which) const;
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
bool ToggleChartSeriesOrder(Document& doc, size_t chart, const TextMeasure& measure = nullptr);

// T2DXYAxisChart::DataChanged: series past the data's taken out of their
// sets (a set left empty dropped), those in no set added to the last (an
// area set's each to a new area set), the sets made when there are none
// (CreateSets: an area chart's series a set each, the others' one set of
// them all); then FindMinMax: the vertical axis from the values (and 0)
// and a stacked area set's sums, the horizontal from the categories (an
// axis given its range keeps it). Only a 2DSM chart's.
void ChartSeriesChanged(ChartPart& chart);

// TAxis::GetRange: the axis's nice range (0 to 0.00001 when it has none);
// TAxis::TickSpacing: its step, or for one without, a nice quarter of its
// data's nice range
void ChartAxisRange(const ChartAxis& axis, float& low, float& high);
float ChartAxisStep(const ChartAxis& axis);

// T2DXYAxisChart::FindMinMax: the vertical axis from the values (and 0)
// and a stacked area set's sums, the horizontal from the categories
void ChartFindMinMax(ChartPart& chart);

// TFormatSeriesCmd: series `series`' style with `property` (its fill
// 'FCol', its symbol 'chSM', its symbol's color 'chSC', ...)
bool FormatChartSeries(Document& doc, size_t chart, size_t series, const Property& property);
// TSetSeriesTypeCmd (T2DXYAxisChart::ChangeSeriesType): the series taken
// from its set (a set left empty dropped) into a set of `type` (bar 0, line
// 1, area 2, stacked area 3), the first of that type or a new one (an area
// set has one series)
bool ChangeChartSeriesType(Document& doc, size_t chart, uint32_t series, uint32_t type);
// TChangeSeriesLayerCmd (ChangeSeriesSetOrder): the series' set a place
// later (`direction` > 0: drawn over the next) or earlier; false at an end
bool ChangeChartSeriesOrder(Document& doc, size_t chart, uint32_t series, int direction);

// the chart's type as the Chart Type dialog shows it: its major (Bar, Pie,
// Line/Area, XY) and minor type (TChartPart +0xb4, +0xb8)
void ChartTypeOf(const ChartPart& chart, uint32_t& major, uint32_t& minor);

// TDataAccessor::SetDataFormat, CalcLogicalSeries: the data's format
// (+0x44) and its counts of series and of values a point
void SetChartDataFormat(ChartData& data, uint32_t format);
// TChart::DataChanged: the legend's series as many as the data's, named
// from it, keeping their styles (more as MakeChart makes them)
void SyncChartSeries(Document& doc, ChartPart& chart);
// T2DXYChart::SetLegendAttributes for the series not given theirs yet: a
// scatter chart's a symbol and color each, no line; a line chart's a line
void StyleXYSeries(Document& doc, ChartPart& chart);

// TChangeChartTypeCmd::Perform: the chart made again as the type (major,
// minor) from what it shows (its legend; new axes; its series sets or
// symbols made for the type; its data read in the type's format) and its
// camera placed. The 2D types so far: bar (0, 0), line, area, stacked
// area (2, 0 to 2), XY scatter and line (3, 0 and 1); false for the
// others or a chart that isn't 2D.
bool ChangeChartType(Document& doc, size_t chart, uint32_t major, uint32_t minor,
	const TextMeasure& measure = nullptr);

// TLegendPiece::FixSize (TChartPart::DataChanged): the legend as many
// series high (TChartLegend: 12 each, 4 more) and its widest series' name
// (each in its series' style) 21 wider; without `measure`, only its height
void FixChartLegend(ChartPart& chart, const TextMeasure& measure);

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
