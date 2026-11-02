#include "TDocWindow.h"

#include <stdio.h>
#include <stdlib.h>

#include <algorithm>
#include <cmath>
#include <vector>
#include <string.h>

#include <Alert.h>
#include <Autolock.h>
#include <Application.h>
#include <Messenger.h>
#include <PrintJob.h>
#include <Roster.h>
#include <Button.h>
#include <CheckBox.h>
#include <ColorControl.h>
#include <FilePanel.h>
#include <FindDirectory.h>
#include <Entry.h>
#include <Font.h>
#include <NodeInfo.h>
#include <Path.h>
#include <LayoutBuilder.h>
#include <Beep.h>
#include <MenuBar.h>
#include <MenuItem.h>
#include <SeparatorItem.h>
#include <String.h>
#include <StringView.h>
#include <TextControl.h>
#include <GroupLayout.h>

#include "DocWriter.h"
#include "Document.h"
#include "Names.h"
#include "Editing.h"
#include "Formats.h"
#include "GobeResources.h"
#include "TDialog.h"
#include "TDocumentDialogs.h"
#include "TDialogLayers.h"
#include "TDrawDialogs.h"
#include "TRecentDocs.h"
#include "TDocView.h"
#include "TPageView.h"
#include "TToolBar.h"
#include "TTranslate.h"
#include "TFormatBar.h"
#include "TGRRuler.h"
#include "TEntryBar.h"
#include "TPageBar.h"
#include "TPartBar.h"
#include "TInkMenus.h"
#include "TSpellBar.h"
#include "TSheetBar.h"
#include "TSheetDialogs.h"
#include "TSpellDialogs.h"
#include "TFindDialog.h"
#include "TPreferences.h"
#include "TWPDialogs.h"
#include "TSharedDocument.h"
#include "TLexiconDialogs.h"
#include "TStylesEdit.h"
#include "TPresentation.h"
#include "TTearOffs.h"
#include "InlineParts.h"
#include "Find.h"
#include "TWPRuler.h"
#include "Styles.h"

#include <ScrollView.h>
#include "TMenu.h"

