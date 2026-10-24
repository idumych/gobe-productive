// The SpellBar.
#include "TSpellBar.h"

#include <Alert.h>
#include <Button.h>
#include <Entry.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <Path.h>
#include <PopUpMenu.h>
#include <StringView.h>
#include <Window.h>

#include <algorithm>

#include "Formats.h"
#include "GobeResources.h"
#include "TPageView.h"
#include "TPreferences.h"

namespace gobe {

namespace {

const uint32 kCommandBase = 'sMC0';		// Item 39's items send it plus their index

void
SimpleAlert(int32 index)
{
	(new BAlert("", GobeResources::IndexString(9, index).String(), "OK"))->Go(NULL);
}

} // namespace


Speller&
TSpellBar::AppSpeller()
{
	static Speller sSpeller;
	return sSpeller;
}


SpellPrefs&
TSpellBar::Prefs()
{
	return AppPreferences().spelling;
}


BString
TSpellBar::DictionariesFolder()
{
	BPath path(GobeResources::Path().String());
	if (path.InitCheck() == B_OK && path.GetParent(&path) == B_OK
			&& path.Append(GobeResources::IndexString(9, 0).String()) == B_OK
			&& BEntry(path.Path()).IsDirectory())
		return path.Path();
	SimpleAlert(1);
	return "";
}


// TSpellSession::StartSession
bool
TSpellBar::StartSession(bool quiet)
{
	Speller& speller = AppSpeller();
	if (speller.Started())
		return true;
	BString dictionaries = DictionariesFolder();
	if (dictionaries.IsEmpty())
		return false;
	std::vector<std::string> languages;
	for (int32 i = 0; i < 4; i++)
		languages.push_back(GobeResources::IndexString(18, i).String());
	int error = speller.Start(dictionaries.String(), Prefs(), languages);
	if (error != 0) {
		if (!quiet)
			SimpleAlert(error);
		return false;
	}
	return true;
}


TSpellBar::TSpellBar(TPageView* pages)
	:
	TRibbon("SpellBar"),
	fPages(pages)
{
	Build();
}


// TSpellBar::LoadAllElements: the five buttons from (2, 2), 5 apart; the
// replacements' menu after them, 110 wide with its gap; the message after
// it; a line at the bottom (TSeparatorElement 'BSep'). (The menu and the
// message are centred on the buttons, which are taller on Haiku.)
void
TSpellBar::Build()
{
	ItemList list;
	const ResourceFile::Resource* r = GobeResources::File().Find(FourCC("Item"), 39);
	if (r != NULL)
		ParseItems(r->data, list);
	while (list.items.size() < 7)
		list.items.push_back(ItemData());
	BPoint at(2, 2);
	float buttonHeight = 20;
	for (int32 i = 0; i < 5; i++) {
		const ItemData& item = list.items[size_t(i)];
		BButton* button = new BButton(BRect(at, at), item.label.c_str(), item.label.c_str(),
			new BMessage(kCommandBase + uint32(std::max<int32>(item.index, i))));
		button->ResizeToPreferred();
		button->SetEnabled(item.enabled);
		AddChild(button);
		fButtons[i] = button;
		buttonHeight = button->Bounds().Height();
		at.x += button->Bounds().Width() + 5;
	}
	fHeight = std::max(29.0f, buttonHeight + 5);

	BPopUpMenu* menu = new BPopUpMenu("", true, true);
	BMenuItem* none = new BMenuItem(GobeResources::IndexString(5, 4).String(), NULL);
	menu->AddItem(none);
	none->SetMarked(true);
	fReplacements = new BMenuField(BRect(at.x, 0, at.x + 105, 20), "replacements", NULL, menu);
	fReplacements->SetDivider(0);
	fReplacements->ResizeToPreferred();
	fReplacements->ResizeTo(105, fReplacements->Bounds().Height());
	fReplacements->MoveTo(at.x, 2 + floorf((buttonHeight - fReplacements->Bounds().Height()) / 2));
	fReplacements->SetEnabled(list.items[5].enabled);
	AddChild(fReplacements);
	at.x += 110;

	fStatus = new BStringView(BRect(at.x, 0, at.x + 400, 20), "status",
		list.items[6].label.c_str(), B_FOLLOW_LEFT_RIGHT | B_FOLLOW_TOP);
	fStatus->ResizeToPreferred();
	fStatus->ResizeTo(1000, fStatus->Bounds().Height());
	fStatus->MoveTo(at.x, 2 + floorf((buttonHeight - fStatus->Bounds().Height()) / 2));
	AddChild(fStatus);
}


void
TSpellBar::AttachedToWindow()
{
	TRibbon::AttachedToWindow();
	for (BButton* button : fButtons)
		button->SetTarget(this);
	rgb_color panel = ViewColor();
	fStatus->SetViewColor(panel);
	fStatus->SetLowColor(panel);
}


void
TSpellBar::Draw(BRect updateRect)
{
	TRibbon::Draw(updateRect);
	BRect bounds = Bounds();
	SetHighColor(0xa0, 0xa0, 0xa0);
	StrokeLine(BPoint(bounds.left, bounds.bottom), BPoint(bounds.right, bounds.bottom));
}


BSize
TSpellBar::MinSize()
{
	return BSize(50, fHeight);
}


BSize
TSpellBar::MaxSize()
{
	return BSize(B_SIZE_UNLIMITED, fHeight);
}


BSize
TSpellBar::PreferredSize()
{
	return BSize(400, fHeight);
}


void
TSpellBar::MessageReceived(BMessage* message)
{
	if (message->what >= kCommandBase && message->what <= kCommandBase + 4) {
		HandleCommand(message);
		return;
	}
	TRibbon::MessageReceived(message);
}


// TSpellBarHandler::MessageReceived
void
TSpellBar::HandleCommand(BMessage* message)
{
	if (message->what == kCommandBase + 5) {
		if (!StartSession())
			return;
		ToggleVisible(false);
		bool spell;
		if (!IsHidden(this) && message->FindBool("dospell", &spell) == B_OK && spell) {
			if (Window() != NULL)
				Window()->UpdateIfNeeded();
			DoStart(false, false, true);
		}
		return;
	}
	if (message->what < kCommandBase || message->what > kCommandBase + 4)
		return;
	if (!StartSession())
		return;
	if (IsHidden(this)) {
		ToggleVisible(true);
		if (Window() != NULL)
			Window()->UpdateIfNeeded();
	}
	switch (message->what - kCommandBase) {
		case 0:
		case 1:
			DoStart(message->what == kCommandBase + 1, false, true);
			break;
		case 2:
			DoSkip();
			break;
		case 3:
			DoLearn();
			break;
		case 4:
			DoReplace();
			break;
	}
}


// TRibbonMgr::ToggleRibbonVisible: shown or hidden (only shown if
// `showOnly`); shown, its contents brought up to date (UpdateContents)
void
TSpellBar::ToggleVisible(bool showOnly)
{
	if (showOnly && !IsHidden(this))
		return;
	if (IsHidden(this)) {
		Show();
		// the views under it laid out again before anything is scrolled
		if (Window() != NULL)
			Window()->Layout(false);
		if (!fStartOnly)
			SetStartOnly(true);
		RememberSelection();
	} else
		Hide();
}


// TSpellBar::DoStart: the text from the cursor (the selection's start or,
// if `selectionEnd`, its end; TTextAccessor::StartFromPosition backs up to
// the word) or from the top
void
TSpellBar::DoStart(bool fromCursor, bool selectionEnd, bool clearSkipped)
{
	const Document& doc = fPages->Doc();
	fChecking = false;
	if (fromCursor && fPages->fCaret.active) {
		TextPosition from, to;
		if (fPages->TextSelection(from, to)) {
			TextPosition at = selectionEnd ? to : from;
			fAt = {fPages->fCaret.part, at.paragraph, at.offset};
		} else
			fAt = {fPages->fCaret.part, fPages->fCaret.paragraph, fPages->fCaret.offset};
		fChecking = SpellStartFromPosition(doc, fAt);
	}
	if (!fChecking)
		fChecking = SpellStartFromTop(doc, fAt);
	StartSession();
	CheckSpelling(clearSkipped);
}


void
TSpellBar::DoSkip()
{
	AppSpeller().Skip();
	fAt.offset += fLength;
	CheckSpelling(false);
}


void
TSpellBar::DoLearn()
{
	fPages->SelectText(fAt.part, {fAt.paragraph, fAt.offset}, fAt.offset);
	AppSpeller().Learn();
	fAt.offset += fLength;
	CheckSpelling(false);
}


// TSpellBar::DoReplace: the word replaced by the menu's choice, which it
// becomes from now on; checking goes on after it
void
TSpellBar::DoReplace()
{
	BMenu* menu = fReplacements->Menu();
	BMenuItem* marked = menu->FindMarked();
	if (marked == NULL)
		return;
	int32 index = menu->IndexOf(marked);
	fPages->ReplaceText(fAt.part, {fAt.paragraph, fAt.offset}, fAt.offset + fLength,
		marked->Label());
	AppSpeller().AddReplacement(index);
	DoStart(true, true, false);
}


// TSpellBar::checkSpelling: on to the next word that isn't good; it is
// selected, its replacements in the menu and what's wrong with it said. A
// word with a replacement chosen before is replaced and checking goes on.
void
TSpellBar::CheckSpelling(bool clearSkipped)
{
	Speller& speller = AppSpeller();
	BString message = GobeResources::IndexString(9, 11);
	SetStartOnly(false);
	if (clearSkipped)
		speller.ClearSkipped();
	int result = 0;
	if (fChecking)
		result = FindMisspelling(fPages->Doc(), speller, fAt, fLength);
	if (result == 0) {
		fChecking = false;
		SetStatus(GobeResources::IndexString(9, 10).String());
		fStartOnly = false;
		RememberSelection();
		return;
	}
	if (result == Speller::kAutoReplace) {
		fPages->ReplaceText(fAt.part, {fAt.paragraph, fAt.offset}, fAt.offset + fLength,
			speller.AutoReplacement());
		DoStart(true, true, false);
		return;
	}
	if (result == Speller::kNoSession)
		message = GobeResources::IndexString(9, 2);
	else {
		fPages->SelectText(fAt.part, {fAt.paragraph, fAt.offset}, fAt.offset + fLength);
		// TSpeller::RebuildAltWordPopUp
		std::vector<std::string> alternatives = speller.Alternatives();
		BMenu* menu = fReplacements->Menu();
		while (BMenuItem* item = menu->RemoveItem(int32(0)))
			delete item;
		for (const std::string& word : alternatives)
			menu->AddItem(new BMenuItem(word.c_str(), NULL));
		if (alternatives.empty()) {
			BMenuItem* none = new BMenuItem(GobeResources::IndexString(5, 4).String(), NULL);
			none->SetEnabled(false);
			menu->AddItem(none);
			MakePopUpNone();
		} else {
			menu->ItemAt(0)->SetMarked(true);
			SetReplaceOn();
		}
		SetSkipAndLearnOn();
		message = GobeResources::IndexString(9, result + 4);
	}
	SetStatus(message.String());
	RememberSelection();
}


// TSpellBar::setStartOnly: Skip, Learn and Replace off, the menu "None";
// the message to press Begin or Continue
void
TSpellBar::SetStartOnly(bool message)
{
	if (fStartOnly)
		return;
	fStartOnly = true;
	if (message)
		SetStatus(GobeResources::IndexString(9, 12).String());
	for (int32 i = 2; i < 5; i++)
		fButtons[i]->SetEnabled(false);
	MakePopUpNone();
}


void
TSpellBar::SetSkipAndLearnOn()
{
	fStartOnly = false;
	fButtons[3]->SetEnabled(true);
	fButtons[2]->SetEnabled(true);
}


void
TSpellBar::SetReplaceOn()
{
	fStartOnly = false;
	fButtons[4]->SetEnabled(true);
	fReplacements->SetEnabled(true);
}


// TSpellBar::makePopUpNone: the menu only "None" (STR# 5), marked, off
void
TSpellBar::MakePopUpNone()
{
	BMenu* menu = fReplacements->Menu();
	BString none = GobeResources::IndexString(5, 4);
	if (menu->CountItems() == 1 && none == menu->ItemAt(0)->Label())
		return;
	while (BMenuItem* item = menu->RemoveItem(int32(0)))
		delete item;
	BMenuItem* item = new BMenuItem(none.String(), NULL);
	menu->AddItem(item);
	item->SetMarked(true);
	fReplacements->SetEnabled(false);
}


void
TSpellBar::SetStatus(const char* text)
{
	fStatus->SetText(text);
}


void
TSpellBar::RememberSelection()
{
	const TPageView::Caret& c = fPages->fCaret;
	const TPageView::Caret& a = fPages->fAnchor;
	fSelection = {c.active, int64_t(c.part), int64_t(c.paragraph), int64_t(c.offset), a.active,
		int64_t(a.part), int64_t(a.paragraph), int64_t(a.offset)};
}


// TSpellBar::UpdateContents: the selection changed by other than the bar
// halts checking until Begin or Continue
void
TSpellBar::Update()
{
	if (fStartOnly || IsHidden(this))
		return;
	std::vector<int64_t> before = fSelection;
	RememberSelection();
	if (fSelection != before)
		SetStartOnly(true);
}


bool
TSpellBar::SkipEnabled() const
{
	return fButtons[2]->IsEnabled();
}


bool
TSpellBar::LearnEnabled() const
{
	return fButtons[3]->IsEnabled();
}


bool
TSpellBar::ReplaceEnabled() const
{
	return fButtons[4]->IsEnabled();
}


BString
TSpellBar::Status() const
{
	return fStatus->Text();
}


std::vector<std::string>
TSpellBar::Replacements() const
{
	std::vector<std::string> list;
	BMenu* menu = fReplacements->Menu();
	for (int32 i = 0; BMenuItem* item = menu->ItemAt(i); i++)
		list.push_back(std::string(item->IsMarked() ? "*" : "") + item->Label());
	return list;
}


void
TSpellBar::ChooseReplacement(int32 index)
{
	if (BMenuItem* item = fReplacements->Menu()->ItemAt(index))
		item->SetMarked(true);
}

} // namespace gobe
