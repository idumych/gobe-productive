#include "TTearOffs.h"

#include <Application.h>
#include <CheckBox.h>
#include <GroupLayout.h>
#include <ListView.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <OutlineListView.h>
#include <PopUpMenu.h>
#include <ScrollView.h>
#include <OS.h>
#include <Slider.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>

#include "GobeResources.h"
#include "TDialog.h"
#include "TDocWindow.h"
#include "TFormatBar.h"
#include "TMenu.h"
#include "Zoom.h"

namespace gobe {

namespace {

const uint32 kMsgRefresh = 'sD=T';

class TTearOff;
// TFloaterMgr: the open floater of each tear-off command
std::map<uint32, TTearOff*> sTearOffs;


// a tear-off floater: one of each, at the drop point, following the front
// document window
class TTearOff : public TDialog {
public:
	TTearOff(int32 dialog, uint32 what, float minWidth, float minHeight)
		:
		TDialog(dialog, dialog, 'APPL', NULL),
		fWhat(what),
		fMinWidth(minWidth),
		fMinHeight(minHeight)
	{
		sTearOffs[what] = this;
	}

	virtual ~TTearOff()
	{
		if (sTearOffs[fWhat] == this)
			sTearOffs.erase(fWhat);
	}

	virtual void MessageReceived(BMessage* message)
	{
		switch (message->what) {
			case kMsgRefresh:
				Refresh();
				return;
			// TFloaterMgr 'sE05': a new document window joins the subset
			case 'sE05':
			{
				BWindow* window;
				if (message->FindPointer("window", (void**)&window) == B_OK)
					AddToSubset(window);
				return;
			}
		}
		TDialog::MessageReceived(message);
	}

	// the front document window's state shown
	void Refresh()
	{
		TearOffState state;
		TDocWindow* window = TDocWindow::Front();
		if (window != NULL && window->LockWithTimeout(100000) == B_OK) {
			window->TearOffState(state);
			window->Unlock();
		}
		fUpdating = true;
		Show(state);
		fUpdating = false;
	}

	virtual void Script(const std::string& command) {}

protected:
	// InitPostRecalc: the subset, the size limits
	virtual void Init()
	{
		for (int32 i = 0; BWindow* window = be_app->WindowAt(i); i++)
			if (dynamic_cast<TDocWindow*>(window) != NULL)
				AddToSubset(window);
		float minWidth, maxWidth, minHeight, maxHeight;
		GetSizeLimits(&minWidth, &maxWidth, &minHeight, &maxHeight);
		if (fMinWidth > 0)
			SetSizeLimits(fMinWidth, maxWidth, fMinHeight, maxHeight);
		Build();
		Refresh();
	}

	// the controls set up, once
	virtual void Build() {}
	// TDialog::UpdateContents
	virtual void Show(const TearOffState& state) = 0;

	// to the front document window (TDialog::PostMessage)
	void Send(BMessage* message)
	{
		if (TDocWindow* window = TDocWindow::Front())
			window->PostMessage(message);
	}

	BListView* List(int32 index) const
	{
		BView* view = Item(index);
		if (BScrollView* scroll = dynamic_cast<BScrollView*>(view))
			view = scroll->Target();
		return dynamic_cast<BListView*>(view);
	}

	// a control the user is moving isn't set from the document (sent
	// less than a third of a second ago)
	bool Moving() const { return system_time() - fSent < 300000; }
	void Sent() { fSent = system_time(); }

	uint32 fWhat;
	float fMinWidth, fMinHeight;
	bool fUpdating = false;		// (controls set from the document send nothing)
	bigtime_t fSent = 0;
};


// Named Selections (TDialogNames: at least 130 by 110)
class TDialogNames : public TTearOff {
public:
	TDialogNames() : TTearOff(81, 'sW06', 130, 110) {}

protected:
	virtual void Build()
	{
		if (BListView* list = List(0))
			list->SetSelectionMessage(new BMessage('sD=S'));
	}

	// TDialogNames::UpdateContents: the names, rebuilt if they changed
	virtual void Show(const TearOffState& state)
	{
		BListView* list = List(0);
		if (list == NULL)
			return;
		bool same = list->CountItems() == int32(state.names.size());
		for (int32 i = 0; same && i < list->CountItems(); i++)
			same = state.names[i] == static_cast<BStringItem*>(list->ItemAt(i))->Text();
		if (same)
			return;
		BMessage* select = list->SelectionMessage() != NULL
			? new BMessage(*list->SelectionMessage()) : NULL;
		list->SetSelectionMessage(NULL);
		list->DeselectAll();
		while (BListItem* item = list->RemoveItem(int32(0)))
			delete item;
		for (const std::string& name : state.names)
			list->AddItem(new BStringItem(name.c_str()));
		list->SetSelectionMessage(select);
		fChosen = -1;
	}

