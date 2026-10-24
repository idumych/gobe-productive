// TPageView editing: a caret in word processing text (click, type,
// Backspace/Delete, Enter, arrows, Home/End, selection, and clipboard
// operations) and spreadsheet cells (click to select, double-click/Enter/
// typing to edit in place, Enter/Tab to commit, Escape to cancel).
// Spreadsheet range selection: shift-click, shift+arrows, mouse drag extend
// the range; copy/cut/paste/clear/selectall work on ranges using
// RangeInput/ClearRange/PasteInput from Editing.h.
#include "TPageView.h"
#include "TFormatBar.h"
#include "TPartBar.h"
#include "TSpellBar.h"
#include "Url.h"
#include "Find.h"
#include "TPreferences.h"
#include "TDocumentDialogs.h"
#include "TWPRuler.h"

#include <Alert.h>
#include <Beep.h>
#include <Clipboard.h>
#include <MenuItem.h>
#include <MessageRunner.h>
#include <Path.h>
#include <PopUpMenu.h>
#include <ScrollBar.h>
#include <Window.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>

#include "Editing.h"
#include "StyleEdit.h"
#include "Fields.h"
#include "Notes.h"
#include "GobeResources.h"
#include "TDocView.h"
#include "TTranslate.h"
#include "TWPDialogs.h"
#include "TStylesEdit.h"
#include "SSStyle.h"
#include "SSEval.h"
#include "TDocWindow.h"
#include "TTranslate.h"

namespace gobe {

// Copy Format's properties (the application's, TApp +0x184)
static std::vector<Property> sCopiedFormat;
// the text last copied with its styles (the clipboard has its plain text)
static TextFragment sCopiedText;

static const uint32 kMsgCellCommit = 'Gcel';
static const uint32 kMsgCellCancel = 'Gccl';
// columns: not in the original's menus (its Insert/Delete Cells dialog
// chooses); for RunScript until that dialog works
static const uint32 kCmdInsertColumns = 'Gicl';
static const uint32 kCmdDeleteColumns = 'Gdcl';

// the port's stand-in cell format commands (the Spreadsheet add-on's
// menus aren't ported; private codes: the original's 'sM20'..'sM24' are
// the Document menu's)
static const uint32 kCmdCellHAlign = 'GcHA';
static const uint32 kCmdCellVAlign = 'GcVA';
static const uint32 kCmdCellFormat = 'GcNF';
static const uint32 kCmdCellFill = 'GcFL';
static const uint32 kCmdCellWrap = 'GcWR';


// the in-place cell editor
class TCellEditor : public BTextView {
public:
	TCellEditor(BRect frame)
		:
		BTextView(frame, "cell editor", frame.OffsetToCopy(B_ORIGIN).InsetByCopy(2, 1),
			B_FOLLOW_NONE, B_WILL_DRAW)
	{
		SetWordWrap(false);
	}

