#include "TDocWindow.h"

#include <stdio.h>

#include <Alert.h>
#include <Application.h>
#include <Messenger.h>
#include <PrintJob.h>
#include <Button.h>
#include <CheckBox.h>
#include <ColorControl.h>
#include <FilePanel.h>
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
#include "TDocView.h"
#include "TPageView.h"
#include "TTranslate.h"
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

// Tab menu commands (MENU 120)
static const uint32 kCmdTabLeft = 3;
static const uint32 kCmdTabCenter = 4;
static const uint32 kCmdTabRight = 5;
static const uint32 kCmdTabAlign = 6;
static const uint32 kCmdDeleteTab = 8;
static const uint32 kCmdEditTab = 9;

// Format menu insert commands
static const uint32 kCmdInsertDate = 'sM53';
static const uint32 kCmdInsertTime = 'sM54';

// Find/Replace dialog controls (DIAL 7, Item 7)
static const uint32 kFindBtn = 6;
static const uint32 kReplaceBtn = 7;
static const uint32 kReplaceFindBtn = 8;
static const uint32 kReplaceAllBtn = 9;

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


// #pragma mark - TFindDialog

TFindDialog::TFindDialog(int32 dialogId, int32 itemsId, uint32 command,
	BHandler* target, Document* doc, TPageView* view)
	:
	TDialog(dialogId, itemsId, command, target),
	fDocument(doc),
	fView(view)
{
}


TFindDialog::~TFindDialog()
{
}


void
TFindDialog::SetFindText(const char* text)
{
	fFindText = text;
}


void
TFindDialog::SetReplaceText(const char* text)
{
	fReplaceText = text;
}


void
TFindDialog::SetCaseSensitive(bool on)
{
	fCaseSensitive = on;
}


void
TFindDialog::SetEntireWord(bool on)
{
	fEntireWord = on;
}


void
TFindDialog::SetWrap(bool on)
{
	fWrap = on;
}


void
TFindDialog::ReadControls()
{
	// Recursively walk all descendant views to find BTextControl and BCheckBox
	struct ControlFinder {
		int32 editCount = 0;

		void Visit(BView* view, TFindDialog* dialog) {
			if (view == NULL) return;
			BTextControl* edit = dynamic_cast<BTextControl*>(view);
			if (edit != NULL) {
				if (editCount == 0)
					dialog->fFindText = edit->Text();
				else if (editCount == 1)
					dialog->fReplaceText = edit->Text();
				editCount++;
				return;
			}
			BCheckBox* box = dynamic_cast<BCheckBox*>(view);
			if (box != NULL) {
				switch (editCount) {
					case 2:
						dialog->fCaseSensitive = !box->Value();
						break;
					case 3:
						dialog->fEntireWord = box->Value() != 0;
						break;
					case 4:
						dialog->fWrap = box->Value() != 0;
						break;
				}
				editCount++;
				return;
			}
			// Recurse into children
			int32 count = view->CountChildren();
			for (int32 i = 0; i < count; i++)
				Visit(view->ChildAt(i), dialog);
		}
	};

	ControlFinder finder;
	// Start from this (the TFindDialog window), cast through BWindow
	finder.Visit(ChildAt(0), this);
}


void
TFindDialog::DoFind()
{
	ReadControls();
	if (fView == NULL || fDocument == NULL || fFindText.empty())
		return;

	FindDialogOptions opts;
	opts.caseSensitive = fCaseSensitive;
	opts.wholeWord = fEntireWord;
	opts.backwards = false;
	opts.wrap = fWrap;

	const WPPart* wp = fDocument->WordPart(fView->fCaret.part);
	if (wp == NULL) {
		fprintf(stderr, "find: no word part\n");
		return;
	}
	TextPosition from = {fView->fCaret.paragraph, fView->fCaret.offset};
	TextPosition start, endPos;
	if (FindText(*wp, from, fFindText, opts, start, endPos)) {
		fView->fAnchor.active = true;
		fView->fAnchor.part = fView->fCaret.part;
		fView->fAnchor.paragraph = start.paragraph;
		fView->fAnchor.offset = start.offset;
		fView->fCaret.active = true;
		fView->fCaret.part = fView->fCaret.part;
		fView->fCaret.paragraph = start.paragraph;
		fView->fCaret.offset = endPos.offset;
		fView->Invalidate();
	} else {
		fprintf(stderr, "find: not found\n");
	}
}


