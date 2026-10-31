// Writing Tools > Thesaurus… (ThesDialog, DIAL/Item 52) and Dictionary…
// (TDialogDictionary, DIAL/Item 53), on the files core/Lexicon reads.
#ifndef GOBE_TLEXICON_DIALOGS_H
#define GOBE_TLEXICON_DIALOGS_H

#include <String.h>

#include "Lexicon.h"
#include "TDialog.h"

class BOutlineListView;
class BTextView;

namespace gobe {

// ThesDialog: Word (2) looked up by Find (0) into the list (3), each part
// of speech with its words under it; Replace (1) sends the front document
// window 'sME5' with the chosen word as "replace" (the Find dialog's
// Replace: the selected text replaced). Find is on when there is a word,
// Replace when a word (not a part) is chosen; a word double-clicked is
// looked up.
class TDialogThesaurus : public TDialog {
public:
	// the dialog for `word` from the thesaurus at `path`; one at a time
	static void Show(const BString& path, const BString& word);

protected:
	virtual void Init();
	virtual bool ItemHit(int32 index, BMessage* message);

private:
	TDialogThesaurus(const BString& path, const BString& word);
	virtual ~TDialogThesaurus();
	void LookUp(const BString& word);
	void FixButtons();

	BString fPath, fWord;
	BOutlineListView* fList = NULL;
};

// TDialogDictionary: Word (3) looked up by Find (0); the entry's text in
// the dialog's own view (4), the parts between backslashes in bold
// (FixFont); Next (1) and Previous (2) the neighbouring entries.
class TDialogDictionary : public TDialog {
public:
	static void Show(const BString& path, const BString& word);

protected:
	virtual void Init();
	virtual bool ItemHit(int32 index, BMessage* message);

private:
	TDialogDictionary(const BString& path, const BString& word);
	virtual ~TDialogDictionary();
	void ShowEntry(size_t entry);

	BString fPath, fWord;
	Dictionary fDictionary;
	size_t fEntry = 0;
	BTextView* fText = NULL;
};

} // namespace gobe

#endif
