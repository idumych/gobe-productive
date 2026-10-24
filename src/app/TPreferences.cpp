// The preferences.
#include "TPreferences.h"

#include <Alert.h>
#include <Box.h>
#include <CheckBox.h>
#include <RadioButton.h>
#include <File.h>
#include <FindDirectory.h>
#include <Font.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <OutlineListView.h>
#include <Path.h>
#include <PopUpMenu.h>
#include <ScrollView.h>

#include <cstdlib>

#include "GobeResources.h"
#include "TSpellBar.h"

namespace gobe {

namespace {

const uint32 kCommand = 'APPL';

// TPreferences' PPNL resources: ApplicationPrefs (0), WP Prefs (4), GR
// Prefs (3), in the order TDialogPrefs::LoadAllPrefs lists them; the groups
// are named by "Application" and the parts' PART resources
const PreferencePanel kPanels[] = {
	{0, "General", 41}, {0, "Spell Checking", 42}, {1, "General", 308}, {2, "General", 204}
};

int32 sLastPanel = 0;		// TDialogPrefs' mTheLastGroup


uint32_t
U32(const std::vector<uint8_t>& data, size_t at)
{
	return at + 4 <= data.size() ? uint32_t(data[at]) | uint32_t(data[at + 1]) << 8
		| uint32_t(data[at + 2]) << 16 | uint32_t(data[at + 3]) << 24 : 0;
}


const std::vector<uint8_t>*
PrefResource(int32 id)
{
	const ResourceFile::Resource* r = GobeResources::File().Find(FourCC("PREF"), id);
	return r != NULL ? &r->data : NULL;
}


// TAppPrefs::PrefDefault (PREF 41): unlimited undo (then off), the undo
// count, the units, the divisions, a flag, the font, the spacing, tool tips
void
GeneralDefaults(Preferences& p)
{
	const std::vector<uint8_t>* data = PrefResource(41);
	if (data == NULL || data->size() < 20)
		return;
	p.unlimitedUndo = false;
	p.undoCount = int32(U32(*data, 4));
	p.measureUnits = int32(U32(*data, 8));
	p.rulerDivisions = int32(U32(*data, 12));
	size_t at = 20, end = at;
	while (end < data->size() && (*data)[end] != 0)
		end++;
	p.defaultFont.assign(data->begin() + long(at), data->begin() + long(end));
	at = end + 1;
	p.spacingType = int32(U32(*data, at));
	p.toolTips = U32(*data, at + 4) != 0;
}


// TAppPrefs::PrefDefault (PREF 42), FixSpellPrefs
void
SpellingDefaults(Preferences& p)
{
	if (const std::vector<uint8_t>* data = PrefResource(42))
		p.spelling.Read(*data);
	if (!p.spelling.suggestTypo && !p.spelling.suggestPhonetic)
		p.spelling.suggestTypo = p.spelling.suggestPhonetic = true;
}


// TWPPrefs::PrefDefault
void
TextDefaults(Preferences& p)
{
	p.pcHomeEnd = p.pcPageKeys = p.liveTextWrap = true;
}


// TGRPrefs::PrefDefault (PREF 204)
void
GraphicsDefaults(Preferences& p)
{
	const std::vector<uint8_t>* data = PrefResource(204);
	if (data == NULL || data->size() < 16)
		return;
	p.fourHandles = U32(*data, 0) != 0;
	p.polyCloseManually = U32(*data, 4) != 0;
	p.polyAutosmooth = U32(*data, 8) != 0;
	p.shiftConstraint = float(U32(*data, 12));
}


BPath
PreferencesPath()
{
	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) == B_OK)
		path.Append("Gobe Productive port preferences");
	return path;
}


