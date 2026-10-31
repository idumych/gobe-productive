// doc_makechart: Make Chart (MakeChart, ChartDataOf). A new spreadsheet
// with a labelled block (series across, categories down) charted, saved
// and opened again: one chart part, its values and names, a link from an
// unnamed reference to the cells, its frame in the sheet's drawing with a
// FRAM shape there, its section written back as read. A wide block
// (series down), a block without labels, and LoanCalculator's payments
// the same way.
// Arguments: documents (LoanCalculator is used if given).
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
		&& frame->extent[0] == 320 && frame->extent[1] == 200, what + ": its frame");
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
	printf("doc_makechart: ok %d, failed %d\n", sOk, sFailed);
	return sFailed ? 1 : 0;
}
