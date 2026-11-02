// doc_makechart: Make Chart (MakeChart, ChartDataOf). A new spreadsheet
// with a labelled block (series across, categories down) charted, saved
// and opened again: one chart part, its values and names, a link from an
// unnamed reference to the cells, its frame in the sheet's drawing with a
// FRAM shape there, its section written back as read. A wide block
// (series down), a block without labels, and LoanCalculator's payments
// the same way.
// Arguments: documents (LoanCalculator is used if given).
#include <cmath>
#include <cstdio>
#include <string>

#include "ChartPart.h"
#include "Document.h"
#include "Editing.h"
#include "PartTail.h"

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


static void
Set(SSPart& ss, uint16_t row, uint16_t column, const std::string& input)
{
	std::string error;
	SetCellInput(ss, row, column, input, [](const std::string&) { return false; }, error);
}


static SSRange
Range(uint16_t r0, uint16_t c0, uint16_t r1, uint16_t c1)
{
	SSRange r;
	r.from.a = r0;
	r.from.b = c0;
	r.to.a = r1;
	r.to.b = c1;
	return r;
}


// a chart made, saved and opened again; `values` series-major
static void
MakeAndReopen(Document& doc, size_t part, const SSRange& range, const std::string& what,
	size_t categoryCount, size_t seriesCount, const std::vector<std::string>& categories,
	const std::vector<std::string>& series, const std::vector<float>& values)
{
	std::string error;
	size_t charts = doc.chartParts.size();
	int chart = MakeChart(doc, part, range, error);
	Check(chart >= 0, what + ": made " + error);
	if (chart < 0)
		return;
	std::string path = "/tmp/doc_makechart_test.gobe";
	Check(doc.Save(path, error), what + ": saved " + error);
	Document back;
	Check(LoadDocument(path, back, error), what + ": opened " + error);
	Check(back.warnings.empty(), what + ": no warnings" + (back.warnings.empty() ? ""
		: ": " + back.warnings[0]));
	Check(back.chartParts.size() == charts + 1, what + ": a chart more");
	const ChartPart* c = nullptr;
	for (const auto& [index, p] : back.chartParts)
		if (index == size_t(chart))
			c = &p;
	Check(c != nullptr, what + ": the chart part read");
	if (c == nullptr)
		return;
	Check(c->chartType == Sig("2DSM") && c->sets.size() == 1 && c->sets[0].kind == 0,
		what + ": a 2D bar chart");
	Check(c->Categories() == categoryCount && c->SeriesCount() == seriesCount,
		what + ": " + std::to_string(c->Categories()) + " categories, "
		+ std::to_string(c->SeriesCount()) + " series");
	Check(c->data.names1 == categories, what + ": the categories' names");
	Check(c->data.names2 == series, what + ": the series' names");
	bool same = c->data.table.size() == 1 && c->data.table[0] == values;
	Check(same, what + ": the values");
	Check(c->series.size() == c->SeriesCount(), what + ": a legend entry per series");
	// the link: from the spreadsheet's reference (its selection the cells)
	// to the chart
	bool linked = c->data.link < back.links.links.size();
	if (linked) {
		const Link& link = back.links.links[c->data.link];
		linked = link.sourcePart == part && link.dependentPart == uint32_t(chart)
			&& link.reference < back.tails[part].refs.size();
		if (linked) {
			const Reference& ref = back.tails[part].refs[link.reference];
			const Selection* sel = back.selections.Find(ref.selection);
			linked = ref.name.empty() && sel != nullptr && sel->ranges.size() == 1
				&& sel->ranges[0][0] == range.from.a && sel->ranges[0][1] == range.from.b
				&& sel->ranges[0][2] == range.to.a && sel->ranges[0][3] == range.to.b;
		}
	}
	Check(linked, what + ": linked to the cells");
	// its frame, in the sheet's drawing, which has a FRAM shape for it
	uint32_t drawing = kNoPart;
	for (const LeafSheet& sheet : back.sheets.sheets)
		if (sheet.mainPart == part)
			drawing = sheet.graphicsPart;
	const Frame* frame = back.FrameOf(uint32_t(chart), 0);
	Check(frame != nullptr && frame->container == drawing && frame->part == uint32_t(chart)
		&& frame->extent[0] == 400 && frame->extent[1] == 200, what + ": its frame");
	bool shape = false;
	for (const auto& [index, gr] : back.graphicsParts)
		if (index == drawing)
			for (const GRLayer& layer : gr.layers)
				for (const Shape& s : layer.shapes)
					shape |= s.kind == Sig("FRAM") && s.frame[0] == uint32_t(chart);
	Check(shape, what + ": the drawing's FRAM shape");
	bool placed = false;
	for (const Document::PlacedFrame& f : back.PlacedFrames())
		placed |= f.part == size_t(chart);
	Check(placed, what + ": shown");
	std::vector<uint8_t> section = WriteChartSection(*c, back.file.header.version.values[2]);
	const std::vector<uint8_t>& record = back.parts.parts[size_t(chart)].data;
	Check(section.size() == c->sectionEnd - c->sectionStart && std::equal(section.begin(),
		section.end(), record.begin() + long(c->sectionStart)), what + ": written as read");
	doc = std::move(back);
}


