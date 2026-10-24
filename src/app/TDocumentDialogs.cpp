// Document dialogs: Margins (TDialogFormatDocument).
#include "TDocumentDialogs.h"

#include <stdio.h>

#include <Alert.h>
#include <Looper.h>
#include <Message.h>

#include "Document.h"
#include "GobeResources.h"
#include "Measure.h"

namespace gobe {

static const uint32 kCmdMargins = 'sM22';


TDialogFormatDocument::TDialogFormatDocument(BLooper* window, const Document& document)
	:
	TDialog(20, 20, 'APPL', NULL),
	fWindow(window),
	fUnit(0),
	fMirror(false),
	fShowMargins(true),
	fShowGuides(true)
{
	document.Margins(fMargins);
	for (int i = 0; i < 4; i++)
		fPaper[i] = 0;
	if (!document.sheets.sheets.empty()) {
		const LeafSheet& sheet = document.sheets.sheets[0];
		std::copy(sheet.paper, sheet.paper + 4, fPaper);
		fUnit = int(sheet.unit);
		fShowMargins = sheet.flags[0];
		fShowGuides = sheet.flags[1];
		fMirror = sheet.flags[2];
	}
	for (int32 i = 0; i < 6; i++)
		fUnitNames.push_back(GobeResources::IndexString(7, i).String());
}


void
TDialogFormatDocument::Init()
{
	// the margins in the sheet's unit, with its name, two decimals
	for (int32 i = 0; i < 4; i++)
		SetTextItem(3 + i, MeasureText(fMargins[i], fUnit, kMeasureWithUnit | kMeasureTwoPlaces,
			fUnitNames).c_str());
	SetCheckBoxItem(8, fShowGuides);
	MakeFocusItem(3);
}


// the largest margin a field may have: the paper less the opposite margin
float
TDialogFormatDocument::Maximum(int32 index) const
{
	float width = fPaper[2] - fPaper[0], height = fPaper[3] - fPaper[1];
	switch (index) {
		case 3: return width - fMargins[2];
		case 4: return height - fMargins[3];
		case 5: return width - fMargins[0];
		case 6: return height - fMargins[1];
	}
	return 0;
}


// TDialogFormatDocument::validTextControl: a length from 0 to the maximum;
// otherwise the original's range alert (STR# 6, 15)
bool
TDialogFormatDocument::ValidTextControl(int32 index, float& value)
{
	float maximum = Maximum(index);
	float points;
	if (ParseMeasure(TextItem(index).String(), fUnit, fUnitNames, false, false, points)
			&& points >= 0 && points <= maximum) {
		value = points;
		return true;
	}
	BString text;
	text.SetToFormat(GobeResources::IndexString(6, 15).String(),
		MeasureText(0, fUnit, kMeasureWithUnit | kMeasureTwoPlaces, fUnitNames).c_str(),
		MeasureText(maximum, fUnit, kMeasureWithUnit | kMeasureTwoPlaces, fUnitNames).c_str());
	(new BAlert("Margins", text.String(), "OK"))->Go();
	MakeFocusItem(index);
	return false;
}


bool
TDialogFormatDocument::ItemHit(int32 index, BMessage* message)
{
	switch (index) {
		case 0: {
			for (int32 i = 3; i <= 6; i++)
				if (!ValidTextControl(i, fMargins[i - 3]))
					return true;
			BMessage margins(kCmdMargins);
			margins.AddRect("Margins", BRect(fMargins[0], fMargins[1], fMargins[2],
				fMargins[3]));
			margins.AddBool("MirrorFacingPages", fMirror);
			margins.AddBool("ShowMargins", fShowMargins);
			margins.AddBool("ShowPageGuides", fShowGuides);
			fWindow->PostMessage(&margins);
			PostMessage(B_QUIT_REQUESTED);
			return false;
		}
		case 1:
			PostMessage(B_QUIT_REQUESTED);
			return false;
		case 3:
		case 4:
		case 5:
		case 6:
			return !ValidTextControl(index, fMargins[index - 3]);
		case 8:
			fShowGuides = CheckBoxItem(8);
			return false;
	}
	return false;
}

} // namespace gobe
