#include "TPartBar.h"

#include <Alert.h>
#include <Region.h>
#include <MenuItem.h>
#include <TextView.h>
#include <Window.h>

#include <algorithm>
#include <cmath>

#include "Document.h"
#include "GobeResources.h"
#include "Measure.h"
#include "Styles.h"
#include "TMenu.h"
#include "TPageView.h"

namespace gobe {

namespace {

// a measure's unit "lines" (TMeasureUnit 5): 16 points each
const int kLines = 5;

std::vector<std::string>
UnitNames()
{
	std::vector<std::string> names;
	for (int32 i = 0; i < 6; i++)
		names.push_back(GobeResources::IndexString(7, i).String());
	return names;
}


// the part factory's small icon (TPartFactory: PART's MICN id at 24), from
// the application or the add-on the part comes from
BBitmap*
PartIcon(uint32 kind)
{
	static const char* const kAddOns[] = {NULL, "Spreadsheet", "Chart", "Table",
		"Presentation", "Clock", "ImageProcessing"};
	for (const char* addOn : kAddOns) {
		const ResourceFile& file = addOn == NULL ? GobeResources::File()
			: GobeResources::Part(addOn);
		for (const ResourceFile::Resource* r : file.All('PART')) {
			const std::vector<uint8_t>& d = r->data;
			if (d.size() < 28)
				continue;
			auto u32 = [&](size_t at) {
				return uint32(d[at + 3]) << 24 | uint32(d[at + 2]) << 16
					| uint32(d[at + 1]) << 8 | d[at];
			};
			if (u32(0) == kind)
				return TMICNElement::LoadIcon(int32(u32(24)), &file);
		}
	}
	return NULL;
}


const ResourceFile&
SheetResources()
{
	return GobeResources::Part("Spreadsheet");
}


// TNameIndicator (a TTextIndicatorElement): the selection's name in a
// sunken frame; a double click is Go To Cell… ('sM77')
class TCellAddressElement : public TRibbonElement {
public:
	TCellAddressElement(BRect rect)
		:
		TRibbonElement('ADDR', rect)
	{
		tip = IndexString(SheetResources(), 1507, 0);
	}

	virtual void Draw(BView* view, bool down)
	{
		BRect r = rect;
		view->SetHighColor(0xd8, 0xd8, 0xd8);
		view->FillRect(r);
		// TIndicatorElement::DrawFrame
		view->SetHighColor(255, 255, 255);
		view->StrokeLine(BPoint(r.left, r.bottom), BPoint(r.right, r.bottom));
		view->StrokeLine(BPoint(r.right, r.top), BPoint(r.right, r.bottom));
		view->SetHighColor(0, 0, 0);
		view->StrokeLine(BPoint(r.left, r.top), BPoint(r.right - 1, r.top));
		view->StrokeLine(BPoint(r.left, r.top), BPoint(r.left, r.bottom - 1));
		// TTextIndicatorElement::Draw: black on the panel, at most 10
		// points, inside the frame
		BFont font(be_plain_font);
		font.SetSize(std::min(font.Size(), 10.0f));
		view->PushState();
		view->SetFont(&font);
		view->SetLowColor(0xd8, 0xd8, 0xd8);
		BRegion clip(r.InsetByCopy(2, 2));
		view->ConstrainClippingRegion(&clip);
		view->DrawString(text.c_str(), BPoint(r.left + 5, r.bottom - 4));
		view->PopState();
	}

	virtual bool Clickable() const { return true; }

	virtual bool MouseDown(TRibbon* ribbon, BPoint where)
	{
		int32 clicks = 1;
		if (ribbon->Window() != NULL && ribbon->Window()->CurrentMessage() != NULL)
			ribbon->Window()->CurrentMessage()->FindInt32("clicks", &clicks);
		if (clicks == 2) {
			BMessage go('sM77');
			ribbon->Send(&go);
		}
		return true;
	}