void
TFindDialog::DoReplace()
{
	ReadControls();
	if (fView == NULL || fDocument == NULL || fFindText.empty())
		return;

	// Check if the current selection matches
	TextPosition selFrom, selTo;
	if (!fView->TextSelection(selFrom, selTo)) {
		fprintf(stderr, "replace: no selection\n");
		return;
	}

	const WPPart* wp = fDocument->WordPart(fView->fCaret.part);
	if (wp == NULL) {
		fprintf(stderr, "replace: no word part\n");
		return;
	}

	std::string selected = RangeText(*wp, selFrom, selTo);

	// Check if it matches (considering case sensitivity)
	bool match = false;
	if (selected.size() == fFindText.size()) {
		if (fCaseSensitive) {
			match = (selected == fFindText);
		} else {
			match = true;
			for (size_t i = 0; i < selected.size(); i++) {
				if (FoldCase(selected[i]) != FoldCase(fFindText[i])) {
					match = false;
					break;
				}
			}
		}
	}

	if (!match) {
		fprintf(stderr, "replace: selection doesn't match\n");
		return;
	}

	// Replace the selection
	fView->fUndo.Begin(*fDocument, fView->fCaret.part,
		"Replace", fView->CurrentViewState());
	WPPart* editWP = fDocument->EditableWordPart(fView->fCaret.part);
	if (editWP) {
		TextPosition result;
		DeleteRange(*editWP, selFrom, selTo, result);
		InsertText(*editWP, result, fReplaceText);
		// Select the replacement
		fView->fAnchor.active = true;
		fView->fAnchor.part = result.paragraph;
		fView->fAnchor.paragraph = result.paragraph;
		fView->fAnchor.offset = result.offset;
		fView->fCaret.active = true;
		fView->fCaret.part = result.paragraph;
		fView->fCaret.paragraph = result.paragraph;
		fView->fCaret.offset = result.offset + fReplaceText.size();
		fView->TextChanged();
	}
}


void
TFindDialog::DoReplaceAndFind()
{
	DoReplace();
	DoFind();
}


void
TFindDialog::DoReplaceAll()
{
	ReadControls();
	if (fView == NULL || fDocument == NULL || fFindText.empty())
		return;

	WPPart* wp = fDocument->EditableWordPart(fView->fCaret.part);
	if (wp == NULL) {
		fprintf(stderr, "replaceall: no word part\n");
		return;
	}

	FindDialogOptions opts;
	opts.caseSensitive = fCaseSensitive;
	opts.wholeWord = fEntireWord;
	opts.wrap = fWrap;

	fView->fUndo.Begin(*fDocument, fView->fCaret.part, "Replace All",
		fView->CurrentViewState());
	int count = ReplaceAll(*wp, fFindText, fReplaceText, opts);
	fView->TextChanged();
	fprintf(stderr, "replaceall: %d replacements\n", count);
}


void
TFindDialog::MessageReceived(BMessage* message)
{
	// Dialog-specific command handling
	if (message->HasInt32("dialog")) {
		int32 index = message->GetInt32("index", -1);

		switch (index) {
			case kFindBtn:
				DoFind();
				break;
			case kReplaceBtn:
				DoReplace();
				break;
			case kReplaceFindBtn:
				DoReplaceAndFind();
				break;
			case kReplaceAllBtn:
				DoReplaceAll();
				break;
			default:
				PostMessage(B_QUIT_REQUESTED);
				break;
		}
		return;
	}
	TDialog::MessageReceived(message);
}

// #pragma mark - TFontDialog

