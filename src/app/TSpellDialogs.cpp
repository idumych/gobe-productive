// The spelling dictionaries' dialogs.
#include "TSpellDialogs.h"

#include <Alert.h>
#include <Button.h>
#include <Directory.h>
#include <File.h>
#include <FilePanel.h>
#include <ListView.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <Messenger.h>
#include <NodeInfo.h>
#include <Path.h>
#include <ScrollView.h>
#include <TextControl.h>

#include <algorithm>
#include <cstring>

#include "GobeResources.h"
#include "Spelling.h"
#include "TPreferences.h"
#include "TSpellBar.h"
#include "WordCount.h"

namespace gobe {

namespace {

const uint32 kCommand = 'APPL';

void
Alert(int32 list, int32 index)
{
	(new BAlert("", GobeResources::IndexString(list, index).String(), "OK"))->Go(NULL);
}


// what a control of the dialog sends (TDialog's control messages)
BMessage*
DialogMessage(int32 dialog, int32 index)
{
	BMessage* message = new BMessage(kCommand + uint32(index));
	message->AddInt32("dialog", dialog);
	message->AddInt32("index", index);
	return message;
}


void
EnableButton(BView* view, bool on)
{
	if (BButton* button = dynamic_cast<BButton*>(view))
		button->SetEnabled(on);
}

} // namespace


// #pragma mark - TDialogSpellDictionaries


TDialogSpellDictionaries::TDialogSpellDictionaries()
	:
	TDialog(51, 51, kCommand, NULL)
{
}


BMenu*
TDialogSpellDictionaries::PopUp(int32 index) const
{
	BMenuField* field = dynamic_cast<BMenuField*>(Item(index));
	return field != NULL ? field->Menu() : NULL;
}


void
TDialogSpellDictionaries::EnableUser(bool on)
{
	if (BMenuField* field = dynamic_cast<BMenuField*>(Item(5)))
		field->SetEnabled(on);
	EnableButton(Item(7), on);
}


// TDialogSpellDictionaries::Init, TSpellSession::BuildLexPopUps
void
TDialogSpellDictionaries::Init()
{
	SpellPrefs& prefs = TSpellBar::Prefs();
	fMain = prefs.mainDict.c_str();
	fUser = prefs.userDict.c_str();
	BMenu* main = PopUp(3);
	BMenu* user = PopUp(5);
	BString folder = TSpellBar::DictionariesFolder();
	if (main == NULL || user == NULL || folder.IsEmpty()) {
		PostMessage(B_QUIT_REQUESTED);
		return;
	}
	while (BMenuItem* item = main->RemoveItem(int32(0)))
		delete item;
	while (BMenuItem* item = user->RemoveItem(int32(0)))
		delete item;
	std::vector<std::string> languages, users;
	DictionaryChoices(folder.String(), languages, users);
	for (const std::string& name : languages)
		main->AddItem(new BMenuItem(name.c_str(), DialogMessage(51, 3)));
	for (const std::string& name : users)
		user->AddItem(new BMenuItem(name.c_str(), DialogMessage(51, 5)));

	if (BMenuItem* item = main->FindItem(fMain.String()))
		item->SetMarked(true);
	else if (main->CountItems() < 1) {
		Alert(9, 3);
		PostMessage(B_QUIT_REQUESTED);
		return;
	} else {
		main->ItemAt(0)->SetMarked(true);
		fMain = main->ItemAt(0)->Label();
		prefs.mainDict = fMain.String();
		fMainChanged = true;
	}
	if (BMenuItem* item = user->FindItem(fUser.String()))
		item->SetMarked(true);
	else if (user->CountItems() < 1)
		EnableUser(false);
	else {
		user->ItemAt(0)->SetMarked(true);
		fUser = user->ItemAt(0)->Label();
		prefs.userDict = fUser.String();
		fUserChanged = true;
	}
}


bool
TDialogSpellDictionaries::ItemHit(int32 index, BMessage* message)
{
	switch (index) {
		case 0:
			if (fMainChanged || fUserChanged) {
				SpellPrefs& prefs = TSpellBar::Prefs();
				if (fMainChanged)
					prefs.mainDict = fMain.String();
				if (fUserChanged)
					prefs.userDict = fUser.String();
				TSpellBar::AppSpeller().Close();
				TSpellBar::StartSession();
				SavePreferences();
			}
			break;
		case 3:
		case 5:
		{
			BMenuItem* marked = PopUp(index)->FindMarked();
			if (marked != NULL) {
				(index == 3 ? fMain : fUser) = marked->Label();
				(index == 3 ? fMainChanged : fUserChanged) = true;
			}
			return true;
		}
		case 6:
		{
			const char* name;
			if (message->FindString("name", &name) != B_OK) {
				(new TDialogSpellName(this, *message))->RunDialog();
				return true;
			}
			fUser = name;
			fUserChanged = true;
			EnableUser(true);
			BMenuItem* item = new BMenuItem(name, DialogMessage(51, 5));
			PopUp(5)->AddItem(item);
			item->SetMarked(true);
			(new TDialogSpellEditUser(fUser.String()))->RunDialog();
			return true;
		}
		case 7:
			(new TDialogSpellEditUser(fUser.String()))->RunDialog();
			return true;
	}
	return TDialog::ItemHit(index, message);
}


// #pragma mark - TDialogSpellName


TDialogSpellName::TDialogSpellName(BLooper* dialog, const BMessage& reply)
	:
	TDialog(31, 31, kCommand, NULL),
	fDialog(dialog),
	fReply(reply)
{
}


// TDialogGenericGetString::Init: the title and the field's label
void
TDialogSpellName::Init()
{
	BString title = GobeResources::IndexString(9, 13);
	SetTitle(title.String());
	SetLabelItem(2, title.String());
	SetTextItem(2, "");
	MakeFocusItem(2);
}


// TDialogSpellName::ValidateAndSendMsg
bool
TDialogSpellName::ItemHit(int32 index, BMessage* message)
{
	if (index == 2)
		return true;
	if (index == 0) {
		BString name = TextItem(2);
		BString folder = TSpellBar::DictionariesFolder();
		if (folder.IsEmpty() || !CreateUserDictionary(folder.String(), name.String())) {
			Alert(9, 14);
			MakeFocusItem(2);
			return true;
		}
		BMessage reply(fReply);
		reply.AddString("name", name);
		fDialog->PostMessage(&reply);
	}
	return TDialog::ItemHit(index, message);
}


// #pragma mark - TDialogSpellEditUser


TDialogSpellEditUser::TDialogSpellEditUser(const char* name)
	:
	TDialog(50, 50, kCommand, NULL),
	fName(name)
{
}


// TSpeller::EditUserSessionStop: the spelling session again
TDialogSpellEditUser::~TDialogSpellEditUser()
{
	delete fOpenPanel;
	delete fSavePanel;
	TSpellBar::StartSession();
}


// TDialogSpellEditUser::Init, TSpeller::EditUserSessionStart,
// EditUserSessionBuildListBox
void
TDialogSpellEditUser::Init()
{
	TSpellBar::AppSpeller().Close();
	BString folder = TSpellBar::DictionariesFolder();
	if (folder.IsEmpty()) {
		PostMessage(B_QUIT_REQUESTED);
		return;
	}
	fLexicon.reset(new ssce::TextLexicon);
	std::string error;
	BString path = folder;
	path << "/" << fName;
	if (!fLexicon->Open(path.String(), error)) {
		Alert(9, 4);
		PostMessage(B_QUIT_REQUESTED);
		return;
	}
	if (BScrollView* scroll = dynamic_cast<BScrollView*>(Item(3)))
		fList = dynamic_cast<BListView*>(scroll->Target());
	if (fList == NULL)
		fList = dynamic_cast<BListView*>(Item(3));
	if (fList != NULL) {
		fList->SetListType(B_MULTIPLE_SELECTION_LIST);
		fList->SetSelectionMessage(DialogMessage(50, 3));
		for (const auto& entry : fLexicon->Entries())
			fList->AddItem(new BStringItem(Utf8FromLatin1(entry.word).c_str()));
		fList->Select(-1, false);
	}
	if (BTextControl* edit = dynamic_cast<BTextControl*>(Item(2))) {
		BMessage* modified = DialogMessage(50, 2);
		modified->AddBool("isMod", true);
		edit->SetModificationMessage(modified);
	}
	EnableButton(Item(4), false);
	EnableButton(Item(5), false);
	MakeFocusItem(2);
}


bool
TDialogSpellEditUser::ItemHit(int32 index, BMessage* message)
{
	switch (index) {
		case 0:
			// TSpeller::EditUserSessionAddWord, RemoveWord
			for (const auto& change : fChanges) {
				std::string word = Latin1FromUtf8(change.first).substr(0, 64);
				if (change.second)
					fLexicon->Add(word);
				else
					fLexicon->Delete(word);
			}
			break;
		case 2:
			EnableButton(Item(4), TextItem(2).Length() > 0);
			return true;
		case 3:
		{
			if (fList == NULL)
				return true;
			int32 selected = 0;
			BStringItem* first = NULL;
			for (int32 i; (i = fList->CurrentSelection(selected)) >= 0; selected++)
				if (selected == 0)
					first = dynamic_cast<BStringItem*>(fList->ItemAt(i));
			EnableButton(Item(5), selected > 0);
			SetTextItem(2, selected == 1 && first != NULL ? first->Text() : "");
			return true;
		}
		case 4:
			AddNewWord(TextItem(2).String());
			SetTextItem(2, "");
			return true;
		case 5:
			for (int32 i = fList != NULL ? fList->CountItems() - 1 : -1; i >= 0; i--) {
				BStringItem* item = dynamic_cast<BStringItem*>(fList->ItemAt(i));
				if (item == NULL || !item->IsSelected())
					continue;
				fChanges.push_back({item->Text(), false});
				delete fList->RemoveItem(i);
			}
			SetTextItem(2, "");
			return true;
		case 6:
			if (fOpenPanel == NULL)
				fOpenPanel = new BFilePanel(B_OPEN_PANEL, new BMessenger(this), NULL,
					B_FILE_NODE, false, DialogMessage(50, 20));
			fOpenPanel->Show();
			return true;
		case 7:
			if (fSavePanel == NULL)
				fSavePanel = new BFilePanel(B_SAVE_PANEL, new BMessenger(this), NULL, 0, false,
					DialogMessage(50, 21));
			fSavePanel->Show();
			return true;
		case 20:
			ImportText(message);
			return true;
		case 21:
			ExportText(message);
			return true;
	}
	return TDialog::ItemHit(index, message);
}


// TDialogSpellEditUser::addNewWord: in order (strcmp), unless it's there
bool
TDialogSpellEditUser::AddNewWord(const std::string& word)
{
	if (fList == NULL)
		return false;
	int32 at = 0;
	int order = 1;
	for (; at < fList->CountItems(); at++) {
		BStringItem* item = dynamic_cast<BStringItem*>(fList->ItemAt(at));
		order = strcmp(word.c_str(), item != NULL ? item->Text() : "");
		if (order < 1)
			break;
	}
	if (order == 0)
		return false;
	fList->AddItem(new BStringItem(word.c_str()), at);
	fChanges.push_back({word, true});
	return true;
}


// TDialogSpellEditUser::importText: the file's words (GetNextWordInBlock)
// under 64 bytes
void
TDialogSpellEditUser::ImportText(BMessage* message)
{
	entry_ref ref;
	message->FindRef("refs", &ref);
	BFile file(&ref, B_READ_ONLY);
	off_t size = 0;
	if (file.InitCheck() != B_OK || file.GetSize(&size) != B_OK) {
		Alert(6, 6);
		return;
	}
	std::string text(size_t(size), '\0');
	ssize_t read = file.Read(&text[0], size_t(size));
	if (read < 0) {
		Alert(6, 6);
		return;
	}
	text.resize(size_t(read));
	size_t at = 0, length;
	bool digits;
	while (NextWordInBlock(text, at, length, digits)) {
		if (length < 64)
			AddNewWord(text.substr(at, length));
		at += length;
	}
}


// TDialogSpellEditUser::exportText: a word a line, a text/plain file
void
TDialogSpellEditUser::ExportText(BMessage* message)
{
	entry_ref ref;
	const char* name = NULL;
	message->FindRef("directory", &ref);
	message->FindString("name", &name);
	BDirectory directory(&ref);
	BFile file;
	if (directory.InitCheck() != B_OK || name == NULL
			|| directory.CreateFile(name, &file, false) != B_OK) {
		Alert(6, 7);
		return;
	}
	BNodeInfo(&file).SetType("text/plain");
	for (int32 i = 0; fList != NULL && i < fList->CountItems(); i++) {
		BStringItem* item = dynamic_cast<BStringItem*>(fList->ItemAt(i));
		if (item == NULL)
			continue;
		file.Write(item->Text(), strlen(item->Text()));
		file.Write("\n", 1);
	}
}

} // namespace gobe
