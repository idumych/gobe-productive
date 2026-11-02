#include "TChartRibbon.h"

#include "GobeResources.h"

namespace gobe {

// TChartRibbon::TChartRibbon, TSubRibbon2DTools::TSubRibbon2DTools
TChartRibbon::TChartRibbon()
	:
	TRibbon("ChartRibbon")
{
	AddElement(new TSeparatorElement('BSep'));
	AddElement(new TTextIndicatorElement(0, BRect(2, 2, 302, 21),
		IndexString(GobeResources::Part("Chart"), 1, 12)));
}

} // namespace gobe
