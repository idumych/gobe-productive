#include "TRibbon.h"

#include "TPreferences.h"

#include <MenuItem.h>
#include <Window.h>

#include <algorithm>
#include <cmath>
#include <cstring>

#include "GobeResources.h"

namespace gobe {

namespace {

const rgb_color kPanel = {0xd8, 0xd8, 0xd8, 255};
const rgb_color kShadow = {0x70, 0x70, 0x70, 255};
const rgb_color kWhite = {255, 255, 255, 255};
const rgb_color kBlack = {0, 0, 0, 255};
const rgb_color kWash = {0xa0, 0xa0, 0xa0, 255};

} // namespace


TRibbonElement::TRibbonElement(uint32 id, BRect rect)
	:
	id(id),
	rect(rect)
{
}


TMICNElement::TMICNElement(uint32 id, BPoint where, int32 micn)
	:
	TRibbonElement(id, BRect(where.x, where.y, where.x + 15, where.y + 15)),
	fIcon(LoadIcon(micn))
{
}


TMICNElement::TMICNElement(uint32 id, BPoint where, BBitmap* icon)
	:
	TRibbonElement(id, BRect(where.x, where.y, where.x + 15, where.y + 15)),
	fIcon(icon)
{
}


BBitmap*
TMICNElement::LoadIcon(int32 micn, const ResourceFile* resources)
{
	const ResourceFile::Resource* r = (resources != NULL ? *resources
		: GobeResources::File()).Find('MICN', micn);
	if (r == NULL || r->data.size() < 256)
		return NULL;
	BBitmap* icon = new BBitmap(BRect(0, 0, 15, 15), B_CMAP8);
	icon->SetBits(r->data.data(), 256, 0, B_CMAP8);
	return icon;
}


void
TMICNElement::SetIcon(BBitmap* icon)
{
	fIcon.reset(icon);
}


// TMICNElement::Draw: the icon over the ribbon
void
TMICNElement::Draw(BView* view, bool down)
{
	view->SetHighColor(kPanel);
	view->FillRect(rect);
	if (fIcon == NULL)
		return;
	view->SetDrawingMode(B_OP_OVER);
	view->DrawBitmap(fIcon.get(), rect.LeftTop());
	view->SetDrawingMode(B_OP_COPY);
}


TMICNButtonElement::TMICNButtonElement(uint32 id, BPoint where, int32 micn, BMessage* message)
	:
	TMICNElement(id, where, micn),
	fMessage(message)
{
	rect.Set(where.x, where.y, where.x + 19, where.y + 19);
}


// TMICNButtonElement::Draw: the icon in the middle, a pixel lower right
// when pressed
void
TMICNButtonElement::Draw(BView* view, bool down)
{
	view->SetHighColor(kPanel);
	view->FillRect(rect);
	if (fIcon != NULL) {
		BPoint at(roundf(rect.left + rect.Width() * 0.5f - 8),
			roundf(rect.top + rect.Height() * 0.5f - 8));
		if (down)
			at += BPoint(1, 1);
		view->SetDrawingMode(B_OP_OVER);
		view->DrawBitmap(fIcon.get(), at);
		view->SetDrawingMode(B_OP_COPY);
	}
	if (down)
		TRibbon::DrawOutlineDown(view, rect);
	else
		TRibbon::DrawOutlineUp(view, rect, selected);
}


void
TMICNButtonElement::Invoke(TRibbon* ribbon)
{
	if (fMessage == NULL || ribbon->Window() == NULL)
		return;
	BMessage message(*fMessage);
	message.AddInt32("modifiers", int32(modifiers()));
	BPoint where;
	uint32 buttons = 0;
	ribbon->GetMouse(&where, &buttons, false);
	message.AddInt32("buttons", int32(buttons));
	ribbon->Send(&message);
}


TRibbon::TRibbon(const char* name)
	:
	BView(name, B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE)
{
	SetViewColor(kPanel);
}


BSize
TRibbon::MinSize()
{
	return BSize(50, 24);
}


BSize
TRibbon::MaxSize()
{
	return BSize(B_SIZE_UNLIMITED, 24);
}


BSize
TRibbon::PreferredSize()
{
	return BSize(400, 24);
}


void
TRibbon::AddElement(TRibbonElement* element)
{
	fElements.emplace_back(element);
	Invalidate(element->rect);
}


void
TRibbon::RemoveAllElements()
{
	fElements.clear();
	fPressed = NULL;
	Invalidate();
}


TRibbonElement*
TRibbon::GetElement(uint32 id) const
{
	for (const auto& element : fElements)
		if (element->id == id)
			return element.get();
	return NULL;
}


void
TRibbon::SetEnabled(uint32 id, bool enabled)
{
	TRibbonElement* element = GetElement(id);
	if (element != NULL && element->enabled != enabled) {
		element->enabled = enabled;
		Invalidate(element->rect);
	}
}


void
TRibbon::SelectElement(uint32 id, uint32 family)
{
	for (const auto& element : fElements) {
		if (element->family != family)
			continue;
		bool selected = element->id == id;
		if (element->selected != selected) {
			element->selected = selected;
			Invalidate(element->rect);
		}
	}
}


TRibbonElement*
TRibbon::FindElement(BPoint where) const
{
	for (const auto& element : fElements)
		if (element->rect.Contains(where))
			return element.get();
	return NULL;
}


// TRibbon::DrawElement: drawn, then washed out (B_OP_MAX) when disabled
void
TRibbon::DrawElement(TRibbonElement* element, bool down)
{
	SetPenSize(1);
	element->Draw(this, down);
	if (!element->enabled) {
		SetDrawingMode(B_OP_MAX);
		SetHighColor(kWash);
		FillRect(element->rect);
		SetDrawingMode(B_OP_COPY);
	}
}


void
TRibbon::Draw(BRect updateRect)
{
	SetHighColor(kPanel);
	FillRect(updateRect);
	for (const auto& element : fElements)
		if (element->rect.Intersects(updateRect))
			DrawElement(element.get(), element.get() == fPressed && fPressedDown);
	BRect bounds = Bounds();
	SetHighColor(kShadow);
	StrokeLine(BPoint(bounds.left, bounds.bottom), BPoint(bounds.right, bounds.bottom));
}


// DrawElementRect and DrawElementOutlineUp: a black frame with its corners
// cut, white top left inside and gray bottom right; a selected button
// darkened (B_OP_MIN)
void
TRibbon::DrawOutlineUp(BView* view, BRect r, bool selected)
{
	view->SetHighColor(kBlack);
	view->StrokeLine(BPoint(r.left + 1, r.top), BPoint(r.right - 1, r.top));
	view->StrokeLine(BPoint(r.left, r.top + 1), BPoint(r.left, r.bottom - 1));
	view->StrokeLine(BPoint(r.right, r.top + 1), BPoint(r.right, r.bottom - 1));
	view->StrokeLine(BPoint(r.left + 1, r.bottom), BPoint(r.right - 1, r.bottom));
	view->SetHighColor(kWhite);
	view->StrokeLine(BPoint(r.left + 1, r.top + 1), BPoint(r.left + 1, r.bottom - 2));
	view->StrokeLine(BPoint(r.left + 1, r.top + 1), BPoint(r.right - 2, r.top + 1));
	view->SetHighColor(kShadow);
	view->StrokeLine(BPoint(r.left + 1, r.bottom - 1), BPoint(r.right - 1, r.bottom - 1));
	view->StrokeLine(BPoint(r.right - 1, r.top + 1), BPoint(r.right - 1, r.bottom - 1));
	view->SetHighColor(kBlack);
	if (selected) {
		view->SetDrawingMode(B_OP_MIN);
		view->SetHighColor(kWash);
		view->FillRect(r.InsetByCopy(1, 1));
		view->SetDrawingMode(B_OP_COPY);
		view->SetHighColor(kBlack);
	}
}


// DrawElementOutlineDown: the bevel the other way
void
TRibbon::DrawOutlineDown(BView* view, BRect r)
{
	view->SetHighColor(kBlack);
	view->StrokeLine(BPoint(r.left + 1, r.top), BPoint(r.right - 1, r.top));
	view->StrokeLine(BPoint(r.left, r.top + 1), BPoint(r.left, r.bottom - 1));
	view->StrokeLine(BPoint(r.right, r.top + 1), BPoint(r.right, r.bottom - 1));
	view->StrokeLine(BPoint(r.left + 1, r.bottom), BPoint(r.right - 1, r.bottom));
	view->SetHighColor(kShadow);
	view->StrokeLine(BPoint(r.left + 1, r.top + 1), BPoint(r.left + 1, r.bottom - 2));
	view->StrokeLine(BPoint(r.left + 1, r.top + 1), BPoint(r.right - 2, r.top + 1));
	view->SetHighColor(kWhite);
	view->StrokeLine(BPoint(r.left + 1, r.bottom - 1), BPoint(r.right - 1, r.bottom - 1));
	view->StrokeLine(BPoint(r.right - 1, r.top + 1), BPoint(r.right - 1, r.bottom - 1));
	view->SetHighColor(kBlack);
}


bool
TRibbon::Click(uint32 id)
{
	TRibbonElement* element = GetElement(id);
	if (element == NULL || !element->enabled || !element->Clickable())
		return false;
	if (element->selectable)
		SelectElement(element->id, element->family);
	fSynchronous = true;
	element->Invoke(this);
	fSynchronous = false;
	return true;
}


void
TRibbon::Send(BMessage* message)
{
	if (Window() == NULL)
		return;
	if (fSynchronous)
		Window()->MessageReceived(message);
	else
		Window()->PostMessage(message);
}


// TRibbonElement::MouseDown: pressed while the mouse is over it, invoked
// when let go there
void
TRibbon::MouseDown(BPoint where)
{
	TRibbonElement* element = FindElement(where);
	if (element == NULL || !element->enabled)
		return;
	if (element->MouseDown(this, where) || !element->Clickable())
		return;
	fPressed = element;
	fPressedDown = true;
	DrawElement(element, true);
	SetMouseEventMask(B_POINTER_EVENTS, B_LOCK_WINDOW_FOCUS);
}


void
TRibbon::MouseMoved(BPoint where, uint32 transit, const BMessage* dragMessage)
{
	if (fPressed != NULL) {
		bool down = fPressed->rect.Contains(where);
		if (down != fPressedDown) {
			fPressedDown = down;
			DrawElement(fPressed, down);
		}
		return;
	}
	// the element's tip
	TRibbonElement* element = transit == B_EXITED_VIEW ? NULL : FindElement(where);
	std::string tip = element != NULL ? element->tip : std::string();
	// the Tool Tips preference
	if (!AppPreferences().toolTips)
		tip.clear();
	if (tip != fTip) {
		fTip = tip;
		SetToolTip(tip.empty() ? (const char*)NULL : tip.c_str());
	}
}


void
TRibbon::MouseUp(BPoint where)
{
	TRibbonElement* element = fPressed;
	if (element == NULL)
		return;
	fPressed = NULL;
	fPressedDown = false;
	DrawElement(element, false);
	if (element->rect.Contains(where)) {
		if (element->selectable)
			SelectElement(element->id, element->family);
		element->Invoke(this);
	}
}

TMICNButtonElement::TMICNButtonElement(uint32 id, BPoint where, BBitmap* icon, BMessage* message)
	:
	TMICNElement(id, where, icon),
	fMessage(message)
{
	rect.Set(where.x, where.y, where.x + 19, where.y + 19);
}


TMenuElement::TMenuElement(uint32 id, BRect rect, const char* label, BBitmap* icon)
	:
	TRibbonElement(id, rect),
	label(label != NULL ? label : ""),
	fIcon(icon)
{
	if (icon != NULL)
		this->rect.Set(rect.left, rect.top, rect.left + 19, rect.top + 19);
}


void
TMenuElement::Draw(BView* view, bool down)
{
	view->SetHighColor(kPanel);
	view->FillRect(rect);
	if (fIcon != NULL) {
		// TMICNMenuElement::Draw: the icon in the middle
		BPoint at(roundf(rect.left + (rect.Width() + 1) * 0.5f - 8),
			roundf(rect.top + (rect.Height() + 1) * 0.5f - 8));
		if (down)
			at += BPoint(1, 1);
		view->SetDrawingMode(B_OP_OVER);
		view->DrawBitmap(fIcon.get(), at);
		view->SetDrawingMode(B_OP_COPY);
		TRibbon::DrawPopUpIndicator(view, down ? rect.OffsetByCopy(1, 1) : rect);
		if (down)
			TRibbon::DrawOutlineDown(view, rect);
		else
			TRibbon::DrawOutlineUp(view, rect, false);
		return;
	}
	// TMenuElement::Draw: the label in the plain font (at most 10 points),
	// truncated to the button
	if (down) {
		view->SetHighColor(kBlack);
		view->StrokeRect(rect);
	} else
		TRibbon::DrawOutlineUp(view, rect, false);
	TRibbon::DrawPopUpIndicator(view, down ? rect.OffsetByCopy(1, 1) : rect);
	BFont font(be_plain_font);
	font.SetSize(std::min(font.Size(), 10.0f));
	// (with the descent and leading of BeOS's Bitstream fonts)
	float baseline = rect.bottom - font.Size() * 0.25f - 3;
	BString text(label.c_str());
	font.TruncateString(&text, B_TRUNCATE_END, rect.right - (rect.left + 5) - 3);
	view->PushState();
	view->SetFont(&font);
	view->SetHighColor(kBlack);
	view->SetLowColor(kPanel);
	view->DrawString(text.String(), BPoint(rect.left + 5, baseline));
	view->PopState();
	if (down) {
		view->SetDrawingMode(B_OP_MIN);
		view->SetHighColor(kWash);
		view->FillRect(rect);
		view->SetDrawingMode(B_OP_COPY);
	}
}


bool
TMenuElement::MouseDown(TRibbon* ribbon, BPoint where)
{
	BPopUpMenu* menu = new BPopUpMenu("element", false, false);
	FillMenu(menu, ribbon);
	ribbon->DrawElement(this, true);
	ribbon->Sync();
	BPoint at = ribbon->ConvertToScreen(BPoint(rect.left + 1, rect.bottom + 1));
	BMenuItem* chosen = menu->Go(at, false, true, false);
	ribbon->DrawElement(this, false);
	if (chosen != NULL && chosen->Message() != NULL)
		ribbon->Send(chosen->Message());
	delete menu;
	return true;
}


void
TRibbon::DrawPopUpIndicator(BView* view, BRect r)
{
	view->SetHighColor(255, 0, 0);
	for (int i = 0; i < 3; i++)
		view->StrokeLine(BPoint(r.right - 5 + i, r.bottom - 3 - i),
			BPoint(r.right - 5 + i, r.bottom - 3));
	view->SetHighColor(kBlack);
}

} // namespace gobe
