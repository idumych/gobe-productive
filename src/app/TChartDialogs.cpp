#include "TChartDialogs.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

#include <ListView.h>
#include <ScrollView.h>
#include <StringView.h>

#include "GobeResources.h"
#include "TIconButton.h"
#include "TIconStringItem.h"

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


// the resource's little-endian int32 at `at`
int32
LittleInt32(const std::vector<uint8_t>& data, size_t at)
{
	if (at + 4 > data.size())
		return 0;
	return int32(uint32(data[at]) | uint32(data[at + 1]) << 8 | uint32(data[at + 2]) << 16
		| uint32(data[at + 3]) << 24);
}


// a NUL-terminated string at `at`, `at` moved past it
std::string
CString(const std::vector<uint8_t>& data, size_t& at)
{
	std::string text;
	while (at < data.size() && data[at] != 0)
		text += char(data[at++]);
	at++;
	return text;
}

} // namespace


TDialogChart::TDialogChart(BLooper* window, uint32 major, uint32 minor)
	:
	TDialog(1000, 1000, 'chrt', NULL, &GobeResources::Part("Chart")),
	fWindow(window),
	fMajor(int32(major)),
	fMinor(int32(minor))
{
}


// the major types as the Chart resource 'chrt' 1 has them: a name, its
// MICN, then seven minor types (a description, its ICON; 0 for none)
void
TDialogChart::LoadChartData()
{
	const ResourceFile::Resource* r = GobeResources::Part("Chart").Find(FourCC("chrt"), 1);
	if (r == NULL)
		return;
	const std::vector<uint8_t>& data = r->data;
	size_t at = 0;
	while (at < data.size()) {
		MajorType type;
		type.name = CString(data, at);
		type.icon = LittleInt32(data, at);
		at += 4;
		for (MinorType& minor : type.minors) {
			minor.description = CString(data, at);
			minor.icon = LittleInt32(data, at);
			at += 4;
			if (minor.icon != 0)
				type.count++;
		}
		fTypes.push_back(type);
	}
}


// TDialogChart::Init: the major types listed with their small icons, 16
// high; the headings bold; the chart's type chosen
void
TDialogChart::Init()
{
	LoadChartData();
	BScrollView* scroll = dynamic_cast<BScrollView*>(Item(3));
	BListView* list = scroll != NULL ? dynamic_cast<BListView*>(scroll->Target()) : NULL;
	if (list == NULL || fTypes.empty())
		return;
	const ResourceFile& resources = GobeResources::Part("Chart");
	for (const MajorType& type : fTypes) {
		const ResourceFile::Resource* r = resources.Find(FourCC("MICN"), type.icon);
		if (r == NULL || r->data.size() < 256)
			continue;
		BBitmap* icon = new BBitmap(BRect(0, 0, 15, 15), B_CMAP8);
		icon->SetBits(r->data.data(), 256, 0, B_CMAP8);
		TIconStringItem* item = new TIconStringItem(type.name, icon, 0);
		list->AddItem(item);
		item->SetHeight(16);
	}
	for (int32 index : {2, 4})
		if (BStringView* text = dynamic_cast<BStringView*>(Item(index)))
			text->SetFont(be_bold_font);
	// (chosen without a message; then one on every choice)
	BMessage* select = new BMessage(*list->InvocationMessage());
	select->AddBool("Select", true);
	list->SetSelectionMessage(NULL);
	if (fMajor < 0 || fMajor >= int32(fTypes.size()))
		fMajor = 0;
	if (fMinor < 0 || fMinor >= fTypes[fMajor].count)
		fMinor = 0;
	list->Select(fMajor, false);
	list->SetSelectionMessage(select);
	SwitchChartType(fMajor, fMinor);
}


// TDialogChart::switchChartType: the major type's minor types' buttons
// shown with their icons, the rest not (the original hides them; here
// they're left in place, disabled, without their icon, which looks the
// same and keeps the layout); its minor type chosen
void
TDialogChart::SwitchChartType(int32 major, int32 minor)
{
	fMajor = major;
	fMinor = minor;
	const MajorType& type = fTypes[fMajor];
	for (int32 i = 0; i < 7; i++) {
		TIconButton* button = dynamic_cast<TIconButton*>(Item(i + 5));
		if (button == NULL)
			continue;
		bool shown = i < type.count;
		button->SetEnabled(shown);
		if (shown)
			button->LoadNewImage(type.minors[i].icon);
		button->SetVisible(shown);
	}
	SwitchMinorChartType(fMinor);
}


// TDialogChart::switchMinorChartType: its button down, its description
void
TDialogChart::SwitchMinorChartType(int32 minor)
{
	fMinor = minor;
	if (TIconButton* button = dynamic_cast<TIconButton*>(Item(minor + 5)))
		button->SetValue(1);
	SetTextItem(12, fTypes[fMajor].minors[fMinor].description.c_str());
}


bool
TDialogChart::ItemHit(int32 index, BMessage* message)
{
	if (fTypes.empty())
		return TDialog::ItemHit(index, message);
	switch (index) {
		case 0: {
			BMessage type('sCH1');
			int32 chosen[2] = {fMajor, fMinor};
			type.AddData("ChartType", B_RAW_TYPE, chosen, sizeof chosen);
			fWindow->PostMessage(&type);
			break;
		}
		case 3: {
			// a major type chosen (none: the one that was, again), its first
			// minor type
			if (!message->GetBool("Select", false))
				return true;
			BScrollView* scroll = dynamic_cast<BScrollView*>(Item(3));
			BListView* list = scroll != NULL ? dynamic_cast<BListView*>(scroll->Target())
				: NULL;
			int32 chosen = list != NULL ? list->CurrentSelection() : -1;
			if (chosen < 0 || chosen >= int32(fTypes.size())) {
				chosen = fMajor;
				if (list != NULL)
					list->Select(chosen, false);
			}
			SwitchChartType(chosen, 0);
			return true;
		}
		case 5:
		case 6:
		case 7:
		case 8:
		case 9:
		case 10:
		case 11:
			SwitchMinorChartType(index - 5);
			return true;
	}
	return TDialog::ItemHit(index, message);
}


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
