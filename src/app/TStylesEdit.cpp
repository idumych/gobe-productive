// Format > Edit Styles….
#include "TStylesEdit.h"

#include <Alert.h>
#include <Application.h>
#include <Autolock.h>
#include <GroupLayout.h>
#include <Locker.h>
#include <MenuBar.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <OutlineListView.h>
#include <ScrollView.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "DocArchive.h"
#include "Document.h"
#include "GobeResources.h"
#include "StyleEdit.h"
#include "Styles.h"
#include "TDocWindow.h"
#include "TFormatBar.h"
#include "TMenu.h"
#include "TPreferences.h"

namespace gobe {

namespace {

TDialogStylesEdit* sOpen = NULL;
// TDoc::EditingStyle asks from the document window's thread
BLocker sEditingLock("edit style");
const Document* sEditingDocument = NULL;
std::string sEditingName;

const uint32 kChooseRow = 'Gsrw';
const uint32 kDump = 'Gsdp';
const char* const kTargetStyleStr = "Target Style";
const char* const kStyleBaseStr = "Style Base";
const rgb_color kSelected = {0xa0, 0xa0, 0xa0, 255};


std::string
StringOf(int32 id, int index)
{
	return GobeResources::IndexString(id, index).String();
}


// TStyleManager::StyleTypeName: the kinds the text part registers
std::string
StyleTypeName(uint32 type)
{
	if (type == Sig("Para"))
		return StringOf(1309, 0);
	if (type == Sig("Basi"))
		return StringOf(13, 7);
	return FourCCString(type);
}


// TStyleListItem: a heading (level 0) or a style, its marks: applied to
// part of the selection, to all of it, the active style (the marks'
// shapes are approximate)
class TStyleListItem : public BListItem {
public:
	TStyleListItem(const std::string& label, uint32 level, bool expanded, uint32 flags)
		:
		BListItem(level, expanded),
		fLabel(label),
		fFlags(flags)
	{
	}

	virtual void DrawItem(BView* owner, BRect frame, bool complete)
	{
		rgb_color back = IsSelected() ? kSelected : owner->ViewColor();
		owner->SetHighColor(back);
		owner->FillRect(frame);
		float middle = floorf((frame.top + frame.bottom) / 2);
		float left = frame.left;
		if (fFlags & 2) {
			owner->SetHighColor(255, 0, 0);
			owner->StrokeLine(BPoint(left, middle - 1), BPoint(left + 5, middle - 1));
			owner->StrokeLine(BPoint(left, middle), BPoint(left + 5, middle));
		} else if (fFlags & 4) {
			owner->SetHighColor(255, 0, 0);
			owner->StrokeLine(BPoint(left + 2, middle), BPoint(left + 4, middle + 2));
			owner->StrokeLine(BPoint(left + 4, middle + 2), BPoint(left + 8, middle - 3));
		} else if (fFlags & 8) {
			owner->SetHighColor(kSelected);
			owner->FillEllipse(BRect(left + 2, middle - 2, left + 5, middle + 1));
		}
		owner->SetHighColor(0, 0, 0);
		owner->SetLowColor(back);
		owner->DrawString(fLabel.c_str(),
			BPoint(left + ((fFlags & 1) ? 4 : 9), frame.bottom - 2));
	}

	const std::string& Label() const { return fLabel; }
	uint32 Flags() const { return fFlags; }

private:
	std::string fLabel;
	uint32 fFlags;
};


// TNamedDataListItem (kind -1) and TPropertyListItem: own (1, black),
// inherited (2, blue) or the selection's own (0, red, "+ "); a value, or
// an ink's swatch
class TPropertyItem : public BListItem {
public:
	TPropertyItem(const std::string& name, const std::string& value, int kind)
		:
		fName(kind == 0 ? "+ " + name : name),
		fValue(value),
		fKind(kind)
	{
	}

	void SetInk(rgb_color color)
	{
		fInk = true;
		fInkColor = color;
	}