	virtual void KeyDown(const char* bytes, int32 count)
	{
		BMessage message(kMsgCellCommit);
		switch (bytes[0]) {
			case B_ENTER:
				message.AddInt32("rows", 1);
				break;
			case B_TAB:
				message.AddInt32("columns", (modifiers() & B_SHIFT_KEY) ? -1 : 1);
				break;
			case B_ESCAPE:
				message.what = kMsgCellCancel;
				break;
			default:
				BTextView::KeyDown(bytes, count);
				return;
		}
		Window()->PostMessage(&message, Parent());
	}
};


// #pragma mark - text

namespace {

float
SubstringWidth(const BFont& font, const std::string& text, size_t from, size_t to)
{
	std::string s;
	for (size_t i = from; i < to && i < text.size(); i++)
		if ((unsigned char)text[i] >= 0x20)
			s += text[i];
	return TDocView::TextWidth(font, s);
}

} // namespace


float
TPageView::OffsetX(size_t lineIndex, size_t offset) const
{
	const Line& line = fLines[lineIndex];
	const WPPart* wp = fDocument.WordPart(line.part);
	if (wp == NULL || offset > wp->paragraphs[line.paragraph].text.size())
		return line.frame.left;
	const std::string& text = wp->paragraphs[line.paragraph].text;
	float at = 0;
	bool first = true;
	for (const Line::Piece& p : line.pieces) {
		if (p.start == SIZE_MAX)
			continue;
		if (first) {
			at = p.x;
			first = false;
		}
		if (offset < p.start)
			break;
		if (offset <= p.end) {
			// a field is one character showing its text
			if (p.field != 0)
				at = p.x + (offset > p.start ? TDocView::TextWidth(p.font, p.text) : 0);
			else
				at = p.x + SubstringWidth(p.font, text, p.start, offset);
			break;
		}
		at = p.x + TDocView::TextWidth(p.font, p.text)
			+ (offset - p.end) * TDocView::TextWidth(p.font, " ");
	}
	return line.frame.left + at;
}


bool
TPageView::CaretLine(const Caret& caret, size_t& lineIndex, float& x) const
{
	const WPPart* wp = fDocument.WordPart(caret.part);
	if (wp == NULL || caret.paragraph >= wp->paragraphs.size())
		return false;
	const std::string& text = wp->paragraphs[caret.paragraph].text;
	bool found = false;
	for (size_t i = 0; i < fLines.size(); i++) {
		const Line& line = fLines[i];
		if (line.part != caret.part || line.paragraph != caret.paragraph || line.angle != 0)
			continue;
		bool last = i + 1 >= fLines.size() || fLines[i + 1].part != line.part
			|| fLines[i + 1].paragraph != line.paragraph;
		if (caret.offset >= line.from && (caret.offset < line.to || last)) {
			lineIndex = i;
			found = true;
			break;
		}
	}
	if (!found)
		return false;
	x = OffsetX(lineIndex, caret.offset);
	return true;
}


// Public accessor: the caret's x position in view coordinates
float
TPageView::CaretX() const
{
	size_t line;
	float x = 0;
	CaretLine(fCaret, line, x);
	return x;
}


bool
TPageView::TextSelection(TextPosition& from, TextPosition& to) const
{
	if (!fAnchor.active || fAnchor.part != fCaret.part
		|| (fAnchor.paragraph == fCaret.paragraph && fAnchor.offset == fCaret.offset))
		return false;
	if (fAnchor.paragraph < fCaret.paragraph
		|| (fAnchor.paragraph == fCaret.paragraph && fAnchor.offset < fCaret.offset)) {
		from = {fAnchor.paragraph, fAnchor.offset};
		to = {fCaret.paragraph, fCaret.offset};
	} else {
		from = {fCaret.paragraph, fCaret.offset};
		to = {fAnchor.paragraph, fAnchor.offset};
	}
	return true;
}


TextStyle
TPageView::StyleAt(TextPosition at) const
{
	const WPPart* wp = fDocument.WordPart(fCaret.part);
	if (wp == NULL || at.paragraph >= wp->paragraphs.size())
		return TextStyle();
	const WPParagraph& p = wp->paragraphs[at.paragraph];
	uint32_t style = p.parStyle;
	for (const WPStyleRun& r : p.runs)
		if (r.position <= at.offset)
			style = r.style;
	return ResolveTextStyle(fDocument.styles, style);
}


std::pair<TextPosition, TextPosition>
TPageView::TextSelectionRange() const
{
	TextPosition from, to;
	if (TextSelection(from, to))
		return {from, to};
	// no selection: zero-length range at caret
	return {{fCaret.paragraph, fCaret.offset},
		{fCaret.paragraph, fCaret.offset}};
}


bool
TPageView::HitText(BPoint where, Caret& caret) const
{
	for (size_t i = 0; i < fLines.size(); i++) {
		const Line& line = fLines[i];
		if (line.angle != 0 || where.y < line.frame.top || where.y >= line.frame.bottom
			|| where.x < line.frame.left - 6 || where.x > line.frame.right + 6)
			continue;
		const WPPart* wp = fDocument.WordPart(line.part);
		if (wp == NULL)
			continue;
		const std::string& text = wp->paragraphs[line.paragraph].text;
		// the character boundary nearest to the click
		size_t best = line.from;
		float bestDistance = 1e9;
		auto consider = [&](size_t offset) {
			Caret c{true, line.part, line.paragraph, offset};
			size_t index;
			float x;
			if (CaretLine(c, index, x) && index == i && fabsf(x - where.x) < bestDistance) {
				bestDistance = fabsf(x - where.x);
				best = offset;
			}
		};
		consider(line.from);
		for (const Line::Piece& p : line.pieces) {
			if (p.start == SIZE_MAX)
				continue;
			for (size_t o = p.start; o <= p.end && o <= text.size(); o = NextCharacter(text, o)) {
				consider(o);
				if (o == text.size())
					break;
			}
		}
		consider(std::min(line.to, EditableLength(wp->paragraphs[line.paragraph])));
		caret = {true, line.part, line.paragraph, best};
		return true;
	}
	return false;
}


bool
TPageView::DeleteSelection()
{
	TextPosition from, to;
	if (!TextSelection(from, to))
		return false;
	TextPosition result;
	if (!DeleteRange(*fDocument.EditableWordPart(fCaret.part), from, to, result)) {
		beep();
		return false;
	}
	fCaret = {true, fCaret.part, result.paragraph, result.offset};
	fAnchor = fCaret;
	return true;
}


void
TPageView::CopySelection()
{
	TextPosition from, to;
	if (!TextSelection(from, to))
		return;
	// the styled text for pasting here; its text for everyone
	sCopiedText = CopyFragment(fDocument, fCaret.part, from, to);
	std::string s = sCopiedText.text;
	be_clipboard->Lock();
	be_clipboard->Clear();
	be_clipboard->Data()->AddData("text/plain", B_MIME_TYPE, s.data(), s.size());
	be_clipboard->Commit();
	be_clipboard->Unlock();
}


void
TPageView::PasteText()
{
	if (!be_clipboard->Lock())
		return;
	const void* data = NULL;
	ssize_t size = 0;
	if (be_clipboard->Data()->FindData("text/plain", B_MIME_TYPE, &data, &size) == B_OK
			&& size > 0) {
		std::string text((const char*)data, size);
		be_clipboard->Unlock();
		TextPosition from, to;
		if (TextSelection(from, to) && !DeleteSelection())
			return;
		// what was copied here keeps its styles
		TextPosition at = text == sCopiedText.text
			? PasteFragment(fDocument, fCaret.part, {fCaret.paragraph, fCaret.offset}, sCopiedText)
			: InsertMultiline(*fDocument.EditableWordPart(fCaret.part),
				{fCaret.paragraph, fCaret.offset}, text);
		fCaret = {true, fCaret.part, at.paragraph, at.offset};
		fAnchor = fCaret;
		TextChanged();
	} else {
		be_clipboard->Unlock();
	}
}


void
TPageView::SelectAllText()
{
	const WPPart* wp = fDocument.WordPart(fCaret.part);
	if (wp == NULL) {
		fprintf(stderr, "selectall: no word part\n");
		return;
	}
	fAnchor = {true, fCaret.part, 0, 0};
	fCaret = {true, fCaret.part, wp->paragraphs.size() - 1,
		EditableLength(wp->paragraphs.back())};
	Invalidate();
}


void
TPageView::DrawSelection()
{
	TextPosition from, to;
	if (!fCaret.active || !TextSelection(from, to))
		return;
	for (size_t i = 0; i < fLines.size(); i++) {
		const Line& line = fLines[i];
		if (line.part != fCaret.part || line.angle != 0
			|| line.paragraph < from.paragraph || line.paragraph > to.paragraph)
			continue;
		size_t start = (line.paragraph == from.paragraph) ? from.offset : 0;
		size_t end = (line.paragraph == to.paragraph) ? to.offset : line.to;
		size_t spanStart = std::max(line.from, start);
		size_t spanEnd = std::min(line.to, end);
		bool extendRight = line.paragraph < to.paragraph;
		if (spanStart >= spanEnd && !extendRight)
			continue;
		float x0 = OffsetX(i, spanStart);
		float x1 = extendRight ? line.frame.right : OffsetX(i, spanEnd);
		PushState();
		SetDrawingMode(B_OP_COPY);
		SetHighColor(180, 205, 255);
		FillRect(BRect(x0, line.frame.top, x1, line.frame.bottom));
		PopState();
	}
}


void
TPageView::DrawCaret()
{
	if (!fCaret.active || !IsFocus())
		return;
	TextPosition from, to;
	if (TextSelection(from, to))
		return;
	size_t index;
	float x;
	if (!CaretLine(fCaret, index, x))
		return;
	const Line& line = fLines[index];
	float size = line.pieces.empty() ? 12 : line.pieces[0].font.Size();
	SetHighColor(0, 0, 0);
	SetPenSize(1);
	StrokeLine(BPoint(x, line.baseline - size * 0.95f), BPoint(x, line.baseline + size * 0.25f));
}


// TWPPart::SetUpContextMenu: Copy Character Format ('sM16', the clicked
// position), Copy Paragraph Format ('sM5E', its paragraph), Paste Format
// ('sM17', when a format was copied; STR# 1302, 2-4); Open URL ('sE03')
// when the selection or the click is in one (ParseURL); the clicked
// word's Suggested Spelling ('sM5L', TSpellMenuInit) when it isn't good
// (a good word's Synonyms need the thesaurus, not ported)
BPopUpMenu*
TPageView::ContextMenu(BPoint where)
{
	Caret click;
	if (!HitText(where, click))
		return NULL;
	const WPPart* wp = fDocument.WordPart(click.part);
	if (wp == NULL || click.paragraph >= wp->paragraphs.size())
		return NULL;
	TextPosition from, to;
	bool selection = TextSelection(from, to) && fCaret.part == click.part;
	TextPosition at = {click.paragraph, click.offset};
	auto before = [](TextPosition a, TextPosition b) {
		return a.paragraph < b.paragraph || (a.paragraph == b.paragraph && a.offset < b.offset);
	};
	// TWPSelection::Contains
	bool inside = selection && !before(at, from) && !before(to, at);
	if (!inside) {
		fCaret = fAnchor = click;
		fUndo.Break();
		Invalidate();
		UpdateMarks();
		selection = false;
	}
	uint32_t position = PartPosition(*wp, at);

	BPopUpMenu* menu = new BPopUpMenu("context", false, false);
	BMessage* message = new BMessage('sM16');
	message->AddInt32(kValueString, int32(position));
	menu->AddItem(new BMenuItem(GobeResources::IndexString(0x516, 2).String(), message));
	message = new BMessage('sM5E');
	message->AddInt32(kValueString, int32(click.paragraph));
	menu->AddItem(new BMenuItem(GobeResources::IndexString(0x516, 3).String(), message));
	message = new BMessage('sM17');
	message->AddBool("replace", true);
	BMenuItem* paste = new BMenuItem(GobeResources::IndexString(0x516, 4).String(), message);
	paste->SetEnabled(!sCopiedFormat.empty());
	menu->AddItem(paste);

	// a URL: the selection's, if it's in the paragraph, else the click's
	const std::string& text = wp->paragraphs[click.paragraph].text;
	size_t start = click.offset, end = click.offset;
	bool url = true;
	if (selection) {
		url = from.paragraph == click.paragraph && to.paragraph == click.paragraph;
		start = from.offset;
		end = to.offset;
	}
	std::string address;
	if (url && ParseURL(text, start, end, address)) {
		message = new BMessage('sE03');
		message->AddString("URL", address.c_str());
		menu->AddSeparatorItem();
		menu->AddItem(new BMenuItem(GobeResources::IndexString(0x516, 5).String(), message));
	}

	// the clicked word (WordBounds) unless it starts with a break, a tab
	// or a space
	size_t wordFrom, wordTo;
	WordBounds(text, click.offset, true, wordFrom, wordTo);
	static const char kBreaks[] = {1, '\n', 2, '\f', 5, '\t', ' '};
	bool skip = wordFrom >= text.size()
		|| std::find(kBreaks, kBreaks + sizeof(kBreaks), text[wordFrom]) != kBreaks + sizeof(kBreaks);
	if (!skip && wordTo > wordFrom && TSpellBar::StartSession(true)) {
		Speller& speller = TSpellBar::AppSpeller();
		if (speller.CheckWord(text.substr(wordFrom, wordTo - wordFrom)) != Speller::kGood) {
			BMenu* suggestions = new BMenu(GobeResources::IndexString(0x516, 14).String());
			// TSpeller::RebuildAltWordPopUp with a message: "text" added
			BMessage replace('sM5L');
			replace.AddInt32("startPos", int32(PartPosition(*wp, {click.paragraph, wordFrom})));
			replace.AddInt32("endPos", int32(PartPosition(*wp, {click.paragraph, wordTo})));
			std::vector<std::string> words = speller.Alternatives();
			for (const std::string& word : words) {
				BMessage* item = new BMessage(replace);
				item->AddString("text", word.c_str());
				suggestions->AddItem(new BMenuItem(word.c_str(), item));
			}
			if (words.empty()) {
				BMenuItem* none = new BMenuItem(GobeResources::IndexString(5, 4).String(), NULL);
				none->SetEnabled(false);
				suggestions->AddItem(none);
			}
			suggestions->SetTargetForItems(Window());
			menu->AddItem(suggestions);
		}
	}
	menu->SetTargetForItems(Window());
	return menu;
}


void
TPageView::SelectText(size_t part, TextPosition from, size_t to)
{
	fAnchor = {true, part, from.paragraph, from.offset};
	fCaret = {true, part, from.paragraph, to};
	fUndo.Break();
	Invalidate();
	UpdateMarks();
	ScrollCaretIntoView();
}


void
TPageView::ReplaceText(size_t part, TextPosition from, size_t to, const std::string& text)
{
	WPPart* wp = fDocument.EditableWordPart(part);
	if (wp == NULL)
		return;
	fUndo.Begin(fDocument, part, "Replace Text", CurrentViewState());
	// (a word holds no anchor DeleteRange would refuse)
	TextPosition at;
	if (!DeleteRange(*wp, from, {from.paragraph, to}, at))
		return;
	InsertText(*wp, at, text);
	fUndo.Break();
	fAnchor = {true, part, at.paragraph, at.offset};
	fCaret = {true, part, at.paragraph, at.offset + text.size()};
	TextChanged();
	ScrollCaretIntoView();
}


void
TPageView::SelectRange(size_t part, uint32_t start, uint32_t end)
{
	const WPPart* wp = fDocument.WordPart(part);
	if (wp == NULL)
		return;
	TextPosition from = FromPartPosition(*wp, start);
	TextPosition to = FromPartPosition(*wp, end);
	fAnchor = {true, part, from.paragraph, from.offset};
	fCaret = {true, part, to.paragraph, to.offset};
	fUndo.Break();
	Invalidate();
	UpdateMarks();
	ScrollCaretIntoView();
}


void
TPageView::ReplaceRange(size_t part, uint32_t start, uint32_t end, const std::string& text)
{
	if (fDocument.WordPart(part) == NULL)
		return;
	fUndo.Begin(fDocument, part, "Replace Text", CurrentViewState());
	FoundText replaced;
	if (!ReplacePartText(fDocument, part, start, end, text, replaced))
		return;
	fUndo.Break();
	TextChanged();
	SelectRange(part, replaced.start, replaced.end);
}


bool
TPageView::HasCopiedFormat()
{
	return !sCopiedFormat.empty();
}


void
TPageView::ScrollCaretIntoView()
{
	if (fLines.empty())
		LayoutText();
	size_t index;
	float x;
	if (!CaretLine(fCaret, index, x))
		return;
	BRect line = fLines[index].frame;
	line.left = line.right = x;
	line = BRect(line.left * fZoom, line.top * fZoom, line.right * fZoom, line.bottom * fZoom);
	BRect bounds = Bounds();
	float dx = 0, dy = 0;
	if (line.top < bounds.top)
		dy = line.top - bounds.top - 20;
	else if (line.bottom > bounds.bottom)
		dy = line.bottom - bounds.bottom + 20;
	if (line.left < bounds.left)
		dx = line.left - bounds.left - 20;
	else if (line.right > bounds.right)
		dx = line.right - bounds.right + 20;
	if (dx != 0 || dy != 0)
		ScrollBy(dx, dy);
}


void
TPageView::TextChanged()
{
	fLines.clear();
	LayoutText();
	Invalidate();
	if (Window() != NULL)
		Window()->PostMessage(kMsgDocumentChanged);
	UpdateMarks();
}


void
TPageView::UpdateMarks()
{
	if (Window() == NULL || !fCaret.active)
		return;
	// Get the style at the caret position
	const WPPart* wp = fDocument.WordPart(fCaret.part);
	if (wp == NULL || fCaret.paragraph >= wp->paragraphs.size())
		return;
	const WPParagraph& par = wp->paragraphs[fCaret.paragraph];

	// Find the run at the caret offset
	TextStyle style = ResolveTextStyle(fDocument.styles, par.parStyle);
	size_t pos = fCaret.offset;
	size_t runIdx = 0;
	for (; runIdx < par.runs.size(); runIdx++) {
		const WPStyleRun& r = par.runs[runIdx];
		if (r.position > pos)
			break;
	}
	if (runIdx < par.runs.size()) {
		style = ResolveTextStyle(fDocument.styles, par.runs[runIdx].style);
	}

	TDocWindow* win = dynamic_cast<TDocWindow*>(Window());
	if (win != NULL) {
		win->UpdateTextMarks(
			style.family.empty() ? NULL : style.family.c_str(),
			style.faceFlags,
			style.size,
			style.alignment);
	}
}


void
TPageView::MoveCaretVertically(int direction)
{
	size_t index;
	float x;
	if (!CaretLine(fCaret, index, x))
		return;
	int target = int(index) + direction;
	if (target < 0 || target >= int(fLines.size())
		|| fLines[target].part != fCaret.part)
		return;
	const Line& line = fLines[target];
	Caret c;
	if (HitText(BPoint(x, (line.frame.top + line.frame.bottom) / 2), c))
		fCaret = c;
}


// TWPPart::DoSpecialKeys' Page Up and Down: the view scrolled a page
// (the scroll bar's large step there; the port's scroll bars keep Haiku's
// steps, so the view's height), the caret where it was in the window; at
// the top or the bottom, the text's start or end
void
TPageView::MoveCaretByPage(int direction)
{
	size_t index;
	float x;
	if (!CaretLine(fCaret, index, x))
		return;
	BScrollBar* bar = ScrollBar(B_VERTICAL);
	float before = Bounds().top;
	float page = Bounds().Height();
	if (bar != NULL) {
		float minimum, maximum;
		bar->GetRange(&minimum, &maximum);
		page = std::max(std::min(page, maximum - before), minimum - before);
		if (direction < 0)
			page = std::min(Bounds().Height(), before - minimum);
	}
	ScrollBy(0, direction * page);
	float moved = Bounds().top - before;
	const WPPart* wp = fDocument.WordPart(fCaret.part);
	float minimum = 0, maximum = 0;
	if (bar != NULL)
		bar->GetRange(&minimum, &maximum);
	if (wp != NULL && bar != NULL && bar->Value() <= minimum && direction < 0) {
		fCaret.paragraph = 0;
		fCaret.offset = 0;
		return;
	}
	if (wp != NULL && bar != NULL && bar->Value() >= maximum && direction > 0) {
		fCaret.paragraph = wp->paragraphs.size() - 1;
		fCaret.offset = EditableLength(wp->paragraphs.back());
		return;
	}
	const Line& line = fLines[index];
	Caret c;
	if (HitText(BPoint(x, (line.frame.top + line.frame.bottom) / 2 + moved / fZoom), c)
			&& c.part == fCaret.part)
		fCaret = c;
}


void
TPageView::EditText(const char* bytes, int32 count)
{
	bool shift = ((modifiers() & B_SHIFT_KEY) != 0) || fScriptShift;
	WPPart* wp = fDocument.EditableWordPart(fCaret.part);
	if (wp == NULL || fCaret.paragraph >= wp->paragraphs.size())
		return;
	bool changed = false;
	switch (bytes[0]) {
		case B_BACKSPACE:
		case B_DELETE: {
			fUndo.Begin(fDocument, fCaret.part,
				bytes[0] == B_BACKSPACE ? "Backspace" : "Delete",
				CurrentViewState());
			TextPosition from, to;
			if (TextSelection(from, to)) {
				if (DeleteSelection())
					changed = true;
			} else if (bytes[0] == B_BACKSPACE) {
				wp = fDocument.EditableWordPart(fCaret.part);
				if (wp == NULL || fCaret.paragraph >= wp->paragraphs.size())
					break;
				const WPParagraph& par = wp->paragraphs[fCaret.paragraph];
				if (fCaret.offset > 0) {
					size_t previous = PreviousCharacter(par.text, fCaret.offset);
					if (DeleteText(*wp, {fCaret.paragraph, previous}, fCaret.offset)) {
						fCaret.offset = previous;
						changed = true;
					} else
						beep();
				} else {
					TextPosition joined;
					if (JoinWithPrevious(*wp, fCaret.paragraph, joined)) {
						fCaret.paragraph = joined.paragraph;
						fCaret.offset = joined.offset;
						changed = true;
					}
				}
			} else {
				wp = fDocument.EditableWordPart(fCaret.part);
				if (wp == NULL || fCaret.paragraph >= wp->paragraphs.size())
					break;
				const WPParagraph& par = wp->paragraphs[fCaret.paragraph];
				if (fCaret.offset < EditableLength(par)) {
					size_t next = NextCharacter(par.text, fCaret.offset);
					if (DeleteText(*wp, {fCaret.paragraph, fCaret.offset}, next))
						changed = true;
					else
						beep();
				} else {
					TextPosition joined;
					changed = JoinWithPrevious(*wp, fCaret.paragraph + 1, joined);
				}
			}
			break;
		}
		case B_ENTER: {
			fUndo.Begin(fDocument, fCaret.part, "Enter",
				CurrentViewState());
			TextPosition from, to;
			if (TextSelection(from, to)) {
				if (!DeleteSelection())
					break;
				wp = fDocument.EditableWordPart(fCaret.part);
				if (wp == NULL || fCaret.paragraph >= wp->paragraphs.size())
					break;
			}
			TextPosition next = SplitParagraph(*wp, {fCaret.paragraph, fCaret.offset});
			fCaret.paragraph = next.paragraph;
			fCaret.offset = next.offset;
			changed = true;
			break;
		}
		case B_LEFT_ARROW:
		case B_RIGHT_ARROW:
		case B_UP_ARROW:
		case B_DOWN_ARROW:
		case B_HOME:
		case B_END:
		case B_PAGE_UP:
		case B_PAGE_DOWN: {
			// TWPPart::DoSpecialKeys: Command with Left and Right to the
			// line's start and end, with Up and Down to the text's; Option
			// or Control with Left and Right a word; Control with Home and
			// End to the text's start and end; Home and End the line's,
			// Page Up and Down a page (the PC Home & End and Page Keys
			// preferences, on by default). Without Shift a selection's start
			// (Left, Up) or end is where the move starts from.
			uint32 mods = fScriptModifiers;
			if (mods == 0 && Window() != NULL && Window()->CurrentMessage() != NULL)
				mods = uint32(Window()->CurrentMessage()->GetInt32("modifiers", 0));
			if (mods == 0)
				mods = modifiers();
			bool command = (mods & B_COMMAND_KEY) != 0;
			bool control = (mods & B_CONTROL_KEY) != 0;
			bool option = (mods & B_OPTION_KEY) != 0;
			char key = bytes[0];
			// the preferences off: TDocWindow::DoSpecialKey scrolls (Home and
			// End to the start or end, the page keys a page), the caret
			// stays
			const Preferences& prefs = AppPreferences();
			if (!control && (((key == B_HOME || key == B_END) && !prefs.pcHomeEnd)
					|| ((key == B_PAGE_UP || key == B_PAGE_DOWN) && !prefs.pcPageKeys))) {
				BScrollBar* bar = ScrollBar(B_VERTICAL);
				float minimum = 0, maximum = 0;
				if (bar != NULL)
					bar->GetRange(&minimum, &maximum);
				if (key == B_HOME || key == B_END)
					ScrollTo(Bounds().left, key == B_HOME ? minimum : maximum);
				else
					ScrollBy(0, (key == B_PAGE_DOWN ? 1 : -1) * Bounds().Height());
				break;
			}
			TextPosition from, to;
			bool selection = TextSelection(from, to);
			if (shift) {
				if (!fAnchor.active || fAnchor.part != fCaret.part)
					fAnchor = fCaret;
			} else if (selection) {
				TextPosition origin = key == B_LEFT_ARROW || key == B_UP_ARROW || key == B_PAGE_UP
					? from : to;
				fCaret = {true, fCaret.part, origin.paragraph, origin.offset};
				// a plain Left or Right only collapses the selection
				if ((key == B_LEFT_ARROW || key == B_RIGHT_ARROW) && !command && !option
						&& !control) {
					fAnchor = fCaret;
					changed = true;
					break;
				}
			}
			wp = fDocument.EditableWordPart(fCaret.part);
			if (wp == NULL || fCaret.paragraph >= wp->paragraphs.size())
				break;
			auto lineEdge = [&](bool end) {
				size_t index;
				float x;
				const WPParagraph& par = wp->paragraphs[fCaret.paragraph];
				if (!CaretLine(fCaret, index, x))
					return;
				fCaret.offset = !end ? fLines[index].from
					: std::min(fLines[index].to, EditableLength(par));
				if (end && fCaret.offset > fLines[index].from
					&& fCaret.offset < EditableLength(par)
					&& par.text[PreviousCharacter(par.text, fCaret.offset)] == ' ')
					fCaret.offset = PreviousCharacter(par.text, fCaret.offset);
			};
			auto textStart = [&]() {
				fCaret.paragraph = 0;
				fCaret.offset = 0;
			};
			// LastTextInsertionPos
			auto textEnd = [&]() {
				fCaret.paragraph = wp->paragraphs.size() - 1;
				fCaret.offset = EditableLength(wp->paragraphs.back());
			};
			auto previous = [&]() {
				if (fCaret.offset > 0)
					fCaret.offset = PreviousCharacter(wp->paragraphs[fCaret.paragraph].text,
						fCaret.offset);
				else if (fCaret.paragraph > 0) {
					fCaret.paragraph--;
					fCaret.offset = EditableLength(wp->paragraphs[fCaret.paragraph]);
				}
			};
			auto next = [&]() {
				const WPParagraph& par = wp->paragraphs[fCaret.paragraph];
				if (fCaret.offset < EditableLength(par))
					fCaret.offset = NextCharacter(par.text, fCaret.offset);
				else if (fCaret.paragraph + 1 < wp->paragraphs.size()) {
					fCaret.paragraph++;
					fCaret.offset = 0;
				}
			};
			switch (key) {
				case B_LEFT_ARROW:
					if (command)
						lineEdge(false);
					else if (option || control) {
						// the word before the blanks before (SpaceBounds,
						// PrevChar, WordBounds)
						size_t blankFrom, blankTo, wordFrom, wordTo;
						SpaceBounds(wp->paragraphs[fCaret.paragraph].text, fCaret.offset,
							blankFrom, blankTo);
						fCaret.offset = blankFrom;
						previous();
						WordBounds(wp->paragraphs[fCaret.paragraph].text, fCaret.offset, false,
							wordFrom, wordTo);
						fCaret.offset = wordFrom;
					} else
						previous();
					break;
				case B_RIGHT_ARROW:
					if (command)
						lineEdge(true);
					else if (option || control) {
						// the end of the word after the blanks after
						const std::string& text = wp->paragraphs[fCaret.paragraph].text;
						size_t blankFrom, blankTo, wordFrom, wordTo;
						SpaceBounds(text, fCaret.offset, blankFrom, blankTo);
						WordBounds(text, blankTo, false, wordFrom, wordTo);
						if (wordTo != blankTo)
							fCaret.offset = std::min(wordTo, EditableLength(wp->paragraphs[
								fCaret.paragraph]));
						else {
							fCaret.offset = blankTo;
							next();
						}
					} else
						next();
					break;
				case B_UP_ARROW:
					if (command)
						textStart();
					else
						MoveCaretVertically(-1);
					break;
				case B_DOWN_ARROW:
					if (command)
						textEnd();
					else
						MoveCaretVertically(1);
					break;
				case B_HOME:
					if (control)
						textStart();
					else
						lineEdge(false);
					break;
				case B_END:
					if (control)
						textEnd();
					else
						lineEdge(true);
					break;
				case B_PAGE_UP:
				case B_PAGE_DOWN:
					MoveCaretByPage(key == B_PAGE_UP ? -1 : 1);
					break;
			}
			if (!shift)
				fAnchor = fCaret;
			fUndo.Break();
			ScrollCaretIntoView();
			break;
		}
		case B_ESCAPE:
			fCaret.active = false;
			fAnchor.active = false;
			break;
		default:
			if ((unsigned char)bytes[0] >= 0x20 || bytes[0] == B_TAB) {
				fUndo.Begin(fDocument, fCaret.part, "Typing",
					CurrentViewState(), uint64_t(fCaret.part) + 1);
				TextPosition from, to;
				if (TextSelection(from, to)) {
					if (!DeleteSelection())
						break;
					wp = fDocument.EditableWordPart(fCaret.part);
					if (wp == NULL || fCaret.paragraph >= wp->paragraphs.size())
						break;
				}
				TextPosition at = InsertText(*wp, {fCaret.paragraph, fCaret.offset},
					std::string(bytes, count));
				fCaret.offset = at.offset;
				changed = true;
			}
			break;
	}
	// edits leave no selection (whether Shift was down, as for a capital,
	// or not); moves set the anchor themselves
	if (changed)
		fAnchor = fCaret;
	if (changed)
		TextChanged();
	else
		Invalidate();
}


// #pragma mark - cells

bool
TPageView::HitCell(BPoint where, size_t& part, int& row, int& column, BRect& rect) const
{
	float dx = fMargin - fBounds.left;
	float dy = fMargin - fBounds.top;
	// the innermost (last drawn) spreadsheet frame under the point
	const Document::PlacedFrame* hit = NULL;
	for (const auto& f : fFrames) {
		if (fDocument.parts.parts[f.part].kind != Sig("SPRD")
			|| fDocument.SpreadsheetPart(f.part) == NULL || f.frame->angle != 0)
			continue;
		BRect r(f.x + dx, f.y + dy, f.x + dx + f.width, f.y + dy + f.height);
		if (r.Contains(where) && (hit == NULL || f.depth >= hit->depth))
			hit = &f;
	}
	if (hit == NULL)
		return false;
	BRect r(hit->x + dx, hit->y + dy, hit->x + dx + hit->width, hit->y + dy + hit->height);
	SheetGeometry g = SheetLayout(*hit, r);
	for (size_t c = 0; c + 1 < g.xs.size(); c++) {
		if (where.x < g.xs[c] || where.x >= g.xs[c + 1])
			continue;
		for (size_t k = 0; k + 1 < g.ys.size(); k++) {
			if (where.y < g.ys[k] || where.y >= g.ys[k + 1])
				continue;
			part = hit->part;
			row = g.firstRow + int(k);
			column = g.firstColumn + int(c);
			rect.Set(g.xs[c], g.ys[k], g.xs[c + 1], g.ys[k + 1]);
			return true;
		}
	}
	return false;
}


BRect
TPageView::CellViewRect(size_t part, int row, int column) const
{
	float dx = fMargin - fBounds.left;
	float dy = fMargin - fBounds.top;
	for (const auto& f : fFrames) {
		if (f.part != part)
			continue;
		BRect r(f.x + dx, f.y + dy, f.x + dx + f.width, f.y + dy + f.height);
		SheetGeometry g = SheetLayout(f, r);
		int c = column - g.firstColumn, k = row - g.firstRow;
		if (c >= 0 && k >= 0 && c + 1 < int(g.xs.size()) && k + 1 < int(g.ys.size()))
			return BRect(g.xs[c], g.ys[k], g.xs[c + 1], g.ys[k + 1]);
	}
	return BRect();
}


void
TPageView::BeginCellEdit(size_t part, int row, int column, BRect rect)
{
	if (fCellEditor != NULL)
		EndCellEdit(true);
	const SSPart* ss = fDocument.SpreadsheetPart(part);
	if (ss == NULL || !rect.IsValid())
		return;
	fSelectedCell.active = true;
	fSelectedCell.part = part;
	fSelectedCell.row = row;
	fSelectedCell.column = column;
	rect.right = std::max(rect.right, rect.left + 120);	// room for formulas
	rect.bottom = std::max(rect.bottom, rect.top + 16);
	fCellEditor = new TCellEditor(BRect(rect.left * fZoom, rect.top * fZoom,
		rect.right * fZoom, rect.bottom * fZoom));
	AddChild(fCellEditor);
	fCellEditor->SetText(CellInput(*ss, row, column).c_str());
	fCellEditor->SelectAll();
	fCellEditor->MakeFocus(true);
}


void
TPageView::EndCellEdit(bool commit, int moveRows, int moveColumns)
{
	if (fCellEditor == NULL)
		return;
	std::string text = fCellEditor->Text();
	RemoveChild(fCellEditor);
	delete fCellEditor;
	fCellEditor = NULL;
	MakeFocus(true);
	if (commit) {
		fUndo.Begin(fDocument, fSelectedCell.part, "Cell Entry",
			CurrentViewState());
		SSPart* ss = fDocument.EditableSpreadsheetPart(fSelectedCell.part);
		std::string error;
		auto isName = [&](const std::string& n) {
			return fDocument.NamedSelection(n, int(fSelectedCell.part)) != nullptr;
		};
		if (ss == NULL || !SetCellInput(*ss, uint16_t(fSelectedCell.row),
				uint16_t(fSelectedCell.column), text, isName, error)) {
			BString message;
			message << "The formula has an error: " << error.c_str();
			(new BAlert("Formula", message.String(), "OK"))->Go(NULL);
			BeginCellEdit(fSelectedCell.part, fSelectedCell.row, fSelectedCell.column,
				CellViewRect(fSelectedCell.part, fSelectedCell.row, fSelectedCell.column));
			if (fCellEditor != NULL)
				fCellEditor->SetText(text.c_str());
			return;
		}
		fBook->Reset();
		// fields in the text show the new values (TPartMgr::CheckDependencies)
		if (!fDocument.wordParts.empty())
			LayoutText();
		if (Window() != NULL)
			Window()->PostMessage(kMsgDocumentChanged);
	}
	fSelectedCell.row = std::max(0, fSelectedCell.row + moveRows);
	fSelectedCell.column = std::max(0, fSelectedCell.column + moveColumns);
	fSelectedCell.anchorRow = fSelectedCell.row;
	fSelectedCell.anchorColumn = fSelectedCell.column;
	Invalidate();
}


// #pragma mark - input

void
TPageView::MouseDown(BPoint where)
{
	int32 clicks = 1, buttons = 0;
	if (Window() != NULL && Window()->CurrentMessage() != NULL) {
		Window()->CurrentMessage()->FindInt32("clicks", &clicks);
		Window()->CurrentMessage()->FindInt32("buttons", &buttons);
	}
	Click(BPoint(where.x / fZoom, where.y / fZoom), clicks, buttons);
}


void
TPageView::Click(BPoint where, int32 clicks, int32 buttons)
{
	if (fCellEditor != NULL)
		EndCellEdit(true);
	if (Window() != NULL)
		MakeFocus(true);

	fUndo.Break();

	size_t part;
	int row, column;
	BRect rect;
	if (HitCell(where, part, row, column, rect)) {
		bool shift = (modifiers() & B_SHIFT_KEY) != 0 || fScriptShift;
		bool same = fSelectedCell.active && fSelectedCell.part == part
			&& fSelectedCell.row == row && fSelectedCell.column == column;
		fCaret.active = false;
		fAnchor.active = false;
		fSelectedCell.active = true;
		fSelectedCell.part = part;
		if (shift && fSelectedCell.active && fSelectedCell.part == part) {
			// extend range: keep anchor, move row/column
			fSelectedCell.row = row;
			fSelectedCell.column = column;
		} else {
			fSelectedCell.row = row;
			fSelectedCell.column = column;
			fSelectedCell.anchorRow = row;
			fSelectedCell.anchorColumn = column;
		}
		if ((same || clicks > 1) && !shift)
			BeginCellEdit(part, row, column, rect);
		else {
			fDraggingCells = true;
			if (Window() != NULL)
				SetMouseEventMask(B_POINTER_EVENTS, B_LOCK_WINDOW_FOCUS);
		}
		Invalidate();
		return;
	}
	Caret caret;
	// TDocView::MouseDown: a context click (the secondary button) shows
	// the clicked frame's menu (TFrame, TWPPart::SetUpContextMenu)
	if ((buttons & B_SECONDARY_MOUSE_BUTTON) != 0 && HitText(where, caret)) {
		BPopUpMenu* menu = ContextMenu(where);
		if (menu != NULL && menu->CountItems() > 0 && Window() != NULL) {
			BPoint screen = ConvertToScreen(BPoint(where.x * fZoom, where.y * fZoom));
			menu->Go(screen, true, false, BRect(screen.x - 5, screen.y - 5, screen.x + 5,
				screen.y + 5), false);
		}
		delete menu;
		return;
	}
	if (HitText(where, caret)) {
		fSelectedCell.active = false;
		bool shift = (modifiers() & B_SHIFT_KEY) != 0 || fScriptShift;
		if (shift && fCaret.active && caret.part == fCaret.part) {
			fCaret = caret;
		} else if (clicks == 2) {
			// TWPClickHandler: a word (TWPPart::WordBounds), the line
			// (LogicalLineBounds), the paragraph (ParBounds)
			const WPPart* wp = fDocument.WordPart(caret.part);
			if (wp != NULL) {
				const std::string& text = wp->paragraphs[caret.paragraph].text;
				size_t from, to;
				WordBounds(text, caret.offset, false, from, to);
				fAnchor = {true, caret.part, caret.paragraph, from};
				fCaret = {true, caret.part, caret.paragraph, to};
			}
		} else if (clicks == 3) {
			size_t index;
			float x;
			if (CaretLine(caret, index, x)) {
				fAnchor = {true, caret.part, caret.paragraph, fLines[index].from};
				fCaret = {true, caret.part, caret.paragraph, fLines[index].to};
			}
		} else if (clicks >= 4) {
			const WPPart* wp = fDocument.WordPart(caret.part);
			if (wp != NULL) {
				fAnchor = {true, caret.part, caret.paragraph, 0};
				fCaret = {true, caret.part, caret.paragraph,
					EditableLength(wp->paragraphs[caret.paragraph])};
			}
		} else {
			fCaret = caret;
			fAnchor = caret;
			fDragging = true;
			if (Window() != NULL)
				SetMouseEventMask(B_POINTER_EVENTS, B_LOCK_WINDOW_FOCUS);
		}
		Invalidate();
		return;
	}
	bool hadFocus = fCaret.active || fSelectedCell.active;
	fCaret.active = false;
	fAnchor.active = false;
	fSelectedCell.active = false;
	if (hadFocus)
		Invalidate();
	else if (fDocument.presentationPart >= 0)
		ShowSlide(fDocument.currentSlide + ((buttons & B_SECONDARY_MOUSE_BUTTON) ? -1 : 1));
}


void
TPageView::MouseMoved(BPoint where, uint32 transit, const BMessage* dragMessage)
{
	// (the document's coordinates; the mouse's are the zoomed view's)
	where.x /= fZoom;
	where.y /= fZoom;
	if (fDragging) {
		Caret c;
		if (HitText(where, c) && c.part == fCaret.part) {
			fCaret = c;
			Invalidate();
		}
	}
	if (fDraggingCells) {
		size_t part;
		int row, column;
		BRect rect;
		if (HitCell(where, part, row, column, rect) && part == fSelectedCell.part) {
			fSelectedCell.row = row;
			fSelectedCell.column = column;
			Invalidate();
		}
	}
	BView::MouseMoved(where, transit, dragMessage);
}


void
TPageView::MouseUp(BPoint where)
{
	fDragging = false;
	fDraggingCells = false;
	BView::MouseUp(where);
}


void
TPageView::KeyDown(const char* bytes, int32 count)
{
	if (count < 1)
		return;
	if (fCaret.active) {
		EditText(bytes, count);
		return;
	}
	if (fSelectedCell.active) {
		bool shift = ((modifiers() & B_SHIFT_KEY) != 0) || fScriptShift;
		BRect rect = CellViewRect(fSelectedCell.part, fSelectedCell.row,
			fSelectedCell.column);
		switch (bytes[0]) {
			case B_UP_ARROW:
				if (shift) {
					fSelectedCell.row = std::max(0, fSelectedCell.row - 1);
				} else {
					fSelectedCell.anchorRow = fSelectedCell.row = std::max(0, fSelectedCell.row - 1);
					fSelectedCell.anchorColumn = fSelectedCell.column;
				}
				break;
			case B_DOWN_ARROW:
				if (shift) {
					fSelectedCell.row++;
				} else {
					fSelectedCell.anchorRow = ++fSelectedCell.row;
					fSelectedCell.anchorColumn = fSelectedCell.column;
				}
				break;
			case B_LEFT_ARROW:
				if (shift) {
					fSelectedCell.column = std::max(0, fSelectedCell.column - 1);
				} else {
					fSelectedCell.anchorColumn = fSelectedCell.column = std::max(0, fSelectedCell.column - 1);
					fSelectedCell.anchorRow = fSelectedCell.row;
				}
				break;
			case B_RIGHT_ARROW:
			case B_TAB:
				if (shift) {
					fSelectedCell.column++;
				} else {
					fSelectedCell.anchorColumn = ++fSelectedCell.column;
					fSelectedCell.anchorRow = fSelectedCell.row;
				}
				break;
			case B_ENTER:
				BeginCellEdit(fSelectedCell.part, fSelectedCell.row, fSelectedCell.column, rect);
				return;
			case B_BACKSPACE:
			case B_DELETE: {
				fUndo.Begin(fDocument, fSelectedCell.part, "Delete",
					CurrentViewState());
				int r0 = std::min(fSelectedCell.row, fSelectedCell.anchorRow);
				int r1 = std::max(fSelectedCell.row, fSelectedCell.anchorRow);
				int c0 = std::min(fSelectedCell.column, fSelectedCell.anchorColumn);
				int c1 = std::max(fSelectedCell.column, fSelectedCell.anchorColumn);
				SSPart* ss = fDocument.EditableSpreadsheetPart(fSelectedCell.part);
				std::string error;
				if (ss != NULL && ClearRange(*ss, uint16_t(r0), uint16_t(c0),
						uint16_t(r1), uint16_t(c1))) {
					fBook->Reset();
					if (Window() != NULL)
						Window()->PostMessage(kMsgDocumentChanged);
				}
				break;
			}
			case B_ESCAPE:
				fSelectedCell.active = false;
				break;
			default:
				if ((unsigned char)bytes[0] >= 0x20) {
					// typing replaces the cell's content
					BeginCellEdit(fSelectedCell.part, fSelectedCell.row, fSelectedCell.column,
						rect);
					if (fCellEditor != NULL) {
						fCellEditor->SetText(std::string(bytes, count).c_str());
						fCellEditor->Select(count, count);
					}
					return;
				}
				break;
		}
		Invalidate();
		return;
	}
	if (fDocument.presentationPart < 0) {
		BView::KeyDown(bytes, count);
		return;
	}
	switch (bytes[0]) {
		case B_RIGHT_ARROW:
		case B_DOWN_ARROW:
		case B_PAGE_DOWN:
		case B_SPACE:
		case B_ENTER:
			ShowSlide(fDocument.currentSlide + 1);
			break;
		case B_LEFT_ARROW:
		case B_UP_ARROW:
		case B_PAGE_UP:
		case B_BACKSPACE:
			ShowSlide(fDocument.currentSlide - 1);
			break;
		case B_HOME:
			ShowSlide(0);
			break;
		case B_END:
			ShowSlide(int(fDocument.slides.size()) - 1);
			break;
		default:
			BView::KeyDown(bytes, count);
	}
}


void
TPageView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgCellCommit:
			EndCellEdit(true, message->GetInt32("rows", 0), message->GetInt32("columns", 0));
			return;
		case kMsgCellCancel:
			EndCellEdit(false);
			return;
		case 'sM10':	// Undo
		case 'sM11': {	// Redo
			// THistory::HandleMessage: "value" steps (1), each shown when
			// "show" (else the history's Show Undo Steps) is on
			if (fCellEditor != NULL)
				EndCellEdit(true);
			int32 steps = message->GetInt32(kValueString, 1);
			bool show = message->GetBool("show", fShowUndoSteps);
			for (int32 i = 0; i < std::max<int32>(1, steps); i++) {
				UndoStack::ViewState v = CurrentViewState();
				int part = message->what == 'sM10' ? fUndo.Undo(fDocument, v)
					: fUndo.Redo(fDocument, v);
				if (part == UndoStack::kDocumentChanged) {
					GeometryChanged();
					RestoreViewState(v);
				} else if (part < 0) {
					if (i == 0)
						beep();
					break;
				} else {
					if (fDocument.SpreadsheetPart(part) != NULL)
						fBook->Reset();
					TextChanged();
					RestoreViewState(v);
				}
				if (show && Window() != NULL)
					Window()->UpdateIfNeeded();
			}
			return;
		}
		// Format > Insert Page #, Insert Separator Line, Insert Line/Frame/
		// Page Break (TWPPart::InsertSpecial, "Insert Special"); no frame or
		// page breaks in a header or footer (+0x98 1-3)
		case 'sM52':
		case 'sM56':
		case 'sM57':
		case 'sM58':
		case 'sM59': {
			WPPart* wp = fCaret.active ? fDocument.EditableWordPart(fCaret.part) : NULL;
			if (wp == NULL || ((message->what == 'sM58' || message->what == 'sM59')
					&& wp->version98 >= 1 && wp->version98 <= 3)) {
				beep();
				return;
			}
			fUndo.Begin(fDocument, fCaret.part, "Insert Special", CurrentViewState());
			TextPosition at{fCaret.paragraph, fCaret.offset};
			wp = fDocument.EditableWordPart(fCaret.part);
			if (message->what == 'sM52' || message->what == 'sM56') {
				WPMiscInfo field{};
				field.kind = message->what == 'sM52' ? Sig("PAGE") : Sig("LINE");
				at = InsertField(*wp, at, field);
			} else
				at = InsertSpecialChar(*wp, at,
					message->what == 'sM57' ? 0x01 : message->what == 'sM58' ? 0x02 : 0x0c);
			fCaret.paragraph = at.paragraph;
			fCaret.offset = at.offset;
			fAnchor = fCaret;
			TextChanged();
			return;
		}
		// Format > Insert Date… / Insert Time… (TWPPart::InsertSpecial): the
		// format dialog, which sends the command back with "format" and
		// "static"; then a formula field "=Now()" in that format
		case 'sM53':
		case 'sM54': {
			if (!fCaret.active || fDocument.WordPart(fCaret.part) == NULL) {
				beep();
				return;
			}
			const char* format;
			if (message->FindString("format", &format) != B_OK) {
				if (Window() != NULL)
					(new TDialogWPDateTimeFormat(Window(), message->what, "", false))->RunDialog();
				return;
			}
			WPMiscInfo field;
			std::string error;
			if (!FormulaField(fDocument, *fBook, fCaret.part, GobeResources::IndexString(0x516, 8).String(),
					format, message->what, message->GetBool("static", false), true, false, field,
					error)) {
				beep();
				return;
			}
			fUndo.Begin(fDocument, fCaret.part, "Insert Special", CurrentViewState());
			TextPosition at = InsertField(*fDocument.EditableWordPart(fCaret.part),
				{fCaret.paragraph, fCaret.offset}, field);
			fCaret.paragraph = at.paragraph;
			fCaret.offset = at.offset;
			fAnchor = fCaret;
			TextChanged();
			return;
		}
		// Format > Columns… (TWPPart, TWPSetColumnsCmd): the dialog with the
		// caret's section's column count; "value" -1 is a column more, less
		// than -1 one less (the ribbon's buttons)
		case 'sM51': {
			const WPPart* wp = fCaret.active ? fDocument.WordPart(fCaret.part) : NULL;
			if (wp == NULL || int(fCaret.part) != fDocument.MainPart()
					|| fDocument.PagePart() < 0 || wp->sections.empty()) {
				beep();
				return;
			}
			uint32_t at = 0;
			for (size_t p = 0; p < fCaret.paragraph && p < wp->paragraphs.size(); p++)
				at += uint32_t(wp->paragraphs[p].text.size());
			at += uint32_t(fCaret.offset);
			size_t section = 0;
			for (size_t k = 0; k < wp->sections.size(); k++)
				if (wp->sections[k].start <= at)
					section = k;
			int32 columns = int32(wp->sections[section].columns.size());
			int32 value;
			if (message->FindInt32(kValueString, &value) != B_OK) {
				if (Window() != NULL)
					(new TDialogSection(Window(), columns))->RunDialog();
				return;
			}
			if (value < 1)
				value = value < -1 ? columns - 1 : columns + 1;
			fUndo.BeginDocument(fDocument, GobeResources::IndexString(0x515, 0).String(),
				CurrentViewState());
			if (fDocument.SetColumnCount(fCaret.part, section, value))
				GeometryChanged();
			return;
		}
		// Edit > Copy Format, Paste Format in text (TWPPart, TPart::
		// CopyFormat, PasteFormat): the character properties of the style at
		// the insertion point, kept for the application; applied to the
		// selection as one step
		case 'sM16': {
			const WPPart* wp = fCaret.active ? fDocument.WordPart(fCaret.part) : NULL;
			if (wp == NULL || fCaret.paragraph >= wp->paragraphs.size()) {
				beep();
				return;
			}
			// the first selected character's, else the one before the caret;
			// the context menu's: the one at "value" (a position in the part)
			auto sel = TextSelectionRange();
			size_t at = sel.first.offset;
			if (sel.first.paragraph == sel.second.paragraph && sel.first.offset == sel.second.offset
					&& at > 0)
				at--;
			int32 value;
			if (message->FindInt32(kValueString, &value) == B_OK) {
				sel.first = FromPartPosition(*wp, uint32(value));
				at = sel.first.offset;
			}
			const WPParagraph& p = wp->paragraphs[sel.first.paragraph];
			uint32_t style = p.parStyle;
			for (const WPStyleRun& r : p.runs)
				if (r.position <= at)
					style = r.style;
			sCopiedFormat = CharacterFormat(fDocument.styles, style);
			return;
		}
		case 'sM17': {
			if (!fCaret.active || fDocument.WordPart(fCaret.part) == NULL
					|| sCopiedFormat.empty()) {
				beep();
				return;
			}
			fUndo.Begin(fDocument, fCaret.part, GobeResources::IndexString(0x516, 4).String(),
				CurrentViewState());
			auto sel = TextSelectionRange();
			for (const Property& property : sCopiedFormat)
				ApplyCharacter(sel.first, sel.second, property);
			TextChanged();
			UpdateMarks();
			return;
		}
		// the context menu's Copy Paragraph Format (TWPPart 'sM5E',
		// TPart::CopyFormat(TStyleID&, false)): paragraph "value"'s style
		// (else the caret's); Paste Format puts the properties on the text
		// (TWPFormatCmd's TCharFormatter), so its character ones are kept
		case 'sM5E': {
			const WPPart* wp = fCaret.active ? fDocument.WordPart(fCaret.part) : NULL;
			if (wp == NULL)
				return;
			int32 value = int32(fCaret.paragraph);
			message->FindInt32(kValueString, &value);
			if (value < 0 || size_t(value) >= wp->paragraphs.size())
				return;
			sCopiedFormat = CharacterFormat(fDocument.styles, wp->paragraphs[size_t(value)].parStyle);
			return;
		}
		// the context menu's Suggested Spelling (TWPPart 'sM5L'): "startPos"
		// to "endPos" replaced by "text" (TWPReplaceTextCmd)
		case 'sM5L': {
			const WPPart* wp = fCaret.active ? fDocument.WordPart(fCaret.part) : NULL;
			int32 start, end;
			const char* text = "";
			if (wp == NULL || message->FindInt32("startPos", &start) != B_OK
					|| message->FindInt32("endPos", &end) != B_OK)
				return;
			message->FindString("text", &text);
			TextPosition from = FromPartPosition(*wp, uint32(start));
			TextPosition to = FromPartPosition(*wp, uint32(end));
			if (to.paragraph != from.paragraph)
				return;
			ReplaceText(fCaret.part, from, to.offset, text);
			return;
		}
		// Format > Show/Hide Invisibles (TWPPart: TDoc +0x70 bit 2)
		case 'sM5J':
			fDocument.documentFlags ^= 4;
			fDocument.documentFlagsChanged = true;
			Invalidate();
			return;
		// File > Insert… in text (TDocWindow, TWPPart: "insert" at the
		// insertion point): a text or RTF file's text, with its styles,
		// where the caret is
		case 'sM1B': {
			entry_ref ref;
			const char* path = NULL;
			BPath refPath;
			if (message->FindRef("refs", &ref) == B_OK && refPath.SetTo(&ref) == B_OK)
				path = refPath.Path();
			else
				message->FindString("path", &path);
			if (path == NULL || !fCaret.active || fDocument.WordPart(fCaret.part) == NULL) {
				beep();
				return;
			}
			FileKind kind = IdentifyFile(path);
			Document inserted;
			BString error, warning;
			if ((kind != kTextFile && kind != kRTFFile && kind != kTableFile)
					|| !ImportFile(path, kind, inserted, error, warning)) {
				// (a Gobe document goes in as a frame: not ported)
				beep();
				return;
			}
			int part = inserted.MainPart();
			const WPPart* text = part >= 0 ? inserted.WordPart(size_t(part)) : NULL;
			if (text == NULL || text->paragraphs.empty()) {
				beep();
				return;
			}
			TextFragment fragment = CopyFragment(inserted, size_t(part), {0, 0},
				{text->paragraphs.size() - 1, EditableLength(text->paragraphs.back())});
			fUndo.Begin(fDocument, fCaret.part, "Insert", CurrentViewState());
			TextPosition from, to;
			if (TextSelection(from, to) && !DeleteSelection())
				return;
			TextPosition at = PasteFragment(fDocument, fCaret.part,
				{fCaret.paragraph, fCaret.offset}, fragment);
			fCaret = {true, fCaret.part, at.paragraph, at.offset};
			fAnchor = fCaret;
			TextChanged();
			return;
		}
		// Writing Tools > Word Count… (TDocWindow, TWordCountDialog): the
		// selected text's, else the whole document's
		case 'sM29': {
			TextPosition from, to;
			WordCount count;
			bool selection = fCaret.active && TextSelection(from, to)
				&& (from.paragraph != to.paragraph || from.offset != to.offset);
			if (selection) {
				if (const WPPart* wp = fDocument.WordPart(fCaret.part))
					CountBlock(RangeText(*wp, from, to), count);
			} else
				count = CountDocument(fDocument);
			if (Window() != NULL)
				(new TWordCountDialog(count, selection))->RunDialog();
			else
				fprintf(stderr, "wordcount %lld characters, %lld words, %lld paragraphs, "
					"%lld pages\n", (long long)count.characters, (long long)count.words,
					(long long)count.paragraphs, (long long)count.pages);
			return;
		}
		// Format > Paragraph… (TWPPart::DoParagraphFormat): the dialog with
		// the caret's paragraph's values; then the indents, spacing and
		// alignment for the selected paragraphs, as one step
		case 'sM50': {
			const WPPart* wp = fCaret.active ? fDocument.WordPart(fCaret.part) : NULL;
			if (wp == NULL || fCaret.paragraph >= wp->paragraphs.size()) {
				beep();
				return;
			}
			float left;
			if (message->FindFloat("left", &left) != B_OK) {
				TextStyle st = ResolveTextStyle(fDocument.styles,
					wp->paragraphs[fCaret.paragraph].parStyle);
				float values[6] = {st.leftIndent, st.firstIndent, st.rightIndent, st.lineSpacing,
					st.spaceBefore, st.spaceAfter};
				bool lines[3] = {st.lineSpacingMultiple, st.spaceBeforeLines, st.spaceAfterLines};
				int unit = fDocument.sheets.sheets.empty() ? 0
					: int(fDocument.sheets.sheets[0].unit);
				if (Window() != NULL)
					(new TDialogFormatParagraph(Window(), values, lines, st.alignment, unit,
						CaretFrameWidth()))->RunDialog();
				return;
			}
			fUndo.Begin(fDocument, fCaret.part, GobeResources::IndexString(3, 0).String(),
				CurrentViewState());
			auto range = TextSelectionRange();
			size_t first = range.first.paragraph, last = range.second.paragraph;
			ApplyParagraph(first, last,
				IndentProperty(Sig("PLef"), left));
			ApplyParagraph(first, last,
				IndentProperty(Sig("PRig"), message->GetFloat("right", 0)));
			ApplyParagraph(first, last,
				IndentProperty(Sig("PInd"), message->GetFloat("first", 0)));
			ApplyParagraph(first, last,
				LineSpacingProperty(message->GetFloat("lineSpacing", 1),
					message->GetBool("lineSpacingLines", true)));
			ApplyParagraph(first, last,
				SpacingProperty(Sig("SpaB"), message->GetFloat("spaceBefore", 0),
					message->GetBool("spaceBeforeLines", true)));
			ApplyParagraph(first, last,
				SpacingProperty(Sig("SpaA"), message->GetFloat("spaceAfter", 0),
					message->GetBool("spaceAfterLines", true)));
			ApplyParagraph(first, last,
				AlignmentProperty(message->GetInt32("align", 0)));
			TextChanged();
			UpdateMarks();
			return;
		}
		// Format > Insert Footnote… (TWPPart::InsertSpecial,
		// TMiscFootnoteRef): the dialog, which sends the command back with
		// "endnote" (and "mark"); the note's text gets the caret, after its
		// mark. No footnotes in headers and footers.
		case 'sM5I': {
			const WPPart* wp = fCaret.active ? fDocument.WordPart(fCaret.part) : NULL;
			if (wp == NULL || (wp->version98 >= 1 && wp->version98 <= 3)) {
				beep();
				return;
			}
			bool endnote;
			if (message->FindBool("endnote", &endnote) != B_OK) {
				if (Window() != NULL)
					(new TDialogWPInsertFootnote(Window(), int16(wp->version9a),
						int(fCaret.part) == fDocument.MainPart()))->RunDialog();
				return;
			}
			const char* mark;
			if (message->FindString("mark", &mark) != B_OK || mark == NULL)
				mark = "";
			fUndo.BeginDocument(fDocument, "Insert Special", CurrentViewState());
			TextPosition after;
			int note = InsertNote(fDocument, fCaret.part, {fCaret.paragraph, fCaret.offset},
				endnote, mark, after);
			if (note < 0) {
				beep();
				return;
			}
			fCaret = {true, size_t(note), 0, 2};
			fAnchor = fCaret;
			GeometryChanged();
			return;
		}
		// Footnote Options (TWPSetFootnoteOptionsCmd): the first number
		case 'sM5K': {
			int16 first;
			if (!fCaret.active || fDocument.WordPart(fCaret.part) == NULL
					|| message->FindInt16("start", &first) != B_OK) {
				beep();
				return;
			}
			fUndo.BeginDocument(fDocument, GobeResources::IndexString(0x515, 6).String(),
				CurrentViewState());
			if (SetFirstNoteNumber(fDocument, fCaret.part, uint16_t(first)))
				TextChanged();
			return;
		}
		// Format > Insert Formula… (TWPPart::InsertSpecial): the dialog,
		// which sends the command back with the formula and how to show it
		case 'sM55': {
			if (!fCaret.active || fDocument.WordPart(fCaret.part) == NULL) {
				beep();
				return;
			}
			const char* formula;
			if (message->FindString("formula", &formula) != B_OK
					|| !message->HasBool("displayFormula")) {
				if (Window() != NULL)
					(new TDialogWPFormula(Window(), "", true, false))->RunDialog();
				return;
			}
			const char* format;
			if (message->FindString("format", &format) != B_OK || format == NULL)
				format = "";
			WPMiscInfo field;
			std::string error;
			if (!FormulaField(fDocument, *fBook, fCaret.part, formula, format, 'sM55',
					message->GetBool("static", false), message->GetBool("displayResult", true),
					message->GetBool("displayFormula", false), field, error)) {
				beep();
				return;
			}
			fUndo.Begin(fDocument, fCaret.part, "Insert Special", CurrentViewState());
			TextPosition at = InsertField(*fDocument.EditableWordPart(fCaret.part),
				{fCaret.paragraph, fCaret.offset}, field);
			fCaret.paragraph = at.paragraph;
			fCaret.offset = at.offset;
			fAnchor = fCaret;
			TextChanged();
			return;
		}
		// Font menu (TFormatCommand: 'sf01', "font", "style")
		case 'sf01': {
			if (!fCaret.active) {
				beep();
				return;
			}
			const char* family = NULL;
			const char* style = "";
			if (message->FindString("font", &family) != B_OK || family == NULL) {
				beep();
				return;
			}
			if (message->FindString("style", &style) != B_OK || style == NULL)
				style = "";
			fUndo.Begin(fDocument, fCaret.part, "Font", CurrentViewState());
			auto sel = TextSelectionRange();
			ApplyCharacter(sel.first, sel.second,
				TextFontProperty(family, style));
			TextChanged();
			UpdateMarks();
			return;
		}
		// Size menu: value_message = 'sf0K', "value" = point size
		case 'sf0K': {
			if (!fCaret.active) {
				beep();
				return;
			}
			// a size, Other… ('sM0U': TDialogGRSize with the size at the
			// selection, which sends a size back as a float)
			int32 value = 0;
			float size = 0;
			if (message->FindInt32(kValueString, &value) == B_OK) {
				if (value == 'sM0U') {
					BString text;
					text << int32(StyleAt(TextSelectionRange().first).size);
					BString title = GobeResources::IndexString(2, 7);
					if (Window() != NULL)
						(new TDialogGRSize(Window(), 'sf0K', title.String(), title.String(),
							text.String()))->RunDialog();
					return;
				}
				size = float(value);
			} else if (message->FindFloat(kValueString, &size) != B_OK) {
				beep();
				return;
			}
			fUndo.Begin(fDocument, fCaret.part, "Size", CurrentViewState());
			auto sel = TextSelectionRange();
			ApplyCharacter(sel.first, sel.second,
				TextSizeProperty(size));
			TextChanged();
			UpdateMarks();
			return;
		}
		// Face menu: value_message = 'sf0C', "value" = face flag
		case 'sf0C': {
			if (!fCaret.active) {
				beep();
				return;
			}
			int32 flag = 0;
			if (message->FindInt32(kValueString, &flag) != B_OK) {
				beep();
				return;
			}
			fUndo.Begin(fDocument, fCaret.part, "Face", CurrentViewState());
			auto sel = TextSelectionRange();
			// Plain Text (1) clears the others; the rest toggle, as at the
			// selection's start
			uint32_t flags = 1;
			if (flag != 1) {
				flags = StyleAt(sel.first).faceFlags ^ uint32_t(flag);
				flags &= ~uint32_t(1);
				if (flags == 0)
					flags = 1;
			}
			ApplyCharacter(sel.first, sel.second,
				TextFaceProperty(flags));
			TextChanged();
			UpdateMarks();
			return;
		}
		// the part bar's alignments (TParFormatCmd::ParsePropertyMesg:
		// 'sM5B' "be:value" below 4 is PAlg)
		case 'sM5B': {
			int32 align = message->GetInt32(kValueString, 0);
			if (!fCaret.active || fDocument.WordPart(fCaret.part) == NULL || align > 3) {
				beep();
				return;
			}
			fUndo.Begin(fDocument, fCaret.part, std::string(GobeResources::IndexString(5, 5).String())
				+ GobeResources::IndexString(1304, 0).String(), CurrentViewState());
			auto range = TextSelectionRange();
			ApplyParagraph(range.first.paragraph,
				range.second.paragraph, AlignmentProperty(align));
			TextChanged();
			UpdateMarks();
			return;
		}
		// the part bar's line spacing (TWPPart 'sM5D'): with "isLines" the
		// value typed (lines, at least 1, or points); else TLineSpaceCmd:
		// each paragraph's half a line (to 10 lines) or a point (to 160)
		// more or less
		case 'sM5D': {
			const WPPart* wp = fCaret.active ? fDocument.WordPart(fCaret.part) : NULL;
			if (wp == NULL) {
				beep();
				return;
			}
			float value = message->GetFloat(kValueString, 1);
			auto range = TextSelectionRange();
			size_t first = range.first.paragraph, last = range.second.paragraph;
			bool lines;
			if (message->FindBool("isLines", &lines) == B_OK) {
				if (lines)
					value = std::max(1.0f, value);
				fUndo.Begin(fDocument, fCaret.part,
					std::string(GobeResources::IndexString(5, 5).String())
						+ GobeResources::IndexString(1304, 14).String(), CurrentViewState());
				ApplyParagraph(first, last,
					LineSpacingProperty(value, lines));
			} else {
				fUndo.Begin(fDocument, fCaret.part,
					GobeResources::IndexString(0x515, value > 0 ? 4 : 5).String(),
					CurrentViewState());
				for (size_t p = first; p <= last && p < wp->paragraphs.size(); p++) {
					TextStyle style = ResolveTextStyle(fDocument.styles,
						fDocument.WordPart(fCaret.part)->paragraphs[p].parStyle);
					float spacing;
					if (!style.lineSpacingMultiple)
						spacing = std::min(style.lineSpacing + value, 160.0f);
					else {
						float half = value == 1 ? floorf(style.lineSpacing * 2)
							: ceilf(style.lineSpacing * 2);
						spacing = std::min(value * 0.5f + 0.5f * half, 10.0f);
					}
					if (spacing > 0)
						ApplyParagraph(p, p,
							LineSpacingProperty(spacing, style.lineSpacingMultiple));
				}
			}
			TextChanged();
			return;
		}
		// the format bar's text ink, a color ('sf02', TFormatCommand::
		// ParsePropertyMesg: TPropTextColor of the ink chosen; "color" for a
		// solid one)
		case 'sf02': {
			const void* data;
			ssize_t size;
			if (!fCaret.active || fDocument.WordPart(fCaret.part) == NULL
					|| message->FindData("color", B_RGB_COLOR_TYPE, &data, &size) != B_OK
					|| size < 4) {
				beep();
				return;
			}
			const rgb_color* c = (const rgb_color*)data;
			Color color;
			color.r = c->red;
			color.g = c->green;
			color.b = c->blue;
			color.a = 255;
			fUndo.Begin(fDocument, fCaret.part, std::string(GobeResources::IndexString(5, 5).String())
				+ GobeResources::IndexString(11, 0).String(), CurrentViewState());
			auto sel = TextSelectionRange();
			ApplyCharacter(sel.first, sel.second,
				TextColorProperty(color));
			TextChanged();
			UpdateMarks();
			return;
		}
		// Styles > New Style… ('sMB6': the dialog; with a "Style Name" from
		// it, TNewStyleCommand: "New Style - " and the name)
		case 'sMB6': {
			const char* name = NULL;
			if (message->FindString(kStyleNameStr, &name) != B_OK || name == NULL) {
				if (Window() != NULL)
					(new TDialogStyleNew(Window(), fDocument.styles))->RunDialog();
				return;
			}
			uint32_t type = uint32_t(message->GetInt32(kStyleTypeStr, int32(Sig("Para"))));
			std::string baseName = message->GetString("Style Base", "");
			uint32_t base = kNoPart;
			if (!baseName.empty())
				base = FindNamedStyle(fDocument.styles, baseName);
			// "Copy formatting from selection": the text's style where the
			// selection starts
			uint32_t from = kNoPart;
			if (message->GetBool("Inherit", false) && fCaret.active) {
				if (const WPPart* wp = fDocument.WordPart(fCaret.part)) {
					TextPosition at = TextSelectionRange().first;
					if (at.paragraph < wp->paragraphs.size()) {
						const WPParagraph& p = wp->paragraphs[at.paragraph];
						from = p.parStyle;
						for (const WPStyleRun& r : p.runs)
							if (r.position <= at.offset)
								from = r.style;
					}
				}
			}
			fUndo.BeginDocument(fDocument, std::string(GobeResources::IndexString(3, 8).String())
				+ name, CurrentViewState());
			if (NewNamedStyle(fDocument, type, name, base, from) == kNoPart) {
				beep();
				return;
			}
			if (Window() != NULL)
				Window()->PostMessage(kMsgDocumentChanged);
			return;
		}
		// Edit Styles' Style > Modify Style… (TStyleManager::DoModifyStyle:
		// with a "Style Name" TModifyStyleCommand, "Modify Style - " and the
		// style; else the dialog) and Edit > Clear Style (DoDeleteStyle:
		// TDeleteStyleCommand, "Delete Style - " and the style, if no style
		// comes from it, else STR# 6, 43)
		case 'sMB7':
		case 'sMB5': {
			const char* target = NULL;
			if (message->FindString("Target Style", &target) != B_OK || target == NULL)
				return;
			uint32_t style = FindNamedStyle(fDocument.styles, target);
			if (style >= fDocument.styles.styles.size()) {
				if (message->what == 'sMB7')
					(new BAlert("", GobeResources::IndexString(6, 36).String(), "OK"))->Go(NULL);
				return;
			}
			if (message->what == 'sMB5') {
				if (!CanDeleteStyle(fDocument.styles, style)) {
					(new BAlert("", GobeResources::IndexString(6, 43).String(), "OK"))->Go(NULL);
					return;
				}
				fUndo.BeginDocument(fDocument, std::string(GobeResources::IndexString(3, 9).String())
					+ target, CurrentViewState());
				DeleteNamedStyle(fDocument, target);
				if (Window() != NULL)
					Window()->PostMessage(kMsgDocumentChanged);
				return;
			}
			const char* name = NULL;
			if (message->FindString(kStyleNameStr, &name) != B_OK || name == NULL) {
				if (Window() != NULL) {
					StylesSnapshot snapshot;
					if (TDocWindow* window = dynamic_cast<TDocWindow*>(Window()))
						window->GetStylesSnapshot(snapshot);
					(new TDialogStyleModify(Window(), target, fDocument.styles,
						snapshot.types))->RunDialog();
				}
				return;
			}
			std::string base = message->GetString("Style Base", "");
			// (TModifyStyleCommand: STR# 5, 4, "None", is no base)
			if (base == GobeResources::IndexString(5, 4).String())
				base.clear();
			if (!CanModifyNamedStyle(fDocument.styles, target, name, base))
				return;
			fUndo.BeginDocument(fDocument, std::string(GobeResources::IndexString(3, 10).String())
				+ target, CurrentViewState());
			ModifyNamedStyle(fDocument, target, name, base);
			TextChanged();
			// TModifyStyleCommand::Swap: Edit Styles updated (UpdateFloaters),
			// then showing it (SetEditStyle)
			if (TDialogStylesEdit* edit = TDialogStylesEdit::Open()) {
				TDialogStylesEdit::Refresh();
				BMessage show('NAME');
				show.AddString("Name", name);
				edit->PostMessage(&show);
			}
			return;
		}
		// the format bar's Styles, a paragraph style ('sf0L' with "Style
		// Name": TWPApplyStyleCmd, "Apply Style - " and the name)
		case 'sf0L': {
			// (TStyleManager::HandleMessage: not while editing a style,
			// STR# 6, 44)
			if (EditStyle() < fDocument.styles.styles.size()) {
				(new BAlert("", GobeResources::IndexString(6, 44).String(), "OK"))->Go(NULL);
				return;
			}
			const char* name = NULL;
			if (!fCaret.active || fDocument.WordPart(fCaret.part) == NULL
					|| message->FindString(kStyleNameStr, &name) != B_OK || name == NULL) {
				beep();
				return;
			}
			uint32_t named = UINT32_MAX;
			for (size_t i = 0; i < fDocument.styles.styles.size(); i++)
				if (fDocument.styles.styles[i].name == name
						&& fDocument.styles.styles[i].value10 == Sig("Para")
						&& !(fDocument.styles.styles[i].value8 & kStyleDeleted))
					named = uint32_t(i);
			if (named == UINT32_MAX) {
				beep();
				return;
			}
			fUndo.Begin(fDocument, fCaret.part, std::string(GobeResources::IndexString(3, 0).String())
				+ name, CurrentViewState());
			auto range = TextSelectionRange();
			if (ApplyParagraphStyle(fDocument, fCaret.part, range.first.paragraph,
					range.second.paragraph, named))
				TextChanged();
			UpdateMarks();
			return;
		}
		// Alignment menu (MENU 300, value message 300): 0 left, 1 center,
		// 2 right, 3 justify; TextStyle has 1 right, 2 center
		case 300: {
			if (!fCaret.active) {
				beep();
				return;
			}
			int32 align = 0;
			if (message->FindInt32(kValueString, &align) != B_OK) {
				beep();
				return;
			}
			fUndo.Begin(fDocument, fCaret.part, "Alignment", CurrentViewState());
			auto range = TextSelectionRange();
			ApplyParagraph(range.first.paragraph,
				range.second.paragraph, AlignmentProperty(align));
			TextChanged();
			UpdateMarks();
			return;
		}
		default: {
			static const uint32 kCmdClear = 'sM12';
			// cell selection: handle before text handling
			if (fSelectedCell.active && fCellEditor == NULL) {
				int r0 = std::min(fSelectedCell.row, fSelectedCell.anchorRow);
				int r1 = std::max(fSelectedCell.row, fSelectedCell.anchorRow);
				int c0 = std::min(fSelectedCell.column, fSelectedCell.anchorColumn);
				int c1 = std::max(fSelectedCell.column, fSelectedCell.anchorColumn);
				switch (message->what) {
					case B_COPY: {
						SSPart* ss = fDocument.EditableSpreadsheetPart(fSelectedCell.part);
						if (ss != NULL) {
							std::string text = RangeInput(*ss, uint16_t(r0), uint16_t(c0),
								uint16_t(r1), uint16_t(c1));
							be_clipboard->Lock();
							be_clipboard->Clear();
							be_clipboard->Data()->AddData("text/plain", B_MIME_TYPE,
								text.data(), text.size());
							be_clipboard->Commit();
							be_clipboard->Unlock();
						}
						return;
					}
					case B_CUT: {
						fUndo.Begin(fDocument, fSelectedCell.part, "Cut",
							CurrentViewState());
						SSPart* ss = fDocument.EditableSpreadsheetPart(fSelectedCell.part);
						if (ss != NULL) {
							std::string text = RangeInput(*ss, uint16_t(r0), uint16_t(c0),
								uint16_t(r1), uint16_t(c1));
							be_clipboard->Lock();
							be_clipboard->Clear();
							be_clipboard->Data()->AddData("text/plain", B_MIME_TYPE,
								text.data(), text.size());
							be_clipboard->Commit();
							be_clipboard->Unlock();
							std::string error;
							if (ClearRange(*ss, uint16_t(r0), uint16_t(c0),
									uint16_t(r1), uint16_t(c1))) {
								fBook->Reset();
								if (Window() != NULL)
									Window()->PostMessage(kMsgDocumentChanged);
							}
						}
						return;
					}
					case B_PASTE: {
						fUndo.Begin(fDocument, fSelectedCell.part, "Paste",
							CurrentViewState());
						if (!be_clipboard->Lock())
							return;
						const void* data = NULL;
						ssize_t size = 0;
						bool ok = false;
						if (be_clipboard->Data()->FindData("text/plain", B_MIME_TYPE,
								&data, &size) == B_OK && size > 0) {
							std::string text((const char*)data, size);
							be_clipboard->Unlock();
							SSPart* ss = fDocument.EditableSpreadsheetPart(fSelectedCell.part);
							if (ss != NULL) {
								std::string error;
								auto isName = [&](const std::string& n) {
									return fDocument.NamedSelection(n,
										int(fSelectedCell.part)) != nullptr;
								};
								uint16_t rows, columns;
								if (PasteInput(*ss, uint16_t(r0), uint16_t(c0), text,
										isName, error, rows, columns)) {
									fBook->Reset();
									if (Window() != NULL)
										Window()->PostMessage(kMsgDocumentChanged);
									// select the pasted block
									fSelectedCell.anchorRow = fSelectedCell.row = r0;
									fSelectedCell.anchorColumn = fSelectedCell.column = c0;
									fSelectedCell.row = r0 + int(rows) - 1;
									fSelectedCell.column = c0 + int(columns) - 1;
									ok = true;
								} else {
									(new BAlert("Formula",
										("The formula has an error: " + error).c_str(),
										"OK"))->Go(NULL);
								}
							}
						} else {
							be_clipboard->Unlock();
						}
						if (ok)
							Invalidate();
						return;
					}
					case kCmdClear:
						{
							fUndo.Begin(fDocument, fSelectedCell.part, "Clear",
								CurrentViewState());
							SSPart* ss = fDocument.EditableSpreadsheetPart(fSelectedCell.part);
							std::string error;
							if (ss != NULL && ClearRange(*ss, uint16_t(r0), uint16_t(c0),
									uint16_t(r1), uint16_t(c1))) {
								fBook->Reset();
								if (Window() != NULL)
									Window()->PostMessage(kMsgDocumentChanged);
							}
							return;
						}
					case B_SELECT_ALL: {
						const SSPart* ss = fDocument.SpreadsheetPart(fSelectedCell.part);
						if (ss != NULL) {
							int maxRow = 0, maxCol = 0;
							for (const SSCell& cell : ss->cells) {
								maxRow = std::max(maxRow, int(cell.id.a));
								maxCol = std::max(maxCol, int(cell.id.b));
							}
							fSelectedCell.anchorRow = 0;
							fSelectedCell.anchorColumn = 0;
							fSelectedCell.row = maxRow;
							fSelectedCell.column = maxCol;
							Invalidate();
						}
						return;
					}
					// Insert/Delete Rows and Columns
					case 'sM73': {	// Insert Cells… (rows, until its dialog)
						fUndo.Begin(fDocument, fSelectedCell.part, "Insert Rows",
							CurrentViewState());
						InsertRowsColumns(fDocument, fSelectedCell.part, false,
							uint16_t(r0), r1 - r0 + 1);
						fBook->Reset();
						Window()->PostMessage(kMsgDocumentChanged);
						return;
					}
					case 'sM74': {	// Delete Cells… (rows, until its dialog)
						fUndo.Begin(fDocument, fSelectedCell.part, "Delete Rows",
							CurrentViewState());
						InsertRowsColumns(fDocument, fSelectedCell.part, false,
							uint16_t(r0), -(r1 - r0 + 1));
						fBook->Reset();
						Window()->PostMessage(kMsgDocumentChanged);
						return;
					}
					case kCmdInsertColumns: {
						fUndo.Begin(fDocument, fSelectedCell.part, "Insert Columns",
							CurrentViewState());
						InsertRowsColumns(fDocument, fSelectedCell.part, true,
							uint16_t(c0), c1 - c0 + 1);
						fBook->Reset();
						Window()->PostMessage(kMsgDocumentChanged);
						return;
					}
					case kCmdDeleteColumns: {
						fUndo.Begin(fDocument, fSelectedCell.part, "Delete Columns",
							CurrentViewState());
						InsertRowsColumns(fDocument, fSelectedCell.part, true,
							uint16_t(c0), -(c1 - c0 + 1));
						fBook->Reset();
						Window()->PostMessage(kMsgDocumentChanged);
						return;
					}
					// Fill Down / Fill Right
					case 'sM70': {	// Fill Down
						fUndo.Begin(fDocument, fSelectedCell.part, "Fill Down",
							CurrentViewState());
						FillRange(*fDocument.EditableSpreadsheetPart(fSelectedCell.part),
							uint16_t(r0), uint16_t(c0), uint16_t(r1), uint16_t(c1),
							false);
						fBook->Reset();
						Window()->PostMessage(kMsgDocumentChanged);
						return;
					}
					case 'sM71': {	// Fill Right
						fUndo.Begin(fDocument, fSelectedCell.part, "Fill Right",
							CurrentViewState());
						FillRange(*fDocument.EditableSpreadsheetPart(fSelectedCell.part),
							uint16_t(r0), uint16_t(c0), uint16_t(r1), uint16_t(c1),
							true);
						fBook->Reset();
						Window()->PostMessage(kMsgDocumentChanged);
						return;
					}
					case kCmdCellHAlign: {	// Cell horizontal alignment
						int32 align = 0;
						if (message->FindInt32(kValueString, &align) != B_OK) {
							beep();
							return;
						}
						fUndo.Begin(fDocument, fSelectedCell.part, "Cell Align",
							CurrentViewState());
						ApplyCellProperty(fDocument, fSelectedCell.part,
							uint16_t(r0), uint16_t(c0), uint16_t(r1), uint16_t(c1),
							CellHAlignProperty(align));
						fBook->Reset();
						Window()->PostMessage(kMsgDocumentChanged);
						return;
					}
					case kCmdCellVAlign: {	// Cell vertical alignment
						int32 align = 0;
						if (message->FindInt32(kValueString, &align) != B_OK) {
							beep();
							return;
						}
						fUndo.Begin(fDocument, fSelectedCell.part, "Cell Align",
							CurrentViewState());
						ApplyCellProperty(fDocument, fSelectedCell.part,
							uint16_t(r0), uint16_t(c0), uint16_t(r1), uint16_t(c1),
							CellVAlignProperty(align));
						fBook->Reset();
						Window()->PostMessage(kMsgDocumentChanged);
						return;
					}
					case kCmdCellWrap: {	// Cell wrap
						bool wrap = false;
						if (message->FindBool("value", &wrap) != B_OK) {
							// Try kValueString as int32 too
							int32 val = 0;
							if (message->FindInt32(kValueString, &val) == B_OK)
								wrap = (val != 0);
							else
								beep();
							return;
						}
						fUndo.Begin(fDocument, fSelectedCell.part, "Cell Wrap",
							CurrentViewState());
						ApplyCellProperty(fDocument, fSelectedCell.part,
							uint16_t(r0), uint16_t(c0), uint16_t(r1), uint16_t(c1),
							CellWrapProperty(wrap));
						fBook->Reset();
						Window()->PostMessage(kMsgDocumentChanged);
						return;
					}
					case kCmdCellFormat: {	// Cell number format
						const char* format = NULL;
						if (message->FindString(kValueString, &format) != B_OK
							|| format == NULL) {
							beep();
							return;
						}
						fUndo.Begin(fDocument, fSelectedCell.part, "Cell Format",
							CurrentViewState());
						ApplyCellProperty(fDocument, fSelectedCell.part,
							uint16_t(r0), uint16_t(c0), uint16_t(r1), uint16_t(c1),
							NumberFormatProperty(format));
						fBook->Reset();
						Window()->PostMessage(kMsgDocumentChanged);
						return;
					}
					case kCmdCellFill: {	// Cell fill color
						int32 r = 0, g = 0, b = 0;
						if (message->FindInt32("red", &r) != B_OK
							|| message->FindInt32("green", &g) != B_OK
							|| message->FindInt32("blue", &b) != B_OK) {
							beep();
							return;
						}
						Color col{uint8_t(r), uint8_t(g), uint8_t(b), 255};
						fUndo.Begin(fDocument, fSelectedCell.part, "Cell Fill",
							CurrentViewState());
						ApplyCellProperty(fDocument, fSelectedCell.part,
							uint16_t(r0), uint16_t(c0), uint16_t(r1), uint16_t(c1),
							CellFillProperty(col));
						fBook->Reset();
						Window()->PostMessage(kMsgDocumentChanged);
						return;
					}
				}
			}
			if (fCaret.active) {
				// the Edit menu (MENU 3) sends B_CUT, B_COPY, B_PASTE, B_SELECT_ALL
				switch (message->what) {
					case B_CUT: {
						fUndo.Begin(fDocument, fCaret.part, "Cut",
							CurrentViewState());
						CopySelection();
						if (DeleteSelection())
							TextChanged();
						return;
					}
					case B_COPY:
						CopySelection();
						return;
					case B_PASTE: {
						fUndo.Begin(fDocument, fCaret.part, "Paste",
							CurrentViewState());
						PasteText();
						return;
					}
					case kCmdClear: {
						fUndo.Begin(fDocument, fCaret.part, "Clear",
							CurrentViewState());
						if (DeleteSelection())
							TextChanged();
						return;
					}
				}
			}
			// B_SELECT_ALL works even when the caret isn't active yet
			// (e.g. when the document is first opened)
			if (message->what == B_SELECT_ALL) {
				SelectAllText();
				return;
			}
			BView::MessageReceived(message);
			return;
		}
	}
}


