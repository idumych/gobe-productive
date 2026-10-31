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

class TSheetBar : public BView {
public:
	TSheetBar(const Document& document);

	virtual void Draw(BRect updateRect);
	virtual void MouseDown(BPoint where);
	virtual BSize MinSize();
	virtual BSize MaxSize();
	virtual BSize PreferredSize();

	// the tabs again (the shown sheet changed)
	void Update();

private:
	BRect TabFrame(int32 index) const;

	const Document& fDocument;
};

} // namespace gobe

#endif
