// TPageView's charts (Parts/Chart: TChartPart): a chart frame entered by a
// double click (the chart part active, TChartPart::Activate), the Chart
// menu's commands (TChartPart::HandleMessage) and what it shows
// (TChartPart::FixMenus).
#include "TPageView.h"

#include <Beep.h>
#include <Region.h>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <Window.h>

#include "ChartPart.h"
#include "DocArchive.h"
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


// TChartLegendSeries::LegendSymbol, SymbolSize and the symbol's color
// (chSM, chSS: 6 without one, chSC)
struct SeriesSymbol {
	int type = 0;
	float size = 6;
	bool colored = false;
	Color color;
};


SeriesSymbol
SymbolOf(const StyleMgr& styles, uint32_t style)
{
	SeriesSymbol symbol;
	ForEachStyleProperty(styles, style, [&](const Property& property) {
		DocArchive a(property.data);
		try {
			if (property.type == Sig("chSM"))
				symbol.type = a.Int16();
			else if (property.type == Sig("chSS"))
				symbol.size = a.Int16();
			else if (property.type == Sig("chSC")) {
				Ink ink;
				if (ReadInk(a, ink)) {
					symbol.colored = true;
					symbol.color = ink.color;
				}
			}
		} catch (...) {
		}
	});
	return symbol;
}


// TChartLegend::GetLegendSymbolPoly: a square (1, 2), a triangle (3, 4)
// or a diamond (5, 6) in a unit square, filled for the odd types
const float kSquare[5][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}, {0, 0}};
const float kTriangle[4][2] = {{0.5f, 0}, {1, 1}, {0, 1}, {0.5f, 0}};
const float kDiamond[5][2] = {{0.5f, 0}, {1, 0.5f}, {0.5f, 1}, {0, 0.5f}, {0.5f, 0}};


rgb_color
InkColor(const Color& c, float opacity)
{
	rgb_color color = {c.r, c.g, c.b, uint8(std::min(255.0f, std::max(0.0f, opacity * 255)))};
	return color;
}

} // namespace


