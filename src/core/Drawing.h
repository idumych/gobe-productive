// The drawing tools (TToolBar, TPartGR::DragOut): shapes made, moved,
// resized and deleted in a graphics part.
#ifndef GOBE_DRAWING_H
#define GOBE_DRAWING_H

#include <array>
#include <cstdint>
#include <string>
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

// Arrange (TGRArrangeCommand): the shapes at `indices` of `layer` brought
// to the front (`order` 0: last, drawn on top), sent to the back (1),
// moved forward (2) or backward (3) one place past the shape that isn't
// among them, keeping their own order. Returns where they are now (in the
// order given).
std::vector<size_t> ArrangeShapes(GRLayer& layer, const std::vector<size_t>& indices, int order);

// a shape's style with `property` set (TGRFormatCommand: the shape's own
// unnamed style changed, or one made from its named style), the styles'
// user counts following; false if there's no such shape
bool SetShapeProperty(Document& doc, size_t part, size_t layer, size_t index,
	const Property& property);

// the angle (degrees, counterclockwise as the shapes' are) of a point
// around the shape's center, for rotating it by dragging (Arrange >
// Rotate)
float AngleAround(const Shape& shape, float x, float y);

// Arrange > Reshape: a path's points (BEZI: the first path's vertices)
// or a line's ends; the one within `tolerance` of the point, -1 if none
int HitVertex(const Shape& shape, float x, float y, float tolerance);
// that point moved to (x, y) (drawing coordinates); a path's box follows
// its points (TShape::Recalc)
void MoveVertex(Shape& shape, int vertex, float x, float y);
// the points Reshape shows (drawing coordinates)
std::vector<DrawPoint> ShapeVertices(const Shape& shape);

// a curve's control points (Reshape): a path vertex's control points
// that aren't on it (TVertex: offsets from the point), as handles numbered
// vertex * 2 (the one before) and vertex * 2 + 1 (the one after); their
// positions (drawing coordinates), the one within `tolerance` (-1 none),
// and one moved (a smooth vertex, flag 2, its other one opposite)
std::vector<std::pair<int, DrawPoint>> ControlPoints(const Shape& shape);
int HitControlPoint(const Shape& shape, float x, float y, float tolerance);
void MoveControlPoint(Shape& shape, int handle, float x, float y);

// Arrange > Lock, Unlock (TLockShapesCommand): TShape +0x24's 0x20; a
// locked shape isn't moved, resized, reshaped or deleted
const uint32_t kShapeLocked = 0x20;
inline bool ShapeLocked(const Shape& shape) { return (shape.values[0] & kShapeLocked) != 0; }

// Align Objects (TGRAlignShapesCommand, DIAL 202: "AlignX", "AlignY"):
// 0 as they are, 1 the left (top) edges, 2 the centers, 3 the right
// (bottom) edges to those of all of them together, 4 the space between
// them made equal (in their order, the outer ones staying)
void AlignShapes(std::vector<Shape*>& shapes, int alignX, int alignY);

// the grid (TGrid: the spacing; AutoGrid snaps, Draw Grid shows it)
float SnapToGrid(float value, float spacing);
// Align to Grid (TGRAlignToGridCommand): a shape's corner moved to the
// nearest grid point
void AlignToGrid(Shape& shape, float spacing);

// Create Outlines (TGRCreateOutlineCommand): a shape as a path (BEZI) of
// the same look: rectangles and round rectangles, ellipses and arcs by
// their curves (quarter ellipses as Bezier curves), a line as an open
// path; a path as it is. False for other kinds.
bool OutlineShape(Shape& shape);

// a toolbar shape (TToolbarShapes::loadToolbarShapes: a 'POLY' resource:
// the count, closed (four bytes), then each vertex's flags and its point
// and control points as six numbers in text, little-endian); false if it
// isn't one
bool ParseToolbarShape(const std::vector<uint8_t>& data, BezierPath& path);
// a toolbar shape made a shape in the box from `from` to `to` (DragOut:
// the shape sized to the drag); -1 as AddShape
int AddToolbarShape(Document& doc, size_t part, size_t layer, const BezierPath& path,
	const DrawPoint& from, const DrawPoint& to);

// Arrange > Group (TGRGroupCommand): the shapes at `indices` of `layer`
// (not other parts' frames) moved into a new drawing part (TPartGR +0xa4
// set: a group; its size their box's), framed in their place: the frame
// where their box is, its FRAM entry where the frontmost of them was.
// Returns the group part, -1 with `error`.
int GroupShapes(Document& doc, size_t drawing, size_t layer, const std::vector<size_t>& indices,
	std::string& error);

// a group's frame shape (a FRAM entry showing a group part); the part
bool IsGroupShape(const Document& doc, const Shape& shape, size_t& group);

// a FRAM entry's box in the drawing (its frame's), and the frame moved
bool FrameShapeBounds(const Document& doc, const Shape& shape, float bounds[4]);
void MoveFrameShape(Document& doc, const Shape& shape, float dx, float dy);

// Arrange > Ungroup: the group at `index` of `layer` taken apart: its
// shapes back in the drawing where it was (in its place, in their order),
// the FRAM entry and the group's frame gone. The group part stays in the
// document, empty and not shown (the port doesn't remove parts). Returns
// how many shapes came back, -1 with `error`.
int UngroupShape(Document& doc, size_t drawing, size_t layer, size_t index, std::string& error);

// the Bezier tool: a path through `vertices` (drawing coordinates, their
// control points offsets from them), open; -1 as AddShape
int AddBezierShape(Document& doc, size_t part, size_t layer, const std::vector<Vertex>& vertices);

} // namespace gobe

#endif
