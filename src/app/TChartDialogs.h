// The Chart part's dialogs (its resources): Axis Options (DIAL/Item 1001,
// TAxisDialog). It sends the document window the axes it was given, as
// changed.
#ifndef GOBE_TCHART_DIALOGS_H
#define GOBE_TCHART_DIALOGS_H

#include "ChartPart.h"
#include "TDialog.h"

namespace gobe {

// TAxisDialog: the chart's axes (the vertical first: T2DXYAxisChart::
// AxisPtr), Show Gridline and the range (Minimum, Maximum, Step; empty for
// an axis ranged by its data) of the one the Axis: pop-up shows; OK sends
// 'sCH8' with the axes as "axis" (the port's order: horizontal, vertical)
class TDialogAxis : public TDialog {
public:
	TDialogAxis(BLooper* window, const ChartAxis axes[2]);

protected:
	virtual void Init();
	virtual bool ItemHit(int32 index, BMessage* message);

private:
	// TAxisDialog::Setup, SetRangeCurAxis
	void Setup(int32 index);
	void TakeRange();
	ChartAxis& Axis(int32 index) { return fAxes[index == 0 ? 1 : 0]; }

	BLooper* fWindow;
	ChartAxis fAxes[2];
	int32 fCurrent = 0;
};

} // namespace gobe

#endif
