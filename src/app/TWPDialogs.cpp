// Word processing dialogs: Insert Date/Time (TDialogWPDateTimeFormat),
// Insert Formula (TDialogWPFormula).
#include "TWPDialogs.h"

#include <time.h>

#include <Alert.h>
#include <Button.h>
#include <Looper.h>
#include <Message.h>

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

} // namespace gobe