// Test hook (--do): commands separated by ';': "click x y", "dclick x y",
// "sclick x y" (shift-click), "tclick x y" (triple-click),
// "key text" (with {enter} {bs} {del} {left} {right} {up} {down} {tab}
// {esc} {home} {end} {sleft} {sright}), "selectall", "copy", "cut", "paste",
// "undo", "redo", "cellrect row col", "cell row col", "celltext row col",
// "selection", "commit" (the cell editor), "save path",
// "cellformat fmt", "cellfill R G B", "cellalign H V", "cellwrap 0|1",
// "cellstyle name".
// "copy", "cut", "paste", "selectall", "{sup}", "{sdown}", "{sleft}",
// "{sright}" work for cell selection too.
// "cellfill_check R C" — check if cell (R,C) has a solid fill.
// "ruler ..." — the window's text ruler (TWPRuler::RunScript).
// "partbar ABCD", "partbar spacing text" — the part bar (TPartBar).
// "formatbar show|color N|style NAME" — the format bar (TFormatBar).
// "spell top|cursor|skip|learn|replace|toggle|check|choose N|show|
// dictionaries" — the SpellBar (Spell Check's commands 'sMC0'..'sMC6',
// "check" the PartBar's button; "show" prints it and the selection).
void
TPageView::RunScript(const std::string& script, Document& document)
{
	size_t start = 0;
	while (start <= script.size()) {
		size_t end = script.find(';', start);
		if (end == std::string::npos)
			end = script.size();
		std::string command = script.substr(start, end - start);
		start = end + 1;
		while (!command.empty() && command[0] == ' ')
			command.erase(0, 1);
		// "wait": the rest of the script a moment later, after what was
		// posted (floaters, dialogs) is handled
		if (command == "wait") {
			if (Window() != NULL && start < script.size()) {
				BMessage rest(kMsgRunScript);
				rest.AddString("script", script.substr(start).c_str());
				BMessageRunner::StartSending(BMessenger(Window()), &rest, 400000, 1);
			}
			return;
		}
		if (command.compare(0, 10, "formatbar ") == 0) {
			// "formatbar show": the labels and the text ink shown;
			// "formatbar color N", "formatbar style NAME": chosen
			if (TFormatBar* bar = dynamic_cast<TFormatBar*>(fFormatBar)) {
				bar->Update();
				std::string what = command.substr(10);
				if (what == "show") {
					printf("formatbar font \"%s\" size \"%s\" style \"%s\" ink %s\n",
						bar->Shown("font").c_str(), bar->Shown("size").c_str(),
						bar->Shown("style").c_str(), bar->Shown("ink").c_str());
					fflush(stdout);
				} else if (what.compare(0, 6, "color ") == 0)
					bar->ChooseColor(atoi(what.c_str() + 6));
				else if (what.compare(0, 6, "style ") == 0)
					bar->ChooseStyle(what.c_str() + 6);
			}
		} else if (command.compare(0, 8, "context ") == 0
				|| command.compare(0, 12, "contextpick ") == 0) {
			// "context x y": the context menu there printed; "contextpick
			// x y label": its item (or a submenu's) chosen
			bool pick = command[7] == 'p';
			float x, y;
			int n = 0;
			if (sscanf(command.c_str() + (pick ? 12 : 8), "%f %f %n", &x, &y, &n) < 2)
				continue;
			std::string label = pick ? command.substr((pick ? 12 : 8) + size_t(n)) : "";
			BPopUpMenu* menu = ContextMenu(BPoint(x, y));
			std::string shown;
			BMenuItem* chosen = NULL;
			for (int32 i = 0; menu != NULL && i < menu->CountItems(); i++) {
				BMenuItem* item = menu->ItemAt(i);
				shown += std::string(i > 0 ? "|" : "") + (item->Label()[0] != '\0' ? item->Label()
					: "-") + (item->IsEnabled() ? "" : "(off)");
				if (label == item->Label())
					chosen = item;
				if (BMenu* sub = item->Submenu()) {
					shown += "[";
					for (int32 j = 0; j < sub->CountItems(); j++) {
						shown += std::string(j > 0 ? "," : "") + sub->ItemAt(j)->Label();
						if (label == sub->ItemAt(j)->Label())
							chosen = sub->ItemAt(j);
					}
					shown += "]";
				}
			}
			if (!pick) {
				printf("context %s\n", shown.c_str());
				fflush(stdout);
			} else if (chosen != NULL && chosen->Message() != NULL) {
				BMessage copy(*chosen->Message());
				if (HandlesCommand(copy.what))
					MessageReceived(&copy);
				else if (Window() != NULL)
					Window()->PostMessage(&copy);
			}
			delete menu;
		} else if (command.compare(0, 6, "spell ") == 0) {
			TSpellBar* bar = dynamic_cast<TSpellBar*>(fSpellBar);
			std::string what = command.substr(6);
			static const char* const kCommands[] = {"top", "cursor", "skip", "learn", "replace",
				"toggle", "check"};
			for (int32 i = 0; bar != NULL && i < 7; i++) {
				if (what != kCommands[i])
					continue;
				BMessage message('sMC0' + uint32(std::min<int32>(i, 5)));
				if (i == 6)
					message.AddBool("dospell", true);
				bar->HandleCommand(&message);
			}
			if (what == "dictionaries" && Window() != NULL)
				Window()->PostMessage('sMC6');
			if (bar != NULL && what.compare(0, 7, "choose ") == 0)
				bar->ChooseReplacement(atoi(what.c_str() + 7));
			if (bar != NULL && what == "show") {
				bar->Update();
				std::string replacements;
				for (const std::string& r : bar->Replacements())
					replacements += (replacements.empty() ? "" : "|") + r;
				TextPosition from, to;
				std::string selected;
				const WPPart* wp = document.WordPart(fCaret.part);
				if (wp != NULL && TextSelection(from, to))
					selected = RangeText(*wp, from, to);
				printf("spell %s status \"%s\" skip %d learn %d replace %d menu \"%s\" "
					"selection %zu %zu %zu \"%s\"\n", bar->IsHidden(bar) ? "hidden" : "shown",
					bar->Status().String(), bar->SkipEnabled(), bar->LearnEnabled(),
					bar->ReplaceEnabled(), replacements.c_str(), fCaret.part, fCaret.paragraph,
					fCaret.offset, selected.c_str());
				fflush(stdout);
			}
		} else if (command.compare(0, 8, "partbar ") == 0) {
			// "partbar ABCD": the part bar's element 'ABCD' clicked;
			// "partbar spacing text": the line spacing typed
			if (TPartBar* bar = dynamic_cast<TPartBar*>(fPartBar)) {
				bar->Update();
				if (command.compare(8, 8, "spacing ") == 0)
					bar->TypeSpacing(command.c_str() + 16);
				else if (command.size() == 12) {
					const char* c = command.c_str() + 8;
					uint32 id = uint32(uint8(c[0])) << 24 | uint32(uint8(c[1])) << 16
						| uint32(uint8(c[2])) << 8 | uint8(c[3]);
					if (!bar->Click(id)) {
						printf("partbar %s: not clickable\n", c);
						fflush(stdout);
					}
				}
			}
		} else if (command.compare(0, 6, "ruler ") == 0) {
			if (fRuler != NULL)
				fRuler->RunScript(command.substr(6));
		} else if (command.compare(0, 6, "click ") == 0) {
			float x, y;
			if (sscanf(command.c_str() + 6, "%f %f", &x, &y) == 2)
				Click(BPoint(x, y), 1, B_PRIMARY_MOUSE_BUTTON);
		} else if (command.compare(0, 7, "dclick ") == 0) {
			float x, y;
			if (sscanf(command.c_str() + 7, "%f %f", &x, &y) == 2)
				Click(BPoint(x, y), 2, B_PRIMARY_MOUSE_BUTTON);
		} else if (command.compare(0, 7, "sclick ") == 0) {
			float x, y;
			if (sscanf(command.c_str() + 7, "%f %f", &x, &y) == 2) {
				fScriptShift = true;
				Click(BPoint(x, y), 1, B_PRIMARY_MOUSE_BUTTON);
				fScriptShift = false;
			}
		} else if (command.compare(0, 7, "tclick ") == 0) {
			float x, y;
			if (sscanf(command.c_str() + 7, "%f %f", &x, &y) == 2)
				Click(BPoint(x, y), 3, B_PRIMARY_MOUSE_BUTTON);
		} else if (command == "selectall") {
			BMessage msg(B_SELECT_ALL);
			// dispatch via MessageReceived for both cell and text selection
			MessageReceived(&msg);
		} else if (command == "copy") {
			BMessage msg(B_COPY);
			MessageReceived(&msg);
		} else if (command == "cut") {
			BMessage msg(B_CUT);
			MessageReceived(&msg);
		} else if (command == "paste") {
			BMessage msg(B_PASTE);
			MessageReceived(&msg);
		} else if (command.size() >= 9 && command.substr(0, 9) == "cellrect ") {
			int row, column;
			if (sscanf(command.c_str() + 9, "%d %d", &row, &column) == 2) {
				BRect r = CellViewRect(fSelectedCell.part, row, column);
				if (r.IsValid())
					fprintf(stderr, "cellrect %d %d %.0f,%.0f-%.0f,%.0f\n",
						row, column, r.left, r.top, r.right, r.bottom);
				else
					fprintf(stderr, "cellrect %d %d (not visible)\n", row, column);
			}
		} else if (command.compare(0, 6, "prefs ") == 0) {
			// "prefs N": the Preferences dialog shows panel N; "prefs
			// press S": its control S pressed; "prefs button B"
			std::string what = command.substr(6);
			if (what.compare(0, 6, "press ") == 0)
				TDialogPrefs::Press(atoi(what.c_str() + 6));
			else if (what.compare(0, 7, "button ") == 0)
				TDialogPrefs::Press(-1, atoi(what.c_str() + 7));
			else
				TDialogPrefs::SelectPanel(atoi(what.c_str()));
		} else if (command.compare(0, 8, "history ") == 0) {
			// "history N": row N chosen in the History floater
			TDialogHistory::Choose(atoi(command.c_str() + 8));
		} else if (command.compare(0, 11, "editstyles ") == 0) {
			// "editstyles row N" (or "invoke N", double-clicked): row N of
			// the Edit Styles floater's list chosen; "editstyles cmd ABCD":
			// one of its menus' commands; "editstyles dump": its rows and
			// properties printed
			std::string what = command.substr(11);
			if (what.compare(0, 4, "row ") == 0)
				TDialogStylesEdit::ChooseRow(atoi(what.c_str() + 4));
			else if (what.compare(0, 7, "invoke ") == 0)
				TDialogStylesEdit::ChooseRow(atoi(what.c_str() + 7), true);
			else if (what.compare(0, 4, "cmd ") == 0 && what.size() >= 8)
				TDialogStylesEdit::Command(uint32(uint8(what[4])) << 24
					| uint32(uint8(what[5])) << 16 | uint32(uint8(what[6])) << 8
					| uint8(what[7]));
			else if (what == "dump")
				TDialogStylesEdit::Dump();
		} else if (command == "caret") {
			// "caret": the caret and the anchor (part, paragraph, offset)
			printf("caret %zu %zu.%zu anchor %zu.%zu\n", fCaret.part, fCaret.paragraph,
				fCaret.offset, fAnchor.paragraph, fAnchor.offset);
			fflush(stdout);
		} else if (command == "selection") {
			if (!fSelectedCell.active) {
				fprintf(stderr, "cells none\n");
			} else {
				int r0 = std::min(fSelectedCell.row, fSelectedCell.anchorRow);
				int r1 = std::max(fSelectedCell.row, fSelectedCell.anchorRow);
				int c0 = std::min(fSelectedCell.column, fSelectedCell.anchorColumn);
				int c1 = std::max(fSelectedCell.column, fSelectedCell.anchorColumn);
				fprintf(stderr, "cells part %zu rows %d-%d columns %d-%d active %d,%d\n",
					fSelectedCell.part, r0, r1, c0, c1,
					fSelectedCell.row, fSelectedCell.column);
			}
		} else if (command.compare(0, 4, "key ") == 0) {
			std::string keys = command.substr(4);
			// {c...}: with Command, {o...}: Option, {k...}: Control
			static const struct { const char* name; char key; uint32 mods; } kNames[] = {
				{"{enter}", B_ENTER, 0}, {"{bs}", B_BACKSPACE, 0}, {"{del}", B_DELETE, 0},
				{"{left}", B_LEFT_ARROW, 0}, {"{right}", B_RIGHT_ARROW, 0},
				{"{up}", B_UP_ARROW, 0}, {"{down}", B_DOWN_ARROW, 0}, {"{tab}", B_TAB, 0},
				{"{esc}", B_ESCAPE, 0}, {"{home}", B_HOME, 0}, {"{end}", B_END, 0},
				{"{sleft}", B_LEFT_ARROW, B_SHIFT_KEY}, {"{sright}", B_RIGHT_ARROW, B_SHIFT_KEY},
				{"{sup}", B_UP_ARROW, B_SHIFT_KEY}, {"{sdown}", B_DOWN_ARROW, B_SHIFT_KEY},
				{"{cleft}", B_LEFT_ARROW, B_COMMAND_KEY}, {"{cright}", B_RIGHT_ARROW, B_COMMAND_KEY},
				{"{cup}", B_UP_ARROW, B_COMMAND_KEY}, {"{cdown}", B_DOWN_ARROW, B_COMMAND_KEY},
				{"{oleft}", B_LEFT_ARROW, B_OPTION_KEY}, {"{oright}", B_RIGHT_ARROW, B_OPTION_KEY},
				{"{khome}", B_HOME, B_CONTROL_KEY}, {"{kend}", B_END, B_CONTROL_KEY},
				{"{pgup}", B_PAGE_UP, 0}, {"{pgdn}", B_PAGE_DOWN, 0}};
			for (size_t i = 0; i < keys.size();) {
				char key = keys[i];
				size_t length = 1;
				uint32 mods = 0;
				for (const auto& n : kNames)
					if (keys.compare(i, strlen(n.name), n.name) == 0) {
						key = n.key;
						length = strlen(n.name);
						mods = n.mods;
					}
				bool shifted = (mods & B_SHIFT_KEY) != 0;
				if (length == 1 && (key & 0x80)) {	// a UTF-8 sequence
					while (i + length < keys.size() && (keys[i + length] & 0xc0) == 0x80)
						length++;
				}
				std::string bytes = length > 1 && key == keys[i] ? keys.substr(i, length)
					: std::string(1, key);
				if (fCellEditor != NULL) {
					if (key == B_ENTER)
						EndCellEdit(true, 1, 0);
					else if (key == B_ESCAPE)
						EndCellEdit(false);
					else if (key == B_TAB)
						EndCellEdit(true, 0, 1);
					else {
						int32 from, to;
						fCellEditor->GetSelection(&from, &to);
						if (from != to)
							fCellEditor->Delete();
						fCellEditor->Insert(bytes.c_str());
					}
				} else {
					fScriptShift = shifted;
					fScriptModifiers = mods & ~uint32(B_SHIFT_KEY);
					KeyDown(bytes.c_str(), int32(bytes.size()));
					fScriptShift = false;
					fScriptModifiers = 0;
				}
				i += length;
			}
		} else if (command == "lines") {
			for (const Line& line : fLines) {
				std::string text;
				for (const Line::Piece& p : line.pieces)
					text += p.text + " ";
				fprintf(stderr, "line part %zu par %zu [%zu,%zu) %.0f,%.0f-%.0f,%.0f: %.50s\n",
					line.part, line.paragraph, line.from, line.to, line.frame.left,
					line.frame.top, line.frame.right, line.frame.bottom, text.c_str());
			}
		} else if (command == "commit") {
			EndCellEdit(true);
		} else if (command.compare(0, 5, "send ") == 0 && command.size() >= 9) {
			// "send ABCD [name=value ...]": the command to the window with
			// fields (digits an int32, true/false a bool, else a string);
			// fields with spaces separated by '|' instead
			const char* c = command.c_str() + 5;
			BMessage msg(uint32(c[0]) << 24 | uint32(c[1]) << 16 | uint32(c[2]) << 8 | c[3]);
			std::string rest = command.size() > 10 ? command.substr(10) : "";
			char separator = rest.find('|') != std::string::npos ? '|' : ' ';
			size_t at = 0;
			while (at < rest.size()) {
				size_t space = rest.find(separator, at);
				std::string field = rest.substr(at, space == std::string::npos ? std::string::npos
					: space - at);
				at = space == std::string::npos ? rest.size() : space + 1;
				size_t equals = field.find('=');
				if (equals == std::string::npos)
					continue;
				std::string key = field.substr(0, equals), value = field.substr(equals + 1);
				if (value == "true" || value == "false")
					msg.AddBool(key.c_str(), value == "true");
				else if (!value.empty() && value.find_first_not_of("-0123456789") == std::string::npos)
					msg.AddInt32(key.c_str(), atoi(value.c_str()));
				else
					msg.AddString(key.c_str(), value.c_str());
			}
			if (Window() != NULL)
				Window()->MessageReceived(&msg);
		} else if (command.compare(0, 8, "findcmd ") == 0 && command.size() >= 12) {
			// "findcmd ABCD [find=..|replace=..|case=0/1|word=0/1|wrap=0/1]":
			// Find/Change's command 'ABCD' with TFindReplace::DoMessage's
			// fields, to the window; then the selection printed
			const char* c = command.c_str() + 8;
			BMessage msg(uint32(c[0]) << 24 | uint32(c[1]) << 16 | uint32(c[2]) << 8 | c[3]);
			std::string args = command.size() > 13 ? command.substr(13) : "";
			for (size_t at = 0; at < args.size();) {
				size_t bar = args.find('|', at);
				std::string field = args.substr(at, bar == std::string::npos ? std::string::npos
					: bar - at);
				at = bar == std::string::npos ? args.size() : bar + 1;
				size_t equals = field.find('=');
				if (equals == std::string::npos)
					continue;
				std::string key = field.substr(0, equals), value = field.substr(equals + 1);
				// \n, \t in the script: the characters
				std::string raw;
				for (size_t i = 0; i < value.size(); i++) {
					if (value[i] == '\\' && i + 1 < value.size() && (value[i + 1] == 'n'
							|| value[i + 1] == 't')) {
						raw += value[++i] == 'n' ? '\n' : '\t';
					} else
						raw += value[i];
				}
				if (key == "find" || key == "replace")
					msg.AddString(key.c_str(), raw.c_str());
				else if (key == "case")
					msg.AddBool("caseSens", raw == "1");
				else if (key == "word")
					msg.AddBool("wholeWord", raw == "1");
				else if (key == "wrap")
					msg.AddBool("wrap", raw == "1");
			}
			if (Window() != NULL && dynamic_cast<TDocWindow*>(Window()) != NULL)
				Window()->MessageReceived(&msg);
			TextPosition from, to;
			std::string selected;
			const WPPart* wp = document.WordPart(fCaret.part);
			bool any = wp != NULL && TextSelection(from, to);
			if (any)
				selected = RangeText(*wp, from, to);
			printf("findcmd %s: part %zu %zu.%zu-%zu.%zu \"%s\"\n", std::string(c, 4).c_str(),
				fCaret.part, any ? from.paragraph : fCaret.paragraph, any ? from.offset
				: fCaret.offset, any ? to.paragraph : fCaret.paragraph, any ? to.offset
				: fCaret.offset, selected.c_str());
			fflush(stdout);
		} else if (command.compare(0, 7, "window ") == 0 && command.size() == 11) {
			// "window ABCD": the command 'ABCD' as a menu sends it, to the
			// window (only in a document window)
			const char* c = command.c_str() + 7;
			BMessage msg(uint32(c[0]) << 24 | uint32(c[1]) << 16 | uint32(c[2]) << 8 | c[3]);
			if (Window() != NULL && dynamic_cast<TDocWindow*>(Window()) != NULL)
				Window()->MessageReceived(&msg);
			else
				fprintf(stderr, "window: not in a document window\n");
		} else if (command.compare(0, 8, "special ") == 0 && command.size() == 12) {
			// "special sM52": a Format > Insert command
			const char* c = command.c_str() + 8;
			BMessage msg(uint32(c[0]) << 24 | uint32(c[1]) << 16 | uint32(c[2]) << 8 | c[3]);
			MessageReceived(&msg);
		} else if (command == "headerfooter 0" || command == "headerfooter 1") {
			// Document > Insert/Remove Header (0), Footer (1)
			ToggleHeaderFooter(command.back() - '0');
		} else if (command == "undo") {
			BMessage msg('sM10');
			MessageReceived(&msg);
		} else if (command == "redo") {
			BMessage msg('sM11');
			MessageReceived(&msg);
		} else if (command == "insertrows") {
			BMessage msg('sM73');
			MessageReceived(&msg);
		} else if (command == "deleterows") {
			BMessage msg('sM74');
			MessageReceived(&msg);
		} else if (command == "insertcolumns") {
			BMessage msg(kCmdInsertColumns);
			MessageReceived(&msg);
		} else if (command == "deletecolumns") {
			BMessage msg(kCmdDeleteColumns);
			MessageReceived(&msg);
		} else if (command == "filldown") {
			BMessage msg('sM70');
			MessageReceived(&msg);
		} else if (command == "fillright") {
			BMessage msg('sM71');
			MessageReceived(&msg);
		} else if (command.compare(0, 11, "cellformat ") == 0) {
			// "cellformat <format>" — apply number format to selection
			if (!fSelectedCell.active) {
				fprintf(stderr, "cellformat: no selection\n");
				continue;
			}
			int r0 = std::min(fSelectedCell.row, fSelectedCell.anchorRow);
			int r1 = std::max(fSelectedCell.row, fSelectedCell.anchorRow);
			int c0 = std::min(fSelectedCell.column, fSelectedCell.anchorColumn);
			int c1 = std::max(fSelectedCell.column, fSelectedCell.anchorColumn);
			fUndo.Begin(fDocument, fSelectedCell.part, "Cell Format",
				CurrentViewState());
			ApplyCellProperty(fDocument, fSelectedCell.part,
				uint16_t(r0), uint16_t(c0), uint16_t(r1), uint16_t(c1),
				NumberFormatProperty(command.substr(11)));
			fBook->Reset();
			Window()->PostMessage(kMsgDocumentChanged);
		} else if (command.compare(0, 8, "cellfill ") == 0) {
			// "cellfill R G B" — apply fill color to selection
			if (!fSelectedCell.active) {
				fprintf(stderr, "cellfill: no selection\n");
				continue;
			}
			int r0 = std::min(fSelectedCell.row, fSelectedCell.anchorRow);
			int r1 = std::max(fSelectedCell.row, fSelectedCell.anchorRow);
			int c0 = std::min(fSelectedCell.column, fSelectedCell.anchorColumn);
			int c1 = std::max(fSelectedCell.column, fSelectedCell.anchorColumn);
			int rr, gg, bb;
			if (sscanf(command.c_str() + 8, "%d %d %d", &rr, &gg, &bb) != 3) {
				fprintf(stderr, "cellfill: expected 'R G B'\n");
				continue;
			}
			Color col{uint8_t(rr), uint8_t(gg), uint8_t(bb), 255};
			fUndo.Begin(fDocument, fSelectedCell.part, "Cell Fill",
				CurrentViewState());
			ApplyCellProperty(fDocument, fSelectedCell.part,
				uint16_t(r0), uint16_t(c0), uint16_t(r1), uint16_t(c1),
				CellFillProperty(col));
			fBook->Reset();
			Window()->PostMessage(kMsgDocumentChanged);
		} else if (command.compare(0, 9, "cellalign ") == 0) {
			// "cellalign H V" — apply horizontal and vertical alignment
			if (!fSelectedCell.active) {
				fprintf(stderr, "cellalign: no selection\n");
				continue;
			}
			int r0 = std::min(fSelectedCell.row, fSelectedCell.anchorRow);
			int r1 = std::max(fSelectedCell.row, fSelectedCell.anchorRow);
			int c0 = std::min(fSelectedCell.column, fSelectedCell.anchorColumn);
			int c1 = std::max(fSelectedCell.column, fSelectedCell.anchorColumn);
			int hAlign = 0, vAlign = 0;
			if (sscanf(command.c_str() + 9, "%d %d", &hAlign, &vAlign) != 2) {
				fprintf(stderr, "cellalign: expected 'H V'\n");
				continue;
			}
			fUndo.Begin(fDocument, fSelectedCell.part, "Cell Align",
				CurrentViewState());
			ApplyCellProperty(fDocument, fSelectedCell.part,
				uint16_t(r0), uint16_t(c0), uint16_t(r1), uint16_t(c1),
				CellHAlignProperty(hAlign));
			ApplyCellProperty(fDocument, fSelectedCell.part,
				uint16_t(r0), uint16_t(c0), uint16_t(r1), uint16_t(c1),
				CellVAlignProperty(vAlign));
			fBook->Reset();
			Window()->PostMessage(kMsgDocumentChanged);
		} else if (command.compare(0, 13, "cellwrap ") == 0) {
			// "cellwrap 0|1" — toggle cell wrap
			if (!fSelectedCell.active) {
				fprintf(stderr, "cellwrap: no selection\n");
				continue;
			}
			int r0 = std::min(fSelectedCell.row, fSelectedCell.anchorRow);
			int r1 = std::max(fSelectedCell.row, fSelectedCell.anchorRow);
			int c0 = std::min(fSelectedCell.column, fSelectedCell.anchorColumn);
			int c1 = std::max(fSelectedCell.column, fSelectedCell.anchorColumn);
			int val = 0;
			if (sscanf(command.c_str() + 9, "%d", &val) != 1) {
				fprintf(stderr, "cellwrap: expected 0 or 1\n");
				continue;
			}
			fUndo.Begin(fDocument, fSelectedCell.part, "Cell Wrap",
				CurrentViewState());
			ApplyCellProperty(fDocument, fSelectedCell.part,
				uint16_t(r0), uint16_t(c0), uint16_t(r1), uint16_t(c1),
				CellWrapProperty(val != 0));
			fBook->Reset();
			Window()->PostMessage(kMsgDocumentChanged);
		} else if (command.compare(0, 9, "cellstyle ") == 0) {
			// "cellstyle <name>" — apply a cell style by name (for future use)
			if (!fSelectedCell.active) {
				fprintf(stderr, "cellstyle: no selection\n");
				continue;
			}
			int r0 = std::min(fSelectedCell.row, fSelectedCell.anchorRow);
			int r1 = std::max(fSelectedCell.row, fSelectedCell.anchorRow);
			int c0 = std::min(fSelectedCell.column, fSelectedCell.anchorColumn);
			int c1 = std::max(fSelectedCell.column, fSelectedCell.anchorColumn);
			std::string name = command.substr(9);
			// TODO: look up style by name and apply
			fprintf(stderr, "cellstyle '%s': not yet implemented\n", name.c_str());
		} else if (command.size() >= 5 && command.substr(0, 5) == "cell ") {
			int row, column;
			if (sscanf(command.c_str() + 5, "%d %d", &row, &column) == 2) {
				const SSPart* ss = fDocument.SpreadsheetPart(fSelectedCell.part);
				if (ss != NULL) {
					std::string val = CellInput(*ss, uint16_t(row), uint16_t(column));
					fprintf(stderr, "cell %d %d %s\n", row, column, val.c_str());
				} else
					fprintf(stderr, "cell %d %d (no sheet)\n", row, column);
			}
		} else if (command.size() >= 9 && command.substr(0, 9) == "celltext ") {
			// "celltext R C" — output the formatted text of cell (R,C)
			int row, column;
			if (sscanf(command.c_str() + 9, "%d %d", &row, &column) == 2) {
				const SSPart* ss = fDocument.SpreadsheetPart(fSelectedCell.part);
				if (ss != NULL) {
					const SSCell* cell = ss->Cell(row, column);
					std::vector<uint32_t> stack = {ss->style};
					if (const SSHeader* h = ss->Column(column))
						stack.push_back(h->cell.style);
					if (const SSHeader* h = ss->Row(row))
						stack.push_back(h->cell.style);
					if (cell != NULL)
						stack.push_back(cell->style);
					CellStyle resolved = ResolveCellStyle(fDocument.styles, stack);
					SSEvaluator eval(*ss, nullptr, int(fSelectedCell.part));
					SSValue value = eval.Value(row, column);
					bool red = false;
					std::string text = FormatCellValue(value, resolved.format, &red);
					fprintf(stderr, "celltext %d %d %s%s\n", row, column, text.c_str(),
						red ? " [red]" : "");
				} else
					fprintf(stderr, "celltext %d %d (no sheet)\n", row, column);
			}
		} else if (command.size() >= 15 && command.substr(0, 15) == "cellfill_check ") {
			// "cellfill_check R C" — check if cell (R,C) has a solid fill
			int row, column;
			if (sscanf(command.c_str() + 15, "%d %d", &row, &column) == 2) {
				const SSPart* ss = fDocument.SpreadsheetPart(fSelectedCell.part);
				if (ss != NULL) {
					const SSCell* cell = ss->Cell(row, column);
					std::vector<uint32_t> stack = {ss->style};
					if (const SSHeader* h = ss->Column(column))
						stack.push_back(h->cell.style);
					if (const SSHeader* h = ss->Row(row))
						stack.push_back(h->cell.style);
					if (cell != NULL)
						stack.push_back(cell->style);
					CellStyle resolved = ResolveCellStyle(fDocument.styles, stack);
					if (resolved.fill.set)
						fprintf(stderr, "cellfill_check %d %d solid %.2x %.2x %.2x\n",
							row, column, resolved.fill.color.r,
							resolved.fill.color.g, resolved.fill.color.b);
					else
						fprintf(stderr, "cellfill_check %d %d no fill\n", row, column);
				} else
					fprintf(stderr, "cellfill_check %d %d (no sheet)\n", row, column);
			}
		} else if (command.compare(0, 5, "save ") == 0) {
			std::string error;
			if (!document.Save(command.substr(5), error))
				fprintf(stderr, "save: %s\n", error.c_str());
			DocumentSaved();
			DocumentReloaded();
		} else if (command.compare(0, 5, "menu ") == 0) {
			// "menu <what> <value>" — send a text menu value message
			// what: "font", "size", "face", "align"
			// value: for font = family name, for size/face/align = integer
			const char* what = command.c_str() + 5;
			const char* space = strchr(what, ' ');
			if (space != NULL) {
				std::string whatStr(what, space - what);
				std::string valStr = space + 1;
				BMessage msg;
				if (whatStr == "font") {
					msg.what = 'sf01';
					msg.AddString("font", valStr.c_str());
				} else if (whatStr == "size") {
					msg.what = 'sf0K';
					int32 size = 0;
					sscanf(valStr.c_str(), "%d", &size);
					msg.AddInt32(kValueString, size);
				} else if (whatStr == "face") {
					msg.what = 'sf0C';
					int32 flag = 0;
					sscanf(valStr.c_str(), "%d", &flag);
					msg.AddInt32(kValueString, flag);
				} else if (whatStr == "align") {
					msg.what = 300;
					int32 align = 0;
					sscanf(valStr.c_str(), "%d", &align);
					msg.AddInt32(kValueString, align);
				} else {
					fprintf(stderr, "menu: unknown what '%s'\n", whatStr.c_str());
					continue;
				}
				MessageReceived(&msg);
			}
		} else if (command.compare(0, 6, "style ") == 0) {
			// "style P O": the style of the caret's part at paragraph P, offset O
			int p, o;
			if (sscanf(command.c_str() + 6, "%d %d", &p, &o) == 2) {
				TextStyle t = StyleAt({size_t(p), size_t(o)});
				int align = -1;
				if (const WPPart* wp = fDocument.WordPart(fCaret.part))
					if (size_t(p) < wp->paragraphs.size())
						align = ResolveTextStyle(fDocument.styles,
							wp->paragraphs[p].parStyle).alignment;
				fprintf(stderr, "style %d %d family %s size %g face %u align %d\n", p, o,
					t.family.c_str(), t.size, t.faceFlags, align);
			}
		} else if (command.compare(0, 5, "font ") == 0) {
			// "font <family>" — apply font family to current selection/caret
			const char* family = command.c_str() + 5;
			if (fCaret.active) {
				fUndo.Begin(fDocument, fCaret.part, "Font", CurrentViewState());
				auto sel = TextSelectionRange();
				ApplyCharacter(sel.first, sel.second,
					TextFontProperty(family, ""));
				TextChanged();
			}
		} else if (command.compare(0, 5, "find ") == 0) {
			// "find <text>" — find the next occurrence of text in the caret's
			// part (backwards when the command has a leading '-' prefix).
			// Selects the match (fAnchor = start, fCaret = end).
			if (!fCaret.active) {
				fprintf(stderr, "find: no caret\n");
				continue;
			}
			std::string query = command.substr(5);
			while (!query.empty() && query[0] == ' ')
				query.erase(query.begin());
			if (query.empty()) {
				fprintf(stderr, "find: no search text\n");
				continue;
			}
			FindDialogOptions opts;
			opts.caseSensitive = false;
			opts.wholeWord = false;
			opts.backwards = false;
			opts.wrap = true;
			bool backwards = false;
			if (!query.empty() && query[0] == '-') {
				backwards = true;
				query = query.substr(1);
			}
			if (backwards)
				opts.backwards = true;
			const WPPart* wp = fDocument.WordPart(fCaret.part);
			if (wp == NULL) {
				fprintf(stderr, "find: no word part\n");
				continue;
			}
			TextPosition from = {fCaret.paragraph, fCaret.offset};
			TextPosition start, endPos;
			if (FindText(*wp, from, query, opts, start, endPos)) {
				fAnchor.active = true;
				fAnchor.part = fCaret.part;
				fAnchor.paragraph = start.paragraph;
				fAnchor.offset = start.offset;
				fCaret.active = true;
				fCaret.part = fCaret.part;
				fCaret.paragraph = start.paragraph;
				fCaret.offset = endPos.offset;
				Invalidate();
			} else {
				fprintf(stderr, "find: not found\n");
			}
		} else if (command.compare(0, 11, "replaceall ") == 0) {
			// "replaceall <query>|<replacement>" — replace all matches in the
			// caret's part.
			if (!fCaret.active) {
				fprintf(stderr, "replaceall: no caret\n");
				continue;
			}
			std::string args = command.substr(11);
			size_t pipe = args.find('|');
			if (pipe == std::string::npos) {
				fprintf(stderr, "replaceall: need query|replacement\n");
				continue;
			}
			std::string query = args.substr(0, pipe);
			std::string replacement = args.substr(pipe + 1);
			WPPart* wp = fDocument.EditableWordPart(fCaret.part);
			if (wp == NULL) {
				fprintf(stderr, "replaceall: no word part\n");
				continue;
			}
			FindDialogOptions opts;
			opts.caseSensitive = false;
			opts.wholeWord = false;
			opts.backwards = false;
			opts.wrap = true;
			fUndo.Begin(fDocument, fCaret.part, GobeResources::IndexString(3, 29).String(),
				CurrentViewState());
			int count = ReplaceAll(*wp, query, replacement, opts);
			TextChanged();
			fprintf(stderr, "replaceall: %d replacements\n", count);
		} else if (command == "selected") {
			// "selected" — print the current text selection:
			// selected P1 O1 - P2 O2
			TextPosition from, to;
			if (TextSelection(from, to)) {
				fprintf(stderr, "selected %zu %zu - %zu %zu\n",
					from.paragraph, from.offset, to.paragraph, to.offset);
			} else {
				// No selection: print caret position twice
				if (fCaret.active)
					fprintf(stderr, "selected %zu %zu - %zu %zu\n",
						fCaret.paragraph, fCaret.offset,
						fCaret.paragraph, fCaret.offset);
				else
					fprintf(stderr, "selected none\n");
			}
		} else if (command.size() >= 9 && command.substr(0, 9) == "colwidth ") {
			// "colwidth <points>" — set column width of the selection
			if (!fSelectedCell.active) {
				fprintf(stderr, "colwidth: no selection\n");
				continue;
			}
			size_t part = fSelectedCell.part;
			int c0, c1;
			c0 = std::min(fSelectedCell.column, fSelectedCell.anchorColumn);
			c1 = std::max(fSelectedCell.column, fSelectedCell.anchorColumn);
			float width = 0;
			if (sscanf(command.c_str() + 9, "%f", &width) != 1) {
				fprintf(stderr, "colwidth: expected points\n");
				continue;
			}
			SSPart* ss = fDocument.EditableSpreadsheetPart(part);
			if (ss == nullptr) {
				fprintf(stderr, "colwidth: no spreadsheet part\n");
				continue;
			}
			fUndo.Begin(fDocument, part, "Column Width", CurrentViewState());
			bool changed = SetLineSize(*ss, true, uint16_t(c0), uint16_t(c1), width);
			if (changed) {
				fBook->Reset();
				Window()->PostMessage(kMsgDocumentChanged);
				fprintf(stderr, "colwidth: %.1f for columns %d-%d\n", width, c0, c1);
			}
		} else if (command.size() >= 10 && command.substr(0, 10) == "rowheight ") {
			// "rowheight <points>" — set row height of the selection
			if (!fSelectedCell.active) {
				fprintf(stderr, "rowheight: no selection\n");
				continue;
			}
			size_t part = fSelectedCell.part;
			int r0, r1;
			r0 = std::min(fSelectedCell.row, fSelectedCell.anchorRow);
			r1 = std::max(fSelectedCell.row, fSelectedCell.anchorRow);
			float height = 0;
			if (sscanf(command.c_str() + 10, "%f", &height) != 1) {
				fprintf(stderr, "rowheight: expected points\n");
				continue;
			}
			SSPart* ss = fDocument.EditableSpreadsheetPart(part);
			if (ss == nullptr) {
				fprintf(stderr, "rowheight: no spreadsheet part\n");
				continue;
			}
			fUndo.Begin(fDocument, part, "Row Height", CurrentViewState());
			bool changed = SetLineSize(*ss, false, uint16_t(r0), uint16_t(r1), height);
			if (changed) {
				fBook->Reset();
				Window()->PostMessage(kMsgDocumentChanged);
				fprintf(stderr, "rowheight: %.1f for rows %d-%d\n", height, r0, r1);
			}
		} else if (command.size() >= 5 && command.substr(0, 5) == "goto ") {
			// "goto <cell>" — go to cell like "B12"
			int row = 0, column = 0;
			const char* p = command.c_str() + 5;
			while (*p == ' ') p++;
			int col = 0;
			while (*p >= 'A' && *p <= 'Z') {
				col = col * 26 + (*p - 'A' + 1);
				p++;
			}
			if (col == 0) {
				fprintf(stderr, "goto: invalid cell reference '%s'\n",
					command.c_str() + 5);
				continue;
			}
			col--;
			const char* d = p;
			while (*p >= '0' && *p <= '9') p++;
			if (p == d) {
				fprintf(stderr, "goto: invalid cell reference '%s'\n",
					command.c_str() + 5);
				continue;
			}
			row = atoi(d) - 1;
			column = col;
			if (row < 0 || row > 0x3fff || column > 0x2be) {
				fprintf(stderr, "goto: cell out of range\n");
				continue;
			}
			fSelectedCell.active = true;
			fSelectedCell.row = row;
			fSelectedCell.column = column;
			fSelectedCell.anchorRow = row;
			fSelectedCell.anchorColumn = column;
			fprintf(stderr, "goto: %s (row %d, column %d)\n",
				command.c_str() + 5, row, column);
		} else if (command.size() >= 11 && command.substr(0, 11) == "fontdialog ") {
			// "fontdialog <family>|<size>|<face flags>" — apply font properties
			// If a parameter is absent, that property is unchanged
			if (!fCaret.active) {
				continue;
			}
			std::string args = command.substr(11);
			size_t pipe1 = args.find('|');
			size_t pipe2 = std::string::npos;
			if (pipe1 != std::string::npos)
				pipe2 = args.find('|', pipe1 + 1);
			fUndo.Begin(fDocument, fCaret.part, "Font", CurrentViewState());
			auto sel = TextSelectionRange();
			// Family
			std::string family = (pipe1 != std::string::npos) ? args.substr(0, pipe1) : "";
			if (!family.empty()) {
				ApplyCharacter(sel.first, sel.second,
					TextFontProperty(family, ""));
			}
			// Size
			if (pipe2 != std::string::npos) {
				std::string sizeStr = args.substr(pipe1 + 1, pipe2 - pipe1 - 1);
				if (!sizeStr.empty()) {
					float size = strtof(sizeStr.c_str(), NULL);
					if (size > 0) {
						ApplyCharacter(sel.first, sel.second,
							TextSizeProperty(size));
					}
				}
			}
			// Face flags
			if (pipe2 != std::string::npos) {
				std::string flagsStr = args.substr(pipe2 + 1);
				if (!flagsStr.empty()) {
					char* end = NULL;
					uint32_t flags = strtoul(flagsStr.c_str(), &end, 10);
					if (end != flagsStr.c_str()) {
						ApplyCharacter(sel.first, sel.second,
							TextFaceProperty(flags));
					}
				}
			}
			TextChanged();
		} else if (command.size() >= 11 && command.substr(0, 11) == "paradialog ") {
			// "paradialog <align>|<left>|<first>|<right>|<before>|<after>|<line spacing>"
			// — apply paragraph properties; absent params leave properties unchanged
			if (!fCaret.active) {
				fprintf(stderr, "paradialog: no caret\n");
				continue;
			}
			std::string args = command.substr(11);
			std::vector<std::string> parts;
			size_t start = 0;
			for (int i = 0; i < 7; i++) {
				size_t p = args.find('|', start);
				if (p == std::string::npos) {
					parts.push_back(args.substr(start));
					break;
				}
				parts.push_back(args.substr(start, p - start));
				start = p + 1;
			}
			fUndo.Begin(fDocument, fCaret.part, "Paragraph", CurrentViewState());
			int32_t firstPar = fCaret.paragraph;
			int32_t lastPar = fCaret.paragraph;
			// Alignment
			if (!parts[0].empty()) {
				char* end = NULL;
				int32_t align = strtol(parts[0].c_str(), &end, 10);
				if (end != parts[0].c_str())
					ApplyParagraph(firstPar, lastPar,
						AlignmentProperty(align));
			}
			// Left indent
			if (!parts[1].empty()) {
				char* end = NULL;
				float val = strtof(parts[1].c_str(), &end);
				if (end != parts[1].c_str())
					ApplyParagraph(firstPar, lastPar,
						IndentProperty(Sig("PLef"), val));
			}
			// First indent
			if (!parts[2].empty()) {
				char* end = NULL;
				float val = strtof(parts[2].c_str(), &end);
				if (end != parts[2].c_str())
					ApplyParagraph(firstPar, lastPar,
						IndentProperty(Sig("PInd"), val));
			}
			// Right indent
			if (!parts[3].empty()) {
				char* end = NULL;
				float val = strtof(parts[3].c_str(), &end);
				if (end != parts[3].c_str())
					ApplyParagraph(firstPar, lastPar,
						IndentProperty(Sig("PRig"), val));
			}
			// Space before
			if (!parts[4].empty()) {
				char* end = NULL;
				float val = strtof(parts[4].c_str(), &end);
				if (end != parts[4].c_str())
					ApplyParagraph(firstPar, lastPar,
						SpacingProperty(Sig("SpaB"), val));
			}
			// Space after
			if (!parts[5].empty()) {
				char* end = NULL;
				float val = strtof(parts[5].c_str(), &end);
				if (end != parts[5].c_str())
					ApplyParagraph(firstPar, lastPar,
						SpacingProperty(Sig("SpaA"), val));
			}
			// Line spacing
			if (!parts[6].empty()) {
				std::string ls = parts[6];
				bool multiple = true;
				if (ls.find("pt") != std::string::npos)
					multiple = false;
				char* end = NULL;
				float val = strtof(ls.c_str(), &end);
				if (end != ls.c_str())
					ApplyParagraph(firstPar, lastPar,
						LineSpacingProperty(val, multiple));
			}
			TextChanged();
		} else if (command.size() > 5 && command.compare(0, 5, "para ") == 0) {
			// "para <par_idx>" — if followed by space, move caret to paragraph;
			// otherwise print paragraph properties
			if (command.size() > 5 && command[5] != '\0' && command.back() == ' ') {
				// "para <par_idx> " — move caret to end of paragraph <par_idx>
				int par;
				if (sscanf(command.c_str() + 5, "%d", &par) == 1) {
					const WPPart* wp = fDocument.WordPart(fCaret.part);
					if (wp != NULL && size_t(par) < wp->paragraphs.size()) {
						fCaret.active = true;
						fCaret.paragraph = size_t(par);
						fCaret.offset = EditableLength(wp->paragraphs[size_t(par)]);
						fAnchor = fCaret;
						Invalidate();
					}
				}
			} else {
				// "para <par_idx>" — print paragraph properties
				int par;
				if (sscanf(command.c_str() + 5, "%d", &par) == 1) {
					const WPPart* wp = fDocument.WordPart(fCaret.part);
					if (wp != NULL && size_t(par) < wp->paragraphs.size()) {
						const WPParagraph& p = wp->paragraphs[size_t(par)];
						uint32_t styleIdx = p.parStyle;
						TextStyle s = ResolveTextStyle(fDocument.styles, styleIdx);
						printf("para %d align %d left %.0f first %.0f right %.0f before %.0f after %.0f spacing %.0f tabs",
							par, s.alignment, s.leftIndent, s.firstIndent, s.rightIndent,
							s.spaceBefore, s.spaceAfter, s.lineSpacing);
						for (const TabStop& tab : s.tabs) {
							const char* tname = "left";
							if (tab.type == 1) tname = "center";
							else if (tab.type == 2) tname = "right";
							else if (tab.type == 3) tname = "align";
							printf(" %s@%.0f", tname, tab.position);
						}
						printf("\n");
					}
				}
			}
		} else if (command.compare(0, 4, "tab ") == 0) {
			// "tab <left|center|right|align> <x points>"
			// — adds a tab stop at the given x position (in points)
			if (!fCaret.active) {
				fprintf(stderr, "tab: no caret\n");
				continue;
			}
			std::string args = command.substr(4);
			size_t space = args.find(' ');
			if (space == std::string::npos) {
				fprintf(stderr, "tab: missing position\n");
				continue;
			}
			std::string typeStr = args.substr(0, space);
			float xPoints = strtof(args.substr(space + 1).c_str(), NULL);
			if (xPoints <= 0) {
				fprintf(stderr, "tab: invalid position\n");
				continue;
			}
			int type = 0;
			if (typeStr == "center")
				type = 1;
			else if (typeStr == "right")
				type = 2;
			else if (typeStr == "align")
				type = 3;
			// left = 0 is default

			if (fDocument.EditableWordPart(fCaret.part) == NULL) {
				fprintf(stderr, "tab: no word part\n");
				continue;
			}
			// Add a new tab stop
			TabStop tab;
			tab.position = xPoints;
			tab.type = type;
			tab.fill = 0;
			tab.alignChar = '.';
			tab.fillChar = '.';
			// one PTab per tab; it replaces a tab at the same position
			fUndo.Begin(fDocument, fCaret.part, "Tab", CurrentViewState());
			ApplyParagraph(fCaret.paragraph, fCaret.paragraph,
				TabProperty(tab));
			TextChanged();
		} else if (command.compare(0, 9, "deletetab") == 0) {
			// "deletetab <x points>" — remove the nearest tab stop to x points
			if (!fCaret.active) {
				fprintf(stderr, "deletetab: no caret\n");
				continue;
			}
			std::string args = command.substr(9);
			float xPoints = strtof(args.c_str(), NULL);
			if (xPoints <= 0) {
				fprintf(stderr, "deletetab: invalid position\n");
				continue;
			}

			WPPart* wp = fDocument.EditableWordPart(fCaret.part);
			if (wp == NULL) {
				fprintf(stderr, "deletetab: no word part\n");
				continue;
			}
			const WPParagraph& par = wp->paragraphs[fCaret.paragraph];
			uint32_t styleIdx = par.parStyle;
			TextStyle style = ResolveTextStyle(fDocument.styles, styleIdx);

			// Find the nearest tab to xPoints
			size_t nearestIdx = SIZE_MAX;
			float nearestDist = 1e18f;
			for (size_t i = 0; i < style.tabs.size(); i++) {
				float dist = fabsf(style.tabs[i].position - xPoints);
				if (dist < nearestDist) {
					nearestDist = dist;
					nearestIdx = i;
				}
			}
			if (nearestIdx == SIZE_MAX || nearestIdx >= style.tabs.size()) {
				fprintf(stderr, "deletetab: no tabs found\n");
				continue;
			}
			fUndo.Begin(fDocument, fCaret.part, "Tab", CurrentViewState());
			RemoveTab(fCaret.paragraph, fCaret.paragraph,
				style.tabs[nearestIdx].position);
			TextChanged();
		} else if (command.compare(0, 7, "insert ") == 0) {
			// "insert path": File > Insert… with that file chosen
			BMessage insert('sM1B');
			insert.AddString("path", command.c_str() + 7);
			MessageReceived(&insert);
		} else if (command.compare(0, 11, "paraformat ") == 0) {
			// "paraformat L F R S Sl B Bl A Al align": what Paragraph…'s OK
			// sends (points; spacings in lines when their flag is 1)
			float v[6];
			int l[3], align;
			if (sscanf(command.c_str() + 11, "%f %f %f %f %d %f %d %f %d %d", &v[0], &v[1],
					&v[2], &v[3], &l[0], &v[4], &l[1], &v[5], &l[2], &align) == 10) {
				BMessage format('sM50');
				format.AddFloat("left", v[0]);
				format.AddFloat("first", v[1]);
				format.AddFloat("right", v[2]);
				format.AddFloat("lineSpacing", v[3]);
				format.AddBool("lineSpacingLines", l[0] != 0);
				format.AddFloat("spaceBefore", v[4]);
				format.AddBool("spaceBeforeLines", l[1] != 0);
				format.AddFloat("spaceAfter", v[5]);
				format.AddBool("spaceAfterLines", l[2] != 0);
				format.AddInt32("align", align);
				MessageReceived(&format);
			}
		} else if (command.compare(0, 9, "footnote ") == 0) {
			// "footnote <f|e> [mark]": Insert Footnote… (e: an endnote)
			BMessage insert('sM5I');
			insert.AddBool("endnote", command[9] == 'e');
			if (command.size() > 11)
				insert.AddString("mark", command.c_str() + 11);
			MessageReceived(&insert);
		} else if (command.compare(0, 8, "columns ") == 0) {
			// "columns N": Format > Columns… with N
			BMessage set('sM51');
			set.AddInt32(kValueString, atoi(command.c_str() + 8));
			MessageReceived(&set);
		} else if (command.compare(0, 13, "formulafield ") == 0) {
			// "formulafield <r|f|b> <formula>": Insert Formula… showing the
			// result, the formula or both
			const char* c = command.c_str() + 13;
			BMessage insert('sM55');
			insert.AddString("formula", c + 2);
			insert.AddString("format", "General");
			insert.AddBool("displayFormula", c[0] != 'r');
			insert.AddBool("displayResult", c[0] != 'f');
			MessageReceived(&insert);
		} else if (command.compare(0, 10, "datefield ") == 0
				|| command.compare(0, 10, "timefield ") == 0) {
			// "datefield <format>": Insert Date… with that format chosen
			// (timefield: Insert Time…), as the dialog sends it
			BMessage insert(command[0] == 'd' ? 'sM53' : 'sM54');
			insert.AddString("format", command.c_str() + 10);
			insert.AddBool("static", false);
			MessageReceived(&insert);
		} else if (command.compare(0, 8, "margins ") == 0) {
			// "margins L T R B" (points): Document > Margins…
			float m[4];
			if (sscanf(command.c_str() + 8, "%f %f %f %f", &m[0], &m[1], &m[2], &m[3]) == 4
					&& !fDocument.sheets.sheets.empty()) {
				const LeafSheet& sheet = fDocument.sheets.sheets[0];
				float paper[4], printable[4] = {sheet.paper[0] + m[0], sheet.paper[1] + m[1],
					sheet.paper[2] - m[2], sheet.paper[3] - m[3]};
				std::copy(sheet.paper, sheet.paper + 4, paper);
				fUndo.BeginDocument(fDocument, GobeResources::IndexString(3, 0x15).String(),
					CurrentViewState());
				fDocument.SetPageGeometry(paper, printable);
				GeometryChanged();
			}
		} else if (command.compare(0, 11, "exporttext ") == 0
				|| command.compare(0, 10, "exportrtf ") == 0) {
			// "exporttext <path>", "exportrtf <path>": Save As in the
			// Plain Text or RTF format
			bool text = command[6] == 't';
			BString error;
			if (!ExportFile(fDocument, command.c_str() + (text ? 11 : 10),
					text ? kSavePlainText : kSaveRTF, error))
				fprintf(stderr, "%s: %s\n", text ? "exporttext" : "exportrtf", error.String());
		}
	}
}


