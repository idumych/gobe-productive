// TApp: application entry. Loads the original binary's resources, then opens a
// document window. The real TApp (splash, preferences, part factories,
// document management) comes with later milestones.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <Alert.h>
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
#include "TPageView.h"

namespace gobe {

static const char* kSignature = "application/x-vnd.gobe-port.productive";

class TApp : public BApplication {
public:
	TApp()
		:
		BApplication(kSignature),
		fOpenMenu(-1),
		fOpenDialog(-1),
		fReady(false),
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
	virtual void ArgvReceived(int32 argc, char** argv)
	{
		for (int32 i = 1; i < argc; i++) {
			if (strcmp(argv[i], "--menu") == 0 && i + 1 < argc)
				fOpenMenu = atoi(argv[++i]);
			else if (strcmp(argv[i], "--dialog") == 0 && i + 1 < argc)
				fOpenDialog = atoi(argv[++i]);
			else if (strcmp(argv[i], "--render") == 0 && i + 1 < argc)
				fRenderPath = argv[++i];
			else if (strcmp(argv[i], "--do") == 0 && i + 1 < argc)
				fScript = argv[++i];
			else if (strcmp(argv[i], "--slide") == 0 && i + 1 < argc)
				fSlide = atoi(argv[++i]) - 1;
			else
				fPending.push_back(argv[i]);	// documents to open
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

	virtual void MessageReceived(BMessage* message)
	{
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

	void OpenDocument(const std::string& path)
	{
		Document* doc = new Document;	// owned by the app for now
		std::string error;
		if (!LoadDocument(path, *doc, error)) {
			BString text;
			text << "Could not open \"" << path.c_str() << "\":\n" << error.c_str();
			(new BAlert("Open", text.String(), "OK"))->Go(NULL);
			delete doc;
			return;
		}
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
		TDocWindow* window = new TDocWindow(frame, p.Leaf(), doc);
		window->Show();
	}

	virtual void ReadyToRun()
	{
		BString error;
		if (!GobeResources::Init(error)) {
			BAlert* alert = new BAlert("Gobe Productive", error.String(), "Quit");
			alert->Go();
			PostMessage(B_QUIT_REQUESTED);
			return;
		}
		fReady = true;
		if (!fPending.empty()) {
			OpenPending();
			return;
		}
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
	BFilePanel* fOpenPanel;
	int32 fWindowCount;
	int fSlide;					// --slide
	std::string fScript;		// --do
	std::vector<std::string> fPending;
	std::string fRenderPath;
};

} // namespace gobe


int
main()
{
	gobe::TApp app;
	app.Run();
	return 0;
}
