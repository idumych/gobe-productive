// TApp: application entry. Loads the original binary's resources, then opens a
// document window. The real TApp (splash, preferences, part factories,
// document management) comes with later milestones.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <Alert.h>
#include <Looper.h>
#include <Roster.h>
#include <exception>
#include <Application.h>
#include <Entry.h>
#include <FilePanel.h>
#include <MenuBar.h>
#include <Path.h>
#include <String.h>

#include <string>
#include <vector>

#include <Bitmap.h>
#include <BitmapStream.h>
#include <File.h>
#include <TranslatorRoster.h>

#include "Document.h"
#include "GobeResources.h"
#include "TDocWindow.h"
#include "TPreferences.h"
#include "TPageView.h"
#include "TDialogNewDoc.h"
#include "TDialogSplash.h"
#include "TRecentDocs.h"
#include "TTranslate.h"
#include "TWindowArrange.h"

namespace gobe {

// TLeafSheet::TLeafSheet: a new sheet's units and ruler divisions are the
// preferences'
static void
NewSheetPreferences(Document& document)
{
	if (document.sheets.sheets.empty())
		return;
	LeafSheet& sheet = document.sheets.sheets[0];
	sheet.unit = uint32_t(AppPreferences().measureUnits);
	sheet.divisions = uint32_t(AppPreferences().rulerDivisions);
	document.sheetsChanged = true;
}


static const char* kSignature = "application/x-vnd.gobe-port.productive";


class TApp : public BApplication {
public:
	TApp()
		:
		BApplication(kSignature),
		fOpenMenu(-1),
		fOpenDialog(-1),
		fReady(false),
		fHasDocument(false),
		fNewDocument(false),
		fNewSpreadsheet(false),
		fOpenPanel(NULL),
		fWindowCount(0),
		fSlide(-1)
	{
	}

	// Test hooks (the VM is driven without a mouse):
	//   --menu <index>    open that menu of the menu bar
	//   --dialog <id>     open that DIAL resource
	//   --render <png>    draw the (first) document's pages into a PNG, quit
	//   --slide <n>       show slide n (from 1) of a presentation
	//   --do <script>     with --render: clicks, keys, saves first (TPageView)
	static void PrintUsage(const char* prog)
	{
		fprintf(stderr,
		    "usage: %s [--menu <n>] [--dialog <id>] [--render <png>]\n"
		    "        [--slide <n>] [--do <script>] [--new] [--new-spreadsheet] <document> ...\n",
		    prog);
	}

	virtual void ArgvReceived(int32 argc, char** argv)
	{
		const char* prog = argc > 0 ? argv[0] : "GobeProductive";
		for (int32 i = 1; i < argc; i++) {
			if (strcmp(argv[i], "--menu") == 0) {
				if (i + 1 >= argc) {
					fprintf(stderr, "--menu requires an argument\n");
					PrintUsage(prog);
					continue;
				}
				fOpenMenu = atoi(argv[++i]);
			} else if (strcmp(argv[i], "--dialog") == 0) {
				if (i + 1 >= argc) {
					fprintf(stderr, "--dialog requires an argument\n");
					PrintUsage(prog);
					continue;
				}
				fOpenDialog = atoi(argv[++i]);
			} else if (strcmp(argv[i], "--render") == 0) {
				if (i + 1 >= argc || argv[i + 1][0] == '-') {
					if (i + 1 >= argc)
						fprintf(stderr, "--render requires an argument\n");
					else {
						fprintf(stderr, "unknown option: %s\n", argv[i + 1]);
						PrintUsage(prog);
					}
					continue;
				}
				fRenderPath = argv[++i];
			} else if (strcmp(argv[i], "--do") == 0) {
				if (i + 1 >= argc || argv[i + 1][0] == '-') {
					if (i + 1 >= argc)
						fprintf(stderr, "--do requires an argument\n");
					else {
						fprintf(stderr, "unknown option: %s\n", argv[i + 1]);
						PrintUsage(prog);
					}
					continue;
				}
				fScript = argv[++i];
			} else if (strcmp(argv[i], "--slide") == 0) {
				if (i + 1 >= argc) {
					fprintf(stderr, "--slide requires an argument\n");
					PrintUsage(prog);
					continue;
				}
				fSlide = atoi(argv[++i]) - 1;
			} else if (strcmp(argv[i], "--new") == 0
					|| strcmp(argv[i], "--new-spreadsheet") == 0) {
				fNewDocument = true;
				fNewSpreadsheet = argv[i][5] == '-';
			} else if (strncmp(argv[i], "--", 2) == 0) {
				fprintf(stderr, "unknown option: %s\n", argv[i]);
				PrintUsage(prog);
			} else {
				fPending.push_back(argv[i]);	// documents to open
			}
		}
		if (fReady)
			OpenPending();
	}

