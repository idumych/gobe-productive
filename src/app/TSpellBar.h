// TSpellBar: the ribbon for spell checking (TSpellBar, TSpellBarHandler;
// Item 39): Begin ('sMC0', from the top), Continue ('sMC1', from the
// cursor), Skip, Learn, Replace ('sMC2'..'sMC4'), the replacements' menu
// and what was found. Document > Spell Check's commands come here too
// ('sMC5' shows or hides it, and with "dospell" begins). One spelling
// session (TSpellSession) serves every window.
#ifndef GOBE_TSPELLBAR_H
#define GOBE_TSPELLBAR_H

#include <String.h>

#include "Spelling.h"
#include "TRibbon.h"

class BButton;
class BMenuField;
class BStringView;

namespace gobe {

class TPageView;

class TSpellBar : public TRibbon {
public:
	TSpellBar(TPageView* pages);

	virtual void AttachedToWindow();
	virtual void Draw(BRect updateRect);
	virtual void MessageReceived(BMessage* message);
	virtual BSize MinSize();
	virtual BSize MaxSize();
	virtual BSize PreferredSize();
	virtual void Update();

	// TSpellBarHandler::MessageReceived: 'sMC0'..'sMC5'
	void HandleCommand(BMessage* message);
	bool SkipEnabled() const;
	bool LearnEnabled() const;
	bool ReplaceEnabled() const;
	// test hooks: what the bar shows
	BString Status() const;
	std::vector<std::string> Replacements() const;
	// test hook: replacement `index` chosen in the menu
	void ChooseReplacement(int32 index);

	// TSpellSession::StartSession (alerts unless `quiet`)
	static bool StartSession(bool quiet = false);
	static Speller& AppSpeller();
	// the spelling preferences (TAppPrefs: PREF 42's, then as Spell
	// Dictionaries… sets them; not saved yet)
	static SpellPrefs& Prefs();
	// TSpellSession::GetDictFolder: the Dictionaries folder (STR# 9's first
	// string) beside the original binary; empty (and an alert) if none
	static BString DictionariesFolder();

private:
	void Build();
	void ToggleVisible(bool showOnly);
	void DoStart(bool fromCursor, bool selectionEnd, bool clearSkipped);
	void DoSkip();
	void DoLearn();
	void DoReplace();
	void CheckSpelling(bool clearSkipped);
	void SetStartOnly(bool message);
	void SetSkipAndLearnOn();
	void SetReplaceOn();
	void MakePopUpNone();
	void SetStatus(const char* text);
	void RememberSelection();

	TPageView* fPages;
	BButton* fButtons[5] = {};
	BMenuField* fReplacements = NULL;
	BStringView* fStatus = NULL;
	float fHeight = 29;
	SpellPosition fAt;
	size_t fLength = 0;
	bool fChecking = false;		// TSpellBar +0x49c: words left to check
	bool fStartOnly = false;	// +0x4ac
	// the selection when the bar last stopped (the port's stand-in for
	// the original's count of its own selection changes, +0x4a8): another
	// halts checking
	std::vector<int64_t> fSelection;
};

} // namespace gobe

#endif
