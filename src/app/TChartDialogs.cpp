#include "TChartDialogs.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

#include "GobeResources.h"

namespace gobe {

namespace {

float
AsFloat(uint32_t bits)
{
	float f;
	memcpy(&f, &bits, sizeof f);
	return f;
}


uint32_t
Bits(float f)
{
	uint32_t bits;
	memcpy(&bits, &f, sizeof bits);
	return bits;
}


// TAxis::NiceNum
float
NiceNum(float x, bool round)
{
	if (!(x > 0))
		return 0;
	double exponent = std::floor(std::log10(double(x)));
	double fraction = double(x) / std::pow(10.0, exponent);
	double nice;
	if (round)
		nice = fraction < 1.5 ? 1 : fraction < 3 ? 2 : fraction < 7 ? 5 : 10;
	else
		nice = fraction <= 1 ? 1 : fraction <= 2 ? 2 : fraction <= 5 ? 5 : 10;
	return float(nice * std::pow(10.0, exponent));
}


// TString(float): the number as the dialog shows it
BString
Number(float value)
{
	return BString().SetToFormat("%g", double(value));
}


// TString::Num: the whole text a number, else false
bool
ParseNumber(const BString& text, float& value)
{
	const char* start = text.String();
	char* end = NULL;
	value = strtof(start, &end);
	return end != start && *end == '\0';
}

} // namespace


TDialogAxis::TDialogAxis(BLooper* window, const ChartAxis axes[2])
	:
	TDialog(1001, 1001, 'chrt', NULL, &GobeResources::Part("Chart")),
	fWindow(window)
{
	fAxes[0] = axes[0];
	fAxes[1] = axes[1];
}


void
TDialogAxis::Init()
{
	fCurrent = 0;
	Setup(0);
}


void
TDialogAxis::Setup(int32 index)
{
	const ChartAxis& axis = Axis(index);
	SetCheckBoxItem(3, axis.flags[1]);
	if (axis.flags[0]) {
		// (ranged by its data: nothing shown)
		SetTextItem(4, "");
		SetTextItem(5, "");
		SetTextItem(6, "");
		return;
	}
	// TAxis::GetRange: the range (0 to 0.00001 if it has none) and
	// TickSpacing (the step, or a quarter of the data's nice range)
	float low = AsFloat(axis.values[2]), high = AsFloat(axis.values[3]);
	if (!(low < high)) {
		low = 0;
		high = 1e-05f;
	}
	float step = AsFloat(axis.values[5]);
	if (!(step > 0))
		step = NiceNum(0.25f * NiceNum(AsFloat(axis.values[1]) - AsFloat(axis.values[0]), false),
			true);
	SetTextItem(4, Number(low).String());
	SetTextItem(5, Number(high).String());
	SetTextItem(6, Number(step).String());
}


// TAxisDialog::SetRangeCurAxis: what's typed taken; with none of it the
// axis is ranged by its data again
void
TDialogAxis::TakeRange()
{
	ChartAxis& axis = Axis(fCurrent);
	float low = 0, high = 0, step = 0;
	BString lowText = TextItem(4), highText = TextItem(5), stepText = TextItem(6);
	bool lowSet = lowText.Length() > 0 && ParseNumber(lowText, low);
	bool highSet = highText.Length() > 0 && ParseNumber(highText, high);
	bool stepSet = stepText.Length() > 0 && ParseNumber(stepText, step);
	if (!lowSet || !highSet) {
		float rangeLow = AsFloat(axis.values[2]), rangeHigh = AsFloat(axis.values[3]);
		if (!(rangeLow < rangeHigh)) {
			rangeLow = 0;
			rangeHigh = 1e-05f;
		}
		if (!lowSet)
			low = rangeLow;
		if (!highSet)
			high = rangeHigh;
	}
	if (high < low)
		high = low;
	if (step < 0)
		step = -step;
	axis.flags[0] = !lowSet && !highSet && !stepSet;
	axis.values[2] = Bits(low);
	axis.values[3] = Bits(high);
	axis.values[5] = Bits(stepSet ? step : -1.0f);
}


bool
TDialogAxis::ItemHit(int32 index, BMessage* message)
{
	switch (index) {
		case 0: {
			TakeRange();
			BMessage axes('sCH8');
			axes.AddData("axes", B_RAW_TYPE, fAxes, sizeof fAxes);
			fWindow->PostMessage(&axes);
			break;
		}
		case 2: {
			// the Axis: pop-up (MENU 301: Vertical Axis, Horizontal Axis)
			TakeRange();
			fCurrent = PopUpValue(2);
			Setup(fCurrent);
			return true;
		}
		case 3:
			Axis(fCurrent).flags[1] = CheckBoxItem(3);
			return true;
	}
	return TDialog::ItemHit(index, message);
}

} // namespace gobe
