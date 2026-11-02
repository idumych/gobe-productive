// TPageView's charts (Parts/Chart: TChartPart): a chart frame entered by a
// double click (the chart part active, TChartPart::Activate), the Chart
// menu's commands (TChartPart::HandleMessage) and what it shows
// (TChartPart::FixMenus).
#include "TPageView.h"

#include <Beep.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <Window.h>

#include "ChartPart.h"
#include "GobeResources.h"
#include "TChartDialogs.h"
#include "Styles.h"
#include "TDocView.h"

namespace gobe {

namespace {

// the chart's piece of `kind` ('Lgnd', 'Titl', 'Rndr'), NULL if none
// (TChartPart::GetPiece)
const Shape*
ChartPiece(const ChartPart& chart, uint32_t kind)
{
	for (const GRLayer& layer : chart.graphics.layers)
		for (const Shape& s : layer.shapes)
			if (s.kind == kind)
				return &s;
	return NULL;
}


Shape*
ChartPiece(ChartPart& chart, uint32_t kind)
{
	return const_cast<Shape*>(ChartPiece(const_cast<const ChartPart&>(chart), kind));
}


// TChartPiece::Visible (+0x30's bit 0)
bool
PieceVisible(const Shape* piece)
{
	return piece != NULL && (piece->pieceValue & 1) != 0;
}


// string `index` of the chart's STR# `id` (TChartPart::ChartStr: 1;
// TChartCommand::ChartCmdString: 3)
BString
ChartString(int32 id, int32 index)
{
	return BString(gobe::IndexString(GobeResources::Part("Chart"), id, index).c_str());
}

} // namespace


// a double click in a chart's frame enters it (the chart part active);
// a click outside the chart entered leaves it
bool
TPageView::ChartClick(BPoint where, int32 clicks)
{
	float dx = fMargin - fBounds.left, dy = fMargin - fBounds.top;
	const Document::PlacedFrame* hit = NULL;
	for (const Document::PlacedFrame& f : fFrames) {
		if (f.part >= fDocument.parts.parts.size()
				|| fDocument.parts.parts[f.part].kind != Sig("chrt")
				|| fDocument.Chart(f.part) == NULL)
			continue;
		BRect r(f.x + dx, f.y + dy, f.x + dx + f.width, f.y + dy + f.height);
		if (r.Contains(where) && (hit == NULL || f.depth >= hit->depth))
			hit = &f;
	}
	if (fChart.active && (hit == NULL || hit->part != fChart.part)) {
		fChart.active = false;
		Invalidate();
	}
	if (hit == NULL)
		return false;
	if (fChart.active)
		return true;		// (in the chart entered: its pieces aren't picked yet)
	if (clicks < 2)
		return false;
	fChart.active = true;
	fChart.part = hit->part;
	fSelectedCell.active = false;
	fCaret.active = false;
	fAnchor.active = false;
	fShapes.clear();
	fFrame = FrameRef();
	Invalidate();
	if (Window() != NULL)
		Window()->PostMessage(kMsgDocumentChanged);
	return true;
}


// TChartPart::FixMenus: Series In Rows marked as the data has it (+0x4d);
// Show or Hide Legend and Title as their pieces are shown (the title on
// only with a title or the data's label); Axis… with two axes
void
TPageView::ChartMenuState(bool& seriesInRows, BString& legend, BString& title,
	bool& titleEnabled, bool& axis) const
{
	seriesInRows = false;
	legend = ChartString(1, 2);
	title = ChartString(1, 0);
	titleEnabled = false;
	axis = false;
	const ChartPart* chart = fChart.active ? fDocument.Chart(fChart.part) : NULL;
	if (chart == NULL)
		return;
	seriesInRows = ChartSeriesInRows(fDocument, fChart.part);
	legend = ChartString(1, PieceVisible(ChartPiece(*chart, Sig("Lgnd"))) ? 3 : 2);
	const Shape* titlePiece = ChartPiece(*chart, Sig("Titl"));
	title = ChartString(1, PieceVisible(titlePiece) ? 1 : 0);
	titleEnabled = titlePiece != NULL || !ChartLabel(fDocument, fChart.part).empty();
	// (TChart::AxisCount: the 2D charts with axes)
	axis = chart->chartType == Sig("2DSM");
}


// TChartPart::HandleMessage: the Chart menu's commands
bool
TPageView::ChartCommand(BMessage* message)
{
	uint32 what = message->what;
	if (what != 'sCHc' && what != 'sCHd' && what != 'sCHe' && what != 'sCH1' && what != 'sCH8')
		return false;
	if (!fChart.active || fDocument.Chart(fChart.part) == NULL) {
		beep();
		return true;
	}
	if (what == 'sCHd' || what == 'sCHe') {
		// THideShowPieceCmd: the piece shown or hidden ("Show Piece",
		// "Hide Piece", by what it will be)
		uint32_t kind = what == 'sCHd' ? Sig("Lgnd") : Sig("Titl");
		const Shape* piece = ChartPiece(*fDocument.Chart(fChart.part), kind);
		bool show = !PieceVisible(piece);
		fUndo.BeginDocument(fDocument, ChartString(3, show ? 1 : 2).String(),
			CurrentViewState());
		// (fixup_text: the title's text in its style's font)
		TextMeasure measure = [this](const std::string& text, uint32_t style, float& width,
				float& height) {
			BFont font = TDocView::MapFont(ResolveTextStyle(fDocument.styles, style));
			font_height fh;
			font.GetHeight(&fh);
			width = font.StringWidth(text.c_str());
			height = fh.ascent + fh.descent;
		};
		if (!ShowChartPiece(fDocument, fChart.part, kind, show, measure)) {
			beep();
			return true;
		}
	} else if (what == 'sCHc') {
		// TToggleSeriesOrderCmd: "Change Series Order"
		fUndo.BeginDocument(fDocument, ChartString(3, 0).String(), CurrentViewState());
		if (!ToggleChartSeriesOrder(fDocument, fChart.part)) {
			beep();
			return true;
		}
	} else if (what == 'sCH8') {
		const void* data = NULL;
		ssize_t size = 0;
		if (message->FindData("axes", B_RAW_TYPE, &data, &size) != B_OK
				|| size != ssize_t(2 * sizeof(ChartAxis))) {
			// TAxisDialog with the chart's axes
			if (Window() != NULL)
				(new TDialogAxis(Window(), fDocument.Chart(fChart.part)->axes))->RunDialog();
			return true;
		}
		// TChangeAxisCmd: "Change Axis Display"
		fUndo.BeginDocument(fDocument, ChartString(3, 6).String(), CurrentViewState());
		ChartPart* chart = fDocument.EditableChart(fChart.part);
		if (chart == NULL) {
			beep();
			return true;
		}
		memcpy(chart->axes, data, size);
	} else {
		// Chart Type… (TDialogChart): not yet
		beep();
		return true;
	}
	Invalidate();
	if (Window() != NULL)
		Window()->PostMessage(kMsgDocumentChanged);
	return true;
}

} // namespace gobe