void
TPageView::DocumentSaved()
{
	fUndo.DocumentSaved();
}


void
TPageView::ClearUndo()
{
	fUndo.Clear();
}


UndoStack::ViewState
TPageView::CurrentViewState() const
{
	if (fCaret.active)
		return {1, int64_t(fCaret.part), int64_t(fCaret.paragraph),
			int64_t(fCaret.offset), int64_t(fAnchor.paragraph),
			int64_t(fAnchor.offset)};
	if (fSelectedCell.active)
		return {2, int64_t(fSelectedCell.part), int64_t(fSelectedCell.row),
			int64_t(fSelectedCell.column), int64_t(fSelectedCell.anchorRow),
			int64_t(fSelectedCell.anchorColumn)};
	return {0};
}


void
TPageView::RestoreViewState(const UndoStack::ViewState& v)
{
	if (v.empty())
		return;
	int64_t type = v[0];
	if (type == 1) {
		fCaret.active = true;
		fCaret.part = size_t(v[1]);
		fCaret.paragraph = size_t(v[2]);
		fCaret.offset = size_t(v[3]);
		fAnchor.active = true;
		fAnchor.part = fCaret.part;
		fAnchor.paragraph = size_t(v[4]);
		fAnchor.offset = size_t(v[5]);
		fSelectedCell.active = false;
	} else if (type == 2) {
		fSelectedCell.active = true;
		fSelectedCell.part = size_t(v[1]);
		fSelectedCell.row = int(v[2]);
		fSelectedCell.column = int(v[3]);
		fSelectedCell.anchorRow = int(v[4]);
		fSelectedCell.anchorColumn = int(v[5]);
		fCaret.active = false;
		fAnchor.active = false;
	} else {
		fCaret.active = false;
		fAnchor.active = false;
		fSelectedCell.active = false;
	}
	Invalidate();
}


