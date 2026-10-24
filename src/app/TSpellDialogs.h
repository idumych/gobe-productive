// Document > Spell Check > Spell Dictionaries… and the dialogs it opens.
#ifndef GOBE_TSPELLDIALOGS_H
#define GOBE_TSPELLDIALOGS_H

#include <memory>

#include "Ssce.h"
#include "TDialog.h"

class BFilePanel;
class BMenu;
class BListView;

namespace gobe {

// TDialogSpellDictionaries (DIAL/Item 51): the main dictionary's language
// (3) and the user dictionary (5) from the Dictionaries folder (the
// preferences' marked, else the first); New (6: a name, then the new
// dictionary edited) and Edit (7: the user dictionary edited). OK sets the
// preferences and starts the spelling session again.
class TDialogSpellDictionaries : public TDialog {
public:
	TDialogSpellDictionaries();

protected:
	virtual void Init();
	virtual bool ItemHit(int32 index, BMessage* message);

private:
	BMenu* PopUp(int32 index) const;
	void EnableUser(bool on);

	BString fMain, fUser;
	bool fMainChanged = false, fUserChanged = false;
};

// TDialogSpellName (TDialogGenericGetString, DIAL/Item 31): a new user
// dictionary's name (STR# 9, 13); OK makes it (TSpellSession::
// CreateNewUserLex; else STR# 9, 14) and sends `reply` with "name" to
// `dialog`
class TDialogSpellName : public TDialog {
public:
	TDialogSpellName(BLooper* dialog, const BMessage& reply);

protected:
	virtual void Init();
	virtual bool ItemHit(int32 index, BMessage* message);

private:
	BLooper* fDialog;
	BMessage fReply;
};

// TDialogSpellEditUser (DIAL/Item 50): a user dictionary's words (3, any
// number selected); the word (2) added (Add Word, 4) or the selected ones
// removed (Remove Word, 5); Import Text… (6: a file's words, under 64
// bytes each) and Export Text… (7: the list, a word a line, text/plain).
// OK writes the additions and removals. The spelling session is closed
// while it is open (TSpeller::EditUserSessionStart, Stop).
class TDialogSpellEditUser : public TDialog {
public:
	TDialogSpellEditUser(const char* name);
	virtual ~TDialogSpellEditUser();

protected:
	virtual void Init();
	virtual bool ItemHit(int32 index, BMessage* message);

private:
	bool AddNewWord(const std::string& word);
	void ImportText(BMessage* message);
	void ExportText(BMessage* message);

	BString fName;
	std::unique_ptr<ssce::TextLexicon> fLexicon;
	BListView* fList = NULL;
	// the changes OK makes: a word and whether it's added
	std::vector<std::pair<std::string, bool>> fChanges;
	BFilePanel* fOpenPanel = NULL;
	BFilePanel* fSavePanel = NULL;
};

} // namespace gobe

#endif
