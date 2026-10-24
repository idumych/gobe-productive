// Word processing dialogs: Insert Date/Time (TDialogWPDateTimeFormat).
#include "TWPDialogs.h"

#include <time.h>

#include <Looper.h>
#include <Message.h>

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

} // namespace gobe
