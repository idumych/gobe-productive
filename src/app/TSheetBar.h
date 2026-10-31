/*
 * The SheetBar (TSheetBar): a tab per sheet of the document; clicking one
 * shows that sheet.
 */
#ifndef T_SHEET_BAR_H
#define T_SHEET_BAR_H

#include <View.h>

namespace gobe {

struct Document;

// TSheetBar's TTabControl: a click on a tab sends 's00E' with "ActiveTab"
// (TSheetMgr::HandleMessage switches the sheet)
static const uint32 kMsgSheetSwitch = 's00E';
// Rename Sheet… ('s00D'): the dialog, or with "name" the sheet renamed
static const uint32 kMsgSheetRename = 's00D';
// New Sheet… ('s00B'): the dialog, or with "name" and "kind" the sheet added
static const uint32 kMsgSheetNew = 's00B';
// Delete Sheet ('s00C'): the shown sheet, when another is left
static const uint32 kMsgSheetDelete = 's00C';
// a tab dragged to another place ('s00N' with "OldIndex", "NewIndex")
static const uint32 kMsgSheetReorder = 's00N';

class TSheetBar : public BView {
public:
	TSheetBar(const Document& document);

	virtual void Draw(BRect updateRect);
	virtual void MouseDown(BPoint where);
	virtual void MouseMoved(BPoint where, uint32 transit, const BMessage* dragged);
	virtual void MouseUp(BPoint where);
	virtual BSize MinSize();
	virtual BSize MaxSize();
	virtual BSize PreferredSize();

	// the tabs again (the shown sheet changed)
	void Update();

private:
	BRect TabFrame(int32 index) const;
	int32 TabAt(BPoint where) const;
	// the arrows that scroll the tabs, when they don't all fit
	bool Overflows() const;
	BRect ArrowFrame(bool right) const;
	void Reveal();

	const Document& fDocument;
	int32 fFirst = 0;			// the first tab shown (the tabs scrolled)
	bool fReveal = false;		// bring the shown sheet's tab into view
	int32 fPressed = -1;		// the tab the mouse went down on
	int32 fDropAt = -1;			// where it would go, dragged
};

} // namespace gobe

#endif
