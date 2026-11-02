// TChartRibbon: the chart part's ribbon (TChartPart::ActivePartRibbon)
// under the PartBar while a chart is entered. Its sub-ribbon for 2D charts
// (TSubRibbon2DTools): a separator along its bottom and a field telling how
// to get at a series' options (the Chart part's STR# 1, 12).
#ifndef GOBE_TCHART_RIBBON_H
#define GOBE_TCHART_RIBBON_H

#include "TRibbon.h"

namespace gobe {

class TChartRibbon : public TRibbon {
public:
	TChartRibbon();
};

} // namespace gobe

#endif