namespace gobe {

// the Clipboard window (TClipboard's), while it's open
static BMessenger sClipboardWindow;

// Message codes for the Font and Paragraph dialogs
static const uint32 kMsgFontOK = 'gFON';
static const uint32 kMsgParagraphOK = 'gPAR';

// ASCII case-folding for a single byte; used when case-insensitive matching.
static inline char
FoldCase(char c)
{
	unsigned char u = (unsigned char)c;
	if (u >= 'A' && u <= 'Z')
		return c + ('a' - 'A');
	return c;
}

static const uint32 kMsgOpenDialog = 'gDLG';
// TDialog's controls: 'sD00' + index (TItemHandler in TDialog's setup)
static const uint32 kDialogCommand = 'sD00';

// Column Width / Row Height / Go To Cell

// Menu command codes
static const uint32 kCmdColWidth = 'sM64';
static const uint32 kCmdRowHeight = 'sM65';
static const uint32 kCmdGoToCell = 'sM77';

// Format menu insert commands
static const uint32 kCmdInsertDate = 'sM53';
static const uint32 kCmdInsertTime = 'sM54';

// Find/Replace menu commands (MENU 7)
static const uint32 kCmdFindReplace = 'sME0';
static const uint32 kCmdFindAgain = 'sME1';
static const uint32 kCmdFindSelection = 'sME2';
static const uint32 kCmdReplaceFindAgain = 'sME3';
static const uint32 kCmdReplaceAll = 'sME4';

// File menu commands (MENU 2)
static const uint32 kCmdNew = 's001';
static const uint32 kCmdSave = 'sM02';
static const uint32 kCmdSaveAs = 'sM03';
static const uint32 kCmdRevert = 'sM04';
static const uint32 kCmdOpen = 'sM01';
static const uint32 kCmdQuit = 'sM09';
static const uint32 kCmdPageSetup = 'sM05';
static const uint32 kCmdPrint = 'sM06';
static const uint32 kCmdMargins = 'sM22';
static const uint32 kCmdInsertHeader = 'sM2B';
static const uint32 kCmdInsertFooter = 'sM2C';

namespace {

// TApp +0x198: the document windows, the front one first
std::vector<TDocWindow*> sWindows;

} // namespace


// #pragma mark - TDocWindow


TDocWindow::TDocWindow(BRect frame, const char* title, Document* document)
	:
	BWindow(frame, title, B_DOCUMENT_WINDOW, B_AUTO_UPDATE_SIZE_LIMITS)
{
	// the document's windows share its history and saved state
	fShared = TSharedDocument::Attach(document, this);
	fSeenGeneration = fShared->generation;
	sWindows.insert(sWindows.begin(), this);
	// the floaters' subsets take the new window (TFloaterMgr 'sE05')
	std::vector<BWindow*> floaters = TearOffWindows();
	for (BWindow* window : {(BWindow*)TDialogHistory::Open(),
			(BWindow*)TDialogStylesEdit::Open(), (BWindow*)TDialogLayers::Open(),
			(BWindow*)TDialogPRNavigate::Open()})
		floaters.push_back(window);
	for (BWindow* floater : floaters) {
		if (floater == NULL)
			continue;
		BMessage add('sE05');
		add.AddPointer("window", this);
		floater->PostMessage(&add);
	}
	// (the part menus follow the document's kind)
	fDocument = document;
	TMenuBar* menuBar = InstallBaseMenus();
	InstallPartMenus();
	// TPartPR::Read: a presentation opens the Presentation window, unless
	// it's open
	if (document != NULL && document->presentationPart >= 0
			&& TDialogPRNavigate::Open() == NULL)
		PostMessage('PR0D');
	// a test aid, not the original's: every DIAL resource, with
	// GOBE_DEBUG_MENUS set
	if (getenv("GOBE_DEBUG_MENUS") != NULL)
		menuBar->AddItem(MakeDialogsMenu());

	fStatus = new BStringView("status", "Gobe Productive port: menus and dialogs "
		"from the original resources");

	BView* desk;
	if (document != NULL) {
		fDocument = document;
		if (TPageView::CanShow(*document)) {
			TPageView* pages = new TPageView(*document, fShared->undo);
			fPages = pages;
			// the SpellBar (TNormalDocWinHelper: hidden until wanted)
			fSpellBar = new TSpellBar(pages);
			fSpellBar->Hide();
			pages->fSpellBar = fSpellBar;
			TDocScrollView* scroll = new TDocScrollView(pages);
			fScaleSlider = scroll->Scale();
			fScroll = scroll;
			pages->fPageBar = scroll->Pages();
			desk = scroll;
			// the ribbons for documents with text or cells: the FormatBar,
			// the PartBar and the active part's ribbon, the text ruler
			// (TWPPart::ActivePartRibbon) or the EntryBar (TPartSS's)
			bool text = false, cells = false;
			for (const auto& part : document->parts.parts) {
				text = text || part.kind == Sig("WORD");
				cells = cells || part.kind == Sig("SPRD");
			}
			// (a drawing document: the FormatBar for its shapes' inks, the
			// PartBar for its alignments and arrows)
			int mainPart = document->MainPart();
			// (a presentation is a drawing too: TPartPR is a TPartGR)
			bool drawing = mainPart >= 0
				&& (document->parts.parts[size_t(mainPart)].kind == Sig("GRFX")
					|| document->parts.parts[size_t(mainPart)].kind == Sig("PRES"));
			if (text || cells || drawing) {
				fFormatBar = new TFormatBar(pages);
				pages->fFormatBar = fFormatBar;
			}
			if (text || cells || drawing) {
				fPartBar = new TPartBar(pages);
				pages->fPartBar = fPartBar;
			}
			if (text) {
				fRuler = new TWPRuler(pages);
				pages->fRuler = fRuler;
			}
			if (cells) {
				fEntryBar = new TEntryBar(pages);
				fEntryBar->Hide();
				pages->fEntryBar = fEntryBar;
			}
			// the ToolBar (Window > Show ToolBar): shown at first in drawings
			fToolBar = new TToolBar(pages);
			pages->fToolBar = fToolBar;
			if (!drawing)
				fToolBar->Hide();
			// the graphics rulers (TNormalDocWinHelper: TGRRuler 13 and 14,
			// hidden at first)
			if (drawing) {
				pages->fGRRulers[0] = new TGRRuler(pages, true);
				pages->fGRRulers[1] = new TGRRuler(pages, false);
				scroll->SetRulers(pages->fGRRulers[0], pages->fGRRulers[1]);
			}
			// the SheetBar, shown for documents with several sheets
			fSheetBar = new TSheetBar(*document);
			if (document->SheetCount() < 2)
				fSheetBar->Hide();
		} else
			desk = new TDocView(*document);
		// (the status line is the port's: which slide a presentation shows,
		// and commands not ported yet)
		BString status;
		if (!document->slides.empty())
			status << "Slide " << document->currentSlide + 1 << " of "
				<< document->slides.size() << " (arrows, space or click to change)";
		fStatus->SetText(status.String());
	} else {
		desk = new BView("desk", B_WILL_DRAW);
		desk->SetViewColor(152, 152, 152);
	}
	desk->SetExplicitMinSize(BSize(400, 300));

	// TDocWindow::TDocWindow: the faces' keys (MENU 102's) send 'sf0C',
	// Shift-O the size's Other… ('sf0K' 'sM0U'); TWPPart::Activate: the
	// alignments on [, \, ] and Shift-\ ('sM5B')
	{
		TMenu faces(102, true);
		static const int32 kFaces[7] = {1, 2, 4, 8, 32, 64, 128};
		for (int32 face : kFaces) {
			BMenuItem* item = faces.FindValue(face);
			if (item == NULL || item->Shortcut() == 0)
				continue;
			BMessage* message = new BMessage('sf0C');
			message->AddInt32(kValueString, face);
			AddShortcut(item->Shortcut(), 0, message);
		}
		BMessage* other = new BMessage('sf0K');
		other->AddInt32(kValueString, 'sM0U');
		AddShortcut('O', B_SHIFT_KEY, other);
		if (fPartBar != NULL) {
			static const char kAlignKeys[4] = {'[', '\\', ']', '\\'};
			for (int32 i = 0; i < 4; i++) {
				BMessage* message = new BMessage('sM5B');
				message->AddInt32(kValueString, i);
				AddShortcut(kAlignKeys[i], i == 3 ? B_SHIFT_KEY : 0, message);
			}
		}
	}

	BLayoutBuilder::Group<> layout(this, B_VERTICAL, 0);
	layout.Add(menuBar);
	// the ToolBar down the left, from the menu bar; the other bars and the
	// document beside it
	{
		BLayoutBuilder::Group<>::GroupBuilder middle = layout.AddGroup(B_HORIZONTAL, 0);
		if (fToolBar != NULL)
			middle.Add(fToolBar);
		auto bars = middle.AddGroup(B_VERTICAL, 0);
		if (fFormatBar != NULL)
			bars.Add(fFormatBar);
		if (fPartBar != NULL)
			bars.Add(fPartBar);
		if (fSpellBar != NULL)
			bars.Add(fSpellBar);
		if (fRuler != NULL)
			bars.Add(fRuler);
		if (fEntryBar != NULL)
			bars.Add(fEntryBar);
		if (fSheetBar != NULL)
			bars.Add(fSheetBar);
		bars.Add(desk);
		bars.End();
		middle.End();
	}
	layout
		.AddGroup(B_HORIZONTAL)
			.SetInsets(B_USE_SMALL_INSETS, 2, B_USE_SMALL_INSETS, 2)
			.Add(fStatus)
			.AddGlue()
		.End();
}


// TFontMenu (TFontNameMenuElement: TFontMenu(true)): the installed font
// families by name; a family with several styles has a submenu of them.
// Items send 'sf01' with "font", "style" (the family's only style, or the
// style chosen) and "cacheIndex" (TFontMenu::loadFonts).
TFontMenu::TFontMenu()
	:
	BMenu(GobeResources::IndexString(5, 14).String(), B_ITEMS_IN_COLUMN)
{
	Rebuild();
}


void
TFontMenu::Rebuild()
{
	while (CountItems() > 0)
		delete RemoveItem((int32)0);
	fCurrentFamily = NULL;
	int32 count = count_font_families();
	for (int32 i = 0; i < count; i++) {
		font_family family;
		uint32 familyFlags;
		if (get_font_family(i, &family, &familyFlags) != B_OK)
			continue;
		BString label(family);
		// sorted by name
		int32 at = 0;
		while (at < CountItems() && strcmp(label.String(), ItemAt(at)->Label()) >= 0)
			at++;
		BMessage* message = new BMessage(kMsgFontFamily);
		message->AddString("font", family);
		int32 styles = count_font_styles(family);
		BMenuItem* item;
		if (styles == 1) {
			font_style style;
			uint32 flags;
			get_font_style(family, 0, &style, &flags);
			label << " " << style;
			message->AddString("style", style);
			message->AddInt32("cacheIndex", i);
			item = new BMenuItem(label.String(), message);
		} else {
			BMenu* submenu = new BMenu(family);
			for (int32 k = 0; k < styles; k++) {
				font_style style;
				uint32 flags;
				if (get_font_style(family, k, &style, &flags) != B_OK)
					continue;
				BMessage* choice = new BMessage(*message);
				choice->AddString("style", style);
				submenu->AddItem(new BMenuItem(style, choice));
			}
			message->AddInt32("cacheIndex", i);
			item = new BMenuItem(submenu, message);
		}
		AddItem(item, at);
	}
}


BMenu*
TDocWindow::MakeFontMenu()
{
	fFontMenu = new TFontMenu();
	return fFontMenu;
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
	// TPartMenu(1, 's00T'): one item per part factory a frame can be made
	// of (the port's: text, spreadsheets, drawings, tables, pictures),
	// sending 's00T' with its Kind (TDocWindow::DoInsertFrame)
	BMenu* parts = new BMenu("Parts");
	static const struct {
		const char* name;
		uint32 kind;
	} kParts[] = {
		{"Word Processing", 'WORD'}, {"Spreadsheet", 'SPRD'}, {"Graphics", 'GRFX'},
		{"Table", 'TBLE'}, {"Image Processing", 'BITS'}
	};
	for (const auto& p : kParts) {
		BMessage* message = new BMessage('s00T');
		message->AddInt32("Kind", int32(p.kind));
		parts->AddItem(new BMenuItem(p.name, message));
	}
	frameMenu->AddItem(parts, 0);
	frameMenu->AddItem(new BSeparatorItem(), 1);
	bar->AddItem(frameMenu);

	bar->AddItem(new TMenu(6));

	fBar = bar;
	// the active part's menus follow (InstallPartMenus)
	return bar;
}


// TPart::InstallMenus for the active part, after the base menus: the
// text part's Format menu (MENU 51; TWPPart), or with cells selected the
// Spreadsheet's Format and Options (MENU 52, 53 of Parts/Spreadsheet;
// TPartSS::InstallMenus). Swapped when the active part changes.
void
TDocWindow::InstallPartMenus()
{
	if (fBar == NULL)
		return;
	bool sheet = fPages != NULL && fPages->fSelectedCell.active;
	// the drawing part's Arrange (MENU 50): shapes or a frame in a drawing
	// selected, or a drawing document's drawing active
	bool drawing = false;
	if (fPages != NULL && !sheet && fDocument != NULL) {
		int main = fDocument->MainPart();
		drawing = fPages->ShapesSelected() || fPages->SelectedFrame() != NULL || (main >= 0
			&& fDocument->parts.parts[size_t(main)].kind == Sig("GRFX"));
	}
	if (fDocument != NULL && fDocument->presentationPart >= 0 && !sheet)
		drawing = true;
	// TTablePart::InstallMenus: Table (MENU 100 of the Table part), the
	// caret in a table's cells
	bool table = !sheet && fPages != NULL && fDocument != NULL && fPages->fCaret.active
		&& fDocument->TableOfText(fPages->fCaret.part) >= 0;
	if (!fPartMenus.empty() && sheet == fSheetMenus && drawing == fDrawingMenus
			&& table == fTableMenus)
		return;
	fDrawingMenus = drawing;
	fTableMenus = table;
	for (BMenu* menu : fPartMenus) {
		fBar->RemoveItem(menu);
		delete menu;
	}
	fPartMenus.clear();
	fSheetMenus = sheet;
	if (sheet) {
		const ResourceFile& resources = GobeResources::Part("Spreadsheet");
		fPartMenus.push_back(new TMenu(52, resources));
		fPartMenus.push_back(new TMenu(53, resources));
	} else if (fDocument != NULL && fDocument->presentationPart >= 0) {
		// TPartPR::InstallMenus: the drawing's, then Presentation (MENU
		// 11001 of the Presentation part)
		fPartMenus.push_back(new TMenu(50));
		fPartMenus.push_back(new TMenu(11001, GobeResources::Part("Presentation")));
	} else if (table) {
		fPartMenus.push_back(new TMenu(100, GobeResources::Part("Table")));
		fPartMenus.push_back(new TMenu(51));
	} else if (drawing)
		fPartMenus.push_back(new TMenu(50));
	else
		fPartMenus.push_back(new TMenu(51));
	// after MENU 2-6
	int32 at = 5;
	for (BMenu* menu : fPartMenus)
		fBar->AddItem(menu, at++);
}


// the part menus are right before a shortcut is looked up
void
TDocWindow::DispatchMessage(BMessage* message, BHandler* handler)
{
	// a window that closes (Quit) doesn't come back from dispatching: it
	// holds the document only to decide (QuitRequested locks it itself)
	if (message->what == B_QUIT_REQUESTED) {
		BWindow::DispatchMessage(message, handler);
		return;
	}
	// one window at a time uses the document (each window has a thread)
	BAutolock locker(fShared->lock);
	// another window changed it: the view laid out anew first
	if (fSeenGeneration != fShared->generation) {
		fSeenGeneration = fShared->generation;
		if (fPages != NULL)
			fPages->DocumentChangedElsewhere();
		if (fSheetBar != NULL)
			fSheetBar->Update();
	}
	uint64 changes = fShared->undo.Changes();
	if (message->what == B_KEY_DOWN)
		InstallPartMenus();
	BWindow::DispatchMessage(message, handler);
	// this one changed it: the others are told
	if (fShared->undo.Changes() != changes || message->what == kMsgDocumentChanged) {
		fShared->Changed(this);
		fSeenGeneration = fShared->generation;
	}
}


BLocker&
TDocWindow::DocumentLock() const
{
	return fShared->lock;
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


// TPartPR::HandleMessage: the Presentation window's commands, each a step
// of the history named by STR# 11001 of the Presentation part (slides and
// layers counted from 0; "layer" -1 is the slide itself)
bool
TDocWindow::PresentationCommand(BMessage* message)
{
	if (fDocument == NULL || fPages == NULL || fDocument->presentationPart < 0)
		return false;
	const ResourceFile& strings = GobeResources::Part("Presentation");
	Document& doc = *fDocument;
	int32 slide = message->GetInt32("slide", doc.currentSlide);
	int32 layer = message->GetInt32("layer", -1);
	const char* name = message->GetString("name", "");
	auto begin = [&](int label) {
		fPages->fUndo.BeginDocument(doc, IndexString(strings, 11001, label).c_str(),
			fPages->CurrentViewState());
	};
	auto done = [&]() {
		doc.SyncSlides();
		doc.ComputeHiddenFrames();
		fPages->GeometryChanged();
		PostMessage(kMsgDocumentChanged);
		RefreshPresentation();
	};
	// TPartPR::HandleMessage's alerts: STR# 11002, 6 (the last layer of a
	// slide) and 7 (a layer in several slides)
	auto lastLayer = [&](int32 k) {
		if (doc.SlideLayers(size_t(k)).size() > 1)
			return false;
		// (SimpleAlert)
		(new BAlert("Alert", IndexString(strings, 11002, 6).c_str(),
			GobeResources::IndexString(2, 0).String()))->Go(NULL);
		return true;
	};
	switch (message->what) {
		case 'PR04':	// New Slide (TPRNewSlideCommand)
		{
			char layerName[256] = "";
			if (message->GetBool("new layer", false)) {
				const GRPart* gr = doc.PresentationGraphics();
				snprintf(layerName, sizeof layerName, IndexString(strings, 11002, 4).c_str(),
					int(gr != NULL ? gr->layers.size() : 0));
			}
			begin(7);
			doc.NewSlide(size_t(std::max<int32>(0, slide)), name, layerName,
				message->GetBool("share background", false));
			done();
			return true;
		}
		case 'PR0A':	// Duplicate Slide or Layer
		{
			std::string error;
			if (layer < 0) {
				char layerName[256] = "";
				if (message->GetBool("new layer", false)) {
					const GRPart* gr = doc.PresentationGraphics();
					snprintf(layerName, sizeof layerName,
						IndexString(strings, 11002, 4).c_str(),
						int(gr != NULL ? gr->layers.size() : 0));
				}
				begin(12);
				doc.DuplicateSlide(size_t(slide), name, message->GetBool("duplicate layers", false),
					layerName, message->GetBool("share background", false), error);
			} else {
				begin(11);
				doc.DuplicateSlideLayer(size_t(slide), size_t(layer), error);
			}
			done();
			return true;
		}
		case 'PR08':	// Delete Slide or Layer
			if (layer < 0) {
				if (doc.slides.size() < 2)
					return true;
				begin(5);
				doc.RemoveSlide(size_t(slide));
				doc.SetCurrentSlide(size_t(std::min<int32>(slide, int32(doc.slides.size()) - 1)));
			} else {
				if (lastLayer(slide))
					return true;
				int part = doc.SlideLayerPart(size_t(slide), size_t(layer));
				if (part < 0)
					return true;
				if (doc.PresentationLayerUses(size_t(part)) > 1) {
					BAlert* alert = new BAlert("Alert", IndexString(strings, 11002, 7).c_str(),
						GobeResources::IndexString(2, 1).String(),
						GobeResources::IndexString(2, 0).String());
					if (alert->Go() != 1)
						return true;
				}
				begin(15);
				doc.DeletePresentationLayer(size_t(part));
				doc.SetCurrentSlide(size_t(slide));
			}
			done();
			return true;
		case 'PR09':	// Remove Layer (from the slide)
			if (layer < 0 || lastLayer(slide))
				return true;
			begin(4);
			doc.SlideRemoveLayer(size_t(slide), size_t(layer));
			doc.SetCurrentSlide(size_t(slide));
			done();
			return true;
		case 'PR05':	// Insert Layer (a layer of another slide shared)
		{
			int32 part = message->GetInt32("index", -1);
			begin(6);
			if (part >= 0 && doc.SlideAddLayer(size_t(slide), size_t(part),
					size_t(message->GetInt32("position", -1))))
				doc.SetCurrentSlide(size_t(slide));
			done();
			return true;
		}
		case 'PR0C':	// Rename Slide
			if (layer >= 0 || name[0] == '\0')
				return true;
			begin(2);
			doc.RenameSlide(size_t(slide), name);
			done();
			return true;
		case 'PR0G':	// New Layer
			if (name[0] == '\0')
				return true;
			begin(8);
			doc.NewSlideLayer(size_t(slide), layer < 0 ? size_t(-1) : size_t(layer), name);
			done();
			return true;
		case 'PR0E':	// a slide or a layer moved (dropped in the list)
		{
			// TPRMoveSlideCommand, TPRMoveLayerCommand: "slide" and
			// "layer" 1 the place it was dropped before
			int32 toSlide = message->GetInt32("slide", 1, -1);
			int32 toLayer = message->GetInt32("layer", 1, -1);
			if (toSlide < 0)
				return true;
			if (layer < 0) {
				if (toSlide == slide)
					return true;
				size_t to = size_t(toSlide > slide ? toSlide - 1 : toSlide);
				begin(9);
				if (doc.MoveSlide(size_t(slide), to))
					doc.SetCurrentSlide(to);
			} else {
				// (a layer stays in its slide)
				if (toSlide != slide || toLayer < 0) {
					beep();
					return true;
				}
				size_t to = size_t(toLayer > layer ? toLayer - 1 : toLayer);
				begin(10);
				if (doc.MoveSlideLayer(size_t(slide), size_t(layer), to))
					doc.ActivateSlideLayer(size_t(slide), int(to));
			}
			done();
			return true;
		}
		case 'PR03':	// Set Transition (TPRSetTransitionCommand)
			begin(3);
			// ("refcon" the transition's type, "transition" its options)
			doc.SetSlideTransition(size_t(slide), uint32(message->GetInt32("refcon", 0)),
				uint32(message->GetInt32("transition", 0)));
			done();
			return true;
		case 'PR06':	// Auto-start
			begin(0);
			doc.SetPresentationFlag(false, !doc.presentation.autoStart);
			done();
			return true;
		case 'PR07':	// Cursor
			begin(1);
			doc.SetPresentationFlag(true, !doc.presentation.cursor);
			done();
			return true;
		case 'sM4H':	// Rename Layer (TPartGR's, the part layer as "index")
		{
			int32 part = message->GetInt32("index", -1);
			if (part < 0 && layer >= 0)
				part = doc.SlideLayerPart(size_t(slide), size_t(layer));
			const GRPart* gr = doc.PresentationGraphics();
			if (gr == NULL || part < 0 || size_t(part) >= gr->layers.size())
				return true;
			if (name[0] == '\0' && !message->HasString("name")) {
				(new TDialogLayerName(this, 'sM4H', part,
					GobeResources::IndexString(1101, 31).String(),
					gr->layers[size_t(part)].name.c_str()))->RunDialog();
				return true;
			}
			fPages->fUndo.BeginDocument(doc, GobeResources::IndexString(1101, 31).String(),
				fPages->CurrentViewState());
			doc.RenamePresentationLayer(size_t(part), name);
			done();
			return true;
		}
		case 'PR02':	// Switch Layer (the slide's layer chosen in the list)
		case 'PR0H':	// a slide chosen
			if (slide >= 0 && slide < int32(doc.slides.size())) {
				// (TPRSwitchLayerCommand: TPartPR::ActivateLayer)
				doc.ActivateSlideLayer(size_t(slide), layer);
				// (not a change of the document: no history step, no
				// "changed" mark)
				fPages->SlideShownChanged();
				RefreshPresentation();
			}
			return true;
	}
	return false;
}


void
TDocWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		// Presentation > Start Presentation…, Start, Start Here (TPartPR
		// 'PR01': TPRWinHelper)
		case 'PR01':
			if (fDocument != NULL && fDocument->presentationPart >= 0)
				StartSlideShow(this, fDocument, message->GetInt32("start_slide", 0));
			return;
		// Presentation > Show Presentation Window… (TPartPR 'PR0D': opened,
		// or brought to the front)
		case 'PR0D':
			if (TDialogPRNavigate* navigate = TDialogPRNavigate::Open())
				navigate->Activate();
			else if (fDocument != NULL && fDocument->presentationPart >= 0)
				(new TDialogPRNavigate)->RunDialog();
			return;
		case 'PR04': case 'PR0A': case 'PR08': case 'PR09': case 'PR05': case 'PR0C':
		case 'PR0G': case 'PR0E': case 'PR03': case 'PR06': case 'PR07': case 'PR02':
		case 'PR0H':
		case 'sM4H':
			if (PresentationCommand(message))
				return;
			break;
		case kMsgDocumentChanged:
			if (!fShared->modified) {
				fShared->modified = true;
				fShared->TitlesChanged();
			}
			// (an undone sheet switch or rename)
			if (fSheetBar != NULL)
				fSheetBar->Update();
			TDialogHistory::Refresh();
			TDialogStylesEdit::Refresh();
			TDialogLayers::Refresh();
			RefreshTearOffs();
			TDialogPRNavigate::Refresh();
			return;
		// another window of the document changed it (laid out anew in
		// DispatchMessage) or its windows' titles changed
		case kMsgSharedDocumentChanged:
			UpdateTitle();
			if (fPages != NULL)
				fPages->Invalidate();
			TDialogHistory::Refresh();
			return;
		case kMsgSharedTitles:
			UpdateTitle();
			return;
		// Window > New Window (TDoc::NewWindow: another window on the
		// document and its sheet, its own view; the titles get ":N")
		case 'sMF0': {
			if (fDocument == NULL)
				return;
			BRect frame = Frame();
			frame.OffsetBy(20, 20);
			TDocWindow* window = new TDocWindow(frame, fDocument->name.c_str(), fDocument);
			fShared->TitlesChanged();
			window->Show();
			return;
		}
		// Frame > Open Frame Window (TDocWindow::OpenFrame: a window of
		// the document on a sheet showing the selected frame, TSheetMgr::
		// NewFrameSheet)
		case 'sM30': {
			const uint32_t* pointer = fPages != NULL ? fPages->SelectedFrame() : NULL;
			if (fDocument == NULL || pointer == NULL || FrameAt(*fDocument, pointer) == NULL) {
				beep();
				return;
			}
			uint32_t shown[3] = {pointer[0], pointer[1], pointer[2]};
			BRect frame = Frame();
			frame.OffsetBy(20, 20);
			TDocWindow* window = new TDocWindow(frame, fDocument->name.c_str(), fDocument);
			if (window->fPages != NULL)
				window->fPages->ShowFrameOnly(shown);
			fShared->TitlesChanged();
			window->Show();
			return;
		}
		// the zoom tool (TDocWindow::ScaleTo)
		case 's003':
			if (fPages != NULL)
				fPages->SetZoom(message->GetFloat("scale", 1));
			// (from the Scale floater: the zoom shows it too)
			if (fScaleSlider != NULL)
				fScaleSlider->SetScale(message->GetFloat("scale", 1), false);
			return;
		// Document > Page View (TDocWindow::SetPageView: the sheet's, saved
		// with it; not a command)
		case 'sM2A':
			if (fPages != NULL && fDocument != NULL && !fDocument->sheets.sheets.empty()) {
				fPages->SetPageView(!fPages->PageView());
				fDocument->sheets.sheets[0].pageView = fPages->PageView();
				fDocument->sheetsChanged = true;
			}
			return;
		// Document > Fit to Window (TPart::FitToWindow: through the zoom
		// tool, TDocWindow::ChangeScale; TWPPart's only in page view)
		case 'sM39':
			if (fPages != NULL && fScaleSlider != NULL && fPages->PageView())
				fScaleSlider->SetScale(fPages->FitZoom(), true);
			return;
		// Window: the arrangements are the application's
		case 'sMF9':
		case 'TILE':
		case 'sMFB':
		case 'sMFC':
		case 'sMFE':
			be_app->PostMessage(message);
			return;
		// Window > Show/Hide Graphics Ruler
		case 'sMF8':
			if (fScroll != NULL)
				fScroll->ShowRulers(!fScroll->RulersShown());
			return;
		// Window > Hide/Show ToolBar
		case 'sMF7':
			if (fToolBar != NULL) {
				if (fToolBar->IsHidden())
					fToolBar->Show();
				else
					fToolBar->Hide();
			}
			return;
		// the ToolBar's tools (TToolBar: the window's current tool)
		case TToolBar::kMsgTool:
			if (fPages != NULL) {
				if (message->HasInt32("shape"))
					fPages->SetToolShape(message->GetInt32("shape", 0));
				fPages->SetTool(uint32(message->GetInt32("tool", 'ARRW')));
			}
			return;
		// Window > Hide/Show FormatBar
		case 'sMF4':
			if (fFormatBar != NULL) {
				if (fFormatBar->IsHidden())
					fFormatBar->Show();
				else
					fFormatBar->Hide();
			}
			return;
		// Window > Hide/Show PartBar: the part bar and the active part's
		// ribbon (the ruler)
		case 'sMFD': {	// Show/Hide Clipboard (TClipboard::ShowHide)
			if (sClipboardWindow.IsValid()) {
				sClipboardWindow.SendMessage(B_QUIT_REQUESTED);
				sClipboardWindow = BMessenger();
				return;
			}
			Document* clipboard = new Document;
			if (!TPageView::MakeClipboardDocument(*clipboard)) {
				delete clipboard;
				beep();
				return;
			}
			BRect frame = Frame().OffsetByCopy(40, 40);
			frame.right = frame.left + 480;
			frame.bottom = frame.top + 320;
			TDocWindow* window = new TDocWindow(frame, clipboard->name.c_str(), clipboard);
			window->Show();
			sClipboardWindow = BMessenger(window);
			return;
		}
		case 'sMF3':	// Show/Hide SheetBar
			if (fSheetBar != NULL) {
				if (fSheetBar->IsHidden())
					fSheetBar->Show();
				else
					fSheetBar->Hide();
			}
			return;
		case kMsgSheetNew: {
			// TSheetMgr::HandleMessage 's00B': the dialog, or with "name" and
			// "kind" TDocSheetNewOrDeleteCommand ("New Sheet"), then the sheet
			// shown
			const char* name = NULL;
			if (fPages == NULL)
				return;
			if (message->FindString("name", &name) != B_OK || name == NULL) {
				std::vector<std::string> names;
				for (size_t i = 0; i < fDocument->SheetCount(); i++)
					names.push_back(fDocument->sheets.sheets[i].name);
				(new TDialogSheetNew(this, names))->RunDialog();
				return;
			}
			uint32 kind = uint32(message->GetInt32("kind", 'SPRD'));
			if (kind != 'SPRD' && kind != 'WORD') {
				beep();
				return;
			}
			fPages->fUndo.BeginDocument(*fDocument, GobeResources::IndexString(3, 16).String(),
				fPages->CurrentViewState());
			std::string error;
			int sheet = fDocument->AddSheet(kind, name, error);
			if (sheet < 0) {
				fprintf(stderr, "New Sheet: %s\n", error.c_str());
				beep();
				return;
			}
			// (TDocWindow::SetSheetPtr, in the same step)
			fPages->ShowSheet(sheet, false);
			if (fSheetBar != NULL) {
				fSheetBar->Show();
				fSheetBar->Update();
			}
			PostMessage(kMsgDocumentChanged);
			ShowActiveRibbon();
			InstallPartMenus();
			return;
		}
		case kMsgSheetDelete: {
			// TSheetMgr::HandleMessage 's00C': the shown sheet, when another
			// is left (TDocSheetNewOrDeleteCommand, "Delete Sheet")
			if (fPages == NULL || fDocument->SheetCount() < 2) {
				beep();
				return;
			}
			int32 index = message->GetInt32("ActiveTab", fDocument->CurrentSheet());
			fPages->fUndo.BeginDocument(*fDocument, GobeResources::IndexString(3, 17).String(),
				fPages->CurrentViewState());
			std::string error;
			if (!fDocument->DeleteSheet(size_t(index), error)) {
				fprintf(stderr, "Delete Sheet: %s\n", error.c_str());
				beep();
				return;
			}
			fPages->SheetShown();
			if (fSheetBar != NULL)
				fSheetBar->Update();
			PostMessage(kMsgDocumentChanged);
			ShowActiveRibbon();
			InstallPartMenus();
			return;
		}
		case kMsgSheetReorder: {
			// TDocSheetReorderCommand ("Reorder Sheet")
			int32 from = message->GetInt32("OldIndex", -1), to = message->GetInt32("NewIndex", -1);
			if (fPages == NULL || from < 0 || to < 0 || from == to
					|| size_t(std::max(from, to)) >= fDocument->SheetCount())
				return;
			fPages->fUndo.BeginDocument(*fDocument, GobeResources::IndexString(3, 20).String(),
				fPages->CurrentViewState());
			fDocument->MoveSheet(size_t(from), size_t(to));
			if (fSheetBar != NULL)
				fSheetBar->Update();
			PostMessage(kMsgDocumentChanged);
			return;
		}
		case kMsgSheetRename: {
			// TSheetMgr::HandleMessage 's00D': the dialog, or with "name"
			// TDocSheetRenameCommand ("Rename Sheet")
			int32 index = message->GetInt32("ActiveTab", fDocument->CurrentSheet());
			const char* name = NULL;
			if (fPages == NULL || index < 0 || size_t(index) >= fDocument->SheetCount())
				return;
			if (message->FindString("name", &name) != B_OK || name == NULL) {
				std::vector<std::string> names;
				for (size_t i = 0; i < fDocument->SheetCount(); i++)
					names.push_back(fDocument->sheets.sheets[i].name);
				(new TDialogSheetRename(this, names, index))->RunDialog();
				return;
			}
			if (fDocument->sheets.sheets[size_t(index)].name == name)
				return;
			fPages->fUndo.BeginDocument(*fDocument, GobeResources::IndexString(3, 18).String(),
				fPages->CurrentViewState());
			fDocument->sheets.sheets[size_t(index)].name = name;
			fDocument->sheetsChanged = true;
			if (fSheetBar != NULL)
				fSheetBar->Update();
			PostMessage(kMsgDocumentChanged);
			return;
		}
		case kMsgSheetSwitch: {
			// TDocWinSheetSwitchCommand (by index, or by "name")
			int32 index = message->GetInt32("ActiveTab", -1);
			const char* name = NULL;
			if (index < 0 && message->FindString("name", &name) == B_OK)
				for (size_t i = 0; i < fDocument->SheetCount(); i++)
					if (fDocument->sheets.sheets[i].name == name)
						index = int32(i);
			if (fPages == NULL || index < 0 || !fPages->ShowSheet(index)) {
				beep();
				return;
			}
			if (fSheetBar != NULL)
				fSheetBar->Update();
			PostMessage(kMsgDocumentChanged);
			ShowActiveRibbon();
			InstallPartMenus();
			return;
		}
		case 'sMF6':
			if (fPartBar != NULL) {
				bool hide = !fPartBar->IsHidden();
				if (hide)
					fPartBar->Hide();
				else
					fPartBar->Show();
				ShowActiveRibbon();
			}
			return;
		// (the page view's: the active part's ribbon to show)
		case kMsgActiveRibbon:
			ShowActiveRibbon();
			InstallPartMenus();
			return;
		case 's00L':
		{
			// the ruler's unit menu, Set Ruler Divisions (TLeafSheet::
			// SetUnitAndRulerDivisions: the sheet's, saved with it; not a
			// command, so the document isn't marked changed)
			if (fDocument == NULL || fDocument->sheets.sheets.empty())
				return;
			LeafSheet& sheet = fDocument->sheets.sheets[0];
			int32 unit = message->GetInt32(kValueString, int32(sheet.unit));
			int32 divisions = message->GetInt32("rulerDivisions", int32(sheet.divisions));
			if (uint32(unit) != sheet.unit || uint32(divisions) != sheet.divisions) {
				sheet.unit = uint32(unit);
				sheet.divisions = uint32(divisions);
				fDocument->sheetsChanged = true;
			}
			if (fRuler != NULL)
				fRuler->Update();
			if (fPages != NULL)
				for (TGRRuler* ruler : fPages->fGRRulers)
					if (ruler != NULL)
						ruler->Update();
			return;
		}
		case kCmdSave:
			if (fDocument == NULL)
				return;
			// an untitled document is saved through the Save panel
			if (fDocument->path.empty())
				PostMessage(kCmdSaveAs);
			else
				Save(fDocument->path);
			return;
		case kCmdSaveAs:
			if (fDocument == NULL)
				return;
			if (fSavePanel == NULL) {
				fSavePanel = new BFilePanel(B_SAVE_PANEL, new BMessenger(this));
				fSavePanel->SetSaveText(fDocument->path.empty() ? fDocument->name.c_str()
					: BPath(fDocument->path.c_str()).Leaf());
				int main = fDocument->MainPart();
				fFormatField = AddFormatField(fSavePanel,
					main >= 0 && fDocument->WordPart(size_t(main)) != NULL,
					main >= 0 && fDocument->SpreadsheetPart(size_t(main)) != NULL);
			}
			fSavePanel->Show();
			return;
		case B_CANCEL:
			// the save panel closed (after a save, its B_SAVE_REQUESTED came
			// first)
			fCloseAfterSave = false;
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

				// the File Format popup: a translator's format writes the
				// main part into a new file and leaves the document as it is
				SaveFormat format = ChosenFormat(fFormatField);
				if (format != kSaveGobe) {
					BString error;
					// (nothing to say after Cancel in the Status window)
					if (!ExportFile(*fDocument, path.Path(), format, error)
							&& error.Length() > 0)
						(new BAlert("Save", error.String(), "OK"))->Go(NULL);
					return;
				}
				if (Save(path.Path())) {
					// a new file: the original's file type
					BNode source(from.c_str()), target(path.Path());
					BNodeInfo sourceInfo(&source), targetInfo(&target);
					char type[B_MIME_TYPE_LENGTH];
					if (sourceInfo.GetType(type) == B_OK)
						targetInfo.SetType(type);
					else
						targetInfo.SetType("application/x-vnd.gobe.productive-document");
					SetTitle(path.Leaf());
					if (fCloseAfterSave) {
						fShared->modified = false;
						PostMessage(B_QUIT_REQUESTED);
					}
				}
			}
			return;
		}
		case kCmdPageSetup:
			PageSetup();
			return;
		case kCmdPrint:
			Print();
			return;
		case kCmdMargins:
		{
			if (fDocument == NULL || fDocument->sheets.sheets.empty())
				return;
			BRect rect;
			if (message->FindRect("Margins", &rect) != B_OK) {
				(new TDialogFormatDocument(this, *fDocument))->RunDialog();
				return;
			}
			// TDocMarginsCommand: a negative margin stays as it is; the page
			// guides flag is set (the others are read but not applied)
			LeafSheet& sheet = fDocument->sheets.sheets[0];
			float current[4];
			fDocument->Margins(current);
			float margins[4] = {rect.left, rect.top, rect.right, rect.bottom};
			for (int i = 0; i < 4; i++)
				if (margins[i] < 0)
					margins[i] = current[i];
			float printable[4] = {sheet.paper[0] + margins[0], sheet.paper[1] + margins[1],
				sheet.paper[2] - margins[2], sheet.paper[3] - margins[3]};
			// TDocMarginsCommand: "Change Margins"
			if (fPages != NULL)
				fPages->fUndo.BeginDocument(*fDocument,
					GobeResources::IndexString(3, 0x15).String(), fPages->CurrentViewState());
			bool guides;
			if (message->FindBool("ShowPageGuides", &guides) == B_OK && guides != sheet.flags[1]) {
				sheet.flags[1] = guides;
				fDocument->sheetsChanged = true;
			}
			float paper[4];
			std::copy(sheet.paper, sheet.paper + 4, paper);
			fDocument->SetPageGeometry(paper, printable);
			GeometryChanged();
			return;
		}
		case kMsgRunScript:
		{
			// test hook: --do in a window
			const char* script;
			if (fPages != NULL && fDocument != NULL
					&& message->FindString("script", &script) == B_OK && script != NULL)
				fPages->RunScript(script, *fDocument);
			return;
		}
		case 'sM1B':
		{
			// File > Insert…: the open panel, then the chosen file
			if (message->HasRef("refs")) {
				if (fPages != NULL)
					fPages->MessageReceived(message);
				return;
			}
			if (fInsertPanel == NULL)
				fInsertPanel = new BFilePanel(B_OPEN_PANEL, new BMessenger(this), NULL,
					B_FILE_NODE, false, new BMessage('sM1B'));
			fInsertPanel->Show();
			return;
		}
		case kCmdInsertHeader:
		case kCmdInsertFooter:
			if (fPages != NULL)
				fPages->ToggleHeaderFooter(message->what == kCmdInsertFooter ? 1 : 0);
			return;
		case kCmdRevert:
		{
			if (fDocument == NULL || !fShared->modified || fDocument->path.empty())
				return;
			// TDoc::RevertAlert: "Revert “name” to last saved version?",
			// Cancel (Esc) or Revert
			BString name = fDocument->name.c_str();
			if (name.IsEmpty())
				name = BPath(fDocument->path.c_str()).Leaf();
			BString text = GobeResources::IndexString(2, 17);
			text << name << GobeResources::IndexString(2, 18);
			BAlert* alert = new BAlert("Revert", text.String(),
				GobeResources::IndexString(2, 1).String(),
				GobeResources::IndexString(2, 16).String(), NULL, B_WIDTH_AS_USUAL,
				B_WARNING_ALERT);
			alert->SetShortcut(0, B_ESCAPE);
			if (alert->Go() != 1)
				return;
			Document fresh;
			std::string error;
			if (!LoadDocument(fDocument->path, fresh, error)) {
				(new BAlert("Revert", error.c_str(), "OK"))->Go(NULL);
				return;
			}
			*fDocument = std::move(fresh);
			if (fPages != NULL) {
				fPages->DocumentReloaded();
				fPages->ClearUndo();
			}
			fShared->modified = false;
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
			be_app->PostMessage(kCmdNew);
			return;
		case kCmdOpen:
			be_app->PostMessage(kCmdOpen);
			return;
		case kCmdQuit:
			be_app->PostMessage(B_QUIT_REQUESTED);
			return;
		case 'sM08':	// Help… (TApp::DoHelp)
		case B_ABOUT_REQUESTED:
			be_app->PostMessage(message->what);
			return;
	}

	// Forward cell format/fill/wrap alignment commands to the page view
	if (fPages != NULL) {
		uint32 what = message->what;
		if (what == 'GcHA' || what == 'GcVA' || what == 'GcNF' || what == 'GcFL'
			|| what == 'GcWR' || what == 'sM52' || what == 'sM56' || what == 'sM57'
			|| what == 'sM58' || what == 'sM59' || what == 'sM55' || what == 'sM51'
			|| what == 'sM5I' || what == 'sM5K' || what == 'sM16' || what == 'sM17'
			|| what == 'sM29' || what == 'sM5J') {
			fPages->MessageReceived(message);
			return;
		}
	}

	if (message->what == kMsgOpenDialog) {
		int32 id = message->GetInt32("id", 0);
		if (id == 7) {
			FindReplaceCommand(message);
			return;
		}
		// most other dialogs use the same id for DIAL and Item
		TDialog* dialog = new TDialog(id, id, kDialogCommand, this);
		dialog->Show();
		return;
	}

	// Document > Find/Change (MENU 7), the Find dialog's 'sME5' (Replace)
	// and 'sE02' (the document view to the fore: focused)
	if (message->what >= kCmdFindReplace && message->what <= 'sME5') {
		FindReplaceCommand(message);
		return;
	}
	if (message->what == 'sE02') {
		if (fPages != NULL)
			fPages->MakeFocus(true);
		return;
	}
	// Column Width, Row Height, Go To Cell
	// (Column Width… and Row Height…: the spreadsheet's, TDialogRowColSize)
	if (message->what == kCmdColWidth || message->what == kCmdRowHeight) {
		if (fPages != NULL)
			fPages->MessageReceived(message);
		return;
	}
	if (message->what == kCmdGoToCell) {
		if (fPages != NULL)
			fPages->MessageReceived(message);
		return;
	}
	// Paragraph… (sM50): the text part's
	if (message->what == 'sM50') {
		if (fPages != NULL)
			fPages->MessageReceived(message);
		return;
	}
	// Insert Date…, Insert Time… (sM53, sM54): the text part's
	if (message->what == kCmdInsertDate || message->what == kCmdInsertTime) {
		if (fPages != NULL)
			fPages->MessageReceived(message);
		return;
	}

	// Document > Spell Check (TSpellBarHandler)
	if (message->what >= 'sMC0' && message->what <= 'sMC5') {
		if (fSpellBar != NULL)
			fSpellBar->HandleCommand(message);
		return;
	}
	// a text part's context menu's Open URL (TDocWindow 'sE03'): mail to
	// the e-mail program, the rest to the browser
	if (message->what == 'sE03') {
		const char* url;
		if (message->FindString("URL", &url) == B_OK) {
			LaunchURL(strncmp(url, "mailto:", 7) == 0 ? "text/x-email" : "text/html", url);
		}
		return;
	}
	// Writing Tools > Thesaurus… and Dictionary… (TDocWindow: the active
	// part's SelectWord, then TThesaurus::GUI, TDictionary::GUI): the word
	// at the caret selected; the data file is the first of STR# 38 (37)
	// there ("~etc": the system's etc folder); none: STR# 6's alert with
	// the last. The port also looks in its settings folder, where
	// tools/wordnet_to_gobe.py's files from WordNet go.
	if (message->what == 'sM27' || message->what == 'sM28') {
		BString word;
		if (fPages != NULL && fPages->fCaret.active && fDocument != NULL) {
			if (const WPPart* wp = fDocument->WordPart(fPages->fCaret.part)) {
				TextPosition from, to;
				if (!fPages->TextSelection(from, to))
					from = {fPages->fCaret.paragraph, fPages->fCaret.offset};
				size_t start, end;
				const std::string& text = wp->paragraphs[from.paragraph].text;
				WordBounds(text, from.offset, true, start, end);
				if (start != end) {
					fPages->SelectText(fPages->fCaret.part, {from.paragraph, start}, end);
					word.SetTo(text.c_str() + start, int32(end - start));
				}
			}
		}
		int32 list = message->what == 'sM27' ? 38 : 37;
		BString path;
		bool found = false;
		for (int32 i = 0; !found; i++) {
			BString candidate = GobeResources::IndexString(list, i);
			if (candidate.IsEmpty())
				break;
			if (candidate.Compare("~etc", 4) == 0) {
				BPath etc;
				if (find_directory(B_SYSTEM_ETC_DIRECTORY, &etc) == B_OK) {
					etc.Append(candidate.String() + 5);
					candidate = etc.Path();
				}
			}
			path = candidate;
			found = BEntry(path.String()).Exists();
		}
		if (!found) {
			// the port's: made from WordNet (tools/wordnet_to_gobe.py) in
			// its settings folder
			BPath settings;
			if (find_directory(B_USER_SETTINGS_DIRECTORY, &settings) == B_OK) {
				settings.Append("Gobe Productive port");
				settings.Append(message->what == 'sM27' ? "thesaurus" : "dictionary");
				if (BEntry(settings.Path()).Exists()) {
					path = settings.Path();
					found = true;
				}
			}
		}
		if (!found) {
			BString text;
			text.SetToFormat(GobeResources::IndexString(6, 0).String(), path.String());
			(new BAlert("", text.String(), "OK"))->Go(NULL);
		} else if (message->what == 'sM27')
			TDialogThesaurus::Show(path, word);
		else
			TDialogDictionary::Show(path, word);
		return;
	}
	// Document > Document Size… (the main part's HandleMessage: for text,
	// TWPPart: the dialog, then TWPSetDocSizeCmd, STR# 3, 12)
	// (drawings: TPartGR, TDialogGRDocSize, TGRSetDocSizeCommand;
	// spreadsheets: TPartSS, TDialogSSDocSize, TSSSetDocSizeCommand;
	// presentations: TPartPR, TDialogPRDocSize, TPRSetDocSizeCommand,
	// STR# 11001, 13 of Parts/Presentation)
	if (message->what == 'sM21' && fPages != NULL && fDocument != NULL
			&& fDocument->presentationPart >= 0) {
		size_t part = size_t(fDocument->presentationPart);
		const GRPart* gr = fDocument->GraphicsPart(part);
		if (gr == NULL)
			return;
		int32 width, height;
		if (message->FindInt32("Width", &width) != B_OK
				|| message->FindInt32("Height", &height) != B_OK) {
			(new TDialogPRDocSize(this, int32(roundf(gr->size[0])),
				int32(roundf(gr->size[1]))))->RunDialog();
			return;
		}
		if (float(width) == gr->size[0] && float(height) == gr->size[1])
			return;
		fPages->fUndo.BeginDocument(*fDocument, IndexString(GobeResources::Part("Presentation"),
			11001, 13).c_str(), fPages->CurrentViewState());
		GRPart* edit = fDocument->EditableGraphicsPart(part);
		edit->size[0] = float(width);
		edit->size[1] = float(height);
		fPages->GeometryChanged();
		PostMessage(kMsgDocumentChanged);
		return;
	}
	if (message->what == 'sM21' && fPages != NULL && fDocument != NULL
			&& fDocument->MainPart() >= 0
			&& fDocument->GraphicsPart(size_t(fDocument->MainPart())) != NULL) {
		size_t main = size_t(fDocument->MainPart());
		float across, down;
		if (message->FindFloat("Width", &across) != B_OK
				|| message->FindFloat("Height", &down) != B_OK) {
			uint32_t pages[2];
			if (fDocument->DrawingPages(main, pages[0], pages[1]))
				(new TDialogGRDocSize(this, float(pages[0]), float(pages[1])))->RunDialog();
			return;
		}
		fPages->fUndo.BeginDocument(*fDocument, GobeResources::IndexString(3, 12).String(),
			fPages->CurrentViewState());
		if (fDocument->SetDrawingPages(main, uint32_t(across), uint32_t(down))) {
			fPages->GeometryChanged();
			PostMessage(kMsgDocumentChanged);
		}
		return;
	}
	if (message->what == 'sM21' && fPages != NULL && fDocument != NULL
			&& fDocument->MainPart() >= 0
			&& fDocument->SpreadsheetPart(size_t(fDocument->MainPart())) != NULL) {
		size_t main = size_t(fDocument->MainPart());
		const SSPart* ss = fDocument->SpreadsheetPart(main);
		int32 rows, columns;
		if (message->FindInt32("Rows", &rows) != B_OK
				|| message->FindInt32("Cols", &columns) != B_OK) {
			// at least the cells in use (TPartSS::PrintRange)
			int32 minRows = 1, minColumns = 1;
			for (const SSCell& cell : ss->cells) {
				minRows = std::max(minRows, int32(cell.id.a) + 1);
				minColumns = std::max(minColumns, int32(cell.id.b) + 1);
			}
			(new TDialogSSDocSize(this, ss->used.to.a - ss->used.from.a + 1,
				ss->used.to.b - ss->used.from.b + 1, minRows, minColumns))->RunDialog();
			return;
		}
		fPages->fUndo.BeginDocument(*fDocument, GobeResources::IndexString(3, 12).String(),
			fPages->CurrentViewState());
		if (fDocument->SetSheetSize(main, rows, columns)) {
			// a selection past the new end goes back to A1
			auto& sel = fPages->fSelectedCell;
			if (sel.active && sel.part == main
					&& (std::max(sel.row, sel.anchorRow) >= rows
						|| std::max(sel.column, sel.anchorColumn) >= columns)) {
				sel.row = sel.column = sel.anchorRow = sel.anchorColumn = 0;
				sel.kind = SelectionKind::cells;
				sel.extra.clear();
			}
			fPages->GeometryChanged();
			PostMessage(kMsgDocumentChanged);
		}
		return;
	}
	if (message->what == 'sM21') {
		if (fPages == NULL || fDocument == NULL || fDocument->PagePart() < 0
				|| fDocument->MainPart() < 0
				|| fDocument->WordPart(size_t(fDocument->MainPart())) == NULL)
			return;
		size_t main = size_t(fDocument->MainPart());
		int32 across;
		if (message->FindInt32("PagesAcross", &across) != B_OK) {
			(new TDialogWPDocSize(this, int32(fDocument->PagesAcrossCount()),
				fDocument->TitlePage(main)))->RunDialog();
			return;
		}
		bool title = message->GetBool("TitlePage", false);
		if (uint32(across) == fDocument->PagesAcrossCount() && title == fDocument->TitlePage(main))
			return;
		fPages->fUndo.BeginDocument(*fDocument, GobeResources::IndexString(3, 12).String(),
			fPages->CurrentViewState());
		if (fDocument->SetDocumentSize(main, uint32(across), title)) {
			fPages->GeometryChanged();
			PostMessage(kMsgDocumentChanged);
		}
		return;
	}
	// Document > History… (a floater) and its Options (THistory::
	// HandleMessage): Set Undo Levels (with "value" the history's size, the
	// document changed; else the dialog), Clear History (the document
	// changed), Show Undo Steps
	// Format > Edit Styles… (TStyleManager::HandleMessage: the floater,
	// with "Name" or the active style first; if it's open, "Name" shown)
	if (message->what == 'sM23') {
		const char* name = NULL;
		bool named = message->FindString("Name", &name) == B_OK;
		if (TDialogStylesEdit* edit = TDialogStylesEdit::Open()) {
			if (named) {
				BMessage show('NAME');
				show.AddString("Name", name);
				edit->PostMessage(&show);
			}
			return;
		}
		std::string active;
		if (!named) {
			StylesSnapshot snapshot;
			GetStylesSnapshot(snapshot);
			active = snapshot.selection.active;
			name = active.c_str();
		}
		(new TDialogStylesEdit(name))->RunDialog();
		return;
	}
	// 'sD=L' (a floater changed): the format bar again
	if (message->what == 'sD=L') {
		if (fFormatBar != NULL)
			fFormatBar->Update();
		return;
	}
	// Document > Views & Layers… (TDocWindow: the floater, DIAL 203) and
	// its commands (TPartGR::HandleMessage, STR# 1101 25-33): New Layer,
	// Delete Layer, Rename Layer (without "name", the dialog), Switch
	// Layer, Set Layer Attributes, New View, Delete View, Rename View,
	// Switch View
	if (message->what == 'sM25') {
		if (TDialogLayers* layers = TDialogLayers::Open())
			layers->Activate(true);
		else
			(new TDialogLayers())->RunDialog();
		return;
	}
	if ((message->what >= 'sM4F' && message->what <= 'sM4N') && fPages != NULL
			&& fDocument != NULL && fDocument->MainPart() >= 0
			&& fDocument->parts.parts[size_t(fDocument->MainPart())].kind == Sig("GRFX")
			&& fDocument->GraphicsPart(size_t(fDocument->MainPart())) != NULL) {
		size_t part = size_t(fDocument->MainPart());
		const GRPart* gr = fDocument->GraphicsPart(part);
		int32 index = message->GetInt32("index", -1);
		const char* name = NULL;
		bool named = message->FindString("name", &name) == B_OK && name != NULL;
		int32 label = 0;
		switch (message->what) {
			case 'sM4F': label = 29; break;
			case 'sM4G': label = 30; break;
			case 'sM4H': label = 31; break;
			case 'sM4I': label = 32; break;
			case 'sM4J': label = 33; break;
			case 'sM4K': label = 25; break;
			case 'sM4L': label = 26; break;
			case 'sM4M': label = 27; break;
			case 'sM4N': label = 28; break;
		}
		if (message->what == 'sM4H' && !named) {
			if (index >= 0 && size_t(index) < gr->layers.size())
				(new TDialogLayerName(this, 'sM4H', index,
					GobeResources::IndexString(1101, 31).String(),
					gr->layers[size_t(index)].name.c_str()))->RunDialog();
			return;
		}
		// the commands' own checks first, so nothing changed adds no step
		Document trial = *fDocument;
		std::string error;
		auto apply = [&](Document& doc) {
			switch (message->what) {
				case 'sM4F': return doc.NewLayer(part);
				case 'sM4G': return index >= 0 && doc.DeleteLayer(part, size_t(index), error);
				case 'sM4H': return index >= 0 && doc.RenameLayer(part, size_t(index), name);
				case 'sM4I': return index >= 0 && doc.SetActiveLayer(part, size_t(index));
				case 'sM4J': return index >= 0 && doc.SetLayerFlag(part, size_t(index),
					int(message->GetInt32("attr", -1)), message->GetInt32("value", 0) != 0);
				case 'sM4K': return doc.NewView(part);
				case 'sM4L': return index >= 0 && doc.DeleteView(part, uint32_t(index));
				case 'sM4M': return index >= 0 && named
					&& doc.RenameView(part, uint32_t(index), name);
				case 'sM4N': return index >= 0 && doc.ShowView(part, uint32_t(index));
			}
			return false;
		};
		if (!apply(trial)) {
			if (!error.empty())
				(new BAlert("", error.c_str(), "OK"))->Go(NULL);
			return;
		}
		fPages->fUndo.BeginDocument(*fDocument, GobeResources::IndexString(1101, label).String(),
			fPages->CurrentViewState());
		apply(*fDocument);
		fPages->ValidateShapeSelection(message->what == 'sM4F' || message->what == 'sM4G');
		fDocument->ComputeHiddenFrames();
		fPages->GeometryChanged();
		fPages->Invalidate();
		PostMessage(kMsgDocumentChanged);
		TDialogLayers::Refresh();
		RefreshTearOffs();
		TDialogPRNavigate::Refresh();
		return;
	}
	if (message->what == 'sM24') {
		if (TDialogHistory* history = TDialogHistory::Open())
			history->Activate(true);
		else
			(new TDialogHistory())->RunDialog();
		return;
	}
	if (message->what == 'sM2D' || message->what == 'sM2E' || message->what == 'sM2F') {
		if (fPages == NULL)
			return;
		int32 levels;
		if (message->what == 'sM2D') {
			if (message->FindInt32(kValueString, &levels) != B_OK) {
				size_t limit = fPages->fUndo.Limit();
				(new TDialogHistorySize(this, limit == 0 ? 20 : int32(limit)))->RunDialog();
				return;
			}
			fPages->fUndo.SetLimit(size_t(std::max<int32>(1, levels)));
			fShared->modified = true;
			UpdateTitle();
		} else if (message->what == 'sM2E') {
			fPages->fUndo.Clear();
			fShared->modified = true;
			UpdateTitle();
		} else
			fPages->fShowUndoSteps = !fPages->fShowUndoSteps;
		TDialogHistory::Refresh();
		return;
	}
	// File > Preferences… (TDocWindow: TDialogPrefs)
	if (message->what == 'sM07') {
		(new TDialogPrefs())->RunDialog();
		return;
	}
	if (message->what == 'sMC6') {
		(new TDialogSpellDictionaries())->RunDialog();
		return;
	}

	// the commands of the parts' menus (text, cells): the page view's
	// (menus send them to the window)
	if (fPages != NULL && TPageView::HandlesCommand(message->what)) {
		fPages->MessageReceived(message);
		return;
	}
	// the system's own messages ('_UKU', ...) are the window's business
	if ((message->what >> 24) == '_') {
		BWindow::MessageReceived(message);
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


// TApp::PrinterName: the default printer (the original read
// ~/config/settings/printer_data; Haiku's print server tells it)
static BString
PrinterName()
{
	BMessenger server("application/x-vnd.Be-PSRV");
	BMessage request(B_GET_PROPERTY), reply;
	request.AddSpecifier("ActivePrinter");
	BString name;
	if (server.IsValid() && server.SendMessage(&request, &reply) == B_OK)
		reply.FindString("result", &name);
	return name;
}


static BMessage*
PrintSettings(const LeafSheet& sheet)
{
	if (sheet.printSettings.empty())
		return NULL;
	BMessage* settings = new BMessage;
	if (settings->Unflatten((const char*)sheet.printSettings.data()) != B_OK) {
		delete settings;
		return NULL;
	}
	return settings;
}


bool
TDocWindow::PageSetup()
{
	if (fDocument == NULL || fDocument->sheets.sheets.empty())
		return false;
	LeafSheet& sheet = fDocument->sheets.sheets[0];
	float margins[4];
	fDocument->Margins(margins);
	BPrintJob job(sheet.name.c_str());
	if (BMessage* settings = PrintSettings(sheet))
		job.SetSettings(settings);
	if (job.ConfigPage() != B_OK)
		return false;
	// the new settings, the printer's name; the paper from the job, the
	// margins as they were
	BMessage* settings = job.Settings();
	if (settings != NULL) {
		BString printer = PrinterName();
		if (settings->HasString("_printerName"))
			settings->ReplaceString("_printerName", printer);
		else
			settings->AddString("_printerName", printer);
		ssize_t size = settings->FlattenedSize();
		sheet.printSettings.resize(size_t(size));
		settings->Flatten((char*)sheet.printSettings.data(), size);
		fDocument->sheetsChanged = true;
		delete settings;
	}
	BRect rect = job.PaperRect();
	float paper[4] = {rect.left, rect.top, rect.right, rect.bottom};
	float printable[4] = {rect.left + margins[0], rect.top + margins[1],
		rect.right - margins[2], rect.bottom - margins[3]};
	fDocument->SetPageGeometry(paper, printable);
	GeometryChanged();
	return true;
}


void
TDocWindow::Print()
{
	if (fDocument == NULL || fDocument->sheets.sheets.empty() || fPages == NULL)
		return;
	// Page Setup first when the settings are for another printer
	LeafSheet* sheet = &fDocument->sheets.sheets[0];
	bool setup = true;
	if (BMessage* settings = PrintSettings(*sheet)) {
		const char* printer = NULL;
		if (settings->FindString("_printerName", &printer) == B_OK && printer != NULL
				&& PrinterName() == printer)
			setup = false;
		delete settings;
	}
	if (setup && !PageSetup())
		return;
	sheet = &fDocument->sheets.sheets[0];
	BMessage* settings = PrintSettings(*sheet);
	if (settings != NULL)
		settings->RemoveName("save");
	BPrintJob job(fDocument->path.empty() ? fDocument->name.c_str()
		: BPath(fDocument->path.c_str()).Leaf());
	if (settings != NULL)
		job.SetSettings(settings);
	if (job.ConfigJob() != B_OK || !job.CanContinue())
		return;
	// TPart::Print: the pages from FirstPage to LastPage (1 to the page
	// count), each drawn where the printer can print
	int32 pages = int32(fDocument->PageCount());
	int32 first = std::max<int32>(1, job.FirstPage());
	int32 last = std::min<int32>(pages, job.LastPage());
	if (last < first)
		return;
	BRect printable = job.PrintableRect();
	job.BeginJob();
	for (int32 page = first; page <= last; page++) {
		BRect area = fPages->PageArea(page - 1, printable);
		job.DrawView(fPages, area, BPoint(0, 0));
		job.SpoolPage();
		if (!job.CanContinue())
			break;
	}
	if (job.CanContinue())
		job.CommitJob();
}


void
TDocWindow::GeometryChanged()
{
	if (fPages != NULL)
		fPages->GeometryChanged();
	fShared->modified = true;
	UpdateTitle();
}


// Writes the document (only edited parts are written anew). The first save
// over an existing file in a session keeps the previous version as
// "<name>~".
bool
TDocWindow::Save(const std::string& path)
{
	if (fDocument == NULL)
		return false;
	if (path == fDocument->path && !fShared->backedUp) {
		BEntry backup((path + "~").c_str());
		if (!backup.Exists()) {
			std::string error;
			if (!WriteFile(path + "~", fDocument->bytes, error)) {
				(new BAlert("Save", error.c_str(), "OK"))->Go(NULL);
				return false;
			}
		}
		fShared->backedUp = true;
	}
	std::string error;
	if (!fDocument->Save(path, error)) {
		BString text;
		text << "Could not save \"" << path.c_str() << "\":\n" << error.c_str();
		(new BAlert("Save", text.String(), "OK"))->Go(NULL);
		return false;
	}
	RecentDocs::Add(path.c_str());
	if (fPages != NULL) {
		fPages->DocumentSaved();
		fPages->DocumentReloaded();
	}
	fShared->modified = false;
	fShared->Changed(this);
	// (this window has the document as saved: not laid out again as if
	// another window had changed it, which marked it changed again)
	fSeenGeneration = fShared->generation;
	fShared->TitlesChanged();
	fStatus->SetText(("Saved " + path).c_str());
	return true;
}


void
TDocWindow::UpdateTitle()
{
	// TDoc: the document's name, ":N" with more than one window
	BString title = Title();
	if (title.EndsWith(" *"))
		title.Truncate(title.Length() - 2);
	if (fDocument != NULL) {
		// a new document's "untitled N", else the file's name
		title = fDocument->name.c_str();
		if (title.IsEmpty() && !fDocument->path.empty())
			title = BPath(fDocument->path.c_str()).Leaf();
		if (int number = fShared->Number(this))
			title << ":" << number;
	}
	if (fShared->modified)
		title << " *";
	SetTitle(title.String());
}


void
TDocWindow::UpdateTextMarks(const char* family, uint32_t faceFlags,
	float textSize, int alignment)
{
	if (fFontMenu != NULL) {
		BMenuItem* current = fFontMenu->CurrentFamily();
		if (current != NULL)
			current->SetMarked(false);
		if (family != NULL && strlen(family) > 0) {
			for (int32 i = 0; i < fFontMenu->CountItems(); i++) {
				BMenuItem* item = fFontMenu->ItemAt(i);
				BMessage* msg = item->Message();
				if (msg != NULL) {
					const char* name = NULL;
					msg->FindString("font", &name);
					if (name != NULL && strcmp(name, family) == 0) {
						item->SetMarked(true);
						fFontMenu->SetCurrentFamily(item);
						break;
					}
				}
			}
		}
	}

	// Size: mark the item whose value matches textSize
	if (fSizeMenu != NULL) {
		for (int32 i = 0; i < fSizeMenu->CountItems(); i++) {
			BMenuItem* item = fSizeMenu->ItemAt(i);
			BMessage* msg = item->Message();
			if (msg != NULL) {
				int32 val = 0;
				if (msg->FindInt32(kValueString, &val) == B_OK
					&& val == int32(textSize)) {
					item->SetMarked(true);
					break;
				}
			}
		}
	}

	// Face: mark the item whose value matches faceFlags
	if (fFaceMenu != NULL) {
		for (int32 i = 0; i < fFaceMenu->CountItems(); i++) {
			BMenuItem* item = fFaceMenu->ItemAt(i);
			BMessage* msg = item->Message();
			if (msg != NULL) {
				int32 val = 0;
				if (msg->FindInt32(kValueString, &val) == B_OK) {
					// Plain Text (1) is the default; if faceFlags is 0 or 1, mark Plain Text
					if (faceFlags == 0 || faceFlags == 1) {
						item->SetMarked(val == 1);
					} else if (val == 2 && (faceFlags & 2)) {	// Bold
						item->SetMarked(true);
					} else if (val == 4 && (faceFlags & 4)) {	// Italic
						item->SetMarked(true);
					} else if (val == 8 && (faceFlags & 8)) {	// Underline
						item->SetMarked(true);
					} else if (val == 16 && (faceFlags & 16)) {	// Double Underline
						item->SetMarked(true);
					} else if (val == 32 && (faceFlags & 32)) {	// Strike Thru
						item->SetMarked(true);
					} else if (val == 64 && (faceFlags & 64)) {	// Superscript
						item->SetMarked(true);
					} else if (val == 128 && (faceFlags & 128)) {	// Subscript
						item->SetMarked(true);
					} else {
						item->SetMarked(false);
					}
				}
			}
		}
	}

	// Alignment: mark the item whose value matches alignment
	// (MENU 300's values are the PAlg values: 1 center, 2 right)
	if (fAlignmentMenu != NULL) {
		for (int32 i = 0; i < fAlignmentMenu->CountItems(); i++) {
			BMenuItem* item = fAlignmentMenu->ItemAt(i);
			BMessage* msg = item->Message();
			if (msg != NULL) {
				int32 val = 0;
				if (msg->FindInt32(kValueString, &val) == B_OK) {
					item->SetMarked(val == alignment);
				}
			}
		}
	}
}


// TNormalDocWinHelper::SetupMenus: Insert Header/Footer read Remove
// Header/Footer when the document has one (STR# 3, 22-25)
// TDocWindow: the active part's ribbon under the PartBar while the PartBar
// shows: the EntryBar with cells selected, else the text ruler
void
TDocWindow::ShowActiveRibbon()
{
	bool ribbons = fPartBar != NULL && !fPartBar->IsHidden();
	bool cells = fPages != NULL && fPages->fSelectedCell.active;
	auto show = [](BView* view, bool on) {
		if (view == NULL || on == !view->IsHidden())
			return;
		if (on)
			view->Show();
		else
			view->Hide();
	};
	show(fEntryBar, ribbons && cells);
	show(fRuler, ribbons && !(cells && fEntryBar != NULL));
}


void
TDocWindow::MenusBeginning()
{
	InstallPartMenus();
	BWindow::MenusBeginning();
	// TTablePart::FixMenus: Merge Cells and Split Cell
	if (fTableMenus && fPages != NULL && KeyMenuBar() != NULL) {
		bool merge, split;
		fPages->TableMenuState(merge, split);
		if (BMenuItem* item = KeyMenuBar()->FindItem('sTB0'))
			item->SetEnabled(merge);
		if (BMenuItem* item = KeyMenuBar()->FindItem('sTB1'))
			item->SetEnabled(split);
	}
	// TPartGR::FixMenus: Arrange's ported items
	if (fDrawingMenus && fPages != NULL)
		for (BMenu* menu : fPartMenus)
			fPages->FixShapeMenu(menu);
		// TPartSS::FixMenus: Fill Down and Up with a block more than one cell
	// high, Fill Right and Left more than one wide, Sort Cells either way.
	// (Commands not ported yet are off.)
	if (fSheetMenus && fPages != NULL && KeyMenuBar() != NULL) {
		const auto& cell = fPages->fSelectedCell;
		int height = abs(cell.row - cell.anchorRow) + 1;
		int width = abs(cell.column - cell.anchorColumn) + 1;
		auto enable = [&](uint32 command, bool on) {
			if (BMenuItem* item = KeyMenuBar()->FindItem(command))
				item->SetEnabled(on);
		};
		enable('sM70', height > 1);
		enable('sM7B', height > 1);
		enable('sM71', width > 1);
		enable('sM7C', width > 1);
		enable('sM75', height > 1 || width > 1);
		// Auto Calculate marked when on (TPartSS::FixMenus)
		if (BMenuItem* item = KeyMenuBar()->FindItem('sM79')) {
			const SSPart* ss = fDocument->SpreadsheetPart(cell.part);
			item->SetMarked(ss != NULL && ss->autoCalculate);
		}
	}
	if (KeyMenuBar() == NULL)
		return;
	for (int which = 0; which < 2; which++) {
		BMenuItem* item = KeyMenuBar()->FindItem(which == 0 ? kCmdInsertHeader : kCmdInsertFooter);
		if (item == NULL)
			continue;
		bool paged = fDocument != NULL && fDocument->PagePart() >= 0;
		bool has = paged && fDocument->HeaderPart(which) >= 0;
		item->SetEnabled(paged);
		item->SetLabel(GobeResources::IndexString(3, 0x16 + 2 * which + (has ? 1 : 0)).String());
	}
	// TNormalDocWinHelper::SetupMenus: Open Recent (RecentDocsMenu), Undo
	// and Redo with what they would do (STR# 5, 28/29 and THistory)
	if (BMenuItem* recent = KeyMenuBar()->FindItem('sM1C'))
		RecentDocs::FillMenu(recent->Submenu());
	for (int redo = 0; redo < 2; redo++) {
		BMenuItem* item = KeyMenuBar()->FindItem(redo ? 'sM11' : 'sM10');
		if (item == NULL)
			continue;
		bool can = fPages != NULL && (redo ? fPages->fUndo.CanRedo() : fPages->fUndo.CanUndo());
		BString label = GobeResources::IndexString(5, redo ? 29 : 28);
		if (fPages != NULL)
			label << (redo ? fPages->fUndo.RedoLabel() : fPages->fUndo.UndoLabel()).c_str();
		item->SetLabel(label.String());
		item->SetEnabled(can);
	}
	// TNormalDocWinHelper::FixMenus: the Window menu's ribbons, Hide or
	// Show (STR# 1); those not ported are off
	static const struct {
		uint32 command;
		int32 string;
	} kRibbons[] = {
		{'sMF4', 2}, {'sMF6', 4}, {'sMF7', 6}, {'sMF8', 8}, {'sMF3', 10}, {'sMFD', 12}
	};
	for (const auto& ribbon : kRibbons) {
		BMenuItem* item = KeyMenuBar()->FindItem(ribbon.command);
		if (item == NULL)
			continue;
		BView* bar = ribbon.command == 'sMF6' ? (BView*)fPartBar
			: ribbon.command == 'sMF4' ? (BView*)fFormatBar
			: ribbon.command == 'sMF3' ? (BView*)fSheetBar
			: ribbon.command == 'sMF7' ? (BView*)fToolBar : NULL;
		if (ribbon.command == 'sMF8' && fPages != NULL)
			bar = fPages->fGRRulers[0];
		bool shown = bar != NULL && !bar->IsHidden();
		if (ribbon.command == 'sMFD')
			shown = sClipboardWindow.IsValid();
		item->SetLabel(GobeResources::IndexString(1, ribbon.string + (shown ? 0 : 1)).String());
		item->SetEnabled(bar != NULL || ribbon.command == 'sMFD');
	}
	if (BMenuItem* item = KeyMenuBar()->FindItem('sMFE')) {
		int32 windows = 0;
		for (int32 i = 0; BWindow* window = be_app->WindowAt(i); i++)
			windows += dynamic_cast<TDocWindow*>(window) != NULL;
		item->SetEnabled(windows > 1);
	}
	// TNormalDocWinHelper::SetupMenus: Page View marked when on (paged
	// documents)
	if (BMenuItem* item = KeyMenuBar()->FindItem('sM2A')) {
		bool paged = fPages != NULL && fDocument != NULL && fDocument->PagePart() >= 0;
		item->SetEnabled(paged);
		item->SetMarked(paged && fPages->PageView());
	}
	// TNormalDocWinHelper::FixMenus: Save when the document changed, Revert
	// then if it was saved before; Paste Special and Duplicate off (a
	// drawing's part turns Duplicate on: not ported); Cut, Copy and Clear
	// with something selected; Copy Format in a part, Paste Format with a
	// format copied; Views & Layers in drawings only; the Frame menu with a
	// frame selected (the port selects none)
	{
		auto enable = [&](uint32 command, bool on) {
			if (BMenuItem* item = KeyMenuBar()->FindItem(command))
				item->SetEnabled(on);
		};
		bool saved = fDocument != NULL && !fDocument->path.empty();
		enable('sM02', fShared->modified);
		enable('sM04', fShared->modified && saved);
		enable('sM13', fPages != NULL && fPages->fSelectedCell.active
			&& TPageView::HasCopiedCells());
		enable('sM15', false);
		bool cells = fPages != NULL && fPages->fSelectedCell.active;
		bool part = cells || (fPages != NULL && fPages->fCaret.active);
		enable('CCUT', HasTextSelection() || cells);
		enable('COPY', HasTextSelection() || cells);
		enable('sM12', HasTextSelection() || cells);
		enable('sM16', part);
		enable('sM17', part && TPageView::HasCopiedFormat());
		bool drawing = false;
		if (fDocument != NULL && fDocument->MainPart() >= 0
				&& size_t(fDocument->MainPart()) < fDocument->parts.parts.size())
			drawing = fDocument->parts.parts[size_t(fDocument->MainPart())].kind == Sig("GRFX");
		enable('sM25', drawing);
		// the Frame menu's frame commands (TNormalDocWinHelper::FixMenus)
		// with a frame selected: Make Linked Frame for text, the autogrow
		// and scroll bar settings marked as the frame has them; Open
		// Frame Window a window on the frame
		const uint32_t* framePointer = fPages != NULL ? fPages->SelectedFrame() : NULL;
		const gobe::Frame* frame = framePointer != NULL && fDocument != NULL
			? FrameAt(*fDocument, framePointer) : NULL;
		enable('sM30', frame != NULL);
		enable('sM32', frame != NULL && fDocument->WordPart(framePointer[0]) != NULL);
		for (uint32 command : {'sM33', 'sM34', 'sM35', 'sM36'})
			enable(command, frame != NULL);
		for (const auto& [command, bit] : {std::make_pair('sM34', 8u), std::make_pair('sM35', 4u),
				std::make_pair('sM36', 0x20u)})
			if (BMenuItem* item = KeyMenuBar()->FindItem(command))
				item->SetMarked(frame != NULL && (frame->values[3] & bit) != 0);
	}
	// TWPPart::FixMenus: Columns… in the main text with Page View on;
	// Insert Footnote…, Frame Break and Page Break not in a header, footer
	// or note (a part of kind 1 to 3)
	if (fPages != NULL && fPages->fCaret.active && fDocument != NULL) {
		if (const WPPart* wp = fDocument->WordPart(fPages->fCaret.part)) {
			bool main = int(fPages->fCaret.part) == fDocument->MainPart();
			if (BMenuItem* item = KeyMenuBar()->FindItem('sM51'))
				item->SetEnabled(main && fPages->PageView());
			bool body = uint16(wp->version98 - 1) > 2;
			for (uint32 command : {'sM5I', 'sM58', 'sM59'})
				if (BMenuItem* item = KeyMenuBar()->FindItem(command))
					item->SetEnabled(body);
		}
	}
	// TNormalDocWinHelper::FixMenus: Find Again and Replace All with a find
	// string, Find Selection with text selected, Replace & Find with both
	{
		bool findText = !AppFindReplace().find.empty();
		bool selection = HasTextSelection();
		static const struct {
			uint32 command;
			int needs;			// 1 find text, 2 a selection
		} kFind[] = {{'sME1', 1}, {'sME2', 2}, {'sME3', 3}, {'sME4', 1}};
		for (const auto& f : kFind)
			if (BMenuItem* item = KeyMenuBar()->FindItem(f.command))
				item->SetEnabled((!(f.needs & 1) || findText) && (!(f.needs & 2) || selection));
	}
	// TNormalDocWinHelper::FixMenus: Hide or Show SpellBar (STR# 1); Skip,
	// Learn and Replace as the bar's buttons
	if (BMenuItem* item = KeyMenuBar()->FindItem('sMC5')) {
		bool shown = fSpellBar != NULL && !fSpellBar->IsHidden(fSpellBar);
		item->SetLabel(GobeResources::IndexString(1, shown ? 0 : 1).String());
		item->SetEnabled(fSpellBar != NULL);
	}
	for (uint32 command = 'sMC0'; command <= 'sMC4'; command++) {
		BMenuItem* item = KeyMenuBar()->FindItem(command);
		if (item == NULL)
			continue;
		bool on = fSpellBar != NULL;
		if (on && command == 'sMC2')
			on = fSpellBar->SkipEnabled();
		else if (on && command == 'sMC3')
			on = fSpellBar->LearnEnabled();
		else if (on && command == 'sMC4')
			on = fSpellBar->ReplaceEnabled();
		item->SetEnabled(on);
	}
	// New Window: for a document
	if (BMenuItem* item = KeyMenuBar()->FindItem('sMF0'))
		item->SetEnabled(fDocument != NULL);
	// TWPPart::SetupMenus: Show Invisibles or Hide Invisibles (STR# 1302)
	if (BMenuItem* item = KeyMenuBar()->FindItem('sM5J'))
		item->SetLabel(GobeResources::IndexString(0x516,
			fDocument != NULL && (fDocument->documentFlags & 4) ? 1 : 0).String());
}


bool
TDocWindow::QuitRequested()
{
	// TDoc::SaveChanges: "Save changes to “name” before closing?" (before
	// quitting, when the application quits): Don't Save (d), Cancel (Esc),
	// Save
	// TDoc::RemoveWindow: one of several windows just closes
	{
		BAutolock locker(fShared->lock);
		if (fShared->windows.size() > 1) {
			fAppQuitting = false;
			return true;
		}
	}
	if (fShared->modified && fDocument != NULL) {
		// the document's name as in the title
		BString name = fDocument->name.c_str();
		if (name.IsEmpty() && !fDocument->path.empty())
			name = BPath(fDocument->path.c_str()).Leaf();
		BString text = GobeResources::IndexString(2, 13);
		text << name << GobeResources::IndexString(2, fAppQuitting ? 14 : 15);
		BAlert* alert = new BAlert("Alert Thread", text.String(),
			GobeResources::IndexString(2, 12).String(), GobeResources::IndexString(2, 1).String(),
			GobeResources::IndexString(2, 11).String(), B_WIDTH_AS_USUAL, B_WARNING_ALERT);
		alert->SetShortcut(0, 'd');
		alert->SetShortcut(1, B_ESCAPE);
		int32 choice = alert->Go();
		fAppQuitting = false;
		if (choice == 1)
			return false;
		if (choice == 2) {
			// an untitled document: Save As, then close
			if (fDocument->path.empty()) {
				fCloseAfterSave = true;
				PostMessage(kCmdSaveAs);
				return false;
			}
			if (!Save(fDocument->path))
				return false;
		}
	}
	// the last document closed: the application quits
	int32 documents = 0;
	for (int32 i = 0; BWindow* w = be_app->WindowAt(i); i++)
		documents += dynamic_cast<TDocWindow*>(w) != NULL;
	if (documents <= 1)
		be_app->PostMessage(B_QUIT_REQUESTED);
	return true;
}

// #pragma mark - Find/Change


TDocWindow::~TDocWindow()
{
	sWindows.erase(std::remove(sWindows.begin(), sWindows.end(), this), sWindows.end());
	TSharedDocument::Detach(fShared, this);
}


void
TDocWindow::WindowActivated(bool active)
{
	BWindow::WindowActivated(active);
	if (active) {
		sWindows.erase(std::remove(sWindows.begin(), sWindows.end(), this), sWindows.end());
		sWindows.insert(sWindows.begin(), this);
		if (TDialogFindChange* find = TDialogFindChange::Open())
			find->PostMessage('sD=L');
		TDialogHistory::Refresh();
		TDialogStylesEdit::Refresh();
		TDialogLayers::Refresh();
		RefreshTearOffs();
		TDialogPRNavigate::Refresh();
	}
}


// TApp::FrontDocWindow
TDocWindow*
TDocWindow::Front()
{
	return sWindows.empty() ? NULL : sWindows.front();
}


SetMgr
TDocWindow::InkSets() const
{
	return fPages != NULL ? fPages->Doc().inkSets : SetMgr();
}


std::vector<PenType>
TDocWindow::Pens() const
{
	if (fPages == NULL)
		return AppPens();
	return DocumentPens(fPages->Doc().pens, AppPens());
}


bool
TDocWindow::LayersState(struct LayersState& state) const
{
	state = gobe::LayersState();
	if (fDocument == NULL || fDocument->MainPart() < 0
			|| fDocument->parts.parts[size_t(fDocument->MainPart())].kind != Sig("GRFX"))
		return false;
	size_t part = size_t(fDocument->MainPart());
	const GRPart* gr = fDocument->GraphicsPart(part);
	if (gr == NULL)
		return false;
	for (const GRView& view : gr->views)
		state.views.push_back(view.name);
	state.shownView = fDocument->ShownView(part);
	if (state.shownView < gr->views.size())
		for (const GRView::LayerInfo& info : gr->views[state.shownView].layers) {
			gobe::LayersState::Row row;
			row.name = info.layer < gr->layers.size() ? gr->layers[info.layer].name : "";
			std::copy(info.flags, info.flags + 5, row.flags);
			state.rows.push_back(row);
		}
	state.layers = gr->layers.size();
	state.activeLayer = fDocument->ActiveLayer(part);
	return true;
}


void
TDocWindow::HistoryState(std::vector<std::string>& labels, size_t& done, bool& show) const
{
	labels.clear();
	done = 0;
	show = false;
	if (fPages == NULL)
		return;
	BAutolock locker(fShared->lock);
	labels = fPages->fUndo.Labels(done);
	show = fPages->fShowUndoSteps;
}


// TStyleManager::GetActiveStyles' and GetActiveStyle's sources: the
// active part (the caret's, else the main part) if it's text, its kinds
// (TWPPart::ActiveStyles: paragraph, basic) and the selection's styles
void
TDocWindow::GetStylesSnapshot(StylesSnapshot& snapshot) const
{
	snapshot = StylesSnapshot();
	if (fDocument == NULL || fPages == NULL)
		return;
	BAutolock locker(fShared->lock);
	snapshot.document = fDocument;
	snapshot.styles = fDocument->styles;
	size_t part = fPages->fCaret.active ? fPages->fCaret.part : size_t(fDocument->MainPart());
	const WPPart* wp = fDocument->MainPart() >= 0 || fPages->fCaret.active
		? fDocument->WordPart(part) : NULL;
	if (wp == NULL)
		return;
	snapshot.types = {Sig("Para"), Sig("Basi")};
	TextPosition from, to;
	if (fPages->fCaret.active) {
		auto range = fPages->TextSelectionRange();
		from = range.first;
		to = range.second;
	}
	// (in a table's cells: TTablePartStyleIterator)
	uint32_t frameStyle;
	if (fPages->CaretCellFrameStyle(frameStyle))
		snapshot.selection = TableCellStyles(snapshot.styles, frameStyle);
	else
		snapshot.selection = TextSelectionStyles(snapshot.styles, *wp, from, to);
	// (CreateActivePartStyleIterator: while a style is being edited, it)
	uint32_t edit = fPages->EditStyle();
	if (edit < snapshot.styles.styles.size()) {
		snapshot.selection = SelectionStyles();
		snapshot.selection.active = snapshot.styles.styles[edit].name;
	}
}


bool
TDocWindow::HasTextSelection() const
{
	TextPosition from, to;
	return fPages != NULL && fPages->fCaret.active
		&& fDocument != NULL && fDocument->WordPart(fPages->fCaret.part) != NULL
		&& fPages->TextSelection(from, to);
}


// TFindReplace::DoMessage: a message's "find", "replace", "caseSens",
// "wholeWord" and "wrap" first; then the dialog (sME0), DoFind (sME1, from
// the selection's end; a beep if there's nothing), DoFindSelection (sME2),
// DoReplaceAndFind (sME3), DoReplaceAll (sME4: from the top, one step) or
// DoReplace (sME5: the selected text, whatever it is); the dialog's
// buttons brought up to date
void
TDocWindow::FindReplaceCommand(BMessage* message)
{
	FindReplace& find = AppFindReplace();
	bool on;
	const char* text;
	if (message->FindBool("caseSens", &on) == B_OK)
		find.SetCaseSensitive(on);
	if (message->FindBool("wholeWord", &on) == B_OK)
		find.wholeWord = on;
	if (message->FindBool("wrap", &on) == B_OK)
		find.wrap = on;
	if (message->FindString("find", &text) == B_OK)
		find.SetFind(text);
	if (message->FindString("replace", &text) == B_OK)
		find.replace = text;

	auto doFind = [&]() {
		if (find.Pattern().empty() || fPages == NULL || fDocument == NULL)
			return;
		bool fromPosition = false;
		size_t part = 0;
		uint32_t position = 0;
		const WPPart* wp = fPages->fCaret.active ? fDocument->WordPart(fPages->fCaret.part)
			: NULL;
		if (wp != NULL) {
			TextPosition from, to;
			if (!fPages->TextSelection(from, to))
				to = {fPages->fCaret.paragraph, fPages->fCaret.offset};
			fromPosition = true;
			part = fPages->fCaret.part;
			position = PartPosition(*wp, to);
		}
		FoundText found;
		if (FindNext(*fDocument, find, fromPosition, part, position, found))
			fPages->SelectRange(found.part, found.start, found.end);
		else
			beep();
	};
	auto doReplace = [&]() {
		TextPosition from, to;
		if (!HasTextSelection() || !fPages->TextSelection(from, to))
			return;
		const WPPart* wp = fDocument->WordPart(fPages->fCaret.part);
		fPages->ReplaceRange(fPages->fCaret.part, PartPosition(*wp, from), PartPosition(*wp, to),
			find.replace);
	};

	switch (message->what) {
		case kCmdFindReplace:
		case kMsgOpenDialog:
			if (TDialogFindChange* dialog = TDialogFindChange::Open())
				dialog->Activate(true);
			else
				(new TDialogFindChange())->RunDialog();
			break;
		case kCmdFindAgain:
			doFind();
			break;
		case kCmdFindSelection:
		{
			TextPosition from, to;
			if (!HasTextSelection() || !fPages->TextSelection(from, to))
				break;
			std::string selected = RangeText(*fDocument->WordPart(fPages->fCaret.part), from, to);
			if (selected.size() < 0x800) {
				find.SetFind(selected);
				doFind();
			} else {
				BString alert;
				alert.SetToFormat(GobeResources::IndexString(6, 22).String(), 0x800);
				(new BAlert("", alert.String(), "OK"))->Go(NULL);
			}
			break;
		}
		case kCmdReplaceFindAgain:
			if (!find.Pattern().empty() && HasTextSelection()) {
				doReplace();
				doFind();
			}
			break;
		case kCmdReplaceAll:
		{
			FoundText found, last;
			if (find.Pattern().empty() || fPages == NULL || fDocument == NULL
					|| !FindNext(*fDocument, find, false, 0, 0, found))
				break;
			fPages->fUndo.BeginDocument(*fDocument, GobeResources::IndexString(3, 29).String(),
				fPages->CurrentViewState());
			if (ReplaceEvery(*fDocument, find, last) > 0) {
				fPages->TextChanged();
				fPages->SelectRange(last.part, last.start, last.end);
			}
			break;
		}
		case 'sME5':
			doReplace();
			break;
	}
	if (TDialogFindChange* dialog = TDialogFindChange::Open())
		dialog->PostMessage('sD=L');
}

void
TDocWindow::TearOffState(struct TearOffState& state) const
{
	if (fDocument == NULL || fPages == NULL)
		return;
	state.document = true;
	state.names = SelectionNames(*fDocument);
	ShapeStyle style;
	if (fPages->SelectedShapeStyle(style)) {
		state.shapes = true;
		state.opacity = int32(std::lround(std::min(style.fillOpacity, style.penOpacity) * 100));
	}
	state.zoomable = true;
	state.zoom = fPages->Zoom();
	if (fPages->fCaret.active && fDocument->WordPart(fPages->fCaret.part) != NULL) {
		TextStyle text = fPages->StyleAt(fPages->TextSelectionRange().first);
		state.text = true;
		state.family = text.family;
		state.size = text.size;
		state.faces = text.faceFlags;
	}
	GetStylesSnapshot(state.styles);
}

void
TDocWindow::PresentationState(struct PresentationState& state) const
{
	if (fDocument == NULL || fDocument->presentationPart < 0)
		return;
	const Document& doc = *fDocument;
	const GRPart* gr = doc.PresentationGraphics();
	if (gr == NULL)
		return;
	state.presentation = true;
	for (const GRLayer& layer : gr->layers)
		state.partLayers.push_back(layer.name);
	for (size_t k = 0; k < doc.slides.size(); k++) {
		PresentationState::SlideRow row;
		row.name = doc.SlideName(k);
		std::vector<bool> in(gr->layers.size(), false);
		for (size_t l = 0; l < doc.SlideLayers(k).size(); l++) {
			int part = doc.SlideLayerPart(k, l);
			if (part >= 0 && size_t(part) < gr->layers.size()) {
				row.layers.push_back(gr->layers[size_t(part)].name);
				in[size_t(part)] = true;
			}
		}
		row.collapsed = doc.slides[k].collapsed;
		row.transition = doc.slides[k].transition;
		row.options = doc.slides[k].options;
		state.slides.push_back(row);
		state.inSlide.push_back(in);
	}
	state.currentSlide = doc.currentSlide;
	state.currentLayer = int32(doc.presentation.layer);
	state.autoStart = doc.presentation.autoStart;
	state.cursor = doc.presentation.cursor;
}


void
TDocWindow::RefreshPresentation()
{
	TDialogPRNavigate::Refresh();
}

} // namespace gobe
