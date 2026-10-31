// draw_shapes: the drawing tools (TPartGR::DragOut and TShape's editing).
// A new drawing gets a shape of each tool; they are hit, moved, resized,
// saved and reloaded the same; undone and redone; a text document's sheet
// drawing and TheWidget (if given) get one too, their other shapes kept.
// Arguments: documents (TheWidget is used when given).
#include <cmath>
#include <cstdio>
#include <string>

#include "Document.h"
#include "ResourceFile.h"
#include "Drawing.h"
#include "Styles.h"
#include "Undo.h"

using namespace gobe;

static int sOk = 0, sFailed = 0;

static void
Check(bool ok, const std::string& what)
{
	if (ok)
		sOk++;
	else {
		sFailed++;
		printf("FAIL %s\n", what.c_str());
	}
}


static bool
SameShape(const Shape& a, const Shape& b)
{
	bool same = a.kind == b.kind && a.origin[0] == b.origin[0] && a.origin[1] == b.origin[1]
		&& a.size[0] == b.size[0] && a.size[1] == b.size[1] && a.values[0] == b.values[0]
		&& a.values[2] == b.values[2] && a.radius[0] == b.radius[0]
		&& a.arcAngles[1] == b.arcAngles[1] && a.paths.size() == b.paths.size();
	for (size_t i = 0; same && i < a.paths.size(); i++) {
		same = a.paths[i].closed == b.paths[i].closed
			&& a.paths[i].vertices.size() == b.paths[i].vertices.size();
		for (size_t k = 0; same && k < a.paths[i].vertices.size(); k++)
			same = a.paths[i].vertices[k].point[0] == b.paths[i].vertices[k].point[0]
				&& a.paths[i].vertices[k].point[1] == b.paths[i].vertices[k].point[1];
	}
	return same;
}


