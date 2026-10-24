// TWPRuler: the word processing ribbon's text ruler (TWPRuler, TRuler,
// TWPRulerElement and its kinds, TWPRulerTrack). Over the page view, it
// shows the ticks and numbers of the page the caret is on in the sheet's
// unit, and for the paragraph the caret is in its left indent (a filled
// triangle), first line indent (a bar), right indent and tab stops (open
// triangles). Dragging them changes the selected paragraphs; a click
// adds a tab; a double click on a tab edits it (Edit Tab); the right button
// gives the tab menu (MENU 120) on a tab, else the unit menu (MENU 241,
// with Set Ruler Divisions…).
#ifndef GOBE_TWPRULER_H
#define GOBE_TWPRULER_H

#include <View.h>

#include <string>
#include <vector>

#include "Styles.h"

namespace gobe {

class TPageView;

// Edit Tab's OK (TDialogTabEdit::ItemHit 's00O'): "tab" (the stop's index),
// "position" (from the text's edge), "type", "alignChar", "fill",
// "fillChar"
static const uint32 kMsgModifyTab = 's00O';

class TWPRuler : public BView {
public:
	TWPRuler(TPageView* pages);

	virtual void Draw(BRect updateRect);
	virtual void MouseDown(BPoint where);
	virtual void MouseMoved(BPoint where, uint32 transit, const BMessage* dragMessage);
	virtual void MouseUp(BPoint where);
	virtual void MessageReceived(BMessage* message);
	virtual BSize MinSize();
	virtual BSize MaxSize();
	virtual BSize PreferredSize();

	// TWPRuler::updatePositions: the caret's frame, paragraph and the
	// view's scrolling as they are now (redrawn if they changed)
	void Update();

	// test hook (TPageView::RunScript "ruler ..."): "markers" prints where
	// the markers are (from the text's edge); "drag left|indent|right|tabN
	// dx [dy [modifiers]]" drags one; "click x [modifiers]" clicks the
	// ruler x points from the text's edge; "edit N" double-clicks tab N;
	// "tabmenu N command" chooses a
	// tab menu (MENU 120) command for tab N; "unit U", "divisions D" set
	// what the unit menu and Set Ruler Divisions set
	void RunScript(const std::string& command);

private:
	enum { kLeft = 0, kIndent = 1, kRight = 2, kTab = 3 };
	struct Element {
		int kind;
		float x;			// the page view's (unzoomed) coordinates
		TabStop tab;		// kTab: position from the text's edge
		bool deleteState;	// a tab dragged off the ruler
	};

	BRect ElementRect(const Element& e) const;
	int FindElement(BPoint where) const;
	void DrawElement(const Element& e);
	void DrawTicks(float start, float end);
	void Ticks(float zoom, float& major, float& minor, int32& count, int32& divisions) const;
	float GridTo(float x) const;
	void Pin(const Element& e, float& x) const;
	bool SetElementPosition(int index, float x, bool force, bool pin);
	void Drag(BPoint from, BPoint to, int32 clicks, uint32 buttons, uint32 modifiers,
		bool live);
	void HandleTabCommand(int index, uint32 command);
	void Track(BPoint where, uint32 modifiers);
	void EndTrack();
	void AddTab(float x);
	void ModifyTab(int index, const TabStop& tab);
	void RemoveTab(int index);
	void TabPopUp(int index, BPoint where);
	void UnitPopUp(BPoint where);
	void EditTab(int index);
	float RulerX(float position) const;	// a position from the text's edge
	void Apply(const std::vector<float>& removeTabs, const std::vector<Property>& add,
		const std::string& label);
	int Unit() const;
	int32 Divisions() const;

	TPageView* fPages;
	bool fActive = false;
	float fStart = 0, fEnd = 0;			// the text's edges (TWPRuler +0xdc, +0xe0)
	float fPageLeft = 0, fPageRight = 0;
	float fScroll = 0;
	float fZoom = 1;
	std::vector<Element> fElements;		// left, indent, right, then the tabs
	// what Update last saw
	TextStyle fStyle;
	int fUnit = -1;
	int32 fDivisions = -1;
	// TWPRuler +0xf0: what a click adds (the last tab edited)
	TabStop fDefaultTab;

	// dragging (TWPRuler::MouseTrack)
	int fTracking = -1;
	float fLast = 0;
	bool fStepStarted = false;
};

} // namespace gobe

#endif
