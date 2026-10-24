#include "TDocWindow.h"

#include <stdio.h>
#include <stdlib.h>

#include <algorithm>
#include <vector>
#include <string.h>

#include <Alert.h>
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
#include "Editing.h"
#include "Formats.h"
#include "GobeResources.h"
#include "TDialog.h"
#include "TDocumentDialogs.h"
#include "TRecentDocs.h"
#include "TDocView.h"
#include "TPageView.h"
#include "TTranslate.h"
#include "TFormatBar.h"
#include "TPageBar.h"
#include "TPartBar.h"
#include "TSpellBar.h"
#include "TSpellDialogs.h"
#include "TFindDialog.h"
#include "TPreferences.h"
#include "Find.h"
#include "TWPRuler.h"
#include "Styles.h"

#include <ScrollView.h>
#include "TMenu.h"

namespace gobe {

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
static const uint32 kMsgColWidth = 'gCOL';
static const uint32 kMsgRowHeight = 'gROW';
static const uint32 kMsgGoTo = 'gGOT';

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


// Helpers for cell reference parsing (A1, AB12, etc.)
namespace {

static bool ParseCellRef(const char* text, int& row, int& column)
{
	const char* p = text;
	// Skip leading whitespace
	while (*p == ' ') p++;
	
	// Parse column letters
	int col = 0;
	while (*p >= 'A' && *p <= 'Z') {
		col = col * 26 + (*p - 'A' + 1);
		p++;
	}
	// Must have at least one letter
	if (col == 0) return false;
	col--;  // 0-indexed
	
	// Parse row digits (must be at least one)
	const char* d = p;
	while (*p >= '0' && *p <= '9') p++;
	if (p == d) return false;
	
	row = atoi(d) - 1;  // 0-indexed
	column = col;
	
	if (row < 0 || row > 0x3fff || column > 0x2be)
		return false;
	return true;
}


static void CellRefToString(int column, int row, BString& out)
{
	// Column letters
	BString colStr;
	int c = column + 1;  // 1-indexed
	while (c > 0) {
		c--;
		char ch = 'A' + (c % 26);
		colStr.Prepend(&ch, 1);
		c /= 26;
	}
	out << colStr << (row + 1);
}

} // namespace


// #pragma mark - TColumnWidthDialog


TColumnWidthDialog::TColumnWidthDialog(BHandler* target, Document* doc,
	TPageView* view)
	:
	BWindow(BRect(100, 100, 320, 200), "Column Width",
		B_TITLED_WINDOW_LOOK, B_NORMAL_WINDOW_FEEL, B_NOT_RESIZABLE
		| B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS),
	fDocument(doc),
	fView(view),
	fField(NULL),
	fOKBtn(NULL)
{
	BView* top = new BView("top", B_WILL_DRAW);
	top->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	SetLayout(new BGroupLayout(B_VERTICAL, 0));
	GetLayout()->AddView(top);
	top->SetLayout(BLayoutBuilder::Group<>(B_VERTICAL, B_USE_SMALL_INSETS)
		.AddGroup(B_HORIZONTAL, B_USE_SMALL_INSETS)
			.Add(new BStringView("label", "Column width (points):"))
			.AddGlue()
		.End()
		.Add(fField = new BTextControl("field", NULL, "", NULL))
		.AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING)
			.AddGlue()
			.Add(fOKBtn = new BButton("ok", "OK",
				new BMessage(kMsgColWidth)))
			.Add(new BButton("cancel", "Cancel",
				new BMessage(B_QUIT_REQUESTED)))
		.End()
		.SetInsets(B_USE_WINDOW_INSETS)
	);
	
	// Pre-fill with current column width
	if (fView != NULL) {
		float w = 0;
		if (fDocument != NULL) {
			const SSPart* ss = fDocument->SpreadsheetPart(fView->fSelectedCell.part);
			if (ss != NULL) {
				int c0 = 0;
				if (fView->fSelectedCell.active)
					c0 = std::min(fView->fSelectedCell.column,
						fView->fSelectedCell.anchorColumn);
				w = ss->ColumnWidth(uint16_t(c0));
			} else
				w = 72.0f;
		} else
			w = 72.0f;
		char buf[32];
		snprintf(buf, sizeof buf, "%.1f", w);
		fField->SetText(buf);
		fField->TextView()->SelectAll();
	}
	
