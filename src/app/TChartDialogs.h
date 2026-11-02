// The Chart part's dialogs (its resources): Chart Type (DIAL/Item 1000,
// TDialogChart) and Axis Options (DIAL/Item 1001, TAxisDialog). They send
// the document window the type chosen, the axes they were given as
// changed.
#ifndef GOBE_TCHART_DIALOGS_H
#define GOBE_TCHART_DIALOGS_H

#include <string>
#include <vector>

#include "ChartPart.h"
#include "TDialog.h"

namespace gobe {

// TDialogChart: the major types (the Chart resource 'chrt' 1: a name, its
// MICN, its minor types' descriptions and ICONs) in a list, the chosen
// one's minor types as icon buttons, the minor type's description; OK sends
// 'sCH1' with the type as "ChartType" (two int32: major, minor)
class TDialogChart : public TDialog {
public:
	TDialogChart(BLooper* window, uint32 major, uint32 minor);

protected:
	virtual void Init();
	virtual bool ItemHit(int32 index, BMessage* message);

private:
	struct MinorType {
		std::string description;
		int32 icon = 0;
	};
	struct MajorType {
		std::string name;
		int32 icon = 0;
		int32 count = 0;
		MinorType minors[7];
	};

	// TDialogChart::loadChartData, switchChartType, switchMinorChartType
	void LoadChartData();
	void SwitchChartType(int32 major, int32 minor);
	void SwitchMinorChartType(int32 minor);

	BLooper* fWindow;
	std::vector<MajorType> fTypes;
	int32 fMajor;
	int32 fMinor;
};

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
