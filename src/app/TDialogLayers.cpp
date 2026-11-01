#include "TDialogLayers.h"

#include <Application.h>
#include <Bitmap.h>
#include <GroupLayout.h>
#include <LayoutBuilder.h>
#include <ScrollBar.h>
#include <StringView.h>
#include <ListItem.h>
#include <ListView.h>
#include <MenuBar.h>
#include <MenuItem.h>
#include <MessageFilter.h>
#include <ScrollView.h>
#include <Window.h>

#include "GobeResources.h"
#include "TDocWindow.h"
#include "TMenu.h"
#include "TRibbon.h"

namespace gobe {

namespace {

TDialogLayers* sLayers = NULL;

// TLayerListView::MouseDown: the checks' columns, 15 points apart from 55
// points left of the list's right edge
const float kChecksFromRight = 55;
const float kCheckColumn = 15;


// TLayerListItem: the layer's name, then a check (MICN 274, 10 x 10:
// TLayerListView's) in each of its flags' columns
class LayerItem : public BStringItem {
public:
	LayerItem(const LayersState::Row& row, BBitmap* check)
		:
		BStringItem(row.name.c_str()),
		fCheck(check)
	{
		std::copy(row.flags, row.flags + 5, fFlags);
	}

	bool Flag(int32 attr) const { return attr >= 0 && attr < 5 && fFlags[attr]; }

	virtual void DrawItem(BView* owner, BRect frame, bool complete)
	{
		rgb_color background = IsSelected() ? ui_color(B_LIST_SELECTED_BACKGROUND_COLOR)
			: owner->ViewColor();
		owner->SetHighColor(background);
		owner->FillRect(frame);
		owner->SetHighColor(0, 0, 0);
		owner->SetLowColor(background);
		font_height height;
		owner->GetFontHeight(&height);
		owner->DrawString(Text(), BPoint(frame.left + 5,
			frame.top + ceilf(height.ascent) + 1));
		if (fCheck == NULL)
			return;
		owner->SetDrawingMode(B_OP_OVER);
		float x = owner->Bounds().right - kChecksFromRight + 1;
		for (int32 attr = 0; attr < 5; attr++, x += kCheckColumn) {
			if (!fFlags[attr])
				continue;
			BRect source(0, 0, 9, 9);
			BRect dest(x, frame.top + 1, x + 9, frame.top + 10);
			owner->DrawBitmap(fCheck, source, dest);
		}
		owner->SetDrawingMode(B_OP_COPY);
	}

private:
	BBitmap* fCheck;
	bool fFlags[5];
};


// TLayerListView::MouseDown: a click in a check's column toggles that flag
// of the row (TLayerListItem::SetAttribute, 'sM4J'); left of them, the
// list's own selection
class CheckFilter : public BMessageFilter {
public:
	CheckFilter(TDialogLayers* dialog)
		:
		BMessageFilter(B_MOUSE_DOWN),
		fDialog(dialog)
	{
	}

