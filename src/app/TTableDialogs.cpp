#include "TTableDialogs.h"

#include <algorithm>
#include <cmath>

#include "GobeResources.h"

namespace gobe {

namespace {

const ResourceFile&
TableResources()
{
	return GobeResources::Part("Table");
}


// gInsertTableColumns, gInsertTableRows
int32 sInsertColumns = 4;
int32 sInsertRows = 4;

} // namespace


TDialogInsertTable::TDialogInsertTable(BLooper* window)
	:
	TDialog(100, 100, 'TBLE', NULL, &TableResources()),
	fWindow(window)
{
}


void
TDialogInsertTable::Init()
{
	SetTextItem(2, BString().SetToFormat("%d", int(sInsertColumns)).String());
	SetTextItem(3, BString().SetToFormat("%d", int(sInsertRows)).String());
	MakeFocusItem(2);
}


bool
TDialogInsertTable::ItemHit(int32 index, BMessage* message)
{
	if (index == 0) {
		if (!CheckTextItem(2, &sInsertColumns, 1, 20)
				|| !CheckTextItem(3, &sInsertRows, 1, 30))
			return true;
		BMessage insert('s00T');
		insert.AddInt32("Kind", 'TBLE');
		insert.AddInt32("rows", sInsertRows);
		insert.AddInt32("columns", sInsertColumns);
		fWindow->PostMessage(&insert);
	}
	return TDialog::ItemHit(index, message);
}


TDialogSplitCell::TDialogSplitCell(BLooper* window, int32 columns, int32 rows)
	:
	TDialog(101, 101, 'TBLE', NULL, &TableResources()),
	fWindow(window),
	fColumns(columns),
	fRows(rows)
{
}


void
TDialogSplitCell::Init()
{
	SetTextItem(2, BString().SetToFormat("%d", int(fColumns)).String());
	SetTextItem(3, BString().SetToFormat("%d", int(fRows)).String());
	MakeFocusItem(2);
}


bool
TDialogSplitCell::ItemHit(int32 index, BMessage* message)
{
	if (index == 0) {
		int32 columns, rows;
		if (!CheckTextItem(2, &columns, 1, std::max<int32>(fColumns, 9))
				|| !CheckTextItem(3, &rows, 1, std::max<int32>(fRows, 9)))
			return true;
		BMessage split('sTB1');
		split.AddInt32("rows", rows);
		split.AddInt32("columns", columns);
		fWindow->PostMessage(&split);
	}
	return TDialog::ItemHit(index, message);
}


TDialogCellGutter::TDialogCellGutter(BLooper* window, float gutter)
	:
	TDialog(102, 102, 'TBLE', NULL, &TableResources()),
	fWindow(window),
	// (ROUND(gutter + 0.5): the FPU's rounding, to even)
	fGutter(int32(std::nearbyint(gutter + 0.5f)))
{
}


void
TDialogCellGutter::Init()
{
	SetTextItem(2, BString().SetToFormat("%d", int(fGutter)).String());
	MakeFocusItem(2);
}


bool
TDialogCellGutter::ItemHit(int32 index, BMessage* message)
{
	if (index == 0) {
		if (!CheckTextItem(2, &fGutter, 0, 8))
			return true;
		BMessage gutter('sTBA');
		gutter.AddInt32("gutter", fGutter);
		fWindow->PostMessage(&gutter);
	}
	return TDialog::ItemHit(index, message);
}

} // namespace gobe
