#include "TStatusDialog.h"

#include <GroupLayout.h>
#include <OS.h>
#include <StatusBar.h>

#include <cstdio>

#include "GobeResources.h"

namespace gobe {

TStatusDialog::TStatusDialog(const char* title, const char* formatName,
		const char* formatType)
	:
	TDialog(71, 71, 'APPL', NULL),
	fTitle(title),
	fFormatName(formatName),
	fFormatType(formatType)
{
}


BString
TStatusDialog::Title(bool import, const char* kind, const char* fileName)
{
	BString format = GobeResources::IndexString(2, import ? 19 : 21);
	char title[1024];
	snprintf(title, sizeof title, format.String(), kind, fileName);
	return BString(title);
}


// TStatusDialog::Init: a BStatusBar ("percentage", 0 to 1) filling the
// user item, the labels
void
TStatusDialog::Init()
{
	SetTitle(fTitle.String());
	if (BView* user = Item(3)) {
		fBar = new BStatusBar("percentage");
		fBar->SetMaxValue(1.0f);
		user->SetLayout(new BGroupLayout(B_VERTICAL, 0));
		user->GetLayout()->AddView(fBar);
	}
	if (fFormatName.Length() > 0)
		SetTextItem(1, fFormatName.String());
	if (fFormatType.Length() > 0)
		SetTextItem(2, fFormatType.String());
}


bool
TStatusDialog::ItemHit(int32 index, BMessage* message)
{
	// Cancel ('sD00'): the work stops when it next reports
	if (index == 0) {
		fCancelled = true;
		return true;
	}
	return TDialog::ItemHit(index, message);
}


void
TStatusDialog::SetPercentRange(float start, float end)
{
	fStart = start;
	fEnd = end;
}


bool
TStatusDialog::Update(float fraction)
{
	float value = (fEnd - fStart) * fraction + fStart;
	bigtime_t now = system_time();
	if (now < fLastUpdate + 100000 && value > 0)
		return !fCancelled;
	fLastUpdate = now;
	if (fBar != NULL && Lock()) {
		float current = fBar->CurrentValue();
		if (value == 0)
			fBar->Reset();
		else if (value > current)
			fBar->Update(value - current);
		Unlock();
	}
	return !fCancelled;
}


void
TStatusDialog::Finish()
{
	if (Lock()) {
		if (fBar != NULL && !IsHidden())
			fBar->Update(1.0f - fBar->CurrentValue());
		Unlock();
	}
	snooze(20000);
	if (Lock())
		Quit();
}

} // namespace gobe