	virtual void MessageReceived(BMessage* message)
	{
		BListView* list = List(0);
		if (message->what == 'sD=S' && list != NULL && !fUpdating) {
			// TDialogNames::ItemHit: a name chosen selects it ('sfN2')
			int32 row = list->CurrentSelection();
			if (row >= 0) {
				fChosen = row;
				BMessage select('sfN2');
				select.AddString("Name", static_cast<BStringItem*>(list->ItemAt(row))->Text());
				Send(&select);
			}
			return;
		}
		TTearOff::MessageReceived(message);
	}

	// a double click: Edit Names… on it ('sfN1')
	virtual bool ItemHit(int32 index, BMessage* message)
	{
		BListView* list = List(0);
		if (index == 0 && list != NULL && fChosen >= 0 && fChosen < list->CountItems()) {
			BMessage edit('sfN1');
			edit.AddString("Name", static_cast<BStringItem*>(list->ItemAt(fChosen))->Text());
			Send(&edit);
		}
		return true;
	}

	virtual void Script(const std::string& command)
	{
		BListView* list = List(0);
		int row = -1;
		if (command == "dump" && list != NULL) {
			printf("names:");
			for (int32 i = 0; i < list->CountItems(); i++)
				printf(" %s", static_cast<BStringItem*>(list->ItemAt(i))->Text());
			printf("\n");
			fflush(stdout);
		} else if (sscanf(command.c_str(), "pick %d", &row) == 1 && list != NULL)
			list->Select(row);
		else if (sscanf(command.c_str(), "invoke %d", &row) == 1 && list != NULL) {
			list->Select(row);
			list->Invoke();
		}
	}

private:
	int32 fChosen = -1;		// +0x4c
};


// Shape Transparency (TDialogTransparency): the opacity slider (1) sends
// 'sf0S' with its percent; the buttons (0, 2) a percent less or more
class TDialogTransparency : public TTearOff {
public:
	TDialogTransparency() : TTearOff(90, 'sW07', 0, 0) {}

protected:
	virtual void Build()
	{
		if (BSlider* slider = Slider())
			slider->SetModificationMessage(new BMessage(*slider->Message()));
	}

	virtual void Show(const TearOffState& state)
	{
		if (BSlider* slider = Slider()) {
			if (!Moving() && slider->Value() != state.opacity)
				slider->SetValue(state.opacity);
			slider->SetEnabled(state.shapes);
		}
		SetItemEnabled(0, state.shapes);
		SetItemEnabled(2, state.shapes);
	}

	virtual bool ItemHit(int32 index, BMessage* message)
	{
		BSlider* slider = Slider();
		if (slider == NULL || fUpdating)
			return true;
		// TDialogTransparency::MessageReceived (TRN-, TRN+ and their
		// still-down messages): a percent off each shape's own opacity
		// ('sf0T'), the slider following
		if (index == 0 || index == 2) {
			float step = index == 0 ? -0.01f : 0.01f;
			BMessage relative('sf0T');
			relative.AddFloat(kValueString, step);
			relative.AddInt32("prop type", ' Tra');
			relative.AddBool("Compress", true);
			relative.AddFloat("max", 1.0f);
			slider->SetValue(std::max(0, std::min(100, int(slider->Value())
				+ (index == 0 ? -1 : 1))));
			Send(&relative);
			Sent();
			return true;
		}
		BMessage set('sf0S');
		set.AddInt32(kValueString, slider->Value());
		set.AddBool("Compress", true);
		Send(&set);
		Sent();
		return true;
	}