	virtual void RefsReceived(BMessage* message)
	{
		entry_ref ref;
		for (int32 i = 0; message->FindRef("refs", i, &ref) == B_OK; i++) {
			BPath path(&ref);
			if (path.InitCheck() == B_OK)
				fPending.push_back(path.Path());
		}
		if (fReady)
			OpenPending();
	}

	// TApp::AboutRequested: the splash dialog
	virtual void AboutRequested()
	{
		(new TDialogSplash())->Show();
	}

	// quitting: the document windows ask "before quitting?"
	virtual bool QuitRequested()
	{
		for (int32 i = 0; BWindow* w = WindowAt(i); i++)
			if (TDocWindow* window = dynamic_cast<TDocWindow*>(w))
				if (window->Lock()) {
					window->fAppQuitting = true;
					window->Unlock();
				}
		return BApplication::QuitRequested();
	}

	// the document windows open
	int32 CountDocuments()
	{
		int32 count = 0;
		for (int32 i = 0; BWindow* w = WindowAt(i); i++)
			count += dynamic_cast<TDocWindow*>(w) != NULL;
		return count;
	}

	virtual void MessageReceived(BMessage* message)
	{
		switch (message->what) {
			// finding the original (LocateOriginal)
			case 'Galt':
				LocateChoice(message);
				return;
			case 'Gloc':
				LocateChosen(message);
				return;
			case B_CANCEL:
				// the locate panel closed without a choice: the alert again
				if (!fReady && !fLocateChosen) {
					BString error;
					GobeResources::Init(error);
					LocateOriginal(error);
					return;
				}
				break;
			// the Window menu (TDocWindow::MessageReceived; TILE with no
			// "horizontal" is the grid)
			case 'sMF9':
				CascadeWindows();
				return;
			case 'TILE':
				TileWindows(false, false);
				return;
			case 'sMFB':
				TileWindows(true, false);
				return;
			case 'sMFC':
				TileWindows(false, true);
				return;
			case 'sMFE':
				RotateWindows();
				return;
		}
		if (message->what == 's001') {	// File > New…: the dialog, then the kind
			int32 kind;
			if (message->FindInt32("kind", &kind) != B_OK) {
				(new TDialogNewDoc(CountDocuments()))->RunDialog();
				return;
			}
			if (kind != 'WORD' && kind != 'SPRD')
				return;
			Document* document = new Document;
			std::string error;
			if (!(kind == 'SPRD' ? CreateBlankSpreadsheet(*document, error)
					: CreateBlankDocument(*document, error))) {
				delete document;
				return;
			}
			NewSheetPreferences(*document);
			BRect frame(80, 60, 720, 620);
			frame.OffsetBy(fWindowCount * 24, fWindowCount * 24);
			fWindowCount++;
			(new TDocWindow(frame, document->name.c_str(), document))->Show();
			return;
		}
		if (message->what == 'sM08') {	// Help…
			DoHelp();
			return;
		}
		if (message->what == 'sM01') {	// File > Open
			if (fOpenPanel == NULL)
				fOpenPanel = new BFilePanel(B_OPEN_PANEL);
			fOpenPanel->Show();
			return;
		}
		BApplication::MessageReceived(message);
	}

	void OpenPending()
	{
		for (const std::string& path : fPending)
			OpenDocument(path);
		fPending.clear();
	}