void
TPageView::DocumentReloaded()
{
	if (fCellEditor != NULL) {
		RemoveChild(fCellEditor);
		delete fCellEditor;
		fCellEditor = NULL;
	}
	fBook.reset(new SSBook(fDocument));
	fImages.clear();
	fCellStyles.clear();
	const WPPart* wp = fDocument.WordPart(fCaret.part);
	if (wp == NULL || fCaret.paragraph >= wp->paragraphs.size()) {
		fCaret.active = false;
		fAnchor.active = false;
	}
	Rebuild();
	Invalidate();
}

// Document > Insert Header / Insert Footer (TDocHeaderFooterCommand):
// inserts it, or removes it if there is one; a new one gets the caret
void
TPageView::ToggleHeaderFooter(int which)
{
	if (fDocument.PagePart() < 0) {
		beep();
		return;
	}
	int removed = fDocument.HeaderPart(which);
	bool insert = removed < 0;
	fUndo.BeginDocument(fDocument,
		GobeResources::IndexString(3, 0x16 + 2 * which + (insert ? 0 : 1)).String(),
		CurrentViewState());
	if (!(insert ? fDocument.InsertHeaderFooter(which) : fDocument.RemoveHeaderFooter(which))) {
		beep();
		return;
	}
	if (insert)
		fCaret = {true, size_t(fDocument.HeaderPart(which)), 0, 0};
	else if (fCaret.part == size_t(removed))
		fCaret = {fDocument.MainPart() >= 0, size_t(std::max(0, fDocument.MainPart())), 0, 0};
	GeometryChanged();
}


