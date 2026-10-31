// The Spreadsheet part's dialogs.
#ifndef GOBE_TSHEETDIALOGS_H
#define GOBE_TSHEETDIALOGS_H

#include <functional>
#include <string>
#include <vector>

#include "NumberFormats.h"
#include "TDialog.h"
#include "SSPart.h"

class BListView;

namespace gobe {

struct Document;

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

// the EntryBar's Paste Function (TDialogPasteFunction; the application's
// DIAL 83): an outline list (3) of All Functions (STR# 2, 34) and each
// category (FCAT, the TFunctionFactorys' functions), the selected one's
// help (5: its signature and FUNC help, or the category's description);
// Paste (0, on with a function) puts its signature in the EntryBar
class TDialogPasteFunction : public TDialog {
public:
	TDialogPasteFunction(class TEntryBar* bar);

protected:
	virtual void Init();
	virtual bool ItemHit(int32 index, BMessage* message);

private:
	class TEntryBar* fBar;
};

// Edit > Paste Special… with cells (TDialogSSPasteSpecial; TSSDialog 24):
// Paste Data (2) as Values and Formulas (4) or Values Only (5), Paste
// Formatting (6); OK, on with either box, sends `target` B_PASTE ('PSTE')
// with "data", "formulas" and "formatting".
class TDialogSSPasteSpecial : public TDialog {
public:
	TDialogSSPasteSpecial(BHandler* target);

protected:
	virtual void Init();
	virtual bool ItemHit(int32 index, BMessage* message);

private:
	BHandler* fTarget;
};

// Name Selection… (TDialogSelectionName, a TDialogGenericGetString: DIAL
// 31): the name, STR# 3, 13 its title and (with ":") its label; OK with a
// name no reference has (else STR# 6, 18) and valid (TString::ValidName,
// else STR# 6, 19) sends `target` 'sfN0' with "name"
class TDialogSelectionName : public TDialog {
public:
	TDialogSelectionName(BHandler* target, const Document& doc, const std::string& text);

protected:
	virtual void Init();
	virtual bool ItemHit(int32 index, BMessage* message);

private:
	BHandler* fTarget;
	const Document& fDocument;
	std::string fText;
};

// Edit Names… (TDialogEditNames, DIAL 80): the document's names (list 4),
// the selected one's ranges (text 7, STR# 5, 10 "Undefined" if none), a new
// name (5). Rename (1, when the new name is valid and not taken) sends
// `target` 'sfN4' with "name" and "newName"; Delete (2) 'sfN3' with "name";
// Done (0) closes. The names and their ranges are copied in, so the dialog
// doesn't read the document from its own thread.
class TDialogEditNames : public TDialog {
public:
	TDialogEditNames(BHandler* target, const std::vector<std::string>& names,
		const std::vector<std::string>& descriptions, const char* selected);

protected:
	virtual void Init();
	virtual bool ItemHit(int32 index, BMessage* message);

private:
	BListView* List() const;
	int32 Selected() const;
	void Post(BMessage* message);

	BHandler* fTarget;
	std::vector<std::string> fNames;
	std::vector<std::string> fDescriptions;
	std::string fSelected;
};

// Rename Sheet… (TDialogSheetRename, a TDialogGenericGetString: DIAL 31,
// title STR# 3, 18, label STR# 2, 3): OK with a valid name (TString::
// ValidName, else STR# 6, 19) no other sheet has (TSheetMgr::
// IsValidSheetName, else STR# 6, 16) sends `target` 's00D' with "name"
// and "ActiveTab"
class TDialogSheetRename : public TDialog {
public:
	TDialogSheetRename(BHandler* target, const std::vector<std::string>& sheets, int32 sheet);

protected:
	virtual void Init();
	virtual bool ItemHit(int32 index, BMessage* message);

private:
	BHandler* fTarget;
	std::vector<std::string> fSheets;
	int32 fSheet;
};

// New Sheet… (TDialogSheetNew, DIAL 38): the kinds of part (list 2:
// spreadsheets and word processing can be made so far) and the name (3, "Sheet N" from STR# 5,
// 24); OK with a valid name no sheet has (STR# 6, 19 and 6, 16) sends
// `target` 's00B' with "name" and "kind"
class TDialogSheetNew : public TDialog {
public:
	TDialogSheetNew(BHandler* target, const std::vector<std::string>& sheets);

protected:
	virtual void Init();
	virtual bool ItemHit(int32 index, BMessage* message);

private:
	BHandler* fTarget;
	std::vector<std::string> fSheets;
};

// Options > Fill Special… (TDialogFillSpecial; TSSDialog 26): Apply To
// rows or columns, the type, date units, Step By, Stop At, Trend. In Gobe

// Transparency > Other… in a cell border menu (TDialogGetTransparent, a
// TDialogGenericGetString: DIAL/Item 31, STR# 1506, 5 the title and 6
// the label, the current opacity as a percent): OK checks 0 to 100 (STR#
// 6, 13) and sends `window` `message` with it as the int32 "value".
class TDialogGetTransparent : public TDialog {
public:
	TDialogGetTransparent(BLooper* window, const BMessage& message, int32 percent);

protected:
	virtual void Init();
	virtual bool ItemHit(int32 index, BMessage* message);

private:
	BLooper* fWindow;
	BMessage fMessage;
	int32 fPercent;
};

// Productive 2.0.1 OK and Cancel both just close it (ItemHit), so here too.
class TDialogFillSpecial : public TDialog {
public:
	TDialogFillSpecial();

protected:
	virtual bool ItemHit(int32 index, BMessage* message);
};

// Options > Sort… (TDialogSort; TSSDialog 25): the range (3), Vertical (4,
// on a range more than one row high) or Horizontal (5), and three order
// keys, each a range "A2..A9" (7, 11, 15) with Ascending (8, 12, 16) or
// Descending (9, 13, 17). The direction begins as the ranges' shapes say
// (all as high: Vertical; all as wide: Horizontal) and the first key as
// the first range's first column (row). OK checks each key
// (STR# 1503: 6 the first is needed, 7 not a range, 8/9 not as high/wide as
// the range) and sends `target` 'sM75' with "SortRanges", "vertical",
// "Key 1".. and "Key 1ascending"...
class TDialogSort : public TDialog {
public:
	TDialogSort(BHandler* target, const std::vector<SSRange>& ranges);

protected:
	virtual void Init();
	virtual bool ItemHit(int32 index, BMessage* message);

private:
	BHandler* fTarget;
	std::vector<SSRange> fRanges;
	bool fVertical;
	int fHeight, fWidth;	// the ranges' (-1: they differ)
	SSRange fKey;
};

} // namespace gobe

#endif
