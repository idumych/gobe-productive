// TRibbon: a bar of elements under the menu bar (TRibbon, TRibbonElement,
// TMICNElement, TMICNButtonElement): 24 high, panel gray, its elements
// drawn by themselves; a disabled one is washed out, a selected one (a
// sticky button of a family, like the alignments) darkened.
#ifndef GOBE_TRIBBON_H
#define GOBE_TRIBBON_H

#include <Bitmap.h>
#include <Message.h>
#include <PopUpMenu.h>
#include <View.h>

#include <memory>
#include <string>
#include <vector>

namespace gobe {

class ResourceFile;
class TRibbon;

class TRibbonElement {
public:
	TRibbonElement(uint32 id, BRect rect);
	virtual ~TRibbonElement() {}

	virtual void Draw(BView* view, bool down) = 0;
	// let go over it (TRibbonElement::MouseDown returned true)
	virtual void Invoke(TRibbon* ribbon) {}
	virtual bool Clickable() const { return false; }
	// pressed: true if the element handled it (a menu popped up)
	virtual bool MouseDown(TRibbon* ribbon, BPoint where) { return false; }

	uint32 id;
	BRect rect;
	uint32 family = 0;			// sticky buttons of a family: one is selected
	bool selectable = false;
	bool selected = false;
	bool enabled = true;
	std::string tip;
};

// TMICNElement: a 16 x 16 icon (MICN, the BeOS palette)
class TMICNElement : public TRibbonElement {
public:
	TMICNElement(uint32 id, BPoint where, int32 micn);
	TMICNElement(uint32 id, BPoint where, BBitmap* icon);

	virtual void Draw(BView* view, bool down);
	void SetIcon(BBitmap* icon);

	// from the application's resources, or `resources` (a part's)
	static BBitmap* LoadIcon(int32 micn, const ResourceFile* resources = NULL);

protected:
	std::unique_ptr<BBitmap> fIcon;
};

// TMICNButtonElement: an icon on a 20 x 20 button, sending `message` (with
// "modifiers" and "buttons") to the window
class TMICNButtonElement : public TMICNElement {
public:
	TMICNButtonElement(uint32 id, BPoint where, int32 micn, BMessage* message);
	TMICNButtonElement(uint32 id, BPoint where, BBitmap* icon, BMessage* message);

	virtual void Draw(BView* view, bool down);
	virtual void Invoke(TRibbon* ribbon);
	virtual bool Clickable() const { return true; }

private:
	std::unique_ptr<BMessage> fMessage;
};

// TMenuElement and TMICNMenuElement: a button with a label or an icon and
// the pop-up indicator; pressing it pops up its menu below it, filled by
// FillMenu (TMenuElement::FixMenu: its items, marked as the selection has
// them), and what is chosen is sent to the window
class TMenuElement : public TRibbonElement {
public:
	TMenuElement(uint32 id, BRect rect, const char* label, BBitmap* icon = NULL);

	virtual void Draw(BView* view, bool down);
	virtual bool MouseDown(TRibbon* ribbon, BPoint where);
	virtual void FillMenu(BPopUpMenu* menu, TRibbon* ribbon) = 0;

	std::string label;

protected:
	std::unique_ptr<BBitmap> fIcon;
};

class TRibbon : public BView {
public:
	TRibbon(const char* name);

	virtual void Draw(BRect updateRect);
	virtual void MouseDown(BPoint where);
	virtual void MouseMoved(BPoint where, uint32 transit, const BMessage* dragMessage);
	virtual void MouseUp(BPoint where);
	virtual BSize MinSize();
	virtual BSize MaxSize();
	virtual BSize PreferredSize();

	// the state of the window's document as it is now (redrawn if changed)
	virtual void Update() {}

	void AddElement(TRibbonElement* element);
	void RemoveAllElements();
	TRibbonElement* GetElement(uint32 id) const;
	void SetEnabled(uint32 id, bool enabled);
	// TRibbon::SelectElement: `id` selected, the others of `family` not
	// (an id of none of them deselects them all)
	void SelectElement(uint32 id, uint32 family);
	// TRibbon::DrawElement
	void DrawElement(TRibbonElement* element, bool down = false);
	// test hook: element `id` clicked (false if it can't be), what it sends
	// handled at once
	bool Click(uint32 id);
	// a message for the window: posted (handled at once in a test hook)
	void Send(BMessage* message);

	// the outlines (DrawElementRect, DrawElementOutlineUp, ...Down)
	static void DrawOutlineUp(BView* view, BRect rect, bool selected);
	static void DrawOutlineDown(BView* view, BRect rect);
	// DrawPopUpIndicator: a small red triangle in the bottom right corner
	static void DrawPopUpIndicator(BView* view, BRect rect);

private:
	TRibbonElement* FindElement(BPoint where) const;

	std::vector<std::unique_ptr<TRibbonElement>> fElements;
	TRibbonElement* fPressed = NULL;
	bool fPressedDown = false;
	std::string fTip;
protected:
	bool fSynchronous = false;
};

} // namespace gobe

#endif
