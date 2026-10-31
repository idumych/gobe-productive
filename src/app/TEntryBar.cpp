#include "TEntryBar.h"

#include <TextView.h>
#include <Window.h>

#include <algorithm>

#include "Editing.h"
#include "GobeResources.h"
#include "TPageView.h"
#include "TSheetDialogs.h"

namespace gobe {

// TEntryEdit: a BTextView in the plain font (at most 12 points), no
// returns, tabs or escapes; Enter and Tab send 's007', Escape 's008'; a
// click begins editing
class TEntryEdit : public BTextView {
public:
	TEntryEdit(BRect frame, TEntryBar* bar)
		:
		BTextView(frame, "Edit text", BRect(), B_FOLLOW_LEFT_RIGHT | B_FOLLOW_TOP,
			B_WILL_DRAW | B_NAVIGABLE),
		fBar(bar)
	{
		BFont font(be_plain_font);
		font.SetSize(std::min(font.Size(), 12.0f));
		SetFontAndColor(&font);
		font_height height;
		font.GetHeight(&height);
		BRect text = Bounds().InsetByCopy(2, 0);
		text.OffsetBy(0, roundf((frame.Height() - (height.ascent + height.descent)) * 0.5f));
		SetTextRect(text);
		SetWordWrap(false);
		DisallowChar('\n');
		DisallowChar(B_ESCAPE);
		DisallowChar(B_TAB);
	}

	virtual void KeyDown(const char* bytes, int32 count)
	{
		int32 modifiers = 0;
		if (Window() != NULL && Window()->CurrentMessage() != NULL)
			modifiers = Window()->CurrentMessage()->GetInt32("modifiers", 0);
		if (bytes[0] == B_ENTER || bytes[0] == B_TAB)
			fBar->Accept(bytes[0], modifiers);
		else if (bytes[0] == B_ESCAPE)
			fBar->Cancel();
		else
			BTextView::KeyDown(bytes, count);
	}

	virtual void MouseDown(BPoint where)
	{
		if (!fBar->InEdit())
			fBar->BeginEdit();
		BTextView::MouseDown(where);
	}

	virtual void Draw(BRect updateRect)
	{
		BTextView::Draw(updateRect);
		// a sunken frame (TEntryEdit in a TRibbon)
		BRect r = Bounds();
		SetHighColor(0x70, 0x70, 0x70);
		StrokeLine(r.LeftTop(), r.RightTop());
		StrokeLine(r.LeftTop(), r.LeftBottom());
		SetHighColor(255, 255, 255);
		StrokeLine(r.LeftBottom(), r.RightBottom());
		StrokeLine(r.RightTop(), r.RightBottom());
	}

private:
	TEntryBar* fBar;
};


// TAcceptButton / TCancelButton / TFunctionButton: Accept is Enter, Cancel
// is Escape, the function button opens Paste Function
class TEntryButton : public TMICNButtonElement {
public:
	TEntryButton(uint32 id, BPoint where, int32 micn, TEntryBar* bar)
		:
		TMICNButtonElement(id, where, micn, NULL),
		fBar(bar)
	{
	}

	virtual void Invoke(TRibbon* ribbon)
	{
		if (id == 'ACPT')
			fBar->Accept(B_ENTER, 0);
		else if (id == 'CANC')
			fBar->Cancel();
		else
			(new TDialogPasteFunction(fBar))->RunDialog();
	}

private:
	TEntryBar* fBar;
};


// TEntryBar::LoadAllElements: Accept at 4, then Cancel and Paste Function
// each 3 after the last, the edit 10 after them to the bar's end
TEntryBar::TEntryBar(TPageView* pages)
	:
	TRibbon("EntryBar"),
	fPages(pages)
{
	BPoint at(4, 2);
	TEntryButton* accept = new TEntryButton('ACPT', at, 0x28, this);
	accept->tip = GobeResources::IndexString(4, 24).String();
	accept->enabled = false;
	AddElement(accept);
	at.x = accept->rect.right + 3;
	TEntryButton* cancel = new TEntryButton('CANC', at, 0x29, this);
	cancel->tip = GobeResources::IndexString(4, 25).String();
	cancel->enabled = false;
	AddElement(cancel);
	at.x = cancel->rect.right + 3;
	// TFunctionButton: the Paste Function dialog ('s005')
	TEntryButton* function = new TEntryButton('PFNC', at, 0x2a, this);
	function->tip = GobeResources::IndexString(4, 26).String();
	AddElement(function);
	float left = function->rect.right + 3 + 10;
	fEdit = new TEntryEdit(BRect(left, 3, left + 300, 21), this);
	AddChild(fEdit);
}


void
TEntryBar::Update()
{
	// the edit as wide as the bar
	float right = Bounds().right - 4;
	if (right > fEdit->Frame().left + 20 && fEdit->Frame().right != right)
		fEdit->ResizeTo(right - fEdit->Frame().left, fEdit->Frame().Height());
	if (fEditing)
		return;
	// TPartSS::SetEntryBar: the active cell's formula or value as typed
	std::string shown;
	const auto& cell = fPages->fSelectedCell;
	if (cell.active)
		if (const SSPart* ss = fPages->Doc().SpreadsheetPart(cell.part))
			shown = CellInput(*ss, uint16_t(cell.row), uint16_t(cell.column));
	if (shown != fShown || shown != fEdit->Text()) {
		fShown = shown;
		fEdit->SetText(shown.c_str());
	}
}


void
TEntryBar::BeginEdit(const char* bytes)
{
	fEditing = true;
	SetEnabled('ACPT', true);
	SetEnabled('CANC', true);
	fEdit->MakeFocus(true);
	if (bytes != NULL) {
		fEdit->SetText("");
		fEdit->Insert(bytes);
	}
}


void
TEntryBar::EndEdit()
{
	fEditing = false;
	SetEnabled('ACPT', false);
	SetEnabled('CANC', false);
	fShown.clear();
	if (fPages != NULL) {
		fPages->MakeFocus(true);
		Update();
	}
}


void
TEntryBar::PasteFunction(const std::string& signature)
{
	if (LockLooper()) {
		size_t open = signature.find('(');
		size_t from = open == std::string::npos ? signature.size() : open + 1;
		size_t to = signature.find(',');
		if (to == std::string::npos)
			to = signature.find(')');
		if (to == std::string::npos)
			to = signature.size();
		int32 start = 0, end = 0;
		if (fEditing) {
			fEdit->GetSelection(&start, &end);
			fEdit->Delete(start, end);
			fEdit->Insert(start, signature.c_str(), signature.size());
		} else {
			BeginEdit();
			fEdit->SetText(signature.c_str());
		}
		from += start;
		to += start;
		if (fEdit->TextLength() == 0 || fEdit->Text()[0] != '=') {
			fEdit->Insert(0, "=", 1);
			from++;
			to++;
		}
		fEdit->Select(int32(from), int32(to));
		fEdit->MakeFocus(true);
		UnlockLooper();
	}
}


void
TEntryBar::Key(const char* bytes, int32 count)
{
	fEdit->KeyDown(bytes, count);
}


const char*
TEntryBar::Text() const
{
	return fEdit->Text();
}


void
TEntryBar::SetText(const char* text)
{
	fEdit->SetText(text);
}


void
TEntryBar::Accept(int32 key, int32 modifiers)
{
	BMessage enter('s007');
	enter.AddInt32("key", key);
	enter.AddInt32("modifiers", modifiers);
	enter.AddString("text", fEdit->Text());
	Send(&enter);
}


void
TEntryBar::Cancel()
{
	BMessage cancel('s008');
	Send(&cancel);
}

} // namespace gobe
