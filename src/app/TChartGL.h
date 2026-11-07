// TChartView, T3DChart and its kinds, T3DRenderer: a 3D chart drawn with
// OpenGL into a bitmap off screen, as TRenderingPiece::DrawContent has it
// drawn (the original's BGLView sits in a BBitmap; this one in a window
// never shown, its frames copied out).
#ifndef GOBE_TCHART_GL_H
#define GOBE_TCHART_GL_H

#include <Bitmap.h>

#include <memory>
#include <string>
#include <vector>

#include "ChartPart.h"
#include "Styles.h"

namespace gobe {

struct ChartRender3D {
	std::string key;		// what it was drawn from
	std::unique_ptr<BBitmap> bitmap;	// B_RGBA32: what isn't the chart is clear
	// T3DVerticalAxis::DrawLabels: each label's text and its place in the piece
	struct Label {
		float x, y;
		std::string text;
	};
	std::vector<Label> labels;
};

// what a render of the chart at the size depends on
std::string ChartRenderKey(const ChartPart& chart, const StyleMgr& styles, int32 width,
	int32 height);

// the chart drawn at the size (TChartView::DoDraw), false if it couldn't be;
// `out` is then empty
bool RenderChart3D(const ChartPart& chart, const StyleMgr& styles, int32 width, int32 height,
	ChartRender3D& out);

// TChartView::FindObject: what's at the piece's point: 0 the vertical axis,
// 1 the horizontal one, 2 and on a series (by its click test color), -1 nothing
int32 FindChart3DObject(const ChartPart& chart, const StyleMgr& styles, int32 width,
	int32 height, BPoint where);

} // namespace gobe

#endif
