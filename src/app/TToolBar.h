// The ToolBar (TToolBar: Window > Show ToolBar, 'sMF7'): a column of tool
// buttons beside the document (TheTools: the arrow, part frames, the
// rectangle, ellipse, round rectangle, arc, line, freehand, Bezier and
// polygon tools, the toolbar shapes; MICN icons ToolIcons, tips STR# 4,
// 12-22), 27 points apart, 6 more after the part tool. Choosing one sends
// the window 'TOOL' with "tool" ("shape" too for the toolbar shapes, a
// POLY resource chosen in its menu); the tool chosen stays selected. The
// part frames tool isn't ported (off).
#ifndef GOBE_TTOOLBAR_H
#define GOBE_TTOOLBAR_H

#include <vector>

#include "Drawing.h"
#include "TRibbon.h"

namespace gobe {

class TPageView;

class TToolBar : public TRibbon {
public:
	explicit TToolBar(TPageView* view);

	virtual BSize MinSize();
	virtual BSize MaxSize();
	virtual BSize PreferredSize();
	virtual void Draw(BRect updateRect);
	virtual void Update();

	static const uint32 kMsgTool = 'TOOL';

	// TToolbarShapes: the toolbar's shapes, the application's 'POLY'
	// resources (ids below kUserShapes) and the ones added with a path's Add
	// To Tool Bar (kUserShapes + n: the file n in the settings' "Gobe
	// Productive port/Toolbar Shapes", in the POLY format), in that order
	static const int32 kUserShapes = 1000;
	static std::vector<int32> ShapeIds();
	static bool ShapePath(int32 id, BezierPath& path);
	// TToolbarShapes::AddShape: the path saved as the next user shape (26 at
	// most); its id, or -1
	static int32 AddShape(const BezierPath& path);

private:
	TPageView* fView;
};

} // namespace gobe

#endif
