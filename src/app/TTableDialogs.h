// The Table part's dialogs (its resources): Insert Table (DIAL/Item 100,
// TInsertTableDialog), Split Cell (101, TSplitCellsDialog) and Cell Gutter
// (102, TTableOptionsDialog). Each sends the document window its command.
#ifndef GOBE_TTABLE_DIALOGS_H
#define GOBE_TTABLE_DIALOGS_H

#include "TDialog.h"

namespace gobe {

// TInsertTableDialog: the columns (1 to 20) and rows (1 to 30), the last
// ones chosen (gInsertTableColumns, gInsertTableRows: 4 and 4 at first);
// 's00T' with "Kind" 'TBLE', "rows" and "columns"
class TDialogInsertTable : public TDialog {
public:
	explicit TDialogInsertTable(BLooper* window);

protected:
	virtual void Init();
	virtual bool ItemHit(int32 index, BMessage* message);

private:
	BLooper* fWindow;
};

// TSplitCellsDialog: the selection's columns and rows at first, each 1 to
// that or 9, whichever is more; 'sTB1' with "rows" and "columns"
class TDialogSplitCell : public TDialog {
public:
	TDialogSplitCell(BLooper* window, int32 columns, int32 rows);

protected:
	virtual void Init();
	virtual bool ItemHit(int32 index, BMessage* message);

private:
	BLooper* fWindow;
	int32 fColumns, fRows;
};

// TTableOptionsDialog: the cell gutter (0 to 8 points, the table's rounded
// up); 'sTBA' with "gutter"
class TDialogCellGutter : public TDialog {
public:
	TDialogCellGutter(BLooper* window, float gutter);

protected:
	virtual void Init();
	virtual bool ItemHit(int32 index, BMessage* message);

private:
	BLooper* fWindow;
	int32 fGutter;
};

} // namespace gobe

#endif