// the frame the caret is in (TWPPart::ActiveFrame): the one holding its
// line, else the part's first; NULL if the part has none
const Document::PlacedFrame*
TPageView::CaretFrame() const
{
	size_t index;
	float x;
	if (CaretLine(fCaret, index, x)) {
		BPoint center(fLines[index].frame.left + 1, fLines[index].frame.top + 1);
		for (const auto& f : fFrames) {
			BRect r(f.x - fBounds.left + fMargin, f.y - fBounds.top + fMargin, 0, 0);
			r.right = r.left + f.width;
			r.bottom = r.top + f.height;
			if (f.part == fCaret.part && r.Contains(center))
				return &f;
		}
	}
	for (const auto& f : fFrames)
		if (f.part == fCaret.part)
			return &f;
	return NULL;
}


// TWPPart::RulerBounds: the width of the frame the caret is in
float
TPageView::CaretFrameWidth() const
{
	const Document::PlacedFrame* f = CaretFrame();
	return f != NULL ? f->width : 468;
}


bool
TPageView::RulerFrame(BRect& text, BRect& page) const
{
	if (!fCaret.active || fDocument.WordPart(fCaret.part) == NULL)
		return false;
	const Document::PlacedFrame* f = CaretFrame();
	if (f == NULL)
		return false;
	float dx = fMargin - fBounds.left, dy = fMargin - fBounds.top;
	text.Set(f->x + dx, f->y + dy, f->x + dx + f->width, f->y + dy + f->height);
	// (the port's padding: the text's edges)
	text.InsetBy(kTextPad, 0);
	page = text;
	int pagePart = fDocument.PagePart();
	for (const auto& p : fFrames)
		if (int(p.part) == pagePart && p.index == f->page) {
			page.Set(p.x + dx, p.y + dy, p.x + dx + p.width, p.y + dy + p.height);
			break;
		}
	return true;
}