	CenterOnScreen();
}


void
TColumnWidthDialog::DoApply()
{
	if (fView == NULL || fDocument == NULL)
		return;
	
	// Parse the width value
	char* end;
	float width = strtof(fField->Text(), &end);
	if (end == fField->Text() || width < 0) {
		fprintf(stderr, "colwidth: invalid value '%s'\n", fField->Text());
		return;
	}
	
	// Get the selection range (columns)
	size_t part = fView->fSelectedCell.part;
	int c0, c1;
	if (fView->fSelectedCell.active && fView->fSelectedCell.column >= 0
		&& fView->fSelectedCell.anchorColumn >= 0) {
		c0 = std::min(fView->fSelectedCell.column, fView->fSelectedCell.anchorColumn);
		c1 = std::max(fView->fSelectedCell.column, fView->fSelectedCell.anchorColumn);
	} else {
		c0 = 0; c1 = 0;
	}
	
	gobe::SSPart* ss = fDocument->EditableSpreadsheetPart(part);
	if (ss == NULL) {
		fprintf(stderr, "colwidth: no spreadsheet part\n");
		return;
	}
	
	fView->fUndo.Begin(*fDocument, part, "Column Width",
		fView->CurrentViewState());
	bool changed = gobe::SetLineSize(*ss, true, uint16_t(c0), uint16_t(c1), width);
	if (changed) {
		fView->fBook->Reset();
		PostMessage(kMsgDocumentChanged);
		fprintf(stderr, "colwidth: %.1f for columns %d-%d\n", width, c0, c1);
	}
}


void
TColumnWidthDialog::MessageReceived(BMessage* message)
{
	if (message->what == kMsgColWidth) {
		DoApply();
		PostMessage(B_QUIT_REQUESTED);
		return;
	}
	BWindow::MessageReceived(message);
}


bool
TColumnWidthDialog::QuitRequested()
{
	delete this;
	return true;
}


// #pragma mark - TRowHeightDialog


TRowHeightDialog::TRowHeightDialog(BHandler* target, Document* doc,
	TPageView* view)
	:
	BWindow(BRect(100, 100, 320, 200), "Row Height",
		B_TITLED_WINDOW_LOOK, B_NORMAL_WINDOW_FEEL, B_NOT_RESIZABLE
		| B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS),
	fDocument(doc),
	fView(view),
	fField(NULL),
	fOKBtn(NULL)
{
	BView* top = new BView("top", B_WILL_DRAW);
	top->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	SetLayout(new BGroupLayout(B_VERTICAL, 0));
	GetLayout()->AddView(top);
	top->SetLayout(BLayoutBuilder::Group<>(B_VERTICAL, B_USE_SMALL_INSETS)
		.AddGroup(B_HORIZONTAL, B_USE_SMALL_INSETS)
			.Add(new BStringView("label", "Row height (points):"))
			.AddGlue()
		.End()
		.Add(fField = new BTextControl("field", NULL, "", NULL))
		.AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING)
			.AddGlue()
			.Add(fOKBtn = new BButton("ok", "OK",
				new BMessage(kMsgRowHeight)))
			.Add(new BButton("cancel", "Cancel",
				new BMessage(B_QUIT_REQUESTED)))
		.End()
		.SetInsets(B_USE_WINDOW_INSETS)
	);
	
	// Pre-fill with current row height
	if (fView != NULL) {
		float h = 0;
		if (fDocument != NULL) {
			const SSPart* ss = fDocument->SpreadsheetPart(fView->fSelectedCell.part);
			if (ss != NULL) {
				int r = 0;
				if (fView->fSelectedCell.active)
					r = fView->fSelectedCell.row;
				h = ss->RowHeight(uint16_t(r));
			} else
				h = 16.0f;
		} else
			h = 16.0f;
		char buf[32];
		snprintf(buf, sizeof buf, "%.1f", h);
		fField->SetText(buf);
		fField->TextView()->SelectAll();
	}
	
	CenterOnScreen();
}


