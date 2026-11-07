#include "TImageDialogs.h"

#include <Alert.h>
#include <Screen.h>
#include <Slider.h>

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

namespace {

// TPartIMFactory's tools with settings, and their Title's STR# (index 0)
enum { kMagicWand = 2, kFindColor = 4, kBucket = 8, kAirbrush = 9 };


int32
TitleStrings(int32 tool)
{
	switch (tool) {
		case kMagicWand: return 107;
		case kFindColor: return 109;
		case kBucket: return 105;
		case kAirbrush: return 102;
	}
	return -1;
}

} // namespace


ImageToolConfig&
ToolConfig(int32 tool)
{
	static ImageToolConfig sConfigs[10];
	return sConfigs[tool >= 0 && tool < 10 ? tool : 0];
}


TDialogConfigTool::TDialogConfigTool(int32 tool)
	:
	TDialog(4, 4, 'BITS', NULL, &GobeResources::Part("ImageProcessing")),
	fTool(tool),
	fSaved(ToolConfig(tool))
{
}


bool
TDialogConfigTool::HasSettings(int32 tool)
{
	return TitleStrings(tool) >= 0;
}


void
TDialogConfigTool::Init()
{
	SetTitle(IndexString(GobeResources::Part("ImageProcessing"), TitleStrings(fTool), 0).c_str());
	const ImageToolConfig& config = ToolConfig(fTool);
	LoadItemSet(2, fTool == kAirbrush ? 6 : 5);
	if (fTool == kAirbrush) {
		// (AirbrushConfigView: "x_radius", "flow_rate", "spacing", "focus")
		const int32 values[4] = {config.airbrush.xRadius, config.airbrush.flowRate,
			config.airbrush.spacing, config.airbrush.focus};
		for (int32 i = 0; i < 4; i++)
			if (BSlider* slider = dynamic_cast<BSlider*>(Item(i, 2))) {
				slider->SetValue(values[i]);
				slider->SetStyle(B_TRIANGLE_THUMB);
			}
	} else if (BSlider* slider = dynamic_cast<BSlider*>(Item(0, 2))) {
		// (ToleranceConfigView: FindTolerance's distance back to percent,
		// cut off)
		float distance = config.tolerance * 722.0f / 100.0f;
		slider->SetValue(int32((long double)100.0 * ((long double)distance / 722.0L)));
		slider->SetStyle(B_TRIANGLE_THUMB);
	}
}


bool
TDialogConfigTool::ItemHit(int32 index, BMessage* message)
{
	if (index == 2) {
		int32 control = message->GetInt32("SubID", -1);
		BSlider* slider = dynamic_cast<BSlider*>(Item(control, 2));
		if (slider == NULL)
			return true;
		int32 value = slider->Value();
		ImageToolConfig& config = ToolConfig(fTool);
		if (fTool != kAirbrush)
			config.tolerance = float(value);
		else if (control == 0) {
			config.airbrush.xRadius = value;
			config.airbrush.yRadius = value;
		} else if (control == 1)
			config.airbrush.flowRate = value;
		else if (control == 2)
			config.airbrush.spacing = value;
		else if (control == 3)
			config.airbrush.focus = value;
		return true;
	}
	if (index == 1)
		ToolConfig(fTool) = fSaved;
	return TDialog::ItemHit(index, message);
}


// TDialogConfigTool::InitPostRecalc: its top left at the pointer, on the
// screen
void
TDialogConfigTool::PlaceWindow()
{
	BPoint where;
	uint32 buttons;
	if (BView* view = ChildAt(0)) {
		view->GetMouse(&where, &buttons, false);
		view->ConvertToScreen(&where);
	}
	BRect frame = Frame();
	frame.OffsetTo(where);
	BRect screen = BScreen(this).Frame();
	// (TRect::Pin)
	if (frame.left < screen.left)
		frame.OffsetBy(screen.left - frame.left, 0);
	if (frame.right > screen.right)
		frame.OffsetBy(screen.right - frame.right, 0);
	if (frame.top < screen.top)
		frame.OffsetBy(0, screen.top - frame.top);
	if (frame.bottom > screen.bottom)
		frame.OffsetBy(0, screen.bottom - frame.bottom);
	MoveTo(frame.LeftTop());
}

} // namespace gobe