// TChartPiece::Draw for each piece shown, in its layer's order: its
// rectangle filled (TShape::FillShape), its content clipped to it, then
// stroked (StrokeShape), each with the piece's style. The plot (TRendering
// Piece::DrawContent, T2DXYAxisChart::Draw2D) is the rectangle from the
// vertical axis's labels (LeftAxisSpace) and 15 down to 13 from the right
// and 18 from the bottom: the horizontal axis, the vertical one, then the
// series sets twice (bars and areas, then lines). The legend
// (TChartLegend::Draw) has a 10 by 10 box of each series' fill, outlined
// black, at 5 across and 4 + 12 a series down, its name 14 right of the
// box's left at its bottom; the title (TTitlePiece::DrawContent) its text
// at its position.
void
TPageView::DrawChart2D(const ChartPart& chart, BRect r)
{
	const StyleMgr& styles = fDocument.styles;
	PushState();
	BRegion frameClip(r);
	ConstrainClippingRegion(&frameClip);
	SetDrawingMode(B_OP_ALPHA);
	SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);

	// TStyleID::Push: the style's font and text color
	auto useFont = [&](uint32_t style) {
		TextStyle ts = ResolveTextStyle(styles, style);
		BFont font = TDocView::MapFont(ts);
		SetFont(&font);
		SetHighColor(InkColor(ts.color, 1));
		return font;
	};
	// the style's pen: its color and size (a hairline for 0); false if it
	// doesn't show
	auto usePen = [&](const ShapeStyle& style) {
		if (!style.pen.set || style.penOpacity <= 0)
			return false;
		SetHighColor(InkColor(style.pen.color, style.penOpacity));
		SetPenSize(std::max(1.0f, style.penSize));
		return true;
	};
	auto useFill = [&](const ShapeStyle& style) {
		if (!style.fill.set || style.fillOpacity <= 0)
			return false;
		SetHighColor(InkColor(style.fill.color, style.fillOpacity));
		return true;
	};
	auto label = [](float value) {
		char text[64];
		snprintf(text, sizeof text, "%.6g", double(value));
		return std::string(text);
	};

	const ChartAxis& vertical = chart.axes[1];
	const ChartAxis& horizontal = chart.axes[0];
	size_t categories = chart.Categories();
	size_t seriesCount = chart.SeriesCount();
	float low, high;
	ChartAxisRange(vertical, low, high);
	float step = ChartAxisStep(vertical);
	bool axisDrawn = high != low && step > 0;
	auto seriesStyle = [&](uint32_t s) {
		return s < chart.series.size() ? chart.series[s].style : 0xffffffffu;
	};

	for (const GRLayer& layer : chart.graphics.layers)
		for (const Shape& piece : layer.shapes) {
			bool rendering = piece.kind == Sig("Rndr");
			if (!rendering && piece.kind != Sig("Lgnd") && piece.kind != Sig("Titl"))
				continue;
			if ((piece.pieceValue & 1) == 0)
				continue;
			BRect pr(r.left + piece.origin[0], r.top + piece.origin[1],
				r.left + piece.origin[0] + piece.size[0], r.top + piece.origin[1] + piece.size[1]);
			ShapeStyle pieceStyle = ResolveShapeStyle(styles, piece.style);
			if (useFill(pieceStyle))
				FillRect(pr);
			PushState();
			BRegion pieceClip(pr);
			ConstrainClippingRegion(&pieceClip);
			if (rendering && categories > 0 && seriesCount > 0) {
				// LeftAxisSpace: 4 more than T2DVerticalAxis::DrawOrMeasure's
				// widest label and 4
				BFont axisFont = useFont(vertical.style);
				float widest = 0;
				if (axisDrawn)
					for (float v = low; v < step * 0.5f + high; v += step)
						widest = std::max(widest, axisFont.StringWidth(label(v).c_str()));
				BRect plot(pr.left + 8 + widest, pr.top + 15, pr.right - 13, pr.bottom - 18);

				bool xy = chart.chartType == Sig("2DXY");
				rgb_color textColor = HighColor();
				float slot = (plot.right - plot.left) / float(categories);
				float xLow, xHigh;
				ChartAxisRange(horizontal, xLow, xHigh);
				if (xy) {
					// T2DXAxis::Draw: the line; from the range's low by its
					// step a tick 3 down, the gridline, the value centered 11
					// under the line
					ShapeStyle hs = ResolveShapeStyle(styles, horizontal.style);
					if (usePen(hs))
						StrokeLine(BPoint(plot.left, plot.bottom), BPoint(plot.right, plot.bottom));
					float xStep = ChartAxisStep(horizontal);
					if (xStep > 0) {
						BFont xf = useFont(horizontal.style);
						rgb_color xText = HighColor();
						float across = (plot.right - plot.left) / (xHigh - xLow) * xStep;
						int i = 0;
						for (float v = xLow; v < xStep * 0.5f + xHigh; v += xStep, i++) {
							float x = plot.left + across * float(i);
							if (usePen(hs))
								StrokeLine(BPoint(x, plot.bottom), BPoint(x, plot.bottom + 3));
							if (horizontal.flags[1]) {
								SetHighColor(200, 200, 200);
								StrokeLine(BPoint(x, plot.bottom - 1), BPoint(x, plot.top));
							}
							std::string text = label(v);
							SetHighColor(xText);
							DrawString(text.c_str(), BPoint(x - 0.5f * xf.StringWidth(text.c_str())
								+ 1, plot.bottom + 11));
						}
					}
				} else {
					// T2DHorizontalAxis::Draw: the line, a tick 3 down at each
					// category's edge, its label centered 12 under the line, the
					// gridlines (not at the first edge)
					ShapeStyle hs = ResolveShapeStyle(styles, horizontal.style);
					BFont hf = useFont(horizontal.style);
					textColor = HighColor();
					if (usePen(hs))
						StrokeLine(BPoint(plot.left, plot.bottom), BPoint(plot.right, plot.bottom));
					for (size_t c = 0; c <= categories; c++) {
						float x = plot.left + slot * float(c);
						if (usePen(hs))
							StrokeLine(BPoint(x, plot.bottom), BPoint(x, plot.bottom + 3));
						if (c < categories && c < chart.data.names1.size()) {
							const char* name = chart.data.names1[c].c_str();
							SetHighColor(textColor);
							DrawString(name, BPoint(x + slot * 0.5f - hf.StringWidth(name) * 0.5f,
								plot.bottom + 12));
						}
						if (c != 0 && horizontal.flags[1]) {
							SetHighColor(200, 200, 200);
							StrokeLine(BPoint(x, plot.bottom - 1), BPoint(x, plot.top));
						}
					}
				}

				// T2DVerticalAxis::DrawOrMeasure: the line, from the range's
				// low up by its step: the value 4 left of a tick 3 long left
				// of the line, the gridline (not at the low)
				ShapeStyle vs = ResolveShapeStyle(styles, vertical.style);
				useFont(vertical.style);
				textColor = HighColor();
				if (axisDrawn) {
					if (usePen(vs))
						StrokeLine(BPoint(plot.left, plot.top), BPoint(plot.left, plot.bottom));
					float x = plot.left - 1, y = plot.bottom;
					float rise = -((plot.bottom - plot.top) / (high - low)) * step;
					for (float v = low; v < step * 0.5f + high; v += step, y += rise) {
						std::string text = label(v);
						SetHighColor(textColor);
						DrawString(text.c_str(), BPoint(x - axisFont.StringWidth(text.c_str()) - 4,
							y + 4));
						if (usePen(vs))
							StrokeLine(BPoint(x, y), BPoint(x - 3, y));
						if (vertical.flags[1] && v != low) {
							SetHighColor(200, 200, 200);
							StrokeLine(BPoint(x + 1, y), BPoint(plot.right, y));
						}
					}
				}

				if (xy) {
					// T2DXYChart::Draw2D: each series' points from the axes'
					// zeros, its symbol at each (TChartLegend::DrawSeriesSymbol),
					// then the line through them in its fill color when its
					// pen shows
					float yLow = low, yHigh = high;
					if (yLow == yHigh)
						yHigh = yLow + 1;
					float xl = xLow, xh = xHigh;
					if (xl == xh)
						xh = xl + 1;
					float xZero = xLow == xHigh ? plot.left
						: plot.left + (plot.right - plot.left) / (xHigh - xLow) * -xLow;
					float yZero = low == high ? plot.bottom
						: plot.bottom + (plot.bottom - plot.top) / (high - low) * low;
					for (size_t s = 0; s < chart.SeriesCount(); s++) {
						uint32_t style = seriesStyle(uint32_t(s));
						ShapeStyle ss = ResolveShapeStyle(styles, style);
						SeriesSymbol symbol = SymbolOf(styles, style);
						std::vector<BPoint> points;
						for (size_t c = 0; c < categories; c++) {
							BPoint p(xZero + chart.Value(chart.PhysicalSeries(s, 0), c)
									* ((plot.right - plot.left) / (xh - xl)),
								yZero - chart.Value(chart.PhysicalSeries(s, 1), c)
									* ((plot.bottom - plot.top) / (yHigh - yLow)));
							points.push_back(p);
							const float (*shape)[2] = NULL;
							int count = 0;
							if (symbol.type == 1 || symbol.type == 2) {
								shape = kSquare;
								count = 5;
							} else if (symbol.type == 3 || symbol.type == 4) {
								shape = kTriangle;
								count = 4;
							} else if (symbol.type == 5 || symbol.type == 6) {
								shape = kDiamond;
								count = 5;
							}
							if (shape == NULL)
								continue;
							BRect box(p.x - symbol.size / 2, p.y - symbol.size / 2,
								p.x + symbol.size / 2, p.y + symbol.size / 2);
							std::vector<BPoint> corners;
							for (int k = 0; k < count; k++)
								corners.push_back(BPoint(box.left + shape[k][0] * box.Width(),
									box.top + shape[k][1] * box.Height()));
							if (symbol.colored)
								SetHighColor(InkColor(symbol.color, 1));
							else
								SetHighColor(0, 0, 0);
							SetPenSize(std::max(1.0f, ss.penSize));
							if (symbol.type % 2 == 1)
								FillPolygon(corners.data(), int32(corners.size()));
							else
								StrokePolygon(corners.data(), int32(corners.size()), false);
						}
						if (ss.penOpacity > 0 && ss.fill.set && points.size() > 1) {
							SetHighColor(InkColor(ss.fill.color, ss.fillOpacity));
							SetPenSize(std::max(1.0f, ss.penSize));
							StrokePolygon(points.data(), int32(points.size()), false);
						}
					}
				} else {
					// the series sets (T2DBarSeries, T2DLineSeries, T2DAreaSeries
					// and T2DStackedAreaSeries::SeriesSetDraw): values from the
					// vertical axis's zero (ZeroPoint), a category's width
					float scale = (plot.bottom - plot.top) / (high - low);
					float zero = low == high ? plot.bottom : plot.bottom + scale * low;
					for (int pass : {1, 6})
						for (const ChartSeriesSet& set : chart.sets) {
							size_t count = set.series.size();
							if (set.kind == 0 && pass == 1) {
								// bars: a tenth of the category left free at its
								// left, the rest shared by the set's series
								float gap = slot / 10;
								float bar = (slot - gap) / float(count);
								for (size_t i = 0; i < count; i++) {
									ShapeStyle ss = ResolveShapeStyle(styles, seriesStyle(set.series[i]));
									for (size_t c = 0; c < categories; c++) {
										float v = chart.Value(set.series[i], c);
										if (v == 0)
											continue;
										float x = plot.left + gap + slot * float(c) + bar * float(i);
										float top = zero - v * scale;
										BRect box(x, std::min(top, zero), x + bar, std::max(top, zero));
										if (useFill(ss))
											FillRect(box);
										if (usePen(ss))
											StrokeRect(box);
									}
								}
							} else if (set.kind == 1 && pass == 6) {
								// lines: through each category's middle, in the
								// series' fill color
								for (uint32_t s : set.series) {
									ShapeStyle ss = ResolveShapeStyle(styles, seriesStyle(s));
									if (!ss.fill.set)
										continue;
									SetHighColor(InkColor(ss.fill.color, ss.fillOpacity));
									SetPenSize(std::max(1.0f, ss.penSize));
									for (size_t c = 1; c < categories; c++)
										StrokeLine(BPoint(plot.left + slot * (float(c) - 0.5f),
												zero - chart.Value(s, c - 1) * scale),
											BPoint(plot.left + slot * (float(c) + 0.5f),
												zero - chart.Value(s, c) * scale));
								}
							} else if ((set.kind == 2 || set.kind == 3) && pass == 1) {
								// areas: from the zero line, or (stacked) from the
								// series before in the set, filled
								std::vector<BPoint> below;
								for (size_t c = 0; c < categories; c++)
									below.push_back(BPoint(plot.left + slot * (float(c) + 0.5f), zero));
								for (uint32_t s : set.series) {
									std::vector<BPoint> points;
									for (size_t c = 0; c < categories; c++)
										points.push_back(BPoint(below[c].x,
											below[c].y - chart.Value(s, c) * scale));
									std::vector<BPoint> polygon = points;
									polygon.insert(polygon.end(), below.rbegin(), below.rend());
									ShapeStyle ss = ResolveShapeStyle(styles, seriesStyle(s));
									if (useFill(ss))
										FillPolygon(polygon.data(), int32(polygon.size()));
									if (set.kind == 3)
										below = points;
								}
							}
						}
				}
				SetPenSize(1);
			} else if (piece.kind == Sig("Lgnd")) {
				for (size_t s = 0; s < chart.series.size(); s++) {
					BRect box(pr.left + chart.legendRect[0] + 5,
						pr.top + chart.legendRect[1] + 4 + 12 * float(s), 0, 0);
					box.right = box.left + 10;
					box.bottom = box.top + 10;
					ShapeStyle ss = ResolveShapeStyle(styles, chart.series[s].style);
					if (useFill(ss))
						FillRect(box);
					SetHighColor(0, 0, 0);
					SetPenSize(1);
					StrokeRect(box);
					useFont(chart.series[s].style);
					DrawString(chart.series[s].name.c_str(), BPoint(box.left + 14, box.bottom));
				}
			} else if (piece.kind == Sig("Titl")) {
				useFont(piece.style);
				DrawString(piece.text.c_str(),
					BPoint(pr.left + piece.radius[0], pr.top + piece.radius[1]));
			}
			PopState();
			if (usePen(pieceStyle))
				StrokeRect(pr);
			SetPenSize(1);
		}
	PopState();
}


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
	// (fixup_text, TLegendPiece::FixSize: text in its style's font)
	TextMeasure measure = [this](const std::string& text, uint32_t style, float& width,
		float& height) {
		BFont font = TDocView::MapFont(ResolveTextStyle(fDocument.styles, style));
		font_height fh;
		font.GetHeight(&fh);
		width = font.StringWidth(text.c_str());
		height = fh.ascent + fh.descent;
	};
	if (what == 'sCHd' || what == 'sCHe') {
		// THideShowPieceCmd: the piece shown or hidden ("Show Piece",
		// "Hide Piece", by what it will be)
		uint32_t kind = what == 'sCHd' ? Sig("Lgnd") : Sig("Titl");
		const Shape* piece = ChartPiece(*fDocument.Chart(fChart.part), kind);
		bool show = !PieceVisible(piece);
		fUndo.BeginDocument(fDocument, ChartString(3, show ? 1 : 2).String(),
			CurrentViewState());
		if (!ShowChartPiece(fDocument, fChart.part, kind, show, measure)) {
			beep();
			return true;
		}
	} else if (what == 'sCHc') {
		// TToggleSeriesOrderCmd: "Change Series Order"
		fUndo.BeginDocument(fDocument, ChartString(3, 0).String(), CurrentViewState());
		if (!ToggleChartSeriesOrder(fDocument, fChart.part, measure)) {
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
		const void* data = NULL;
		ssize_t size = 0;
		if (message->FindData("ChartType", B_RAW_TYPE, &data, &size) != B_OK
				|| size != ssize_t(2 * sizeof(int32))) {
			// TDialogChart with the chart's type
			uint32_t major, minor;
			ChartTypeOf(*fDocument.Chart(fChart.part), major, minor);
			if (Window() != NULL)
				(new TDialogChart(Window(), major, minor))->RunDialog();
			return true;
		}
		// TChangeChartTypeCmd: "Change Chart Type"
		int32 type[2];
		memcpy(type, data, sizeof type);
		fUndo.BeginDocument(fDocument, ChartString(3, 3).String(), CurrentViewState());
		if (!ChangeChartType(fDocument, fChart.part, uint32_t(type[0]), uint32_t(type[1]),
				measure)) {
			beep();
			return true;
		}
	}
	Invalidate();
	if (Window() != NULL)
		Window()->PostMessage(kMsgDocumentChanged);
	return true;
}

} // namespace gobe
