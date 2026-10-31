// Make Chart: a chart of a spreadsheet's selected cells (TDocWindow::
// DoInsertFrame with Kind 'chrt', TChartPart::TChartPart(TDoc*),
// TDataAccessor::DataChanged, T2DXYAxisChart::FindMinMax).
#include "ChartPart.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "DocWriter.h"
#include "Document.h"
#include "Editing.h"
#include "PartTail.h"
#include "SSEval.h"
#include "StyleEdit.h"

namespace gobe {

namespace {

// TChartLegend::ChartDefaultColors: the series' fill colors
const uint8_t kSeriesColors[16][3] = {
	{0xff, 0x00, 0x00}, {0x00, 0xff, 0x00}, {0x00, 0x00, 0xff}, {0xff, 0x00, 0xff},
	{0xff, 0xff, 0x00}, {0x00, 0x00, 0x00}, {0xff, 0xaf, 0x13}, {0xff, 0x80, 0x80},
	{0x80, 0xff, 0x80}, {0x80, 0x80, 0xff}, {0xcb, 0x66, 0xff}, {0xff, 0xff, 0x98},
	{0x80, 0x00, 0x00}, {0x00, 0x80, 0x00}, {0x00, 0x00, 0x80}, {0x80, 0x00, 0xff}
};


Property
FloatProperty(const char (&type)[5], float value)
{
	DocWriter w;
	w.Float(value);
	return Property{Sig(type), w.Data()};
}


Property
ShortProperty(const char (&type)[5], uint16_t value)
{
	DocWriter w;
	w.UInt16(value);
	return Property{Sig(type), w.Data()};
}


// a solid ink (TPenInk: on, the color, the low color 'wtw', a solid
// pattern), as the chart's styles have them
Property
InkProperty(const char (&type)[5], uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
	std::vector<uint8_t> data(17, 0xff);
	const uint8_t head[9] = {1, r, g, b, a, 0x77, 0x74, 0x77, 0};
	std::copy(head, head + 9, data.begin());
	return Property{Sig(type), data};
}


uint32_t
Bits(float value)
{
	uint32_t bits;
	std::memcpy(&bits, &value, 4);
	return bits;
}


// TAxis::NiceNum (Heckbert's): 1, 2, 5 or 10 times a power of ten
float
NiceNum(float x, bool round)
{
	if (!(x > 0))
		return 0;
	double exponent = std::floor(std::log10(double(x)));
	double fraction = double(x) / std::pow(10.0, exponent);
	double nice;
	if (round)
		nice = fraction < 1.5 ? 1 : fraction < 3 ? 2 : fraction < 7 ? 5 : 10;
	else
		nice = fraction <= 1 ? 1 : fraction <= 2 ? 2 : fraction <= 5 ? 5 : 10;
	return float(nice * std::pow(10.0, exponent));
}


// TAxis::SetRange: the range, the tick count (5 for -1), a nice range
// and step, and the step's multiples around the range; written as TAxis::
// Write orders them (min, max, nice min, nice max, range, step, ticks)
void
SetRange(ChartAxis& axis, float low, float high, int ticks)
{
	float range = NiceNum(high - low, false);
	float step = NiceNum(ticks > 1 ? range / float(ticks - 1) : range, high - low > 1);
	float niceLow = 0, niceHigh = 0;
	if (step > 0) {
		niceLow = float(std::floor(low / step)) * step;
		niceHigh = float(std::ceil(high / step)) * step;
	}
	axis.values[0] = Bits(low);
	axis.values[1] = Bits(high);
	axis.values[2] = Bits(niceLow);
	axis.values[3] = Bits(niceHigh);
	axis.values[4] = Bits(range);
	axis.values[5] = Bits(step);
	axis.values[6] = uint32_t(ticks < 0 ? 5 : ticks);
}


std::string
ValueText(const SSValue& v)
{
	if (v.type == SSValue::kNumber) {
		char text[64];
		snprintf(text, sizeof text, "%g", v.number);
		return text;
	}
	return v.text;
}

} // namespace


ChartData
ChartDataOf(const Document& doc, size_t part, const SSRange& range)
{
	ChartData d;
	const SSPart* ss = doc.SpreadsheetPart(part);
	if (ss == nullptr)
		return d;
	SSBook book(doc);
	SSEvaluator eval(*ss, &book, int(part));
	int rows = range.to.a - range.from.a + 1, columns = range.to.b - range.from.b + 1;
	auto value = [&](int row, int column) {
		return eval.Value(uint16_t(range.from.a + row), uint16_t(range.from.b + column));
	};
	auto number = [&](int row, int column) { return value(row, column).type == SSValue::kNumber; };
	// CalcIsRowSeries (no preference): categories down the rows unless
	// the range is wider than tall
	bool rowSeries = columns <= rows;
	int textInColumn = 0, textInRow = 0;
	for (int r = 0; r < rows; r++)
		textInColumn += !number(r, 0);
	for (int c = 0; c < columns; c++)
		textInRow += !number(0, c);
	bool columnLabels = rows / 2 < textInColumn, rowLabels = columns / 2 < textInRow;
	int categories = rowSeries ? rows : columns, series = rowSeries ? columns : rows;
	// labels in the first series hold the categories' names, labels in the
	// first category the series' names
	bool seriesSkip = rowSeries ? columnLabels : rowLabels;
	bool categorySkip = rowSeries ? rowLabels : columnLabels;
	auto at = [&](int category, int s) {
		return rowSeries ? value(category, s) : value(s, category);
	};
	if (categorySkip)
		for (int s = seriesSkip; s < series; s++)
			d.names2.push_back(ValueText(at(0, s)));
	if (seriesSkip)
		for (int c = categorySkip; c < categories; c++)
			d.names1.push_back(ValueText(at(c, 0)));
	std::vector<float> table;
	for (int s = seriesSkip; s < series; s++)
		for (int c = categorySkip; c < categories; c++) {
			SSValue v = at(c, s);
			table.push_back(v.type == SSValue::kNumber ? float(v.number) : 0);
		}
	d.table.push_back(table);
	d.values[0] = uint32_t(categories - categorySkip);
	d.values[1] = uint32_t(series - seriesSkip);
	d.values[2] = d.values[1];
	d.values[3] = 1;
	d.values[4] = 1;
	d.flag4c = true;
	return d;
}


int
MakeChart(Document& doc, size_t part, const SSRange& range, std::string& error)
{
	const SSPart* ss = doc.SpreadsheetPart(part);
	if (ss == nullptr || part >= doc.tails.size() || !doc.tailRead[part]) {
		error = "not a spreadsheet";
		return -1;
	}
	// the sheet's drawing: the chart's frame goes in it
	size_t drawing = kNoPart;
	for (const LeafSheet& sheet : doc.sheets.sheets)
		if (sheet.mainPart == part)
			drawing = sheet.graphicsPart;
	auto graphics = std::find_if(doc.graphicsParts.begin(), doc.graphicsParts.end(),
		[&](const std::pair<size_t, GRPart>& g) { return g.first == drawing; });
	if (graphics == doc.graphicsParts.end() || drawing >= doc.tails.size()) {
		error = "the sheet has no drawing";
		return -1;
	}
	ChartData data = ChartDataOf(doc, part, range);
	if (data.values[0] == 0 || data.values[1] == 0) {
		error = "no values to chart";
		return -1;
	}
	const int format = doc.file.header.version.values[2];
	const int major = doc.file.header.version.values[0];
	size_t chart = doc.parts.parts.size();
	uint32_t series = data.values[1];

	// the styles (TStyleManager::NewStyle, each used once more): the
	// frame's (no fill, a hairline pen), the axes' and the pieces' based
	// on "GR Default", each series' (TChartLegend: its fill, a black
	// symbol, symbol 0 at size 6)
	uint32_t base = FindNamedStyle(doc.styles, "GR Default");
	uint32_t plain = FindNamedStyle(doc.styles, "Default");
	if (base >= doc.styles.styles.size())
		base = plain;
	auto style = [&](uint32_t parent, const std::vector<Property>& properties) {
		uint32_t s = doc.NewStyle(Sig("Basi"), "", 0x20, parent, properties);
		doc.styles.styles[s].value4++;
		return s;
	};
	uint32_t frameStyle = style(plain, {FloatProperty("FTra", 1), FloatProperty("PENz", 0),
		FloatProperty("PTra", 1)});
	uint32_t axisStyles[2] = {style(base, {}), style(base, {})};
	uint32_t renderStyle = style(base, {FloatProperty("PTra", 0)});
	uint32_t legendStyle = style(base, {ShortProperty("TSiz", 10)});
	std::vector<uint32_t> seriesStyles;
	for (uint32_t i = 0; i < series; i++) {
		const uint8_t* c = kSeriesColors[i % 16];
		seriesStyles.push_back(style(base, {InkProperty("FCol", c[0], c[1], c[2], 0),
			InkProperty("chSC", 0, 0, 0, 0xff), ShortProperty("chSM", 0),
			ShortProperty("chSS", 6)}));
	}

	// the cells: an unnamed reference to a selection of them in the
	// spreadsheet (TReference), and the link from it to the chart
	uint32_t cellSelection = uint32_t(doc.selections.selections.size());
	{
		DocWriter body;
		body.Sig(Sig("SPRD"));
		body.UInt32(uint32_t(part));
		body.UInt32(cellSelection);
		body.Sig(Sig("CELL"));
		body.UInt16(range.from.a);
		body.UInt16(range.from.b);
		body.UInt32(1);
		body.UInt16(range.from.a);
		body.UInt16(range.from.b);
		body.UInt16(range.to.a);
		body.UInt16(range.to.b);
		body.Bool(true);
		DocWriter record;
		record.UInt32(uint32_t(body.Data().size()));
		record.Bytes(body.Data());
		doc.addedSelections.push_back(record.Data());
		Selection s;
		s.id = cellSelection;
		s.kind = Sig("SPRD");
		s.part = uint32_t(part);
		s.value = cellSelection;
		s.cell[0] = range.from.a;
		s.cell[1] = range.from.b;
		s.ranges.push_back({range.from.a, range.from.b, range.to.a, range.to.b});
		s.hasCell = true;
		doc.selections.selections.push_back(s);
	}
	PartTail& cells = doc.tails[part];
	Reference reference;
	reference.value14 = cells.refNext++;
	reference.selection = cellSelection;
	reference.valueC = uint32_t(cells.refs.size());
	reference.value10 = 1;
	cells.refs.push_back(reference);
	cells.refsChanged = true;
	doc.dirtyTails.insert(part);
	data.link = LinkFor(doc, part, cells.refs.size() - 1, chart);
	doc.linksChanged = true;

	// the chart's selection (a TGRSelection: no shapes)
	uint32_t chartSelection = uint32_t(doc.selections.selections.size());
	{
		DocWriter record;
		record.UInt32(16);
		record.Sig(Sig("chrt"));
		record.UInt32(uint32_t(chart));
		record.UInt32(chartSelection);
		record.UInt32(0);
		doc.addedSelections.push_back(record.Data());
		Selection s;
		s.id = chartSelection;
		s.kind = Sig("chrt");
		s.part = uint32_t(chart);
		s.value = chartSelection;
		doc.selections.selections.push_back(s);
	}

	// where: right of the last column with anything in it, level with the
	// cells, 320 x 200 (the port's choice; the original asks the drawing,
	// TChartPart::DefaultFrameRect)
	const float width = 320, height = 200;
	uint16_t last = range.to.b;
	for (const SSCell& c : ss->cells)
		if (c.id.b != 0xffff)
			last = std::max(last, c.id.b);
	float origin[2] = {20, 0};
	for (uint16_t c = 0; c <= last; c++)
		origin[0] += ss->ColumnWidth(c);
	for (uint16_t r = 0; r < range.from.a; r++)
		origin[1] += ss->RowHeight(r);

	// TChartPart(TDoc*): TPartGR with a 32767-point drawing, the pieces
	// 'Rndr' (T3DRenderer: the camera, one light at (0, 0, 0.5)) and
	// 'Lgnd'; their places as TChartFrame::SetDefaultRenderPiecePos puts
	// them: the legend at the right, the plot left of it
	ChartPart model;
	GRPart& gr = model.graphics;
	gr.gridSpacing = 9;
	gr.size[0] = gr.size[1] = 32767;
	float legendHeight = float(series * 12 + 4);
	Shape legend;
	legend.kind = Sig("Lgnd");
	legend.size[0] = 106;
	legend.size[1] = legendHeight;
	legend.origin[0] = width - legend.size[0] - 13;
	legend.origin[1] = std::max(5.0f, (height - legendHeight) / 2);
	legend.style = legendStyle;
	legend.pieceValue = 1;
	{
		DocWriter w;
		w.Sig(Sig("lgp+"));
		w.Sig(Sig("lgp-"));
		legend.pieceData = w.Data();
	}
	Shape plot;
	plot.kind = Sig("Rndr");
	plot.origin[0] = 12;
	plot.origin[1] = 10;
	plot.size[0] = legend.origin[0] - 6 - plot.origin[0];
	plot.size[1] = height - 20;
	plot.style = renderStyle;
	plot.pieceValue = 1;
	{
		DocWriter w;
		w.Sig(Sig("rnp+"));
		const float camera[9] = {0, 0, 0, 0, 0, 0, 0, 1, 0};
		for (float v : camera)
			w.Float(v);
		const float light[9] = {0, 0, 0.5f, 0, 0, 0, 0, 0, 0};
		for (float v : light)
			w.Float(v);
		w.UInt32(0x4000);		// GL_LIGHT0
		w.Bool(false);
		w.Sig(Sig("rnp-"));
		plot.pieceData = w.Data();
	}
	GRLayer layer;
	layer.name = "Layer 1";
	layer.shapes = {plot, legend};
	gr.layers.push_back(layer);
	GRView view;
	view.name = "View 1";
	view.layers.push_back({0, {true, true, false, true, false}});
	gr.views.push_back(view);

	model.data = data;
	model.chartType = Sig("2DSM");
	model.legendRect[0] = model.legendRect[1] = 0;
	model.legendRect[2] = 100;
	model.legendRect[3] = legendHeight;
	for (uint32_t i = 0; i < series; i++) {
		ChartSeries s;
		s.name = i < data.names2.size() ? data.names2[i] : "Series " + std::to_string(i + 1);
		s.style = seriesStyles[i];
		model.series.push_back(s);
	}
	model.chartFlags = 0x8002;
	// FindMinMax: the values from 0 (bars stack nothing), the categories
	float low = 0, high = 0;
	for (float v : data.table[0]) {
		low = std::min(low, v);
		high = std::max(high, v);
	}
	SetRange(model.axes[0], 0, float(data.values[0]), int(data.values[0]));
	model.axes[0].style = axisStyles[0];
	model.axes[0].flags[1] = false;
	SetRange(model.axes[1], low, high, -1);
	model.axes[1].style = axisStyles[1];
	model.seriesKind = 0;
	ChartSeriesSet set;
	for (uint32_t i = 0; i < series; i++)
		set.series.push_back(i);
	model.sets.push_back(set);

	// the record: TPart (level 0, a new id, its selection), the drawing
	// data, the chart, its frame in the sheet's drawing (TChartFrame)
	DocWriter w;
	w.Sig(Sig("PART"));
	w.Sig(Sig("chrt"));
	w.UInt32(0);
	w.UInt32(0);
	w.UInt32(doc.parts.value14++);		// TPartMgr::NewPartID
	w.UInt32(0xffffffff);
	w.UInt32(1);
	w.UInt32(chartSelection);
	w.Bytes(WriteGRData(gr, format, major));
	w.Bytes(WriteChartSection(model, format));
	w.Sig(Sig("REFT"));
	w.UInt32(1);
	w.UInt32(0);
	w.Sig(Sig("RefT"));
	w.UInt32(1);
	w.Sig(Sig("PORT"));
	w.UInt32(uint32_t(drawing));
	w.UInt32(0);
	w.UInt32(0);
	w.UInt32(1);
	w.UInt32(1);
	w.Sig(Sig("FRAM"));
	w.Float(0);
	w.Point(origin);
	w.Float(width);
	w.Float(height);
	w.UInt32(frameStyle);
	for (int i = 0; i < 3; i++)
		w.UInt32(0);
	w.UInt32(uint32_t(chart));
	w.UInt32(uint32_t(drawing));
	w.UInt32(0);
	w.UInt32(0);
	w.Sig(Sig("PENz"));			// (as the original's chart frames have it)
	w.UInt32(0);
	const float shown[4] = {0, 0, width, height};
	w.Rect(shown);				// TFrameGR: the drawing's rectangle shown
	w.Sig(Sig("FraM"));
	w.UInt32(0);				// TGRPortal: view 0
	w.Sig(Sig("PorT"));
	w.Sig(Sig("ParT"));

	DocArchive versions(std::vector<uint8_t>{});
	versions.SetVersions(major, doc.file.header.version.values[3], format);
	PartRecord record{Sig("chrt"), 0, w.Data()};
	ChartPart parsed;
	PartTail tail;
	try {
		DocArchive a(record.data);
		a.CopyVersions(versions);
		ReadChartPart(a, parsed);
	} catch (const ArchiveError& x) {
		error = std::string("new chart: ") + x.what();
		return -1;
	}
	if (!ReadPartTail(record.data, record.kind, versions, tail)) {
		error = "new chart: its frame";
		return -1;
	}
	doc.parts.parts.push_back(std::move(record));
	doc.chartParts.emplace_back(chart, std::move(parsed));
	doc.tails.push_back(std::move(tail));
	doc.tailRead.push_back(true);
	doc.dirtyParts.insert(chart);
	doc.dirtyTails.insert(chart);

	// the drawing gets a FRAM shape for it (TPartGR::InsertChildFrame)
	GRPart& sheetDrawing = graphics->second;
	if (sheetDrawing.layers.empty())
		sheetDrawing.layers.push_back({"Layer 1", {}});
	Shape frame;
	frame.kind = Sig("FRAM");
	frame.frame[0] = uint32_t(chart);
	frame.frame[1] = 0;
	frame.frame[2] = 0;
	sheetDrawing.layers.back().shapes.push_back(frame);
	std::vector<uint8_t>& drawingRecord = doc.parts.parts[drawing].data;
	std::vector<uint8_t> written = WriteGRData(sheetDrawing, format, major);
	int64_t shift = int64_t(written.size())
		- int64_t(sheetDrawing.dataEnd - sheetDrawing.dataStart);
	drawingRecord.erase(drawingRecord.begin() + long(sheetDrawing.dataStart),
		drawingRecord.begin() + long(sheetDrawing.dataEnd));
	drawingRecord.insert(drawingRecord.begin() + long(sheetDrawing.dataStart), written.begin(),
		written.end());
	sheetDrawing.dataEnd = size_t(int64_t(sheetDrawing.dataEnd) + shift);
	doc.tails[drawing].offset = size_t(int64_t(doc.tails[drawing].offset) + shift);
	doc.dirtyParts.insert(drawing);

	// a factory for charts if the document hasn't had one
	bool known = false;
	for (const PartFactoryEntry& f : doc.factories)
		known |= f.kind == Sig("chrt");
	if (!known) {
		doc.factories.push_back({Sig("chrt"), 1, "Chart Frame"});
		doc.factoriesChanged = true;
	}
	return int(chart);
}

} // namespace gobe
