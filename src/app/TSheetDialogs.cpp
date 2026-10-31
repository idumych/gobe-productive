#include "TSheetDialogs.h"

#include <stdio.h>
#include <stdlib.h>

#include <Alert.h>
#include <RadioButton.h>
#include <ListItem.h>
#include <ListView.h>
#include <ScrollView.h>
#include <StringView.h>
#include <TextView.h>

#include "GobeResources.h"
#include "SSSort.h"
#include "SSEval.h"
#include "SSStyle.h"

namespace gobe {

const NumberFormats&
AppNumberFormats()
{
	static NumberFormats sFormats;
	static bool sLoaded = false;
	if (!sLoaded) {
		std::string error;
		if (!sFormats.Load(GobeResources::File(), error))
			fprintf(stderr, "number formats: %s\n", error.c_str());
		sLoaded = true;
	}
	return sFormats;
}


TDialogNumFormat::TDialogNumFormat(BHandler* target, const std::string& format, double sample)
	:
	TDialog(82, 82, 'APPL', NULL),
	fTarget(target),
	fFormat(format),
	fSample(sample)
{
}


BListView*
TDialogNumFormat::List(int32 index) const
{
	BScrollView* scroll = dynamic_cast<BScrollView*>(Item(index));
	return scroll != NULL ? dynamic_cast<BListView*>(scroll->Target()) : NULL;
}


// TDialogNumFormat::Init: the categories, a type list kept as wide as a
// type, the selection's format chosen
void
TDialogNumFormat::Init()
{
	const NumberFormats& formats = AppNumberFormats();
	// the lists report each selection (the original's "Select")
	for (int32 index : {4, 7}) {
		BListView* list = List(index);
		if (list != NULL && list->InvocationMessage() != NULL) {
			BMessage* selection = new BMessage(*list->InvocationMessage());
			selection->AddBool("Select", true);
			list->SetSelectionMessage(selection);
		}
	}
	if (BListView* categories = List(4)) {
		for (const NumberFormatCategory& category : formats.Categories())
			categories->AddItem(new BStringItem(category.name.c_str()));
	}
	int32 category = 0;
	fGroup = NumberFormatGroupInfo();
	if (!fFormat.empty() && formats.Find(fFormat, fGroup))
		category = fGroup.category;
	SetCheckBoxItem(8, fGroup.category >= 0 && fGroup.commas);
	bool decimals = fGroup.category >= 0
		&& formats.Categories()[fGroup.category].decimals;
	fDecimals = decimals ? fGroup.decimals : 2;
	BString text;
	text << fDecimals;
	SetTextItem(5, text.String());
	if (BListView* categories = List(4)) {
		categories->Select(category);
		categories->ScrollToSelection();
	}
}


// TDialogNumFormat::SelectCategory
void
TDialogNumFormat::SelectCategory(int32 category)
{
	const NumberFormats& formats = AppNumberFormats();
	if (category < 0 || category >= int32(formats.Categories().size()))
		return;
	const NumberFormatCategory& c = formats.Categories()[category];
	if (fGroup.category >= 0 && fGroup.category != category)
		fGroup.category = -1;
	BuildTypesList(category);
	SetHelpText(category);
	UpdateGroup();
	SetItemEnabled(8, c.commas);
	SetItemEnabled(5, c.decimals);
	SetCheckBoxItem(8, fGroup.category >= 0 && fGroup.commas);
	if (fGroup.category >= 0 && c.decimals) {
		BString text;
		text << fGroup.decimals;
		SetTextItem(5, text.String());
	}
}


// TDialogNumFormat::BuildTypesList: each type as its format string with the
// decimal places and separator asked for; the chosen group's selected
void
TDialogNumFormat::BuildTypesList(int32 category)
{
	const NumberFormats& formats = AppNumberFormats();
	BListView* types = List(7);
	if (types == NULL || category < 0 || category >= int32(formats.Categories().size()))
		return;
	const NumberFormatCategory& c = formats.Categories()[category];
	int32 decimals = atoi(TextItem(5).String());
	if (decimals > 30)
		decimals = 30;
	if (decimals < 0 || !c.decimals)
		decimals = 0;
	bool commas = CheckBoxItem(8) && c.commas;
	types->DeselectAll();
	while (BListItem* item = types->RemoveItem(int32(0)))
		delete item;
	for (int32 type = 0; type < int32(c.groups.size()); type++) {
		std::string text = formats.FormatString(category, type, decimals, commas);
		types->AddItem(new BStringItem(text.c_str()));
	}
	int32 selected = fGroup.category >= 0 ? fGroup.type : -1;
	if (fGroup.category == 0)
		selected = 0;
	if (selected < 0 && types->CountItems() > 0)
		selected = 0;
	if (selected >= 0)
		types->Select(selected);
	types->ScrollToSelection();
}


// TDialogNumFormat::UpdateGroup: the group of the selected category and type
// with the decimal places and separator (as the category allows them)
void
TDialogNumFormat::UpdateGroup()
{
	const NumberFormats& formats = AppNumberFormats();
	BListView* categories = List(4);
	int32 category = categories != NULL ? categories->CurrentSelection() : -1;
	if (category < 0 || category >= int32(formats.Categories().size()))
		fGroup.category = -1;
	else {
		const NumberFormatCategory& c = formats.Categories()[category];
		int32 decimals = atoi(TextItem(5).String());
		BListView* types = List(7);
		int32 type = types != NULL ? types->CurrentSelection() : 0;
		if (type < 0)
			type = 0;
		if (decimals > 30)
			decimals = 30;
		if (decimals < 0 || !c.decimals)
			decimals = 0;
		fGroup.category = category;
		fGroup.type = type;
		fGroup.decimals = decimals;
		fGroup.commas = CheckBoxItem(8) && c.commas;
	}
	UpdateSample();
}


// TDialogNumFormat::UpdateSample: the sample number in the group's format,
// red if it's negative and the format says so
void
TDialogNumFormat::UpdateSample()
{
	std::string text;
	bool red = false;
	if (fGroup.category >= 0) {
		std::string format = AppNumberFormats().FormatString(fGroup.category, fGroup.type,
			fGroup.decimals, fGroup.commas);
		text = FormatCellValue(SSValue::Number(fSample), format, &red);
		red = red && fSample < 0;
	}
	SetTextItem(12, text.c_str());
	if (BStringView* sample = dynamic_cast<BStringView*>(Item(12))) {
		sample->SetHighColor(red ? (rgb_color){255, 0, 0, 255} : (rgb_color){0, 0, 0, 255});
		sample->Invalidate();
	}
}


// TDialogNumFormat::SetHelpText: the category's description
void
TDialogNumFormat::SetHelpText(int32 category)
{
	const NumberFormats& formats = AppNumberFormats();
	std::string help;
	if (category >= 0 && category < int32(formats.Categories().size()))
		help = formats.Categories()[category].help;
	if (BScrollView* scroll = dynamic_cast<BScrollView*>(Item(10))) {
		if (BTextView* text = dynamic_cast<BTextView*>(scroll->Target()))
			text->SetText(help.c_str());
	}
}


// TDialogNumFormat::CheckDecimals: a number from 0 to 8 (STR# 6, 4 if it
// isn't a number, 13 with the range if it's outside it); else back to the
// last good one
bool
TDialogNumFormat::CheckDecimals()
{
	BString text = TextItem(5);
	char* end = NULL;
	long value = strtol(text.String(), &end, 10);
	bool number = text.Length() > 0 && end != NULL && *end == '\0';
	BString message;
	if (!number)
		message = GobeResources::IndexString(6, 4);
	else if (value < 0 || value > 8) {
		char buffer[256];
		snprintf(buffer, sizeof(buffer), GobeResources::IndexString(6, 13).String(), 0, 8);
		message = buffer;
	}
	if (message.Length() > 0) {
		(new BAlert("", message.String(), "OK"))->Go(NULL);
		BString last;
		last << fDecimals;
		SetTextItem(5, last.String());
		return false;
	}
	fDecimals = int32(value);
	return true;
}


// TDialogNumFormat::ItemHit
bool
TDialogNumFormat::ItemHit(int32 index, BMessage* message)
{
	BListView* categories = List(4);
	int32 selected = categories != NULL ? categories->CurrentSelection() : -1;
	int32 category = selected < 0 ? 0 : selected;
	switch (index) {
		case 0: {
			UpdateGroup();
			BMessage apply('sM60');
			std::string format = fGroup.category >= 0
				? AppNumberFormats().FormatString(fGroup.category, fGroup.type,
					fGroup.decimals, fGroup.commas)
				: std::string();
			apply.AddString("format", format.c_str());
			if (fTarget != NULL && fTarget->Looper() != NULL)
				fTarget->Looper()->PostMessage(&apply, fTarget);
			PostMessage(B_QUIT_REQUESTED);
			return false;
		}
		case 1:
			PostMessage(B_QUIT_REQUESTED);
			return false;
		case 4:
			if (message->GetBool("Select", false) && selected >= 0)
				SelectCategory(selected);
			return false;
		case 5:
			if (!CheckDecimals())
				return true;
			UpdateGroup();
			BuildTypesList(category);
			return false;
		case 8:
			UpdateGroup();
			BuildTypesList(category);
			return false;
		case 7:
			UpdateGroup();
			return false;
	}
	return false;
}

TDialogDisplayOptions::TDialogDisplayOptions(BHandler* target, const bool options[6])
	:
	TDialog(23, 23, 'SPRD', NULL, &GobeResources::Part("Spreadsheet")),
	fTarget(target)
{
	for (int i = 0; i < 6; i++)
		fOptions[i] = options[i];
}


void
TDialogDisplayOptions::Init()
{
	for (int i = 0; i < 6; i++)
		SetCheckBoxItem(2 + i, fOptions[i]);
}


bool
TDialogDisplayOptions::ItemHit(int32 index, BMessage* message)
{
	if (index == 0) {
		uint8 options[6];
		for (int i = 0; i < 6; i++)
			options[i] = CheckBoxItem(2 + i) ? 1 : 0;
		BMessage apply('sM66');
		apply.AddData("displayOptions", B_RAW_TYPE, options, sizeof(options));
		if (fTarget != NULL && fTarget->Looper() != NULL)
			fTarget->Looper()->PostMessage(&apply, fTarget);
	} else if (index != 1)
		return false;
	PostMessage(B_QUIT_REQUESTED);
	return false;
}

TDialogRowColSize::TDialogRowColSize(BHandler* target, bool columns, float size,
		float defaultSize)
	:
	TDialog(5, 5, 'SPRD', NULL, &GobeResources::Part("Spreadsheet")),
	fTarget(target),
	fColumns(columns),
	fSize(size),
	fDefault(defaultSize)
{
}


static BString
SizeText(float size)
{
	// (TDialog::SetTextItem with a float: no decimals when there are none)
	BString text;
	if (size == float(int32(size)))
		text << int32(size);
	else
		text << size;
	return text;
}


void
TDialogRowColSize::Init()
{
	if (!fColumns) {
		std::string label = IndexString(GobeResources::Part("Spreadsheet"), 1506, 0);
		SetLabelItem(2, label.c_str());
		if (!label.empty())
			label.erase(label.size() - 1);
		SetTitle(label.c_str());
	}
	SetTextItem(2, SizeText(fSize).String());
	MakeFocusItem(2);
}


bool
TDialogRowColSize::ItemHit(int32 index, BMessage* message)
{
	switch (index) {
		case 0: {
			BString text = TextItem(2);
			char* end = NULL;
			float size = strtof(text.String(), &end);
			if (text.Length() == 0 || end == NULL || *end != '\0' || size < 0
					|| size > 512) {
				char buffer[256];
				snprintf(buffer, sizeof(buffer),
					GobeResources::IndexString(6, 13).String(), 0, 512);
				(new BAlert("", buffer, "OK"))->Go(NULL);
				MakeFocusItem(2);
				return true;
			}
			fSize = size;
			BMessage apply(fColumns ? 'sM64' : 'sM65');
			apply.AddFloat("value", fSize);
			if (fTarget != NULL && fTarget->Looper() != NULL)
				fTarget->Looper()->PostMessage(&apply, fTarget);
			break;
		}
		case 1:
			break;
		case 3:
			SetTextItem(2, SizeText(fDefault).String());
			MakeFocusItem(2);
			return false;
		default:
			return false;
	}
	PostMessage(B_QUIT_REQUESTED);
	return false;
}

TDialogGotoMove::TDialogGotoMove(BHandler* target, bool goTo, const std::string& cell,
		std::function<bool(const std::string&, uint16&, uint16&)> parse)
	:
	TDialog(21, 21, 'SPRD', NULL, &GobeResources::Part("Spreadsheet")),
	fTarget(target),
	fGoTo(goTo),
	fCell(cell),
	fParse(parse)
{
}


void
TDialogGotoMove::Init()
{
	if (!fGoTo) {
		const ResourceFile& resources = GobeResources::Part("Spreadsheet");
		SetTitle(IndexString(resources, 1501, 6).c_str());
		SetLabelItem(2, IndexString(resources, 1506, 1).c_str());
	}
	SetTextItem(2, fCell.c_str());
	MakeFocusItem(2);
}


bool
TDialogGotoMove::ItemHit(int32 index, BMessage* message)
{
	if (index == 1) {
		PostMessage(B_QUIT_REQUESTED);
		return false;
	}
	if (index != 0)
		return false;
	uint16 row = 0, column = 0;
	if (!fParse || !fParse(TextItem(2).String(), row, column)) {
		(new BAlert("", GobeResources::IndexString(6, 4).String(), "OK"))->Go(NULL);
		MakeFocusItem(2);
		return true;
	}
	BMessage apply(fGoTo ? 'sM77' : 'sM76');
	uint16 cell[2] = {row, column};
	apply.AddData("cellID", B_RAW_TYPE, cell, sizeof(cell));
	if (fTarget != NULL && fTarget->Looper() != NULL)
		fTarget->Looper()->PostMessage(&apply, fTarget);
	PostMessage(B_QUIT_REQUESTED);
	return false;
}


TDialogDeleteInsert::TDialogDeleteInsert(BHandler* target, bool remove, bool vertical)
	:
	TDialog(22, 22, 'SPRD', NULL, &GobeResources::Part("Spreadsheet")),
	fTarget(target),
	fDelete(remove),
	fVertical(vertical)
{
}


void
TDialogDeleteInsert::Init()
{
	SetRadioItem(fVertical ? 2 : 3, true);
	if (!fDelete) {
		const ResourceFile& resources = GobeResources::Part("Spreadsheet");
		SetTitle(IndexString(resources, 1506, 4).c_str());
		SetLabelItem(2, IndexString(resources, 1506, 2).c_str());
		SetLabelItem(3, IndexString(resources, 1506, 3).c_str());
	}
}


bool
TDialogDeleteInsert::ItemHit(int32 index, BMessage* message)
{
	if (index == 0) {
		BRadioButton* radio = dynamic_cast<BRadioButton*>(Item(2));
		BMessage apply(fDelete ? 'sM74' : 'sM73');
		apply.AddBool("shiftVertical", radio != NULL && radio->Value() == B_CONTROL_ON);
		if (fTarget != NULL && fTarget->Looper() != NULL)
			fTarget->Looper()->PostMessage(&apply, fTarget);
	} else if (index != 1)
		return false;
	PostMessage(B_QUIT_REQUESTED);
	return false;
}


TDialogSort::TDialogSort(BHandler* target, const std::vector<SSRange>& ranges)
	:
	TDialog(25, 25, 'SPRD', NULL, &GobeResources::Part("Spreadsheet")),
	fTarget(target),
	fRanges(ranges),
	fVertical(true)
{
	const SSRange& first = ranges.front();
	fHeight = first.to.a - first.from.a + 1;
	fWidth = first.to.b - first.from.b + 1;
	for (size_t i = 1; i < ranges.size(); i++) {
		if (ranges[i].to.a - ranges[i].from.a + 1 != fHeight) {
			fVertical = false;
			fHeight = -1;
		}
		if (ranges[i].to.b - ranges[i].from.b + 1 != fWidth) {
			fWidth = -1;
			fVertical = true;
		}
	}
	if (fHeight < 2)
		fVertical = false;
	else if (fWidth < 2)
		fVertical = true;
	// the first key: the first range's first column (row)
	fKey = fVertical ? SSRange{first.from, {first.to.a, first.from.b}}
		: SSRange{first.from, {first.from.a, first.to.b}};
}


void
TDialogSort::Init()
{
	SetRadioItem(4, fVertical);
	SetRadioItem(5, !fVertical);
	for (int32 key = 0; key < 3; key++)
		SetRadioItem(8 + key * 4, true);
	SetItemEnabled(4, fHeight > 1);
	SetItemEnabled(5, fWidth > 1);
	SetTextItem(3, RangeListName(fRanges).c_str());
	SetTextItem(7, RangeName(fKey).c_str());
}


bool
TDialogSort::ItemHit(int32 index, BMessage* message)
{
	if (index == 1) {
		PostMessage(B_QUIT_REQUESTED);
		return false;
	}
	if (index != 0)
		return false;
	const ResourceFile& resources = GobeResources::Part("Spreadsheet");
	auto alert = [&](int32 which, int32 focus) {
		(new BAlert("", IndexString(resources, 1503, which).c_str(), "OK"))->Go(NULL);
		MakeFocusItem(focus);
		return true;
	};
	BRadioButton* vertical = dynamic_cast<BRadioButton*>(Item(4));
	bool down = vertical != NULL && vertical->Value() == B_CONTROL_ON;
	BMessage sort('sM75');
	sort.AddString("SortRanges", RangeListName(fRanges).c_str());
	for (int32 key = 0; key < 3; key++) {
		BString text = TextItem(7 + key * 4);
		if (text.Length() == 0) {
			if (key == 0)
				return alert(6, 7);
			continue;
		}
		SSRange range;
		if (!ParseRangeName(text.String(), range))
			return alert(7, 7 + key * 4);
		if (down ? int(uint16(range.to.a - range.from.a + 1)) != fHeight
				: int(uint16(range.to.b - range.from.b + 1)) != fWidth)
			return alert(down ? 8 : 9, 7 + key * 4);
		BString name;
		name.SetToFormat("Key %d", int(key + 1));
		sort.AddString(name.String(), RangeName(range).c_str());
		BRadioButton* ascending = dynamic_cast<BRadioButton*>(Item(8 + key * 4));
		name << "ascending";
		sort.AddBool(name.String(), ascending != NULL && ascending->Value() == B_CONTROL_ON);
	}
	sort.AddBool("vertical", down);
	if (fTarget != NULL && fTarget->Looper() != NULL)
		fTarget->Looper()->PostMessage(&sort, fTarget);
	PostMessage(B_QUIT_REQUESTED);
	return false;
}

} // namespace gobe
