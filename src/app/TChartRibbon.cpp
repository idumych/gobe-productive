#include "TChartRibbon.h"

#include "GobeResources.h"

namespace gobe {

// TChartRibbon::TChartRibbon
TChartRibbon::TChartRibbon()
	:
	TRibbon("ChartRibbon")
{
	SelectSubRibbon(0);
}


void
TChartRibbon::SelectSubRibbon(int32 subRibbon)
{
	if (subRibbon == fSubRibbon)
		return;
	fSubRibbon = subRibbon;
	RemoveAllElements();
	for (TRibbonElement*& tool : fTools)
		tool = NULL;
	AddElement(new TSeparatorElement('BSep'));
	const ResourceFile& resources = GobeResources::Part("Chart");
	if (subRibbon == 0) {
		// TSubRibbon2DTools::TSubRibbon2DTools
		AddElement(new TTextIndicatorElement(0, BRect(2, 2, 302, 21),
			IndexString(resources, 1, 12)));
	} else {
		// TSubRibbon3DTools::TSubRibbon3DTools: MICN 400 to 403, 20 apart, their
		// tips the part's STR# 4
		for (int32 i = 0; i < 4; i++) {
			TMICNButtonElement* button = new TMICNButtonElement(uint32(1000 + i),
				BPoint(2 + 20 * i, 2), TMICNElement::LoadIcon(400 + i, &resources), NULL);
			button->tip = IndexString(resources, 4, i);
			button->family = 'BUTT';
			button->selectable = true;
			button->selected = i == 2;
			fTools[i] = button;
			AddElement(button);
		}
	}
	Invalidate();
}


int32
TChartRibbon::SelectedTool() const
{
	for (int32 i = 0; i < 4; i++)
		if (fTools[i] != NULL && fTools[i]->selected)
			return i;
	return -1;
}

} // namespace gobe
