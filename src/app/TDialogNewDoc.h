// File > New… (TDialogNewDoc, DIAL/Item 2, "New Document"): "Gobe
// Productive" (STR# 5, 19-20) and the kinds of document the parts make,
// each with its part's small icon, by name; OK sends the application
// 's001' with the chosen "kind", Cancel quits if no document is open,
// Open… shows the open panel. (Word processing, spreadsheet and drawing
// documents can be made so far; the other kinds are listed disabled.)
#ifndef GOBE_TDIALOG_NEW_DOC_H
#define GOBE_TDIALOG_NEW_DOC_H

#include "TDialog.h"

class BListView;

namespace gobe {

// the kinds of document the parts make, with their icons, by name (as
// File > New… and New Sheet… list them); `select`'s selected, the kinds
// `canMake` refuses disabled
void FillPartKinds(BListView* list, uint32 select, bool (*canMake)(uint32 kind));
// the selected kind (0 for none or a disabled one)
uint32 SelectedPartKind(BListView* list);

class TDialogNewDoc : public TDialog {
public:
	// `documents`: how many documents are open
	explicit TDialogNewDoc(int32 documents);

protected:
	virtual void Init();
	virtual bool ItemHit(int32 index, BMessage* message);

private:
	uint32 Selected() const;

	int32 fDocuments;
};

} // namespace gobe

#endif
