#include "TIconButton.h"

#include <Bitmap.h>
#include <Window.h>

#include "GobeResources.h"
#include "ResourceFile.h"
#include "TRibbon.h"

namespace gobe {

TIconButton::TIconButton(BMessage* message, int32 id, int32 set, bool visible,
		const ResourceFile* resources)
	:
	BControl("iconbutton", NULL, message, B_WILL_DRAW | B_NAVIGABLE),
	fResources(resources),
	fSet(set),
	fVisible(visible)
{
	SetViewColor(216, 216, 216);
	SetDrawingMode(B_OP_OVER);
	SetExplicitMinSize(BSize(35, 35));
	SetExplicitMaxSize(BSize(35, 35));
	if (visible)
		LoadNewImage(id);
}


TIconButton::~TIconButton()
{
	delete fIcon;
}


// the frame inset by 2: 32 x 32, in the BeOS palette
void
TIconButton::LoadNewImage(int32 id)
{
	delete fIcon;
	fIcon = NULL;
	const ResourceFile::Resource* r = fResources != NULL
		? fResources->Find(FourCC("ICON"), id) : NULL;
	if (r == NULL)
		r = GobeResources::File().Find(FourCC("ICON"), id);
	if (r == NULL || r->data.size() < 1024)
		return;
	fIcon = new BBitmap(BRect(0, 0, 31, 31), B_CMAP8);
	fIcon->SetBits(r->data.data(), 1024, 0, B_CMAP8);
}


void
TIconButton::SetVisible(bool visible)
{
	fVisible = visible;
	Invalidate();
}


// a button of a selection set turned on turns off the one of its set that
// was (only buttons of a set change)
void
TIconButton::SetValue(int32 value)
{
	if (fSet == 0 || value == Value())
		return;
	if (value != 0 && Parent() != NULL) {
		for (BView* child = Parent()->ChildAt(0); child != NULL; child = child->NextSibling()) {
			TIconButton* other = dynamic_cast<TIconButton*>(child);
			if (other != NULL && other != this && other->Value() != 0 && other->fSet == fSet) {
				other->SetValue(0);
				break;
			}
		}
	}
	BControl::SetValue(value);
}


// the parent's color; the icon (moved down and right while pressed) in
// the outline, up (darkened when on) or down while pressed; washed out
// when disabled; the focus mark
void
TIconButton::Draw(BRect)
{
	BRect bounds = Bounds();
	SetHighColor(Parent() != NULL ? Parent()->ViewColor() : ViewColor());
	FillRect(bounds);
	if (!fVisible)
		return;
	BRect icon = bounds.InsetByCopy(2, 2);
	if (fPressed)
		icon.OffsetBy(1, 1);
	if (fIcon != NULL)
		DrawBitmapAsync(fIcon, icon);
	if (!fPressed)
		TRibbon::DrawOutlineUp(this, bounds, Value() != 0);
	else
		TRibbon::DrawOutlineDown(this, bounds);
	SetDrawingMode(B_OP_OVER);
	if (!IsEnabled()) {
		SetDrawingMode(B_OP_MAX);
		SetHighColor(0x70, 0x70, 0x70);
		FillRect(bounds);
		SetDrawingMode(B_OP_OVER);
	}
	if (IsFocus()) {
		SetHighColor(ui_color(B_KEYBOARD_NAVIGATION_COLOR));
		StrokeRect(bounds.InsetByCopy(1, 1));
	}
}


// TIconButton::MouseDown: pressed while the pointer is over it; sent at
// once
void
TIconButton::MouseDown(BPoint where)
{
	if (!IsEnabled() || !Bounds().Contains(where))
		return;
	SetMouseEventMask(B_POINTER_EVENTS, B_LOCK_WINDOW_FOCUS);
	SetTracking(true);
	MouseMoved(where, B_INSIDE_VIEW, NULL);
	Invoke();
}


void
TIconButton::MouseMoved(BPoint where, uint32 transit, const BMessage* dragMessage)
{
	if (!IsTracking()) {
		BControl::MouseMoved(where, transit, dragMessage);
		return;
	}
	bool pressed = Bounds().Contains(where);
	if (pressed != fPressed) {
		fPressed = pressed;
		Invalidate();
	}
}


// TIconButton::MouseUp: let go over it, a button of a set is turned on
void
TIconButton::MouseUp(BPoint where)
{
	if (!IsEnabled() || !IsTracking())
		return;
	SetTracking(false);
	if (Bounds().Contains(where) && fSet != 0)
		SetValue(1);
	fPressed = false;
	Invalidate();
}

} // namespace gobe
