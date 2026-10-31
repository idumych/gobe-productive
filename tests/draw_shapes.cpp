// draw_shapes: the drawing tools (TPartGR::DragOut and TShape's editing).
// A new drawing gets a shape of each tool; they are hit, moved, resized,
// saved and reloaded the same; undone and redone; a text document's sheet
// drawing and TheWidget (if given) get one too, their other shapes kept.
// Arguments: documents (TheWidget is used when given).
#include <cmath>
#include <cstdio>
#include <string>

#include "Document.h"
#include "Drawing.h"
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
