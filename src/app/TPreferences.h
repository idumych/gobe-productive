// The preferences (TAppPrefs, TWPPrefs, TGRPrefs; the add-on parts'
// panels aren't ported): factory settings from the PREF resources (41, 42,
// 204; the text part's are fixed), kept in the port's preferences file
// under the original's names (TPrefStore), and File > Preferences…
// (TDialogPrefs, DIAL/Item 40).
#ifndef GOBE_TPREFERENCES_H
#define GOBE_TPREFERENCES_H

#include <string>

#include "Spelling.h"
#include "TDialog.h"

namespace gobe {

struct Preferences {
	// TAppPrefs: General (Item 41)
	bool unlimitedUndo = false;
	int32 undoCount = 20;
	int32 measureUnits = 0;
	int32 rulerDivisions = 8;
	std::string defaultFont;
	int32 spacingType = 1;
	bool toolTips = true;
	// Spell Checking (Item 42)
	SpellPrefs spelling;
	// TWPPrefs (Item 308): PC Home & End Keys, PC Page Keys, Live Text Wrap
	bool pcHomeEnd = true, pcPageKeys = true, liveTextWrap = true;
	// TGRPrefs (Item 204)
	bool fourHandles = false, polyCloseManually = true, polyAutosmooth = true;
	float shiftConstraint = 45;
};

// the preferences as they are (read the first time)
Preferences& AppPreferences();
// TApp::WriteAllPreferences
void SavePreferences();

// the panels: their group (0 Application, 1 Word Processing, 2 Graphics),
// name and Item resource
struct PreferencePanel {
	int group;
	const char* name;
	int32 items;
};

class TDialogPrefs : public TDialog {
public:
	TDialogPrefs();
	virtual ~TDialogPrefs();

	// test hooks: the open dialog shows panel `panel` (in kPanels' order);
	// its control `sub` of the panel pressed (a check box toggled), or its
	// button `button` (sub -1)
	static void SelectPanel(int32 panel);
	static void Press(int32 sub, int32 button = -1);

	virtual void MessageReceived(BMessage* message);

protected:
	virtual void Init();
	virtual bool ItemHit(int32 index, BMessage* message);

private:
	void ShowPanel(int32 panel);
	// TPreferences::PrefInit, PrefDefault, PrefItemHit, PrefClose for the
	// panel shown
	void PanelInit(bool revert);
	void PanelDefault();
	bool PanelItemHit(int32 sub, BMessage* message);
	void PanelClose();

	int32 fPanel = -1;
	bool fChanged = false;		// TDialogPrefs +0x48
	Preferences fBackup;		// TAppPrefs::updateBackups
};

} // namespace gobe

#endif
