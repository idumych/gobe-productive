#include "TToolBar.h"

#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <FindDirectory.h>
#include <Path.h>
#include <MenuItem.h>
#include <PopUpMenu.h>
#include <Shape.h>
#include <Window.h>

#include <algorithm>
#include <string>

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
// (33 wide: the tools at 2, 28 square)
const float kWidth = 32;


// a toolbar shape's outline scaled into `frame`
void
ShapeIn(const BezierPath& path, BRect frame, BShape& shape)
{
	float left = 1e9f, top = 1e9f, right = -1e9f, bottom = -1e9f;
	for (const Vertex& v : path.vertices) {
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
	const std::vector<Vertex>& v = path.vertices;
	if (v.empty())
		return;
	shape.MoveTo(at(v[0].point[0], v[0].point[1]));
	size_t n = v.size();
	for (size_t i = 1; i < n + (path.closed ? 1 : 0); i++) {
		const Vertex& a = v[i - 1];
		const Vertex& b = v[i % n];
		BPoint c[3] = {at(a.point[0] + a.control2[0], a.point[1] + a.control2[1]),
			at(b.point[0] + b.control1[0], b.point[1] + b.control1[1]),
			at(b.point[0], b.point[1])};
		shape.BezierTo(c);
	}
	if (path.closed)
		shape.Close();
}


// the toolbar shape 'POLY' `id`
bool
ToolbarShape(int32 id, BezierPath& path)
{
	return TToolBar::ShapePath(id, path);
}


// TToolbarShapes::getToolbarShapesFolder: the user's added shapes
BPath
UserShapesFolder(bool create)
{
	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) != B_OK)
		return BPath();
	path.Append("Gobe Productive port/Toolbar Shapes");
	if (create)
		create_directory(path.Path(), 0755);
	return path;
}


// the part tool (TPartMenu behind its button): a pop-up of the part kinds
// a frame can be made of; the one chosen sends TDocWindow::SetTool's
// 'sM4Q' with Tool 'PART' and its Kind
class TPartToolElement : public TMICNButtonElement {
public:
	TPartToolElement(BPoint where, int32 micn)
		:
		TMICNButtonElement(kToolPart, where, micn, NULL)
	{
	}

	virtual void Invoke(TRibbon* ribbon)
	{
		static const struct {
			const char* name;
			uint32 kind;
		} kParts[] = {
			{"Word Processing", 'WORD'}, {"Spreadsheet", 'SPRD'}, {"Graphics", 'GRFX'},
			{"Table", 'TBLE'}, {"Image Processing", 'BITS'}
		};
		BPopUpMenu* menu = new BPopUpMenu("parts", false, false);
		for (const auto& p : kParts) {
			BMessage* message = new BMessage('sM4Q');
			message->AddInt32("Tool", int32(kToolPart));
			message->AddInt32("Kind", int32(p.kind));
			menu->AddItem(new BMenuItem(p.name, message));
		}
		BPoint at = ribbon->ConvertToScreen(BPoint(rect.right + 1, rect.top));
		BMenuItem* chosen = menu->Go(at, false, true, false);
		if (chosen != NULL && chosen->Message() != NULL)
			ribbon->Send(chosen->Message());
		delete menu;
	}
};


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
		BShape shape;
		ShapeIn(fPath, Frame().InsetByCopy(3, 3), shape);
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
// chosen is the tool's shape, and its picture the tool's icon (at first
// the menu's first, TToolBarPopUp::getFirstMenuImage): white, outlined
class TShapesElement : public TMICNButtonElement {
public:
	TShapesElement(BPoint where, int32 icon)
		: TMICNButtonElement(kToolShapes, where, icon, NULL)
	{
		BezierPath path;
		for (int32 id : TToolBar::ShapeIds()) {
			if (ToolbarShape(id, path)) {
				fShape = id;
				break;
			}
		}
	}