	virtual void DrawItem(BView* owner, BRect frame, bool complete)
	{
		rgb_color back = IsSelected() ? kSelected : owner->ViewColor();
		owner->SetHighColor(back);
		owner->FillRect(frame);
		rgb_color text = {0, 0, 0, 255};
		if (fKind == 2)
			text = {0, 0, 255, 255};
		else if (fKind == 0)
			text = {255, 0, 0, 255};
		owner->SetHighColor(text);
		owner->SetLowColor(back);
		float x = frame.left + 5;
		owner->DrawString(fName.c_str(), BPoint(x, frame.bottom - 2));
		float nameEnd = x + owner->StringWidth(fName.c_str()) + 2;
		if (fInk) {
			BRect swatch(frame.right - 2 - 35, frame.top + 1, frame.right - 2, frame.bottom - 1);
			owner->SetHighColor(fInkColor);
			owner->FillRect(swatch);
			owner->SetHighColor(0, 0, 0);
			owner->StrokeRect(swatch);
			return;
		}
		float valueX = frame.right - owner->StringWidth(fValue.c_str()) - 2;
		if (valueX < nameEnd + 10)
			valueX = nameEnd + 10;
		owner->DrawString(fValue.c_str(), BPoint(valueX, frame.bottom - 2));
	}