int
main(int argc, char** argv)
{
	Document doc;
	std::string error;
	Check(CreateBlankDrawing(doc, error), "a new drawing " + error);
	const uint32_t tools[7] = {kToolRect, kToolRoundRect, kToolEllipse, kToolArc, kToolLine,
		kToolFree, kToolPolygon};
	for (int i = 0; i < 7; i++) {
		float x = 10 + i * 60.0f;
		std::vector<DrawPoint> points = {{x, 10}, {x + 50, 40}};
		if (tools[i] == kToolFree || tools[i] == kToolPolygon)
			points = {{x, 10}, {x + 20, 40}, {x + 50, 15}};
		Check(AddShape(doc, 0, 0, tools[i], points) == i, "shape " + std::to_string(i));
	}
	Check(AddShape(doc, 0, 0, kToolRect, {{5, 5}, {5, 30}}) < 0, "no shape without a width");
	Check(AddShape(doc, 0, 0, kToolArrow, {{5, 5}, {50, 30}}) < 0, "no shape with the arrow");
	const GRPart* gr = doc.GraphicsPart(0);
	Check(gr != nullptr && gr->layers[0].shapes.size() == 7, "seven shapes");
	if (gr == nullptr || gr->layers[0].shapes.size() != 7)
		return 1;
	const std::vector<Shape>& s = gr->layers[0].shapes;
	Check(s[0].kind == Sig("RRCT") && s[0].radius[0] == 0 && s[0].size[0] == 50
		&& s[0].size[1] == 30, "the rectangle tool: a round rectangle, radius 0");
	Check(s[1].kind == Sig("RRCT") && s[1].radius[0] == 25 && s[1].radius[1] == 25,
		"the round rectangle tool: radius 25");
	Check(s[2].kind == Sig("ELPS"), "an ellipse");
	Check(s[3].kind == Sig("ARC ") && s[3].arcAngles[0] == 0 && s[3].arcAngles[1] == 90,
		"an arc from 0, 90 degrees");
	Check(s[4].kind == Sig("LINE") && s[4].values[0] == 0x100 && s[4].origin[0] == 250
		&& s[4].size[0] == 50 && s[4].size[1] == 30, "a line, flags 0x100");
	Check(s[5].kind == Sig("BEZI") && s[5].paths.size() == 1 && !s[5].paths[0].closed
		&& s[5].paths[0].vertices.size() == 3 && s[5].size[1] == 30, "freehand: an open path");
	Check(s[6].kind == Sig("BEZI") && s[6].paths[0].closed, "polygon: a closed path");
	bool ids = true;
	for (int i = 0; i < 7; i++)
		ids = ids && s[size_t(i)].values[2] == uint32_t(i);
	Check(ids, "ids 0 to 6");
	const Style& style = doc.styles.styles[s[0].style];
	Check(style.name.empty() && style.links[0] < doc.styles.styles.size()
		&& doc.styles.styles[style.links[0]].name == "GR Default" && style.value8 == 0x20,
		"its style: unnamed, based on GR Default");
	ShapeStyle look = ResolveShapeStyle(doc.styles, s[0].style);
	Check(look.pen.set && look.fill.set && look.penSize == 1, "drawn with GR Default's ink");

	// hitting
	size_t layer, index;
	std::vector<bool> hidden;
	Check(HitShape(*gr, 30, 20, 2, hidden, layer, index) && index == 0, "the rectangle hit");
	Check(HitShape(*gr, 275, 25, 2, hidden, layer, index) && index == 4, "the line hit");
	Check(!HitShape(*gr, 255, 38, 2, hidden, layer, index), "beside the line: none");
	Check(!HitShape(*gr, 132, 12, 1, hidden, layer, index), "the ellipse's corner: none");
	Check(!HitShape(*gr, 30, 200, 2, hidden, layer, index), "empty space: none");

	// moving and resizing
	{
		GRPart* edit = doc.EditableGraphicsPart(0);
		MoveShape(edit->layers[0].shapes[0], 5, 100);
		const float box[4] = {15, 110, 115, 150};
		ResizeShape(edit->layers[0].shapes[0], box);
		Check(edit->layers[0].shapes[0].origin[1] == 110 && edit->layers[0].shapes[0].size[0] == 100,
			"moved and resized");
		Check(HitHandle(edit->layers[0].shapes[0], 115, 150, 3) == 2, "the bottom right handle");
		float dragged[4];
		DragHandle(box, 2, 135, 170, dragged);
		Check(dragged[2] == 135 && dragged[3] == 170 && dragged[0] == 15, "a handle dragged");
		const float halve[4] = {5, 10, 105, 25};
		ResizeShape(edit->layers[0].shapes[5], halve);
		Check(edit->layers[0].shapes[5].paths[0].vertices[2].point[0] == 100
			&& edit->layers[0].shapes[5].paths[0].vertices[1].point[1] == 15, "a path scaled");
	}

	// saved and reloaded
	std::string path = "/tmp/draw_shapes_test";
	std::vector<Shape> before = doc.GraphicsPart(0)->layers[0].shapes;
	Check(doc.Save(path, error), "save " + error);
	Document back;
	Check(LoadDocument(path, back, error), "reload " + error);
	const GRPart* again = back.GraphicsPart(0);
	bool same = again != nullptr && again->layers[0].shapes.size() == before.size();
	for (size_t i = 0; same && i < before.size(); i++)
		same = SameShape(before[i], again->layers[0].shapes[i]);
	Check(same, "the shapes reloaded as they were");
	Check(back.warnings.empty(), "no warnings reloading");

	// undo and redo (a document step)
	UndoStack undo;
	UndoStack::ViewState view;
	undo.BeginDocument(back, "Create Shape", view);
	AddShape(back, 0, 0, kToolEllipse, {{0, 0}, {10, 10}});
	Check(back.GraphicsPart(0)->layers[0].shapes.size() == 8, "an eighth shape");
	Check(undo.Undo(back, view) == UndoStack::kDocumentChanged
		&& back.GraphicsPart(0)->layers[0].shapes.size() == 7, "undone");
	Check(undo.Redo(back, view) == UndoStack::kDocumentChanged
		&& back.GraphicsPart(0)->layers[0].shapes.size() == 8, "redone");

	// a text document's sheet drawing (part 1)
	Document text;
	Check(CreateBlankDocument(text, error), "a text document " + error);
	Check(AddShape(text, 1, 0, kToolEllipse, {{20, 20}, {120, 80}}) == 0, "a shape in its drawing");
	path = "/tmp/draw_shapes_text";
	Check(text.Save(path, error), "save the text document " + error);
	Document textBack;
	Check(LoadDocument(path, textBack, error) && textBack.GraphicsPart(1) != nullptr
		&& textBack.GraphicsPart(1)->layers[0].shapes.size() == 1
		&& textBack.GraphicsPart(1)->layers[0].shapes[0].kind == Sig("ELPS")
		&& textBack.WordPart(0) != nullptr, "reloaded with its ellipse and its text");

	// arranging: shapes 0 and 2 of a, b, c, d, e
	{
		GRLayer layer;
		for (int i = 0; i < 5; i++) {
			Shape sh;
			sh.values[2] = uint32_t(i);
			layer.shapes.push_back(sh);
		}
		auto ids = [&]() {
			std::string out;
			for (const Shape& sh : layer.shapes)
				out += char('a' + sh.values[2]);
			return out;
		};
		GRLayer copy = layer;
		std::vector<size_t> now = ArrangeShapes(layer, {0, 2}, 0);
		Check(ids() == "bdeac" && now == std::vector<size_t>{3, 4}, "to the front: " + ids());
		layer = copy;
		now = ArrangeShapes(layer, {2, 4}, 1);
		Check(ids() == "ceabd" && now == std::vector<size_t>{0, 1}, "to the back: " + ids());
		layer = copy;
		now = ArrangeShapes(layer, {0, 2}, 2);
		Check(ids() == "badce" && now == std::vector<size_t>{1, 3}, "forward: " + ids());
		layer = copy;
		now = ArrangeShapes(layer, {2, 4}, 3);
		Check(ids() == "acbed" && now == std::vector<size_t>{1, 3}, "backward: " + ids());
		layer = copy;
		ArrangeShapes(layer, {3, 4}, 2);
		Check(ids() == "abcde", "the top ones forward: as they were");
	}

	// a shape's fill and pen (its own style changed, the others' not)
	{
		Document fills;
		Check(CreateBlankDrawing(fills, error), "a drawing for inks " + error);
		AddShape(fills, 0, 0, kToolRect, {{0, 0}, {20, 20}});
		AddShape(fills, 0, 0, kToolRect, {{30, 0}, {50, 20}});
		Ink red;
		red.set = true;
		red.color.r = 255;
		Check(SetShapeProperty(fills, 0, 0, 0, InkProperty(Sig("FCol"), red)), "a red fill");
		Check(SetShapeProperty(fills, 0, 0, 0, OpacityProperty(Sig("PTra"), 0.5f)), "a half pen");
		const std::vector<Shape>& two = fills.GraphicsPart(0)->layers[0].shapes;
		ShapeStyle a = ResolveShapeStyle(fills.styles, two[0].style);
		ShapeStyle b = ResolveShapeStyle(fills.styles, two[1].style);
		Check(a.fill.color.r == 255 && a.fill.color.g == 0 && a.penOpacity == 0.5f,
			"the first: red, its pen half transparent");
		Check(b.fill.color.r == 255 && b.fill.color.g == 255 && b.penOpacity == 1,
			"the second as it was (white, opaque)");
		Check(fills.styles.styles[two[0].style].links[0]
			< fills.styles.styles.size(), "still based on GR Default");
		path = "/tmp/draw_shapes_inks";
		Check(fills.Save(path, error), "saved with inks " + error);
		Document inks;
		Check(LoadDocument(path, inks, error) && ResolveShapeStyle(inks.styles,
			inks.GraphicsPart(0)->layers[0].shapes[0].style).fill.color.r == 255,
			"reloaded red");
	}

	// rotating and reshaping
	{
		Shape box;
		box.kind = Sig("RRCT");
		box.origin[0] = 0;
		box.origin[1] = 0;
		box.size[0] = 20;
		box.size[1] = 20;
		Check(std::fabs(AngleAround(box, 20, 10)) < 0.01f, "right of the center: 0 degrees");
		Check(std::fabs(AngleAround(box, 10, 0) - 90) < 0.01f, "above it: 90");
		Document paths;
		Check(CreateBlankDrawing(paths, error), "a drawing for paths " + error);
		AddShape(paths, 0, 0, kToolPolygon, {{10, 10}, {50, 10}, {30, 40}});
		AddShape(paths, 0, 0, kToolLine, {{0, 0}, {10, 10}});
		GRPart* edit = paths.EditableGraphicsPart(0);
		Shape& poly = edit->layers[0].shapes[0];
		Check(HitVertex(poly, 50, 10, 2) == 1 && HitVertex(poly, 40, 30, 2) < 0, "a point hit");
		MoveVertex(poly, 2, 30, 70);
		std::vector<DrawPoint> points = ShapeVertices(poly);
		Check(points.size() == 3 && points[2][0] == 30 && points[2][1] == 70
			&& points[0][0] == 10 && points[0][1] == 10, "a point moved, the others kept");
		Check(poly.origin[1] == 10 && poly.size[1] == 60, "the box follows: 60 high");
		MoveVertex(poly, 0, 0, 0);
		points = ShapeVertices(poly);
		Check(poly.origin[0] == 0 && poly.origin[1] == 0 && points[1][0] == 50 && points[1][1] == 10,
			"the box's corner moved, the other points kept");
		Shape& line = edit->layers[0].shapes[1];
		MoveVertex(line, 1, 30, 5);
		Check(line.origin[0] == 0 && line.size[0] == 30 && line.size[1] == 5, "a line's end");
		MoveVertex(line, 0, 10, 10);
		Check(line.origin[0] == 10 && line.size[0] == 20 && line.size[1] == -5, "its start");
	}

	// a turned shape hit by its turned outline: 100 x 20 at 0, 0 turned 90
	// degrees stands up about its center (50, 10)
	{
		GRPart turned;
		turned.layers.resize(1);
		Shape bar;
		bar.kind = Sig("RRCT");
		bar.size[0] = 100;
		bar.size[1] = 20;
		bar.angle = 90;
		turned.layers[0].shapes = {bar};
		Check(HitShape(turned, 50, -30, 1, {}, layer, index), "turned: hit above the center");
		Check(!HitShape(turned, 90, 10, 1, {}, layer, index), "turned: not where it was");
		turned.layers[0].shapes[0].angle = 0;
		Check(HitShape(turned, 90, 10, 1, {}, layer, index), "unturned: hit there");
	}

	// aligning: three boxes
	{
		Shape a, b, c;
		for (Shape* sh : {&a, &b, &c})
			sh->kind = Sig("RRCT");
		a.origin[0] = 0; a.origin[1] = 0; a.size[0] = 10; a.size[1] = 10;
		b.origin[0] = 40; b.origin[1] = 20; b.size[0] = 20; b.size[1] = 30;
		c.origin[0] = 100; c.origin[1] = 5; c.size[0] = 10; c.size[1] = 10;
		std::vector<Shape*> three = {&a, &b, &c};
		AlignShapes(three, 0, 1);
		Check(a.origin[1] == 0 && b.origin[1] == 0 && c.origin[1] == 0 && b.origin[0] == 40,
			"top edges aligned, x kept");
		AlignShapes(three, 3, 0);
		Check(a.origin[0] == 100 && b.origin[0] == 90 && c.origin[0] == 100, "right edges");
		a.origin[0] = 0; b.origin[0] = 40; c.origin[0] = 100;
		AlignShapes(three, 2, 3);
		Check(a.origin[0] == 50 && b.origin[0] == 45 && a.origin[1] == 20 && b.origin[1] == 0,
			"centers across, bottom edges");
		a.origin[0] = 0; b.origin[0] = 20; c.origin[0] = 100;
		AlignShapes(three, 4, 0);
		// 110 wide in all, 40 of shapes: two gaps of 35
		Check(a.origin[0] == 0 && b.origin[0] == 45 && c.origin[0] == 100, "space distributed");
	}

	// the grid
	{
		Check(SnapToGrid(13, 9) == 9 && SnapToGrid(14, 9) == 18 && SnapToGrid(7, 0) == 7, "snapped");
		Shape sh;
		sh.kind = Sig("ELPS");
		sh.origin[0] = 13;
		sh.origin[1] = 22;
		sh.size[0] = 10;
		sh.size[1] = 10;
		AlignToGrid(sh, 9);
		Check(sh.origin[0] == 9 && sh.origin[1] == 18 && sh.size[0] == 10, "aligned to the grid");
		Shape locked = sh;
		locked.values[0] |= kShapeLocked;
		Check(ShapeLocked(locked) && !ShapeLocked(sh), "the lock flag");
	}

	// outlines: each kind as a path of the same box
	{
		Shape r, e, rr, arc, line;
		r.kind = Sig("RRCT"); rr.kind = Sig("RRCT"); e.kind = Sig("ELPS"); arc.kind = Sig("ARC ");
		line.kind = Sig("LINE");
		for (Shape* sh : {&r, &e, &rr, &arc}) {
			sh->origin[0] = 10; sh->origin[1] = 20; sh->size[0] = 40; sh->size[1] = 30;
		}
		rr.radius[0] = rr.radius[1] = 5;
		arc.arcAngles[1] = 90;
		line.origin[0] = 50; line.origin[1] = 50; line.size[0] = -30; line.size[1] = 10;
		line.values[0] = 0x100;
		Check(OutlineShape(r) && r.kind == Sig("BEZI") && r.paths[0].closed
			&& r.paths[0].vertices.size() == 4 && r.paths[0].vertices[2].point[0] == 40
			&& r.paths[0].vertices[2].point[1] == 30 && r.origin[0] == 10, "a rectangle's outline");
		Check(OutlineShape(e) && e.paths[0].vertices.size() == 4 && e.paths[0].vertices[1].point[0] == 40
			&& std::fabs(e.paths[0].vertices[0].control2[0] - 20 * 0.5522847f) < 0.001f,
			"an ellipse's: four smooth quarters");
		Check(OutlineShape(rr) && rr.paths[0].vertices.size() == 8, "a round rectangle's: eight");
		Check(OutlineShape(arc) && arc.paths[0].vertices.size() == 3
			&& arc.paths[0].vertices[0].point[0] == 20 && arc.paths[0].vertices[1].point[0] == 40
			&& std::fabs(arc.paths[0].vertices[2].point[1]) < 0.001f, "an arc's: center, 0 to 90");
		Check(OutlineShape(line) && !line.paths[0].closed && line.origin[0] == 20
			&& line.paths[0].vertices[0].point[0] == 30 && line.paths[0].vertices[1].point[0] == 0
			&& (line.values[0] & 0x100) == 0, "a line's: open, its direction kept");
	}

	// the toolbar shapes: the original's POLY resources
	{
		ResourceFile app;
		std::string base = "../original/resources/Gobe Productive/POLY/";
		size_t parsed = 0;
		BezierPath star;
		for (int id = 0; id < 64; id++) {
			FILE* f = fopen((base + std::to_string(id) + ".bin").c_str(), "rb");
			if (f == nullptr)
				continue;
			std::vector<uint8_t> bytes;
			int c;
			while ((c = fgetc(f)) != EOF)
				bytes.push_back(uint8_t(c));
			fclose(f);
			BezierPath p;
			if (ParseToolbarShape(bytes, p))
				parsed++;
			if (id == 0)
				star = p;
		}
		if (parsed > 0) {
			Check(star.vertices.size() == 10 && star.closed && star.vertices[0].point[0] == 0
				&& star.vertices[0].point[1] == 1, "POLY 0: ten points from (0, 1), closed");
			Document shapes;
			Check(CreateBlankDrawing(shapes, error), "a drawing for toolbar shapes " + error);
			int made = AddToolbarShape(shapes, 0, 0, star, {100, 100}, {160, 140});
			Check(made == 0, "a toolbar shape made");
			if (made == 0) {
				const Shape& sh = shapes.GraphicsPart(0)->layers[0].shapes[0];
				Check(sh.kind == Sig("BEZI") && sh.origin[0] == 100 && sh.origin[1] == 100
					&& std::fabs(sh.size[0] - 60) < 0.01f && std::fabs(sh.size[1] - 40) < 0.01f,
					"in the dragged box");
			}
			printf("toolbar shapes %zu\n", parsed);
		}
		// a cut resource is refused
		std::vector<uint8_t> cut = {2, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, '1', 0};
		BezierPath p;
		Check(!ParseToolbarShape(cut, p), "a cut POLY refused");
	}

	// the Bezier tool's path and its control points
	{
		Document curves;
		Check(CreateBlankDrawing(curves, error), "a drawing for curves " + error);
		Vertex a{{10, 10}, {0, 0}, {20, 0}, 4}, b{{60, 40}, {-10, -10}, {10, 10}, 2};
		Check(AddBezierShape(curves, 0, 0, {a, b}) == 0, "a Bezier path");
		Shape& sh = curves.EditableGraphicsPart(0)->layers[0].shapes[0];
		Check(sh.origin[0] == 10 && sh.size[0] == 50 && !sh.paths[0].closed
			&& sh.paths[0].vertices[1].point[0] == 50, "its box the points'");
		std::vector<std::pair<int, DrawPoint>> handles = ControlPoints(sh);
		Check(handles.size() == 3 && handles[0].first == 1 && handles[0].second[0] == 30,
			"three control points, the first after point 0");
		Check(HitControlPoint(sh, 50, 30, 2) == 2, "the one before point 1 hit");
		MoveControlPoint(sh, 2, 60, 20);
		Check(sh.paths[0].vertices[1].control1[0] == 0 && sh.paths[0].vertices[1].control1[1] == -20
			&& sh.paths[0].vertices[1].control2[0] == 0
			&& std::fabs(sh.paths[0].vertices[1].control2[1] - 14.1421f) < 0.01f,
			"moved; the smooth point's other one opposite, its length kept");
	}

	// grouping (TGRGroupCommand): a rectangle, an ellipse and a line; the
	// rectangle and the line grouped
	{
		Document doc2;
		Check(CreateBlankDrawing(doc2, error), "a drawing to group in " + error);
		AddShape(doc2, 0, 0, kToolRect, {{10, 20}, {50, 40}});
		AddShape(doc2, 0, 0, kToolEllipse, {{100, 100}, {140, 130}});
		AddShape(doc2, 0, 0, kToolLine, {{30, 60}, {80, 90}});
		std::vector<Shape> before3 = doc2.GraphicsPart(0)->layers[0].shapes;
		size_t parts = doc2.parts.parts.size();
		int group = GroupShapes(doc2, 0, 0, {0, 2}, error);
		Check(group == int(parts), "a group part made " + error);
		const std::vector<Shape>& top = doc2.GraphicsPart(0)->layers[0].shapes;
		size_t gp = 0;
		Check(top.size() == 2 && top[0].kind == Sig("ELPS") && IsGroupShape(doc2, top[1], gp)
			&& gp == size_t(group), "the ellipse, then the group where the line was");
		float box[4];
		Check(FrameShapeBounds(doc2, top[1], box) && box[0] == 10 && box[1] == 20 && box[2] == 80
			&& box[3] == 90, "the group's frame: the box of the two");
		const GRPart* inside = doc2.GraphicsPart(size_t(group));
		Check(inside != nullptr && inside->flag && inside->size[0] == 70 && inside->size[1] == 70
			&& inside->layers[0].shapes.size() == 2 && inside->layers[0].shapes[0].origin[0] == 0
			&& inside->layers[0].shapes[1].origin[0] == 20 && inside->layers[0].shapes[1].origin[1] == 40,
			"inside: a group (flag), 70 x 70, the shapes from its corner");
		path = "/tmp/draw_shapes_group";
		Check(doc2.Save(path, error), "saved grouped " + error);
		Document grouped;
		Check(LoadDocument(path, grouped, error) && grouped.warnings.empty(), "reloaded " + error);
		const std::vector<Shape>& top2 = grouped.GraphicsPart(0)->layers[0].shapes;
		size_t gp2 = 0;
		Check(top2.size() == 2 && IsGroupShape(grouped, top2[1], gp2)
			&& grouped.GraphicsPart(gp2)->layers[0].shapes.size() == 2
			&& grouped.GraphicsPart(gp2)->flag, "reloaded: the group and its two shapes");
		{
			// saved again unchanged: the same bytes
			Document copy;
			LoadDocument(path, copy, error);
			std::vector<uint8_t> first = copy.bytes;
			Check(copy.Save("/tmp/draw_shapes_group2", error), "saved again " + error);
			Document second;
			Check(LoadDocument("/tmp/draw_shapes_group2", second, error) && second.bytes == first,
				"the grouped drawing saves to the same bytes");
		}
		int placed = 0;
		for (const Document::PlacedFrame& f : grouped.PlacedFrames())
			if (f.part == gp2 && f.width == 70)
				placed++;
		Check(placed == 1, "the group placed");
		Check(UngroupShape(grouped, 0, 0, 1, error) == 2, "ungrouped " + error);
		const std::vector<Shape>& apart = grouped.GraphicsPart(0)->layers[0].shapes;
		bool same = apart.size() == 3 && apart[0].kind == Sig("ELPS");
		for (size_t k = 1; same && k < 3; k++) {
			const Shape& was = before3[k == 1 ? 0 : 2];
			same = apart[k].kind == was.kind && apart[k].origin[0] == was.origin[0]
				&& apart[k].origin[1] == was.origin[1] && apart[k].size[0] == was.size[0]
				&& apart[k].size[1] == was.size[1] && apart[k].style == was.style;
		}
		Check(same, "the shapes back where they were, in their order, with their styles");
		int stillPlaced = 0;
		for (const Document::PlacedFrame& f : grouped.PlacedFrames())
			stillPlaced += f.part == gp2;
		Check(stillPlaced == 0, "the group shown nowhere");
		path = "/tmp/draw_shapes_ungrouped";
		Check(grouped.Save(path, error), "saved ungrouped " + error);
		Document again;
		Check(LoadDocument(path, again, error) && again.warnings.empty()
			&& again.GraphicsPart(0)->layers[0].shapes.size() == 3, "reloaded ungrouped " + error);
		// undone and redone
		UndoStack steps;
		UndoStack::ViewState state;
		steps.BeginDocument(again, "Group Shapes", state);
		size_t partsNow = again.parts.parts.size();
		Check(GroupShapes(again, 0, 0, {1, 2}, error) >= 0, "grouped again");
		Check(steps.Undo(again, state) == UndoStack::kDocumentChanged
			&& again.parts.parts.size() == partsNow
			&& again.GraphicsPart(0)->layers[0].shapes.size() == 3, "the group undone");
		Check(steps.Redo(again, state) == UndoStack::kDocumentChanged
			&& again.GraphicsPart(0)->layers[0].shapes.size() == 2, "redone");
		Check(UngroupShape(again, 0, 0, 0, error) < 0, "the ellipse isn't a group");
	}

	// a sample's drawing: one more shape, the others as they were
	for (int i = 1; i < argc; i++) {
		std::string name = argv[i];
		if (name.size() < 9 || name.compare(name.size() - 9, 9, "TheWidget") != 0)
			continue;
		Document widget;
		if (!LoadDocument(name, widget, error))
			continue;
		std::vector<Shape> old = widget.GraphicsPart(0)->layers[0].shapes;
		int added = AddShape(widget, 0, 0, kToolRoundRect, {{40, 40}, {90, 70}});
		Check(added == int(old.size()), "TheWidget: a shape added");
		path = "/tmp/draw_shapes_widget";
		Check(widget.Save(path, error), "TheWidget saved " + error);
		Document reloaded;
		Check(LoadDocument(path, reloaded, error), "TheWidget reloaded " + error);
		const std::vector<Shape>& now = reloaded.GraphicsPart(0)->layers[0].shapes;
		bool kept = now.size() == old.size() + 1;
		for (size_t k = 0; kept && k < old.size(); k++)
			kept = SameShape(old[k], now[k]);
		Check(kept && now.back().kind == Sig("RRCT") && now.back().radius[0] == 25,
			"TheWidget: its shapes kept, the new one after them");
		Check(reloaded.Chart(6) != nullptr && reloaded.WordPart(1) != nullptr,
			"TheWidget: its chart and text still read");
	}
	printf("draw_shapes: ok %d, failed %d\n", sOk, sFailed);
	return sFailed ? 1 : 0;
}
