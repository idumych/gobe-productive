#include "TPartBar.h"

#include <memory>

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
#include "TInkMenus.h"
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
class TCellAddressElement : public TTextIndicatorElement {
public:
	TCellAddressElement(BRect rect)
		:
		TTextIndicatorElement('ADDR', rect)
	{
		tip = IndexString(SheetResources(), 1507, 0);
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


const ResourceFile&
TableResources()
{
	return GobeResources::Part("Table");
}


// TVertAlignMenuElement (a TMICNMenuElement, MICN 104 of the Table part):
// MENU 200 (Align Top, Center, Bottom: 'sTBB' to 'sTBD'), FixMenu marking
// what the cells have (Align Top for none)
class TTableVAlignElement : public TMenuElement {
public:
	TTableVAlignElement(BRect rect, TPageView* pages)
		:
		TMenuElement('tb01', rect, NULL, TMICNElement::LoadIcon(104, &TableResources())),
		fPages(pages)
	{
		tip = IndexString(TableResources(), 100, 0);
	}

	virtual void FillMenu(BPopUpMenu* menu, TRibbon* ribbon)
	{
		TMenu source(200, TableResources());
		int32 align = fPages->TableVerticalAlignment();
		int32 index = 0;
		while (source.CountItems() > 0) {
			BMenuItem* item = source.RemoveItem((int32)0);
			item->SetMarked(++index == align);
			menu->AddItem(item);
		}
	}

private:
	TPageView* fPages;
};


// TArrowsMenu (a TMICNMenuElement, MICN 0x1c): the arrows of the
// selected lines and paths as line samples, sending 'sf0F' with the
// value (STR# 11, 20-23: none, at the start, at the end, both ends)
class TArrowsElement : public TMenuElement {
public:
	TArrowsElement(BRect rect, TPageView* pages)
		:
		TMenuElement('GRAR', rect, NULL, TMICNElement::LoadIcon(0x1c)),
		fPages(pages)
	{
	}

	// (TStyleManager::FixPropMenu: what all the selection has marked)
	virtual void FillMenu(BPopUpMenu* menu, TRibbon* ribbon)
	{
		TPageView::SelectedPen state;
		fPages->SelectedPens(state);
		for (int32 i = 0; i < 4; i++) {
			BMessage* message = new BMessage('sf0F');
			message->AddInt32(kValueString, i);
			TLineSampleItem* item = new TLineSampleItem(PenType(), i, message);
			item->SetMarked(state.any && state.sameArrows && state.arrows == i);
			menu->AddItem(item);
		}
	}

private:
	TPageView* fPages;
};


// TSSBorderMenuElement (a TMICNMenuBarElement): a color set (MICN 0x16)
// and the transparencies (MENU 103, MICN 0x20) for the outline ('sM6A',
// 'sM6B') or one side ('sM68', 'sM69' with "border": 0 left, 1 top, 2
// right, 3 bottom)
class TCellBorderElement : public TMenuElement {
public:
	TCellBorderElement(uint32 id, BPoint where, int32 micn, int32 tipIndex, int32 side)
		:
		TMenuElement(id, BRect(where, where), NULL,
			TMICNElement::LoadIcon(micn, &SheetResources())),
		fSide(side)
	{
		tip = IndexString(SheetResources(), 1507, tipIndex);
	}

	virtual void FillMenu(BPopUpMenu* menu, TRibbon* ribbon) {}

	virtual bool MouseDown(TRibbon* ribbon, BPoint where)
	{
		BPopUpMenu* menu = new BPopUpMenu("border", false, false, B_ITEMS_IN_ROW);
		BMessage color(fSide < 0 ? 'sM6A' : 'sM68');
		if (fSide >= 0)
			color.AddInt32("border", fSide);
		menu->AddItem(new TIconMenuItem(0x16, ColorSetMenu(color)));
		BMenu* transparency = new BMenu("");
		TMenu source(103, true);
		while (source.CountItems() > 0) {
			BMenuItem* item = source.RemoveItem((int32)0);
			if (BMessage* message = item->Message()) {
				// Other… ('sM0U'): the value 'sM0U' asks for the dialog
				// (TPartSS::HandleMessage, TDialogGetTransparent)
				int32 value = message->what == 'sM0U' ? int32('sM0U')
					: message->GetInt32(kValueString, 0);
				message->MakeEmpty();
				message->what = fSide < 0 ? 'sM6B' : 'sM69';
				message->AddInt32(kValueString, value);
				if (fSide >= 0)
					message->AddInt32("border", fSide);
			}
			transparency->AddItem(item);
		}
		menu->AddItem(new TIconMenuItem(0x20, transparency));
		ribbon->DrawElement(this, true);
		ribbon->Sync();
		BPoint at = ribbon->ConvertToScreen(BPoint(rect.left - 1, rect.top - 2));
		BMenuItem* chosen = menu->Go(at, false, true, false);
		ribbon->DrawElement(this, false);
		if (chosen != NULL && chosen->Message() != NULL)
			ribbon->Send(chosen->Message());
		delete menu;
		return true;
	}

private:
	int32 fSide;
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


const ResourceFile&
ImageResources()
{
	return GobeResources::Part("ImageProcessing");
}


// TIMColorGadget ('kolr', 30, 1 to 57, 24): the foreground color's square
// over the background's (TIMColorGadget::ForeRect, BackRect), each outlined
// in black, and the arrows that swap them (MICN 100 of the image part) at
// its top right. A click on a square edits its color (TDialogEditColor:
// 'sI01' the foreground, 'sI00' the background), elsewhere swaps them
// ('sI09'); the tip is the image part's STR# 3.
class TIMColorGadget : public TRibbonElement {
public:
	TIMColorGadget(TPageView* pages)
		:
		TRibbonElement('kolr', BRect(30, 1, 57, 24)),
		fPages(pages),
		fArrows(TMICNElement::LoadIcon(100, &ImageResources()))
	{
		tip = IndexString(ImageResources(), 3, 0);
	}

	BRect ForeRect() const
	{
		float top = rect.top + 3, right = rect.right - 4;
		return BRect(rect.left, top, right - (right - rect.left) / 3,
			rect.bottom - (rect.bottom - top) / 3);
	}

	BRect BackRect() const
	{
		float top = rect.top + 3, right = rect.right - 4;
		return BRect((right - rect.left) / 3 + rect.left, (rect.bottom - top) / 3 + top, right,
			rect.bottom);
	}

	virtual void Draw(BView* view, bool down)
	{
		rgb_color fore = {0, 0, 0, 255}, back = {255, 255, 255, 255};
		fPages->ImageColors(fore, back);
		view->SetHighColor(back);
		view->FillRect(BackRect());
		view->SetHighColor(0, 0, 0);
		view->StrokeRect(BackRect());
		view->SetHighColor(fore);
		view->FillRect(ForeRect());
		view->SetHighColor(0, 0, 0);
		view->StrokeRect(ForeRect());
		if (fArrows != NULL) {
			view->SetDrawingMode(B_OP_OVER);
			view->DrawBitmap(fArrows.get(), BPoint(rect.right - 9, rect.top));
			view->SetDrawingMode(B_OP_COPY);
		}
	}

	virtual bool Clickable() const { return true; }

	virtual bool MouseDown(TRibbon* ribbon, BPoint where)
	{
		uint32 what = ForeRect().Contains(where) ? 'sI01' : BackRect().Contains(where) ? 'sI00'
			: 'sI09';
		BMessage message(what);
		ribbon->Send(&message);
		return true;
	}

private:
	TPageView* fPages;
	std::unique_ptr<BBitmap> fArrows;
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
TPartBar::Build(bool text, uint16 kind, bool table)
{
	RemoveAllElements();
	SetHeight(24);
	if (fSpacing != NULL) {
		fSpacing->RemoveSelf();
		delete fSpacing;
		fSpacing = NULL;
	}
	fText = text;
	fTable = table;
	fSheet = false;
	fDrawing = false;
	fImage = false;
	fKind = kind;
	fAlignment = -2;
	fLineSpacing = -1;
	fColumns = -1;
	TMICNElement* part = new TMICNElement('Part', BPoint(5, 4),
		text ? PartIcon(table ? 'TBLE' : 'WORD') : NULL);
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
	if (!table)
		return;

	// TTablePart::AddPartBarElements: the vertical alignment, Insert
	// Column, Insert Row, Merge Cells and Split Cell (MICN 100-103, the
	// tips STR# 100 of the Table part)
	BRect menu(r.right + 12, 2, r.right + 12 + 25, 21);
	AddElement(new TTableVAlignElement(menu, fPages));
	static const uint32 kCommands[4] = {'sTB3', 'sTB2', 'sTB0', 'sTB1'};
	static const float kAfter[4] = {26, 22, 26, 22};
	BPoint at(menu.left, 2);
	for (int32 i = 0; i < 4; i++) {
		at.x += kAfter[i];
		button = new TMICNButtonElement('tb02' + i, at,
			TMICNElement::LoadIcon(100 + i, &TableResources()), new BMessage(kCommands[i]));
		button->tip = IndexString(TableResources(), 100, i + 1);
		AddElement(button);
	}
}


// TPartSS::AddPartBarElements: the address from 29 to 180, the alignment
// menus, the five border menus, Number Formatting and Make Chart, each
// group 15 after the last
void
TPartBar::BuildSheet()
{
	RemoveAllElements();
	SetHeight(24);
	fImage = false;
	if (fSpacing != NULL) {
		fSpacing->RemoveSelf();
		delete fSpacing;
		fSpacing = NULL;
	}
	fText = false;
	fSheet = true;
	fDrawing = false;
	TMICNElement* part = new TMICNElement('Part', BPoint(5, 4), PartIcon('SPRD'));
	part->tip = GobeResources::IndexString(4, 23).String();
	AddElement(part);
	AddElement(new TCellAddressElement(BRect(29, 2, 180, 21)));
	BRect r(195, 2, 214, 21);
	AddElement(new TCellAlignElement('HAln', r, 302, 0x423, 1, fPages));
	r.OffsetBy(22, 0);
	AddElement(new TCellAlignElement('VAln', r, 303, 0x424, 2, fPages));
	BPoint at(r.right + 1 + 15, 2);
	// the outline, then left, top, right and bottom
	static const int32 kBorders[5] = {0x41b, 0x425, 0x426, 0x427, 0x428};
	for (int32 i = 0; i < 5; i++) {
		AddElement(new TCellBorderElement('Bor0' + i, at, kBorders[i], 3 + i, i - 1));
		at.x += 20 + 3;
	}
	at.x += 15 - 3;
	TMICNButtonElement* number = new TMICNButtonElement('NUMF', at,
		TMICNElement::LoadIcon(0x429, &SheetResources()), new BMessage('sM60'));
	number->tip = IndexString(SheetResources(), 1507, 8);
	AddElement(number);
	at.x += 20 + 15;
	// Make Chart ('s00T' Kind 'chrt')
	BMessage* chart = new BMessage('s00T');
	chart->AddInt32("Kind", 'chrt');
	TMICNButtonElement* make = new TMICNButtonElement('chrt', at, PartIcon('chrt'), chart);
	make->tip = IndexString(SheetResources(), 1507, 9);
	AddElement(make);
}


// TPartIM::AddPartBarElements: the part's icon, the colors (TIMColorGadget),
// then a TIMToolbarElement for each tool of TPartIMFactory (its selection
// tools, then its others), 20 apart from 70, 4, five to a row 23 apart:
// MICN 0x5dc on of the image part, their tips its STR# 100 on; 'sI07' with
// the tool's index, the one in use down (all but Find Color stay down)
void
TPartBar::BuildImage()
{
	RemoveAllElements();
	if (fSpacing != NULL) {
		fSpacing->RemoveSelf();
		delete fSpacing;
		fSpacing = NULL;
	}
	fText = false;
	fSheet = false;
	fDrawing = false;
	fImage = true;
	TMICNElement* part = new TMICNElement('Part', BPoint(5, 4), PartIcon('BITS'));
	AddElement(part);
	AddElement(new TIMColorGadget(fPages));
	// (MarqueeTool, LassoTool, MagicWandTool, MagnifyTool, FindColorTool,
	// PencilTool, EraserTool, EyeDropperTool, BucketTool, AirBrushTool: their
	// ToolIconID, TipText's STR#)
	static const int32 kIcons[10] = {0x5dd, 0x5e1, 0x5e0, 0x5e3, 0x5e2, 0x5dc, 0x5e4, 0x5df,
		0x5de, 0x5e5};
	static const int32 kTips[10] = {103, 104, 107, 108, 109, 100, 101, 106, 105, 102};
	for (int32 i = 0; i < 10; i++) {
		BMessage* message = new BMessage('sI07');
		message->AddInt32("Tool", i);
		TMICNButtonElement* tool = new TMICNButtonElement('IM00' + i,
			BPoint(70 + 20 * (i % 5), 4 + 23 * (i / 5)),
			TMICNElement::LoadIcon(kIcons[i], &ImageResources()), message);
		tool->tip = IndexString(ImageResources(), kTips[i], 0);
		tool->family = 'sI0G';
		tool->selectable = i != 4;
		tool->selected = i == fPages->ImageTool();
		AddElement(tool);
	}
	// (the PartBar as high as its elements and 2)
	SetHeight(50);
}


// TPartGR::AddPartBarElements: from 29, four vertical alignments ('GRAV':
// top, middle, bottom, to each other: "AlignY" 1-4), 15 on four
// horizontal ones ('GRAH': "AlignX" 1-4), then Text Wrap ('GRTW', MICN
// 0x127) and the arrows ('GRAR'), each group 15 after the last; the tips
// STR# 1103, 7-16
void
TPartBar::BuildDrawing(uint32 kind)
{
	RemoveAllElements();
	SetHeight(24);
	fImage = false;
	if (fSpacing != NULL) {
		fSpacing->RemoveSelf();
		delete fSpacing;
		fSpacing = NULL;
	}
	fText = false;
	fSheet = false;
	fDrawing = true;
	fDrawingKind = kind;
	fShapeCount = size_t(-1);
	TMICNElement* part = new TMICNElement('Part', BPoint(5, 4), PartIcon(kind));
	part->tip = GobeResources::IndexString(4, 23).String();
	AddElement(part);
	auto tip = [](int32 index) { return std::string(GobeResources::IndexString(1103, index).String()); };
	BPoint at(29, 2);
	for (int32 axis = 0; axis < 2; axis++) {
		for (int32 i = 0; i < 4; i++) {
			BMessage* message = new BMessage('sM44');
			message->AddInt32(axis == 0 ? "AlignY" : "AlignX", i + 1);
			TMICNButtonElement* button = new TMICNButtonElement(
				(axis == 0 ? 'GRAV' : 'GRAH') + i, at, (axis == 0 ? 0x11f : 0x123) + i, message);
			button->tip = tip((axis == 0 ? 7 : 11) + i);
			AddElement(button);
			at.x += 22;
		}
		at.x += 15;
	}
	// Text Wrap with the part's GRConfigFlags bit 0 (a chart's clears it)
	if (kind != 'chrt') {
		TMICNButtonElement* wrap = new TMICNButtonElement('GRTW', at, 0x127,
			new BMessage('sf0U'));
		wrap->tip = tip(15);
		AddElement(wrap);
		at.x += 22;
	}
	at.x += 15;
	TArrowsElement* arrows = new TArrowsElement(BRect(at, at + BPoint(19, 19)), fPages);
	arrows->tip = tip(16);
	AddElement(arrows);
}


// TPartGR::PartBarUpdateContents: the alignments with more than one
// shape selected, Text Wrap and the arrows with any
void
TPartBar::UpdateDrawing()
{
	size_t count = fPages->SelectedShapeCount();
	if (count == fShapeCount)
		return;
	fShapeCount = count;
	for (int32 i = 0; i < 4; i++) {
		SetEnabled('GRAV' + i, count > 1);
		SetEnabled('GRAH' + i, count > 1);
	}
	SetEnabled('GRTW', count > 0);
	SetEnabled('GRAR', count > 0);
}


// TPartSS::PartBarUpdateContents: the selection's name in the address
void
TPartBar::UpdateSheet()
{
	if (TCellAddressElement* address = static_cast<TCellAddressElement*>(GetElement('ADDR'))) {
		std::string name = fPages->SelectionName();
		if (name != address->text) {
			address->text = name;
			Invalidate(address->rect);
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
	// an image document's image (TPartIM): its tools
	int imageMain = doc.MainPart();
	if (imageMain >= 0 && doc.parts.parts[size_t(imageMain)].kind == Sig("BITS")
			&& fPages->ActiveChartPart() < 0) {
		if (!fImage)
			BuildImage();
		else {
			SelectElement('IM00' + fPages->ImageTool(), 'sI0G');
			if (TRibbonElement* colors = GetElement('kolr'))
				DrawElement(colors);
		}
		return;
	}
	// a chart entered: its drawing's (TChartPart is a TPartGR)
	if (fPages->ActiveChartPart() >= 0) {
		if (!fDrawing || fDrawingKind != 'chrt')
			BuildDrawing('chrt');
		UpdateDrawing();
		return;
	}
	if (fPages->fSelectedCell.active && doc.SpreadsheetPart(fPages->fSelectedCell.part) != NULL) {
		if (!fSheet)
			BuildSheet();
		UpdateSheet();
		return;
	}
	const WPPart* wp = fPages->fCaret.active ? doc.WordPart(fPages->fCaret.part) : NULL;
	bool text = wp != NULL && fPages->fCaret.paragraph < wp->paragraphs.size();
	// the drawing's: shapes selected, or a drawing with no text being typed
	int main = doc.MainPart();
	if (!text && (fPages->ShapesSelected()
			|| (main >= 0 && (doc.parts.parts[size_t(main)].kind == Sig("GRFX")
				|| doc.parts.parts[size_t(main)].kind == Sig("PRES"))))) {
		if (!fDrawing || fDrawingKind != 'GRFX')
			BuildDrawing('GRFX');
		UpdateDrawing();
		return;
	}
	uint16 kind = text ? wp->version98 : 0;
	bool table = text && doc.TableOfText(fPages->fCaret.part) >= 0;
	if (text != fText || kind != fKind || table != fTable || fSheet || fDrawing)
		Build(text, kind, table);
	if (!text)
		return;
	if (table) {
		// TTablePart::PartBarUpdateContents: Merge Cells and Split Cell
		bool merge, split;
		fPages->TableMenuState(merge, split);
		SetEnabled('tb04', merge);
		SetEnabled('tb05', split);
	}

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
