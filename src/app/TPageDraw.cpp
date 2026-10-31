// The drawing tools in the page view (TPartGR::DragOut, TGRRubberBand,
// TShape's handles, MENU 50 Arrange): a shape tool drags a shape out in
// the drawing under the pointer (the polygon tool: a point per click, a
// double click or Enter ends it); the arrow selects shapes (Shift adds or
// takes one away, a drag over empty drawing those inside the band),
// moves them, and by a corner handle resizes, rotates or reshapes one
// (Arrange > Resize, Rotate, Reshape: a path's points, a line's ends);
// Delete removes them; Arrange's Bring to Front, Send to Back, Move
// Forward and Move Backward; the FormatBar's fill, pen and transparency.
// Each change is one undo step (STR# 1101's labels).
#include "TPageView.h"

#include <Menu.h>
#include <Shape.h>
#include <MenuItem.h>
#include <Window.h>

#include <algorithm>
#include <cmath>

#include "DocArchive.h"
#include "Drawing.h"
#include "GobeResources.h"
#include "SSStyle.h"
#include "TRibbon.h"
#include "TDialog.h"
#include "TSheetDialogs.h"

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


const Shape*
ShapeAt(const Document& doc, size_t part, size_t layer, size_t index)
{
	const GRPart* gr = doc.GraphicsPart(part);
	if (gr == nullptr || layer >= gr->layers.size() || index >= gr->layers[layer].shapes.size())
		return nullptr;
	return &gr->layers[layer].shapes[index];
}


// the ink a FormatBar command chose: "ink" (an ink set's pattern or
// gradient, its bytes) or "color" (a color set's), as `type`'s property
bool
InkFromMessage(BMessage* message, uint32_t type, Property& property)
{
	const void* data;
	ssize_t size;
	Ink ink;
	if (message->FindData("ink", B_RAW_TYPE, &data, &size) == B_OK && size > 0) {
		std::vector<uint8_t> bytes((const uint8*)data, (const uint8*)data + size);
		try {
			DocArchive a(bytes);
			ReadInk(a, ink);
		} catch (const ArchiveError&) {
			return false;
		}
	} else if (message->FindData("color", B_RGB_COLOR_TYPE, &data, &size) == B_OK && size >= 4) {
		const rgb_color* c = (const rgb_color*)data;
		ink.set = true;
		ink.color.r = c->red;
		ink.color.g = c->green;
		ink.color.b = c->blue;
		ink.color.a = 255;
	} else
		return false;
	property = InkProperty(type, ink);
	return true;
}


// a toolbar shape (TToolbarShapes: the application's 'POLY' resources)
bool
ToolbarShapePath(int32 index, BezierPath& path)
{
	const ResourceFile::Resource* r = GobeResources::File().Find('POLY', index);
	return r != NULL && ParseToolbarShape(r->data, path);
}


// TDialogAlignObjects (DIAL 202): None, Top Edges, Centers, Bottom Edges
// and Distribute Space top to bottom (3-7), the same left to right
// (9-13); each choice is tried at once ('sM44' with "AlignY", "AlignX",
// the last try taken back first: SendKillLastMessage); OK keeps it,
// Cancel takes it back
class TDialogAlignObjects : public TDialog {
public:
	TDialogAlignObjects(BLooper* window)
		: TDialog(202, 202, 'APPL', NULL), fWindow(window) {}

protected:
	virtual void Init()
	{
		SetRadioItem(fY, true);
		SetRadioItem(fX, true);
	}

	virtual bool ItemHit(int32 index, BMessage* message)
	{
		if ((index >= 3 && index <= 7) || (index >= 9 && index <= 13)) {
			(index <= 7 ? fY : fX) = index;
			BMessage align('sM44');
			align.AddInt32("AlignY", fY - 3);
			align.AddInt32("AlignX", fX - 9);
			align.AddBool("preview", true);
			fWindow->PostMessage(&align);
			return true;
		}
		if (index == 0 || index == 1) {
			BMessage end('sM44');
			end.AddBool(index == 0 ? "done" : "cancel", true);
			fWindow->PostMessage(&end);
		}
		return TDialog::ItemHit(index, message);
	}

private:
	BLooper* fWindow;
	int32 fY = 3, fX = 9;
};


TPageView::ShapeRef
Ref(size_t part, size_t layer, size_t index)
{
	TPageView::ShapeRef ref;
	ref.active = true;
	ref.part = part;
	ref.layer = layer;
	ref.index = index;
	return ref;
}

} // namespace