void
TRowHeightDialog::DoApply()
{
	if (fView == NULL || fDocument == NULL)
		return;
	
	// Parse the height value
	char* end;
	float height = strtof(fField->Text(), &end);
	if (end == fField->Text() || height < 0) {
		fprintf(stderr, "rowheight: invalid value '%s'\n", fField->Text());
		return;
	}
	
	// Get the selection range (rows)
	size_t part = fView->fSelectedCell.part;
	int r0, r1;
	if (fView->fSelectedCell.active && fView->fSelectedCell.row >= 0
		&& fView->fSelectedCell.anchorRow >= 0) {
		r0 = std::min(fView->fSelectedCell.row, fView->fSelectedCell.anchorRow);
		r1 = std::max(fView->fSelectedCell.row, fView->fSelectedCell.anchorRow);
	} else {
		r0 = 0; r1 = 0;
	}
	
	gobe::SSPart* ss = fDocument->EditableSpreadsheetPart(part);
	if (ss == NULL) {
		fprintf(stderr, "rowheight: no spreadsheet part\n");
		return;
	}
	
	fView->fUndo.Begin(*fDocument, part, "Row Height",
		fView->CurrentViewState());
	bool changed = gobe::SetLineSize(*ss, false, uint16_t(r0), uint16_t(r1), height);
	if (changed) {
		fView->fBook->Reset();
		PostMessage(kMsgDocumentChanged);
		fprintf(stderr, "rowheight: %.1f for rows %d-%d\n", height, r0, r1);
	}
}


void
TRowHeightDialog::MessageReceived(BMessage* message)
{
	if (message->what == kMsgRowHeight) {
		DoApply();
		PostMessage(B_QUIT_REQUESTED);
		return;
	}
	BWindow::MessageReceived(message);
}


bool
TRowHeightDialog::QuitRequested()
{
	delete this;
	return true;
}


// #pragma mark - TGoToCellDialog


TGoToCellDialog::TGoToCellDialog(BHandler* target, Document* doc,
	TPageView* view)
	:
	BWindow(BRect(100, 100, 300, 180), "Go To Cell",
		B_TITLED_WINDOW_LOOK, B_NORMAL_WINDOW_FEEL, B_NOT_RESIZABLE
		| B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS),
	fDocument(doc),
	fView(view),
	fField(NULL),
	fOKBtn(NULL)
{
	BView* top = new BView("top", B_WILL_DRAW);
	top->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	SetLayout(new BGroupLayout(B_VERTICAL, 0));
	GetLayout()->AddView(top);
	top->SetLayout(BLayoutBuilder::Group<>(B_VERTICAL, B_USE_SMALL_INSETS)
		.AddGroup(B_HORIZONTAL, B_USE_SMALL_INSETS)
			.Add(new BStringView("label", "Cell reference:"))
			.AddGlue()
		.End()
		.Add(fField = new BTextControl("field", NULL, "", NULL))
		.AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING)
			.AddGlue()
			.Add(fOKBtn = new BButton("ok", "OK",
				new BMessage(kMsgGoTo)))
			.Add(new BButton("cancel", "Cancel",
				new BMessage(B_QUIT_REQUESTED)))
		.End()
		.SetInsets(B_USE_WINDOW_INSETS)
	);
	
	// Pre-fill with current cell reference
	if (fView != NULL) {
		BString ref;
		if (fView->fSelectedCell.active) {
			CellRefToString(fView->fSelectedCell.column, fView->fSelectedCell.row, ref);
		} else {
			ref = "A1";
		}
		fField->SetText(ref.String());
		fField->TextView()->SelectAll();
	}
	
	CenterOnScreen();
}


void
TGoToCellDialog::DoGoTo()
{
	if (fView == NULL || fDocument == NULL)
		return;
	
	// Parse the cell reference
	int row, column;
	if (!ParseCellRef(fField->Text(), row, column)) {
		fprintf(stderr, "goto: invalid cell reference '%s'\n", fField->Text());
		return;
	}
	
	// Find the spreadsheet part
	size_t part = fView->fSelectedCell.part;
	const SSPart* ss = fDocument->SpreadsheetPart(part);
	if (ss == NULL) {
		fprintf(stderr, "goto: no spreadsheet part\n");
		return;
	}
	
	// Move the selection
	fView->fSelectedCell.active = true;
	fView->fSelectedCell.part = part;
	fView->fSelectedCell.row = row;
	fView->fSelectedCell.column = column;
	fView->fSelectedCell.anchorRow = row;
	fView->fSelectedCell.anchorColumn = column;
	
	fprintf(stderr, "goto: %s (row %d, column %d)\n", fField->Text(), row, column);
}


