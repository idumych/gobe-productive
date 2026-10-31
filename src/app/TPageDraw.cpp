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
#include <MenuItem.h>
#include <Window.h>

#include <algorithm>
#include <cmath>

#include "DocArchive.h"
#include "Drawing.h"
#include "GobeResources.h"
#include "SSStyle.h"
#include "TRibbon.h"
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
	fShapeDrag = ShapeDrag();
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
	for (int32 i = 0; BMenuItem* item = menu->ItemAt(i); i++) {
		uint32 what = item->Message() != NULL ? item->Message()->what : 0;
		bool arrange = what == 'sM40' || what == 'sM41' || what == 'sM42' || what == 'sM43';
		bool mode = what == 'sM4A' || what == 'sM4B' || what == 'sM4C';
		item->SetEnabled((arrange && !fShapes.empty()) || mode);
		if (mode)
			item->SetMarked(kModes[fShapeMode] == what);
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
	if (ToolMakesShapes(fTool)) {
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
		int vertex = s != nullptr && fShapeMode == 2 ? HitVertex(*s, px, py, kHandle + 1) : -1;
		int handle = s != nullptr && fShapeMode != 2 ? HitHandle(*s, px, py, kHandle + 1) : -1;
		if (vertex >= 0 || handle >= 0) {
			startDrag(vertex >= 0 ? 5 : fShapeMode == 1 ? 4 : 3);
			fShapeDrag.handle = vertex >= 0 ? vertex : handle;
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
	if (fShapeDrag.mode == 1) {
		if (fTool != kToolPolygon)
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
		} else
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
	if (fShapeDrag.mode == 1 && fTool == kToolPolygon
			&& (bytes[0] == B_ENTER || bytes[0] == B_ESCAPE)) {
		FinishShape();
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
				&& ref.index < gr->layers[ref.layer].shapes.size())
			gr->layers[ref.layer].shapes.erase(gr->layers[ref.layer].shapes.begin()
				+ long(ref.index));
	fShapes.clear();
	Invalidate();
	if (Window() != NULL)
		Window()->PostMessage(kMsgDocumentChanged);
	return true;
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
		if (fShapeMode == 2 && fShapes.size() == 1) {
			// Reshape: the points (open squares)
			for (const DrawPoint& p : ShapeVertices(*s))
				StrokeRect(BRect(p[0] + ox - kHandle, p[1] + oy - kHandle, p[0] + ox + kHandle,
					p[1] + oy + kHandle));
			continue;
		}
		// Rotate: round handles; Resize: squares
		for (BPoint corner : {box.LeftTop(), box.RightTop(), box.RightBottom(), box.LeftBottom()}) {
			BRect h(corner.x - kHandle, corner.y - kHandle, corner.x + kHandle, corner.y + kHandle);
			if (fShapeMode == 1 && fShapes.size() == 1)
				FillEllipse(h);
			else
				FillRect(h);
		}
	}
	PopState();
}

} // namespace gobe
