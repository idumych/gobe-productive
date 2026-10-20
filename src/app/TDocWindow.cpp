#include "TDocWindow.h"

#include <stdio.h>

#include <Alert.h>
#include <Application.h>
#include <FilePanel.h>
#include <Entry.h>
#include <NodeInfo.h>
#include <Path.h>
#include <LayoutBuilder.h>
#include <MenuBar.h>
#include <MenuItem.h>
#include <SeparatorItem.h>
#include <String.h>
#include <StringView.h>

#include "DocWriter.h"
#include "Document.h"
#include "Formats.h"
#include "GobeResources.h"
#include "TDialog.h"
#include "TDocView.h"
#include "TPageView.h"

#include <ScrollView.h>
#include "TMenu.h"

namespace gobe {

static const uint32 kMsgOpenDialog = 'gDLG';
// TDialog's controls: 'sD00' + index (TItemHandler in TDialog's setup)
static const uint32 kDialogCommand = 'sD00';

// File menu commands (MENU 2)
static const uint32 kCmdNew = 's001';
static const uint32 kCmdSave = 'sM02';
static const uint32 kCmdSaveAs = 'sM03';
static const uint32 kCmdRevert = 'sM04';
static const uint32 kCmdOpen = 'sM01';
static const uint32 kCmdQuit = 'sM09';


TDocWindow::TDocWindow(BRect frame, const char* title, Document* document)
	:
	BWindow(frame, title, B_DOCUMENT_WINDOW, B_AUTO_UPDATE_SIZE_LIMITS)
{
	TMenuBar* menuBar = InstallBaseMenus();
	menuBar->AddItem(MakeDialogsMenu());

	fStatus = new BStringView("status", "Gobe Productive port: menus and dialogs "
		"from the original resources");

	BView* desk;
	if (document != NULL) {
		fDocument = document;
		if (TPageView::CanShow(*document)) {
			TPageView* pages = new TPageView(*document);
			fPages = pages;
			desk = new BScrollView("pages", pages, 0, true, true, B_NO_BORDER);
		} else
			desk = new TDocView(*document);
		BString status;
		status << document->parts.parts.size() << " parts, "
			<< document->wordParts.size() << " with text, "
			<< document->styles.styles.size() << " styles";
		if (!document->warnings.empty())
			status << "; " << document->warnings.size() << " warnings";
		if (!document->slides.empty())
			status << "; slide " << document->currentSlide + 1 << " of "
				<< document->slides.size() << " (arrows, space or click to change)";
		fStatus->SetText(status.String());
	} else {
		desk = new BView("desk", B_WILL_DRAW);
		desk->SetViewColor(152, 152, 152);
	}
	desk->SetExplicitMinSize(BSize(400, 300));

	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.Add(menuBar)
		.Add(desk)
		.AddGroup(B_HORIZONTAL)
			.SetInsets(B_USE_SMALL_INSETS, 2, B_USE_SMALL_INSETS, 2)
			.Add(fStatus)
			.AddGlue()
		.End();
}


// TNormalDocWinHelper::InstallBaseMenus: MENU 2-6, with the part menu
// (TPartMenu) and a separator at the top of the Frame menu (MENU 5).
TMenuBar*
TDocWindow::InstallBaseMenus()
{
	TMenuBar* bar = new TMenuBar();
	bar->AddItem(new TMenu(2));
	bar->AddItem(new TMenu(3));
	bar->AddItem(new TMenu(4));

	TMenu* frameMenu = new TMenu(5);
	// TPartMenu(1, 's00T'): one item per part factory; parts aren't ported
	// yet, so list them from the PART resources' names as disabled items
	BMenu* parts = new BMenu("Parts");
	BMenuItem* none = new BMenuItem("(parts not ported yet)", NULL);
	none->SetEnabled(false);
	parts->AddItem(none);
	frameMenu->AddItem(parts, 0);
	frameMenu->AddItem(new BSeparatorItem(), 1);
	bar->AddItem(frameMenu);

	bar->AddItem(new TMenu(6));
	return bar;
}


BMenu*
TDocWindow::MakeDialogsMenu()
{
	BMenu* menu = new BMenu("Dialogs (debug)");
	for (const ResourceFile::Resource* r : GobeResources::File().All(FourCC("DIAL"))) {
		DialogData d;
		ParseDialog(r->data, d);
		BString label;
		label << r->id << ": " << (d.title.empty() ? "(untitled)" : d.title.c_str());
		BMessage* message = new BMessage(kMsgOpenDialog);
		message->AddInt32("id", r->id);
		menu->AddItem(new BMenuItem(label.String(), message));
	}
	return menu;
}


void
TDocWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgDocumentChanged:
			if (!fModified) {
				fModified = true;
				UpdateTitle();
			}
			return;
		case kCmdSave:
			if (fDocument != NULL)
				Save(fDocument->path);
			return;
		case kCmdSaveAs:
			if (fDocument == NULL)
				return;
			if (fSavePanel == NULL) {
				fSavePanel = new BFilePanel(B_SAVE_PANEL, new BMessenger(this));
				fSavePanel->SetSaveText(BPath(fDocument->path.c_str()).Leaf());
			}
			fSavePanel->Show();
			return;
		case B_SAVE_REQUESTED:
		{
			entry_ref directory;
			const char* name;
			if (message->FindRef("directory", &directory) == B_OK
				&& message->FindString("name", &name) == B_OK) {
				BPath path(&directory);
				path.Append(name);
				std::string from = fDocument->path;
				if (Save(path.Path())) {
					// a new file: the original's file type
					BNode source(from.c_str()), target(path.Path());
					BNodeInfo sourceInfo(&source), targetInfo(&target);
					char type[B_MIME_TYPE_LENGTH];
					if (sourceInfo.GetType(type) == B_OK)
						targetInfo.SetType(type);
					SetTitle(path.Leaf());
				}
			}
			return;
		}
		case kCmdRevert:
		{
			if (fDocument == NULL || !fModified)
				return;
			Document fresh;
			std::string error;
			if (!LoadDocument(fDocument->path, fresh, error)) {
				(new BAlert("Revert", error.c_str(), "OK"))->Go(NULL);
				return;
			}
			*fDocument = std::move(fresh);
			if (fPages != NULL)
				fPages->DocumentReloaded();
			fModified = false;
			UpdateTitle();
			return;
		}
		case kMsgSlideChanged:
		{
			BString status;
			status << "Slide " << message->GetInt32("slide", 0) + 1 << ": "
				<< message->GetString("name", "");
			fStatus->SetText(status.String());
			return;
		}
		case kCmdNew:
		{
			TDocWindow* window = new TDocWindow(Frame().OffsetByCopy(20, 20), "Untitled");
			window->Show();
			return;
		}
		case kCmdOpen:
			be_app->PostMessage(kCmdOpen);
			return;
		case kCmdQuit:
			be_app->PostMessage(B_QUIT_REQUESTED);
			return;
	}

	if (message->what == kMsgOpenDialog) {
		int32 id = message->GetInt32("id", 0);
		// most dialogs use the same id for DIAL and Item
		TDialog* dialog = new TDialog(id, id, kDialogCommand, this);
		dialog->Show();
		return;
	}

	BString text;
	BString code = FourCCString(message->what).c_str();
	text << "Command '" << code << "'";
	int32 value;
	if (message->FindInt32(kValueString, &value) == B_OK)
		text << " value " << value << " ('" << FourCCString(value).c_str() << "')";
	const char* name;
	if (message->FindString("name", &name) == B_OK)
		text << " name \"" << name << "\"";
	text << " - not ported yet";
	fStatus->SetText(text.String());
	printf("%s\n", text.String());

	switch (message->what) {
		case B_ABOUT_REQUESTED:
		case B_QUIT_REQUESTED:
			BWindow::MessageReceived(message);
			break;
	}
}


