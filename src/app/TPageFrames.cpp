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
#include <cmath>

#include "GobeResources.h"
#include "GRPart.h"
#include "InlineParts.h"
#include "TToolBar.h"

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

} // namespace


// the arrow on a frame's edge (within kEdge of it) in a drawing: the
// frame selected (TFrame::Select), the topmost
bool
TPageView::FrameClick(BPoint where)
{
	if (fTool != 'ARRW')
		return false;
	float dx = fMargin - fBounds.left, dy = fMargin - fBounds.top;
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
	fCaret.active = false;
	fAnchor.active = false;
	fSelectedCell.active = false;
	fShape = ShapeRef();
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

} // namespace gobe