uint32_t
TPageView::EditStyle() const
{
	std::string name = TDialogStylesEdit::EditingStyle(&fDocument);
	return name.empty() ? 0xffffffff : FindNamedStyle(fDocument.styles, name);
}


bool
TPageView::ApplyCharacter(TextPosition from, TextPosition to, const Property& property)
{
	uint32_t style = EditStyle();
	if (style < fDocument.styles.styles.size())
		return SetStyleProperty(fDocument, style, property);
	return ApplyCharacterProperty(fDocument, fCaret.part, from, to, property);
}


bool
TPageView::ApplyParagraph(size_t first, size_t last, const Property& property)
{
	uint32_t style = EditStyle();
	if (style < fDocument.styles.styles.size())
		return SetStyleProperty(fDocument, style, property);
	return ApplyParagraphProperty(fDocument, fCaret.part, first, last, property);
}


bool
TPageView::RemoveTab(size_t first, size_t last, float position)
{
	uint32_t style = EditStyle();
	if (style < fDocument.styles.styles.size()) {
		TabStop tab;
		tab.position = position;
		return RemoveStyleProperty(fDocument, style, TabProperty(tab));
	}
	return RemoveParagraphTab(fDocument, fCaret.part, first, last, position);
}


void
TPageView::FormatParagraphs(const std::vector<float>& removeTabs,
	const std::vector<Property>& add, const std::string& label, bool newStep)
{
	if (!fCaret.active || fDocument.WordPart(fCaret.part) == NULL)
		return;
	if (newStep)
		fUndo.Begin(fDocument, fCaret.part, label, CurrentViewState());
	auto range = TextSelectionRange();
	size_t first = range.first.paragraph, last = range.second.paragraph;
	for (float position : removeTabs)
		RemoveTab(first, last, position);
	for (const Property& property : add)
		ApplyParagraph(first, last, property);
	TextChanged();
}


bool
TPageView::HandlesCommand(uint32 what)
{
	switch (what) {
		case 'sM10': case 'sM11': case 'sM12': case 'sM16': case 'sM17':	// Edit
		case 'sM5E': case 'sM5L':											// context menu
		case 'sM29':														// Word Count
		case 'sM50': case 'sM51': case 'sM52': case 'sM53': case 'sM54':	// Format
		case 'sM55': case 'sM56': case 'sM57': case 'sM58': case 'sM59':
		case 'sM5I': case 'sM5J': case 'sM5K':
		case 'sf01': case 'sf0K': case 'sf0C': case 300:					// text menus
		case 'sM5B': case 'sM5D':											// part bar
		case 'sf02': case 'sf0L': case 'sMB6':								// format bar
		case 'sMB5': case 'sMB7':											// Edit Styles
		case B_CUT: case B_COPY: case B_PASTE: case B_SELECT_ALL:
		case 'sM70': case 'sM71': case 'sM73': case 'sM74':				// cells
			return true;
		default:
			return false;
	}
}

} // namespace gobe