	std::string text;
};


// TCellHAlignMenuElement / TCellVAlignMenuElement (TMICNMenuElement): MENU
// 302 or 303 of the Spreadsheet add-on, sending 'sM67' with the value
// (0-3 horizontal, 4-6 vertical, 7 Wrap Text); FixMenu marks what the
// selected cells all have
class TCellAlignElement : public TMenuElement {
public:
	TCellAlignElement(uint32 id, BRect rect, int32 menu, int32 micn, int32 tipIndex,
			TPageView* pages)
		:
		TMenuElement(id, rect, NULL, TMICNElement::LoadIcon(micn, &SheetResources())),
		fMenu(menu),
		fPages(pages)
	{
		tip = IndexString(SheetResources(), 1507, tipIndex);
	}

	virtual void FillMenu(BPopUpMenu* menu, TRibbon* ribbon)
	{
		TMenu source(fMenu, SheetResources());
		int32 h = -2, v = -2, wrap = -2;	// -2 none seen, -1 mixed
		fPages->SelectedCellsAlignment(h, v, wrap);
		while (source.CountItems() > 0) {
			BMenuItem* item = source.RemoveItem((int32)0);
			if (item->Message() != NULL) {
				int32 value = item->Message()->GetInt32(kValueString, -1);
				if (value >= 0 && value <= 3)
					item->SetMarked(h == value);
				else if (value >= 4 && value <= 6)
					item->SetMarked(v == value - 3);
				else if (value == 7) {
					item->SetMarked(wrap == 1);
					item->Message()->AddBool("marked", wrap == 1);
				}
			}
			menu->AddItem(item);
		}
	}

private:
	int32 fMenu;
	TPageView* fPages;
};


// TSSBorderMenuElement: the borders' menus (cell borders aren't ported:
// shown, off)
class TCellBorderElement : public TMenuElement {
public:
	TCellBorderElement(uint32 id, BPoint where, int32 micn, int32 tipIndex)
		:
		TMenuElement(id, BRect(where, where), NULL,
			TMICNElement::LoadIcon(micn, &SheetResources()))
	{
		tip = IndexString(SheetResources(), 1507, tipIndex);
		enabled = false;
	}

	virtual void FillMenu(BPopUpMenu* menu, TRibbon* ribbon) {}
};

} // namespace


// TSpacingEdit (TWPRibbonEdit): white, framed, centred, in a font at most
// 10 high; a click makes it editable, Enter or Tab takes what was typed,
// Escape not
class TSpacingEdit : public BTextView {
public:
	TSpacingEdit(TPartBar* bar, BRect frame)
		:
		BTextView(frame, "spacing", BRect(), B_FOLLOW_NONE, B_WILL_DRAW),
		fBar(bar)
	{
		// TWPRibbonEdit::calcTextRect
		BRect text = Bounds().InsetByCopy(2, 2);
		text.top += 2;
		SetTextRect(text);
		MakeEditable(false);
		SetViewColor(255, 255, 255);
		SetWordWrap(false);
		SetAlignment(B_ALIGN_CENTER);
		DisallowChar('\n');
		DisallowChar(B_ESCAPE);
		DisallowChar(B_TAB);
		// (with the height BeOS's Bitstream fonts had, 1.2 x the size, as
		// the text layout measures lines; Haiku's are taller)
		BFont font;
		GetFont(&font);
		while (font.Size() * 1.2f > 10 && font.Size() > 1)
			font.SetSize(font.Size() - 1);
		SetFontAndColor(&font);
	}

	virtual void Draw(BRect updateRect)
	{
		BTextView::Draw(updateRect);
		SetHighColor(0, 0, 0);
		StrokeRect(Bounds());
	}

	virtual void KeyDown(const char* bytes, int32 count)
	{
		if (bytes[0] == B_ESCAPE)
			SetEditable(false, false);
		else if (bytes[0] == B_ENTER || bytes[0] == B_TAB)
			SetEditable(false, true);
		else
			BTextView::KeyDown(bytes, count);
	}