// Writes the document (only edited parts are written anew). The first save
// over an existing file in a session keeps the previous version as
// "<name>~".
bool
TDocWindow::Save(const std::string& path)
{
	if (fDocument == NULL)
		return false;
	if (path == fDocument->path && !fBackedUp) {
		BEntry backup((path + "~").c_str());
		if (!backup.Exists()) {
			std::string error;
			if (!WriteFile(path + "~", fDocument->bytes, error)) {
				(new BAlert("Save", error.c_str(), "OK"))->Go(NULL);
				return false;
			}
		}
		fBackedUp = true;
	}
	std::string error;
	if (!fDocument->Save(path, error)) {
		BString text;
		text << "Could not save \"" << path.c_str() << "\":\n" << error.c_str();
		(new BAlert("Save", text.String(), "OK"))->Go(NULL);
		return false;
	}
	if (fPages != NULL)
		fPages->DocumentReloaded();
	fModified = false;
	UpdateTitle();
	fStatus->SetText(("Saved " + path).c_str());
	return true;
}


void
TDocWindow::UpdateTitle()
{
	BString title = Title();
	if (title.EndsWith(" *"))
		title.Truncate(title.Length() - 2);
	if (fModified)
		title << " *";
	SetTitle(title.String());
}


bool
TDocWindow::QuitRequested()
{
	if (fModified) {
		BAlert* alert = new BAlert("Close", "Save changes to this document?",
			"Cancel", "Don't save", "Save", B_WIDTH_AS_USUAL, B_WARNING_ALERT);
		alert->SetShortcut(0, B_ESCAPE);
		int32 choice = alert->Go();
		if (choice == 0)
			return false;
		if (choice == 2 && !Save(fDocument->path))
			return false;
	}
	be_app->PostMessage(B_QUIT_REQUESTED);
	return true;
}

} // namespace gobe