// TAppPrefs::PrefLoad, TWPPrefs::PrefLoad, TGRPrefs::PrefLoad (TPrefStore)
void
Load(Preferences& p)
{
	BFile file(PreferencesPath().Path(), B_READ_ONLY);
	BMessage store;
	if (file.InitCheck() != B_OK || store.Unflatten(&file) != B_OK)
		return;
	bool b;
	int32 n;
	float f;
	const char* s;
	if (store.FindBool("unlimited undo", &b) == B_OK)
		p.unlimitedUndo = b;
	if (store.FindInt32("undo count", &n) == B_OK)
		p.undoCount = n;
	// (TAppPrefs: unlimited undo isn't kept on)
	if (p.unlimitedUndo) {
		p.unlimitedUndo = false;
		p.undoCount = 20;
	}
	if (store.FindInt32("measure units", &n) == B_OK)
		p.measureUnits = n;
	if (store.FindInt32("ruler divisions", &n) == B_OK)
		p.rulerDivisions = n;
	if (store.FindString("default font family", &s) == B_OK)
		p.defaultFont = s;
	if (store.FindInt32("spacing type", &n) == B_OK)
		p.spacingType = n;
	if (store.FindBool("tool tips", &b) == B_OK)
		p.toolTips = b;
	SpellPrefs& sp = p.spelling;
	bool* flags[6] = {&sp.ignoreMixedCase, &sp.ignoreMixedDigits, &sp.ignoreAllCaps,
		&sp.ignoreCapitalized, &sp.suggestTypo, &sp.suggestPhonetic};
	const char* names[6] = {"ignore mixed case", "ignore mixed digits",
		"ignore mixed all uppercase", "ignore capitalized", "suggest typo", "suggest phonetic"};
	for (int i = 0; i < 6; i++)
		if (store.FindBool(names[i], &b) == B_OK)
			*flags[i] = b;
	if (store.FindInt32("search depth", &n) == B_OK)
		sp.depth = n;
	if (store.FindString("main dict", &s) == B_OK)
		sp.mainDict = s;
	if (store.FindString("user dict", &s) == B_OK)
		sp.userDict = s;
	if (!sp.suggestTypo && !sp.suggestPhonetic)
		sp.suggestTypo = sp.suggestPhonetic = true;
	if (store.FindBool("PC Home & End Keys", &b) == B_OK)
		p.pcHomeEnd = b;
	if (store.FindBool("PC Page Keys", &b) == B_OK)
		p.pcPageKeys = b;
	if (store.FindBool("Live Text Wrap", &b) == B_OK)
		p.liveTextWrap = b;
	if (store.FindBool("4 handle selection", &b) == B_OK)
		p.fourHandles = b;
	if (store.FindBool("poly close manually", &b) == B_OK)
		p.polyCloseManually = b;
	if (store.FindBool("poly autosmooth", &b) == B_OK)
		p.polyAutosmooth = b;
	if (store.FindFloat("shift constraint", &f) == B_OK)
		p.shiftConstraint = f;
}

} // namespace


Preferences&
AppPreferences()
{
	static Preferences sPreferences;
	static bool sLoaded = false;
	if (!sLoaded) {
		sLoaded = true;
		GeneralDefaults(sPreferences);
		SpellingDefaults(sPreferences);
		TextDefaults(sPreferences);
		GraphicsDefaults(sPreferences);
		Load(sPreferences);
	}
	return sPreferences;
}


