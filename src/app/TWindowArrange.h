// The Window menu's arrangements of the document windows (TApp::
// CascadeWindows, TileWindows, RotateWindows), done by the application.
#ifndef GOBE_TWINDOWARRANGE_H
#define GOBE_TWINDOWARRANGE_H

#include <Rect.h>

namespace gobe {

// GetUsableScreenRect: the screen less the Deskbar, inset by 5
BRect UsableScreenRect();
// TApp::CalcFirstWindowFrame: at most 640 wide, 18 below the top
BRect FirstWindowFrame();

void CascadeWindows();
// `rows` and `columns` both or neither: a grid; else in a column of rows,
// or a row of columns
void TileWindows(bool rows, bool columns);
// the next document window comes to the front
void RotateWindows();

} // namespace gobe

#endif
