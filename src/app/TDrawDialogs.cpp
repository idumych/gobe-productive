#include "TDrawDialogs.h"

#include <Alert.h>
#include <MenuField.h>
#include <MenuItem.h>

#include <cstdlib>
#include <string>

#include "GobeResources.h"
#include "ResourceFile.h"

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


// TDialog::SetTextItem with a float: no decimals when there are none
BString
Number(float value)
{
	BString text;
	if (value == float(int32(value)))
		text << int32(value);
	else
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



namespace {

// SimpleAlert(6, 13): the value isn't from `low` to `high`
void
OutOfRange(int32 low, int32 high)
{
	BString text;
	text.SetToFormat(GobeResources::IndexString(6, 13).String(), int(low), int(high));
	(new BAlert("", text.String(), "OK"))->Go(NULL);
}

// TDialogPRDocSize::Init's sizes
const int32 kSlideSizes[][2] = {{640, 480}, {800, 600}, {1024, 768}, {1152, 864},
	{1152, 900}, {1280, 1024}, {1600, 1200}, {4800, 3600}};

} // namespace


TDialogGRDocSize::TDialogGRDocSize(BLooper* window, float across, float down)
	:
	TDialog(200, 200, 'APPL', NULL),
	fWindow(window),
	fPages{across, down}
{
}


// TDialogGRDocSize::Init
void
TDialogGRDocSize::Init()
{
	SetTextItem(2, Number(fPages[0]).String());
	SetTextItem(3, Number(fPages[1]).String());
	MakeFocusItem(2);
}


// TDialogGRDocSize::ItemHit: OK takes 1 to 40 pages each way
bool
TDialogGRDocSize::ItemHit(int32 index, BMessage* message)
{
	if (index == 0) {
		float pages[2];
		for (int32 i = 0; i < 2; i++) {
			if (!NumberIn(TextItem(2 + i), pages[i]) || pages[i] < 1 || pages[i] > 40) {
				OutOfRange(1, 40);
				MakeFocusItem(2 + i);
				return true;
			}
		}
		BMessage set('sM21');
		set.AddFloat("Width", pages[0]);
		set.AddFloat("Height", pages[1]);
		fWindow->PostMessage(&set);
	}
	return TDialog::ItemHit(index, message);
}


TDialogPRDocSize::TDialogPRDocSize(BLooper* window, int32 width, int32 height)
	:
	TDialog(11005, 11005, 'sD00', NULL, &GobeResources::Part("Presentation")),
	fWindow(window),
	fSize{width, height}
{
}


// TDialogPRDocSize::Init: the sizes' items send 'sD02' with "width" and
// "height"
void
TDialogPRDocSize::Init()
{
	if (BMenuField* field = dynamic_cast<BMenuField*>(Item(2))) {
		BMenu* menu = field->Menu();
		while (menu->CountItems() > 0)
			delete menu->RemoveItem(menu->CountItems() - 1);
		std::string format = IndexString(GobeResources::Part("Presentation"), 11002, 3);
		for (const auto& size : kSlideSizes) {
			BString label;
			label.SetToFormat(format.c_str(), int(size[0]), int(size[1]));
			BMessage* message = new BMessage('sD02');
			message->AddInt32("dialog", 11005);
			message->AddInt32("index", 2);
			message->AddInt32("width", size[0]);
			message->AddInt32("height", size[1]);
			BMenuItem* item = new BMenuItem(label.String(), message);
			menu->AddItem(item);
			if (size[0] == fSize[0] && size[1] == fSize[1])
				item->SetMarked(true);
		}
	}
	SetTextItem(3, std::to_string(fSize[0]).c_str());
	SetTextItem(4, std::to_string(fSize[1]).c_str());
	MakeFocusItem(0);
}


// TDialogPRDocSize::ItemHit: a size fills in the width and height; OK
// takes 1 to 10000 each
bool
TDialogPRDocSize::ItemHit(int32 index, BMessage* message)
{
	if (index == 2) {
		int32 width, height;
		if (message->FindInt32("width", &width) == B_OK
				&& message->FindInt32("height", &height) == B_OK) {
			SetTextItem(3, std::to_string(width).c_str());
			SetTextItem(4, std::to_string(height).c_str());
		}
		return false;
	}
	if (index == 0) {
		int32 size[2];
		for (int32 i = 0; i < 2; i++) {
			BString text = TextItem(3 + i);
			char* end;
			long value = strtol(text.String(), &end, 10);
			if (end == text.String() || value < 1 || value > 10000) {
				OutOfRange(1, 10000);
				MakeFocusItem(3 + i);
				return true;
			}
			size[i] = int32(value);
		}
		BMessage set('sM21');
		set.AddInt32("Width", size[0]);
		set.AddInt32("Height", size[1]);
		fWindow->PostMessage(&set);
	}
	return TDialog::ItemHit(index, message);
}

} // namespace gobe
