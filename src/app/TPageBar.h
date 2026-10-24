// The tools left of the horizontal scroll bar: the zoom (TScaleSlider:
// minus and plus step through 1/64 .. 64, a percent between them that
// switches back and forth between 100% and the last other zoom; 's003'
// with "scale" to the window) and the pages in view (TPageBar: "Page 1" or
// "Page 1 - 2"); TDocScrollView makes room for them.
#ifndef GOBE_TPAGEBAR_H
#define GOBE_TPAGEBAR_H

#include <ScrollView.h>
#include <String.h>
#include <View.h>

class BMessageRunner;

namespace gobe {

class TPageView;

class TScaleSlider : public BView {
public:
	TScaleSlider();

	virtual void Draw(BRect updateRect);
	virtual void MouseDown(BPoint where);
	virtual void MouseUp(BPoint where);
	virtual void MessageReceived(BMessage* message);

	// TScaleSlider::SetScale: shown, and sent to the window if `send`
	void SetScale(float scale, bool send);
	float Scale() const { return fScale; }

private:
	BRect ButtonRect(bool minus) const;
	void DrawButton(bool minus, bool down);
	float NextStepScale(bool minus) const;
	void Step();

	float fScale = 1;
	float fOther = 1;			// TScaleSlider +0xe4: the last zoom not 100%
	int fPressed = 0;			// -1 minus, 1 plus
	bool fFine = false;			// by 1% (secondary button or Option)
	BMessageRunner* fRepeat = NULL;
};

class TPageBar : public BView {
public:
	TPageBar();

	virtual void Draw(BRect updateRect);

	// TPageBar::SetPageNumbers: the first and last page in view (from 1;
	// -1 for none)
	void SetPageNumbers(int32 first, int32 last);

private:
	int32 fFirst = 0, fLast = 0;
	BString fText;
};

class TDocScrollView : public BScrollView {
public:
	TDocScrollView(TPageView* pages);

	virtual void DoLayout();

	TScaleSlider* Scale() const { return fScale; }
	TPageBar* Pages() const { return fPageBar; }

private:
	TScaleSlider* fScale;
	TPageBar* fPageBar;
};

} // namespace gobe

#endif
