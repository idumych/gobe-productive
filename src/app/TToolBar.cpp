#include "TToolBar.h"

#include <MenuItem.h>
#include <PopUpMenu.h>
#include <Shape.h>
#include <Window.h>

#include <algorithm>

#include "Drawing.h"
#include "GobeResources.h"
#include "TPageView.h"

namespace gobe {

namespace {

// TheTools, ToolIcons and ToolTips (the original's data)
const struct {
	uint32 tool;
	int32 icon;
	int32 tip;
} kTools[] = {
	{kToolArrow, 0x65, 12}, {kToolPart, 0xca, 13}, {kToolRect, 0x66, 14},
	{kToolEllipse, 0x67, 15}, {kToolRoundRect, 0x68, 16}, {kToolArc, 0x69, 17},
	{kToolLine, 0x6a, 18}, {kToolFree, 0x6b, 19}, {kToolBezier, 0x6d, 20},
	{kToolPolygon, 0xfa, 21}, {kToolShapes, 0xfa, 22}
};

const uint32 kToolFamily = 'TOOL';
const float kWidth = 26;


// a toolbar shape's menu item (TToolbarShapesMenu: each shape's picture)
class TShapeItem : public BMenuItem {
public:
	TShapeItem(const BezierPath& path, BMessage* message)
		: BMenuItem("", message), fPath(path) {}

	virtual void GetContentSize(float* width, float* height)
	{
		*width = 24;
		*height = 24;
	}

	virtual void DrawContent()
	{
		BView* view = Menu();
		BRect frame = Frame().InsetByCopy(3, 3);
		float left = 1e9f, top = 1e9f, right = -1e9f, bottom = -1e9f;
		for (const Vertex& v : fPath.vertices) {
			left = std::min(left, v.point[0]);
			right = std::max(right, v.point[0]);
			top = std::min(top, v.point[1]);
			bottom = std::max(bottom, v.point[1]);
		}
		float scale = std::min(frame.Width() / std::max(right - left, 0.001f),
			frame.Height() / std::max(bottom - top, 0.001f));
		auto at = [&](float x, float y) {
			return BPoint(frame.left + (x - left) * scale, frame.top + (y - top) * scale);
		};
		BShape shape;
		const std::vector<Vertex>& v = fPath.vertices;
		shape.MoveTo(at(v[0].point[0], v[0].point[1]));
		size_t n = v.size();
		for (size_t i = 1; i < n + (fPath.closed ? 1 : 0); i++) {
			const Vertex& a = v[i - 1];
			const Vertex& b = v[i % n];
			BPoint c[3] = {at(a.point[0] + a.control2[0], a.point[1] + a.control2[1]),
				at(b.point[0] + b.control1[0], b.point[1] + b.control1[1]),
				at(b.point[0], b.point[1])};
			shape.BezierTo(c);
		}
		if (fPath.closed)
			shape.Close();
		view->PushState();
		view->MovePenTo(B_ORIGIN);
		view->SetHighColor(0, 0, 0);
		view->StrokeShape(&shape);
		view->PopState();
	}

private:
	BezierPath fPath;
};


// TToolBarPopUp 'TOOS' (TToolbarShapesMenu): the shapes in a grid; one
// chosen is the tool's shape
class TShapesElement : public TMICNButtonElement {
public:
	TShapesElement(BPoint where, int32 icon)
		: TMICNButtonElement(kToolShapes, where, icon, NULL) {}

	virtual bool MouseDown(TRibbon* ribbon, BPoint where)
	{
		BPopUpMenu* menu = new BPopUpMenu("shapes", false, false, B_ITEMS_IN_MATRIX);
		const int32 kColumns = 6;
		int32 count = 0;
		for (int32 id = 0; id < 256; id++) {
			const ResourceFile::Resource* r = GobeResources::File().Find('POLY', id);
			BezierPath path;
			if (r == NULL || !ParseToolbarShape(r->data, path))
				continue;
			BMessage* message = new BMessage(TToolBar::kMsgTool);
			message->AddInt32("tool", int32(kToolShapes));
			message->AddInt32("shape", id);
			float x = (count % kColumns) * 30.0f, y = (count / kColumns) * 30.0f;
			menu->AddItem(new TShapeItem(path, message), BRect(x, y, x + 29, y + 29));
			count++;
		}
		ribbon->DrawElement(this, true);
		BPoint at = ribbon->ConvertToScreen(BPoint(rect.right + 1, rect.top));
		BMenuItem* chosen = count > 0 ? menu->Go(at, false, true, false) : NULL;
		ribbon->DrawElement(this, false);
		if (chosen != NULL && chosen->Message() != NULL) {
			ribbon->SelectElement(kToolShapes, kToolFamily);
			ribbon->Send(chosen->Message());
		}
		delete menu;
		return true;
	}
};

} // namespace


TToolBar::TToolBar(TPageView* view)
	:
	TRibbon("ToolBar"),
	fView(view)
{
	// TToolBar::LoadAllElements
	float y = 2;
	for (const auto& t : kTools) {
		TMICNButtonElement* button;
		if (t.tool == kToolShapes)
			button = new TShapesElement(BPoint(2, y), t.icon);
		else {
			BMessage* message = new BMessage(kMsgTool);
			message->AddInt32("tool", int32(t.tool));
			button = new TMICNButtonElement(t.tool, BPoint(2, y), t.icon, message);
		}
		button->family = kToolFamily;
		button->selectable = true;
		// (the part frames tool isn't ported)
		button->enabled = t.tool != kToolPart;
		button->tip = GobeResources::IndexString(4, t.tip).String();
		AddElement(button);
		y += 27;
		if (t.tool == kToolPart)
			y += 6;
	}
	SelectElement(kToolArrow, kToolFamily);
}


BSize
TToolBar::MinSize()
{
	return BSize(kWidth, 100);
}


BSize
TToolBar::MaxSize()
{
	return BSize(kWidth, B_SIZE_UNLIMITED);
}


BSize
TToolBar::PreferredSize()
{
	return BSize(kWidth, 320);
}


void
TToolBar::Draw(BRect updateRect)
{
	TRibbon::Draw(updateRect);
	// (a column: the edge on the right instead of the bottom)
	BRect bounds = Bounds();
	SetHighColor(tint_color(ui_color(B_PANEL_BACKGROUND_COLOR), B_DARKEN_2_TINT));
	StrokeLine(BPoint(bounds.right, bounds.top), BPoint(bounds.right, bounds.bottom));
}


void
TToolBar::Update()
{
	if (fView != NULL)
		SelectElement(fView->Tool(), kToolFamily);
}

} // namespace gobe
