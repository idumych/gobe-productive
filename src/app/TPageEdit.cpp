// TPageView editing: a caret in word processing text (click, type,
// Backspace/Delete, Enter, arrows, Home/End, selection, and clipboard
// operations) and spreadsheet cells (click to select, double-click/Enter/
// typing to edit in place, Enter/Tab to commit, Escape to cancel). Changes
// go to the Document through the Editing operations; TDocWindow saves them.
#include "TPageView.h"

#include <Alert.h>
#include <Beep.h>
#include <Clipboard.h>
#include <Window.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "Editing.h"
#include "SSEval.h"

namespace gobe {

static const uint32 kMsgCellCommit = 'Gcel';
static const uint32 kMsgCellCancel = 'Gccl';


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
	if (wp == NULL)
		return;
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
			break;
		}
		case B_ESCAPE:
			fCaret.active = false;
			fAnchor.active = false;
			break;
		default:
			if ((unsigned char)bytes[0] >= 0x20 || bytes[0] == B_TAB) {
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
		if (Window() != NULL)
			Window()->PostMessage(kMsgDocumentChanged);
	}
	fSelectedCell.row = std::max(0, fSelectedCell.row + moveRows);
	fSelectedCell.column = std::max(0, fSelectedCell.column + moveColumns);
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

	size_t part;
	int row, column;
	BRect rect;
	if (HitCell(where, part, row, column, rect)) {
		bool same = fSelectedCell.active && fSelectedCell.part == part
			&& fSelectedCell.row == row && fSelectedCell.column == column;
		fCaret.active = false;
		fAnchor.active = false;
		fSelectedCell.active = true;
		fSelectedCell.part = part;
		fSelectedCell.row = row;
		fSelectedCell.column = column;
		if (same || clicks > 1)
			BeginCellEdit(part, row, column, rect);
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
	BView::MouseMoved(where, transit, dragMessage);
}


void
TPageView::MouseUp(BPoint where)
{
	fDragging = false;
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
		BRect rect = CellViewRect(fSelectedCell.part, fSelectedCell.row,
			fSelectedCell.column);
		switch (bytes[0]) {
			case B_UP_ARROW:
				fSelectedCell.row = std::max(0, fSelectedCell.row - 1);
				break;
			case B_DOWN_ARROW:
				fSelectedCell.row++;
				break;
			case B_LEFT_ARROW:
				fSelectedCell.column = std::max(0, fSelectedCell.column - 1);
				break;
			case B_RIGHT_ARROW:
			case B_TAB:
				fSelectedCell.column++;
				break;
			case B_ENTER:
				BeginCellEdit(fSelectedCell.part, fSelectedCell.row, fSelectedCell.column, rect);
				return;
			case B_BACKSPACE:
			case B_DELETE: {
				SSPart* ss = fDocument.EditableSpreadsheetPart(fSelectedCell.part);
				std::string error;
				if (ss != NULL && SetCellInput(*ss, uint16_t(fSelectedCell.row),
						uint16_t(fSelectedCell.column), "", nullptr, error)) {
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
		default:
			if (fCaret.active) {
				// the Edit menu (MENU 3) sends B_CUT, B_COPY, B_PASTE, B_SELECT_ALL
				static const uint32 kCmdClear = 'sM12';
				switch (message->what) {
					case B_CUT:
						CopySelection();
						if (DeleteSelection())
							TextChanged();
						return;
					case B_COPY:
						CopySelection();
						return;
					case B_PASTE:
						PasteText();
						return;
					case kCmdClear:
						if (DeleteSelection())
							TextChanged();
						return;
					case B_SELECT_ALL:
						SelectAllText();
						return;
				}
			}
			BView::MessageReceived(message);
	}
}


// Test hook (--do): commands separated by ';': "click x y", "dclick x y",
// "sclick x y" (shift-click), "tclick x y" (triple-click),
// "key text" (with {enter} {bs} {del} {left} {right} {up} {down} {tab}
// {esc} {home} {end} {sleft} {sright}), "selectall", "copy", "cut", "paste",
// "commit" (the cell editor), "save path".
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
			SelectAllText();
		} else if (command == "copy") {
			CopySelection();
		} else if (command == "cut") {
			CopySelection();
			if (DeleteSelection())
				TextChanged();
		} else if (command == "paste") {
			PasteText();
		} else if (command.compare(0, 4, "key ") == 0) {
			std::string keys = command.substr(4);
			static const struct { const char* name; char key; } kNames[] = {
				{"{enter}", B_ENTER}, {"{bs}", B_BACKSPACE}, {"{del}", B_DELETE},
				{"{left}", B_LEFT_ARROW}, {"{right}", B_RIGHT_ARROW}, {"{up}", B_UP_ARROW},
				{"{down}", B_DOWN_ARROW}, {"{tab}", B_TAB}, {"{esc}", B_ESCAPE},
				{"{home}", B_HOME}, {"{end}", B_END}, {"{sleft}", B_LEFT_ARROW},
				{"{sright}", B_RIGHT_ARROW}};
			for (size_t i = 0; i < keys.size();) {
				char key = keys[i];
				size_t length = 1;
				bool shifted = keys.compare(i, 7, "{sleft}") == 0
					|| keys.compare(i, 8, "{sright}") == 0;
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
		} else if (command.compare(0, 5, "save ") == 0) {
			std::string error;
			if (!document.Save(command.substr(5), error))
				fprintf(stderr, "save: %s\n", error.c_str());
			DocumentReloaded();
		}
	}
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
	if (wp == NULL || fCaret.paragraph >= wp->paragraphs.size())
		fCaret.active = false;
		fAnchor.active = false;
	Rebuild();
	Invalidate();
}

} // namespace gobe
