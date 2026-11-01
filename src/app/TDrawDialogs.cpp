#include "TDrawDialogs.h"

#include <Alert.h>

#include <cstdlib>

#include "GobeResources.h"

namespace gobe {

namespace {

// a number in a text item, or false (TString::Num)
bool
NumberIn(const BString& text, float& value)
{
	char* end;
	value = strtof(text.String(), &end);
	return end != text.String();
}


// SimpleAlert(6, 4): the value isn't a valid number
void
NotANumber()
{
	(new BAlert("", GobeResources::IndexString(6, 4).String(), "OK"))->Go(NULL);
}


BString
Number(float value)
{
	BString text;
	text << value;
	return text;
}

} // namespace


TDialogAngle::TDialogAngle(BLooper* window, int32 type, float angle)
	:
	TDialog(30, 30, 'APPL', NULL),
	fWindow(window),
	fType(type),
	fAngle(angle)
{
}


// TDialogAngle::Init
void
TDialogAngle::Init()
{
	SetTextItem(2, Number(fAngle).String());
	MakeFocusItem(2);
	SetTitle(GobeResources::IndexString(1101, fType == 1 ? 18 : 19).String());
}


// TDialogAngle::ItemHit: OK takes 0 to 360
bool
TDialogAngle::ItemHit(int32 index, BMessage* message)
{
	if (index == 0) {
		float angle;
		if (!NumberIn(TextItem(2), angle) || angle < 0 || angle > 360) {
			NotANumber();
			MakeFocusItem(2);
			return true;
		}
		BMessage set('sM4P');
		set.AddInt32("Type", fType);
		set.AddFloat("Angle", angle);
		fWindow->PostMessage(&set);
	}
	return TDialog::ItemHit(index, message);
}


TDialogRadius::TDialogRadius(BLooper* window, float x, float y)
	:
	TDialog(201, 201, 'APPL', NULL),
	fWindow(window),
	fRadius{x, y}
{
}


// TDialogRadius::Init
void
TDialogRadius::Init()
{
	SetTextItem(2, Number(fRadius[0]).String());
	SetTextItem(3, Number(fRadius[1]).String());
	MakeFocusItem(2);
}


// TDialogRadius::ItemHit: OK takes radii of 0 or more
bool
TDialogRadius::ItemHit(int32 index, BMessage* message)
{
	if (index == 0) {
		float x, y;
		if (!NumberIn(TextItem(2), x) || x < 0) {
			NotANumber();
			MakeFocusItem(2);
			return true;
		}
		if (!NumberIn(TextItem(3), y) || y < 0) {
			NotANumber();
			MakeFocusItem(3);
			return true;
		}
		BMessage set('sM4R');
		set.AddFloat("XRadius", x);
		set.AddFloat("YRadius", y);
		fWindow->PostMessage(&set);
	}
	return TDialog::ItemHit(index, message);
}

} // namespace gobe
