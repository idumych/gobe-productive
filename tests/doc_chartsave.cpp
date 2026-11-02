// doc_chartsave: a chart changed after it was made (the Chart menu's
// commands) saved and opened again: Show/Hide Legend and Title
// (THideShowPieceCmd), Series In Rows (TToggleSeriesOrderCmd), an axis
// changed (TChangeAxisCmd); an unchanged chart written as read. No
// arguments.
#include <cstdio>
#include <cstdlib>
#include <string>

#include "ChartPart.h"
#include "Document.h"
#include "Editing.h"

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


static const Shape*
Piece(const ChartPart& chart, uint32_t kind)
{
	for (const GRLayer& layer : chart.graphics.layers)
		for (const Shape& s : layer.shapes)
			if (s.kind == kind)
				return &s;
	return nullptr;
}


static bool
Reopen(Document& doc, Document& back, const std::string& name)
{
	const char* dir = getenv("TMPDIR");
	std::string path = std::string(dir != nullptr ? dir : "/tmp") + "/" + name;
	std::string error;
	bool saved = doc.Save(path, error);
	Check(saved, name + ": saved " + error);
	bool opened = saved && LoadDocument(path, back, error);
	Check(opened, name + ": opened " + error);
	return opened;
}


int
main()
{
	// a labelled block, taller than wide (series in columns): the corner
	// cell and the names, two series of three categories
	Document doc;
	std::string error;
	Check(CreateBlankSpreadsheet(doc, error), "a new spreadsheet " + error);
	SSPart& ss = *doc.EditableSpreadsheetPart(0);
	const char* cells[4][3] = {{"Fruit", "Spring", "Fall"}, {"Apples", "4", "7"},
		{"Pears", "2", "5"}, {"Plums", "6", "1"}};
	for (uint16_t r = 0; r < 4; r++)
		for (uint16_t c = 0; c < 3; c++)
			SetCellInput(ss, r, c, cells[r][c], [](const std::string&) { return false; }, error);
	SSRange range;
	range.from = {0, 0};
	range.to = {3, 2};
	int made = MakeChart(doc, 0, range, error);
	Check(made >= 0, "made " + error);
	if (made < 0) {
		printf("doc_chartsave ok %d, failed %d\n", sOk, sFailed);
		return 1;
	}
	size_t chart = size_t(made);
	Document saved;
	if (!Reopen(doc, saved, "doc_chartsave_1.gobe")) {
		printf("doc_chartsave ok %d, failed %d\n", sOk, sFailed);
		return 1;
	}

	// what it reads from its cells
	Check(!ChartSeriesInRows(saved, chart), "taller than wide: series in columns");
	Check(ChartLabel(saved, chart) == "Fruit", "the label: the corner cell");

	// unchanged: written as read
	Document again;
	if (Reopen(saved, again, "doc_chartsave_2.gobe"))
		Check(again.parts.parts[chart].data == saved.parts.parts[chart].data,
			"an unchanged chart written as read");

	// the legend hidden, a title shown (made from the label)
	auto measure = [](const std::string& text, uint32_t, float& width, float& height) {
		width = 6.0f * float(text.size());
		height = 12;
	};
	Check(ShowChartPiece(saved, chart, Sig("Lgnd"), false, measure), "the legend hidden");
	Check(!ShowChartPiece(saved, chart, Sig("Lgnd"), false, measure), "hidden already: no change");
	// (a new chart has its label as a title already: hidden, shown again)
	Check(ShowChartPiece(saved, chart, Sig("Titl"), false, measure), "the title hidden");
	Check(ShowChartPiece(saved, chart, Sig("Titl"), true, measure), "the title shown");
	// Series In Rows, and the vertical axis given a range
	Check(ToggleChartSeriesOrder(saved, chart), "Series In Rows");
	Check(ChartSeriesInRows(saved, chart), "series in rows now");
	ChartPart* c = saved.EditableChart(chart);
	Check(c != nullptr, "the chart to change");
	if (c != nullptr) {
		c->axes[1].flags[0] = false;
		c->axes[1].flags[1] = false;
	}

	Document back;
	if (Reopen(saved, back, "doc_chartsave_3.gobe")) {
		const ChartPart* b = back.Chart(chart);
		Check(b != nullptr, "the chart read back");
		if (b != nullptr) {
			const Shape* legend = Piece(*b, Sig("Lgnd"));
			const Shape* title = Piece(*b, Sig("Titl"));
			Check(legend != nullptr && (legend->pieceValue & 1) == 0, "read back: the legend hidden");
			Check(title != nullptr && (title->pieceValue & 1) != 0 && title->text == "Fruit",
				"read back: the title, its text");
			Check(b->data.value40 == 2, "read back: series in rows (+0x40 2)");
			Check(b->data.values[0] == 2 && b->data.values[1] == 3
				&& b->data.names1 == std::vector<std::string>({"Spring", "Fall"})
				&& b->data.names2 == std::vector<std::string>({"Apples", "Pears", "Plums"}),
				"read back: three series of two categories, their names");
			Check(b->series.size() == 3 && b->series[2].name == "Plums",
				"read back: the legend's series");
			Check(b->Value(0, 1) == 7 && b->Value(2, 0) == 6, "read back: the values");
			Check(!b->axes[1].flags[0] && !b->axes[1].flags[1], "read back: the axis");
		}
		Check(back.parts.parts.size() == saved.parts.parts.size(), "the same parts");
	}

	printf("doc_chartsave ok %d, failed %d\n", sOk, sFailed);
	return sFailed > 0 ? 1 : 0;
}
