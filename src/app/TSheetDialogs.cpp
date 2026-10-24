#include "TSheetDialogs.h"

#include <stdio.h>
#include <stdlib.h>

#include <Alert.h>
#include <ListItem.h>
#include <ListView.h>
#include <ScrollView.h>
#include <StringView.h>
#include <TextView.h>

#include "GobeResources.h"
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

} // namespace gobe
