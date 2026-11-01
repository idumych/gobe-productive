#include "TPageBar.h"

#include <Application.h>
#include <MessageRunner.h>
#include <Messenger.h>
#include <ScrollBar.h>
#include <Slider.h>
#include <Window.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "GobeResources.h"
#include "TGRRuler.h"
#include "TPageView.h"
#include "TRibbon.h"
#include "Zoom.h"

namespace gobe {

namespace {

const rgb_color kPanel = {0xd8, 0xd8, 0xd8, 255};
const rgb_color kGray = {0xa0, 0xa0, 0xa0, 255};

// TScaleSlider::getNextStepScale's steps
const float kSteps[20] = {0.015625f, 0.03125f, 0.0625f, 0.125f, 0.25f, 1.0f / 3, 0.5f,
	2.0f / 3, 0.75f, 1.0f, 1.25f, 4.0f / 3, 1.5f, 1.75f, 2.0f, 4.0f, 8.0f, 16.0f, 32.0f, 64.0f};

const uint32 kMsgRepeat = 'Gzrp';
const uint32 kMsgSliderValue = 'Gzsv';	// the slider window's value


// the plain font, smaller until its ascent and descent fit `height` (as
// TPageBar and TScaleSlider shrink theirs)
BFont
FittingFont(float height)
{
	BFont font(be_plain_font);
	font_height fh;
	font.GetHeight(&fh);
	while (fh.ascent + fh.descent > height && font.Size() > 1) {
		font.SetSize(font.Size() - 1);
		font.GetHeight(&fh);
	}
	return font;
}

} // namespace


TScaleSlider::TScaleSlider()
	:
	BView(BRect(0, 0, 69, 14), "ScaleSlider", B_FOLLOW_NONE, B_WILL_DRAW)
{
	SetViewColor(B_TRANSPARENT_COLOR);
	BFont font = FittingFont(14);
	SetFont(&font);
}


// TScaleSlider::buttonRect: minus at the left, plus 57 to the right
BRect
TScaleSlider::ButtonRect(bool minus) const
{
	BRect r(0, 1, 12, 13);
	if (!minus)
		r.OffsetBy(57, 0);
	return r;
}


// TScaleSlider::DrawButton: a bevelled button with a minus or a plus
void
TScaleSlider::DrawButton(bool minus, bool down)
{
	BRect r = ButtonRect(minus);
	SetHighColor(kPanel);
	FillRect(r);
	if (down)
		TRibbon::DrawOutlineDown(this, r);
	else
		TRibbon::DrawOutlineUp(this, r, false);
	SetHighColor(0, 0, 0);
	float middle = roundf((r.bottom - r.top) * 0.5f);
	float x = middle + r.left - 2, y = middle + r.top;
	StrokeLine(BPoint(x, y), BPoint(x + 4, y));
	if (!minus)
		StrokeLine(BPoint(x + 2, y - 2), BPoint(x + 2, y + 2));
}


// TScaleSlider::Draw, DrawText: the percent in a raised box with a small
// triangle at its bottom right, the buttons either side
void
TScaleSlider::Draw(BRect updateRect)
{
	BRect bounds = Bounds();
	SetHighColor(kPanel);
	FillRect(bounds);
	BRect text(12, 1, 57, 13);
	// (the original truncates scale x 100 + 0.5)
	char label[32];
	snprintf(label, sizeof label, "%d%%", int(floorf(fScale * 100.0f + 0.5f)));
	font_height height;
	GetFontHeight(&height);
	int width = int(roundf(StringWidth(label)));
	int pad = 1;
	if (width + 4 < 46)
		pad = int(roundf((46 - (width + 4)) * 0.5f)) + 1;
	SetHighColor(0, 0, 0);
	SetLowColor(kPanel);
	DrawString(label, BPoint(pad + 12, height.ascent + 1));
	for (int i = 0; i < 3; i++)
		StrokeLine(BPoint(52 + i, 10 - i), BPoint(52 + i, 10));
	TRibbon::DrawOutlineUp(this, text, false);
	DrawButton(true, fPressed < 0);
	DrawButton(false, fPressed > 0);
	SetHighColor(kGray);
	StrokeLine(BPoint(bounds.left, bounds.top), BPoint(bounds.right, bounds.top));
}


// TScaleSlider::getNextStepScale
float
TScaleSlider::NextStepScale(bool minus) const
{
	int32 i;
	if (minus) {
		for (i = 19; i >= 0; i--)
			if (kSteps[i] < fScale)
				break;
	} else {
		for (i = 0; i < 20; i++)
			if (fScale < kSteps[i])
				break;
	}
	return kSteps[std::max<int32>(0, std::min<int32>(19, i))];
}


void
TScaleSlider::SetScale(float scale, bool send)
{
	scale = std::max(0.015625f, std::min(64.0f, scale));
	if (scale == fScale)
		return;
	fScale = scale;
	if (scale != 1)
		fOther = scale;
	Invalidate();
	if (send && Window() != NULL) {
		BMessage message('s003');
		message.AddFloat("scale", fScale);
		Window()->PostMessage(&message);
	}
}


void
TScaleSlider::Step()
{
	SetScale(fFine ? fScale + (fPressed < 0 ? -0.01f : 0.01f) : NextStepScale(fPressed < 0),
		true);
}


// TScaleSlider::MouseDown, ButtonTrack: a button steps the zoom, again
// every 175 ms while held (by 1% with the secondary button or Option); the
// percent switches between 100% and the last other zoom
void
TScaleSlider::MouseDown(BPoint where)
{
	int32 buttons = 0;
	if (Window()->CurrentMessage() != NULL)
		Window()->CurrentMessage()->FindInt32("buttons", &buttons);
	if (where.x <= 12 || where.x >= 58) {
		fPressed = where.x <= 12 ? -1 : 1;
		fFine = buttons == B_SECONDARY_MOUSE_BUTTON || (modifiers() & B_OPTION_KEY) != 0;
		Invalidate();
		Step();
		BMessage repeat(kMsgRepeat);
		delete fRepeat;
		fRepeat = new BMessageRunner(BMessenger(this), &repeat, 175000);
		SetMouseEventMask(B_POINTER_EVENTS, B_LOCK_WINDOW_FOCUS);
		return;
	}
	// TScaleSlider::MouseDown: the percent clicked switches between 100%
	// and the last other zoom; held (a third of a second) or pressed with
	// another button, the slider window (TScaleSliderTrack, SliderTrack)
	uint32 held = 0;
	BPoint pointer;
	GetMouse(&pointer, &held, false);
	if (held == B_PRIMARY_MOUSE_BUTTON) {
		for (int i = 0; i < 6 && held != 0; i++) {
			snooze(50000);
			GetMouse(&pointer, &held, false);
		}
	}
	if (held == 0) {
		SetScale(fScale == 1 ? fOther : 1, true);
		return;
	}
	TSliderWindow::Track(this, ConvertToScreen(B_ORIGIN));
}


void
TScaleSlider::MouseUp(BPoint where)
{
	delete fRepeat;
	fRepeat = NULL;
	if (fPressed != 0) {
		fPressed = 0;
		Invalidate();
	}
}


void
TScaleSlider::MessageReceived(BMessage* message)
{
	// TSliderView::trackInThumb: TScaleSlider::SetValue
	if (message->what == kMsgSliderValue) {
		SetScale(ZoomForSliderValue(message->GetFloat("value", 0.5f)), true);
		return;
	}
	if (message->what == kMsgRepeat) {
		BPoint where;
		uint32 buttons;
		GetMouse(&where, &buttons, false);
		if (fPressed != 0 && buttons != 0 && ButtonRect(fPressed < 0).Contains(where))
			Step();
		return;
	}
	BView::MessageReceived(message);
}


TPageBar::TPageBar()
	:
	BView(BRect(0, 0, 78, 14), "PageBar", B_FOLLOW_NONE, B_WILL_DRAW)
{
	SetViewColor(B_TRANSPARENT_COLOR);
	BFont font = FittingFont(14);
	SetFont(&font);
}


// "Page 1", "Page 1 - 2" (STR# 5, 13), or STR# 5, 35 for none
void
TPageBar::SetPageNumbers(int32 first, int32 last)
{
	if (first == fFirst && last == fLast)
		return;
	fFirst = first;
	fLast = last;
	fText = GobeResources::IndexString(5, 35);
	if (first >= 0 && last >= 0) {
		fText = GobeResources::IndexString(5, 13);
		fText << " " << first;
		if (first != last)
			fText << " - " << last;
	}
	Invalidate();
}


// TPageBar::Draw: the text 3 above the bottom, a gray line on the left
void
TPageBar::Draw(BRect updateRect)
{
	BRect bounds = Bounds();
	SetHighColor(kPanel);
	FillRect(bounds);
	SetHighColor(0, 0, 0);
	SetLowColor(kPanel);
	DrawString(fText.String(), BPoint(bounds.left + 4, bounds.bottom - 3));
	SetHighColor(kGray);
	StrokeLine(BPoint(bounds.left, bounds.top), BPoint(bounds.left, bounds.bottom));
	StrokeLine(BPoint(bounds.left, bounds.top), BPoint(bounds.right, bounds.top));
}


// TSliderWindow: a bordered floating window 150 by 25 just above the
// zoom, its TSliderView a BSlider of 0 to 100 at the zoom's position
// (TScaleSlider +0xdc)
void
TSliderWindow::Track(TScaleSlider* scale, BPoint origin)
{
	BRect frame(origin.x, origin.y - 25, origin.x + 149, origin.y - 1);
	BWindow* window = new BWindow(frame, "SliderWindow", B_BORDERED_WINDOW_LOOK,
		B_FLOATING_ALL_WINDOW_FEEL, B_NOT_MOVABLE | B_NOT_CLOSABLE | B_NOT_ZOOMABLE
			| B_NOT_MINIMIZABLE | B_NOT_RESIZABLE | B_AVOID_FOCUS);
	BView* view = new BView(BRect(0, 0, 149, 24), "SliderView", B_FOLLOW_ALL, B_WILL_DRAW);
	view->SetViewColor(kPanel);
	BSlider* slider = new BSlider(BRect(3, 0, 146, 24), "BeSlider", NULL, NULL, 0, 100,
		B_BLOCK_THUMB, B_FOLLOW_LEFT_RIGHT | B_FOLLOW_TOP);
	slider->SetHashMarks(B_HASH_MARKS_BOTH);
	slider->SetHashMarkCount(3);
	slider->SetBarColor(kGray);
	slider->SetValue(int32(std::lround(SliderValueForZoom(scale->Scale()) * 100)));
	view->AddChild(slider);
	window->AddChild(view);
	window->Show();

	struct Tracking {
		BWindow* window;
		BView* view;
		BSlider* slider;
		BMessenger target;
	};
	Tracking* tracking = new Tracking{window, view, slider, BMessenger(scale)};
	thread_id thread = spawn_thread([](void* data) -> int32 {
		Tracking* t = static_cast<Tracking*>(data);
		BPoint where;
		uint32 buttons = 0;
		// TSliderView::Track: on the thumb, it follows the pointer; out of
		// the view (14 more below it), the slider window is torn off
		if (!t->window->Lock()) {
			delete t;
			return 0;
		}
		BRect thumb = t->slider->ThumbFrame();
		thumb = t->view->ConvertFromParent(t->slider->ConvertToParent(thumb));
		BRect keep = t->view->Bounds();
		keep.bottom += 14;
		t->view->GetMouse(&where, &buttons, false);
		t->window->Unlock();
		bool inThumb = false, tearOff = false;
		while (buttons != 0) {
			if (thumb.Contains(where)) {
				inThumb = true;
				break;
			}
			if (!keep.Contains(where)) {
				tearOff = true;
				break;
			}
			snooze(20000);
			if (!t->window->Lock())
				break;
			t->view->GetMouse(&where, &buttons, false);
			t->window->Unlock();
		}
		if (inThumb) {
			// TSliderView::trackInThumb: each new value zooms
			int32 last = -1;
			while (buttons != 0 && t->window->Lock()) {
				int32 value = t->slider->ValueForPoint(t->slider->ConvertFromParent(
					t->view->ConvertToParent(where)));
				value = std::max(int32(0), std::min(int32(100), value));
				if (value != last) {
					t->slider->SetValue(value);
					last = value;
					BMessage set(kMsgSliderValue);
					set.AddFloat("value", value / 100.0f);
					t->target.SendMessage(&set);
				}
				t->window->Unlock();
				snooze(20000);
				if (!t->window->Lock())
					break;
				t->view->GetMouse(&where, &buttons, false);
				t->window->Unlock();
			}
		} else if (tearOff && t->window->Lock()) {
			// TSliderView::trackTearOff: an outline of the view, centered
			// on the pointer, follows it until it comes back or the button
			// comes up; dropped outside, the Scale floater there
			BRect outline = t->view->Bounds();
			outline.OffsetTo(where.x - outline.Width() / 2, where.y - outline.Height() / 2);
			t->view->BeginRectTracking(outline, B_TRACK_WHOLE_RECT);
			t->window->Unlock();
			while (buttons != 0 && !keep.Contains(where)) {
				snooze(20000);
				if (!t->window->Lock())
					break;
				t->view->GetMouse(&where, &buttons, false);
				t->window->Unlock();
			}
			if (t->window->Lock()) {
				t->view->EndRectTracking();
				if (!keep.Contains(where)) {
					BMessage torn('sW05');
					torn.AddPoint("DropPoint", t->view->ConvertToScreen(BPoint(
						where.x - outline.Width() / 2, where.y - outline.Height() / 2)));
					be_app->PostMessage(&torn);
				}
				t->window->Unlock();
			}
		}
		// TSliderWindow::StartTracking: then it closes
		if (t->window->Lock())
			t->window->Quit();
		delete t;
		return 0;
	}, "slider track", B_NORMAL_PRIORITY, tracking);
	if (thread >= 0)
		resume_thread(thread);
	else {
		delete tracking;
		if (window->Lock())
			window->Quit();
	}
}


TDocScrollView::TDocScrollView(TPageView* pages)
	:
	BScrollView("pages", pages, 0, true, true, B_NO_BORDER),
	fScale(new TScaleSlider),
	fPageBar(new TPageBar)
{
	AddChild(fScale);
	AddChild(fPageBar);
}


// the zoom and the pages left of the horizontal scroll bar
void
TDocScrollView::DoLayout()
{
	BScrollView::DoLayout();
	BScrollBar* bar = ScrollBar(B_HORIZONTAL);
	if (bar == NULL)
		return;
	BRect frame = bar->Frame();
	float width = fScale->Bounds().Width() + 1 + fPageBar->Bounds().Width() + 1;
	fScale->MoveTo(frame.left, frame.top);
	fScale->ResizeTo(fScale->Bounds().Width(), frame.Height());
	fPageBar->MoveTo(frame.left + fScale->Bounds().Width() + 1, frame.top);
	fPageBar->ResizeTo(fPageBar->Bounds().Width(), frame.Height());
	bar->MoveTo(frame.left + width, frame.top);
	bar->ResizeTo(std::max(10.0f, frame.Width() - width), frame.Height());
	if (!RulersShown())
		return;
	// the rulers' 16 pixels taken from the document and its vertical bar
	float size = TGRRuler::kSize + 1;
	BRect bounds = Bounds();
	fRulers[0]->MoveTo(0, 0);
	fRulers[0]->ResizeTo(bounds.Width(), size - 1);
	fRulers[1]->MoveTo(0, size);
	fRulers[1]->ResizeTo(size - 1, std::max(0.0f, frame.top - 1 - size));
	if (BView* target = Target()) {
		BRect area = target->Frame();
		target->MoveTo(area.left + size, area.top + size);
		target->ResizeTo(std::max(0.0f, area.Width() - size), std::max(0.0f,
			area.Height() - size));
	}
	if (BScrollBar* vertical = ScrollBar(B_VERTICAL)) {
		BRect area = vertical->Frame();
		vertical->MoveTo(area.left, area.top + size);
		vertical->ResizeTo(area.Width(), std::max(0.0f, area.Height() - size));
	}
}


void
TDocScrollView::SetRulers(BView* top, BView* left)
{
	fRulers[0] = top;
	fRulers[1] = left;
	AddChild(top);
	AddChild(left);
	top->Hide();
	left->Hide();
}


bool
TDocScrollView::RulersShown() const
{
	return fRulers[0] != NULL && !fRulers[0]->IsHidden();
}


void
TDocScrollView::ShowRulers(bool show)
{
	if (fRulers[0] == NULL || show == RulersShown())
		return;
	for (BView* ruler : fRulers) {
		if (show)
			ruler->Show();
		else
			ruler->Hide();
	}
	DoLayout();
	Invalidate();
}

} // namespace gobe
