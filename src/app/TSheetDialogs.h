// The Spreadsheet part's dialogs.
#ifndef GOBE_TSHEETDIALOGS_H
#define GOBE_TSHEETDIALOGS_H

#include <functional>
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

// Format > Column Width… and Row Height… (TDialogRowColSize; TSSDialog 5):
// the size (0-512 points, else STR# 6, 13), Default (item 3) puts the
// default one in. For rows the field's label and the title are STR# 1506, 0
// ("Row Height:"). OK sends `target` 'sM64' (columns) or 'sM65' with the
// float "value".
class TDialogRowColSize : public TDialog {
public:
	TDialogRowColSize(BHandler* target, bool columns, float size, float defaultSize);

protected:
	virtual void Init();
	virtual bool ItemHit(int32 index, BMessage* message);

private:
	BHandler* fTarget;
	bool fColumns;
	float fSize;
	float fDefault;
};

// Options > Go To Cell… and Move Cells… (TDialogGotoMove; TSSDialog 21):
// a cell, the active one's name to begin with. `parse` (TParser::
// ParseReference) gives the single cell inside the sheet a text names, or
// false (STR# 6, 4). OK sends `target` 'sM77' (go to) or 'sM76' (move)
// with "cellID", the cell's row and column. Move Cells' title and label are
// STR# 1501, 6 and STR# 1506, 1.
class TDialogGotoMove : public TDialog {
public:
	TDialogGotoMove(BHandler* target, bool goTo, const std::string& cell,
		std::function<bool(const std::string&, uint16&, uint16&)> parse);

protected:
	virtual void Init();
	virtual bool ItemHit(int32 index, BMessage* message);

private:
	BHandler* fTarget;
	bool fGoTo;
	std::string fCell;
	std::function<bool(const std::string&, uint16&, uint16&)> fParse;
};

// Options > Delete Cells… and Insert Cells… for a selection of cells
// (TDialogDeleteInsert; TSSDialog 22): Shift Cells Up (2) or Left (3); for
// Insert the title and the radios are STR# 1506, 4, 2 and 3 (Down,
// Right). `vertical` is the radio on first (the selection is at least as
// wide as it's high). OK sends `target` 'sM74' (delete) or 'sM73' (insert)
// with "shiftVertical".
class TDialogDeleteInsert : public TDialog {
public:
	TDialogDeleteInsert(BHandler* target, bool remove, bool vertical);

protected:
	virtual void Init();
	virtual bool ItemHit(int32 index, BMessage* message);

private:
	BHandler* fTarget;
	bool fDelete;
	bool fVertical;
};

} // namespace gobe

#endif