TFontDialog::TFontDialog(Document* doc, TPageView* view)
	:
	BWindow(BRect(100, 100, 370, 340), "Font",
		B_TITLED_WINDOW_LOOK, B_NORMAL_WINDOW_FEEL, B_NOT_RESIZABLE
		| B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS),
	fDocument(doc),
	fView(view)
{
	BView* top = new BView("top", B_WILL_DRAW);
	top->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	SetLayout(new BGroupLayout(B_VERTICAL, 0));
	GetLayout()->AddView(top);

	// Get the style at the selection start for prefilling
	TextStyle style;
	if (fView != NULL && fView->fCaret.active && fDocument != NULL) {
		style = fView->StyleAt({fView->fCaret.paragraph, fView->fCaret.offset});
	}

	// Build layout manually (older Haiku: BLayoutBuilder::End() returns void*)
	BGroupLayout* outerLayout = new BGroupLayout(B_VERTICAL, B_USE_SMALL_SPACING);
	top->SetLayout(outerLayout);

	BGroupLayout* familyLayout = new BGroupLayout(B_HORIZONTAL, B_USE_SMALL_SPACING);
	outerLayout->AddView(new BStringView("family_label", "Font family:"));
	outerLayout->AddView(fFamily = new BTextControl("family", NULL, style.family.c_str(), NULL));

	BGroupLayout* sizeLayout = new BGroupLayout(B_HORIZONTAL, B_USE_SMALL_SPACING);
	outerLayout->AddView(new BStringView("size_label", "Size:"));
	outerLayout->AddView(fSize = new BTextControl("size", NULL, "", NULL));

	BGroupLayout* faceLayout = new BGroupLayout(B_HORIZONTAL, B_USE_SMALL_INSETS);
	outerLayout->AddView(fBold = new BCheckBox("bold", "Bold", NULL));
	outerLayout->AddView(fItalic = new BCheckBox("italic", "Italic", NULL));
	outerLayout->AddView(fUnderline = new BCheckBox("underline", "Underline", NULL));

	outerLayout->AddView(fColor = new BColorControl(
		B_ORIGIN, B_CELLS_32x8, 8, "color",
		new BMessage('kColor')));

	BGroupLayout* btnLayout = new BGroupLayout(B_HORIZONTAL, B_USE_DEFAULT_SPACING);
	BView* spacer = new BView(NULL, B_WILL_DRAW);
	spacer->SetExplicitSize(BSize(9999, B_SIZE_UNSET));
	outerLayout->AddView(spacer);
	outerLayout->AddView(new BButton("ok", "OK", new BMessage(kMsgFontOK)));
	outerLayout->AddView(new BButton("cancel", "Cancel", new BMessage(B_QUIT_REQUESTED)));

	// Pre-fill with current values
	char buf[32];
	snprintf(buf, sizeof buf, "%.0f", style.size);
	fSize->SetText(buf);
	fSize->TextView()->SelectAll();

	fBold->SetValue(style.faceFlags & 2 ? B_CONTROL_ON : B_CONTROL_OFF);
	fItalic->SetValue(style.faceFlags & 4 ? B_CONTROL_ON : B_CONTROL_OFF);
	fUnderline->SetValue((style.faceFlags & 8) ? B_CONTROL_ON : B_CONTROL_OFF);

	rgb_color c = { style.color.r, style.color.g, style.color.b, 255 };
	fColor->SetValue(c);

	CenterOnScreen();
}


void
TFontDialog::DoApply()
{
	if (fView == NULL || fDocument == NULL)
		return;

	if (!fView->fCaret.active) {
		beep();
		return;
	}

	fView->fUndo.Begin(*fDocument, fView->fCaret.part, "Font",
		fView->CurrentViewState());

	auto sel = fView->TextSelectionRange();

	// Family
	const char* family = fFamily->Text();
	if (family != NULL && strlen(family) > 0) {
		ApplyCharacterProperty(*fDocument, fView->fCaret.part,
			sel.first, sel.second,
			TextFontProperty(family, ""));
	}

	// Size
	float size = strtof(fSize->Text(), NULL);
	if (size > 0) {
		ApplyCharacterProperty(*fDocument, fView->fCaret.part,
			sel.first, sel.second,
			TextSizeProperty(size));
	}

	// Face flags: compute from checkboxes
	uint32_t flags = 1;  // plain text base
	if (fBold->Value() == B_CONTROL_ON)
		flags |= 2;
	if (fItalic->Value() == B_CONTROL_ON)
		flags |= 4;
	if (fUnderline->Value() == B_CONTROL_ON)
		flags |= 8;
	ApplyCharacterProperty(*fDocument, fView->fCaret.part,
		sel.first, sel.second,
		TextFaceProperty(flags));

	// Color (BColorControl stores inverse; ValueAsColor returns inverse)
	rgb_color c = fColor->ValueAsColor();
	Color color{c.red, c.green, c.blue, 255};
	ApplyCharacterProperty(*fDocument, fView->fCaret.part,
		sel.first, sel.second,
		TextColorProperty(color));

	fView->TextChanged();
	PostMessage(B_QUIT_REQUESTED);
}


