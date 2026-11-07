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


float
AsFloat(uint32_t bits)
{
	float value;
	std::memcpy(&value, &bits, 4);
	return value;
}


// TAxis::NiceNum (Heckbert's): 1, 2, 5 or 10 times a power of ten
float
NiceNum(float x, bool round)
{
	// (none for an infinite one: a range over no steps, SetRange's one
	// tick)
	if (!(x > 0) || std::isinf(x))
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
	if (ticks < 0)
		ticks = 5;
	axis.values[0] = Bits(low);
	axis.values[1] = Bits(high);
	axis.values[6] = uint32_t(ticks);
	// (an axis given its range keeps it)
	if (!axis.flags[0])
		return;
	float range = NiceNum(high - low, false);
	float step = NiceNum(range / float(ticks - 1), high - low > 1);
	float niceLow = 0, niceHigh = 0;
	if (step > 0) {
		niceLow = float(std::floor(low / step)) * step;
		niceHigh = float(std::ceil(high / step)) * step;
	}
	axis.values[2] = Bits(niceLow);
	axis.values[3] = Bits(niceHigh);
	axis.values[4] = Bits(range);
	axis.values[5] = Bits(step);
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
ChartDataOf(const Document& doc, size_t part, const SSRange& range, uint32_t mode,
	bool* seriesInRows, std::string* label)
{
	ChartData d;
	d.value40 = mode;
	if (label != nullptr)
		label->clear();
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
	// CalcIsRowSeries: by the preference, else the range's shape (series in
	// rows unless it's taller than wide); rowSeries here is the other way:
	// the categories down the rows
	bool inRows = mode == 2 || (mode == 0 && rows <= columns) || (mode == 1 && columns < rows);
	if (seriesInRows != nullptr)
		*seriesInRows = inRows;
	bool rowSeries = !inRows;
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
	// (the label: the corner's text when both names are there)
	if (label != nullptr && categorySkip && seriesSkip)
		*label = ValueText(value(0, 0));
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
MakeChart(Document& doc, size_t part, const SSRange& range, std::string& error,
	const std::function<void(const std::string&, uint32_t, float&, float&)>& measure,
	const float* center)
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
	std::string label;
	ChartData data = ChartDataOf(doc, part, range, 0, nullptr, &label);
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
	// (the legend's and the title's: the axes' one, TStyleManager::
	// NewStyle finding it)
	uint32_t legendStyle = style(base, {});
	std::vector<uint32_t> seriesStyles;
	for (uint32_t i = 0; i < series; i++) {
		const uint8_t* c = kSeriesColors[i % 16];
		seriesStyles.push_back(style(base, {InkProperty("FCol", c[0], c[1], c[2], 0xff),
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
	// range's first row, 320 x 200 (the port's choice; the original asks
	// the drawing, TChartPart::DefaultFrameRect). The sheet's drawing lies
	// over its frame from the corner, so the headings, when shown, come
	// before the cells: the row headings' 36 points and the column
	// headings' row, as TSSFrame draws them.
	float width = 400, height = 200;
	uint16_t last = range.to.b, lastRow = range.to.a;
	for (const SSCell& c : ss->cells)
		if (c.id.b != 0xffff && c.id.a != 0xffff) {
			last = std::max(last, c.id.b);
			lastRow = std::max(lastRow, c.id.a);
		}
	const float headings[2] = {ss->options[4] ? 36.0f : 0.0f,
		ss->options[3] ? ss->defaultHeight : 0.0f};
	float origin[2] = {headings[0] + 20, headings[1]};
	for (uint16_t c = 0; c <= last; c++)
		origin[0] += ss->ColumnWidth(c);
	for (uint16_t r = 0; r < range.from.a; r++)
		origin[1] += ss->RowHeight(r);
	// within the drawing's frame (the sheet shows nothing past it): where
	// it doesn't fit at the right, below the last row with anything in it
	// (under the cells, not over them); where that doesn't fit either (a
	// sheet as large as its cells, as StockPrice's), moved left and up to
	// fit, and no larger than the frame
	for (size_t p = 0; p < doc.tails.size(); p++) {
		if (!doc.tailRead[p])
			continue;
		for (const Portal& portal : doc.tails[p].portals)
			for (const Frame& f : portal.frames)
				if (f.part == drawing && f.extent[0] > 0 && f.extent[1] > 0) {
					if (origin[0] + width > f.extent[0]) {
						float below = headings[1] + 20;
						for (uint16_t r = 0; r <= lastRow; r++)
							below += ss->RowHeight(r);
						if (below + height <= f.extent[1]) {
							origin[0] = headings[0] + 20;
							origin[1] = below;
						}
					}
					width = std::min(width, f.extent[0]);
					height = std::min(height, f.extent[1]);
					origin[0] = std::max(0.0f, std::min(origin[0], f.extent[0] - width));
					origin[1] = std::max(0.0f, std::min(origin[1], f.extent[1] - height));
					p = doc.tails.size() - 1;
				}
	}

	if (center != nullptr) {
		origin[0] = center[0] - width / 2;
		origin[1] = center[1] - height / 2;
	}
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
	// (TLegendPiece::FixSize: the widest series name, 21 more)
	float widest = 0;
	for (uint32_t i = 0; i < series && measure; i++) {
		std::string name = i < data.names2.size() ? data.names2[i]
			: "Series " + std::to_string(i + 1);
		float w = 0, h = 0;
		measure(name, legendStyle, w, h);
		widest = std::max(widest, w);
	}
	legend.size[0] = widest + 21;
	legend.size[1] = legendHeight;
	legend.origin[0] = 5;
	legend.origin[1] = 5;
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
	// (SetDefaultRenderPiecePos: 12 more than the legend is wide, to 12 from
	// the right, 6 from the top and bottom (a frame over 15 high); its
	// sides made whole, outwards: TRect::MakeIntSides)
	float left = legend.size[0] + 12, right = width - 12;
	if (right <= left)
		right = left + right + 1;
	float top = 0, bottom = height;
	if (bottom - top > 15) {
		top += 6;
		bottom -= 6;
	}
	plot.origin[0] = std::floor(left);
	plot.origin[1] = std::floor(top);
	plot.size[0] = std::ceil(right) - plot.origin[0];
	plot.size[1] = std::ceil(bottom) - plot.origin[1];
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
	if (!label.empty()) {
		// FixupPiecePositions: the label a title (TTitlePiece::fixup_text)
		Shape title;
		title.kind = Sig("Titl");
		title.style = legendStyle;
		title.text = label;
		float w = 0, h = 0;
		if (measure)
			measure(label, legendStyle, w, h);
		title.size[0] = w + 4;
		title.size[1] = h + 4;
		title.origin[0] = width * 0.5f - title.size[0] * 0.5f;
		title.origin[1] = 4;
		title.radius[0] = 2;
		title.radius[1] = h;
		title.pieceValue = 1;
		DocWriter w2;
		w2.Sig(Sig("tlp+"));
		w2.String(label);
		w2.Point(title.radius);
		w2.Sig(Sig("tlp-"));
		title.pieceData = w2.Data();
		layer.shapes.push_back(title);
	}
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


bool
ChartCells(const Document& doc, size_t chart, size_t& sheet, SSRange& range)
{
	const ChartPart* c = doc.Chart(chart);
	if (c == nullptr || c->data.link >= doc.links.links.size())
		return false;
	const Link& link = doc.links.links[c->data.link];
	if (link.sourcePart >= doc.tails.size() || doc.SpreadsheetPart(link.sourcePart) == nullptr)
		return false;
	const PartTail& tail = doc.tails[link.sourcePart];
	if (link.reference >= tail.refs.size())
		return false;
	const Selection* selection = doc.selections.Find(tail.refs[link.reference].selection);
	if (selection == nullptr || selection->ranges.empty())
		return false;
	const auto& r = selection->ranges[0];
	sheet = link.sourcePart;
	range.from = {r[0], r[1]};
	range.to = {r[2], r[3]};
	return true;
}


bool
ChartSeriesInRows(const Document& doc, size_t chart)
{
	size_t sheet;
	SSRange range;
	const ChartPart* c = doc.Chart(chart);
	if (c == nullptr || !ChartCells(doc, chart, sheet, range))
		return false;
	bool inRows = false;
	ChartDataOf(doc, sheet, range, c->data.value40, &inRows);
	return inRows;
}


std::string
ChartLabel(const Document& doc, size_t chart)
{
	size_t sheet;
	SSRange range;
	std::string label;
	const ChartPart* c = doc.Chart(chart);
	if (c != nullptr && ChartCells(doc, chart, sheet, range))
		ChartDataOf(doc, sheet, range, c->data.value40, nullptr, &label);
	return label;
}


namespace {

// the chart's piece of `kind`, nullptr if none (TChartPart::GetPiece)
Shape*
Piece(ChartPart& chart, uint32_t kind)
{
	for (GRLayer& layer : chart.graphics.layers)
		for (Shape& s : layer.shapes)
			if (s.kind == kind)
				return &s;
	return nullptr;
}


// the chart's frame's width (its first), 0 if it has none
float
ChartFrameWidth(const Document& doc, size_t chart)
{
	if (chart < doc.tails.size())
		for (const Portal& portal : doc.tails[chart].portals)
			for (const Frame& f : portal.frames)
				return f.extent[0];
	return 0;
}

} // namespace


bool
ShowChartPiece(Document& doc, size_t chart, uint32_t kind, bool show, const TextMeasure& measure)
{
	std::string label = kind == Sig("Titl") ? ChartLabel(doc, chart) : std::string();
	float frameWidth = ChartFrameWidth(doc, chart);
	ChartPart* c = doc.EditableChart(chart);
	if (c == nullptr)
		return false;
	Shape* piece = Piece(*c, kind);
	if (piece == nullptr) {
		if (kind != Sig("Titl") || !show || label.empty() || c->graphics.layers.empty())
			return false;
		// TChartPart::InsertNewPiece: a title (its style the legend's), then
		// TTitlePiece::SetText (fixup_text: the text's width and height in
		// its font, 4 more each) and FixupPiecePositions (centered, 4 down)
		Shape title;
		title.kind = Sig("Titl");
		const Shape* legend = Piece(*c, Sig("Lgnd"));
		title.style = legend != nullptr ? legend->style : 0;
		title.text = label;
		float width = 0, height = 0;
		if (measure)
			measure(label, title.style, width, height);
		title.size[0] = width + 4;
		title.size[1] = height + 4;
		title.origin[0] = frameWidth * 0.5f - title.size[0] * 0.5f;
		title.origin[1] = 4;
		title.radius[0] = 2;
		title.radius[1] = height;
		DocWriter w;
		w.Sig(Sig("tlp+"));
		w.String(label);
		w.Point(title.radius);
		w.Sig(Sig("tlp-"));
		title.pieceData = w.Data();
		title.pieceValue = 0;
		c->graphics.layers[0].shapes.push_back(title);
		piece = &c->graphics.layers[0].shapes.back();
	}
	if (((piece->pieceValue & 1) != 0) == show)
		return false;
	piece->pieceValue = show ? (piece->pieceValue | 1) : (piece->pieceValue & ~1u);
	return true;
}


void
SetChartDataFormat(ChartData& data, uint32_t format)
{
	data.value44 = format;
	uint32_t physical = data.values[1];
	data.values[2] = format == 1 ? physical / 2 : format == 2 ? (physical > 0 ? physical - 1 : 0)
		: physical;
	data.values[3] = format == 0 ? 1 : 2;
}


void
SyncChartSeries(Document& doc, ChartPart& chart)
{
	// the series' styles: the ones they had, more as MakeChart makes them
	size_t count = chart.SeriesCount();
	uint32_t base = FindNamedStyle(doc.styles, "GR Default");
	if (base >= doc.styles.styles.size())
		base = FindNamedStyle(doc.styles, "Default");
	std::vector<ChartSeries> series;
	for (size_t i = 0; i < count; i++) {
		ChartSeries s;
		s.name = i < chart.data.names2.size() ? chart.data.names2[i]
			: "Series " + std::to_string(i + 1);
		if (i < chart.series.size())
			s.style = chart.series[i].style;
		else {
			const uint8_t* color = kSeriesColors[i % 16];
			s.style = doc.NewStyle(Sig("Basi"), "", 0x20, base, {InkProperty("FCol",
				color[0], color[1], color[2], 0xff), InkProperty("chSC", 0, 0, 0, 0xff),
				ShortProperty("chSM", 0), ShortProperty("chSS", 6)});
			doc.styles.styles[s.style].value4++;
		}
		series.push_back(s);
	}
	chart.series = series;
}


void
StyleXYSeries(Document& doc, ChartPart& chart)
{
	// T2DXYChart::SetLegendAttributes: a scatter chart's series each a
	// symbol of its own ((s + 1) mod 7), in its default color, without a
	// line (PTra 0); a line chart's with a line (PTra 1)
	for (size_t i = chart.xyStyled; i < chart.series.size(); i++) {
		std::vector<Property> properties;
		if (chart.xyMinor == 0) {
			const uint8_t* color = kSeriesColors[i % 16];
			properties = {ShortProperty("chSM", uint16_t((i + 1) % 7)),
				InkProperty("chSC", color[0], color[1], color[2], 0xff), FloatProperty("PTra", 0)};
		} else
			properties = {FloatProperty("PTra", 1)};
		uint32_t style = chart.series[i].style;
		for (const Property& property : properties) {
			uint32_t made = StyleWithProperty(doc.styles, style, property);
			if (made == style)
				continue;
			doc.styles.styles[made].value4++;
			if (style < doc.styles.styles.size() && doc.styles.styles[style].value4 > 1)
				doc.styles.styles[style].value4--;
			style = made;
		}
		chart.series[i].style = style;
		doc.stylesChanged = true;
	}
	chart.xyStyled = uint32_t(chart.series.size());
}


bool
ToggleChartSeriesOrder(Document& doc, size_t chart, const TextMeasure& measure)
{
	size_t sheet;
	SSRange range;
	const ChartPart* current = doc.Chart(chart);
	if (current == nullptr || !ChartCells(doc, chart, sheet, range))
		return false;
	bool inRows = false;
	ChartDataOf(doc, sheet, range, current->data.value40, &inRows);
	ChartData data = ChartDataOf(doc, sheet, range, inRows ? 3 : 2);
	data.link = current->data.link;
	if (data.values[1] == 0)
		return false;
	uint32_t format = current->data.value44;
	ChartPart* c = doc.EditableChart(chart);
	if (c == nullptr)
		return false;
	c->data = data;
	SetChartDataFormat(c->data, format);
	SyncChartSeries(doc, *c);
	if (c->chartType == Sig("2DXY"))
		StyleXYSeries(doc, *c);
	ChartSeriesChanged(*c);
	FixChartLegend(*c, measure);
	return true;
}


void
FixChartLegend(ChartPart& chart, const TextMeasure& measure)
{
	chart.legendRect[3] = chart.legendRect[1] + float(chart.series.size() * 12 + 4);
	for (GRLayer& layer : chart.graphics.layers)
		for (Shape& legend : layer.shapes) {
			if (legend.kind != Sig("Lgnd"))
				continue;
			if (measure) {
				float widest = 0;
				for (const ChartSeries& s : chart.series) {
					float w = 0, h = 0;
					measure(s.name, s.style, w, h);
					widest = std::max(widest, w);
				}
				legend.size[0] = widest + 10 + 11;
			}
			legend.size[1] = chart.legendRect[3] - chart.legendRect[1];
		}
}


void
ChartSeriesChanged(ChartPart& chart)
{
	if (chart.chartType == Sig("2DXY")) {
		// T2DXYChart::FindMinMax: the x values' range and the y values'
		// (TDataAccessor::FindMinMax: each from 0)
		float range[2][2] = {{0, 0}, {0, 0}};
		for (size_t c = 0; c < chart.Categories(); c++)
			for (size_t s = 0; s < chart.SeriesCount(); s++)
				for (int which = 0; which < 2; which++) {
					float v = chart.Value(chart.PhysicalSeries(s, which), c);
					range[which][0] = std::min(range[which][0], v);
					range[which][1] = std::max(range[which][1], v);
				}
		SetRange(chart.axes[0], range[0][0], range[0][1], -1);
		SetRange(chart.axes[1], range[1][0], range[1][1], -1);
		return;
	}
	if (chart.Is3DAxis()) {
		// T3DXYAxisChart::FindMinMax: the values (and 0), the categories;
		// as deep as the series
		float low = 0, high = 0;
		for (size_t c = 0; c < chart.Categories(); c++)
			for (size_t s = 0; s < chart.SeriesCount(); s++) {
				float v = chart.Value(s, c);
				low = std::min(low, v);
				high = std::max(high, v);
			}
		SetRange(chart.axes[1], low, high, -1);
		chart.axisDepth[1] = float(chart.SeriesCount());
		SetRange(chart.axes[0], 0, float(chart.Categories()), int(chart.Categories()));
		chart.axisDepth[0] = float(chart.SeriesCount());
		return;
	}
	if (chart.chartType == Sig("PIE3")) {
		// T3DPieChart::DataChanged: an explosion for each series
		chart.explosions.resize(chart.SeriesCount());
		return;
	}
	if (chart.chartType != Sig("2DSM"))
		return;
	int32_t count = int32_t(chart.SeriesCount());
	for (size_t i = chart.sets.size(); i-- > 0;) {
		std::vector<uint32_t>& series = chart.sets[i].series;
		for (size_t j = series.size(); j-- > 0;) {
			if (int32_t(series[j]) < count)
				continue;
			series.erase(series.begin() + long(j));
			if (series.empty()) {
				chart.sets.erase(chart.sets.begin() + long(i));
				break;
			}
		}
	}
	for (int32_t s = count - 1; s >= 0; s--) {
		bool present = false;
		for (const ChartSeriesSet& set : chart.sets)
			for (uint32_t index : set.series)
				present |= int32_t(index) == s;
		if (chart.sets.empty()) {
			// CreateSets: an area chart's series a set each, the others'
			// all in one
			uint32_t sets = chart.seriesKind == 2 ? uint32_t(count) : 1;
			for (uint32_t i = 0; i < sets && count != 0; i++) {
				ChartSeriesSet set;
				set.kind = chart.seriesKind;
				if (chart.seriesKind == 2)
					set.series.push_back(i);
				else
					for (int32_t j = 0; j < count; j++)
						set.series.push_back(uint32_t(j));
				chart.sets.push_back(set);
			}
		} else if (!present) {
			if (chart.sets.back().kind == 2) {
				ChartSeriesSet set;
				set.kind = 2;
				chart.sets.push_back(set);
			}
			chart.sets.back().series.push_back(uint32_t(s));
		}
	}
	ChartFindMinMax(chart);
}


void
ChartFindMinMax(ChartPart& chart)
{
	if (chart.chartType != Sig("2DSM"))
		return;
	// FindMinMax: the values from 0, a stacked set's sums; the categories
	float low = 0, high = 0;
	for (size_t c = 0; c < chart.Categories(); c++)
		for (size_t s = 0; s < chart.SeriesCount(); s++) {
			float v = chart.Value(s, c);
			high = std::max(high, v);
			low = std::min(low, v);
		}
	for (const ChartSeriesSet& set : chart.sets) {
		if (set.kind != 3)
			continue;
		for (size_t c = 0; c < chart.Categories(); c++) {
			float sum = 0;
			for (uint32_t s : set.series)
				sum += chart.Value(s, c);
			high = std::max(high, sum);
		}
	}
	SetRange(chart.axes[1], low, high, -1);
	SetRange(chart.axes[0], 0, float(chart.Categories()), int(chart.Categories()));
}


void
ChartAxisRange(const ChartAxis& axis, float& low, float& high)
{
	low = AsFloat(axis.values[2]);
	high = AsFloat(axis.values[3]);
	if (low == high) {
		low = 0;
		high = 1e-05f;
	}
}


float
ChartAxisStep(const ChartAxis& axis)
{
	float step = AsFloat(axis.values[5]);
	if (step > 0)
		return step;
	return NiceNum(0.25f * NiceNum(AsFloat(axis.values[1]) - AsFloat(axis.values[0]), false),
		true);
}


bool
FormatChartSeries(Document& doc, size_t chart, size_t series, const Property& property)
{
	ChartPart* c = doc.EditableChart(chart);
	if (c == nullptr || series >= c->series.size())
		return false;
	uint32_t style = c->series[series].style;
	uint32_t made = StyleWithProperty(doc.styles, style, property);
	if (made == style)
		return true;
	doc.styles.styles[made].value4++;
	if (style < doc.styles.styles.size() && doc.styles.styles[style].value4 > 1)
		doc.styles.styles[style].value4--;
	c->series[series].style = made;
	doc.stylesChanged = true;
	return true;
}


bool
ChangeChartSeriesType(Document& doc, size_t chart, uint32_t series, uint32_t type)
{
	ChartPart* c = doc.EditableChart(chart);
	if (c == nullptr || c->chartType != Sig("2DSM") || type > 3 || series >= c->SeriesCount())
		return false;
	for (size_t i = 0; i < c->sets.size(); i++) {
		std::vector<uint32_t>& list = c->sets[i].series;
		auto at = std::find(list.begin(), list.end(), series);
		if (at == list.end())
			continue;
		list.erase(at);
		if (list.empty()) {
			c->sets.erase(c->sets.begin() + long(i));
			i--;
		}
	}
	// (an area set holds one series)
	bool added = false;
	for (ChartSeriesSet& set : c->sets)
		if (set.kind == type && type != 2) {
			set.series.push_back(series);
			added = true;
			break;
		}
	if (!added) {
		ChartSeriesSet set;
		set.kind = type;
		set.series.push_back(series);
		c->sets.push_back(set);
	}
	ChartFindMinMax(*c);
	return true;
}


bool
ChangeChartSeriesOrder(Document& doc, size_t chart, uint32_t series, int direction)
{
	const ChartPart* current = doc.Chart(chart);
	if (current == nullptr || current->chartType != Sig("2DSM"))
		return false;
	for (size_t i = 0; i < current->sets.size(); i++) {
		const std::vector<uint32_t>& list = current->sets[i].series;
		if (std::find(list.begin(), list.end(), series) == list.end())
			continue;
		size_t to = direction > 0 ? i + 1 : i - 1;
		if ((direction > 0 && i + 1 >= current->sets.size()) || (direction <= 0 && i == 0))
			return false;
		ChartPart* c = doc.EditableChart(chart);
		std::swap(c->sets[i], c->sets[to]);
		return true;
	}
	return false;
}


void
ChartTypeOf(const ChartPart& chart, uint32_t& major, uint32_t& minor)
{
	major = chart.partValues[0];
	minor = chart.partValues[1];
}


bool
ChangeChartType(Document& doc, size_t chart, uint32_t major, uint32_t minor,
	const TextMeasure& measure)
{
	// T2DXYAxisChart's kinds: bar (0, 0), line, area and stacked area
	// (2, 0 to 2); T2DXYChart: scatter and line (3, 0 and 1); T3DBarChart
	// (0, 1), T3DPieChart (1), T3DAreaChart (2, 3)
	int32_t kind = -1;
	uint32_t type = 0;
	bool xy = false;
	if (major == 0 && minor == 0)
		kind = 0;
	else if (major == 0 && minor == 1)
		type = Sig("BAR3");
	else if (major == 1)
		type = Sig("PIE3");
	else if (major == 2 && minor <= 2)
		kind = int32_t(minor) + 1;
	else if (major == 2 && minor == 3)
		type = Sig("ARE3");
	else if (major == 3 && minor <= 1)
		xy = true;
	if (kind < 0 && !xy && type == 0)
		return false;
	size_t sheet = 0;
	SSRange range;
	bool cells = ChartCells(doc, chart, sheet, range);
	ChartPart* c = doc.EditableChart(chart);
	if (c == nullptr)
		return false;
	// what the camera does: the chart that was (a T2DChart, a T3DPieChart)
	// and the one it becomes
	bool was2D = c->chartType == Sig("2DSM") || c->chartType == Sig("2DXY");
	bool wasPie = c->chartType == Sig("PIE3");
	bool toPie = type == Sig("PIE3");
	c->partValues[0] = major;
	c->partValues[1] = minor;
	// T2DXYAxisChart(TChart const&, ...), T2DXYChart(TChart const&, ...),
	// T3DXYAxisChart, T3DPieChart: the legend kept (its series' styles
	// copied: the same ones), new axes (TAxis: a style each on "GR
	// Default", by its data; T2DXYAxisChart's horizontal one without
	// gridlines; a T3DAxis's depth -1 until it has its data), no sets
	uint32_t base = FindNamedStyle(doc.styles, "GR Default");
	if (base >= doc.styles.styles.size())
		base = FindNamedStyle(doc.styles, "Default");
	for (int i = 0; i < 2; i++) {
		ChartAxis& axis = c->axes[i];
		if (axis.style < doc.styles.styles.size() && doc.styles.styles[axis.style].value4 > 1)
			doc.styles.styles[axis.style].value4--;
		axis = ChartAxis();
		c->axisDepth[i] = -1;
		if (toPie) {
			axis.style = 0xffffffff;
			continue;
		}
		axis.style = doc.NewStyle(Sig("Basi"), "", 0x20, base, {});
		doc.styles.styles[axis.style].value4++;
	}
	c->chartValue8 = 0;
	c->surfaceLabels.clear();
	c->sets.clear();
	c->explosions.clear();
	c->seriesKind = 0;
	c->xyMinor = 0;
	c->xyStyled = 0;
	c->chartBody.clear();
	if (type != 0) {
		c->chartType = type;
		// (T3DChart's 1; T3DXYAxisChart's 2 or T3DPieChart's 4)
		c->chartFlags = toPie ? 5 : 3;
	} else if (xy) {
		c->chartType = Sig("2DXY");
		c->chartFlags = 2;
		c->xyMinor = minor;
	} else {
		c->chartType = Sig("2DSM");
		c->axes[0].flags[1] = false;
		c->chartFlags = 0x8002;
		c->seriesKind = uint32_t(kind);
	}
	// TChartPart::SetNewChartPtr: a series direction taken from the cells'
	// shape now taken from the chart (its flags' bit 2), the data read
	// again; the chart's data format (TDataAccessor::SetDataFormat: an XY
	// chart's series in pairs, x and y)
	if (c->data.value40 < 2) {
		uint32_t mode = (c->chartFlags >> 2) & 1;
		if (mode != c->data.value40 && cells) {
			ChartData data = ChartDataOf(doc, sheet, range, mode);
			data.link = c->data.link;
			c->data = data;
		}
		c->data.value40 = mode;
	}
	SetChartDataFormat(c->data, xy ? 1 : 0);
	SyncChartSeries(doc, *c);
	if (xy)
		StyleXYSeries(doc, *c);
	ChartSeriesChanged(*c);
	FixChartLegend(*c, measure);
	// TChangeChartTypeCmd::Perform: PositionCamera from a 2D chart or to or
	// from a pie; a 3D chart to a 3D chart with axes keeps its place
	if (was2D || wasPie || toPie) {
		ChartCamera camera;
		if (ReadChartCamera(*c, camera)) {
			const uint32_t place[2][6] = {{0x3f79a7c1, 0x3f45fba4, 0x3f3ccaa3, 0x3ed26cc2,
				0x3ec8c975, 0xbf209b20}, {0x3b9f6230, 0xbeaeb980, 0x3f459b6f, 0xbdc1d29e,
				0x3d921d54, 0xbeab69ba}};
			for (int i = 0; i < 3; i++) {
				memcpy(&camera.position[i], &place[toPie ? 1 : 0][i], sizeof(float));
				memcpy(&camera.look[i], &place[toPie ? 1 : 0][i + 3], sizeof(float));
			}
			WriteChartCamera(*c, camera);
		}
	}
	return true;
}

} // namespace gobe
