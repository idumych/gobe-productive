// The drawing tools in the page view (TPartGR::DragOut, TGRRubberBand,
// TShape's handles): a shape tool drags a shape out in the drawing under
// the pointer (the polygon tool: a point per click, a double click or
// Enter ends it); the arrow selects a shape, moves it or, by a corner
// handle, resizes it; Delete removes it. Each change is one undo step
// (STR# 1101: Create Shape, Delete Shape(s), Resize Shapes, Move Shapes).
#include "TPageView.h"

#include <Window.h>

#include <algorithm>
#include <cmath>

#include "Drawing.h"
#include "GobeResources.h"
#include "TRibbon.h"

namespace gobe {

namespace {

const float kHandle = 3;		// a handle's half size
const float kTolerance = 3;		// how near a line counts as on it


std::vector<bool>
HiddenLayers(const Document& doc, size_t part)
{
	std::vector<bool> hidden;
	if (const GRPart* gr = doc.GraphicsPart(part))
		for (size_t l = 0; l < gr->layers.size(); l++)
			hidden.push_back(doc.LayerHidden(part, l));
	return hidden;
}


// the layer new shapes go on: the first shown
size_t
DrawingLayer(const Document& doc, size_t part)
{
	const GRPart* gr = doc.GraphicsPart(part);
	if (gr == nullptr)
		return 0;
	for (size_t l = 0; l < gr->layers.size(); l++)
		if (!doc.LayerHidden(part, l))
			return l;
	return 0;
}

} // namespace


void
TPageView::SetTool(uint32 tool)
{
	if (fShapeDrag.mode == 1 && fTool == kToolPolygon)
		FinishShape();
	fShapeDrag = ShapeDrag();
	fTool = tool;
	if (fToolBar != NULL)
		fToolBar->Update();
}


bool
TPageView::DrawingAt(BPoint where, size_t& part, float& x, float& y) const
{
	float dx = fMargin - fBounds.left, dy = fMargin - fBounds.top;
	const Document::PlacedFrame* found = nullptr;
	for (const Document::PlacedFrame& f : fFrames) {
		uint32_t kind = fDocument.parts.parts[f.part].kind;
		if ((kind != Sig("GRFX") && kind != Sig("PRES")) || fDocument.GraphicsPart(f.part) == nullptr)
			continue;
		BRect r(f.x + dx, f.y + dy, f.x + dx + f.width, f.y + dy + f.height);
		if (!r.Contains(where))
			continue;
		if (found == nullptr || f.depth >= found->depth)
			found = &f;
	}
	if (found == nullptr)
		return false;
	part = found->part;
	x = found->x + dx;
	y = found->y + dy;
	return true;
}


bool
TPageView::DrawingClick(BPoint where, int32 clicks)
{
	// the polygon tool: each click a point; a double click ends it
	if (fShapeDrag.mode == 1 && fTool == kToolPolygon) {
		fShapeDrag.points.back() = {where.x - fShapeDrag.x, where.y - fShapeDrag.y};
		if (clicks >= 2)
			FinishShape();
		else
			fShapeDrag.points.push_back(fShapeDrag.points.back());
		Invalidate();
		return true;
	}
	if (ToolMakesShapes(fTool) || fTool == kToolPart) {
		size_t part;
		float x, y;
		if (!DrawingAt(where, part, x, y))
			return true;		// (nothing to draw in: the click does nothing)
		fShapeDrag = ShapeDrag();
		fShapeDrag.mode = 1;
		fShapeDrag.part = part;
		fShapeDrag.x = x;
		fShapeDrag.y = y;
		std::array<float, 2> p = {where.x - x, where.y - y};
		fShapeDrag.points = {p, p};
		fShape = ShapeRef();
		Invalidate();
		return true;
	}
	if (fTool != kToolArrow)
		return false;
	// the selected shape's handles first, then the shapes under the point
	if (fShape.active) {
		const GRPart* gr = fDocument.GraphicsPart(fShape.part);
		size_t part;
		float x, y;
		if (gr != nullptr && fShape.layer < gr->layers.size()
				&& fShape.index < gr->layers[fShape.layer].shapes.size()
				&& DrawingAt(where, part, x, y) && part == fShape.part) {
			const Shape& s = gr->layers[fShape.layer].shapes[fShape.index];
			int handle = HitHandle(s, where.x - x, where.y - y, kHandle + 1);
			if (handle >= 0) {
				fShapeDrag = ShapeDrag();
				fShapeDrag.mode = 3;
				fShapeDrag.part = part;
				fShapeDrag.x = x;
				fShapeDrag.y = y;
				fShapeDrag.handle = handle;
				ShapeBounds(s, fShapeDrag.bounds);
				fShapeDrag.start[0] = fShapeDrag.now[0] = where.x - x;
				fShapeDrag.start[1] = fShapeDrag.now[1] = where.y - y;
				return true;
			}
		}
	}
	size_t part;
	float x, y;
	size_t layer, index;
	if (DrawingAt(where, part, x, y) && HitShape(*fDocument.GraphicsPart(part), where.x - x,
			where.y - y, kTolerance, HiddenLayers(fDocument, part), layer, index)) {
		fShape.active = true;
		fShape.part = part;
		fShape.layer = layer;
		fShape.index = index;
		fShapeDrag = ShapeDrag();
		fShapeDrag.mode = 2;
		fShapeDrag.part = part;
		fShapeDrag.x = x;
		fShapeDrag.y = y;
		fShapeDrag.start[0] = fShapeDrag.now[0] = where.x - x;
		fShapeDrag.start[1] = fShapeDrag.now[1] = where.y - y;
		Invalidate();
		return true;
	}
	if (fShape.active) {
		fShape = ShapeRef();
		Invalidate();
	}
	return false;
}


bool
TPageView::DrawingMoved(BPoint where)
{
	if (fShapeDrag.mode == 0)
		return false;
	float x = where.x - fShapeDrag.x, y = where.y - fShapeDrag.y;
	if (fShapeDrag.mode == 1) {
		std::array<float, 2> p = {x, y};
		// freehand: a point wherever the pointer went, the others the end
		if (fTool == kToolFree) {
			const std::array<float, 2>& last = fShapeDrag.points[fShapeDrag.points.size() - 2];
			if (std::fabs(last[0] - x) + std::fabs(last[1] - y) >= 3) {
				fShapeDrag.points.back() = p;
				fShapeDrag.points.push_back(p);
			}
		}
		fShapeDrag.points.back() = p;
	} else {
		fShapeDrag.now[0] = x;
		fShapeDrag.now[1] = y;
	}
	Invalidate();
	return true;
}


void
TPageView::FinishShape()
{
	std::vector<DrawPoint> points(fShapeDrag.points.begin(), fShapeDrag.points.end());
	size_t part = fShapeDrag.part;
	fShapeDrag = ShapeDrag();
	// the part tool: a part frame (TPageFrames.cpp)
	if (fTool == kToolPart) {
		if (!FinishPartFrame(part, points))
			Invalidate();
		return;
	}
	if (fTool == kToolPolygon && points.size() > 2)
		points.pop_back();		// (the point following the pointer)
	size_t layer = DrawingLayer(fDocument, part);
	if (!ShapeHasSize(fTool, points)) {
		Invalidate();
		return;		// (a click without a drag: no shape)
	}
	fUndo.BeginDocument(fDocument, GobeResources::IndexString(1101, 0).String(),
		CurrentViewState());
	int index = AddShape(fDocument, part, layer, fTool, points);
	if (index < 0) {
		Invalidate();
		return;
	}
	fShape.active = true;
	fShape.part = part;
	fShape.layer = layer;
	fShape.index = size_t(index);
	// (the tool goes back to the arrow, as TDocWindow does after a shape)
	fTool = kToolArrow;
	if (fToolBar != NULL)
		fToolBar->Update();
	Invalidate();
	if (Window() != NULL)
		Window()->PostMessage(kMsgDocumentChanged);
}


bool
TPageView::DrawingUp(BPoint where)
{
	if (fShapeDrag.mode == 0)
		return false;
	DrawingMoved(where);
	if (fShapeDrag.mode == 1) {
		if (fTool != kToolPolygon)
			FinishShape();
		return true;
	}
	float dx = fShapeDrag.now[0] - fShapeDrag.start[0];
	float dy = fShapeDrag.now[1] - fShapeDrag.start[1];
	int mode = fShapeDrag.mode;
	ShapeDrag drag = fShapeDrag;
	fShapeDrag = ShapeDrag();
	if (dx == 0 && dy == 0) {
		Invalidate();
		return true;
	}
	fUndo.BeginDocument(fDocument,
		GobeResources::IndexString(1101, mode == 2 ? 7 : 6).String(), CurrentViewState());
	GRPart* gr = fDocument.EditableGraphicsPart(fShape.part);
	if (gr != nullptr && fShape.layer < gr->layers.size()
			&& fShape.index < gr->layers[fShape.layer].shapes.size()) {
		Shape& s = gr->layers[fShape.layer].shapes[fShape.index];
		if (mode == 2)
			MoveShape(s, dx, dy);
		else {
			float bounds[4];
			DragHandle(drag.bounds, drag.handle, drag.now[0], drag.now[1], bounds);
			ResizeShape(s, bounds);
		}
	}
	Invalidate();
	if (Window() != NULL)
		Window()->PostMessage(kMsgDocumentChanged);
	return true;
}


bool
TPageView::DrawingKey(const char* bytes, int32 count)
{
	if (count < 1)
		return false;
	if (fShapeDrag.mode == 1 && fTool == kToolPolygon
			&& (bytes[0] == B_ENTER || bytes[0] == B_ESCAPE)) {
		FinishShape();
		return true;
	}
	if (!fShape.active || (bytes[0] != B_DELETE && bytes[0] != B_BACKSPACE))
		return false;
	fUndo.BeginDocument(fDocument, GobeResources::IndexString(1101, 2).String(),
		CurrentViewState());
	GRPart* gr = fDocument.EditableGraphicsPart(fShape.part);
	if (gr != nullptr && fShape.layer < gr->layers.size()
			&& fShape.index < gr->layers[fShape.layer].shapes.size()) {
		std::vector<Shape>& shapes = gr->layers[fShape.layer].shapes;
		shapes.erase(shapes.begin() + long(fShape.index));
	}
	fShape = ShapeRef();
	Invalidate();
	if (Window() != NULL)
		Window()->PostMessage(kMsgDocumentChanged);
	return true;
}


// the shape being dragged out (its outline), the one moved or resized,
// and the selected shape's handles (TShape::DrawHandle: small squares at
// the corners)
void
TPageView::DrawShapeSelection(float dx, float dy)
{
	(void)dx;
	(void)dy;
	PushState();
	SetPenSize(1);
	SetDrawingMode(B_OP_COPY);
	if (fShapeDrag.mode == 1 && fShapeDrag.points.size() >= 2) {
		SetHighColor(0, 0, 0);
		float ox = fShapeDrag.x, oy = fShapeDrag.y;
		const auto& a = fShapeDrag.points.front();
		const auto& b = fShapeDrag.points.back();
		BRect r(std::min(a[0], b[0]) + ox, std::min(a[1], b[1]) + oy,
			std::max(a[0], b[0]) + ox, std::max(a[1], b[1]) + oy);
		if (fTool == kToolLine)
			StrokeLine(BPoint(a[0] + ox, a[1] + oy), BPoint(b[0] + ox, b[1] + oy), B_MIXED_COLORS);
		else if (fTool == kToolEllipse)
			StrokeEllipse(r, B_MIXED_COLORS);
		else if (fTool == kToolRoundRect)
			StrokeRoundRect(r, 25, 25, B_MIXED_COLORS);
		else if (fTool == kToolArc)
			StrokeArc(r, 0, 90, B_MIXED_COLORS);
		else if (fTool == kToolFree || fTool == kToolPolygon) {
			for (size_t i = 1; i < fShapeDrag.points.size(); i++)
				StrokeLine(BPoint(fShapeDrag.points[i - 1][0] + ox, fShapeDrag.points[i - 1][1] + oy),
					BPoint(fShapeDrag.points[i][0] + ox, fShapeDrag.points[i][1] + oy),
					B_MIXED_COLORS);
		} else
			StrokeRect(r, B_MIXED_COLORS);
	}
	const GRPart* gr = fShape.active ? fDocument.GraphicsPart(fShape.part) : nullptr;
	if (gr != nullptr && fShape.layer < gr->layers.size()
			&& fShape.index < gr->layers[fShape.layer].shapes.size()) {
		float ox = 0, oy = 0;
		float ddx = fMargin - fBounds.left, ddy = fMargin - fBounds.top;
		for (const Document::PlacedFrame& f : fFrames)
			if (f.part == fShape.part) {
				ox = f.x + ddx;
				oy = f.y + ddy;
			}
		float b[4];
		ShapeBounds(gr->layers[fShape.layer].shapes[fShape.index], b);
		if (fShapeDrag.mode == 2) {
			float mx = fShapeDrag.now[0] - fShapeDrag.start[0];
			float my = fShapeDrag.now[1] - fShapeDrag.start[1];
			b[0] += mx;
			b[2] += mx;
			b[1] += my;
			b[3] += my;
		} else if (fShapeDrag.mode == 3) {
			float out[4];
			DragHandle(fShapeDrag.bounds, fShapeDrag.handle, fShapeDrag.now[0],
				fShapeDrag.now[1], out);
			std::copy(out, out + 4, b);
		}
		BRect box(b[0] + ox, b[1] + oy, b[2] + ox, b[3] + oy);
		if (fShapeDrag.mode != 0) {
			SetHighColor(0, 0, 0);
			StrokeRect(box, B_MIXED_COLORS);
		}
		SetHighColor(0, 0, 0);
		for (BPoint corner : {box.LeftTop(), box.RightTop(), box.RightBottom(), box.LeftBottom()})
			FillRect(BRect(corner.x - kHandle, corner.y - kHandle, corner.x + kHandle,
				corner.y + kHandle));
	}
	PopState();
}

} // namespace gobe