void
TFontDialog::MessageReceived(BMessage* message)
{
	if (message->what == kMsgFontOK) {
		DoApply();
		return;
	}
	BWindow::MessageReceived(message);
}


bool
TFontDialog::QuitRequested()
{
	delete this;
	return true;
}

// #pragma mark - TParagraphDialog

TParagraphDialog::TParagraphDialog(Document* doc, TPageView* view)
	:
	BWindow(BRect(100, 100, 350, 420), "Paragraph",
		B_TITLED_WINDOW_LOOK, B_NORMAL_WINDOW_FEEL, B_NOT_RESIZABLE
		| B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS),
	fDocument(doc),
	fView(view)
{
	BView* top = new BView("top", B_WILL_DRAW);
	top->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	SetLayout(new BGroupLayout(B_VERTICAL, 0));
	GetLayout()->AddView(top);

	// Get the style at the caret's paragraph for prefilling
	TextStyle style;
	if (fView != NULL && fView->fCaret.active && fDocument != NULL) {
		style = fView->StyleAt({fView->fCaret.paragraph, fView->fCaret.offset});
	}

	// Alignment popup: MENU 300's items, the value is the PAlg value
	// (TDialogFormatParagraph::initIndentSet: FindValue(alignment))
	BPopUpMenu* alignMenu = new BPopUpMenu("alignment");
	static const char* kAlignLabels[] = { "Left", "Center", "Right", "Justify" };
	for (int32 i = 0; i < 4; i++) {
		BMessage* m = new BMessage('PAlg');
		m->AddInt32("value", i);
		alignMenu->AddItem(new BMenuItem(kAlignLabels[i], m));
	}
	alignMenu->SetLabelFromMarked(true);

	// Build layout manually (older Haiku: BLayoutBuilder::End() returns void*)
	BGroupLayout* outerLayout = new BGroupLayout(B_VERTICAL, B_USE_SMALL_SPACING);
	top->SetLayout(outerLayout);

	BGroupLayout* alignLayout = new BGroupLayout(B_HORIZONTAL, B_USE_SMALL_SPACING);
	outerLayout->AddView(new BStringView("align_label", "Alignment:"));
	outerLayout->AddView(fAlignmentField = new BMenuField("alignment", "", alignMenu));

	BGroupLayout* leftLayout = new BGroupLayout(B_HORIZONTAL, B_USE_SMALL_SPACING);
	outerLayout->AddView(new BStringView("left_label", "Left indent:"));
	outerLayout->AddView(fLeftIndent = new BTextControl("left_indent", NULL, "", NULL));

	BGroupLayout* firstLayout = new BGroupLayout(B_HORIZONTAL, B_USE_SMALL_SPACING);
	outerLayout->AddView(new BStringView("first_label", "First indent:"));
	outerLayout->AddView(fFirstIndent = new BTextControl("first_indent", NULL, "", NULL));

	BGroupLayout* rightLayout = new BGroupLayout(B_HORIZONTAL, B_USE_SMALL_SPACING);
	outerLayout->AddView(new BStringView("right_label", "Right indent:"));
	outerLayout->AddView(fRightIndent = new BTextControl("right_indent", NULL, "", NULL));

	BGroupLayout* beforeLayout = new BGroupLayout(B_HORIZONTAL, B_USE_SMALL_SPACING);
	outerLayout->AddView(new BStringView("before_label", "Space before:"));
	outerLayout->AddView(fSpaceBefore = new BTextControl("space_before", NULL, "", NULL));

	BGroupLayout* afterLayout = new BGroupLayout(B_HORIZONTAL, B_USE_SMALL_SPACING);
	outerLayout->AddView(new BStringView("after_label", "Space after:"));
	outerLayout->AddView(fSpaceAfter = new BTextControl("space_after", NULL, "", NULL));

	BGroupLayout* lineLayout = new BGroupLayout(B_HORIZONTAL, B_USE_SMALL_SPACING);
	outerLayout->AddView(new BStringView("linespacing_label", "Line spacing:"));
	outerLayout->AddView(fLineSpacing = new BTextControl("line_spacing", NULL, "", NULL));

	BGroupLayout* btnLayout = new BGroupLayout(B_HORIZONTAL, B_USE_DEFAULT_SPACING);
	BView* spacer = new BView(NULL, B_WILL_DRAW);
	spacer->SetExplicitSize(BSize(9999, B_SIZE_UNSET));
	outerLayout->AddView(spacer);
	outerLayout->AddView(new BButton("ok", "OK", new BMessage(kMsgParagraphOK)));
	outerLayout->AddView(new BButton("cancel", "Cancel", new BMessage(B_QUIT_REQUESTED)));

	// Pre-fill with current values
	char buf[32];

	snprintf(buf, sizeof buf, "%.1f", style.leftIndent);
	fLeftIndent->SetText(buf);
	snprintf(buf, sizeof buf, "%.1f", style.firstIndent);
	fFirstIndent->SetText(buf);
	snprintf(buf, sizeof buf, "%.1f", style.rightIndent);
	fRightIndent->SetText(buf);
	// TParSpaceProperty::GetDataStr: "<value> lines", or points
	snprintf(buf, sizeof buf, style.spaceBeforeLines ? "%g lines" : "%g pt", style.spaceBefore);
	fSpaceBefore->SetText(buf);
	snprintf(buf, sizeof buf, style.spaceAfterLines ? "%g lines" : "%g pt", style.spaceAfter);
	fSpaceAfter->SetText(buf);

	if (style.lineSpacingMultiple) {
		snprintf(buf, sizeof buf, "%.1f", style.lineSpacing);
	} else {
		snprintf(buf, sizeof buf, "%.1f pt", style.lineSpacing);
	}
	fLineSpacing->SetText(buf);

	// Mark the alignment item
	for (int32_t i = 0; i < alignMenu->CountItems(); i++) {
		BMenuItem* item = alignMenu->ItemAt(i);
		int32_t val = 0;
		if (item->Message() != NULL &&
			item->Message()->FindInt32("value", &val) == B_OK) {
			if (val == style.alignment) {
				item->SetMarked(true);
				break;
			}
		}
	}

	CenterOnScreen();
}