	virtual void MouseDown(BPoint where)
	{
		if (!fEnabled)
			return;
		if (!IsEditable())
			SetEditable(true, true);
		else
			BTextView::MouseDown(where);
	}

	// TSpacingEdit::SetEditable: leaving it, what was typed as a measure
	// from 1 point to 160 (10 lines), else STR# 6, 15
	void SetEditable(bool editable, bool take)
	{
		if (editable == IsEditable())
			return;
		if (!editable && take) {
			float points;
			int unit = fUnit;
			std::vector<std::string> names = UnitNames();
			if (!ParseMeasure(Text(), fUnit, names, true, false, points, &unit)
					|| points < 1 || points > 160) {
				BString alert;
				alert.SetToFormat(GobeResources::IndexString(6, 15).String(),
					MeasureText(1, fUnit, kMeasureTwoPlaces, names).c_str(),
					MeasureText(160, fUnit, kMeasureWithUnit, names).c_str());
				(new BAlert("", alert.String(), "OK"))->Go();
				fBar->SpacingCancelled();
			} else
				fBar->SpacingEntered(unit == kLines ? points * 0.0625f : points, unit == kLines);
		} else if (!editable)
			fBar->SpacingCancelled();
		MakeEditable(editable);
		if (editable) {
			MakeFocus(true);
			SelectAll();
		} else if (Window() != NULL && Window()->FindView("pages") != NULL)
			Window()->FindView("pages")->ChildAt(0)->MakeFocus(true);
		Invalidate();
	}

	// TSpacingEdit::SetValue: `points` shown in `unit`
	void SetValue(float points, int unit)
	{
		fUnit = unit;
		SetText(MeasureText(points, unit, kMeasureWithUnit | kMeasureTwoPlaces,
			UnitNames()).c_str());
	}

