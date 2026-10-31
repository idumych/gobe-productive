// The ToolBar (TToolBar: Window > Show ToolBar, 'sMF7'): a column of tool
// buttons beside the document (TheTools: the arrow, part frames, the
// rectangle, ellipse, round rectangle, arc, line, freehand, Bezier and
// polygon tools, the toolbar shapes; MICN icons ToolIcons, tips STR# 4,
// 12-22), 27 points apart, 6 more after the part tool. Choosing one sends
// the window 'TOOL' with "tool"; the tool chosen stays selected. The part,
// Bezier and toolbar shapes tools aren't ported (off).
#ifndef GOBE_TTOOLBAR_H
#define GOBE_TTOOLBAR_H

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

private:
	TPageView* fView;
};

} // namespace gobe

#endif
