// Writing Tools > Thesaurus… (ThesDialog) and Dictionary… (TDialogDictionary).
#include "TLexiconDialogs.h"

#include <Button.h>
#include <GroupLayout.h>
#include <OutlineListView.h>
#include <ScrollView.h>
#include <StringItem.h>
#include <TextControl.h>
#include <TextView.h>

#include "TDocWindow.h"

namespace gobe {

namespace {

TDialogThesaurus* sThesaurus = NULL;
TDialogDictionary* sDictionary = NULL;


BOutlineListView*
OutlineOf(BView* view)
{
	if (BScrollView* scroll = dynamic_cast<BScrollView*>(view))
		return dynamic_cast<BOutlineListView*>(scroll->Target());
	return dynamic_cast<BOutlineListView*>(view);
}


void
Enable(BView* view, bool on)
{
	if (BButton* button = dynamic_cast<BButton*>(view))
		button->SetEnabled(on);
}

} // namespace


// #pragma mark - TDialogThesaurus


void
TDialogThesaurus::Show(const BString& path, const BString& word)
{
	if (sThesaurus != NULL && sThesaurus->Lock()) {
		sThesaurus->fPath = path;
		sThesaurus->LookUp(word);
		sThesaurus->Activate(true);
		sThesaurus->Unlock();
		return;
	}
	(new TDialogThesaurus(path, word))->RunDialog();
}


TDialogThesaurus::TDialogThesaurus(const BString& path, const BString& word)
	:
	TDialog(52, 52, 'APPL', NULL),
	fPath(path),
	fWord(word)
{
	sThesaurus = this;
}


TDialogThesaurus::~TDialogThesaurus()
{
	if (sThesaurus == this)
		sThesaurus = NULL;
}


// ThesDialog::InitPostRecalc: Find and Replace off until there's
// something to find or replace; the word looked up at once
void
TDialogThesaurus::Init()
{
	fList = OutlineOf(Item(3));
	if (fList != NULL && fList->InvocationMessage() != NULL) {
		BMessage* selected = new BMessage(*fList->InvocationMessage());
		selected->AddBool("selected", true);
		fList->SetSelectionMessage(selected);
	}
	if (BTextControl* edit = dynamic_cast<BTextControl*>(Item(2))) {
		if (edit->Message() != NULL) {
			BMessage* modified = new BMessage(*edit->Message());
			modified->AddBool("isMod", true);
			edit->SetModificationMessage(modified);
		}
	}
	LookUp(fWord);
	MakeFocusItem(2);
}


// ThesDialog::LookUpWord: the word in the Word box, its parts and words
// in the list (TThesaurus::Lookup, else Manufacture)
void
TDialogThesaurus::LookUp(const BString& word)
{
	fWord = word;
	SetTextItem(2, word.String());
	if (fList == NULL)
		return;
	while (BListItem* item = fList->FullListItemAt(0)) {
		fList->RemoveItem(item);
		delete item;
	}
	for (const ThesaurusPart& part : ThesaurusLookup(fPath.String(), word.String())) {
		BStringItem* heading = new BStringItem(part.name.c_str(), 0, true);
		fList->AddItem(heading);
		for (const std::string& w : part.words)
			fList->AddUnder(new BStringItem(w.c_str(), 1), heading);
	}
	FixButtons();
}


void
TDialogThesaurus::FixButtons()
{
	Enable(Item(0), TextItem(2).Length() > 0);
	BListItem* chosen = fList != NULL ? fList->FullListItemAt(fList->FullListCurrentSelection())
		: NULL;
	Enable(Item(1), chosen != NULL && chosen->OutlineLevel() > 0);
}


bool
TDialogThesaurus::ItemHit(int32 index, BMessage* message)
{
	switch (index) {
		case 0:
			LookUp(TextItem(2));
			return true;
		case 1: {
			// Replace: the Find dialog's Replace with the chosen word
			BStringItem* chosen = fList != NULL ? dynamic_cast<BStringItem*>(
				fList->FullListItemAt(fList->FullListCurrentSelection())) : NULL;
			TDocWindow* window = TDocWindow::Front();
			if (chosen == NULL || chosen->OutlineLevel() == 0 || window == NULL)
				return true;
			BMessage replace('sME5');
			replace.AddString("replace", chosen->Text());
			window->PostMessage(&replace);
			return true;
		}
		case 2:
			FixButtons();
			return true;
		case 3: {
			FixButtons();
			// a word double-clicked: looked up
			BStringItem* chosen = fList != NULL ? dynamic_cast<BStringItem*>(
				fList->FullListItemAt(fList->FullListCurrentSelection())) : NULL;
			if (message != NULL && !message->GetBool("selected", false) && chosen != NULL
					&& chosen->OutlineLevel() > 0)
				LookUp(chosen->Text());
			return true;
		}
	}
	return TDialog::ItemHit(index, message);
}


// #pragma mark - TDialogDictionary


void
TDialogDictionary::Show(const BString& path, const BString& word)
{
	if (sDictionary != NULL && sDictionary->Lock()) {
		if (sDictionary->fPath != path) {
			sDictionary->fPath = path;
			sDictionary->fDictionary.Open(path.String());
		}
		sDictionary->SetTextItem(3, word.String());
		sDictionary->ShowEntry(sDictionary->fDictionary.Find(word.String()));
		sDictionary->Activate(true);
		sDictionary->Unlock();
		return;
	}
	(new TDialogDictionary(path, word))->RunDialog();
}


TDialogDictionary::TDialogDictionary(const BString& path, const BString& word)
	:
	TDialog(53, 53, 'APPL', NULL),
	fPath(path),
	fWord(word)
{
	sDictionary = this;
	fDictionary.Open(path.String());
}


TDialogDictionary::~TDialogDictionary()
{
	if (sDictionary == this)
		sDictionary = NULL;
}


// TDialogDictionary::InitPostRecalc: the entry's text in the dialog's
// own view (a read-only text view), the word looked up
void
TDialogDictionary::Init()
{
	if (BView* user = Item(4)) {
		fText = new BTextView("entry");
		fText->MakeEditable(false);
		fText->SetStylable(true);
		fText->SetWordWrap(true);
		BScrollView* scroll = new BScrollView("entry scroll", fText, 0, false, true);
		if (user->GetLayout() == NULL)
			user->SetLayout(new BGroupLayout(B_VERTICAL, 0));
		user->GetLayout()->AddView(scroll);
	}
	SetTextItem(3, fWord.String());
	ShowEntry(fDictionary.Find(fWord.String()));
	MakeFocusItem(3);
}


// TDialogDictionary::LookUpWord and FixFont: the headword in bold, then
// the text, the parts between backslashes bold; FixButtons: Next and
// Previous where there's an entry that way
void
TDialogDictionary::ShowEntry(size_t entry)
{
	if (fText == NULL)
		return;
	fEntry = entry;
	if (entry >= fDictionary.Count()) {
		fText->SetText("");
		Enable(Item(1), false);
		Enable(Item(2), false);
		return;
	}
	std::string headword = fDictionary.Headword(entry);
	std::vector<std::pair<size_t, size_t>> runs;
	std::string text = headword + "\n" + DictionaryRuns(fDictionary.Text(entry), runs);
	fText->SetText(text.c_str());
	BFont plain(be_plain_font), bold(be_bold_font);
	fText->SetFontAndColor(0, int32(text.size()), &plain);
	fText->SetFontAndColor(0, int32(headword.size()), &bold);
	for (const auto& run : runs)
		fText->SetFontAndColor(int32(headword.size() + 1 + run.first),
			int32(headword.size() + 1 + run.second), &bold);
	fText->ScrollToOffset(0);
	Enable(Item(1), entry + 1 < fDictionary.Count());
	Enable(Item(2), entry > 0);
}


bool
TDialogDictionary::ItemHit(int32 index, BMessage* message)
{
	switch (index) {
		case 0:
			ShowEntry(fDictionary.Find(TextItem(3).String()));
			return true;
		case 1:
			if (fEntry + 1 < fDictionary.Count()) {
				ShowEntry(fEntry + 1);
				SetTextItem(3, fDictionary.Headword(fEntry).c_str());
			}
			return true;
		case 2:
			if (fEntry > 0 && fEntry <= fDictionary.Count()) {
				ShowEntry(fEntry - 1);
				SetTextItem(3, fDictionary.Headword(fEntry).c_str());
			}
			return true;
		case 3:
			return true;
	}
	return TDialog::ItemHit(index, message);
}

} // namespace gobe