namespace {

// AutoGrid (TGrid +0x4): a point snapped to the drawing's grid
void
Snap(const Document& doc, size_t part, float& x, float& y)
{
	const GRPart* gr = doc.GraphicsPart(part);
	if (gr == nullptr || !gr->gridFlags[0])
		return;
	x = SnapToGrid(x, gr->gridSpacing);
	y = SnapToGrid(y, gr->gridSpacing);
}


// the drawing Arrange's grid commands act on: the selected shapes', or a
// drawing document's
bool
CurrentDrawing(const Document& doc, const std::vector<TPageView::ShapeRef>& shapes, size_t& part)
{
	if (!shapes.empty()) {
		part = shapes[0].part;
		return true;
	}
	int main = doc.MainPart();
	if (main < 0 || doc.GraphicsPart(size_t(main)) == nullptr)
		return false;
	part = size_t(main);
	return true;
}

} // namespace


bool
TPageView::ShapeSelected(size_t part, size_t layer, size_t index) const
{
	for (const ShapeRef& s : fShapes)
		if (s.part == part && s.layer == layer && s.index == index)
			return true;
	return false;
}


void
TPageView::SetTool(uint32 tool)
{
	if (fShapeDrag.mode == 1 && fTool == kToolPolygon)
		FinishShape();
	else if (fShapeDrag.mode == 1 && fTool == kToolBezier)
		FinishShape(false);
	fShapeDrag = ShapeDrag();
	fBezier.clear();
	fTool = tool;
	if (fToolBar != NULL)
		fToolBar->Update();
}


bool
TPageView::SelectedShapeStyle(ShapeStyle& style) const
{
	if (fShapes.empty())
		return false;
	const Shape* s = ShapeAt(fDocument, fShapes[0].part, fShapes[0].layer, fShapes[0].index);
	if (s == nullptr)
		return false;
	style = ResolveShapeStyle(fDocument.styles, s->style);
	return true;
}