	virtual filter_result Filter(BMessage* message, BHandler** target)
	{
		BListView* list = dynamic_cast<BListView*>(*target);
		BPoint where;
		if (list == NULL || message->FindPoint("be:view_where", &where) != B_OK)
			return B_DISPATCH_MESSAGE;
		float from = list->Bounds().right - kChecksFromRight;
		if (where.x <= from)
			return B_DISPATCH_MESSAGE;
		int32 row = list->IndexOf(where);
		LayerItem* item = dynamic_cast<LayerItem*>(list->ItemAt(row));
		int32 attr = int32((where.x - from) / kCheckColumn);
		if (item == NULL || attr >= 5)
			return B_SKIP_MESSAGE;
		BMessage set('sM4J');
		set.AddInt32("index", row);
		set.AddInt32("attr", attr);
		set.AddInt32("value", item->Flag(attr) ? 0 : 1);
		if (TDocWindow* window = TDocWindow::Front())
			window->PostMessage(&set);
		return B_SKIP_MESSAGE;
	}

private:
	TDialogLayers* fDialog;
};

} // namespace


TDialogLayers::TDialogLayers()
	:
	TDialog(203, 203, 'APPL', NULL)
{
	sLayers = this;
}


TDialogLayers::~TDialogLayers()
{
	if (sLayers == this)
		sLayers = NULL;
}


TDialogLayers*
TDialogLayers::Open()
{
	return sLayers;
}


void
TDialogLayers::Refresh()
{
	if (sLayers != NULL)
		sLayers->PostMessage('sD=L');
}


BListView*
TDialogLayers::List() const
{
	BView* view = Item(5);
	if (BScrollView* scroll = dynamic_cast<BScrollView*>(view))
		return dynamic_cast<BListView*>(scroll->Target());
	return dynamic_cast<BListView*>(view);
}


namespace {

// the layout holding `view`, searched from `layout` down
BLayout*
LayoutOf(BLayout* layout, BView* view, int32& index)
{
	if (layout == NULL)
		return NULL;
	index = layout->IndexOfView(view);
	if (index >= 0)
		return layout;
	for (int32 i = 0; i < layout->CountItems(); i++)
		if (BLayout* inner = dynamic_cast<BLayout*>(layout->ItemAt(i)))
			if (BLayout* found = LayoutOf(inner, view, index))
				return found;
	return NULL;
}

} // namespace


// TDialogLayers::Init: the View and Layer menus (MENU 220, 221) in a menu
// bar; the list's font 9; at least 125 by 80
void
TDialogLayers::Init()
{
	for (int32 i = 0; BWindow* window = be_app->WindowAt(i); i++)
		if (dynamic_cast<TDocWindow*>(window) != NULL)
			AddToSubset(window);
	float minWidth, maxWidth, minHeight, maxHeight;
	GetSizeLimits(&minWidth, &maxWidth, &minHeight, &maxHeight);
	SetSizeLimits(125, maxWidth, 80, maxHeight);
	BMenuBar* bar = new BMenuBar("mMenuBar");
	fViews = new TMenu(220);
	fLayers = new TMenu(221);
	bar->AddItem(fViews);
	bar->AddItem(fLayers);
	if (BGroupLayout* layout = dynamic_cast<BGroupLayout*>(GetLayout()))
		layout->AddView(0, bar);
	if (BListView* list = List()) {
		BFont font;
		list->GetFont(&font);
		font.SetSize(9);
		list->SetFont(&font);
		BMessage* select = new BMessage('APPL' + 5);
		select->AddInt32("dialog", 203);
		select->AddInt32("index", 5);
		list->SetSelectionMessage(select);
		BMessage* invoke = new BMessage('APPL' + 5);
		invoke->AddInt32("dialog", 203);
		invoke->AddInt32("index", 5);
		invoke->AddBool("Invoke", true);
		list->SetInvocationMessage(invoke);
		list->AddFilter(new CheckFilter(this));
	}
	// TDialogLayers::InitPostRecalc: the view's name left, the four icons
	// right, side by side over their checks' columns (each 15 points; the
	// checks 10 points wide start 54 points left of the list's right edge)
	int32 at, was;
	BLayout* old = LayoutOf(GetLayout(), Item(1), was);
	BLayout* layout = LayoutOf(GetLayout(), Item(5), at);
	if (layout != NULL) {
		BGroupLayout* row = new BGroupLayout(B_HORIZONTAL, 0);
		layout->AddItem(at, row);
		for (int32 i = 0; i <= 4; i++) {
			BView* view = Item(i);
			if (view == NULL)
				continue;
			view->RemoveSelf();
			if (i == 0) {
				view->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
				row->AddView(view);
			} else {
				view->SetExplicitMinSize(BSize(15, 15));
				view->SetExplicitMaxSize(BSize(15, 15));
				row->AddView(view);
			}
		}
		row->AddItem(BSpaceLayoutItem::CreateHorizontalStrut(B_V_SCROLL_BAR_WIDTH - 2));
		// (the resource's row, emptied)
		if (old != NULL && old != layout && old->CountItems() == 0 && old->RemoveSelf())
			delete old;
	}
	Update();
}


// TDialogLayers::UpdateContents
void
TDialogLayers::Update()
{
	LayersState state;
	bool drawing = false;
	TDocWindow* window = TDocWindow::Front();
	if (window != NULL && window->LockWithTimeout(100000) == B_OK) {
		drawing = window->LayersState(state);
		window->Unlock();
	}
	fState = state;
	BListView* list = List();
	if (list == NULL || fViews == NULL || fLayers == NULL)
		return;
	BMessage* select = list->SelectionMessage() != NULL
		? new BMessage(*list->SelectionMessage()) : NULL;
	list->SetSelectionMessage(NULL);
	while (BListItem* item = list->RemoveItem(int32(0)))
		delete item;
	while (fViews->CountItems() > 4)
		delete fViews->RemoveItem(fViews->CountItems() - 1);
	SetTextItem(0, "");
	static BBitmap* check = TMICNElement::LoadIcon(274);
	if (drawing) {
		for (const LayersState::Row& row : state.rows)
			list->AddItem(new LayerItem(row, check));
		list->Select(int32(state.activeLayer));
		for (size_t v = 0; v < state.views.size(); v++) {
			BMessage* show = new BMessage(10);
			show->AddInt32("index", int32(v));
			BMenuItem* item = new BMenuItem(state.views[v].c_str(), show);
			item->SetTarget(this);
			if (v == state.shownView) {
				item->SetMarked(true);
				SetTextItem(0, state.views[v].c_str());
			}
			fViews->AddItem(item);
		}
	}
	if (BMenuItem* item = fViews->ItemAt(1))
		item->SetEnabled(drawing && state.views.size() > 1);
	if (BMenuItem* item = fLayers->ItemAt(1))
		item->SetEnabled(drawing && state.layers > 1);
	list->Invalidate();
	list->SetSelectionMessage(select);
}


void
TDialogLayers::Send(uint32 what, int32 index)
{
	TDocWindow* window = TDocWindow::Front();
	if (window == NULL)
		return;
	BMessage message(what);
	if (index >= 0)
		message.AddInt32("index", index);
	window->PostMessage(&message);
}


void
TDialogLayers::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case 'sD=L':
			Update();
			return;
		// TFloaterMgr 'sE05': a new document window joins the subset
		case 'sE05':
		{
			BWindow* window;
			if (message->FindPointer("window", (void**)&window) == B_OK)
				AddToSubset(window);
			return;
		}
		// TDialogLayers::ItemHit's menu commands (the MENUs' 0 to 5; a
		// view's item 10)
		case 0:		// New View
			Send('sM4K');
			return;
		case 1:		// Delete View
			if (fState.shownView < fState.views.size())
				Send('sM4L', int32(fState.shownView));
			return;
		case 2:		// Rename View… (TDialogLayers::RenameMarkedGRView)
			if (fState.shownView < fState.views.size() && TDocWindow::Front() != NULL)
				(new TDialogLayerName(TDocWindow::Front(), 'sM4M', int32(fState.shownView),
					GobeResources::IndexString(2, 3).String(),
					fState.views[fState.shownView].c_str()))->RunDialog();
			return;
		case 3:		// New Layer
			Send('sM4F');
			return;
		case 4:		// Delete Layer
			if (List() != NULL && List()->CurrentSelection() >= 0)
				Send('sM4G', List()->CurrentSelection());
			return;
		case 5:		// Rename Layer… (TDialogLayers::RenameSelectedLayer)
			if (List() != NULL && List()->CurrentSelection() >= 0)
				Send('sM4H', List()->CurrentSelection());
			return;
		case 10:	// a view
			Send('sM4N', message->GetInt32("index", 0));
			return;
	}
	TDialog::MessageReceived(message);
}


