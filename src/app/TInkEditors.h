// The Inks window's editors (TDialogNewColor's): the colour, pattern and
// gradient dialogs (TDialogEditColor DIAL/Item 6, TDialogEditPattern 3,
// TDialogEditGradient 4), what Cut and Copy keep for Paste (TDialogNewColor
// +0x80 the pen, +0x84 the ink, +0x88 the kind copied) and the pen view's
// width controls (TPenView: the width, its buttons and slider).
#ifndef GOBE_TINKEDITORS_H
#define GOBE_TINKEDITORS_H

#include <string>

#include <Message.h>
#include <View.h>

#include "InkEdit.h"
#include "Pens.h"

class BLooper;
class BControl;
class BSlider;
class BStringView;

namespace gobe {

// TDialogNewColor::DoEditInkDialog: the editor of `minorType` (0 a colour,
// 1 a pattern, 2 a gradient) on `ink` (NULL: white, a white pattern,
// black to white; DoNewInk passes the selected ink). OK posts a copy of
// `reply` to `target` with "ink" (B_RAW_TYPE: InkBytes), and a colour's
// "color" (B_RGB_COLOR_TYPE).
// A new ink is `reply`'s what 'sfI0' (DoNewInk: its title "New …"), an
// edited one's 'sfI2' (DoEditInk, with "ink name" and "ink index").
void EditInk(int minorType, const Ink* ink, BLooper* target, const BMessage& reply);

// test hook: the open editor's controls ("color R G B", "cell ROW COL" in
// the pattern's edit view, "click X Y" in the gradient's, "tool 5|6|7",
// "size COLS ROWS",
// "type GDdr|GDrl", "angle TEXT", "colors N", "stop I R G B", "ok",
// "cancel", "revert", "show")
void InkEditorScript(const std::string& command);

// Cut and Copy's ink or pen, for Paste (TDialogNewColor::DoCopy, DoPaste)
class InkClipboard {
public:
	// DoCopy: the selected ink of minor type `minorType` (0-2) or pen (3)
	static void CopyInk(int minorType, const Ink& ink);
	static void CopyPen(const PenType& pen);
	// DoPaste's "RGBColor" (a colour dropped or pasted from elsewhere):
	// it becomes the colour copied; true if the message had one
	static bool TakeColor(const BMessage* message);
	// the kind last copied (+0x88: Paste is enabled with it showing), -1
	// for none
	static int Kind();
	static bool HasInk();
	static const Ink& CopiedInk();
	static bool HasPen();
	static const PenType& CopiedPen();
};

// TPenView's width controls: the width ("hairline" or "N.N pt"), smaller
// and larger (MICN 0x22, 0x27: the pen size one point less or more,
// 'PEN-' / 'PEN+') and a vertical slider (0-64; past 31 a step is 7
// points: 'PEN#', 'PEN%' while dragging). Each posts the pen size to the
// front document ('sf09' with the size as "value").
class TPenWidthView : public BView {
public:
	TPenWidthView();

	virtual void AttachedToWindow();
	virtual void MessageReceived(BMessage* message);

	// TPenView::SetPenWidth: 0-255, the label and the slider
	void SetPenWidth(float width);
	float PenWidth() const { return fWidth; }
	// TPenView::UpdateContents: the front document's selection's pen
	// size (unless the slider is being dragged)
	void Update();

private:
	void Post(float width);

	float fWidth = 0;
	BStringView* fLabel = NULL;
	BControl* fSmaller = NULL;
	BControl* fLarger = NULL;
	BSlider* fSlider = NULL;
};

// TInkView's opacity controls, right of the inks: the opacity ("100%"),
// more and less opaque (MICN 0x21, 0x20: 'TRN-' / 'TRN+', a hundredth
// more or less) and a vertical slider of the transparency (0-100: 'TRN#',
// 'TRN%' while dragging), all to the window (TDialogNewColor::
// MessageReceived), which posts them to the front document.
class TInkOpacityView : public BView {
public:
	TInkOpacityView();

	virtual void AttachedToWindow();

	// TInkView::SetOpacity: 0-100, the label and the slider (at 100 less)
	void SetOpacity(int32 opacity);
	int32 Opacity() const { return fOpacity; }
	// the slider's transparency
	int32 Transparency() const;

private:
	int32 fOpacity = 100;
	BStringView* fLabel = NULL;
	BControl* fMore = NULL;
	BControl* fLess = NULL;
	BSlider* fSlider = NULL;
};

} // namespace gobe

#endif