	virtual bool MouseDown(TRibbon* ribbon, BPoint where)
	{
		BPopUpMenu* menu = new BPopUpMenu("shapes", false, false, B_ITEMS_IN_MATRIX);
		const int32 kColumns = 6;
		int32 count = 0;
		for (int32 id : TToolBar::ShapeIds()) {
			BezierPath path;
			if (!ToolbarShape(id, path))
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
			fShape = chosen->Message()->GetInt32("shape", fShape);
			ribbon->SelectElement(kToolShapes, kToolFamily);
			ribbon->Send(chosen->Message());
		}
		delete menu;
		return true;
	}

protected:
	virtual void DrawIcon(BView* view, BPoint where)
	{
		BezierPath path;
		if (fShape < 0 || !ToolbarShape(fShape, path)) {
			TMICNButtonElement::DrawIcon(view, where);
			return;
		}
		BShape shape;
		ShapeIn(path, BRect(where, where + BPoint(15, 15)), shape);
		view->PushState();
		view->MovePenTo(B_ORIGIN);
		view->SetHighColor(255, 255, 255);
		view->FillShape(&shape);
		view->SetHighColor(0, 0, 0);
		view->StrokeShape(&shape);
		view->PopState();
	}

private:
	int32 fShape = -1;
};

} // namespace


TToolBar::TToolBar(TPageView* view)
	:
	TRibbon("ToolBar"),
	fView(view)
{
	// TToolBar::LoadAllElements: TToolBarElements and TToolBarPopUps (the
	// arrow, part and shapes tools), 28 square, each 27 below the last
	// (their edges shared), 6 more after the part tool
	float y = 2;
	for (const auto& t : kTools) {
		TMICNButtonElement* button;
		if (t.tool == kToolPart)
			button = new TPartToolElement(BPoint(2, y), t.icon);
		else if (t.tool == kToolShapes)
			button = new TShapesElement(BPoint(2, y), t.icon);
		else {
			BMessage* message = new BMessage(kMsgTool);
			message->AddInt32("tool", int32(t.tool));
			button = new TMICNButtonElement(t.tool, BPoint(2, y), t.icon, message);
		}
		button->rect.Set(2, y, 2 + 28 - 1, y + 28 - 1);
		button->popUp = t.tool == kToolArrow || t.tool == kToolPart || t.tool == kToolShapes;
		button->family = kToolFamily;
		button->selectable = true;
		button->enabled = true;
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

std::vector<int32>
TToolBar::ShapeIds()
{
	std::vector<int32> ids;
	for (int32 id = 0; id < 256; id++)
		if (GobeResources::File().Find('POLY', id) != NULL)
			ids.push_back(id);
	BPath folder = UserShapesFolder(false);
	for (int32 n = 0; n < 26 && folder.InitCheck() == B_OK; n++) {
		BPath file(folder);
		file.Append(std::to_string(n).c_str());
		BEntry entry(file.Path());
		if (entry.Exists())
			ids.push_back(kUserShapes + n);
	}
	return ids;
}


bool
TToolBar::ShapePath(int32 id, BezierPath& path)
{
	if (id < kUserShapes) {
		const ResourceFile::Resource* r = GobeResources::File().Find('POLY', id);
		return r != NULL && ParseToolbarShape(r->data, path);
	}
	BPath file = UserShapesFolder(false);
	file.Append(std::to_string(id - kUserShapes).c_str());
	BFile in(file.Path(), B_READ_ONLY);
	off_t size = 0;
	if (in.InitCheck() != B_OK || in.GetSize(&size) != B_OK || size <= 0 || size > 1 << 20)
		return false;
	std::vector<uint8_t> data(static_cast<size_t>(size));
	if (in.Read(data.data(), data.size()) != ssize_t(data.size()))
		return false;
	return ParseToolbarShape(data, path);
}


// TToolbarShapes::AddShape: the first free number of 26, written as POLY
int32
TToolBar::AddShape(const BezierPath& path)
{
	BPath folder = UserShapesFolder(true);
	if (folder.InitCheck() != B_OK)
		return -1;
	for (int32 n = 0; n < 26; n++) {
		BPath file(folder);
		file.Append(std::to_string(n).c_str());
		if (BEntry(file.Path()).Exists())
			continue;
		std::vector<uint8_t> data = WriteToolbarShape(path);
		BFile out(file.Path(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
		if (out.InitCheck() != B_OK || out.Write(data.data(), data.size()) != ssize_t(data.size()))
			return -1;
		return kUserShapes + n;
	}
	return -1;
}

} // namespace gobe
