// TPageView editing: a caret in word processing text (click, type,
// Backspace/Delete, Enter, arrows, Home/End, selection, and clipboard
// operations) and spreadsheet cells (click to select, double-click/Enter/
// typing to edit in place, Enter/Tab to commit, Escape to cancel).
// Spreadsheet range selection: shift-click, shift+arrows, mouse drag extend
// the range; copy/cut/paste/clear/selectall work on ranges using
// RangeInput/ClearRange/PasteInput from Editing.h.
#include "TPageView.h"

#include <Alert.h>
#include <Beep.h>
#include <Clipboard.h>
#include <Window.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>

#include "Editing.h"
#include "Fields.h"
#include "GobeResources.h"
#include "TWPDialogs.h"
#include "SSStyle.h"
#include "SSEval.h"
#include "TDocWindow.h"
#include "TTranslate.h"

namespace gobe {

static const uint32 kMsgCellCommit = 'Gcel';
static const uint32 kMsgCellCancel = 'Gccl';
// columns: not in the original's menus (its Insert/Delete Cells dialog
// chooses); for RunScript until that dialog works
static const uint32 kCmdInsertColumns = 'Gicl';
static const uint32 kCmdDeleteColumns = 'Gdcl';

// Cell format menu commands: value_message = 'sM20' (HAln), 'sM21' (VAln),
// 'sM22' (number format), 'sM23' (fill color), 'sM24' (wrap).
static const uint32 kCmdCellHAlign = 'sM20';
static const uint32 kCmdCellVAlign = 'sM21';
static const uint32 kCmdCellFormat = 'sM22';
static const uint32 kCmdCellFill = 'sM23';
static const uint32 kCmdCellWrap = 'sM24';


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
	return font.StringWidth(s.c_str());
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
				at = p.x + (offset > p.start ? p.font.StringWidth(p.text.c_str()) : 0);
			else
				at = p.x + SubstringWidth(p.font, text, p.start, offset);
			break;
		}
		at = p.x + p.font.StringWidth(p.text.c_str())
			+ (offset - p.end) * p.font.StringWidth(" ");
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
	std::string s = RangeText(*fDocument.WordPart(fCaret.part), from, to);
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
		TextPosition at = InsertMultiline(*fDocument.EditableWordPart(fCaret.part),
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
		case B_END: {
			if (shift) {
				if (!fAnchor.active || fAnchor.part != fCaret.part)
					fAnchor = fCaret;
			} else {
				TextPosition from, to;
				if (TextSelection(from, to)) {
					if (bytes[0] == B_LEFT_ARROW || bytes[0] == B_UP_ARROW || bytes[0] == B_HOME)
						fCaret = {true, fCaret.part, from.paragraph, from.offset};
					else
						fCaret = {true, fCaret.part, to.paragraph, to.offset};
					fAnchor = fCaret;
					changed = true;
					break;
				}
			}
			wp = fDocument.EditableWordPart(fCaret.part);
			if (wp == NULL || fCaret.paragraph >= wp->paragraphs.size())
				break;
			const WPParagraph& par = wp->paragraphs[fCaret.paragraph];
			switch (bytes[0]) {
				case B_LEFT_ARROW:
					if (fCaret.offset > 0)
						fCaret.offset = PreviousCharacter(par.text, fCaret.offset);
					else if (fCaret.paragraph > 0) {
						fCaret.paragraph--;
						fCaret.offset = EditableLength(wp->paragraphs[fCaret.paragraph]);
					}
					break;
				case B_RIGHT_ARROW:
					if (fCaret.offset < EditableLength(par))
						fCaret.offset = NextCharacter(par.text, fCaret.offset);
					else if (fCaret.paragraph + 1 < wp->paragraphs.size()) {
						fCaret.paragraph++;
						fCaret.offset = 0;
					}
					break;
				case B_UP_ARROW:
					MoveCaretVertically(-1);
					break;
				case B_DOWN_ARROW:
					MoveCaretVertically(1);
					break;
				case B_HOME:
				case B_END: {
					size_t index;
					float x;
					if (CaretLine(fCaret, index, x))
						fCaret.offset = bytes[0] == B_HOME ? fLines[index].from
							: std::min(fLines[index].to, EditableLength(par));
					if (bytes[0] == B_END && fCaret.offset > fLines[index].from
						&& fCaret.offset < EditableLength(par)
						&& par.text[PreviousCharacter(par.text, fCaret.offset)] == ' ')
						fCaret.offset = PreviousCharacter(par.text, fCaret.offset);
					break;
				}
			}
			if (!shift)
				fAnchor = fCaret;
			fUndo.Break();
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
	// edits leave no selection; moves set the anchor themselves
	if (changed && !shift)
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
	fCellEditor = new TCellEditor(rect);
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
	Click(where, clicks, buttons);
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
	if (HitText(where, caret)) {
		fSelectedCell.active = false;
		bool shift = (modifiers() & B_SHIFT_KEY) != 0 || fScriptShift;
		if (shift && fCaret.active && caret.part == fCaret.part) {
			fCaret = caret;
		} else if (clicks == 2) {
			const WPPart* wp = fDocument.WordPart(caret.part);
			if (wp != NULL) {
				const std::string& text = wp->paragraphs[caret.paragraph].text;
				size_t from, to;
				WordRange(text, caret.offset, from, to);
				fAnchor = {true, caret.part, caret.paragraph, from};
				fCaret = {true, caret.part, caret.paragraph, to};
			}
		} else if (clicks >= 3) {
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
			if (fCellEditor != NULL)
				EndCellEdit(true);
			UndoStack::ViewState v = CurrentViewState();
			int part = message->what == 'sM10' ? fUndo.Undo(fDocument, v)
				: fUndo.Redo(fDocument, v);
			if (part == UndoStack::kDocumentChanged) {
				GeometryChanged();
				RestoreViewState(v);
				return;
			}
			if (part < 0) {
				beep();
				return;
			}
			if (fDocument.SpreadsheetPart(part) != NULL)
				fBook->Reset();
			TextChanged();
			RestoreViewState(v);
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
			if (!FormulaField(*fBook, fCaret.part, GobeResources::IndexString(0x516, 8).String(),
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
			ApplyCharacterProperty(fDocument, fCaret.part, sel.first, sel.second,
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
			int32 size = 0;
			if (message->FindInt32(kValueString, &size) != B_OK) {
				beep();
				return;
			}
			fUndo.Begin(fDocument, fCaret.part, "Size", CurrentViewState());
			auto sel = TextSelectionRange();
			ApplyCharacterProperty(fDocument, fCaret.part, sel.first, sel.second,
				TextSizeProperty((float)size));
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
			ApplyCharacterProperty(fDocument, fCaret.part, sel.first, sel.second,
				TextFaceProperty(flags));
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
			ApplyParagraphProperty(fDocument, fCaret.part, range.first.paragraph,
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
		if (command.compare(0, 6, "click ") == 0) {
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
			static const struct { const char* name; char key; } kNames[] = {
				{"{enter}", B_ENTER}, {"{bs}", B_BACKSPACE}, {"{del}", B_DELETE},
				{"{left}", B_LEFT_ARROW}, {"{right}", B_RIGHT_ARROW}, {"{up}", B_UP_ARROW},
				{"{down}", B_DOWN_ARROW}, {"{tab}", B_TAB}, {"{esc}", B_ESCAPE},
				{"{home}", B_HOME}, {"{end}", B_END}, {"{sleft}", B_LEFT_ARROW},
				{"{sright}", B_RIGHT_ARROW}, {"{sup}", B_UP_ARROW},
				{"{sdown}", B_DOWN_ARROW}};
			for (size_t i = 0; i < keys.size();) {
				char key = keys[i];
				size_t length = 1;
				bool shifted = keys.compare(i, 7, "{sleft}") == 0
					|| keys.compare(i, 8, "{sright}") == 0
					|| keys.compare(i, 4, "{sup}") == 0
					|| keys.compare(i, 7, "{sdown}") == 0;
				for (const auto& n : kNames)
					if (keys.compare(i, strlen(n.name), n.name) == 0) {
						key = n.key;
						length = strlen(n.name);
					}
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
					KeyDown(bytes.c_str(), int32(bytes.size()));
					fScriptShift = false;
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
				ApplyCharacterProperty(fDocument, fCaret.part, sel.first, sel.second,
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
			fUndo.Begin(fDocument, fCaret.part, "Replace All", CurrentViewState());
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
				ApplyCharacterProperty(fDocument, fCaret.part, sel.first, sel.second,
					TextFontProperty(family, ""));
			}
			// Size
			if (pipe2 != std::string::npos) {
				std::string sizeStr = args.substr(pipe1 + 1, pipe2 - pipe1 - 1);
				if (!sizeStr.empty()) {
					float size = strtof(sizeStr.c_str(), NULL);
					if (size > 0) {
						ApplyCharacterProperty(fDocument, fCaret.part, sel.first, sel.second,
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
						ApplyCharacterProperty(fDocument, fCaret.part, sel.first, sel.second,
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
					ApplyParagraphProperty(fDocument, fCaret.part, firstPar, lastPar,
						AlignmentProperty(align));
			}
			// Left indent
			if (!parts[1].empty()) {
				char* end = NULL;
				float val = strtof(parts[1].c_str(), &end);
				if (end != parts[1].c_str())
					ApplyParagraphProperty(fDocument, fCaret.part, firstPar, lastPar,
						IndentProperty(Sig("PLef"), val));
			}
			// First indent
			if (!parts[2].empty()) {
				char* end = NULL;
				float val = strtof(parts[2].c_str(), &end);
				if (end != parts[2].c_str())
					ApplyParagraphProperty(fDocument, fCaret.part, firstPar, lastPar,
						IndentProperty(Sig("PInd"), val));
			}
			// Right indent
			if (!parts[3].empty()) {
				char* end = NULL;
				float val = strtof(parts[3].c_str(), &end);
				if (end != parts[3].c_str())
					ApplyParagraphProperty(fDocument, fCaret.part, firstPar, lastPar,
						IndentProperty(Sig("PRig"), val));
			}
			// Space before
			if (!parts[4].empty()) {
				char* end = NULL;
				float val = strtof(parts[4].c_str(), &end);
				if (end != parts[4].c_str())
					ApplyParagraphProperty(fDocument, fCaret.part, firstPar, lastPar,
						SpacingProperty(Sig("SpaB"), val));
			}
			// Space after
			if (!parts[5].empty()) {
				char* end = NULL;
				float val = strtof(parts[5].c_str(), &end);
				if (end != parts[5].c_str())
					ApplyParagraphProperty(fDocument, fCaret.part, firstPar, lastPar,
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
					ApplyParagraphProperty(fDocument, fCaret.part, firstPar, lastPar,
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
			ApplyParagraphProperty(fDocument, fCaret.part, fCaret.paragraph, fCaret.paragraph,
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
			RemoveParagraphTab(fDocument, fCaret.part, fCaret.paragraph, fCaret.paragraph,
				style.tabs[nearestIdx].position);
			TextChanged();
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


} // namespace gobe