void
TParagraphDialog::DoApply()
{
	if (fView == NULL || fDocument == NULL)
		return;

	if (!fView->fCaret.active) {
		beep();
		return;
	}

	// Determine the paragraph range
	int32_t firstPar = fView->fCaret.paragraph;
	int32_t lastPar = fView->fCaret.paragraph;

	if (fView->fAnchor.active && fView->fAnchor.part == fView->fCaret.part) {
		int32_t aPar = std::min(fView->fCaret.paragraph, fView->fAnchor.paragraph);
		int32_t bPar = std::max(fView->fCaret.paragraph, fView->fAnchor.paragraph);
		firstPar = aPar;
		lastPar = bPar;
	}

	fView->fUndo.Begin(*fDocument, fView->fCaret.part, "Paragraph",
		fView->CurrentViewState());

	// Alignment
	if (fAlignmentField != NULL) {
		BMenuItem* marked = fAlignmentField->Menu()->FindMarked();
		if (marked != NULL && marked->Message() != NULL) {
			int32_t align = 0;
			if (marked->Message()->FindInt32("value", &align) == B_OK) {
				ApplyParagraphProperty(*fDocument, fView->fCaret.part,
					firstPar, lastPar,
					AlignmentProperty(align));
			}
		}
	}

	// Indents
	char* end;
	float leftIndent = strtof(fLeftIndent->Text(), &end);
	if (end != fLeftIndent->Text())
		ApplyParagraphProperty(*fDocument, fView->fCaret.part,
			firstPar, lastPar,
			IndentProperty(Sig("PLef"), leftIndent));

	float firstIndent = strtof(fFirstIndent->Text(), &end);
	if (end != fFirstIndent->Text())
		ApplyParagraphProperty(*fDocument, fView->fCaret.part,
			firstPar, lastPar,
			IndentProperty(Sig("PInd"), firstIndent));

	float rightIndent = strtof(fRightIndent->Text(), &end);
	if (end != fRightIndent->Text())
		ApplyParagraphProperty(*fDocument, fView->fCaret.part,
			firstPar, lastPar,
			IndentProperty(Sig("PRig"), rightIndent));

	// Spacing: lines unless given in points ("pt")
	float spaceBefore = strtof(fSpaceBefore->Text(), &end);
	if (end != fSpaceBefore->Text())
		ApplyParagraphProperty(*fDocument, fView->fCaret.part,
			firstPar, lastPar,
			SpacingProperty(Sig("SpaB"), spaceBefore, strstr(end, "pt") == NULL));

	float spaceAfter = strtof(fSpaceAfter->Text(), &end);
	if (end != fSpaceAfter->Text())
		ApplyParagraphProperty(*fDocument, fView->fCaret.part,
			firstPar, lastPar,
			SpacingProperty(Sig("SpaA"), spaceAfter, strstr(end, "pt") == NULL));

	// Line spacing
	std::string ls = fLineSpacing->Text();
	float lineSpacing = strtof(ls.c_str(), &end);
	if (end != ls.c_str()) {
		bool multiple = true;
		if (ls.find("pt") != std::string::npos)
			multiple = false;
		ApplyParagraphProperty(*fDocument, fView->fCaret.part,
			firstPar, lastPar,
			LineSpacingProperty(lineSpacing, multiple));
	}

	fView->TextChanged();
	PostMessage(B_QUIT_REQUESTED);
}