	// Test hook: draw the page view offscreen and save it as PNG.
	void Render(Document& doc, const std::string& out)
	{
		TPageView* view = new TPageView(doc);
		BSize size = view->PreferredSize();
		BRect bounds(0, 0, ceilf(size.width), ceilf(size.height));
		BBitmap* bitmap = new BBitmap(bounds, B_RGBA32, true);
		view->ResizeTo(bounds.Width(), bounds.Height());
		bitmap->Lock();
		bitmap->AddChild(view);
		if (!fScript.empty()) {
			view->RunScript(fScript, doc);
			BSize after = view->PreferredSize();
			if (after.width > size.width + 1 || after.height > size.height + 1)
				printf("render: the page grew to %gx%g\n", after.width, after.height);
		}
		view->Draw(bounds);
		view->Sync();
		bitmap->RemoveChild(view);
		bitmap->Unlock();
		delete view;

		BBitmapStream stream(bitmap);
		BFile file(out.c_str(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
		BTranslatorRoster* roster = BTranslatorRoster::Default();
		status_t status = roster->Translate(&stream, NULL, NULL, &file, B_PNG_FORMAT);
		printf("render %s: %s\n", out.c_str(), strerror(status));
		stream.DetachBitmap(&bitmap);
		delete bitmap;
	}

	// The original wasn't found: an alert says so (Quit or Locate…), then
	// the user shows where it is in a file panel ('Galt', 'Gloc'). Both
	// asynchronous: the application thread keeps running (Cosmoe's Wayland
	// backend needs it).
	void LocateOriginal(const BString& error)
	{
		// (titled with the app's name: on Linux it's the window title the
		// desktop and the release smoke tests see)
		BAlert* alert = new BAlert("GobeProductive", error.String(), "Quit", "Locate…",
			NULL, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
		alert->Go(new BInvoker(new BMessage('Galt'), this));
	}

	void LocateChoice(BMessage* message)
	{
		int32 which = message->GetInt32("which", 0);
		if (which != 1) {
			PostMessage(B_QUIT_REQUESTED);
			return;
		}
		// (a file or a folder: the program, the installer, the CD image or
		// a folder from the CD)
		if (fLocatePanel == NULL) {
			fLocatePanel = new BFilePanel(B_OPEN_PANEL, new BMessenger(this), NULL,
				B_FILE_NODE | B_DIRECTORY_NODE, false, new BMessage('Gloc'), NULL, true, true);
			fLocatePanel->Window()->SetTitle(
				"Where is Gobe Productive (CD image, installer or folder)?");
		}
		fLocateChosen = false;
		fLocatePanel->Show();
	}

	void LocateChosen(BMessage* message)
	{
		entry_ref ref;
		if (message->FindRef("refs", &ref) != B_OK)
			return;
		fLocateChosen = true;
		BPath located(&ref);
		BString error, useError;
		if (GobeResources::Use(located.Path(), useError)) {
			GobeResources::Remember();
			StartUp();
			return;
		}
		// the CD image, the installer or a folder: unpacked
		if (GobeResources::Import(located.Path(), error)) {
			StartUp();
			return;
		}
		error.Prepend("The original Gobe Productive couldn't be found there:\n");
		LocateOriginal(error);
	}

	// TApp::DoHelp, GetHelpFile: the application folder's "Help" (STR# 30,
	// 1) "index.html" in the HTML viewer, else STR# 6, 0
	void DoHelp()
	{
		BPath folder(GobeResources::Path().String());
		folder.GetParent(&folder);
		BString file;
		file << folder.Path() << "/" << GobeResources::IndexString(30, 1) << "/index.html";
		if (!BEntry(file.String()).Exists()) {
			BString message;
			message.SetToFormat(GobeResources::IndexString(6, 0).String(), file.String());
			(new BAlert("Help", message.String(), "OK"))->Go(NULL);
			return;
		}
		BString url = "file://";
		url << file;
		LaunchURL("text/html", url.String());
	}

	// Test hook: --do without --render runs the script in the window
	void RunScriptIn(TDocWindow* window)
	{
		if (fScript.empty())
			return;
		BMessage run(kMsgRunScript);
		run.AddString("script", fScript.c_str());
		window->PostMessage(&run);
	}

	void OpenDocument(const std::string& path)
	{
		try {
			Document* doc = new Document;	// owned by the app for now
			std::string error;
			// TApp::RefsReceived: a Gobe document, else the translator that
			// reads it best (a new untitled document)
			FileKind kind = IdentifyFile(path.c_str());
			bool ok;
			BString message, warning;
			if (kind == kGobeDocument) {
				ok = LoadDocument(path, *doc, error);
				message << "Could not open \"" << path.c_str() << "\":\n" << error.c_str();
			} else if (kind == kUnknownFile) {
				// "Unable to import the file “%s”."
				ok = false;
				message.SetToFormat(GobeResources::IndexString(6, 17).String(),
					BPath(path.c_str()).Leaf());
			} else
				ok = ImportFile(path.c_str(), kind, *doc, message, warning);
			if (!ok) {
				if (fRenderPath.empty())
					(new BAlert("Open", message.String(), "OK"))->Go(NULL);
				else
					fprintf(stderr, "error opening %s: %s\n", path.c_str(), message.String());
				delete doc;
				return;
			}
			if (warning.Length() > 0) {
				if (fRenderPath.empty())
					(new BAlert("Bad RTF Alert", warning.String(), "OK"))->Go(NULL);
				else
					fprintf(stderr, "%s: %s\n", path.c_str(), warning.String());
			}
			fHasDocument = true;
			if (fRenderPath.empty())
				RecentDocs::Add(path.c_str());
			for (const std::string& w : doc->warnings)
				printf("%s: %s\n", path.c_str(), w.c_str());
			if (fSlide >= 0)
				doc->ShowSlide(fSlide);
			if (!fRenderPath.empty()) {
				Render(*doc, fRenderPath);
				PostMessage(B_QUIT_REQUESTED);
				return;
			}
			BPath p(path.c_str());
			BRect frame(80, 60, 720, 620);
			frame.OffsetBy(fWindowCount * 24, fWindowCount * 24);
			fWindowCount++;
			// an imported document is untitled: "<name> (converted)"
			TDocWindow* window = new TDocWindow(frame,
				doc->path.empty() ? doc->name.c_str() : p.Leaf(), doc);
			window->Show();
			RunScriptIn(window);
		} catch (const std::exception& e) {
			fprintf(stderr, "%s: %s\n", path.c_str(), e.what());
		}
	}

	virtual void ReadyToRun()
	{
		BString error;
		if (!GobeResources::Init(error)) {
			LocateOriginal(error);
			return;
		}
		StartUp();
	}

	// the original found: the documents asked for, else File > New…
	void StartUp()
	{
		if (fReady)
			return;
		fReady = true;
		InstallTextConverter();
		if (!fPending.empty()) {
			OpenPending();
			if (!fRenderPath.empty() && !fHasDocument) {
				fprintf(stderr, "--render requires a document\n");
				PostMessage(B_QUIT_REQUESTED);
				return;
			}
			return;
		}
		if (fNewDocument) {
			Document* document = new Document;
			std::string error;
			if (fNewSpreadsheet ? CreateBlankSpreadsheet(*document, error)
					: CreateBlankDocument(*document, error)) {
				NewSheetPreferences(*document);
				if (!fRenderPath.empty()) {
					Render(*document, fRenderPath);
					PostMessage(B_QUIT_REQUESTED);
					return;
				}
				BRect frame(80, 60, 720, 520);
				frame.OffsetBy(fWindowCount * 24, fWindowCount * 24);
				fWindowCount++;
				TDocWindow* window = new TDocWindow(frame, document->name.c_str(), document);
				window->Show();
				RunScriptIn(window);
			} else
				fprintf(stderr, "CreateBlankDocument: %s\n", error.c_str());
			return;
		}
		// TApp::ReadyToRun: no documents: File > New…, or the open panel
		// with Shift held
		if (fOpenDialog < 0 && fOpenMenu < 0) {
			if (modifiers() & B_SHIFT_KEY)
				PostMessage('sM01');
			else
				PostMessage('s001');
			return;
		}
		// test hooks (--dialog, --menu): an empty window to show them in
		TDocWindow* window = new TDocWindow(BRect(80, 60, 720, 520), "Untitled 1");
		window->Show();

		if (fOpenDialog >= 0) {
			BMessage open('gDLG');
			open.AddInt32("id", fOpenDialog);
			window->PostMessage(&open);
		}
		if (fOpenMenu >= 0 && window->Lock()) {
			TMenuBar* bar = dynamic_cast<TMenuBar*>(window->KeyMenuBar());
			if (bar != NULL)
				bar->OpenMenu(fOpenMenu);
			window->Unlock();
		}
	}

private:
	int32 fOpenMenu;
	int32 fOpenDialog;
	bool fReady;
	bool fHasDocument;
	bool fNewDocument;
	bool fNewSpreadsheet;
	BFilePanel* fOpenPanel;
	int32 fWindowCount;
	int fSlide;					// --slide
	std::string fScript;		// --do
	std::vector<std::string> fPending;
	std::string fRenderPath;
	BFilePanel* fLocatePanel = NULL;
	bool fLocateChosen = false;
};

} // namespace gobe


int
main()
{
	gobe::TApp app;
	app.Run();
	return 0;
}