int
main(int argc, char** argv)
{
	// a new spreadsheet: names across the top, quarters down the side
	Document doc;
	std::string error;
	Check(CreateBlankSpreadsheet(doc, error), "a new spreadsheet " + error);
	size_t part = doc.spreadsheetParts.empty() ? 0 : doc.spreadsheetParts[0].first;
	SSPart* ss = doc.EditableSpreadsheetPart(part);
	if (ss == nullptr)
		return 1;
	const char* cells[4][3] = {
		{"", "North", "South"}, {"Q1", "10", "20"}, {"Q2", "15", "5"}, {"Q3", "7", "9.5"}};
	for (uint16_t r = 0; r < 4; r++)
		for (uint16_t c = 0; c < 3; c++)
			Set(*ss, r, c, cells[r][c]);
	MakeAndReopen(doc, part, Range(0, 0, 3, 2), "labelled", 3, 2, {"Q1", "Q2", "Q3"},
		{"North", "South"}, {10, 15, 7, 20, 5, 9.5f});
	{
		// placed in the middle of what the window shows (TPartGR::
		// InsertChildFrame): 400 by 200 round the point given
		Document centered;
		Check(CreateBlankSpreadsheet(centered, error), "a sheet for a centered chart " + error);
		size_t sp = centered.spreadsheetParts.empty() ? 0 : centered.spreadsheetParts[0].first;
		SSPart* cs = centered.EditableSpreadsheetPart(sp);
		for (uint16_t r = 0; r < 4; r++)
			for (uint16_t c = 0; c < 3; c++)
				Set(*cs, r, c, cells[r][c]);
		const float center[2] = {300, 250};
		int made = MakeChart(centered, sp, Range(0, 0, 3, 2), error, nullptr, center);
		const Frame* placed = nullptr;
		for (size_t p = 0; p < centered.tails.size(); p++)
			if (centered.tailRead[p])
				for (const Portal& portal : centered.tails[p].portals)
					for (const Frame& f : portal.frames)
						if (made >= 0 && f.part == uint32_t(made))
							placed = &f;
		Check(placed != nullptr && std::fabs(placed->origin[0] - 100) < 0.5f
			&& std::fabs(placed->origin[1] - 150) < 0.5f,
			"the chart round the point given: "
			+ (placed == nullptr ? std::string("none") : std::to_string(placed->origin[0]) + ","
			+ std::to_string(placed->origin[1])));
		// its pieces where the original puts them (the legend's and
		// title's widths measured: 6 a character, 12 high)
		Document measured;
		Check(CreateBlankSpreadsheet(measured, error), "a sheet for a measured chart " + error);
		SSPart* ms = measured.EditableSpreadsheetPart(sp);
		const char* named[4][3] = {
			{"Year", "North", "South"}, {"Q1", "10", "20"}, {"Q2", "15", "5"}, {"Q3", "7", "9.5"}};
		for (uint16_t r = 0; r < 4; r++)
			for (uint16_t c = 0; c < 3; c++)
				Set(*ms, r, c, named[r][c]);
		auto measure = [](const std::string& text, uint32_t, float& width, float& height) {
			width = 6.0f * float(text.size());
			height = 12;
		};
		int chart = MakeChart(measured, sp, Range(0, 0, 3, 2), error, measure, center);
		const ChartPart* c = chart >= 0 ? measured.Chart(size_t(chart)) : nullptr;
		const Shape* plot = nullptr;
		const Shape* legend = nullptr;
		const Shape* title = nullptr;
		if (c != nullptr)
			for (const Shape& shape : c->graphics.layers[0].shapes) {
				if (shape.kind == Sig("Rndr"))
					plot = &shape;
				else if (shape.kind == Sig("Lgnd"))
					legend = &shape;
				else if (shape.kind == Sig("Titl"))
					title = &shape;
			}
		Check(plot != nullptr && plot->origin[0] == 39 && plot->origin[1] == 6
			&& plot->size[0] == 349 && plot->size[1] == 188, "the plot at 39, 6, 349 by 188");
		Check(legend != nullptr && legend->origin[0] == 5 && legend->origin[1] == 5
			&& legend->size[0] == 6 * 5 + 21 && legend->size[1] == 2 * 12 + 4,
			"the legend at 5, 5, its widest name and 21 across, 12 a series and 4 down");
		Check(title != nullptr && title->text == "Year" && (title->pieceValue & 1) != 0
			&& title->size[0] == 6 * 4 + 4 && title->size[1] == 16
			&& title->origin[0] == 200 - 14 && title->origin[1] == 4,
			"the corner cell a title, centered, 4 down");
	}

	{
		// four columns: no room at the right in the new sheet's frame (609
		// points): below the cells instead, not over them
		Document four;
		Check(CreateBlankSpreadsheet(four, error), "another new spreadsheet " + error);
		size_t fourPart = four.spreadsheetParts.empty() ? 0 : four.spreadsheetParts[0].first;
		SSPart* fs = four.EditableSpreadsheetPart(fourPart);
		const char* block[4][4] = {{"", "North", "South", "West"}, {"Q1", "10", "20", "12"},
			{"Q2", "15", "5", "9"}, {"Q3", "7", "9.5", "14"}};
		for (uint16_t r = 0; fs != nullptr && r < 4; r++)
			for (uint16_t c = 0; c < 4; c++)
				Set(*fs, r, c, block[r][c]);
		int made = MakeChart(four, fourPart, Range(0, 0, 3, 3), error);
		const Frame* placed = nullptr;
		for (size_t p = 0; p < four.tails.size(); p++)
			if (four.tailRead[p])
				for (const Portal& portal : four.tails[p].portals)
					for (const Frame& f : portal.frames)
						if (made >= 0 && f.part == uint32_t(made))
							placed = &f;
		const SSPart* sheet = four.SpreadsheetPart(fourPart);
		float below = sheet->defaultHeight + 20;
		for (uint16_t r = 0; r <= 3; r++)
			below += sheet->RowHeight(r);
		Check(placed != nullptr && std::fabs(placed->origin[0] - 56) < 0.5f
			&& std::fabs(placed->origin[1] - below) < 0.5f,
			"four columns: the chart below the cells: " + (placed == nullptr ? std::string("none")
			: std::to_string(placed->origin[0]) + "," + std::to_string(placed->origin[1])));
	}

	// wide (more columns than rows): series down; and no labels at all
	ss = doc.EditableSpreadsheetPart(part);
	const char* wide[2][4] = {{"", "Jan", "Feb", "Mar"}, {"Rain", "3", "1", "4"}};
	for (uint16_t r = 0; r < 2; r++)
		for (uint16_t c = 0; c < 4; c++)
			Set(*ss, uint16_t(10 + r), c, wide[r][c]);
	MakeAndReopen(doc, part, Range(10, 0, 11, 3), "wide", 3, 1, {"Jan", "Feb", "Mar"}, {"Rain"},
		{3, 1, 4});
	MakeAndReopen(doc, part, Range(1, 1, 3, 2), "no labels", 3, 2, {}, {}, {10, 15, 7, 20, 5, 9.5f});
	Check(doc.chartParts.size() == 3, "three charts in the document");

	// LoanCalculator: the first ten payments' interest and principal
	for (int i = 1; i < argc; i++) {
		std::string path = argv[i];
		if (path.size() < 14 || path.compare(path.size() - 14, 14, "LoanCalculator") != 0)
			continue;
		Document loan;
		Check(LoadDocument(path, loan, error), "LoanCalculator " + error);
		size_t main = size_t(loan.MainPart());
		const SSPart* p = loan.SpreadsheetPart(main);
		Check(p != nullptr, "LoanCalculator's spreadsheet");
		if (p == nullptr)
			break;
		// the expected values as the chart takes them (ChartDataOf's own
		// reading is checked above); here: made, saved, opened, the same
		SSRange range = Range(14, 4, 24, 5);
		ChartData expected = ChartDataOf(loan, main, range);
		Check(expected.values[0] == 10 && expected.values[1] == 2
			&& expected.names2 == std::vector<std::string>{"Interest", "Principal"}
			&& expected.names1.empty(), "LoanCalculator: 10 payments, Interest and Principal");
		// the first payment's interest: 225000 x 7% / 12 = 1312.50
		Check(!expected.table.empty() && expected.table[0].size() == 20
			&& expected.table[0][0] == 1312.5f, "LoanCalculator: the first interest 1312.50");
		MakeAndReopen(loan, main, range, "LoanCalculator", 10, 2, expected.names1, expected.names2,
			expected.table.empty() ? std::vector<float>() : expected.table[0]);
	}
	// StockPrice: its sheet only as large as its cells (the drawing's
	// frame 284 x 629): the chart made inside it, not right of the cells
	for (int i = 1; i < argc; i++) {
		std::string path = argv[i];
		if (path.size() < 10 || path.compare(path.size() - 10, 10, "StockPrice") != 0)
			continue;
		Document stock;
		Check(LoadDocument(path, stock, error), "StockPrice " + error);
		size_t main = size_t(stock.MainPart());
		int chart = MakeChart(stock, main, Range(10, 2, 12, 3), error);
		Check(chart >= 0, "StockPrice: a chart " + error);
		const Frame* inside = nullptr;
		const Frame* drawingFrame = nullptr;
		for (size_t p = 0; p < stock.tails.size(); p++)
			if (stock.tailRead[p])
				for (const Portal& portal : stock.tails[p].portals)
					for (const Frame& f : portal.frames) {
						if (chart >= 0 && f.part == uint32_t(chart))
							inside = &f;
						else if (f.part == stock.sheets.sheets[0].graphicsPart)
							drawingFrame = &f;
					}
		Check(inside != nullptr && drawingFrame != nullptr
			&& inside->origin[0] >= 0 && inside->origin[1] >= 0
			&& inside->origin[0] + inside->extent[0] <= drawingFrame->extent[0]
			&& inside->origin[1] + inside->extent[1] <= drawingFrame->extent[1],
			"StockPrice: the chart within the drawing's frame: " + (inside == nullptr ? std::string("none") : std::to_string(inside->origin[0]) + "," + std::to_string(inside->origin[1]) + " " + std::to_string(inside->extent[0]) + "x" + std::to_string(inside->extent[1])) + (drawingFrame == nullptr ? " no drawing" : " in " + std::to_string(drawingFrame->extent[0]) + "x" + std::to_string(drawingFrame->extent[1])));
	}
	printf("doc_makechart: ok %d, failed %d\n", sOk, sFailed);
	return sFailed ? 1 : 0;
}
