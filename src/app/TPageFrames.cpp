// Part frames in the page view: selected by their edge, made with the
// ToolBar's part tool or the Frame menu's parts (TPartGR::DragOut with a
// part kind, TDocWindow::DoInsertFrame), and the Frame menu's commands
// for them (TDocWindow::MakeLinkedFrame, DoCloneFrame, TFrameCommand).
#include "TPageView.h"

#include <Beep.h>
#include <MenuBar.h>
#include <MenuItem.h>
#include <Window.h>

#include <algorithm>
#include <map>
#include <cmath>

#include "GobeResources.h"
#include "GRPart.h"
#include "InlineParts.h"
#include "TToolBar.h"
#include "TWPDialogs.h"
#include "TextWrap.h"
#include "Drawing.h"

namespace gobe {

namespace {

const float kEdge = 4;		// how near its edge a click selects a frame


bool
IsDrawingPart(const Document& doc, size_t part)
{
	return part < doc.parts.parts.size() && (doc.parts.parts[part].kind == Sig("GRFX")
		|| doc.parts.parts[part].kind == Sig("PRES"));
}


// the pointer to a placed frame (the part, its portal's id, its id)
bool
PointerOf(const Document& doc, const Document::PlacedFrame& placed, uint32_t pointer[3])
{
	if (placed.part >= doc.tails.size())
		return false;
	for (const Portal& p : doc.tails[placed.part].portals)
		for (const gobe::Frame& f : p.frames)
			if (&f == placed.frame) {
				pointer[0] = uint32_t(placed.part);
				pointer[1] = p.values[0];
				pointer[2] = f.values[1];
				return true;
			}
	return false;
}

const Shape*
SelectedShape(const Document& doc, size_t part, size_t layer, size_t index)
{
	const GRPart* gr = doc.GraphicsPart(part);
	if (gr == nullptr || layer >= gr->layers.size() || index >= gr->layers[layer].shapes.size())
		return nullptr;
	return &gr->layers[layer].shapes[index];
}


// the eight handles of a frame's rectangle, from the top left clockwise
BPoint
HandleAt(BRect r, int i)
{
	float cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2;
	const float xs[8] = {r.left, cx, r.right, r.right, r.right, cx, r.left, r.left};
	const float ys[8] = {r.top, r.top, r.top, cy, r.bottom, r.bottom, r.bottom, cy};
	return BPoint(xs[i], ys[i]);
}


// a rectangle with handle `i` dragged by dx, dy (TFrame::Track)
BRect
Resized(BRect r, int i, float dx, float dy)
{
	if (i == 0 || i == 6 || i == 7)
		r.left += dx;
	if (i == 2 || i == 3 || i == 4)
		r.right += dx;
	if (i == 0 || i == 1 || i == 2)
		r.top += dy;
	if (i == 4 || i == 5 || i == 6)
		r.bottom += dy;
	return BRect(std::min(r.left, r.right), std::min(r.top, r.bottom),
		std::max(r.left, r.right), std::max(r.top, r.bottom));
}

} // namespace


// the selected frame's handle or edge (before the drawing's shapes): it
// resized or moved (TFrame::Track)
bool
TPageView::FrameGrab(BPoint where)
{
	if (!fFrame.active || fTool != 'ARRW')
		return false;
	float dx = fMargin - fBounds.left, dy = fMargin - fBounds.top;
	const gobe::Frame* selected = FrameAt(fDocument, fFrame.pointer);
	for (const Document::PlacedFrame& f : fFrames) {
		if (f.frame != selected || selected == NULL)
			continue;
		BRect r(f.x + dx, f.y + dy, f.x + dx + f.width, f.y + dy + f.height);
		int handle = -1;
		for (int i = 0; i < 8 && handle < 0; i++) {
			BPoint h = HandleAt(r, i);
			if (std::fabs(where.x - h.x) <= kEdge && std::fabs(where.y - h.y) <= kEdge)
				handle = i;
		}
		BRect outer = r.InsetByCopy(-kEdge, -kEdge), inner = r.InsetByCopy(kEdge, kEdge);
		bool edge = outer.Contains(where) && !(inner.IsValid() && inner.Contains(where));
		if (handle < 0 && !edge)
			break;
		fFrameDrag.mode = handle >= 0 ? 2 : 1;
		fFrameDrag.handle = handle;
		fFrameDrag.start = fFrameDrag.now = where;
		fFrameDrag.rect = r;
		if (Window() != NULL)
			SetMouseEventMask(B_POINTER_EVENTS, B_LOCK_WINDOW_FOCUS);
		return true;
	}
	return false;
}


// the arrow on a frame's edge (within kEdge of it) in a drawing: the
// frame selected (TFrame::Select), the topmost
bool
TPageView::FrameClick(BPoint where)
{
	if (fTool != 'ARRW')
		return false;
	float dx = fMargin - fBounds.left, dy = fMargin - fBounds.top;
	if (FrameGrab(where))
		return true;
	const Document::PlacedFrame* found = NULL;
	for (const Document::PlacedFrame& f : fFrames) {
		if (f.frame == NULL || !IsDrawingPart(fDocument, f.frame->container))
			continue;
		BRect r(f.x + dx, f.y + dy, f.x + dx + f.width, f.y + dy + f.height);
		BRect outer = r.InsetByCopy(-kEdge, -kEdge), inner = r.InsetByCopy(kEdge, kEdge);
		if (!outer.Contains(where) || (inner.IsValid() && inner.Contains(where)))
			continue;
		if (found == NULL || f.depth >= found->depth)
			found = &f;
	}
	uint32_t pointer[3];
	if (found == NULL || !PointerOf(fDocument, *found, pointer)) {
		if (fFrame.active) {
			fFrame = FrameRef();
			Invalidate();
		}
		return false;
	}
	fFrame.active = true;
	std::copy(pointer, pointer + 3, fFrame.pointer);
	// (a drag from the edge goes on to move it)
	fFrameDrag.mode = 1;
	fFrameDrag.start = fFrameDrag.now = where;
	fFrameDrag.rect = BRect(found->x + dx, found->y + dy, found->x + dx + found->width,
		found->y + dy + found->height);
	if (Window() != NULL)
		SetMouseEventMask(B_POINTER_EVENTS, B_LOCK_WINDOW_FOCUS);
	fCaret.active = false;
	fAnchor.active = false;
	fSelectedCell.active = false;
	fShapes.clear();
	Invalidate();
	return true;
}


// TFrame::DrawSelection: its outline and the eight handles
void
TPageView::DrawFrameSelection(float dx, float dy)
{
	if (!fFrame.active)
		return;
	const gobe::Frame* selected = FrameAt(fDocument, fFrame.pointer);
	for (const Document::PlacedFrame& f : fFrames) {
		if (f.frame != selected)
			continue;
		BRect r(f.x + dx, f.y + dy, f.x + dx + f.width, f.y + dy + f.height);
		if (fFrameDrag.mode != 0)
			r = DraggedFrame();
		PushState();
		SetDrawingMode(B_OP_COPY);
		SetHighColor(0, 0, 0);
		SetPenSize(1);
		StrokeRect(r, B_MIXED_COLORS);
		for (float x : {r.left, (r.left + r.right) / 2, r.right})
			for (float y : {r.top, (r.top + r.bottom) / 2, r.bottom}) {
				if (x == (r.left + r.right) / 2 && y == (r.top + r.bottom) / 2)
					continue;
				FillRect(BRect(x - 2, y - 2, x + 2, y + 2));
			}
		PopState();
		break;
	}
}


// the rectangle a selected frame is being dragged to
BRect
TPageView::DraggedFrame() const
{
	float dx = fFrameDrag.now.x - fFrameDrag.start.x, dy = fFrameDrag.now.y - fFrameDrag.start.y;
	if (fFrameDrag.mode == 2)
		return Resized(fFrameDrag.rect, fFrameDrag.handle, dx, dy);
	return fFrameDrag.rect.OffsetByCopy(dx, dy);
}


bool
TPageView::FrameDragMoved(BPoint where)
{
	if (fFrameDrag.mode == 0)
		return false;
	fFrameDrag.now = where;
	Invalidate();
	return true;
}


// the drag's end (TFrame::Track's): the frame moved or resized, undoably
bool
TPageView::FrameDragUp(BPoint where)
{
	if (fFrameDrag.mode == 0)
		return false;
	fFrameDrag.now = where;
	BRect to = DraggedFrame();
	BRect from = fFrameDrag.rect;
	int mode = fFrameDrag.mode;
	fFrameDrag = FrameDrag();
	const gobe::Frame* f = FrameAt(fDocument, fFrame.pointer);
	if (f == NULL || to == from) {
		Invalidate();
		return true;
	}
	// STR# 1101: 7 Move Shapes, 6 Resize Shapes
	fUndo.BeginDocument(fDocument, GobeResources::IndexString(1101,
		mode == 2 ? 6 : 7).String(), CurrentViewState());
	bool done;
	if (mode == 1)
		done = MoveFrame(fDocument, fFrame.pointer, to.left - from.left, to.top - from.top);
	else {
		float dx = to.left - from.left, dy = to.top - from.top;
		float rect[4] = {f->origin[0] + dx, f->origin[1] + dy,
			f->origin[0] + dx + to.Width(), f->origin[1] + dy + to.Height()};
		done = ResizeFrame(fDocument, fFrame.pointer, rect);
	}
	if (done) {
		GeometryChanged();
		if (Window() != NULL)
			Window()->PostMessage(kMsgDocumentChanged);
	} else
		beep();
	Invalidate();
	return true;
}


// the part tool's drag (TPartGR::DragOut): a frame of the chosen kind at
// the dragged rectangle, selected; the tool back to the arrow
bool
TPageView::FinishPartFrame(size_t drawing, const std::vector<std::array<float, 2>>& points)
{
	if (points.size() < 2)
		return false;
	float rect[4] = {points.front()[0], points.front()[1], points.back()[0], points.back()[1]};
	if (std::fabs(rect[2] - rect[0]) < 4 || std::fabs(rect[3] - rect[1]) < 4)
		return false;		// (a click: no frame)
	fUndo.BeginDocument(fDocument, GobeResources::IndexString(1101, 0).String(),
		CurrentViewState());
	std::string error;
	const GRPart* gr = fDocument.GraphicsPart(drawing);
	size_t layer = gr != NULL && !gr->layers.empty() ? gr->layers.size() - 1 : 0;
	int part = AddDrawingFrame(fDocument, drawing, layer, fPartKind, rect, error);
	if (part < 0) {
		fprintf(stderr, "part frame: %s\n", error.c_str());
		return false;
	}
	fFrame.active = true;
	fFrame.pointer[0] = uint32_t(part);
	fFrame.pointer[1] = fFrame.pointer[2] = 0;
	fTool = 'ARRW';
	if (fToolBar != NULL)
		fToolBar->Update();
	GeometryChanged();
	if (Window() != NULL)
		Window()->PostMessage(kMsgDocumentChanged);
	return true;
}


// Frame > a part (TDocWindow::DoInsertFrame, 's00T' with its Kind): a
// frame of it in the sheet's drawing, 3 by 2 inches an inch in
void
TPageView::InsertPartFrame(uint32_t kind)
{
	int sheet = fDocument.CurrentSheet();
	if (sheet < 0 || size_t(sheet) >= fDocument.sheets.sheets.size())
		return;
	size_t drawing = fDocument.sheets.sheets[size_t(sheet)].graphicsPart;
	if (fDocument.GraphicsPart(drawing) == NULL) {
		beep();
		return;
	}
	uint32 saved = fPartKind;
	fPartKind = kind;
	FinishPartFrame(drawing, {{72, 72}, {72 + 216, 72 + 144}});
	fPartKind = saved;
}


// the Frame menu's commands for the selected frame, the part tool's kind
bool
TPageView::FrameCommand(BMessage* message)
{
	switch (message->what) {
		case 'sM4Q':
			// TDocWindow::SetTool: the ToolBar's part menu (Tool and Kind)
			if (message->HasInt32("Kind"))
				fPartKind = uint32(message->GetInt32("Kind", 'WORD'));
			SetTool(uint32(message->GetInt32("Tool", 'ARRW')));
			return true;
		case 's00T': {
			uint32 kind = uint32(message->GetInt32("Kind", 0));
			if (kind == 'chrt')
				return false;		// Make Chart: the spreadsheet's
			InsertPartFrame(kind);
			return true;
		}
		case 'sf0U':
			TextWrapCommand(message);
			return true;
		case 'sM32':
		case 'sM33':
		case 'sM34':
		case 'sM35':
		case 'sM36':
			break;
		default:
			return false;
	}
	if (!fFrame.active || FrameAt(fDocument, fFrame.pointer) == NULL) {
		beep();
		return true;
	}
	std::string label;
	if (BMenuItem* item = Window() != NULL && Window()->KeyMenuBar() != NULL
			? Window()->KeyMenuBar()->FindItem(message->what) : NULL)
		label = item->Label();
	fUndo.BeginDocument(fDocument, label, CurrentViewState());
	std::string error;
	if (message->what == 'sM32' || message->what == 'sM33') {
		uint32_t made[3];
		bool ok = message->what == 'sM32'
			? AddLinkedFrame(fDocument, fFrame.pointer, made, error)
			: CloneFrame(fDocument, fFrame.pointer, made, error);
		if (!ok) {
			fprintf(stderr, "frame: %s\n", error.c_str());
			beep();
			return true;
		}
		std::copy(made, made + 3, fFrame.pointer);
	} else {
		gobe::Frame* f = const_cast<gobe::Frame*>(FrameAt(fDocument, fFrame.pointer));
		f->values[3] = FrameCommandFlags(f->values[3], message->what);
		fDocument.dirtyTails.insert(fFrame.pointer[0]);
	}
	GeometryChanged();
	if (Window() != NULL)
		Window()->PostMessage(kMsgDocumentChanged);
	return true;
}

// Text Wrap ('sf0U'; TPart::HandleMessage, TGRFormatCommand): without its
// kind, the dialog with the first selected shape's or the frame's (None,
// both sides, 10 points round without one); with it, the selected shapes
// and frame wrapped so
void
TPageView::TextWrapCommand(BMessage* message)
{
	const gobe::Frame* frame = fFrame.active ? FrameAt(fDocument, fFrame.pointer) : NULL;
	if (fShapes.empty() && frame == NULL) {
		beep();
		return;
	}
	if (!message->HasInt32("Wrap Kind") || !message->HasInt32("Wrap To")) {
		ShapeWrap current;
		current.kind = 0;
		current.to = 0;
		std::fill(current.margins, current.margins + 4, 10.0f);
		uint32_t flags = 0, style = 0;
		if (!fShapes.empty()) {
			if (const Shape* s = SelectedShape(fDocument, fShapes[0].part, fShapes[0].layer,
					fShapes[0].index)) {
				flags = s->values[0];
				style = s->style;
			}
		} else {
			flags = frame->shape[0];
			style = frame->style;
		}
		if ((flags & kShapeWraps) != 0)
			current = ResolveShapeStyle(fDocument.styles, style).wrap;
		int unit = fDocument.sheets.sheets.empty() ? 0 : int(fDocument.sheets.sheets[0].unit);
		(new TDialogTextWrap(Window(), current.kind, current.to, current.margins, unit))
			->RunDialog();
		return;
	}
	ShapeWrap wrap;
	wrap.kind = message->GetInt32("Wrap Kind", 0);
	wrap.to = message->GetInt32("Wrap To", 0);
	BRect margins = message->GetRect("margins", BRect(0, 0, 0, 0));
	wrap.margins[0] = margins.left;
	wrap.margins[1] = margins.top;
	wrap.margins[2] = margins.right;
	wrap.margins[3] = margins.bottom;
	// TGRFormatCommand ("Format - ", as the shapes' other formatting)
	fUndo.BeginDocument(fDocument, std::string(GobeResources::IndexString(5, 5).String())
		+ GobeResources::IndexString(11, 0).String(), CurrentViewState());
	for (const ShapeRef& ref : fShapes) {
		const Shape* s = SelectedShape(fDocument, ref.part, ref.layer, ref.index);
		if (s != NULL && s->kind == Sig("FRAM"))
			SetFrameWrap(fDocument, s->frame, wrap);
		else
			SetShapeWrap(fDocument, ref.part, ref.layer, ref.index, wrap);
	}
	if (frame != NULL)
		SetFrameWrap(fDocument, fFrame.pointer, wrap);
	GeometryChanged();
	if (Window() != NULL)
		Window()->PostMessage(kMsgDocumentChanged);
}

// TTextWrapMgr (TShape::CheckTextWrap): the shapes with flag 0x40 in the
// drawings shown, and the frames whose FRAM shape has it, by their boxes
// (rotation aside); Tight as Regular with one distance round
void
TPageView::CollectWrapObstacles()
{
	fWrapObstacles.clear();
	fWrapBounds.clear();
	float dx = fMargin - fBounds.left, dy = fMargin - fBounds.top;
	auto add = [&](BRect r, const ShapeWrap& wrap) {
		if (wrap.kind == 0)
			return;
		const float* m = wrap.margins;
		bool tight = wrap.kind == 2;
		WrapObstacle o{{r.left - (tight ? m[1] : m[0]), r.top - m[1],
			r.right + (tight ? m[1] : m[2]), r.bottom + (tight ? m[1] : m[3])}, wrap.kind, wrap.to};
		fWrapObstacles.push_back(o);
		fWrapBounds.push_back(r);
	};
	for (const Document::PlacedFrame& f : fFrames) {
		if (f.frame != NULL && (f.frame->shape[0] & kShapeWraps) != 0)
			add(BRect(f.x + dx, f.y + dy, f.x + dx + f.width, f.y + dy + f.height),
				ResolveShapeStyle(fDocument.styles, f.frame->style).wrap);
		uint32_t kind = fDocument.parts.parts[f.part].kind;
		const GRPart* gr = kind == Sig("GRFX") || kind == Sig("PRES")
			? fDocument.GraphicsPart(f.part) : NULL;
		if (gr == NULL)
			continue;
		for (const GRLayer& layer : gr->layers)
			for (const Shape& shape : layer.shapes) {
				if ((shape.values[0] & kShapeWraps) == 0 || shape.kind == Sig("FRAM"))
					continue;
				float b[4];
				ShapeBounds(shape, b);
				add(BRect(f.x + dx + b[0], f.y + dy + b[1], f.x + dx + b[2], f.y + dy + b[3]),
					ResolveShapeStyle(fDocument.styles, shape.style).wrap);
			}
	}
}

void
TPageView::ShowFrameOnly(const uint32_t pointer[3])
{
	fFrameOnly.active = true;
	std::copy(pointer, pointer + 3, fFrameOnly.pointer);
	Rebuild();
	InvalidateLayout();
}


// a frame window's frames: the frame and the frames in its part (and in
// theirs), moved to the corner
void
TPageView::KeepFrameOnly()
{
	const gobe::Frame* shown = FrameAt(fDocument, fFrameOnly.pointer);
	auto it = std::find_if(fFrames.begin(), fFrames.end(),
		[&](const Document::PlacedFrame& f) { return f.frame == shown; });
	if (shown == NULL || it == fFrames.end()) {
		fFrameOnly = FrameRef();
		return;
	}
	Document::PlacedFrame top = *it;
	// each part's container (its first frame's)
	std::map<size_t, uint32_t> containers;
	for (const Document::PlacedFrame& f : fFrames)
		containers.emplace(f.part, f.frame->container);
	auto inside = [&](const Document::PlacedFrame& f) {
		if (f.frame == shown)
			return true;
		uint32_t container = f.frame->container;
		for (int depth = 0; depth < 16 && container != kNoPart; depth++) {
			if (container == top.part)
				return true;
			auto c = containers.find(container);
			if (c == containers.end())
				return false;
			container = c->second;
		}
		return false;
	};
	std::vector<Document::PlacedFrame> kept;
	for (const Document::PlacedFrame& f : fFrames)
		if (inside(f)) {
			// (the part's other frames aren't shown)
			if (f.part == top.part && f.frame != shown)
				continue;
			Document::PlacedFrame moved = f;
			moved.x -= top.x;
			moved.y -= top.y;
			moved.page = -1;
			kept.push_back(moved);
		}
	fFrames = kept;
}

} // namespace gobe