void
SavePreferences()
{
	const Preferences& p = AppPreferences();
	BMessage store;
	store.AddBool("unlimited undo", p.unlimitedUndo);
	store.AddInt32("undo count", p.undoCount);
	store.AddInt32("measure units", p.measureUnits);
	store.AddInt32("ruler divisions", p.rulerDivisions);
	store.AddString("default font family", p.defaultFont.c_str());
	store.AddInt32("spacing type", p.spacingType);
	store.AddBool("tool tips", p.toolTips);
	const SpellPrefs& sp = p.spelling;
	store.AddBool("ignore mixed case", sp.ignoreMixedCase);
	store.AddBool("ignore mixed digits", sp.ignoreMixedDigits);
	store.AddBool("ignore mixed all uppercase", sp.ignoreAllCaps);
	store.AddBool("ignore capitalized", sp.ignoreCapitalized);
	store.AddBool("suggest typo", sp.suggestTypo);
	store.AddBool("suggest phonetic", sp.suggestPhonetic);
	store.AddInt32("search depth", sp.depth);
	store.AddString("main dict", sp.mainDict.c_str());
	store.AddString("user dict", sp.userDict.c_str());
	store.AddBool("PC Home & End Keys", p.pcHomeEnd);
	store.AddBool("PC Page Keys", p.pcPageKeys);
	store.AddBool("Live Text Wrap", p.liveTextWrap);
	store.AddBool("4 handle selection", p.fourHandles);
	store.AddBool("poly close manually", p.polyCloseManually);
	store.AddBool("poly autosmooth", p.polyAutosmooth);
	store.AddFloat("shift constraint", p.shiftConstraint);
	BFile file(PreferencesPath().Path(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	if (file.InitCheck() == B_OK)
		store.Flatten(&file);
}


// #pragma mark - TDialogPrefs


TDialogPrefs* sOpenPrefs = NULL;


TDialogPrefs::TDialogPrefs()
	:
	TDialog(40, 40, kCommand, NULL)
{
	sOpenPrefs = this;
}


TDialogPrefs::~TDialogPrefs()
{
	if (sOpenPrefs == this)
		sOpenPrefs = NULL;
}


void
TDialogPrefs::SelectPanel(int32 panel)
{
	if (sOpenPrefs == NULL)
		return;
	BMessage message('Gpnl');
	message.AddInt32("panel", panel);
	sOpenPrefs->PostMessage(&message);
}


// TDialogPrefs::LoadAllPrefs: "Application" and the parts' names over
// their panels (TPrefListItem), the last panel shown selected
void
TDialogPrefs::Init()
{
	fBackup = AppPreferences();
	BOutlineListView* list = NULL;
	if (BScrollView* scroll = dynamic_cast<BScrollView*>(Item(4)))
		list = dynamic_cast<BOutlineListView*>(scroll->Target());
	if (list == NULL)
		return;
	BFont font;
	list->GetFont(&font);
	font.SetSize(9);
	list->SetFont(&font);
	const char* groups[3] = {"Application", NULL, NULL};
	// the parts' names (PART 4, 3)
	static const int32 kParts[3] = {0, 4, 3};
	std::string names[3];
	for (int g = 1; g < 3; g++) {
		if (const ResourceFile::Resource* r = GobeResources::File().Find(FourCC("PART"),
				kParts[g])) {
			if (r->data.size() > 28) {
				size_t end = 28;
				while (end < r->data.size() && r->data[end] != 0)
					end++;
				names[g].assign(r->data.begin() + 28, r->data.begin() + long(end));
			}
		}
		groups[g] = names[g].c_str();
	}
	int group = -1;
	for (const PreferencePanel& panel : kPanels) {
		if (panel.group != group) {
			group = panel.group;
			list->AddItem(new BStringItem(groups[group]));
		}
		list->AddItem(new BStringItem(panel.name, 1));
	}
	BMessage* select = new BMessage(kCommand + 4);
	select->AddInt32("dialog", 40);
	select->AddInt32("index", 4);
	select->AddBool("Select", true);
	list->SetSelectionMessage(select);
	// the list's rows: groups and panels in turn
	int32 row = 0;
	group = -1;
	for (int32 i = 0; i < int32(sizeof(kPanels) / sizeof(kPanels[0])); i++) {
		if (kPanels[i].group != group) {
			group = kPanels[i].group;
			row++;
		}
		if (i == sLastPanel) {
			list->Select(row);
			ShowPanel(i);
		}
		row++;
	}
}


void
TDialogPrefs::Press(int32 sub, int32 button)
{
	if (sOpenPrefs == NULL)
		return;
	BMessage message('Gprs');
	message.AddInt32("sub", sub);
	message.AddInt32("button", button);
	sOpenPrefs->PostMessage(&message);
}


void
TDialogPrefs::MessageReceived(BMessage* message)
{
	if (message->what == 'Gpnl') {
		ShowPanel(message->GetInt32("panel", 0));
		return;
	}
	if (message->what == 'Gprs') {
		int32 sub = message->GetInt32("sub", -1);
		BControl* control = dynamic_cast<BControl*>(sub >= 0 ? Item(sub, 7)
			: Item(message->GetInt32("button", 1)));
		if (control == NULL)
			return;
		if (dynamic_cast<BRadioButton*>(control) != NULL)
			control->SetValue(B_CONTROL_ON);
		else if (dynamic_cast<BCheckBox*>(control) != NULL)
			control->SetValue(control->Value() == B_CONTROL_ON ? B_CONTROL_OFF : B_CONTROL_ON);
		control->Invoke();
		return;
	}
	TDialog::MessageReceived(message);
}


void
TDialogPrefs::ShowPanel(int32 panel)
{
	if (panel == fPanel || panel < 0
			|| panel >= int32(sizeof(kPanels) / sizeof(kPanels[0])))
		return;
	fPanel = panel;
	sLastPanel = panel;
	if (BBox* box = dynamic_cast<BBox*>(Item(5)))
		box->SetLabel(kPanels[panel].name);
	LoadItemSet(7, kPanels[panel].items);
	PanelInit(false);
	InvalidateLayout();
}


bool
TDialogPrefs::ItemHit(int32 index, BMessage* message)
{
	switch (index) {
		case 0:
			if (fChanged)
				SavePreferences();
			fBackup = AppPreferences();
			PanelClose();
			break;
		case 1:
			PanelClose();
			break;
		case 2:
			PanelInit(true);
			return true;
		case 3:
			PanelDefault();
			fChanged = true;
			return true;
		case 4:
		{
			BOutlineListView* list = NULL;
			if (BScrollView* scroll = dynamic_cast<BScrollView*>(Item(4)))
				list = dynamic_cast<BOutlineListView*>(scroll->Target());
			int32 selected = list != NULL ? list->FullListCurrentSelection() : -1;
			if (selected < 0)
				return true;
			// the panel at that row (a group's row shows nothing)
			int32 row = 0, group = -1;
			for (int32 i = 0; i < int32(sizeof(kPanels) / sizeof(kPanels[0])); i++) {
				if (kPanels[i].group != group) {
					group = kPanels[i].group;
					row++;
				}
				if (row == selected)
					ShowPanel(i);
				row++;
			}
			return true;
		}
		case 7:
			fChanged = true;
			return PanelItemHit(message->GetInt32("SubID", -1), message);
	}
	return TDialog::ItemHit(index, message);
}


// TAppPrefs::setPanel1Items, setPanel2Items, TWPPrefs::setPanel1Items,
// TGRPrefs' (PrefInit; with `revert` the panel's values as they were)
void
TDialogPrefs::PanelInit(bool revert)
{
	Preferences& p = AppPreferences();
	if (fPanel < 0)
		return;
	int group = kPanels[fPanel].group;
	if (revert) {
		if (kPanels[fPanel].items == 41) {
			Preferences keep = p;
			p = fBackup;
			p.spelling = keep.spelling;
			p.pcHomeEnd = keep.pcHomeEnd;
			p.pcPageKeys = keep.pcPageKeys;
			p.liveTextWrap = keep.liveTextWrap;
			p.fourHandles = keep.fourHandles;
			p.polyCloseManually = keep.polyCloseManually;
			p.polyAutosmooth = keep.polyAutosmooth;
			p.shiftConstraint = keep.shiftConstraint;
		} else if (kPanels[fPanel].items == 42)
			p.spelling = fBackup.spelling;
		else if (group == 1) {
			p.pcHomeEnd = fBackup.pcHomeEnd;
			p.pcPageKeys = fBackup.pcPageKeys;
			p.liveTextWrap = fBackup.liveTextWrap;
		} else {
			p.fourHandles = fBackup.fourHandles;
			p.polyCloseManually = fBackup.polyCloseManually;
			p.polyAutosmooth = fBackup.polyAutosmooth;
			p.shiftConstraint = fBackup.shiftConstraint;
		}
	}
	switch (kPanels[fPanel].items) {
		case 41:
		{
			SetTextItem(1, std::to_string(p.undoCount).c_str(), 7);
			// the units' menu: its first four
			BMenuField* units = dynamic_cast<BMenuField*>(Item(3, 7));
			if (units != NULL && units->Menu()->CountItems() > 4)
				RemoveLastPopUpItems(3, 2, 7);
			SetPopUpItem(3, p.measureUnits, 7);
			SetTextItem(4, std::to_string(p.rulerDivisions).c_str(), 7);
			// the font families (TFontMenu)
			if (BMenuField* fonts = dynamic_cast<BMenuField*>(Item(6, 7))) {
				BMenu* menu = fonts->Menu();
				while (BMenuItem* item = menu->RemoveItem(int32(0)))
					delete item;
				int32 count = count_font_families();
				for (int32 i = 0; i < count; i++) {
					font_family family;
					uint32 flags;
					if (get_font_family(i, &family, &flags) != B_OK)
						continue;
					BMessage* message = new BMessage(kCommand + 7);
					message->AddInt32("dialog", 40);
					message->AddInt32("index", 7);
					message->AddInt32("SubID", 6);
					menu->AddItem(new BMenuItem(family, message));
				}
				BMenuItem* marked = menu->FindItem(p.defaultFont.c_str());
				if (marked == NULL)
					marked = menu->ItemAt(0);
				if (marked != NULL)
					marked->SetMarked(true);
				fonts->SetEnabled(true);
			}
			SetPopUpItem(7, p.spacingType, 7);
			SetCheckBoxItem(8, p.toolTips, 7);
			break;
		}
		case 42:
		{
			const SpellPrefs& s = p.spelling;
			SetCheckBoxItem(1, s.ignoreMixedCase, 7);
			SetCheckBoxItem(2, s.ignoreMixedDigits, 7);
			SetCheckBoxItem(3, s.ignoreAllCaps, 7);
			SetCheckBoxItem(4, s.ignoreCapitalized, 7);
			SetCheckBoxItem(6, s.suggestTypo, 7);
			SetItemEnabled(6, s.suggestPhonetic || !s.suggestTypo, 7);
			SetCheckBoxItem(7, s.suggestPhonetic, 7);
			SetItemEnabled(7, s.suggestTypo || !s.suggestPhonetic, 7);
			SetPopUpItem(8, s.depth, 7);
			break;
		}
		case 308:
			SetRadioItem(p.pcHomeEnd ? 1 : 2, true, 7);
			SetRadioItem(p.pcPageKeys ? 4 : 5, true, 7);
			SetCheckBoxItem(6, p.liveTextWrap, 7);
			break;
		case 204:
			SetRadioItem(2, p.fourHandles, 7);
			SetRadioItem(4, !p.fourHandles, 7);
			SetRadioItem(7, p.polyCloseManually, 7);
			SetRadioItem(9, !p.polyCloseManually, 7);
			SetCheckBoxItem(10, p.polyAutosmooth, 7);
			SetTextItem(11, std::to_string(int(p.shiftConstraint)).c_str(), 7);
			break;
	}
}


// PrefDefault: the panel's factory settings
void
TDialogPrefs::PanelDefault()
{
	Preferences& p = AppPreferences();
	switch (kPanels[fPanel].items) {
		case 41:
			GeneralDefaults(p);
			break;
		case 42:
		{
			// (TAppPrefs keeps the dictionaries)
			std::string mainDict = p.spelling.mainDict, userDict = p.spelling.userDict;
			SpellingDefaults(p);
			p.spelling.mainDict = mainDict;
			p.spelling.userDict = userDict;
			TSpellBar::AppSpeller().SetUserOptions(p.spelling);
			break;
		}
		case 308:
			TextDefaults(p);
			break;
		case 204:
			GraphicsDefaults(p);
			break;
	}
	PanelInit(false);
}


bool
TDialogPrefs::PanelItemHit(int32 sub, BMessage* message)
{
	Preferences& p = AppPreferences();
	auto alertRange = [](int low, int high) {
		BString text;
		text.SetToFormat(GobeResources::IndexString(6, 13).String(), low, high);
		(new BAlert("", text.String(), "OK"))->Go(NULL);
	};
	switch (kPanels[fPanel].items) {
		case 41:
			switch (sub) {
				case 1:
				case 4:
				{
					int high = sub == 1 ? 999 : 512;
					char* end;
					BString text = TextItem(sub, 7);
					long value = strtol(text.String(), &end, 10);
					if (end == text.String() || *end != '\0' || value < 1 || value > high) {
						alertRange(1, high);
						SetTextItem(sub, std::to_string(sub == 1 ? p.undoCount
							: p.rulerDivisions).c_str(), 7);
						return true;
					}
					(sub == 1 ? p.undoCount : p.rulerDivisions) = int32(value);
					break;
				}
				case 3:
					p.measureUnits = message->GetInt32(kValueString, p.measureUnits);
					break;
				case 6:
				{
					BMenuField* fonts = dynamic_cast<BMenuField*>(Item(6, 7));
					BMenuItem* marked = fonts != NULL ? fonts->Menu()->FindMarked() : NULL;
					if (marked != NULL)
						p.defaultFont = marked->Label();
					break;
				}
				case 7:
					p.spacingType = message->GetInt32(kValueString, p.spacingType);
					break;
				case 8:
					p.toolTips = CheckBoxItem(8, 7);
					break;
			}
			break;
		case 42:
		{
			SpellPrefs& s = p.spelling;
			switch (sub) {
				case 1: s.ignoreMixedCase = CheckBoxItem(1, 7); break;
				case 2: s.ignoreMixedDigits = CheckBoxItem(2, 7); break;
				case 3: s.ignoreAllCaps = CheckBoxItem(3, 7); break;
				case 4: s.ignoreCapitalized = CheckBoxItem(4, 7); break;
				// one way of suggesting at least: the other box off while
				// it's the only one
				case 6:
					s.suggestTypo = CheckBoxItem(6, 7);
					SetItemEnabled(7, s.suggestTypo, 7);
					break;
				case 7:
					s.suggestPhonetic = CheckBoxItem(7, 7);
					SetItemEnabled(6, s.suggestPhonetic, 7);
					break;
				case 8:
					s.depth = message->GetInt32(kValueString, s.depth);
					break;
			}
			TSpellBar::AppSpeller().SetUserOptions(s);
			break;
		}
		case 308:
			switch (sub) {
				case 1: p.pcHomeEnd = true; break;
				case 2: p.pcHomeEnd = false; break;
				case 4: p.pcPageKeys = true; break;
				case 5: p.pcPageKeys = false; break;
				case 6: p.liveTextWrap = !p.liveTextWrap; break;
			}
			break;
		case 204:
			switch (sub) {
				// the handles and closing radios are in groups of their own
				// (TDialog::SetMultiGroupRadioItem)
				case 2:
				case 4:
					p.fourHandles = sub == 2;
					SetRadioItem(2, p.fourHandles, 7);
					SetRadioItem(4, !p.fourHandles, 7);
					break;
				case 7:
				case 9:
					p.polyCloseManually = sub == 7;
					SetRadioItem(7, p.polyCloseManually, 7);
					SetRadioItem(9, !p.polyCloseManually, 7);
					break;
				case 10:
					p.polyAutosmooth = CheckBoxItem(10, 7);
					break;
				case 11:
				{
					BString text = TextItem(11, 7);
					char* end;
					double value = strtod(text.String(), &end);
					if (end == text.String() || value < 1 || value > 45) {
						alertRange(1, 45);
						return true;
					}
					p.shiftConstraint = float(value);
					break;
				}
			}
			break;
	}
	return false;
}


// PrefClose: the active panel's preferences as they were saved (the
// original restores the shown panel's object only)
void
TDialogPrefs::PanelClose()
{
	Preferences& p = AppPreferences();
	if (fPanel < 0)
		return;
	int group = kPanels[fPanel].group;
	if (group == 0) {
		Preferences keep = p;
		p = fBackup;
		p.pcHomeEnd = keep.pcHomeEnd;
		p.pcPageKeys = keep.pcPageKeys;
		p.liveTextWrap = keep.liveTextWrap;
		p.fourHandles = keep.fourHandles;
		p.polyCloseManually = keep.polyCloseManually;
		p.polyAutosmooth = keep.polyAutosmooth;
		p.shiftConstraint = keep.shiftConstraint;
		TSpellBar::AppSpeller().SetUserOptions(p.spelling);
	} else if (group == 1) {
		p.pcHomeEnd = fBackup.pcHomeEnd;
		p.pcPageKeys = fBackup.pcPageKeys;
		p.liveTextWrap = fBackup.liveTextWrap;
	} else {
		p.fourHandles = fBackup.fourHandles;
		p.polyCloseManually = fBackup.polyCloseManually;
		p.polyAutosmooth = fBackup.polyAutosmooth;
		p.shiftConstraint = fBackup.shiftConstraint;
	}
}

} // namespace gobe