void
TParagraphDialog::MessageReceived(BMessage* message)
{
	if (message->what == kMsgParagraphOK) {
		DoApply();
		return;
	}
	BWindow::MessageReceived(message);
}


bool
TParagraphDialog::QuitRequested()
{
	delete this;
	return true;
}

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

	// Text menus (from the original's TTextMenuElement):
	// Font (dynamic), Size (MENU 101), Face (MENU 102), Alignment (MENU 300)
	bar->AddItem(MakeFontMenu());
	fSizeMenu = new TMenu(101);
	bar->AddItem(fSizeMenu);
	fFaceMenu = new TMenu(102);
	bar->AddItem(fFaceMenu);
	fAlignmentMenu = new TMenu(300);
	bar->AddItem(fAlignmentMenu);

	// Tab menu (MENU 120)
	BMenu* tabMenu = new BMenu("Tabs");
	tabMenu->AddItem(new BMenuItem("Left Tab", new BMessage(kCmdTabLeft), 'l'));
	tabMenu->AddItem(new BMenuItem("Center Tab", new BMessage(kCmdTabCenter), 'c'));
	tabMenu->AddItem(new BMenuItem("Right Tab", new BMessage(kCmdTabRight), 'r'));
	tabMenu->AddItem(new BMenuItem("Align Tab", new BMessage(kCmdTabAlign)));
	tabMenu->AddItem(new BSeparatorItem());
	tabMenu->AddItem(new BMenuItem("Delete Tab", new BMessage(kCmdDeleteTab), 'D'));
	tabMenu->AddItem(new BMenuItem("Edit Tab…", new BMessage(kCmdEditTab)));
	bar->AddItem(tabMenu);

	// the Spreadsheet part's Format menu (MENU 52 of Parts/Spreadsheet)
	// and Options menu (MENU 53), with the items that work so far
	if (fDocument != NULL && !fDocument->spreadsheetParts.empty()) {
		// Format menu: Column Width, Row Height, Display Options
		BMenu* format = new BMenu("Format");
		format->AddItem(new BMenuItem("Column Width…", new BMessage(kCmdColWidth), 'W'));
		format->AddItem(new BMenuItem("Row Height…", new BMessage(kCmdRowHeight), 'H'));
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
		options->AddItem(new BMenuItem("Go To Cell…", new BMessage(kCmdGoToCell), 'G'));
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
		case kCmdInsertHeader:
		case kCmdInsertFooter:
			if (fPages != NULL)
				fPages->ToggleHeaderFooter(message->what == kCmdInsertFooter ? 1 : 0);
			return;
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
		{
			Document* document = new Document;
			std::string error;
			if (CreateBlankDocument(*document, error)) {
				TDocWindow* window = new TDocWindow(Frame().OffsetByCopy(20, 20),
					document->name.c_str(), document);
				window->Show();
			} else {
				fprintf(stderr, "kCmdNew: %s\n", error.c_str());
				delete document;
			}
			return;
		}
		case kCmdOpen:
			be_app->PostMessage(kCmdOpen);
			return;
		case kCmdQuit:
			be_app->PostMessage(B_QUIT_REQUESTED);
			return;
	}

	// Forward cell format/fill/wrap alignment commands to the page view
	if (fPages != NULL) {
		uint32 what = message->what;
		if (what == 'sM20' || what == 'sM21' || what == 'sM22' || what == 'sM23'
			|| what == 'sM24' || what == 'sM52' || what == 'sM56' || what == 'sM57'
			|| what == 'sM58' || what == 'sM59') {
			fPages->MessageReceived(message);
			return;
		}
	}

	if (message->what == kMsgOpenDialog) {
		int32 id = message->GetInt32("id", 0);
		if (id == 7) {
			// Find/Replace dialog: use TFindDialog
			if (fPages != NULL) {
				TFindDialog* dialog = new TFindDialog(id, id, kDialogCommand, this,
					fDocument, fPages);
				dialog->Show();
			}
			return;
		}
		// most other dialogs use the same id for DIAL and Item
		TDialog* dialog = new TDialog(id, id, kDialogCommand, this);
		dialog->Show();
		return;
	}

	// Handle Find/Replace menu commands (MENU 7)
	if (message->what == kCmdFindReplace) {
		if (fPages != NULL) {
			TFindDialog* dialog = new TFindDialog(7, 7, kDialogCommand, this,
				fDocument, fPages);
			dialog->Show();
		}
		return;
	}
	if (message->what == kCmdFindSelection) {
		if (fPages != NULL) {
			// Pre-fill the find field with current text selection
			TextPosition from, to;
			if (fPages->TextSelection(from, to)) {
				const WPPart* wp = fDocument->WordPart(fPages->fCaret.part);
				if (wp != NULL) {
					std::string sel = RangeText(*wp, from, to);
					TFindDialog* dialog = new TFindDialog(7, 7, kDialogCommand, this,
						fDocument, fPages);
					dialog->SetFindText(sel.c_str());
					dialog->Show();
				}
			}
		}
		return;
	}
	if (message->what == kCmdReplaceAll) {
		if (fPages != NULL && fDocument != NULL) {
			// Pre-fill find text from current selection if any
			TextPosition from, to;
			std::string prefill;
			if (fPages->TextSelection(from, to)) {
				const WPPart* wp = fDocument->WordPart(fPages->fCaret.part);
				if (wp != NULL)
					prefill = RangeText(*wp, from, to);
			}
			TFindDialog* dialog = new TFindDialog(7, 7, kDialogCommand, this,
				fDocument, fPages);
			if (!prefill.empty())
				dialog->SetFindText(prefill.c_str());
			dialog->Show();
		}
		return;
	}
	// sME1 (Find Again) and sME3 (Replace & Find Again) — open dialog
	if (message->what == kCmdFindAgain || message->what == kCmdReplaceFindAgain) {
		if (fPages != NULL) {
			TFindDialog* dialog = new TFindDialog(7, 7, kDialogCommand, this,
				fDocument, fPages);
			dialog->Show();
		}
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
	if (message->what == 'sM50') {
		// Paragraph dialog from the Format menu
		if (fPages != NULL) {
			TParagraphDialog* dialog = new TParagraphDialog(fDocument, fPages);
			dialog->Show();
		}
		return;
	}
	// Insert Date… (sM53) — insert current date
	if (message->what == kCmdInsertDate) {
		if (fPages != NULL) {
			fPages->fCaret = fPages->fCaret;  // ensure caret is valid
			// Use the same pattern as the RunScript: get the date and insert it
			time_t now = time(NULL);
			struct tm* tm_now = localtime(&now);
			char dateStr[32];
			strftime(dateStr, sizeof(dateStr), "%m/%d/%Y", tm_now);

			WPPart* wp = fDocument->EditableWordPart(fPages->fCaret.part);
			if (wp != NULL && fPages->fCaret.active) {
				fPages->fUndo.Begin(*fDocument, fPages->fCaret.part, "Date",
					fPages->CurrentViewState());
				InsertText(*wp, {fPages->fCaret.paragraph, fPages->fCaret.offset}, dateStr);
				fPages->fCaret.offset += strlen(dateStr);
				fPages->TextChanged();
			}
		}
		return;
	}
	// Insert Time… (sM54) — insert current time
	if (message->what == kCmdInsertTime) {
		if (fPages != NULL) {
			time_t now = time(NULL);
			struct tm* tm_now = localtime(&now);
			char timeStr[32];
			strftime(timeStr, sizeof(timeStr), "%l:%M %p", tm_now);

			WPPart* wp = fDocument->EditableWordPart(fPages->fCaret.part);
			if (wp != NULL && fPages->fCaret.active) {
				fPages->fUndo.Begin(*fDocument, fPages->fCaret.part, "Time",
					fPages->CurrentViewState());
				InsertText(*wp, {fPages->fCaret.paragraph, fPages->fCaret.offset}, timeStr);
				fPages->fCaret.offset += strlen(timeStr);
				fPages->TextChanged();
			}
		}
		return;
	}
	// Tab menu: add tab of given type at caret position
	if (message->what == kCmdTabLeft || message->what == kCmdTabCenter
		|| message->what == kCmdTabRight || message->what == kCmdTabAlign) {
		if (fPages != NULL && fPages->fCaret.active) {
			// Get the x position of the caret in points
			float xPoints = fPages->CaretX();

			int type = 0;
			if (message->what == kCmdTabCenter) type = 1;
			else if (message->what == kCmdTabRight) type = 2;
			else if (message->what == kCmdTabAlign) type = 3;

			TabStop tab;
			tab.position = xPoints;
			tab.type = type;
			tab.fill = 0;
			tab.alignChar = '.';
			tab.fillChar = '.';
			// one PTab per tab; it replaces a tab at the same position
			fPages->fUndo.Begin(*fDocument, fPages->fCaret.part, "Tab",
				fPages->CurrentViewState());
			ApplyParagraphProperty(*fDocument, fPages->fCaret.part,
				fPages->fCaret.paragraph, fPages->fCaret.paragraph,
				TabProperty(tab));
			fPages->TextChanged();
		}
		return;
	}
	// Delete Tab — remove nearest tab to caret's x position
	if (message->what == kCmdDeleteTab) {
		if (fPages != NULL && fPages->fCaret.active) {
			float xPoints = fPages->CaretX();

			WPPart* wp = fDocument->EditableWordPart(fPages->fCaret.part);
			if (wp == NULL) return;
			const WPParagraph& par = wp->paragraphs[fPages->fCaret.paragraph];
			uint32_t styleIdx = par.parStyle;
			TextStyle style = ResolveTextStyle(fDocument->styles, styleIdx);

			size_t nearestIdx = SIZE_MAX;
			float nearestDist = 1e18f;
			for (size_t i = 0; i < style.tabs.size(); i++) {
				float dist = fabsf(style.tabs[i].position - xPoints);
				if (dist < nearestDist) {
					nearestDist = dist;
					nearestIdx = i;
				}
			}
			if (nearestIdx == SIZE_MAX || nearestIdx >= style.tabs.size())
				return;
			fPages->fUndo.Begin(*fDocument, fPages->fCaret.part, "Tab",
				fPages->CurrentViewState());
			RemoveParagraphTab(*fDocument, fPages->fCaret.part,
				fPages->fCaret.paragraph, fPages->fCaret.paragraph,
				style.tabs[nearestIdx].position);
			fPages->TextChanged();
		}
		return;
	}
	// Edit Tab… — placeholder (not implemented yet)
	if (message->what == kCmdEditTab) {
		fStatus->SetText("Edit Tab… not implemented yet");
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
