// The Spreadsheet part's dialogs.
#ifndef GOBE_TSHEETDIALOGS_H
#define GOBE_TSHEETDIALOGS_H

#include <string>

#include "NumberFormats.h"
#include "TDialog.h"

class BListView;

namespace gobe {

// the application's number formats (GetNumFormatFactory), loaded from the
// original's resources on first use
const NumberFormats& AppNumberFormats();

// Format > Number… (TDialogNumFormat; DIAL and Item 82 of the application):
// the categories and their types, the decimal places, the thousands
// separator and a sample (the active cell's number, else -1234.1234). OK
// sends `target` 'sM60' with "format", the chosen format string.
class TDialogNumFormat : public TDialog {
public:
	// `format`: the selection's number format ("" none)
	TDialogNumFormat(BHandler* target, const std::string& format, double sample);

protected:
	virtual void Init();
	virtual bool ItemHit(int32 index, BMessage* message);

private:
	BListView* List(int32 index) const;
	void SelectCategory(int32 category);
	void BuildTypesList(int32 category);
	void UpdateGroup();
	void UpdateSample();
	void SetHelpText(int32 category);
	bool CheckDecimals();

	BHandler* fTarget;
	std::string fFormat;
	double fSample;
	// the chosen format group (field_0x50): category -1 for none
	NumberFormatGroupInfo fGroup;
	int32 fDecimals = 2;		// field_0x5c: the last valid decimal places
};

// Format > Display Options… (TDialogDisplayOptions; TSSDialog 23 of
// Parts/Spreadsheet): the six display options as check boxes (items 2-7:
// grid, locked cells shaded, formulas, column headings, row headings,
// circular references marked). OK sends `target` 'sM66' with
// "displayOptions", six bytes.
class TDialogDisplayOptions : public TDialog {
public:
	TDialogDisplayOptions(BHandler* target, const bool options[6]);

protected:
	virtual void Init();
	virtual bool ItemHit(int32 index, BMessage* message);

private:
	BHandler* fTarget;
	bool fOptions[6];
};

} // namespace gobe

#endif
