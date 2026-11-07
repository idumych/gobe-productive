#include "TImageDialogs.h"

#include <Alert.h>

#include "GobeResources.h"
#include "Measure.h"

namespace gobe {

TDialogBTDocSize::TDialogBTDocSize(BLooper* window, float width, float height)
	:
	TDialog(1, 1, 'BITS', NULL, &GobeResources::Part("ImageProcessing")),
	fWindow(window),
	fSize{width, height}
{
	for (int32 i = 0; i < 6; i++)
		fUnitNames.push_back(GobeResources::IndexString(7, i).String());
}


// TDialogBTDocSize::Init: each as TMeasure(points, 4).GetText(3)
void
TDialogBTDocSize::Init()
{
	for (int32 i = 0; i < 2; i++)
		SetTextItem(2 + i, MeasureText(fSize[i], 4, kMeasureWithUnit | kMeasureTwoPlaces,
			fUnitNames).c_str());
}


bool
TDialogBTDocSize::ItemHit(int32 index, BMessage* message)
{
	if (index == 0) {
		float size[2];
		for (int32 i = 0; i < 2; i++) {
			if (!ParseMeasure(TextItem(2 + i).String(), 4, fUnitNames, false, false, size[i])
					|| size[i] < 1 || size[i] > 10000) {
				(new BAlert("", GobeResources::IndexString(6, 4).String(), "OK"))->Go(NULL);
				MakeFocusItem(2 + i);
				return true;
			}
		}
		BMessage set('sM21');
		set.AddFloat("Width", size[0]);
		set.AddFloat("Height", size[1]);
		fWindow->PostMessage(&set);
	}
	return TDialog::ItemHit(index, message);
}

} // namespace gobe