	virtual void Script(const std::string& command)
	{
		int value = 0;
		BSlider* slider = Slider();
		if (command == "dump" && slider != NULL) {
			printf("transparency %d %s\n", int(slider->Value()),
				slider->IsEnabled() ? "enabled" : "disabled");
			fflush(stdout);
		} else if (sscanf(command.c_str(), "slider %d", &value) == 1 && slider != NULL) {
			slider->SetValue(value);
			ItemHit(1, NULL);
		} else if (sscanf(command.c_str(), "button %d", &value) == 1)
			ItemHit(value, NULL);
	}

private:
	BSlider* Slider() const { return dynamic_cast<BSlider*>(Item(1)); }
};


// Scale (TDialogScale): the zoom as a slider of 0 to 100
// (TScaleSlider::SetValue's position in hundredths)
class TDialogScale : public TTearOff {
public:
	TDialogScale() : TTearOff(72, 'sW05', 0, 0) {}

protected:
	virtual void Build()
	{
		if (BSlider* slider = Slider())
			slider->SetModificationMessage(new BMessage(*slider->Message()));
	}

	virtual void Show(const TearOffState& state)
	{
		BSlider* slider = Slider();
		if (slider == NULL)
			return;
		slider->SetEnabled(state.zoomable);
		int32 value = int32(std::lround(SliderValueForZoom(state.zoom) * 100));
		if (!Moving() && state.zoom != fZoom)
			slider->SetValue(value);
		fZoom = state.zoom;
	}

	// TDialogScale::SliderSetValue: the front window zoomed
	virtual bool ItemHit(int32 index, BMessage* message)
	{
		BSlider* slider = Slider();
		if (slider == NULL || fUpdating || index != 0)
			return true;
		float zoom = ZoomForSliderValue(slider->Value() / 100.0f);
		if (zoom == fZoom)
			return true;
		fZoom = zoom;
		BMessage scale('s003');
		scale.AddFloat("scale", zoom);
		Send(&scale);
		Sent();
		return true;
	}

	virtual void Script(const std::string& command)
	{
		int value = 0;
		BSlider* slider = Slider();
		if (command == "dump" && slider != NULL) {
			printf("scale %d zoom %g\n", int(slider->Value()), fZoom);
			fflush(stdout);
		} else if (sscanf(command.c_str(), "slider %d", &value) == 1 && slider != NULL) {
			slider->SetValue(value);
			ItemHit(0, NULL);
		}
	}

private:
	BSlider* Slider() const { return dynamic_cast<BSlider*>(Item(0)); }
	float fZoom = -1;		// +0x50
};


// Text Formatting (TDialogText): the font (1) and size (2) menus, the size
// slider (4, 2 to 510) with A- and A+ (3, 5), the faces (7 to 14)
class TDialogText : public TTearOff {
public:
	TDialogText() : TTearOff(64, 'sW04', 0, 0) {}

protected:
	// TDialogText::InitPostRecalc: the font and size menus as pop-ups in
	// the user items
	virtual void Build()
	{
		if (BView* user = Item(1)) {
			fFonts = new BPopUpMenu("font");
			TFontMenu fonts;
			while (fonts.CountItems() > 0)
				fFonts->AddItem(fonts.RemoveItem((int32)0));
			fFonts->SetTargetForItems(this);
			BMenuField* field = new BMenuField("font", NULL, fFonts);
			user->SetLayout(new BGroupLayout(B_HORIZONTAL, 0));
			user->GetLayout()->AddView(field);
		}
		if (BView* user = Item(2)) {
			fSizes = new BPopUpMenu("size");
			TMenu source(101, true);
			while (source.CountItems() > 0)
				fSizes->AddItem(source.RemoveItem((int32)0));
			fSizes->SetTargetForItems(this);
			BMenuField* field = new BMenuField("size", NULL, fSizes);
			user->SetLayout(new BGroupLayout(B_HORIZONTAL, 0));
			user->GetLayout()->AddView(field);
		}
		if (BSlider* slider = Slider())
			slider->SetModificationMessage(new BMessage(*slider->Message()));
	}