void
TGoToCellDialog::MessageReceived(BMessage* message)
{
	if (message->what == kMsgGoTo) {
		DoGoTo();
		PostMessage(B_QUIT_REQUESTED);
		return;
	}
	BWindow::MessageReceived(message);
}


bool
TGoToCellDialog::QuitRequested()
{
	delete this;
	return true;
}


// #pragma mark - TDocWindow


TDocWindow::TDocWindow(BRect frame, const char* title, Document* document)
	:
	BWindow(frame, title, B_DOCUMENT_WINDOW, B_AUTO_UPDATE_SIZE_LIMITS)
{
	sWindows.insert(sWindows.begin(), this);
	TMenuBar* menuBar = InstallBaseMenus();
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
			TPageView* pages = new TPageView(*document);
			fPages = pages;
			// the SpellBar (TNormalDocWinHelper: hidden until wanted)
			fSpellBar = new TSpellBar(pages);
			fSpellBar->Hide();
			pages->fSpellBar = fSpellBar;
			TDocScrollView* scroll = new TDocScrollView(pages);
			fScaleSlider = scroll->Scale();
			pages->fPageBar = scroll->Pages();
			desk = scroll;
			// the text ruler (TWPPart::ActivePartRibbon) for documents with text
			bool text = false;
			for (const auto& part : document->parts.parts)
				text = text || part.kind == Sig("WORD");
			if (text) {
				fFormatBar = new TFormatBar(pages);
				pages->fFormatBar = fFormatBar;
				fPartBar = new TPartBar(pages);
				pages->fPartBar = fPartBar;
				fRuler = new TWPRuler(pages);
				pages->fRuler = fRuler;
			}
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
	if (fFormatBar != NULL)
		layout.Add(fFormatBar);
	if (fPartBar != NULL)
		layout.Add(fPartBar);
	if (fSpellBar != NULL)
		layout.Add(fSpellBar);
	if (fRuler != NULL)
		layout.Add(fRuler);
	layout
		.Add(desk)
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

	// the text part's Format menu (MENU 51)
	bar->AddItem(new TMenu(51));


	// the Spreadsheet part's Format menu (MENU 52 of Parts/Spreadsheet)
	// and Options menu (MENU 53), with the items that work so far
	if (fDocument != NULL && !fDocument->spreadsheetParts.empty()) {
		// Format menu: Column Width, Row Height, Display Options
		BMenu* format = new BMenu("Format");
		format->AddItem(new BMenuItem("Column Width…", new BMessage(kCmdColWidth)));
		format->AddItem(new BMenuItem("Row Height…", new BMessage(kCmdRowHeight)));
		BMenuItem* displayOpts = new BMenuItem("Display Options", NULL);
		displayOpts->SetEnabled(false);
		format->AddItem(displayOpts);
		bar->AddItem(format);

		BMenu* options = new BMenu("Options");
		options->AddItem(new BMenuItem("Fill Down", new BMessage('sM70'), 'D'));
		options->AddItem(new BMenuItem("Fill Right", new BMessage('sM71'), 'R'));
		options->AddSeparatorItem();

		// Cell Format submenu
		BMenu* formatMenu = new BMenu("Cell Format");
		{
			BMessage* m = new BMessage('sM22');
			m->AddString("value", "");
			formatMenu->AddItem(new BMenuItem("None", m));
		}
		{
			BMessage* m = new BMessage('sM22');
			m->AddString("value", "0.00");
			formatMenu->AddItem(new BMenuItem("0.00", m));
		}
		{
			BMessage* m = new BMessage('sM22');
			m->AddString("value", "0.00%");
			formatMenu->AddItem(new BMenuItem("0.00%", m));
		}
		{
			BMessage* m = new BMessage('sM22');
			m->AddString("value", "0E+00");
			formatMenu->AddItem(new BMenuItem("0E+00", m));
		}
		{
			BMessage* m = new BMessage('sM22');
			m->AddString("value", "$#,##0");
			formatMenu->AddItem(new BMenuItem("$#,##0", m));
		}
		{
			BMessage* m = new BMessage('sM22');
			m->AddString("value", "#,##0.00");
			formatMenu->AddItem(new BMenuItem("#,##0.00", m));
		}
		options->AddItem(new BMenuItem(formatMenu));

		// Cell Fill submenu
		BMenu* fillMenu = new BMenu("Cell Fill");
		{
			BMessage* m = new BMessage('sM23');
			m->AddInt32("red", 255); m->AddInt32("green", 255); m->AddInt32("blue", 255);
			fillMenu->AddItem(new BMenuItem("None", m));
		}
		{
			BMessage* m = new BMessage('sM23');
			m->AddInt32("red", 255); m->AddInt32("green", 0); m->AddInt32("blue", 0);
			fillMenu->AddItem(new BMenuItem("Red", m));
		}
		{
			BMessage* m = new BMessage('sM23');
			m->AddInt32("red", 255); m->AddInt32("green", 255); m->AddInt32("blue", 0);
			fillMenu->AddItem(new BMenuItem("Yellow", m));
		}
		{
			BMessage* m = new BMessage('sM23');
			m->AddInt32("red", 0); m->AddInt32("green", 255); m->AddInt32("blue", 0);
			fillMenu->AddItem(new BMenuItem("Green", m));
		}
		{
			BMessage* m = new BMessage('sM23');
			m->AddInt32("red", 0); m->AddInt32("green", 0); m->AddInt32("blue", 255);
			fillMenu->AddItem(new BMenuItem("Blue", m));
		}
		{
			BMessage* m = new BMessage('sM23');
			m->AddInt32("red", 255); m->AddInt32("green", 255); m->AddInt32("blue", 255);
			fillMenu->AddItem(new BMenuItem("White", m));
		}
		options->AddItem(new BMenuItem(fillMenu));

		// Cell Align Horizontal
		BMenu* hAlignMenu = new BMenu("Align Horizontal");
		{
			BMessage* m = new BMessage('sM20');
			m->AddInt32("value", 0);
			hAlignMenu->AddItem(new BMenuItem("Left", m));
		}
		{
			BMessage* m = new BMessage('sM20');
			m->AddInt32("value", 1);
			hAlignMenu->AddItem(new BMenuItem("Center", m));
		}
		{
			BMessage* m = new BMessage('sM20');
			m->AddInt32("value", 2);
			hAlignMenu->AddItem(new BMenuItem("Right", m));
		}
		options->AddItem(new BMenuItem(hAlignMenu));

		// Cell Align Vertical
		BMenu* vAlignMenu = new BMenu("Align Vertical");
		{
			BMessage* m = new BMessage('sM21');
			m->AddInt32("value", 0);
			vAlignMenu->AddItem(new BMenuItem("Top", m));
		}
		{
			BMessage* m = new BMessage('sM21');
			m->AddInt32("value", 1);
			vAlignMenu->AddItem(new BMenuItem("Center", m));
		}
		{
			BMessage* m = new BMessage('sM21');
			m->AddInt32("value", 2);
			vAlignMenu->AddItem(new BMenuItem("Bottom", m));
		}
		options->AddItem(new BMenuItem(vAlignMenu));

		options->AddSeparatorItem();
		// Wrap Text toggle
		{
			BMessage* m = new BMessage('sM24');
			m->AddInt32("value", 1);
			options->AddItem(new BMenuItem("Wrap Text", m));
		}
		options->AddSeparatorItem();

		options->AddItem(new BMenuItem("Insert Cells…", new BMessage('sM73')));
		options->AddItem(new BMenuItem("Delete Cells…", new BMessage('sM74')));
		options->AddSeparatorItem();
		options->AddItem(new BMenuItem("Go To Cell…", new BMessage(kCmdGoToCell)));
		bar->AddItem(options);
	}

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
		// the zoom tool (TDocWindow::ScaleTo)
		case 's003':
			if (fPages != NULL)
				fPages->SetZoom(message->GetFloat("scale", 1));
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
		case 'sMF6':
			if (fPartBar != NULL) {
				bool hide = !fPartBar->IsHidden();
				for (BView* view : {(BView*)fPartBar, (BView*)fRuler}) {
					if (view == NULL)
						continue;
					if (hide)
						view->Hide();
					else
						view->Show();
				}
			}
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
					main >= 0 && fDocument->WordPart(size_t(main)) != NULL);
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
					if (!ExportFile(*fDocument, path.Path(), format, error))
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
						fModified = false;
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
			if (fDocument == NULL || !fModified || fDocument->path.empty())
				return;
			// TDoc::RevertAlert: "Revert “name” to last saved version?",
			// Cancel (Esc) or Revert
			BString text = GobeResources::IndexString(2, 17);
			text << fDocument->name.c_str() << GobeResources::IndexString(2, 18);
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
		if (what == 'sM20' || what == 'sM21' || what == 'sM22' || what == 'sM23'
			|| what == 'sM24' || what == 'sM52' || what == 'sM56' || what == 'sM57'
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
	if (message->what == kCmdColWidth) {
		if (fPages != NULL) {
			TColumnWidthDialog* dialog = new TColumnWidthDialog(this, fDocument, fPages);
			dialog->Show();
		}
		return;
	}
	if (message->what == kCmdRowHeight) {
		if (fPages != NULL) {
			TRowHeightDialog* dialog = new TRowHeightDialog(this, fDocument, fPages);
			dialog->Show();
		}
		return;
	}
	if (message->what == kCmdGoToCell) {
		if (fPages != NULL) {
			TGoToCellDialog* dialog = new TGoToCellDialog(this, fDocument, fPages);
			dialog->Show();
		}
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
			char* argv[1] = {const_cast<char*>(url)};
			be_roster->Launch(strncmp(url, "mailto:", 7) == 0 ? "text/x-email" : "text/html", 1,
				argv);
		}
		return;
	}
	// Writing Tools > Thesaurus… and Dictionary… (TDocWindow: the active
	// part's SelectWord, then TThesaurus::GUI, TDictionary::GUI): the word
	// at the caret selected; the data file is the first of STR# 38 (37)
	// there ("~etc": the system's etc folder); none: STR# 6's alert with
	// the last. (The dialogs need the BeOS goodies' files; not ported.)
	if (message->what == 'sM27' || message->what == 'sM28') {
		if (fPages != NULL && fPages->fCaret.active && fDocument != NULL) {
			if (const WPPart* wp = fDocument->WordPart(fPages->fCaret.part)) {
				TextPosition from, to;
				if (!fPages->TextSelection(from, to))
					from = {fPages->fCaret.paragraph, fPages->fCaret.offset};
				size_t start, end;
				WordBounds(wp->paragraphs[from.paragraph].text, from.offset, true, start, end);
				if (start != end)
					fPages->SelectText(fPages->fCaret.part, {from.paragraph, start}, end);
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
			BString text;
			text.SetToFormat(GobeResources::IndexString(6, 0).String(), path.String());
			(new BAlert("", text.String(), "OK"))->Go(NULL);
		} else if (fStatus != NULL)
			fStatus->SetText("The thesaurus and dictionary dialogs are not ported yet");
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
	fModified = true;
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
	RecentDocs::Add(path.c_str());
	if (fPages != NULL) {
		fPages->DocumentSaved();
		fPages->DocumentReloaded();
	}
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
		BMenuItem* found = NULL;
		for (int32 i = 0; i < fSizeMenu->CountItems(); i++) {
			BMenuItem* item = fSizeMenu->ItemAt(i);
			BMessage* msg = item->Message();
			if (msg != NULL) {
				int32 val = 0;
				if (msg->FindInt32(kValueString, &val) == B_OK
					&& val == int32(textSize)) {
					item->SetMarked(true);
					found = item;
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
void
TDocWindow::MenusBeginning()
{
	BWindow::MenusBeginning();
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
			: ribbon.command == 'sMF4' ? (BView*)fFormatBar : NULL;
		bool shown = bar != NULL && !bar->IsHidden();
		item->SetLabel(GobeResources::IndexString(1, ribbon.string + (shown ? 0 : 1)).String());
		item->SetEnabled(bar != NULL);
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
		enable('sM02', fModified);
		enable('sM04', fModified && saved);
		enable('sM13', false);
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
		for (uint32 command : {'sM30', 'sM32', 'sM33', 'sM34', 'sM35', 'sM36'})
			enable(command, false);
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
	// (New Window: a second window on a document isn't ported)
	if (BMenuItem* item = KeyMenuBar()->FindItem('sMF0'))
		item->SetEnabled(false);
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
	if (fModified && fDocument != NULL) {
		BString text = GobeResources::IndexString(2, 13);
		text << fDocument->name.c_str() << GobeResources::IndexString(2, fAppQuitting ? 14 : 15);
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
	}
}


// TApp::FrontDocWindow
TDocWindow*
TDocWindow::Front()
{
	return sWindows.empty() ? NULL : sWindows.front();
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

} // namespace gobe