void
TPageView::FixShapeMenu(BMenu* menu) const
{
	// (TPartGR::FixMenus: the ported items, Arrange's with shapes
	// selected; the handles' mode marked; the others off)
	static const uint32 kModes[3] = {'sM4A', 'sM4B', 'sM4C'};
	bool locked = false, unlocked = false;
	for (const ShapeRef& ref : fShapes)
		if (const Shape* s = ShapeAt(fDocument, ref.part, ref.layer, ref.index))
			(ShapeLocked(*s) ? locked : unlocked) = true;
	size_t drawing;
	const GRPart* gr = CurrentDrawing(fDocument, fShapes, drawing)
		? fDocument.GraphicsPart(drawing) : nullptr;
	for (int32 i = 0; BMenuItem* item = menu->ItemAt(i); i++) {
		uint32 what = item->Message() != NULL ? item->Message()->what : 0;
		bool arrange = what == 'sM40' || what == 'sM41' || what == 'sM42' || what == 'sM43'
			|| what == 'sM45' || what == 'sM4T';
		bool mode = what == 'sM4A' || what == 'sM4B' || what == 'sM4C';
		bool grid = what == 'sM4D' || what == 'sM4E';
		// (Group, Ungroup and Text Wrap aren't ported: off)
		item->SetEnabled((arrange && !fShapes.empty()) || mode || (grid && gr != nullptr)
			|| (what == 'sM44' && fShapes.size() >= 2) || (what == 'sM48' && unlocked)
			|| (what == 'sM49' && locked));
		if (mode)
			item->SetMarked(kModes[fShapeMode] == what);
		if (grid)
			item->SetMarked(gr != nullptr && gr->gridFlags[what == 'sM4D' ? 1 : 0]);
	}
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
	bool shift = ((modifiers() & B_SHIFT_KEY) != 0) || fScriptShift;
	// the polygon tool: each click a point; a double click ends it
	if (fShapeDrag.mode == 1 && fTool == kToolPolygon) {
		fShapeDrag.points.back() = {where.x - fShapeDrag.x, where.y - fShapeDrag.y};
		if (clicks >= 2)
			FinishShape(false);		// (the double click's point is the last)
		else
			fShapeDrag.points.push_back(fShapeDrag.points.back());
		Invalidate();
		return true;
	}
	// the Bezier tool: each press a point, a drag from it its control
	// points (a smooth point); a double click or Enter ends the path
	if (fTool == kToolBezier) {
		size_t part;
		float x, y;
		bool building = (fShapeDrag.mode == 1 || fShapeDrag.mode == 7) && !fBezier.empty();
		if (building) {
			part = fShapeDrag.part;
			x = fShapeDrag.x;
			y = fShapeDrag.y;
		} else if (!DrawingAt(where, part, x, y))
			return true;
		else {
			fBezier.clear();
			fShapes.clear();
		}
		float px = where.x - x, py = where.y - y;
		Snap(fDocument, part, px, py);
		Vertex v = {{px, py}, {0, 0}, {0, 0}, 4};
		if (clicks >= 2 && !fBezier.empty()) {
			if (fBezier.back().point[0] != px || fBezier.back().point[1] != py)
				fBezier.push_back(v);
			FinishShape(false);
			return true;
		}
		fBezier.push_back(v);
		fShapeDrag = ShapeDrag();
		fShapeDrag.mode = 7;
		fShapeDrag.part = part;
		fShapeDrag.x = x;
		fShapeDrag.y = y;
		fShapeDrag.start[0] = fShapeDrag.now[0] = px;
		fShapeDrag.start[1] = fShapeDrag.now[1] = py;
		Invalidate();
		return true;
	}
	if (ToolMakesShapes(fTool) || fTool == kToolShapes || fTool == kToolPart) {
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
		Snap(fDocument, part, p[0], p[1]);
		fShapeDrag.points = {p, p};
		fShapes.clear();
		Invalidate();
		return true;
	}
	if (fTool != kToolArrow)
		return false;
	size_t part;
	float x, y;
	bool inDrawing = DrawingAt(where, part, x, y);
	auto startDrag = [&](int mode) {
		fShapeDrag = ShapeDrag();
		fShapeDrag.mode = mode;
		fShapeDrag.part = part;
		fShapeDrag.x = x;
		fShapeDrag.y = y;
		fShapeDrag.start[0] = fShapeDrag.now[0] = where.x - x;
		fShapeDrag.start[1] = fShapeDrag.now[1] = where.y - y;
	};
	// one selected shape's handles first: resizing, rotating or a point
	// reshaped
	if (fShapes.size() == 1 && inDrawing && part == fShapes[0].part && !shift) {
		const Shape* s = ShapeAt(fDocument, part, fShapes[0].layer, fShapes[0].index);
		float px = where.x - x, py = where.y - y;
		bool editable = s != nullptr && !ShapeLocked(*s);
		int control = editable && fShapeMode == 2 ? HitControlPoint(*s, px, py, kHandle + 1) : -1;
		int vertex = editable && fShapeMode == 2 && control < 0
			? HitVertex(*s, px, py, kHandle + 1) : -1;
		int handle = editable && fShapeMode != 2 ? HitHandle(*s, px, py, kHandle + 1) : -1;
		if (control >= 0 || vertex >= 0 || handle >= 0) {
			startDrag(control >= 0 ? 8 : vertex >= 0 ? 5 : fShapeMode == 1 ? 4 : 3);
			fShapeDrag.handle = control >= 0 ? control : vertex >= 0 ? vertex : handle;
			ShapeBounds(*s, fShapeDrag.bounds);
			fShapeDrag.angle = s->angle;
			return true;
		}
	}
	size_t layer, index;
	if (inDrawing && HitShape(*fDocument.GraphicsPart(part), where.x - x, where.y - y,
			kTolerance, HiddenLayers(fDocument, part), layer, index)) {
		bool selected = ShapeSelected(part, layer, index);
		if (!fShapes.empty() && fShapes[0].part != part)
			fShapes.clear();
		if (shift && selected) {
			// Shift: taken away from the selection
			fShapes.erase(std::remove_if(fShapes.begin(), fShapes.end(),
				[&](const ShapeRef& s) { return s.layer == layer && s.index == index; }),
				fShapes.end());
			Invalidate();
			return true;
		}
		if (!selected) {
			if (!shift)
				fShapes.clear();
			fShapes.push_back(Ref(part, layer, index));
		}
		startDrag(2);
		Invalidate();
		return true;
	}
	// empty drawing: a rubber band (TGRRubberBand) in a drawing document
	// (in a text document the click goes to the text)
	int main = fDocument.MainPart();
	if (inDrawing && main >= 0 && size_t(main) == part) {
		// (in a frame the drawing shows, the click is the frame's part's)
		float vx = fMargin - fBounds.left, vy = fMargin - fBounds.top;
		for (const Document::PlacedFrame& f : fFrames)
			if (f.frame != NULL && f.frame->container == part
					&& BRect(f.x + vx, f.y + vy, f.x + vx + f.width, f.y + vy + f.height)
						.Contains(where)) {
				if (!fShapes.empty()) {
					fShapes.clear();
					Invalidate();
				}
				return false;
			}
		if (!shift)
			fShapes.clear();
		startDrag(6);
		Invalidate();
		return true;
	}
	if (!fShapes.empty()) {
		fShapes.clear();
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
	if (fShapeDrag.mode != 2 && fShapeDrag.mode != 4 && fShapeDrag.mode != 6)
		Snap(fDocument, fShapeDrag.part, x, y);
	if (fShapeDrag.mode == 7) {
		// a Bezier point's control points: the one after it where the
		// pointer is, the one before opposite (a smooth point)
		Vertex& v = fBezier.back();
		v.control2[0] = x - v.point[0];
		v.control2[1] = y - v.point[1];
		v.control1[0] = -v.control2[0];
		v.control1[1] = -v.control2[1];
		v.flags = v.control2[0] != 0 || v.control2[1] != 0 ? 2 : 4;
		Invalidate();
		return true;
	}
	if (fShapeDrag.mode == 1 && fTool == kToolBezier) {
		fShapeDrag.now[0] = x;
		fShapeDrag.now[1] = y;
		Invalidate();
		return true;
	}
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
TPageView::FinishShape(bool dropFollower)
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
	if (fTool == kToolBezier || fTool == kToolShapes) {
		std::vector<Vertex> vertices;
		vertices.swap(fBezier);
		BezierPath shape;
		bool made = fTool == kToolBezier ? vertices.size() >= 2
			: points.size() >= 2 && ToolbarShapePath(fToolShape, shape)
				&& points.front()[0] != points.back()[0] && points.front()[1] != points.back()[1];
		if (!made) {
			Invalidate();
			return;
		}
		size_t layer = DrawingLayer(fDocument, part);
		fUndo.BeginDocument(fDocument, GobeResources::IndexString(1101, 0).String(),
			CurrentViewState());
		int index = fTool == kToolBezier ? AddBezierShape(fDocument, part, layer, vertices)
			: AddToolbarShape(fDocument, part, layer, shape, points.front(), points.back());
		if (index >= 0)
			fShapes = {Ref(part, layer, size_t(index))};
		fTool = kToolArrow;
		if (fToolBar != NULL)
			fToolBar->Update();
		Invalidate();
		if (Window() != NULL)
			Window()->PostMessage(kMsgDocumentChanged);
		return;
	}
	if (dropFollower && fTool == kToolPolygon && points.size() > 2)
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
	fShapes = {Ref(part, layer, size_t(index))};
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
	if (fShapeDrag.mode == 7) {
		// a Bezier point placed: the path goes on
		fShapeDrag.mode = 1;
		return true;
	}
	if (fShapeDrag.mode == 1) {
		if (fTool != kToolPolygon && fTool != kToolBezier)
			FinishShape();
		return true;
	}
	ShapeDrag drag = fShapeDrag;
	fShapeDrag = ShapeDrag();
	if (drag.mode == 6) {
		// the shapes wholly inside the band
		float band[4] = {std::min(drag.start[0], drag.now[0]), std::min(drag.start[1], drag.now[1]),
			std::max(drag.start[0], drag.now[0]), std::max(drag.start[1], drag.now[1])};
		const GRPart* gr = fDocument.GraphicsPart(drag.part);
		std::vector<bool> hidden = HiddenLayers(fDocument, drag.part);
		for (size_t l = 0; gr != nullptr && l < gr->layers.size(); l++) {
			if (l < hidden.size() && hidden[l])
				continue;
			for (size_t i = 0; i < gr->layers[l].shapes.size(); i++) {
				const Shape& s = gr->layers[l].shapes[i];
				float b[4];
				ShapeBounds(s, b);
				if (s.kind == Sig("FRAM") || b[0] < band[0] || b[1] < band[1] || b[2] > band[2]
						|| b[3] > band[3] || ShapeSelected(drag.part, l, i))
					continue;
				fShapes.push_back(Ref(drag.part, l, i));
			}
		}
		Invalidate();
		return true;
	}
	float dx = drag.now[0] - drag.start[0];
	float dy = drag.now[1] - drag.start[1];
	// AutoGrid: a move lands the first shape's corner on the grid
	if (drag.mode == 2 && !fShapes.empty()) {
		if (const Shape* first = ShapeAt(fDocument, drag.part, fShapes[0].layer, fShapes[0].index)) {
			float b[4];
			ShapeBounds(*first, b);
			float x = b[0] + dx, y = b[1] + dy;
			Snap(fDocument, drag.part, x, y);
			dx = x - b[0];
			dy = y - b[1];
		}
	}
	if (dx == 0 && dy == 0) {
		Invalidate();
		return true;
	}
	// STR# 1101: 7 Move Shapes, 6 Resize Shapes, 8 Rotate Shapes, 14
	// Reshape Shapes
	int label = drag.mode == 2 ? 7 : drag.mode == 3 ? 6 : drag.mode == 4 ? 8 : 14;
	fUndo.BeginDocument(fDocument, GobeResources::IndexString(1101, label).String(),
		CurrentViewState());
	GRPart* gr = fDocument.EditableGraphicsPart(drag.part);
	for (const ShapeRef& ref : fShapes) {
		if (gr == nullptr || ref.layer >= gr->layers.size()
				|| ref.index >= gr->layers[ref.layer].shapes.size())
			continue;
		Shape& s = gr->layers[ref.layer].shapes[ref.index];
		if (ShapeLocked(s))
			continue;		// (TShape::GetDeleteMode: a locked shape stays)
		if (drag.mode == 2)
			MoveShape(s, dx, dy);
		else if (drag.mode == 3) {
			float bounds[4];
			DragHandle(drag.bounds, drag.handle, drag.now[0], drag.now[1], bounds);
			ResizeShape(s, bounds);
		} else if (drag.mode == 4) {
			// TShape::SetAngle: by how far the pointer went around the
			// center, in whole degrees
			float turn = AngleAround(s, drag.now[0], drag.now[1])
				- AngleAround(s, drag.start[0], drag.start[1]);
			s.angle = std::fmod(std::round(drag.angle + turn) + 720.0f, 360.0f);
		} else if (drag.mode == 8)
			MoveControlPoint(s, drag.handle, drag.now[0], drag.now[1]);
		else
			MoveVertex(s, drag.handle, drag.now[0], drag.now[1]);
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
	if (fShapeDrag.mode == 1 && (fTool == kToolPolygon || fTool == kToolBezier)
			&& (bytes[0] == B_ENTER || bytes[0] == B_ESCAPE)) {
		FinishShape(fTool == kToolPolygon);
		return true;
	}
	if (fShapes.empty() || (bytes[0] != B_DELETE && bytes[0] != B_BACKSPACE))
		return false;
	fUndo.BeginDocument(fDocument, GobeResources::IndexString(1101, 2).String(),
		CurrentViewState());
	GRPart* gr = fDocument.EditableGraphicsPart(fShapes[0].part);
	// (the last first, so the others keep their places)
	std::vector<ShapeRef> doomed = fShapes;
	std::sort(doomed.begin(), doomed.end(), [](const ShapeRef& a, const ShapeRef& b) {
		return a.layer != b.layer ? a.layer > b.layer : a.index > b.index;
	});
	for (const ShapeRef& ref : doomed)
		if (gr != nullptr && ref.layer < gr->layers.size()
				&& ref.index < gr->layers[ref.layer].shapes.size()
				&& !ShapeLocked(gr->layers[ref.layer].shapes[ref.index]))
			gr->layers[ref.layer].shapes.erase(gr->layers[ref.layer].shapes.begin()
				+ long(ref.index));
	fShapes.clear();
	Invalidate();
	if (Window() != NULL)
		Window()->PostMessage(kMsgDocumentChanged);
	return true;
}


// Align Objects' last try taken back (SendKillLastMessage), the selection
// kept
void
TPageView::UndoAlignPreview()
{
	std::vector<ShapeRef> shapes = fShapes;
	UndoStack::ViewState v = CurrentViewState();
	if (fUndo.Undo(fDocument, v) == UndoStack::kDocumentChanged) {
		GeometryChanged();
		RestoreViewState(v);
	}
	fShapes = shapes;
	fAlignPreview = false;
}


bool
TPageView::ShapeCommand(BMessage* message)
{
	uint32 what = message->what;
	// Arrange > Resize, Rotate, Reshape: what a handle does
	if (what == 'sM4A' || what == 'sM4B' || what == 'sM4C') {
		fShapeMode = what == 'sM4A' ? 0 : what == 'sM4B' ? 1 : 2;
		Invalidate();
		return true;
	}
	// Draw Grid ('sM4D': TGrid +0x5), AutoGrid ('sM4E': +0x4): the drawing's
	// own, saved with it
	size_t drawing;
	if ((what == 'sM4D' || what == 'sM4E') && CurrentDrawing(fDocument, fShapes, drawing)) {
		if (GRPart* gr = fDocument.EditableGraphicsPart(drawing)) {
			bool& flag = gr->gridFlags[what == 'sM4D' ? 1 : 0];
			flag = !flag;
		}
		Invalidate();
		if (Window() != NULL)
			Window()->PostMessage(kMsgDocumentChanged);
		return true;
	}
	// Align Objects… ('sM44', TDialogAlignObjects): the dialog, its tries
	if (what == 'sM44') {
		if (message->GetBool("done", false) || message->GetBool("cancel", false)) {
			if (message->GetBool("cancel", false) && fAlignPreview)
				UndoAlignPreview();
			fAlignPreview = false;
			return true;
		}
		if (!message->HasInt32("AlignX")) {
			if (fShapes.size() >= 2 && Window() != NULL) {
				fAlignPreview = false;
				(new TDialogAlignObjects(Window()))->RunDialog();
			}
			return true;
		}
		if (fShapes.size() < 2)
			return true;
		if (fAlignPreview)
			UndoAlignPreview();
		// STR# 1101, 20: Align Shapes
		fUndo.BeginDocument(fDocument, GobeResources::IndexString(1101, 20).String(),
			CurrentViewState());
		GRPart* gr = fDocument.EditableGraphicsPart(fShapes[0].part);
		std::vector<Shape*> shapes;
		for (const ShapeRef& ref : fShapes)
			if (gr != nullptr && ref.layer < gr->layers.size()
					&& ref.index < gr->layers[ref.layer].shapes.size()
					&& !ShapeLocked(gr->layers[ref.layer].shapes[ref.index]))
				shapes.push_back(&gr->layers[ref.layer].shapes[ref.index]);
		AlignShapes(shapes, message->GetInt32("AlignX", 0), message->GetInt32("AlignY", 0));
		fAlignPreview = true;
		Invalidate();
		if (Window() != NULL)
			Window()->PostMessage(kMsgDocumentChanged);
		return true;
	}
	// Align to Grid (1101, 21), Lock and Unlock (4, 5), Create Outlines (37)
	if ((what == 'sM45' || what == 'sM48' || what == 'sM49' || what == 'sM4T')
			&& !fShapes.empty()) {
		int label = what == 'sM45' ? 21 : what == 'sM48' ? 4 : what == 'sM49' ? 5 : 37;
		fUndo.BeginDocument(fDocument, GobeResources::IndexString(1101, label).String(),
			CurrentViewState());
		GRPart* gr = fDocument.EditableGraphicsPart(fShapes[0].part);
		for (const ShapeRef& ref : fShapes) {
			if (gr == nullptr || ref.layer >= gr->layers.size()
					|| ref.index >= gr->layers[ref.layer].shapes.size())
				continue;
			Shape& sh = gr->layers[ref.layer].shapes[ref.index];
			if (what == 'sM48')
				sh.values[0] |= kShapeLocked;
			else if (what == 'sM49')
				sh.values[0] &= ~kShapeLocked;
			else if (ShapeLocked(sh))
				continue;
			else if (what == 'sM45')
				AlignToGrid(sh, gr->gridSpacing);
			else
				OutlineShape(sh);
		}
		Invalidate();
		if (Window() != NULL)
			Window()->PostMessage(kMsgDocumentChanged);
		return true;
	}
	bool arrange = what == 'sM40' || what == 'sM41' || what == 'sM42' || what == 'sM43';
	bool format = what == 'sf03' || what == 'sf04' || what == 'sf05' || what == 'sf06'
		|| what == 'sf07' || what == 'sf08' || what == 'sf0D' || what == 'sf0E' || what == 'sf0S';
	if ((!arrange && !format) || fShapes.empty())
		return false;
	size_t part = fShapes[0].part;
	if (arrange) {
		// TGRArrangeCommand (STR# 1101: 10 Bring Shapes to Front, 11 Send
		// Shapes to Back, 12 Move Shapes Forward, 13 Move Shapes Backward),
		// each layer's shapes among themselves
		int order = what == 'sM40' ? 0 : what == 'sM41' ? 1 : what == 'sM42' ? 2 : 3;
		fUndo.BeginDocument(fDocument, GobeResources::IndexString(1101, 10 + order).String(),
			CurrentViewState());
		GRPart* gr = fDocument.EditableGraphicsPart(part);
		std::vector<ShapeRef> now;
		for (size_t l = 0; gr != nullptr && l < gr->layers.size(); l++) {
			std::vector<size_t> indices;
			for (const ShapeRef& s : fShapes)
				if (s.layer == l)
					indices.push_back(s.index);
			if (indices.empty())
				continue;
			for (size_t i : ArrangeShapes(gr->layers[l], indices, order))
				now.push_back(Ref(part, l, i));
		}
		fShapes = now;
		Invalidate();
		if (Window() != NULL)
			Window()->PostMessage(kMsgDocumentChanged);
		return true;
	}
	// the FormatBar: the fill ('sf03' color, 'sf04' pattern, 'sf05'
	// gradient: FCol; 'sf0D' FTra), the pen ('sf06', 'sf07', 'sf08': PCol;
	// 'sf0E' PTra), the shapes' transparency ('sf0S': fill and pen)
	std::vector<Property> properties;
	if (what == 'sf0D' || what == 'sf0E' || what == 'sf0S') {
		int32 value = message->GetInt32(kValueString, 100);
		if (value == int32('sM0U')) {
			// Other… (TPart::HandleMessage): the dialog, with the first
			// shape's opacity
			ShapeStyle style;
			float current = 1;
			if (SelectedShapeStyle(style))
				current = what == 'sf0D' ? style.fillOpacity : what == 'sf0E' ? style.penOpacity
					: std::min(style.fillOpacity, style.penOpacity);
			BString label = GobeResources::IndexString(2, 8);
			BString title = label;
			title.Truncate(title.CountChars() > 0 ? title.CountChars() - 1 : 0);
			(new TDialogGetTransparent(Window(), *message, int32(std::lround(current * 100)),
				title, label))->RunDialog();
			return true;
		}
		if (value < 0 || value > 100)
			return true;
		float opacity = value / 100.0f;
		if (what != 'sf0E')
			properties.push_back(OpacityProperty(Sig("FTra"), opacity));
		if (what != 'sf0D')
			properties.push_back(OpacityProperty(Sig("PTra"), opacity));
	} else {
		Property ink;
		if (!InkFromMessage(message, what == 'sf03' || what == 'sf04' || what == 'sf05'
				? Sig("FCol") : Sig("PCol"), ink))
			return true;
		properties.push_back(ink);
	}
	// TGRFormatCommand ("Format - " and the property's name, as text
	// formatting's label)
	fUndo.BeginDocument(fDocument, std::string(GobeResources::IndexString(5, 5).String())
		+ GobeResources::IndexString(11, 0).String(), CurrentViewState());
	for (const ShapeRef& ref : fShapes)
		for (const Property& p : properties)
			SetShapeProperty(fDocument, part, ref.layer, ref.index, p);
	Invalidate();
	if (Window() != NULL)
		Window()->PostMessage(kMsgDocumentChanged);
	return true;
}


// the shape being dragged out (its outline), the ones moved, resized or
// rotated, the rubber band, and the selected shapes' handles (TShape::
// DrawHandle: squares at the corners; reshaping: at the points)
void
TPageView::DrawShapeSelection(float dx, float dy)
{
	(void)dx;
	(void)dy;
	PushState();
	SetPenSize(1);
	SetDrawingMode(B_OP_COPY);
	SetHighColor(0, 0, 0);
	if (fShapeDrag.mode == 1 && fShapeDrag.points.size() >= 2) {
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
	if (fTool == kToolBezier && !fBezier.empty()
			&& (fShapeDrag.mode == 1 || fShapeDrag.mode == 7)) {
		// the Bezier path so far, to the pointer, and the last point's
		// control points
		float ox = fShapeDrag.x, oy = fShapeDrag.y;
		BShape path;
		path.MoveTo(BPoint(fBezier[0].point[0] + ox, fBezier[0].point[1] + oy));
		for (size_t i = 1; i < fBezier.size(); i++) {
			const Vertex& a = fBezier[i - 1];
			const Vertex& b = fBezier[i];
			BPoint c[3] = {BPoint(a.point[0] + a.control2[0] + ox, a.point[1] + a.control2[1] + oy),
				BPoint(b.point[0] + b.control1[0] + ox, b.point[1] + b.control1[1] + oy),
				BPoint(b.point[0] + ox, b.point[1] + oy)};
			path.BezierTo(c);
		}
		MovePenTo(B_ORIGIN);
		StrokeShape(&path, B_MIXED_COLORS);
		const Vertex& last = fBezier.back();
		BPoint at(last.point[0] + ox, last.point[1] + oy);
		if (fShapeDrag.mode == 1)
			StrokeLine(at, BPoint(fShapeDrag.now[0] + ox, fShapeDrag.now[1] + oy), B_MIXED_COLORS);
		else if (last.control2[0] != 0 || last.control2[1] != 0) {
			BPoint c1 = at + BPoint(last.control1[0], last.control1[1]);
			BPoint c2 = at + BPoint(last.control2[0], last.control2[1]);
			StrokeLine(c1, c2, B_MIXED_COLORS);
			for (BPoint c : {c1, c2})
				FillEllipse(BRect(c.x - kHandle, c.y - kHandle, c.x + kHandle, c.y + kHandle));
		}
	}
	if (fShapeDrag.mode == 6) {
		BRect band(std::min(fShapeDrag.start[0], fShapeDrag.now[0]) + fShapeDrag.x,
			std::min(fShapeDrag.start[1], fShapeDrag.now[1]) + fShapeDrag.y,
			std::max(fShapeDrag.start[0], fShapeDrag.now[0]) + fShapeDrag.x,
			std::max(fShapeDrag.start[1], fShapeDrag.now[1]) + fShapeDrag.y);
		StrokeRect(band, B_MIXED_COLORS);
	}
	float ddx = fMargin - fBounds.left, ddy = fMargin - fBounds.top;
	for (const ShapeRef& ref : fShapes) {
		const Shape* s = ShapeAt(fDocument, ref.part, ref.layer, ref.index);
		if (s == nullptr)
			continue;
		float ox = 0, oy = 0;
		for (const Document::PlacedFrame& f : fFrames)
			if (f.part == ref.part) {
				ox = f.x + ddx;
				oy = f.y + ddy;
			}
		float b[4];
		ShapeBounds(*s, b);
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
		if (fShapeDrag.mode == 2 || fShapeDrag.mode == 3)
			StrokeRect(box, B_MIXED_COLORS);
		if (fShapeDrag.mode == 4) {
			// the line from the center to the pointer
			StrokeLine(BPoint((box.left + box.right) / 2, (box.top + box.bottom) / 2),
				BPoint(fShapeDrag.now[0] + ox, fShapeDrag.now[1] + oy), B_MIXED_COLORS);
		}
		if (fShapeMode == 2 && fShapes.size() == 1 && !ShapeLocked(*s)) {
			// Reshape: the points (open squares), a curve's control points
			// (round, joined to their point)
			for (const DrawPoint& p : ShapeVertices(*s))
				StrokeRect(BRect(p[0] + ox - kHandle, p[1] + oy - kHandle, p[0] + ox + kHandle,
					p[1] + oy + kHandle));
			const std::vector<Vertex>& vertices = s->paths.empty()
				? std::vector<Vertex>() : s->paths[0].vertices;
			for (const auto& [handle, p] : ControlPoints(*s)) {
				const Vertex& v = vertices[size_t(handle / 2)];
				BPoint from(s->origin[0] + v.point[0] + ox, s->origin[1] + v.point[1] + oy);
				BPoint c(p[0] + ox, p[1] + oy);
				StrokeLine(from, c, B_MIXED_COLORS);
				FillEllipse(BRect(c.x - kHandle, c.y - kHandle, c.x + kHandle, c.y + kHandle));
			}
			continue;
		}
		// Rotate: round handles; Resize: squares; a locked shape's hollow
		// (TShape::DrawHandle)
		for (BPoint corner : {box.LeftTop(), box.RightTop(), box.RightBottom(), box.LeftBottom()}) {
			BRect h(corner.x - kHandle, corner.y - kHandle, corner.x + kHandle, corner.y + kHandle);
			if (ShapeLocked(*s))
				StrokeRect(h);
			else if (fShapeMode == 1 && fShapes.size() == 1)
				FillEllipse(h);
			else
				FillRect(h);
		}
	}
	PopState();
}

} // namespace gobe