	void SetEnabled(bool enabled)
	{
		fEnabled = enabled;
		if (!enabled && IsEditable())
			MakeEditable(false);
		SetViewColor(enabled ? 255 : 0xe8, enabled ? 255 : 0xe8, enabled ? 255 : 0xe8);
		Invalidate();
	}

private:
	TPartBar* fBar;
	int fUnit = 0;
	bool fEnabled = true;
};


TPartBar::TPartBar(TPageView* pages)
	:
	TRibbon("PartBar"),
	fPages(pages)
{
	Build(false, 0);
}


// TPartBar::LoadAllElements (the part's icon at 5, 4) and TWPPart::
// AddPartBarElements
void
TPartBar::Build(bool text, uint16 kind)
{
	RemoveAllElements();
	if (fSpacing != NULL) {
		fSpacing->RemoveSelf();
		delete fSpacing;
		fSpacing = NULL;
	}
	fText = text;
	fSheet = false;
	fKind = kind;
	fAlignment = -2;
	fLineSpacing = -1;
	fColumns = -1;
	TMICNElement* part = new TMICNElement('Part', BPoint(5, 4), text ? PartIcon('WORD') : NULL);
	part->tip = GobeResources::IndexString(4, 23).String();
	AddElement(part);
	if (!text)
		return;

	auto tip = [](int32 index) { return std::string(GobeResources::IndexString(1303, index).String()); };
	BRect r(29, 2, 48, 21);
	for (int32 i = 0; i < 4; i++) {
		BMessage* message = new BMessage('sM5B');
		message->AddInt32(kValueString, i);
		TMICNButtonElement* button = new TMICNButtonElement('ALIG' + i, r.LeftTop(), 0x4b1 + i,
			message);
		button->family = 'ALIG';
		button->selectable = true;
		button->tip = tip(i + 8);
		AddElement(button);
		r.OffsetBy(22, 0);
	}
	r.OffsetBy(15, 0);
	BMessage* less = new BMessage('sM5D');
	less->AddFloat(kValueString, -1);
	TMICNButtonElement* button = new TMICNButtonElement('SPAC', r.LeftTop(), 0x4b5, less);
	button->tip = tip(12);
	AddElement(button);
	r.left += 19;
	r.right = r.left + 40;
	fSpacing = new TSpacingEdit(this, r);
	AddChild(fSpacing);
	r.left = r.right;
	r.right = r.left + 20 - 1;
	BMessage* more = new BMessage('sM5D');
	more->AddFloat(kValueString, 1);
	button = new TMICNButtonElement('SPAD', r.LeftTop(), 0x4b6, more);
	button->tip = tip(14);
	AddElement(button);
	if (kind != 2) {
		r.OffsetBy(37, 0);
		BMessage* message = new BMessage('sM51');
		message->AddInt32(kValueString, -2);
		button = new TMICNButtonElement('COLU', r.LeftTop(), 0x4b8, message);
		button->tip = tip(15);
		AddElement(button);
		r.OffsetBy(22, 0);
		message = new BMessage('sM51');
		message->AddInt32(kValueString, -1);
		button = new TMICNButtonElement('COLV', r.LeftTop(), 0x4b7, message);
		button->tip = tip(16);
		AddElement(button);
	}
	r.OffsetBy(37, 0);
	BMessage* spell = new BMessage('sMC5');
	spell->AddBool("dospell", true);
	button = new TMICNButtonElement('SPCK', r.LeftTop(), 0x4ba, spell);
	button->tip = tip(18);
	AddElement(button);
	r.OffsetBy(22, 0);
	button = new TMICNButtonElement('INVS', r.LeftTop(), 0x4b9, new BMessage('sM5J'));
	button->tip = tip(17);
	AddElement(button);
}


// TPartSS::AddPartBarElements: the address from 29 to 180, the alignment
// menus, the five border menus, Number Formatting and Make Chart, each
// group 15 after the last
void
TPartBar::BuildSheet()
{
	RemoveAllElements();
	if (fSpacing != NULL) {
		fSpacing->RemoveSelf();
		delete fSpacing;
		fSpacing = NULL;
	}
	fText = false;
	fSheet = true;
	TMICNElement* part = new TMICNElement('Part', BPoint(5, 4), PartIcon('SPRD'));
	part->tip = GobeResources::IndexString(4, 23).String();
	AddElement(part);
	AddElement(new TCellAddressElement(BRect(29, 2, 180, 21)));
	BRect r(195, 2, 214, 21);
	AddElement(new TCellAlignElement('HAln', r, 302, 0x423, 1, fPages));
	r.OffsetBy(22, 0);
	AddElement(new TCellAlignElement('VAln', r, 303, 0x424, 2, fPages));
	BPoint at(r.right + 1 + 15, 2);
	static const int32 kBorders[5] = {0x41b, 0x425, 0x426, 0x427, 0x428};
	for (int32 i = 0; i < 5; i++) {
		AddElement(new TCellBorderElement('Bor0' + i, at, kBorders[i], 3 + i));
		at.x += 20 + 3;
	}
	at.x += 15 - 3;
	TMICNButtonElement* number = new TMICNButtonElement('NUMF', at,
		TMICNElement::LoadIcon(0x429, &SheetResources()), new BMessage('sM60'));
	number->tip = IndexString(SheetResources(), 1507, 8);
	AddElement(number);
	at.x += 20 + 15;
	// (making a chart from cells isn't ported: off)
	BMessage* chart = new BMessage('s00T');
	chart->AddInt32("Kind", 'chrt');
	TMICNButtonElement* make = new TMICNButtonElement('chrt', at, PartIcon('chrt'), chart);
	make->tip = IndexString(SheetResources(), 1507, 9);
	make->enabled = false;
	AddElement(make);
}


// TPartSS::PartBarUpdateContents: the selection's name in the address
void
TPartBar::UpdateSheet()
{
	if (TCellAddressElement* address = static_cast<TCellAddressElement*>(GetElement('ADDR'))) {
		std::string name = fPages->SelectionName();
		if (name != address->text) {
			address->text = name;
			DrawElement(address);
		}
	}
}


// TPartBar::UpdateContents, TWPPart::PartBarUpdateContents: the
// paragraphs' alignment if they share one; their line spacing (less down
// to a line or a point, more up to 10 lines or 160 points); a column less
// or more in the main text's pages (1 to 10)
void
TPartBar::Update()
{
	const Document& doc = fPages->Doc();
	if (fPages->fSelectedCell.active && doc.SpreadsheetPart(fPages->fSelectedCell.part) != NULL) {
		if (!fSheet)
			BuildSheet();
		UpdateSheet();
		return;
	}
	const WPPart* wp = fPages->fCaret.active ? doc.WordPart(fPages->fCaret.part) : NULL;
	bool text = wp != NULL && fPages->fCaret.paragraph < wp->paragraphs.size();
	uint16 kind = text ? wp->version98 : 0;
	if (text != fText || kind != fKind || fSheet)
		Build(text, kind);
	if (!text)
		return;

	auto range = fPages->TextSelectionRange();
	size_t first = std::min(range.first.paragraph, wp->paragraphs.size() - 1);
	size_t last = std::min(range.second.paragraph, wp->paragraphs.size() - 1);
	TextStyle style = ResolveTextStyle(doc.styles, wp->paragraphs[first].parStyle);
	int32 alignment = style.alignment;
	for (size_t p = first + 1; p <= last; p++)
		if (ResolveTextStyle(doc.styles, wp->paragraphs[p].parStyle).alignment != alignment)
			alignment = -1;
	if (alignment != fAlignment) {
		fAlignment = alignment;
		SelectElement(alignment >= 0 ? 'ALIG' + alignment : 0, 'ALIG');
	}

	int unit = doc.sheets.sheets.empty() ? 0 : int(doc.sheets.sheets[0].unit);
	if (style.lineSpacing != fLineSpacing || style.lineSpacingMultiple != fLines
			|| unit != fUnit) {
		fLineSpacing = style.lineSpacing;
		fLines = style.lineSpacingMultiple;
		fUnit = unit;
		float points = fLines ? fLineSpacing * 16 : fLineSpacing;
		if (fSpacing != NULL && !fSpacing->IsEditable())
			fSpacing->SetValue(points, fLines ? kLines : unit);
		SetEnabled('SPAC', points > (fLines ? 16 : 1));
		SetEnabled('SPAD', points < 160);
	}

	if (fKind != 2) {
		// TWPPart: the section the insertion point is in
		uint32_t at = 0;
		for (size_t p = 0; p < fPages->fCaret.paragraph; p++)
			at += uint32_t(wp->paragraphs[p].text.size());
		at += uint32_t(fPages->fCaret.offset);
		int32 columns = 1;
		for (const WPSection& section : wp->sections)
			if (section.start <= at)
				columns = int32(section.columns.size());
		bool on = kind == 0 && int(fPages->fCaret.part) == doc.MainPart() && doc.PagePart() >= 0;
		if (columns != fColumns || on != fColumnsOn) {
			fColumns = columns;
			fColumnsOn = on;
			SetEnabled('COLU', on && columns > 1);
			SetEnabled('COLV', on && columns < 10);
		}
	}
}


// TWPPart: the value typed goes to the window as 'sM5D' with "isLines"
void
TPartBar::SpacingEntered(float value, bool lines)
{
	BMessage spacing('sM5D');
	spacing.AddBool("isLines", lines);
	spacing.AddFloat(kValueString, value);
	Send(&spacing);
	fLineSpacing = -1;
}


void
TPartBar::TypeSpacing(const char* text)
{
	if (fSpacing == NULL)
		return;
	fSynchronous = true;
	fSpacing->SetEditable(true, true);
	fSpacing->SetText(text);
	fSpacing->SetEditable(false, true);
	fSynchronous = false;
}


void
TPartBar::SpacingCancelled()
{
	fLineSpacing = -1;
	Update();
}

} // namespace gobe