	std::string Text() const
	{
		return fName + " = " + (fInk ? "(ink)" : fValue);
	}

private:
	std::string fName, fValue;
	int fKind;
	bool fInk = false;
	rgb_color fInkColor;
};


BListView*
ListOf(BView* view)
{
	if (BScrollView* scroll = dynamic_cast<BScrollView*>(view))
		return dynamic_cast<BListView*>(scroll->Target());
	return dynamic_cast<BListView*>(view);
}


void
SetSmallFont(BView* view)
{
	BFont font;
	view->GetFont(&font);
	font.SetSize(9);
	view->SetFont(&font);
}

} // namespace


TDialogStylesEdit::TDialogStylesEdit(const char* name)
	:
	TDialog(10, 10, 'APPL', NULL),
	fName(name != NULL ? name : "")
{
	sOpen = this;
}


TDialogStylesEdit::~TDialogStylesEdit()
{
	if (sOpen == this)
		sOpen = NULL;
	{
		BAutolock lock(sEditingLock);
		sEditingDocument = NULL;
		sEditingName.clear();
	}
	if (TDocWindow* window = TDocWindow::Front())
		window->PostMessage('sD=L');
}


TDialogStylesEdit*
TDialogStylesEdit::Open()
{
	return sOpen;
}


void
TDialogStylesEdit::Refresh()
{
	if (sOpen != NULL)
		sOpen->PostMessage('sD=L');
}


std::string
TDialogStylesEdit::EditingStyle(const Document* document)
{
	BAutolock lock(sEditingLock);
	if (document == NULL || document != sEditingDocument)
		return "";
	return sEditingName;
}


void
TDialogStylesEdit::ChooseRow(int32 row, bool invoke)
{
	if (sOpen == NULL)
		return;
	BMessage choose(kChooseRow);
	choose.AddInt32("row", row);
	choose.AddBool("invoke", invoke);
	sOpen->PostMessage(&choose);
}


void
TDialogStylesEdit::Command(uint32 command)
{
	if (sOpen != NULL)
		sOpen->PostMessage(command);
}


void
TDialogStylesEdit::Dump()
{
	if (sOpen != NULL)
		sOpen->PostMessage(kDump);
}


// TDialogStylesEdit::Init: the menus (MENU 210-212) in a menu bar; the
// style list a TStyleListView (CustomListBoxFactory), both lists' font 9
void
TDialogStylesEdit::Init()
{
	for (int32 i = 0; BWindow* window = be_app->WindowAt(i); i++)
		if (dynamic_cast<TDocWindow*>(window) != NULL)
			AddToSubset(window);
	BMenuBar* bar = new BMenuBar("mMenuBar");
	fFile = new TMenu(210);
	fEdit = new TMenu(211);
	fStyle = new TMenu(212);
	bar->AddItem(fFile);
	bar->AddItem(fEdit);
	bar->AddItem(fStyle);
	if (BGroupLayout* layout = dynamic_cast<BGroupLayout*>(GetLayout()))
		layout->AddView(0, bar);

	if (BScrollView* scroll = dynamic_cast<BScrollView*>(Item(1))) {
		fStyles = new BOutlineListView("listview", B_SINGLE_SELECTION_LIST);
		scroll->SetTarget(fStyles);
	}
	if (fStyles != NULL) {
		SetSmallFont(fStyles);
		BMessage* select = new BMessage('APPL' + 1);
		select->AddInt32("dialog", 10);
		select->AddInt32("index", 1);
		select->AddBool("Select", true);
		fStyles->SetSelectionMessage(select);
		BMessage* invoke = new BMessage('APPL' + 1);
		invoke->AddInt32("dialog", 10);
		invoke->AddInt32("index", 1);
		invoke->AddBool("Invoke", true);
		fStyles->SetInvocationMessage(invoke);
	}
	fProperties = ListOf(Item(3));
	if (fProperties != NULL) {
		SetSmallFont(fProperties);
		fProperties->SetInvocationMessage(NULL);
	}
	Update(false);
}


void
TDialogStylesEdit::MessageReceived(BMessage* message)
{
	switch (message->what) {
		// TDialogStylesEdit::ShowStyleByName
		case 'NAME':
		{
			const char* name;
			if (message->FindString("Name", &name) == B_OK && FindStyleByName(name) != -1) {
				fName = name;
				GetStyles();
				ReloadProperties(FindStyleByName(fName));
				fReloadStyles = false;
			}
			return;
		}
		case 'sD=L':
			Update(message->GetBool("force", false));
			return;
		case 'sE05':
		{
			BWindow* window;
			if (message->FindPointer("window", (void**)&window) == B_OK)
				AddToSubset(window);
			return;
		}
		case kChooseRow:
			if (fStyles != NULL) {
				int32 row = message->GetInt32("row", 0);
				BListItem* item = fStyles->FullListItemAt(row);
				int32 index = item != NULL ? fStyles->IndexOf(item) : -1;
				if (index >= 0) {
					fStyles->Select(index);
					if (message->GetBool("invoke", false))
						fStyles->Invoke();
				}
			}
			return;
		case kDump:
			for (size_t i = 0; i < fRows.size(); i++)
				printf("styles row %zu: %s%s flags %u%s\n", i, fRows[i].style < 0 ? "[" : "",
					fRows[i].label.c_str(), unsigned(fRows[i].flags),
					int32(i) == SelectedRow() ? " (chosen)" : "");
			if (fProperties != NULL)
				for (int32 i = 0; i < fProperties->CountItems(); i++)
					if (TPropertyItem* item = dynamic_cast<TPropertyItem*>(fProperties->ItemAt(i)))
						printf("styles property %d: %s\n", int(i), item->Text().c_str());
			fflush(stdout);
			return;
		// the menus (TDialogStylesEdit::ItemHit 0xffff): to the front window
		// with the style chosen
		case 'sMB0': case 'sMB1': case 'sMB2': case 'sMB3':
		case 'sMB4': case 'sMB5': case 'sMB6': case 'sMB7':
		{
			BMessage forward(*message);
			int32 row = SelectedRow();
			std::string name;
			if (row >= 0 && size_t(row) < fRows.size() && fRows[row].style >= 0)
				name = fSnapshot.styles.styles[fRows[row].style].name;
			forward.AddString(kTargetStyleStr, name.c_str());
			forward.AddPointer("orig_window", this);
			if (TDocWindow* window = TDocWindow::Front())
				window->PostMessage(&forward);
			return;
		}
	}
	TDialog::MessageReceived(message);
}


// TDialogStylesEdit::MenusBeginning: Modify Style, Cut and Clear Style
// with a style chosen that isn't one of the parts' own, Copy Style with
// one chosen, Paste Style with styles copied
void
TDialogStylesEdit::MenusBeginning()
{
	TDialog::MenusBeginning();
	int32 row = SelectedRow();
	bool chosen = row >= 0 && size_t(row) < fRows.size() && fRows[row].style >= 0;
	bool locked = chosen
		&& (fSnapshot.styles.styles[fRows[row].style].value8 & kStyleUserLock) != 0;
	auto enable = [&](TMenu* menu, uint32 command, bool on) {
		if (menu != NULL)
			if (BMenuItem* item = menu->FindItem(command))
				item->SetEnabled(on);
	};
	// (Cut, Copy, Paste, Import and Export Styles aren't ported yet)
	enable(fEdit, 'sMB5', chosen && !locked);
	enable(fEdit, 'sMB2', false);
	enable(fEdit, 'sMB3', false);
	enable(fEdit, 'sMB4', false);
	enable(fFile, 'sMB0', false);
	enable(fFile, 'sMB1', false);
	enable(fStyle, 'sMB7', chosen && !locked);
}


bool
TDialogStylesEdit::ItemHit(int32 index, BMessage* message)
{
	if (index != 1 || fStyles == NULL)
		return true;
	int32 row = SelectedRow();
	bool valid = row >= 0 && size_t(row) < fRows.size() && fRows[row].style >= 0;
	// a style double-clicked: Modify Style (not the parts' own)
	if (message->GetBool("Invoke", false) && valid) {
		const Style& style = fSnapshot.styles.styles[fRows[row].style];
		if (!(style.value8 & kStyleUserLock)) {
			BMessage modify('sMB7');
			modify.AddString(kTargetStyleStr, style.name.c_str());
			if (TDocWindow* window = TDocWindow::Front())
				window->PostMessage(&modify);
		}
	}
	if (message->GetBool("Select", false)) {
		fName = valid ? fSnapshot.styles.styles[fRows[row].style].name : "";
		ReloadProperties(row);
	}
	return true;
}


int32
TDialogStylesEdit::SelectedRow() const
{
	return fStyles != NULL ? fStyles->FullListCurrentSelection() : -1;
}


// TDialogStylesEdit::UpdateContents: the front document's styles (another
// document: its first style chosen if the one chosen isn't there)
void
TDialogStylesEdit::Update(bool force)
{
	StylesSnapshot snapshot;
	TDocWindow* window = TDocWindow::Front();
	if (window == NULL || window->LockWithTimeout(100000) != B_OK)
		return;
	window->GetStylesSnapshot(snapshot);
	window->Unlock();
	if (snapshot.document == NULL)
		return;
	bool other = !fHaveSnapshot || snapshot.document != fSnapshot.document || force
		|| snapshot.types != fSnapshot.types;
	fSnapshot = snapshot;
	fHaveSnapshot = true;
	GetStyles();
	if (other && FindStyleByName(fName) == -1) {
		fName.clear();
		for (const Row& row : fRows)
			if (row.style >= 0) {
				fName = fSnapshot.styles.styles[row.style].name;
				break;
			}
	}
	ReloadProperties(FindStyleByName(fName));
	fReloadStyles = false;
}


// TStyleManager::GetActiveStyles: each kind's named styles (by name, the
// cleared ones not) under a heading; TStyleListView::BuildList: the
// active one shown as it is ("+"), the applied ones marked; the list made
// again only if it changed
void
TDialogStylesEdit::GetStyles()
{
	const StyleMgr& styles = fSnapshot.styles;
	std::string active = fSnapshot.selection.active;
	std::string bare = active;
	if (!bare.empty() && bare.back() == '+')
		bare.pop_back();
	std::vector<Row> rows;
	for (uint32 type : fSnapshot.types) {
		std::vector<int32> named;
		for (size_t i = 0; i < styles.styles.size(); i++) {
			const Style& s = styles.styles[i];
			if (!s.name.empty() && s.value10 == type && !(s.value8 & kStyleDeleted))
				named.push_back(int32(i));
		}
		if (named.empty())
			continue;
		std::stable_sort(named.begin(), named.end(), [&](int32 a, int32 b) {
			return strcmp(styles.styles[a].name.c_str(), styles.styles[b].name.c_str()) < 0;
		});
		Row heading;
		heading.label = StyleTypeName(type);
		heading.type = type;
		heading.flags = 1;
		rows.push_back(heading);
		for (int32 index : named) {
			Row row;
			row.label = styles.styles[index].name;
			row.type = type;
			row.style = index;
			for (const auto& applied : fSnapshot.selection.applied)
				if (applied.first == row.label)
					row.flags |= applied.second ? 2 : 4;
			if (row.label == bare) {
				row.label = active;
				row.flags |= 8;
			}
			rows.push_back(row);
		}
	}
	bool same = fStyles != NULL && rows.size() == size_t(fStyles->FullListCountItems());
	for (size_t i = 0; same && i < rows.size(); i++) {
		TStyleListItem* item = dynamic_cast<TStyleListItem*>(fStyles->FullListItemAt(i));
		same = item != NULL && item->Label() == rows[i].label && item->Flags() == rows[i].flags;
	}
	std::vector<int32> oldStyles;
	for (const Row& row : fRows)
		oldStyles.push_back(row.style);
	fRows = rows;
	if (same || fStyles == NULL) {
		// the same items, maybe other styles (indexes) behind them
		return;
	}
	BMessage* select = fStyles->SelectionMessage() != NULL
		? new BMessage(*fStyles->SelectionMessage()) : NULL;
	fStyles->SetSelectionMessage(NULL);
	// the headings' open or closed (TStyleListView::ItemToggled)
	for (int32 i = 0; i < fStyles->FullListCountItems(); i++) {
		TStyleListItem* item = dynamic_cast<TStyleListItem*>(fStyles->FullListItemAt(i));
		if (item != NULL && (item->Flags() & 1))
			fExpanded[item->Label()] = item->IsExpanded();
	}
	while (fStyles->FullListCountItems() > 0) {
		BListItem* item = fStyles->FullListItemAt(0);
		fStyles->RemoveItem(item);
		delete item;
	}
	for (const Row& row : fRows) {
		// (AddItem with the level: AddUnder would put them in reverse)
		if (row.style < 0) {
			auto known = fExpanded.find(row.label);
			bool expanded = known == fExpanded.end() || known->second;
			fStyles->AddItem(new TStyleListItem(row.label, 0, expanded, row.flags));
		} else
			fStyles->AddItem(new TStyleListItem(row.label, 1, true, row.flags));
	}
	fStyles->SetSelectionMessage(select);
}


int32
TDialogStylesEdit::FindStyleByName(const std::string& name)
{
	for (size_t i = 0; i < fRows.size(); i++) {
		if (fRows[i].style < 0 || fSnapshot.styles.styles[fRows[i].style].name != name)
			continue;
		if (fStyles != NULL && SelectedRow() != int32(i)) {
			BMessage* select = fStyles->SelectionMessage() != NULL
				? new BMessage(*fStyles->SelectionMessage()) : NULL;
			fStyles->SetSelectionMessage(NULL);
			int32 index = fStyles->IndexOf(fStyles->FullListItemAt(i));
			if (index >= 0)
				fStyles->Select(index);
			fStyles->SetSelectionMessage(select);
		}
		return int32(i);
	}
	return -1;
}


// TDialogStylesEdit::ReloadPropertiesListBox: the style's base (STR# 13,
// 3), its own properties, its inherited ones, and with "+" the
// selection's own (TStyleManager::GetIncrementalProps)
void
TDialogStylesEdit::ReloadProperties(int32 row)
{
	if (fProperties != NULL) {
		while (BListItem* item = fProperties->RemoveItem(int32(0)))
			delete item;
	}
	if (row >= 0 && size_t(row) < fRows.size() && fRows[row].style >= 0 && fProperties != NULL) {
		const StyleMgr& styles = fSnapshot.styles;
		const Style& style = styles.styles[fRows[row].style];
		std::string base;
		if (style.links[0] < styles.styles.size())
			base = styles.styles[style.links[0]].name;
		fProperties->AddItem(new TPropertyItem(StringOf(13, 3), base, -1));
		StringLookup strings = [](int32_t id, int index) { return StringOf(id, index); };
		int unit = AppPreferences().measureUnits;
		auto add = [&](uint32_t index, int kind) {
			if (index >= styles.properties.size())
				return;
			const Property& property = styles.properties[index];
			TPropertyItem* item = new TPropertyItem(PropertyTypeName(property, strings),
				PropertyDataText(property, strings, unit), kind);
			if (PropertyIsInk(property)) {
				DocArchive a(property.data);
				Ink ink;
				try {
					ReadInk(a, ink);
				} catch (const ArchiveError&) {
				}
				item->SetInk({ink.color.r, ink.color.g, ink.color.b, 255});
			}
			fProperties->AddItem(item);
		};
		for (uint32_t p : style.props)
			add(p, 1);
		for (uint32_t p : style.props2)
			add(p, 2);
		const std::string& label = fRows[row].label;
		if (!label.empty() && label.back() == '+')
			for (uint32_t p : fSnapshot.selection.incremental)
				add(p, 0);
	}
	if (fProperties != NULL) {
		fProperties->DeselectAll();
		fProperties->Invalidate();
	}
	SetEditing();
	// (the original then has the front window and the format floaters
	// show the style: TFloaterMgr::UpdateFloater)
	if (TDocWindow* window = TDocWindow::Front())
		window->PostMessage('sD=L');
}


// TDialogStylesEdit::SelectedStyle, for TDoc::EditingStyle
void
TDialogStylesEdit::SetEditing()
{
	int32 row = SelectedRow();
	BAutolock lock(sEditingLock);
	sEditingDocument = fSnapshot.document;
	sEditingName = row >= 0 && size_t(row) < fRows.size() && fRows[row].style >= 0
		? fSnapshot.styles.styles[fRows[row].style].name : "";
}


// #pragma mark - TDialogStyleModify


TDialogStyleModify::TDialogStyleModify(BLooper* window, const std::string& name,
	const StyleMgr& styles, const std::vector<uint32>& types)
	:
	TDialog(12, 12, 'APPL', NULL),
	fWindow(window),
	fName(name),
	fStyles(styles),
	fTypes(types)
{
	// TStyleID::BaseName
	uint32_t style = FindNamedStyle(fStyles, fName);
	if (style < fStyles.styles.size() && fStyles.styles[style].links[0] < fStyles.styles.size())
		fBase = fStyles.styles[fStyles.styles[style].links[0]].name;
}


// TDialogStyleModify::Init: the name; Based On (TStyleManager::
// FillBaseStyleMenu: for each kind of the active part, a separator and its
// named styles that don't come from this one, the base marked)
void
TDialogStyleModify::Init()
{
	SetTextItem(2, fName.c_str());
	uint32_t style = FindNamedStyle(fStyles, fName);
	if (BMenuField* field = dynamic_cast<BMenuField*>(Item(3))) {
		BMenu* menu = field->Menu();
		std::vector<uint32> types = fTypes;
		if (types.empty())
			types.push_back(Sig("Basi"));
		bool marked = false;
		for (uint32 type : types) {
			bool first = true;
			for (size_t i = 0; i < fStyles.styles.size(); i++) {
				const Style& s = fStyles.styles[i];
				if ((s.value8 & kStyleDeleted) || s.value10 != type || s.name.empty()
						|| IsStyleAncestor(fStyles, style, uint32_t(i)))
					continue;
				if (first)
					menu->AddSeparatorItem();
				first = false;
				BMenuItem* item = new BMenuItem(s.name.c_str(), NULL);
				if (s.name == fBase) {
					item->SetMarked(true);
					marked = true;
				}
				menu->AddItem(item);
			}
		}
		if (!marked && menu->ItemAt(0) != NULL)
			menu->ItemAt(0)->SetMarked(true);
	}
	MakeFocusItem(2);
}


// TDialogStyleModify::ItemHit
bool
TDialogStyleModify::ItemHit(int32 index, BMessage* message)
{
	if (index == 0) {
		std::string name = TextItem(2).String();
		if (name != fName && FindNamedStyle(fStyles, name) < fStyles.styles.size()) {
			(new BAlert("", GobeResources::IndexString(6, 36).String(), "OK"))->Go(NULL);
			MakeFocusItem(2);
			return true;
		}
		std::string base;
		if (BMenuField* field = dynamic_cast<BMenuField*>(Item(3)))
			if (BMenuItem* marked = field->Menu()->FindMarked())
				if (field->Menu()->IndexOf(marked) >= 2)
					base = marked->Label();
		if (name != fName || base != fBase) {
			BMessage modify('sMB7');
			modify.AddString(kTargetStyleStr, fName.c_str());
			modify.AddString(kStyleNameStr, name.c_str());
			modify.AddString(kStyleBaseStr, base.c_str());
			fWindow->PostMessage(&modify);
		}
	}
	return TDialog::ItemHit(index, message);
}

} // namespace gobe
