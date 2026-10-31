// The drawing tools (TToolBar, TPartGR::DragOut): shapes made, moved,
// resized and deleted in a graphics part.
#ifndef GOBE_DRAWING_H
#define GOBE_DRAWING_H

#include <array>
#include <cstdint>
#include <vector>

#include "GRPart.h"
#include "Styles.h"

namespace gobe {

struct Document;

typedef std::array<float, 2> DrawPoint;

// TToolBar's tools (TheTools): the arrow (select), part frames, then the
// shapes; 'TOOS' (the toolbar shapes) and 'BEZI' aren't ported
enum : uint32_t {
	kToolArrow = 0x41525257,		// 'ARRW'
	kToolPart = 0x50415254,			// 'PART'
	kToolRect = 0x52454354,			// 'RECT'
	kToolEllipse = 0x454c5053,		// 'ELPS'
	kToolRoundRect = 0x52524354,	// 'RRCT'
	kToolArc = 0x41524320,			// 'ARC '
	kToolLine = 0x4c494e45,			// 'LINE'
	kToolFree = 0x46524545,			// 'FREE'
	kToolBezier = 0x42455a49,		// 'BEZI'
	kToolPolygon = 0x504f4c59,		// 'POLY'
	kToolShapes = 0x544f4f53		// 'TOOS'
};

// whether the port makes shapes with `tool`
bool ToolMakesShapes(uint32_t tool);
// whether `points` make a shape of `tool` (a line two different points,
// the others a box with a width and a height)
bool ShapeHasSize(uint32_t tool, const std::vector<DrawPoint>& points);

// The style a new shape gets: an unnamed one based on "GR Default" (the
// document's shape style, TStyleID copied in DragOut), as the original's
// shapes have them (Basi, flags 0x20).
uint32_t NewShapeStyle(StyleMgr& styles);

// TPartGR::DragOut: a new shape of `tool` in layer `layer` of graphics
// part `part`, from `points` (drawing coordinates): RECT a round rectangle
// with radius 0, RRCT radius 25, ELPS an ellipse, ARC from 0 sweeping 90
// degrees, LINE from the first point to the last; the box of the first
// and last points; FREE an open path through the points, POLY a closed
// one (BEZI with straight segments). A shape gets the next id among the
// part's (TShape +0x20), flags 0x100 for a line. Returns its index in the
// layer, or -1 (no such part or layer, or a box with no size).
int AddShape(Document& doc, size_t part, size_t layer, uint32_t tool,
	const std::vector<DrawPoint>& points);

// a shape's box (left, top, right, bottom), its size made positive
void ShapeBounds(const Shape& shape, float bounds[4]);

// TShape::Contains: the topmost shape at a point (not other parts'
// frames): within its box, a line or an open path within `tolerance` of
// its segments, an ellipse inside its curve; layers hidden by `hidden`
// (one per layer) skipped. False if there's none.
bool HitShape(const GRPart& part, float x, float y, float tolerance,
	const std::vector<bool>& hidden, size_t& layer, size_t& index);

// TShape::SetPosition: moved by dx, dy
void MoveShape(Shape& shape, float dx, float dy);
// TShape::SetSize: its box made `bounds` (left, top, right, bottom); a
// line keeps its direction, a path's points are scaled
void ResizeShape(Shape& shape, const float bounds[4]);

// which handle (TShape::InHandles: 0 top left, 1 top right, 2 bottom right,
// 3 bottom left) of the box is within `tolerance` of the point; -1 none
int HitHandle(const Shape& shape, float x, float y, float tolerance);
// the box with handle `handle` dragged to (x, y)
void DragHandle(const float bounds[4], int handle, float x, float y, float out[4]);

} // namespace gobe

#endif