	// TDialogText::UpdateContents, UpdateFaceCheckBoxes
	virtual void Show(const TearOffState& state)
	{
		for (int32 i = 1; i <= 14; i++)
			if (i != 6)
				SetItemEnabled(i, state.text);
		if (!state.text)
			return;
		if (fFonts != NULL) {
			if (BMenuItem* marked = fFonts->FindMarked())
				marked->SetMarked(false);
			if (BMenuItem* item = fFonts->FindItem(state.family.c_str()))
				item->SetMarked(true);
			else if (fFonts->Superitem() != NULL)
				fFonts->Superitem()->SetLabel(state.family.c_str());
		}
		if (fSizes != NULL) {
			char label[32];
			snprintf(label, sizeof label, "%g", state.size);
			if (BMenuItem* marked = fSizes->FindMarked())
				marked->SetMarked(false);
			if (BMenuItem* item = fSizes->FindItem(label))
				item->SetMarked(true);
			else if (fSizes->Superitem() != NULL)
				fSizes->Superitem()->SetLabel(label);
		}
		if (BSlider* slider = Slider())
			if (!Moving())
				slider->SetValue(int32(std::lround(state.size)));
		fFaces = state.faces;
		static const uint32 kFaces[8] = {1, 2, 4, 8, 16, 32, 64, 128};
		for (int32 i = 0; i < 8; i++)
			SetCheckBoxItem(7 + i, (state.faces & kFaces[i]) != 0
				|| (kFaces[i] == 1 && state.faces == 0));
	}

	virtual void MessageReceived(BMessage* message)
	{
		// the font and size menus' commands go on to the document
		if (message->what == 'sf01' || message->what == 'sf0K') {
			Send(message);
			return;
		}
		TTearOff::MessageReceived(message);
	}

	virtual bool ItemHit(int32 index, BMessage* message)
	{
		if (fUpdating)
			return true;
		BSlider* slider = Slider();
		if ((index == 3 || index == 5) && slider != NULL) {
			// TDialogText::MessageReceived (SIZ-, SIZ+): a point off each
			// run's own size ('sf0O', the slider's for runs without one)
			BMessage relative('sf0O');
			relative.AddFloat(kValueString, index == 3 ? -1.0f : 1.0f);
			relative.AddFloat("default_value", float(slider->Value()));
			relative.AddBool("Compress", true);
			slider->SetValue(std::max(2, std::min(510, int(slider->Value()
				+ (index == 3 ? -1 : 1)))));
			Send(&relative);
			Sent();
			return true;
		}
		if (index == 4 && slider != NULL) {
			// the slider ('sf0K', TDialogText::SetTextSize)
			int32 size = slider->Value();
			BMessage set('sf0K');
			set.AddInt32(kValueString, size);
			set.AddBool("Compress", true);
			Send(&set);
			Sent();
			return true;
		}
		// TDialogText::ItemHit: a face toggled ('sf0C')
		if (index >= 7 && index <= 14) {
			BMessage face('sf0C');
			face.AddInt32(kValueString, 1 << (index - 7));
			Send(&face);
		}
		return true;
	}

	virtual void Script(const std::string& command)
	{
		int value = 0;
		if (command == "dump") {
			BSlider* slider = Slider();
			printf("text font %s size %s slider %d faces",
				fFonts != NULL && fFonts->Superitem() != NULL
					? fFonts->Superitem()->Label() : "?",
				fSizes != NULL && fSizes->Superitem() != NULL
					? fSizes->Superitem()->Label() : "?",
				slider != NULL ? int(slider->Value()) : -1);
			for (int32 i = 7; i <= 14; i++)
				printf(" %d", CheckBoxItem(i) ? 1 : 0);
			printf(" (flags %#x)\n", unsigned(fFaces));
			fflush(stdout);
		} else if (sscanf(command.c_str(), "slider %d", &value) == 1 && Slider() != NULL) {
			Slider()->SetValue(value);
			ItemHit(4, NULL);
		} else if (sscanf(command.c_str(), "button %d", &value) == 1)
			ItemHit(value, NULL);
	}

private:
	BSlider* Slider() const { return dynamic_cast<BSlider*>(Item(4)); }

	BPopUpMenu* fFonts = NULL;
	BPopUpMenu* fSizes = NULL;
	uint32 fFaces = 0;
};


// Styles (TDialogStyles: at least 170 by 140; the list's font 9)
class TDialogStyles : public TTearOff {
public:
	TDialogStyles() : TTearOff(9, 'sW02', 170, 140) {}

protected:
	virtual void Build()
	{
		BView* view = Item(0);
		BScrollView* scroll = dynamic_cast<BScrollView*>(view);
		if (scroll == NULL)
			return;
		// (CLST: a TStyleListView, an outline list)
		BListView* old = dynamic_cast<BListView*>(scroll->Target());
		fList = new BOutlineListView("styles", B_SINGLE_SELECTION_LIST);
		fList->SetSelectionMessage(new BMessage('sD=S'));
		BFont font(be_plain_font);
		font.SetSize(9);
		fList->SetFont(&font);
		if (old != NULL) {
			scroll->SetTarget(NULL);
			old->RemoveSelf();
			delete old;
		}
		// (BScrollView::SetTarget adds it as a child)
		scroll->SetTarget(fList);
	}