// TDialogLayers::ItemHit: the list: a double click renames the layer
// (TLayerListView::SelectionChanged: a row chosen is the active layer)
bool
TDialogLayers::ItemHit(int32 index, BMessage* message)
{
	if (index != 5 || List() == NULL)
		return false;
	int32 row = List()->CurrentSelection();
	if (message->GetBool("Invoke", false)) {
		if (row >= 0)
			Send('sM4H', row);
		return false;
	}
	if (row < 0)
		List()->Select(int32(fState.activeLayer));
	else if (size_t(row) != fState.activeLayer)
		Send('sM4I', row);
	return false;
}


TDialogLayerName::TDialogLayerName(BLooper* window, uint32 what, int32 index,
		const char* title, const char* name)
	:
	TDialog(31, 31, 'APPL', NULL),
	fWindow(window),
	fWhat(what),
	fIndex(index),
	fTitle(title),
	fName(name)
{
}


void
TDialogLayerName::Init()
{
	SetTitle(fTitle.String());
	SetLabelItem(2, "Name:");
	SetTextItem(2, fName.String());
	MakeFocusItem(2);
}


// TDialogLayerName::ValidateAndSendMsg
bool
TDialogLayerName::ItemHit(int32 index, BMessage* message)
{
	if (index == 0) {
		BMessage rename(fWhat);
		rename.AddInt32("index", fIndex);
		rename.AddString("name", TextItem(2).String());
		fWindow->PostMessage(&rename);
	}
	return TDialog::ItemHit(index, message);
}

} // namespace gobe
