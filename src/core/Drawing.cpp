// The drawing tools: TPartGR::DragOut's shapes, TShape's Contains,
// SetPosition, SetSize and InHandles.
#include "Drawing.h"

#include <algorithm>
#include <cmath>

#include "Document.h"

namespace gobe {

bool
ToolMakesShapes(uint32_t tool)
{
	return tool == kToolRect || tool == kToolEllipse || tool == kToolRoundRect
		|| tool == kToolArc || tool == kToolLine || tool == kToolFree || tool == kToolPolygon;
}


uint32_t
NewShapeStyle(StyleMgr& styles)
{
	uint32_t base = 0xffffffff;
	for (size_t i = 0; i < styles.styles.size(); i++)
		if (styles.styles[i].name == "GR Default") {
			base = uint32_t(i);
			break;
		}
	Style style;
	style.id = styles.styles.empty() ? 0 : styles.styles[0].id;
	style.value4 = 1;			// its users
	style.value8 = 0x20;
	style.valueC = 0xffff;
	style.value10 = Sig("Basi");
	style.links[0] = base;
	style.links[1] = style.links[2] = 0xffffffff;
	styles.styles.push_back(style);
	return uint32_t(styles.styles.size() - 1);
}


int
AddShape(Document& doc, size_t part, size_t layer, uint32_t tool,
	const std::vector<DrawPoint>& points)
{
	if (!ToolMakesShapes(tool) || points.size() < 2)
		return -1;
	GRPart* gr = doc.EditableGraphicsPart(part);
	if (gr == nullptr || layer >= gr->layers.size())
		return -1;
	const DrawPoint& a = points.front();
	const DrawPoint& b = points.back();
	Shape s;
	float left = std::min(a[0], b[0]), top = std::min(a[1], b[1]);
	float right = std::max(a[0], b[0]), bottom = std::max(a[1], b[1]);
	if (tool == kToolFree || tool == kToolPolygon) {
		for (const DrawPoint& p : points) {
			left = std::min(left, p[0]);
			top = std::min(top, p[1]);
			right = std::max(right, p[0]);
			bottom = std::max(bottom, p[1]);
		}
	}
	if (tool == kToolLine) {
		if (a == b)
			return -1;
		s.kind = Sig("LINE");
		s.origin[0] = a[0];
		s.origin[1] = a[1];
		s.size[0] = b[0] - a[0];
		s.size[1] = b[1] - a[1];
		s.values[0] = 0x100;
	} else {
		if (right - left <= 0 || bottom - top <= 0)
			return -1;
		s.origin[0] = left;
		s.origin[1] = top;
		s.size[0] = right - left;
		s.size[1] = bottom - top;
		if (tool == kToolRect || tool == kToolRoundRect) {
			// DragOut: the rectangle tool's are round rectangles too
			s.kind = Sig("RRCT");
			s.radius[0] = s.radius[1] = tool == kToolRoundRect ? 25 : 0;
		} else if (tool == kToolEllipse)
			s.kind = Sig("ELPS");
		else if (tool == kToolArc) {
			s.kind = Sig("ARC ");
			s.arcAngles[0] = 0;
			s.arcAngles[1] = 90;
		} else {
			// a path of straight segments through the points (each
			// vertex's control points on it)
			s.kind = Sig("BEZI");
			BezierPath path;
			path.closed = tool == kToolPolygon;
			for (const DrawPoint& p : points) {
				Vertex v;
				v.point[0] = v.control1[0] = v.control2[0] = p[0] - left;
				v.point[1] = v.control1[1] = v.control2[1] = p[1] - top;
				v.flags = 0;
				if (!path.vertices.empty() && path.vertices.back().point[0] == v.point[0]
						&& path.vertices.back().point[1] == v.point[1])
					continue;
				path.vertices.push_back(v);
			}
			if (path.vertices.size() < 2)
				return -1;
			s.paths.push_back(path);
		}
	}
	// the next id among the part's shapes (TShape +0x20)
	uint32_t id = 0;
	for (const GRLayer& l : gr->layers)
		for (const Shape& other : l.shapes)
			if (other.kind != Sig("FRAM"))
				id = std::max(id, other.values[2] + 1);
	s.values[2] = id;
	s.style = NewShapeStyle(doc.styles);
	doc.stylesChanged = true;
	gr->layers[layer].shapes.push_back(s);
	return int(gr->layers[layer].shapes.size() - 1);
}


void
ShapeBounds(const Shape& shape, float bounds[4])
{
	bounds[0] = std::min(shape.origin[0], shape.origin[0] + shape.size[0]);
	bounds[1] = std::min(shape.origin[1], shape.origin[1] + shape.size[1]);
	bounds[2] = std::max(shape.origin[0], shape.origin[0] + shape.size[0]);
	bounds[3] = std::max(shape.origin[1], shape.origin[1] + shape.size[1]);
}


namespace {

float
SegmentDistance(float x, float y, float x1, float y1, float x2, float y2)
{
	float dx = x2 - x1, dy = y2 - y1;
	float length = dx * dx + dy * dy;
	float t = length > 0 ? ((x - x1) * dx + (y - y1) * dy) / length : 0;
	t = std::max(0.0f, std::min(1.0f, t));
	float px = x1 + t * dx - x, py = y1 + t * dy - y;
	return std::sqrt(px * px + py * py);
}


bool
Contains(const Shape& s, float x, float y, float tolerance)
{
	float b[4];
	ShapeBounds(s, b);
	if (x < b[0] - tolerance || x > b[2] + tolerance || y < b[1] - tolerance
			|| y > b[3] + tolerance)
		return false;
	if (s.kind == Sig("LINE"))
		return SegmentDistance(x, y, s.origin[0], s.origin[1], s.origin[0] + s.size[0],
			s.origin[1] + s.size[1]) <= tolerance;
	if (s.kind == Sig("ELPS")) {
		float rx = (b[2] - b[0]) / 2 + tolerance, ry = (b[3] - b[1]) / 2 + tolerance;
		float cx = (b[0] + b[2]) / 2, cy = (b[1] + b[3]) / 2;
		return rx > 0 && ry > 0
			&& (x - cx) * (x - cx) / (rx * rx) + (y - cy) * (y - cy) / (ry * ry) <= 1;
	}
	if (s.kind == Sig("BEZI")) {
		for (const BezierPath& p : s.paths) {
			if (p.closed)
				return true;	// (the box, for a closed path)
			for (size_t i = 1; i < p.vertices.size(); i++)
				if (SegmentDistance(x, y, s.origin[0] + p.vertices[i - 1].point[0],
						s.origin[1] + p.vertices[i - 1].point[1],
						s.origin[0] + p.vertices[i].point[0],
						s.origin[1] + p.vertices[i].point[1]) <= tolerance)
					return true;
		}
		return false;
	}
	return true;
}

} // namespace


bool
HitShape(const GRPart& part, float x, float y, float tolerance, const std::vector<bool>& hidden,
	size_t& layer, size_t& index)
{
	for (size_t l = part.layers.size(); l-- > 0;) {
		if (l < hidden.size() && hidden[l])
			continue;
		const std::vector<Shape>& shapes = part.layers[l].shapes;
		for (size_t i = shapes.size(); i-- > 0;) {
			if (shapes[i].kind == Sig("FRAM") || !Contains(shapes[i], x, y, tolerance))
				continue;
			layer = l;
			index = i;
			return true;
		}
	}
	return false;
}


void
MoveShape(Shape& shape, float dx, float dy)
{
	shape.origin[0] += dx;
	shape.origin[1] += dy;
}


void
ResizeShape(Shape& shape, const float bounds[4])
{
	float old[4];
	ShapeBounds(shape, old);
	float width = bounds[2] - bounds[0], height = bounds[3] - bounds[1];
	if (shape.kind == Sig("LINE")) {
		// the line from the corner it started at, its direction kept
		bool flipX = shape.size[0] < 0, flipY = shape.size[1] < 0;
		shape.origin[0] = flipX ? bounds[2] : bounds[0];
		shape.origin[1] = flipY ? bounds[3] : bounds[1];
		shape.size[0] = flipX ? -width : width;
		shape.size[1] = flipY ? -height : height;
		return;
	}
	float sx = old[2] > old[0] ? width / (old[2] - old[0]) : 1;
	float sy = old[3] > old[1] ? height / (old[3] - old[1]) : 1;
	for (BezierPath& p : shape.paths)
		for (Vertex& v : p.vertices)
			for (float* q : {v.point, v.control1, v.control2}) {
				q[0] *= sx;
				q[1] *= sy;
			}
	shape.origin[0] = bounds[0];
	shape.origin[1] = bounds[1];
	shape.size[0] = width;
	shape.size[1] = height;
}


int
HitHandle(const Shape& shape, float x, float y, float tolerance)
{
	float b[4];
	ShapeBounds(shape, b);
	const float corners[4][2] = {{b[0], b[1]}, {b[2], b[1]}, {b[2], b[3]}, {b[0], b[3]}};
	for (int i = 0; i < 4; i++)
		if (std::fabs(x - corners[i][0]) <= tolerance && std::fabs(y - corners[i][1]) <= tolerance)
			return i;
	return -1;
}


void
DragHandle(const float bounds[4], int handle, float x, float y, float out[4])
{
	std::copy(bounds, bounds + 4, out);
	if (handle == 0 || handle == 3)
		out[0] = x;
	else
		out[2] = x;
	if (handle == 0 || handle == 1)
		out[1] = y;
	else
		out[3] = y;
	if (out[0] > out[2])
		std::swap(out[0], out[2]);
	if (out[1] > out[3])
		std::swap(out[1], out[3]);
}

} // namespace gobe