	// TDialogStyles::UpdateContents: TStyleListView::GetStyles, BuildList
	virtual void Show(const TearOffState& state)
	{
		fSnapshot = state.styles;
		fRows = StyleRows(fSnapshot);
		if (fList != NULL)
			BuildStyleList(fList, fRows, fExpanded);
		SetItemEnabled(1, !state.styles.types.empty());
		SetItemEnabled(2, state.document);
	}

	virtual void MessageReceived(BMessage* message)
	{
		if (message->what == 'sD=S' && fList != NULL && !fUpdating) {
			// TDialogStyles::ItemHit: a style chosen is applied ('sf0L'),
			// or taken off when all of the selection has it ('sf0V')
			int32 row = fList->FullListCurrentSelection();
			if (row >= 0 && size_t(row) < fRows.size() && fRows[row].style >= 0) {
				const StyleRow& chosen = fRows[row];
				const Style& style = fSnapshot.styles.styles[chosen.style];
				BMessage apply((chosen.flags & 4) != 0 ? 'sf0V' : 'sf0L');
				apply.AddInt32(kStyleLevelStr, -1);
				apply.AddString(kStyleNameStr, style.name.c_str());
				apply.AddInt32(kStyleTypeStr, int32(style.value10));
				Send(&apply);
			}
			return;
		}
		TTearOff::MessageReceived(message);
	}

	// New ('sMB6', New Style…) and Edit ('sM23', Edit Styles…)
	virtual bool ItemHit(int32 index, BMessage* message)
	{
		if (index == 1) {
			BMessage create('sMB6');
			create.AddPointer("orig_window", this);
			Send(&create);
		} else if (index == 2) {
			BMessage edit('sM23');
			Send(&edit);
		}
		return true;
	}

	virtual void Script(const std::string& command)
	{
		int row = -1;
		if (command == "dump") {
			printf("styles:");
			for (const StyleRow& r : fRows)
				printf(" %s%s(%u)", r.style < 0 ? "#" : "", r.label.c_str(), unsigned(r.flags));
			printf("\n");
			fflush(stdout);
		} else if (sscanf(command.c_str(), "pick %d", &row) == 1 && fList != NULL) {
			int32 index = fList->IndexOf(fList->FullListItemAt(row));
			if (index >= 0)
				fList->Select(index);
		} else if (sscanf(command.c_str(), "button %d", &row) == 1)
			ItemHit(row, NULL);
	}

private:
	BOutlineListView* fList = NULL;
	StylesSnapshot fSnapshot;
	std::vector<StyleRow> fRows;
	std::map<std::string, bool> fExpanded;
};

} // namespace


void
OpenTearOff(uint32 what, BPoint where)
{
	auto open = sTearOffs.find(what);
	if (open != sTearOffs.end()) {
		// TFloaterMgr::GetFloater: the one there, moved to the drop point
		TTearOff* floater = open->second;
		if (floater->Lock()) {
			floater->MoveTo(where);
			floater->Activate();
			floater->Unlock();
		}
		return;
	}
	TTearOff* floater = NULL;
	switch (what) {
		case 'sW02': floater = new TDialogStyles; break;
		case 'sW04': floater = new TDialogText; break;
		case 'sW05': floater = new TDialogScale; break;
		case 'sW06': floater = new TDialogNames; break;
		case 'sW07': floater = new TDialogTransparency; break;
		default: return;
	}
	floater->RunDialog();
	if (floater->Lock()) {
		floater->MoveTo(where);
		floater->Unlock();
	}
}


void
RefreshTearOffs()
{
	for (const auto& [what, floater] : sTearOffs)
		floater->PostMessage(kMsgRefresh);
}


std::vector<BWindow*>
TearOffWindows()
{
	std::vector<BWindow*> windows;
	for (const auto& [what, floater] : sTearOffs)
		windows.push_back(floater);
	return windows;
}


void
TearOffScript(uint32 what, const std::string& command)
{
	auto open = sTearOffs.find(what);
	if (open == sTearOffs.end()) {
		printf("tearoff not open\n");
		fflush(stdout);
		return;
	}
	if (open->second->Lock()) {
		open->second->Refresh();
		open->second->Script(command);
		open->second->Unlock();
	}
}

} // namespace gobe
