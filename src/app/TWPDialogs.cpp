// Word processing dialogs: Insert Date/Time (TDialogWPDateTimeFormat),
// Insert Formula (TDialogWPFormula), Columns (TDialogSection), Insert
// Footnote (TDialogWPInsertFootnote, TDialogWPFootnoteOptions).
#include "TWPDialogs.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include <Alert.h>
#include <Button.h>
#include <Looper.h>
#include <Message.h>
#include <TextControl.h>

#include <functional>

#include "Editing.h"
#include "GobeResources.h"
#include "SSEval.h"
#include "SSStyle.h"

namespace gobe {

TDialogWPDateTimeFormat::TDialogWPDateTimeFormat(BLooper* window, uint32 command,
	const std::string& format, bool isStatic)
	:
	TDialog(302, 302, 'APPL', NULL),
	fWindow(window),
	fCommand(command),
	fFormat(format),
	fStatic(isStatic)
{
}


// TDialogWPDateTimeFormat::GetFormat
std::string
TDialogWPDateTimeFormat::Format(int32 index) const
{
	return GobeResources::IndexString(fCommand == 'sM53' ? 0x51b : 0x51c, index).String();
}


void
TDialogWPDateTimeFormat::Init()
{
	if (fCommand != 'sM53')
		SetTitle(GobeResources::IndexString(0x516, 9).String());
	// each format shows "=Now()" in it
	SSValue now = SSValue::Time(int64_t(time(NULL)));
	int32 chosen = 7;
	for (int32 item = 3; item <= 7; item++) {
		std::string format = Format(item - 3);
		SetLabelItem(item, FormatCellValue(now, format).c_str());
		if (format == fFormat)
			chosen = item;
	}
	SetRadioItem(chosen, true);
	SetCheckBoxItem(8, fStatic);
}


bool
TDialogWPDateTimeFormat::ItemHit(int32 index, BMessage* message)
{
	if (index == 0) {
		BMessage insert(fCommand);
		insert.AddString("format", Format(RadioValue(2)).c_str());
		insert.AddBool("static", CheckBoxItem(8));
		fWindow->PostMessage(&insert);
	}
	return TDialog::ItemHit(index, message);
}

TDialogWPFormula::TDialogWPFormula(BLooper* window, const std::string& formula,
	bool displayResult, bool displayFormula)
	:
	TDialog(300, 300, 'APPL', NULL),
	fWindow(window),
	fFormula(formula),
	fDisplayResult(displayResult),
	fDisplayFormula(displayFormula)
{
}


void
TDialogWPFormula::Init()
{
	if (BButton* ok = dynamic_cast<BButton*>(Item(0)))
		ok->SetEnabled(!fFormula.empty());
	int32 radio = 4;
	if (fDisplayFormula)
		radio = fDisplayResult ? 6 : 5;
	SetRadioItem(radio, true);
	SetTextItem(2, fFormula.c_str());
	MakeFocusItem(2);
}


bool
TDialogWPFormula::ItemHit(int32 index, BMessage* message)
{
	if (index == 2) {
		// OK only with a formula
		if (BButton* ok = dynamic_cast<BButton*>(Item(0)))
			ok->SetEnabled(TextItem(2).Length() > 0);
		return true;
	}
	if (index != 0)
		return TDialog::ItemHit(index, message);
	std::string formula = TextItem(2).String();
	if (formula.empty()) {
		(new BAlert("", GobeResources::IndexString(6, 4).String(), "OK"))->Go(NULL);
		MakeFocusItem(2);
		return true;
	}
	if (formula[0] != '=')
		formula.insert(0, "=");
	std::vector<FormulaToken> tokens;
	std::string error;
	if (!ParseFormula(formula.substr(1), CellID(), [](const std::string&) { return true; },
			tokens, error)) {
		(new BAlert("", error.c_str(), "OK"))->Go(NULL);
		return true;
	}
	int32 display = RadioValue(3);
	bool result = display == 0 || display == 2, shown = display == 1 || display == 2;
	BMessage insert('sM55');
	insert.AddString("formula", formula.c_str());
	insert.AddString("format", "General");
	insert.AddBool("displayFormula", shown);
	insert.AddBool("displayResult", result);
	fWindow->PostMessage(&insert);
	return TDialog::ItemHit(index, message);
}

TDialogSection::TDialogSection(BLooper* window, int32 columns)
	:
	TDialog(303, 303, 'APPL', NULL),
	fWindow(window),
	fColumns(columns)
{
}


void
TDialogSection::Init()
{
	BString text;
	text << fColumns;
	SetTextItem(2, text.String());
	MakeFocusItem(2);
}


bool
TDialogSection::ItemHit(int32 index, BMessage* message)
{
	if (index == 0) {
		BString text = TextItem(2);
		char* end;
		long columns = strtol(text.String(), &end, 10);
		if (end == text.String() || columns < 1 || columns > 10) {
			BString alert;
			alert.SetToFormat(GobeResources::IndexString(6, 13).String(), 1, 10);
			(new BAlert("", alert.String(), "OK"))->Go(NULL);
			MakeFocusItem(2);
			return true;
		}
		BMessage set('sM51');
		set.AddInt32("value", int32(columns));
		fWindow->PostMessage(&set);
	}
	return TDialog::ItemHit(index, message);
}

// the dialog's last choice (gIsEndnote)
static bool sIsEndnote = false;
static const uint32 kMsgMarkModified = 'MoDF';


TDialogWPInsertFootnote::TDialogWPInsertFootnote(BLooper* window, int16 first, bool mainText)
	:
	TDialog(306, 306, 'APPL', NULL),
	fWindow(window),
	fFirst(first),
	fMainText(mainText)
{
}


void
TDialogWPInsertFootnote::Init()
{
	SetRadioItem(4, !sIsEndnote);
	SetRadioItem(5, sIsEndnote);
	SetRadioItem(7, true);
	SetLabelItem(4, GobeResources::IndexString(0x516, fMainText ? 10 : 11).String());
	SetLabelItem(5, GobeResources::IndexString(0x516, fMainText ? 12 : 13).String());
	// typing a mark chooses Custom Mark
	if (BTextControl* mark = dynamic_cast<BTextControl*>(Item(9))) {
		BMessage* modified = new BMessage(kMsgMarkModified);
		modified->AddBool("isMod", true);
		mark->SetModificationMessage(modified);
	}
}


void
TDialogWPInsertFootnote::MessageReceived(BMessage* message)
{
	if (message->what == kMsgMarkModified) {
		SetRadioItem(7, false);
		SetRadioItem(8, true);
		if (BButton* ok = dynamic_cast<BButton*>(Item(0)))
			ok->SetEnabled(TextItem(9).Length() > 0);
		return;
	}
	TDialog::MessageReceived(message);
}


bool
TDialogWPInsertFootnote::ItemHit(int32 index, BMessage* message)
{
	switch (index) {
		case 0:
		{
			BMessage insert('sM5I');
			sIsEndnote = RadioValue(3) == 1;
			insert.AddBool("endnote", sIsEndnote);
			if (RadioValue(6) != 0) {
				BString mark = TextItem(9);
				if (mark.Length() == 0)
					return true;
				insert.AddString("mark", mark.String());
			}
			fWindow->PostMessage(&insert);
			break;
		}
		case 2:
			(new TDialogWPFootnoteOptions(fWindow, fFirst))->RunDialog();
			return true;
		case 7:
			MakeFocusItem(9);
			SetRadioItem(8, false);
			SetRadioItem(7, true);
			if (BButton* ok = dynamic_cast<BButton*>(Item(0)))
				ok->SetEnabled(true);
			return true;
		case 8:
		{
			MakeFocusItem(9);
			BMessage modified(kMsgMarkModified);
			MessageReceived(&modified);
			return true;
		}
	}
	return TDialog::ItemHit(index, message);
}


TDialogWPFootnoteOptions::TDialogWPFootnoteOptions(BLooper* window, int16 first)
	:
	TDialog(307, 307, 'APPL', NULL),
	fWindow(window),
	fFirst(first)
{
}


void
TDialogWPFootnoteOptions::Init()
{
	BString text;
	text << fFirst;
	SetTextItem(2, text.String());
	MakeFocusItem(2);
}


bool
TDialogWPFootnoteOptions::ItemHit(int32 index, BMessage* message)
{
	if (index == 0) {
		BString text = TextItem(2);
		char* end;
		long first = strtol(text.String(), &end, 10);
		if (end == text.String())
			return true;
		BMessage set('sM5K');
		set.AddInt16("start", int16(first));
		fWindow->PostMessage(&set);
	}
	return TDialog::ItemHit(index, message);
}

} // namespace gobe
