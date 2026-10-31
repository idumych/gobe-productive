// The drawing tools: TPartGR::DragOut's shapes, TShape's Contains,
// SetPosition, SetSize and InHandles.
#include "Drawing.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>

#include "Document.h"
#include "Editing.h"
#include "InlineParts.h"

namespace gobe {

bool
ToolMakesShapes(uint32_t tool)
{
	return tool == kToolRect || tool == kToolEllipse || tool == kToolRoundRect
		|| tool == kToolArc || tool == kToolLine || tool == kToolFree || tool == kToolPolygon;
}


bool
ShapeHasSize(uint32_t tool, const std::vector<DrawPoint>& points)
{
	if (points.size() < 2)
		return false;
	if (tool == kToolLine)
		return points.front() != points.back();
	float left = points[0][0], right = left, top = points[0][1], bottom = top;
	size_t used = tool == kToolFree || tool == kToolPolygon ? points.size() : 0;
	for (size_t i = 0; i < points.size(); i++) {
		if (used == 0 && i != 0 && i + 1 != points.size())
			continue;
		left = std::min(left, points[i][0]);
		right = std::max(right, points[i][0]);
		top = std::min(top, points[i][1]);
		bottom = std::max(bottom, points[i][1]);
	}
	return right > left && bottom > top;
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
	if (!ToolMakesShapes(tool) || !ShapeHasSize(tool, points))
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
			// a path of straight segments through the points: corner
			// vertices (flag 4) whose control points (offsets from the
			// point) are 0, as BusinessCards' are
			s.kind = Sig("BEZI");
			BezierPath path;
			path.closed = tool == kToolPolygon;
			for (const DrawPoint& p : points) {
				Vertex v;
				v.point[0] = p[0] - left;
				v.point[1] = p[1] - top;
				v.control1[0] = v.control1[1] = v.control2[0] = v.control2[1] = 0;
				v.flags = 4;
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
	if (s.angle != 0) {
		// a turned shape: the point turned back about the center (the
		// shape is drawn turned by -angle in screen coordinates)
		float cx = (b[0] + b[2]) / 2, cy = (b[1] + b[3]) / 2;
		float a = float(s.angle * M_PI / 180), dx = x - cx, dy = y - cy;
		x = cx + dx * std::cos(a) - dy * std::sin(a);
		y = cy + dx * std::sin(a) + dy * std::cos(a);
	}
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



std::vector<size_t>
ArrangeShapes(GRLayer& layer, const std::vector<size_t>& indices, int order)
{
	std::vector<Shape>& shapes = layer.shapes;
	std::vector<bool> chosen(shapes.size(), false);
	for (size_t i : indices)
		if (i < shapes.size())
			chosen[i] = true;
	// the shapes in their places, each with whether it's chosen and where
	// it was
	std::vector<std::pair<size_t, bool>> order2;
	for (size_t i = 0; i < shapes.size(); i++)
		order2.push_back({i, chosen[i]});
	if (order == 0 || order == 1) {
		std::stable_partition(order2.begin(), order2.end(),
			[order](const std::pair<size_t, bool>& e) { return order == 0 ? !e.second : e.second; });
	} else if (order == 2) {
		// forward: from the top down, each chosen one past the next one up
		for (size_t i = order2.size(); i-- > 1;)
			if (order2[i - 1].second && !order2[i].second)
				std::swap(order2[i - 1], order2[i]);
	} else {
		for (size_t i = 1; i < order2.size(); i++)
			if (order2[i].second && !order2[i - 1].second)
				std::swap(order2[i - 1], order2[i]);
	}
	std::vector<Shape> arranged;
	std::vector<size_t> where(shapes.size());
	for (size_t k = 0; k < order2.size(); k++) {
		arranged.push_back(shapes[order2[k].first]);
		where[order2[k].first] = k;
	}
	shapes = std::move(arranged);
	std::vector<size_t> now;
	for (size_t i : indices)
		now.push_back(i < where.size() ? where[i] : i);
	return now;
}


bool
SetShapeProperty(Document& doc, size_t part, size_t layer, size_t index, const Property& property)
{
	GRPart* gr = doc.EditableGraphicsPart(part);
	if (gr == nullptr || layer >= gr->layers.size() || index >= gr->layers[layer].shapes.size())
		return false;
	Shape& shape = gr->layers[layer].shapes[index];
	uint32_t to = StyleWithProperty(doc.styles, shape.style, property);
	SetCellStyle(doc.styles, shape.style, to);
	doc.stylesChanged = true;
	return true;
}


float
AngleAround(const Shape& shape, float x, float y)
{
	float b[4];
	ShapeBounds(shape, b);
	float cx = (b[0] + b[2]) / 2, cy = (b[1] + b[3]) / 2;
	// (y down: counterclockwise on screen is a falling atan2)
	return float(-std::atan2(y - cy, x - cx) * 180 / M_PI);
}


std::vector<DrawPoint>
ShapeVertices(const Shape& shape)
{
	std::vector<DrawPoint> points;
	if (shape.kind == Sig("LINE")) {
		points.push_back({shape.origin[0], shape.origin[1]});
		points.push_back({shape.origin[0] + shape.size[0], shape.origin[1] + shape.size[1]});
	} else if (shape.kind == Sig("BEZI") && !shape.paths.empty()) {
		for (const Vertex& v : shape.paths[0].vertices)
			points.push_back({shape.origin[0] + v.point[0], shape.origin[1] + v.point[1]});
	}
	return points;
}


int
HitVertex(const Shape& shape, float x, float y, float tolerance)
{
	std::vector<DrawPoint> points = ShapeVertices(shape);
	for (size_t i = 0; i < points.size(); i++)
		if (std::fabs(points[i][0] - x) <= tolerance && std::fabs(points[i][1] - y) <= tolerance)
			return int(i);
	return -1;
}


void
MoveVertex(Shape& shape, int vertex, float x, float y)
{
	if (shape.kind == Sig("LINE")) {
		if (vertex == 0) {
			float endX = shape.origin[0] + shape.size[0], endY = shape.origin[1] + shape.size[1];
			shape.origin[0] = x;
			shape.origin[1] = y;
			shape.size[0] = endX - x;
			shape.size[1] = endY - y;
		} else if (vertex == 1) {
			shape.size[0] = x - shape.origin[0];
			shape.size[1] = y - shape.origin[1];
		}
		return;
	}
	if (shape.kind != Sig("BEZI") || shape.paths.empty() || vertex < 0
			|| size_t(vertex) >= shape.paths[0].vertices.size())
		return;
	Vertex& v = shape.paths[0].vertices[size_t(vertex)];
	v.point[0] = x - shape.origin[0];
	v.point[1] = y - shape.origin[1];
	// TShape::Recalc: the box around the points again (the control
	// points are offsets: they move with theirs)
	float left = 0, top = 0, right = 0, bottom = 0;
	bool first = true;
	for (const BezierPath& p : shape.paths)
		for (const Vertex& w : p.vertices) {
			if (first) {
				left = right = w.point[0];
				top = bottom = w.point[1];
				first = false;
			}
			left = std::min(left, w.point[0]);
			right = std::max(right, w.point[0]);
			top = std::min(top, w.point[1]);
			bottom = std::max(bottom, w.point[1]);
		}
	for (BezierPath& p : shape.paths)
		for (Vertex& w : p.vertices) {
			w.point[0] -= left;
			w.point[1] -= top;
		}
	shape.origin[0] += left;
	shape.origin[1] += top;
	shape.size[0] = right - left;
	shape.size[1] = bottom - top;
}



std::vector<std::pair<int, DrawPoint>>
ControlPoints(const Shape& shape)
{
	std::vector<std::pair<int, DrawPoint>> points;
	if (shape.kind != Sig("BEZI") || shape.paths.empty())
		return points;
	const std::vector<Vertex>& v = shape.paths[0].vertices;
	for (size_t i = 0; i < v.size(); i++) {
		float px = shape.origin[0] + v[i].point[0], py = shape.origin[1] + v[i].point[1];
		if (v[i].control1[0] != 0 || v[i].control1[1] != 0)
			points.push_back({int(i * 2), {px + v[i].control1[0], py + v[i].control1[1]}});
		if (v[i].control2[0] != 0 || v[i].control2[1] != 0)
			points.push_back({int(i * 2 + 1), {px + v[i].control2[0], py + v[i].control2[1]}});
	}
	return points;
}


int
HitControlPoint(const Shape& shape, float x, float y, float tolerance)
{
	for (const auto& [handle, p] : ControlPoints(shape))
		if (std::fabs(p[0] - x) <= tolerance && std::fabs(p[1] - y) <= tolerance)
			return handle;
	return -1;
}


void
MoveControlPoint(Shape& shape, int handle, float x, float y)
{
	if (shape.kind != Sig("BEZI") || shape.paths.empty() || handle < 0)
		return;
	std::vector<Vertex>& v = shape.paths[0].vertices;
	size_t i = size_t(handle / 2);
	if (i >= v.size())
		return;
	float dx = x - shape.origin[0] - v[i].point[0], dy = y - shape.origin[1] - v[i].point[1];
	float* moved = handle % 2 == 0 ? v[i].control1 : v[i].control2;
	float* other = handle % 2 == 0 ? v[i].control2 : v[i].control1;
	moved[0] = dx;
	moved[1] = dy;
	// a smooth vertex keeps its two control points in a line
	if ((v[i].flags & 2) != 0) {
		float length = std::sqrt(other[0] * other[0] + other[1] * other[1]);
		float now = std::sqrt(dx * dx + dy * dy);
		if (now > 0) {
			other[0] = -dx / now * length;
			other[1] = -dy / now * length;
		}
	}
}


void
AlignShapes(std::vector<Shape*>& shapes, int alignX, int alignY)
{
	if (shapes.size() < 2)
		return;
	float all[4];
	ShapeBounds(*shapes[0], all);
	for (Shape* s : shapes) {
		float b[4];
		ShapeBounds(*s, b);
		all[0] = std::min(all[0], b[0]);
		all[1] = std::min(all[1], b[1]);
		all[2] = std::max(all[2], b[2]);
		all[3] = std::max(all[3], b[3]);
	}
	for (int axis = 0; axis < 2; axis++) {
		int align = axis == 0 ? alignX : alignY;
		float low = all[axis], high = all[axis + 2];
		if (align >= 1 && align <= 3) {
			for (Shape* s : shapes) {
				float b[4];
				ShapeBounds(*s, b);
				float to = align == 1 ? low : align == 3 ? high - (b[axis + 2] - b[axis])
					: (low + high) / 2 - (b[axis + 2] - b[axis]) / 2;
				s->origin[axis] += to - b[axis];
			}
		} else if (align == 4 && shapes.size() > 2) {
			// the space between them equal: in their order along the axis
			std::vector<Shape*> order = shapes;
			std::stable_sort(order.begin(), order.end(), [axis](Shape* a, Shape* b) {
				float ba[4], bb[4];
				ShapeBounds(*a, ba);
				ShapeBounds(*b, bb);
				return ba[axis] < bb[axis];
			});
			float sizes = 0;
			for (Shape* s : order) {
				float b[4];
				ShapeBounds(*s, b);
				sizes += b[axis + 2] - b[axis];
			}
			float first[4], last[4];
			ShapeBounds(*order.front(), first);
			ShapeBounds(*order.back(), last);
			float gap = (last[axis + 2] - first[axis] - sizes) / float(order.size() - 1);
			float at = first[axis];
			for (Shape* s : order) {
				float b[4];
				ShapeBounds(*s, b);
				s->origin[axis] += at - b[axis];
				at += b[axis + 2] - b[axis] + gap;
			}
		}
	}
}


float
SnapToGrid(float value, float spacing)
{
	if (spacing <= 0)
		return value;
	return std::round(value / spacing) * spacing;
}


void
AlignToGrid(Shape& shape, float spacing)
{
	float b[4];
	ShapeBounds(shape, b);
	MoveShape(shape, SnapToGrid(b[0], spacing) - b[0], SnapToGrid(b[1], spacing) - b[1]);
}


bool
OutlineShape(Shape& s)
{
	if (s.kind == Sig("BEZI"))
		return true;
	float b[4];
	ShapeBounds(s, b);
	float w = b[2] - b[0], h = b[3] - b[1];
	auto vertex = [](float x, float y, float c1x, float c1y, float c2x, float c2y, uint8_t flags) {
		Vertex v;
		v.point[0] = x;
		v.point[1] = y;
		v.control1[0] = c1x;
		v.control1[1] = c1y;
		v.control2[0] = c2x;
		v.control2[1] = c2y;
		v.flags = flags;
		return v;
	};
	BezierPath path;
	// a quarter ellipse's control points: kappa of its radii
	const float k = 0.5522847f;
	if (s.kind == Sig("LINE")) {
		path.closed = false;
		float x0 = s.size[0] < 0 ? -s.size[0] : 0, y0 = s.size[1] < 0 ? -s.size[1] : 0;
		path.vertices.push_back(vertex(x0, y0, 0, 0, 0, 0, 4));
		path.vertices.push_back(vertex(x0 + s.size[0], y0 + s.size[1], 0, 0, 0, 0, 4));
	} else if (s.kind == Sig("RECT") || (s.kind == Sig("RRCT") && s.radius[0] <= 0
			&& s.radius[1] <= 0)) {
		path.closed = true;
		path.vertices = {vertex(0, 0, 0, 0, 0, 0, 4), vertex(w, 0, 0, 0, 0, 0, 4),
			vertex(w, h, 0, 0, 0, 0, 4), vertex(0, h, 0, 0, 0, 0, 4)};
	} else if (s.kind == Sig("RRCT")) {
		path.closed = true;
		float rx = std::min(s.radius[0], w / 2), ry = std::min(s.radius[1], h / 2);
		float cx = rx * k, cy = ry * k;
		path.vertices = {
			vertex(rx, 0, -cx, 0, 0, 0, 4), vertex(w - rx, 0, 0, 0, cx, 0, 4),
			vertex(w, ry, 0, -cy, 0, 0, 4), vertex(w, h - ry, 0, 0, 0, cy, 4),
			vertex(w - rx, h, cx, 0, 0, 0, 4), vertex(rx, h, 0, 0, -cx, 0, 4),
			vertex(0, h - ry, 0, cy, 0, 0, 4), vertex(0, ry, 0, 0, 0, -cy, 4)};
	} else if (s.kind == Sig("ELPS")) {
		path.closed = true;
		float rx = w / 2, ry = h / 2;
		path.vertices = {
			vertex(rx, 0, -rx * k, 0, rx * k, 0, 2), vertex(w, ry, 0, -ry * k, 0, ry * k, 2),
			vertex(rx, h, rx * k, 0, -rx * k, 0, 2), vertex(0, ry, 0, ry * k, 0, -ry * k, 2)};
	} else if (s.kind == Sig("ARC ")) {
		// the arc's sweep from its start (degrees counterclockwise, 0 at
		// the right), in quarters or less, closed through the center as
		// the arc is filled
		float rx = w / 2, ry = h / 2;
		float start = s.arcAngles[0], sweep = s.arcAngles[1];
		int pieces = std::max(1, int(std::ceil(std::fabs(sweep) / 90)));
		float step = sweep / float(pieces);
		auto at = [&](float degrees, float& x, float& y, float& tx, float& ty) {
			float a = float(degrees * M_PI / 180);
			x = rx + rx * std::cos(a);
			y = ry - ry * std::sin(a);
			tx = -rx * std::sin(a);
			ty = -ry * std::cos(a);
		};
		float t = float(4.0 / 3.0 * std::tan(step * M_PI / 180 / 4));
		path.closed = true;
		path.vertices.push_back(vertex(rx, ry, 0, 0, 0, 0, 4));
		for (int i = 0; i <= pieces; i++) {
			float x, y, tx, ty;
			at(start + step * float(i), x, y, tx, ty);
			path.vertices.push_back(vertex(x, y, i > 0 ? -tx * t : 0, i > 0 ? -ty * t : 0,
				i < pieces ? tx * t : 0, i < pieces ? ty * t : 0, 4));
		}
	} else
		return false;
	s.kind = Sig("BEZI");
	s.origin[0] = b[0];
	s.origin[1] = b[1];
	s.size[0] = w;
	s.size[1] = h;
	s.radius[0] = s.radius[1] = 0;
	s.arcAngles[0] = s.arcAngles[1] = 0;
	s.values[0] &= ~uint32_t(0x100);
	s.paths = {path};
	return true;
}


bool
ParseToolbarShape(const std::vector<uint8_t>& data, BezierPath& path)
{
	auto u32 = [&](size_t at) {
		return uint32_t(data[at]) | uint32_t(data[at + 1]) << 8 | uint32_t(data[at + 2]) << 16
			| uint32_t(data[at + 3]) << 24;
	};
	if (data.size() < 8)
		return false;
	uint32_t count = u32(0);
	path = BezierPath();
	path.closed = u32(4) != 0;
	size_t at = 8;
	for (uint32_t i = 0; i < count; i++) {
		if (at + 4 > data.size())
			return false;
		Vertex v;
		v.flags = uint8_t(u32(at));
		at += 4;
		float values[6];
		for (float& value : values) {
			size_t end = at;
			while (end < data.size() && data[end] != 0)
				end++;
			if (end >= data.size())
				return false;
			value = std::strtof(std::string(data.begin() + long(at), data.begin() + long(end)).c_str(),
				nullptr);
			at = end + 1;
		}
		v.point[0] = values[0];
		v.point[1] = values[1];
		v.control1[0] = values[2];
		v.control1[1] = values[3];
		v.control2[0] = values[4];
		v.control2[1] = values[5];
		path.vertices.push_back(v);
	}
	return count > 1;
}


namespace {

// a path shape from vertices (drawing coordinates), its box the points'
int
AddPathShape(Document& doc, size_t part, size_t layer, BezierPath path)
{
	GRPart* gr = doc.EditableGraphicsPart(part);
	if (gr == nullptr || layer >= gr->layers.size() || path.vertices.size() < 2)
		return -1;
	float left = path.vertices[0].point[0], right = left;
	float top = path.vertices[0].point[1], bottom = top;
	for (const Vertex& v : path.vertices) {
		left = std::min(left, v.point[0]);
		right = std::max(right, v.point[0]);
		top = std::min(top, v.point[1]);
		bottom = std::max(bottom, v.point[1]);
	}
	if (right - left <= 0 && bottom - top <= 0)
		return -1;
	for (Vertex& v : path.vertices) {
		v.point[0] -= left;
		v.point[1] -= top;
	}
	Shape s;
	s.kind = Sig("BEZI");
	s.origin[0] = left;
	s.origin[1] = top;
	s.size[0] = right - left;
	s.size[1] = bottom - top;
	s.paths = {path};
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

} // namespace


int
AddToolbarShape(Document& doc, size_t part, size_t layer, const BezierPath& path,
	const DrawPoint& from, const DrawPoint& to)
{
	float box[4] = {std::min(from[0], to[0]), std::min(from[1], to[1]),
		std::max(from[0], to[0]), std::max(from[1], to[1])};
	if (box[2] - box[0] <= 0 || box[3] - box[1] <= 0)
		return -1;
	int index = AddPathShape(doc, part, layer, path);
	if (index < 0)
		return -1;
	Shape& s = doc.EditableGraphicsPart(part)->layers[layer].shapes[size_t(index)];
	ResizeShape(s, box);
	return index;
}


int
AddBezierShape(Document& doc, size_t part, size_t layer, const std::vector<Vertex>& vertices)
{
	BezierPath path;
	path.closed = false;
	path.vertices = vertices;
	return AddPathShape(doc, part, layer, path);
}



bool
IsGroupShape(const Document& doc, const Shape& shape, size_t& group)
{
	if (shape.kind != Sig("FRAM"))
		return false;
	const Frame* frame = FrameAt(doc, shape.frame);
	if (frame == nullptr)
		return false;
	const GRPart* gr = doc.GraphicsPart(frame->part);
	if (gr == nullptr || !gr->flag)
		return false;
	group = frame->part;
	return true;
}


bool
FrameShapeBounds(const Document& doc, const Shape& shape, float bounds[4])
{
	const Frame* frame = shape.kind == Sig("FRAM") ? FrameAt(doc, shape.frame) : nullptr;
	if (frame == nullptr)
		return false;
	bounds[0] = frame->origin[0];
	bounds[1] = frame->origin[1];
	bounds[2] = frame->origin[0] + frame->extent[0];
	bounds[3] = frame->origin[1] + frame->extent[1];
	return true;
}


void
MoveFrameShape(Document& doc, const Shape& shape, float dx, float dy)
{
	Frame* frame = const_cast<Frame*>(FrameAt(doc, shape.frame));
	if (frame == nullptr)
		return;
	frame->origin[0] += dx;
	frame->origin[1] += dy;
	doc.dirtyTails.insert(shape.frame[0]);
}


int
GroupShapes(Document& doc, size_t drawing, size_t layer, const std::vector<size_t>& indices,
	std::string& error)
{
	const GRPart* gr = doc.GraphicsPart(drawing);
	if (gr == nullptr || layer >= gr->layers.size()) {
		error = "not a drawing";
		return -1;
	}
	// the shapes, back to front, and their box
	std::vector<size_t> chosen;
	for (size_t i : indices)
		if (i < gr->layers[layer].shapes.size() && gr->layers[layer].shapes[i].kind != Sig("FRAM")
				&& std::find(chosen.begin(), chosen.end(), i) == chosen.end())
			chosen.push_back(i);
	std::sort(chosen.begin(), chosen.end());
	if (chosen.empty()) {
		error = "no shapes";
		return -1;
	}
	float box[4];
	ShapeBounds(gr->layers[layer].shapes[chosen[0]], box);
	for (size_t i : chosen) {
		float b[4];
		ShapeBounds(gr->layers[layer].shapes[i], b);
		box[0] = std::min(box[0], b[0]);
		box[1] = std::min(box[1], b[1]);
		box[2] = std::max(box[2], b[2]);
		box[3] = std::max(box[3], b[3]);
	}
	// (a frame needs some size: a line's box can be flat)
	box[2] = std::max(box[2], box[0] + 4);
	box[3] = std::max(box[3], box[1] + 4);
	uint32_t plainStyle = 0;
	if (drawing < doc.tails.size())
		for (const Portal& p : doc.tails[drawing].portals)
			if (!p.frames.empty()) {
				plainStyle = p.frames[0].style;
				break;
			}
	int group = AddDrawingFrame(doc, drawing, layer, Sig("GRFX"), box, error);
	if (group < 0)
		return -1;
	// the group's frame: transparent, as the drawing's own
	doc.tails[size_t(group)].portals[0].frames[0].style = plainStyle;
	doc.dirtyTails.insert(size_t(group));
	GRPart* parent = doc.EditableGraphicsPart(drawing);
	GRPart* inside = doc.EditableGraphicsPart(size_t(group));
	std::vector<Shape>& shapes = parent->layers[layer].shapes;
	// the FRAM entry AddDrawingFrame put last goes where the frontmost
	// grouped shape was
	Shape frameShape = shapes.back();
	shapes.pop_back();
	if (inside->layers.empty())
		inside->layers.push_back({"Layer 1", {}});
	for (size_t i : chosen) {
		Shape moved = shapes[i];
		MoveShape(moved, -box[0], -box[1]);
		inside->layers[0].shapes.push_back(moved);
	}
	size_t front = chosen.back() + 1 - chosen.size();
	for (size_t k = chosen.size(); k-- > 0;)
		shapes.erase(shapes.begin() + long(chosen[k]));
	shapes.insert(shapes.begin() + long(front), frameShape);
	inside->size[0] = box[2] - box[0];
	inside->size[1] = box[3] - box[1];
	inside->flag = true;
	return group;
}


int
UngroupShape(Document& doc, size_t drawing, size_t layer, size_t index, std::string& error)
{
	const GRPart* gr = doc.GraphicsPart(drawing);
	size_t group;
	if (gr == nullptr || layer >= gr->layers.size() || index >= gr->layers[layer].shapes.size()
			|| !IsGroupShape(doc, gr->layers[layer].shapes[index], group) || group == drawing) {
		error = "not a group";
		return -1;
	}
	Shape frameShape = gr->layers[layer].shapes[index];
	const Frame* frame = FrameAt(doc, frameShape.frame);
	float dx = frame->origin[0], dy = frame->origin[1];
	GRPart* inside = doc.EditableGraphicsPart(group);
	std::vector<Shape> back;
	for (GRLayer& l : inside->layers) {
		for (Shape& s : l.shapes)
			if (s.kind != Sig("FRAM")) {
				Shape moved = s;
				MoveShape(moved, dx, dy);
				back.push_back(moved);
			}
		l.shapes.clear();
	}
	// the group's frame gone: its part shown nowhere
	doc.tails[group].portals.clear();
	doc.dirtyTails.insert(group);
	GRPart* parent = doc.EditableGraphicsPart(drawing);
	std::vector<Shape>& shapes = parent->layers[layer].shapes;
	shapes.erase(shapes.begin() + long(index));
	// fresh ids among the drawing's shapes
	uint32_t id = 0;
	for (const GRLayer& l : parent->layers)
		for (const Shape& s : l.shapes)
			if (s.kind != Sig("FRAM"))
				id = std::max(id, s.values[2] + 1);
	for (Shape& s : back)
		s.values[2] = id++;
	shapes.insert(shapes.begin() + long(index), back.begin(), back.end());
	return int(back.size());
}

} // namespace gobe
