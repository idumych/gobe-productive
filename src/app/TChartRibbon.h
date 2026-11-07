// TChartRibbon: the chart part's ribbon (TChartPart::ActivePartRibbon)
// under the PartBar while a chart is entered. Its sub-ribbons: for 2D charts
// (TSubRibbon2DTools) a separator along its bottom and a field telling how
// to get at a series' options (the Chart part's STR# 1, 12); for 3D charts
// (TSubRibbon3DTools) the camera's tools, one selected.
#ifndef GOBE_TCHART_RIBBON_H
#define GOBE_TCHART_RIBBON_H

#include "TRibbon.h"

namespace gobe {

class TChartRibbon : public TRibbon {
public:
	TChartRibbon();

	// TChartRibbon::SelectSubRibbon: 0 the 2D one, 1 the 3D one
	// (TSubRibbon3DTools::LoadAllElements: its third tool selected)
	void SelectSubRibbon(int32 subRibbon);
	// TSubRibbon::SelectedElement('BUTT'): the tool selected, -1 for none
	int32 SelectedTool() const;

private:
	int32 fSubRibbon = -1;
	TRibbonElement* fTools[4] = {};
};

} // namespace gobe

#endif
