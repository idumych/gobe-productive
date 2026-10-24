#include "TWindowArrange.h"

#include <Application.h>
#include <Deskbar.h>
#include <Screen.h>

#include <cmath>
#include <vector>

#include "TDocWindow.h"

namespace gobe {

namespace {

// TApp::FrontDocWindow first, then the others (TApp::DocWindowAt)
std::vector<BWindow*>
DocWindows()
{
	std::vector<BWindow*> windows;
	BWindow* front = NULL;
	for (int32 i = 0; BWindow* window = be_app->WindowAt(i); i++) {
		if (dynamic_cast<TDocWindow*>(window) == NULL)
			continue;
		if (front == NULL && window->IsActive())
			front = window;
		else
			windows.push_back(window);
	}
	if (front != NULL)
		windows.insert(windows.begin(), front);
	return windows;
}


// BWindow::Zoom(origin, width, height)
void
Place(BWindow* window, BPoint origin, float width, float height)
{
	if (window->Lock()) {
		window->MoveTo(origin);
		window->ResizeTo(width, height);
		window->Unlock();
	}
}

} // namespace


BRect
UsableScreenRect()
{
	BRect screen = BScreen().Frame();
	BRect deskbar = BDeskbar().Frame();
	if (screen.Width() == deskbar.Width()) {
		if (screen.top == deskbar.top)
			screen.top = deskbar.bottom + 2;
		if (screen.bottom == deskbar.bottom)
			screen.bottom = deskbar.top - 2;
	} else if (screen.left == deskbar.left)
		screen.left = deskbar.right + 2;
	else if (screen.right == deskbar.right)
		screen.right = deskbar.left - 2;
	screen.InsetBy(5, 5);
	return screen;
}


BRect
FirstWindowFrame()
{
	BRect usable = UsableScreenRect();
	return BRect(usable.left, usable.top + 18, std::min(usable.right, usable.left + 640),
		usable.bottom);
}


// 80% wide and 60% high, 15 apart, the front one last; back to the left
// (15 further right each time) when they would leave the screen
void
CascadeWindows()
{
	BRect usable = UsableScreenRect();
	BRect first = FirstWindowFrame();
	BRect frame(first.left, first.top,
		first.left + roundf((first.right - first.left) * 0.8f),
		first.top + roundf((first.bottom - first.top) * 0.6f));
	std::vector<BWindow*> windows = DocWindows();
	int column = 0;
	for (int32 i = int32(windows.size()) - 1; i >= 0; i--) {
		Place(windows[i], frame.LeftTop(), frame.Width(), frame.Height());
		frame.OffsetBy(15, 15);
		if (!usable.Contains(frame)) {
			frame.OffsetTo(first.LeftTop());
			column++;
			frame.OffsetBy(column * 15.0f, 0);
			if (!usable.Contains(frame)) {
				frame.OffsetTo(first.LeftTop());
				column = 0;
			}
		}
	}
}


void
TileWindows(bool rows, bool columns)
{
	BRect usable = UsableScreenRect();
	BRect first = FirstWindowFrame();
	std::vector<BWindow*> windows = DocWindows();
	int32 count = int32(windows.size());
	if (count == 0)
		return;
	float width = usable.right - first.left, height = usable.bottom - first.top;
	float stepX = 0, stepY = 0, windowWidth, windowHeight;
	int32 down = 1;
	if (rows != columns) {
		if (columns) {
			stepX = (width - 1 + 8) / count;
			windowWidth = stepX - 8;
			windowHeight = height;
		} else {
			stepY = (height + 26 + 2) / count;
			windowWidth = width - 2;
			windowHeight = stepY - 26;
		}
	} else {
		// the fewest rows, then columns, that hold them all
		int32 across = 1;
		for (down = 1; ; down++) {
			for (across = 1; across <= down; across++)
				if (count <= across * down)
					break;
			if (across <= down)
				break;
		}
		stepX = (width - 1 + 8) / across;
		windowWidth = stepX - 8;
		stepY = (height + 26 + 2) / down;
		windowHeight = stepY - 26;
	}
	BPoint where = first.LeftTop();
	for (int32 i = count - 1; i >= 0; i--) {
		if (rows == columns)
			where = first.LeftTop() + BPoint(stepX * (i / down), stepY * (i % down));
		Place(windows[i], where, windowWidth, windowHeight - 2);
		if (rows != columns)
			where += BPoint(stepX, stepY);
	}
}


// (the port takes the window after the active one: Haiku's list of
// windows isn't in front-to-back order)
void
RotateWindows()
{
	std::vector<BWindow*> windows;
	int32 active = -1;
	for (int32 i = 0; BWindow* window = be_app->WindowAt(i); i++) {
		if (dynamic_cast<TDocWindow*>(window) == NULL)
			continue;
		if (window->IsActive())
			active = int32(windows.size());
		windows.push_back(window);
	}
	for (size_t n = 1; n <= windows.size(); n++) {
		BWindow* window = windows[(active + n) % windows.size()];
		if (window->Lock()) {
			bool minimized = window->IsMinimized();
			if (!minimized)
				window->Activate(true);
			window->Unlock();
			if (!minimized)
				return;
		}
	}
}

} // namespace gobe
