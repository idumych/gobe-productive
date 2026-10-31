#include "TFormatBar.h"

#include <MenuItem.h>
#include <PopUpMenu.h>
#include <Window.h>

#include <algorithm>
#include <cmath>
#include <cstring>

#include "Document.h"
#include "GobeResources.h"
#include "StyleText.h"
#include "Styles.h"
#include "SSStyle.h"
#include "TDocWindow.h"
#include "TInkMenus.h"
#include "Names.h"
#include "TMenu.h"
#include "TPageView.h"

namespace gobe {

namespace {

const rgb_color kPanel = {0xd8, 0xd8, 0xd8, 255};
const rgb_color kBlack = {0, 0, 0, 255};
const rgb_color kWhite = {255, 255, 255, 255};
const rgb_color kWash = {0xa0, 0xa0, 0xa0, 255};


} // namespace


// a FormatBar menu (TMenuElement), filled from the FormatBar's state
class TFormatMenuElement : public TMenuElement {
public:
	TFormatMenuElement(uint32 id, BRect rect, const char* label, int32 micn = 0)
		:
		TMenuElement(id, rect, label, micn != 0 ? TMICNElement::LoadIcon(micn) : NULL)
	{
	}

	virtual void FillMenu(BPopUpMenu* menu, TRibbon* ribbon)
	{
		Fill(menu, static_cast<TFormatBar*>(ribbon));
	}

	// TMenuElement::FixMenu: the items, marked as the selection has them
	virtual void Fill(BPopUpMenu* menu, TFormatBar* bar) = 0;
};


namespace {

// moves a MENU's items into `menu`
void
AddMenuItems(BMenu* menu, int32 id)
{
	TMenu source(id, true);
	while (source.CountItems() > 0)
		menu->AddItem(source.RemoveItem((int32)0));
}


// the style of the caret's text (the paragraph's, or the run's at the
// insertion point)
bool
CaretStyles(TPageView* pages, TextStyle& character, uint32_t& paragraphStyle)
{
	const Document& doc = pages->Doc();
	const WPPart* wp = pages->fCaret.active ? doc.WordPart(pages->fCaret.part) : NULL;
	if (wp == NULL || pages->fCaret.paragraph >= wp->paragraphs.size())
		return false;
	// (TDocWindow::CreateActivePartStyleIterator: the style Edit Styles
	// has chosen, TEditStyleIterator)
	uint32_t edit = pages->EditStyle();
	if (edit < doc.styles.styles.size()) {
		character = ResolveTextStyle(doc.styles, edit);
		paragraphStyle = edit;
		return true;
	}
	auto range = pages->TextSelectionRange();
	character = pages->StyleAt(range.first);
	paragraphStyle = wp->paragraphs[std::min(range.first.paragraph,
		wp->paragraphs.size() - 1)].parStyle;
	return true;
}


class TFontElement : public TFormatMenuElement {
public:
	TFontElement(BRect rect)
		:
		TFormatMenuElement('Font', rect, GobeResources::IndexString(5, 14).String())
	{
		tip = GobeResources::IndexString(4, 0).String();
	}

	virtual void Fill(BPopUpMenu* menu, TFormatBar* bar)
	{
		TFontMenu fonts;
		while (fonts.CountItems() > 0) {
			BMenuItem* item = fonts.RemoveItem((int32)0);
			item->SetMarked(label == item->Label());
			menu->AddItem(item);
		}
	}
};


class TSizeElement : public TFormatMenuElement {
public:
	TSizeElement(BRect rect)
		:
		TFormatMenuElement('Size', rect, GobeResources::IndexString(5, 15).String())
	{
		tip = GobeResources::IndexString(4, 1).String();
	}

	// TStyleManager::FixPropMenu(TSiz): the size the selection has
	virtual void Fill(BPopUpMenu* menu, TFormatBar* bar)
	{
		AddMenuItems(menu, 101);
		int32 size = atoi(label.c_str());
		for (int32 i = 0; i < menu->CountItems(); i++) {
			BMenuItem* item = menu->ItemAt(i);
			int32 value;
			if (item->Message() != NULL
					&& item->Message()->FindInt32(kValueString, &value) == B_OK)
				item->SetMarked(value == size);
		}
	}
};


class TFaceElement : public TFormatMenuElement {
public:
	TFaceElement(BPoint where)
		:
		TFormatMenuElement('Face', BRect(where, where), NULL, 0x15)
	{
		tip = GobeResources::IndexString(4, 2).String();
	}

	// TFontFaceMenuElement::FixMenu: the faces the text has
	virtual void Fill(BPopUpMenu* menu, TFormatBar* bar)
	{
		AddMenuItems(menu, 102);
		TextStyle style;
		uint32_t paragraph;
		if (!CaretStyles(bar->Pages(), style, paragraph))
			return;
		for (int32 i = 0; i < menu->CountItems(); i++) {
			BMenuItem* item = menu->ItemAt(i);
			int32 value;
			if (item->Message() != NULL
					&& item->Message()->FindInt32(kValueString, &value) == B_OK)
				item->SetMarked(value == 1 ? style.faceFlags == 0
					: (style.faceFlags & uint32(value)) != 0);
		}
	}
};


// TTextMenuElement (a TMICNMenuBarElement): the ink sets' menus behind a
// row of their icons (color 22, pattern 24, gradient 25, transparency 32)
class TInkElement : public TFormatMenuElement {
public:
	TInkElement(uint32 id, BRect rect, int32 micn, uint32 colorCommand, int32 tipIndex)
		:
		TFormatMenuElement(id, rect, NULL, micn),
		fColorCommand(colorCommand)
	{
		tip = GobeResources::IndexString(4, tipIndex).String();
	}

	virtual void Fill(BPopUpMenu* menu, TFormatBar* bar)
	{
	}

	virtual bool MouseDown(TRibbon* ribbon, BPoint where)
	{
		BPopUpMenu* menu = new BPopUpMenu("ink", false, false, B_ITEMS_IN_ROW);
		static const int32 kIcons[4] = {0x16, 0x18, 0x19, 0x20};
		for (int32 i = 0; i < 4; i++) {
			BMenu* palette;
			if (i == 0) {
				// TInkSetMenu: the active color set (COLR 0) as a matrix
				palette = ColorSetMenu(BMessage(fColorCommand));
			} else {
				// (the pattern and gradient sets and transparency aren't
				// ported yet)
				palette = new BMenu("");
				palette->SetEnabled(false);
			}
			menu->AddItem(new TIconMenuItem(kIcons[i], palette));
		}
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
	uint32 fColorCommand;
};


class TStylesElement : public TFormatMenuElement {
public:
	TStylesElement(BRect rect)
		:
		TFormatMenuElement('Styl', rect, GobeResources::IndexString(5, 16).String())
	{
		tip = GobeResources::IndexString(4, 9).String();
	}

	// TStylesMenuElement::FixMenu: MENU 100, then the named styles of the
	// active part's kinds (TWPPart::ActiveStyles: paragraph, basic; not the
	// internal ones), a separator between kinds; those applied to all of
	// the selection marked and sending 'sf0V' (unapply), those applied to
	// part of it neither (TMenuItem::SetMixed isn't drawn)
	virtual void Fill(BPopUpMenu* menu, TFormatBar* bar)
	{
		AddMenuItems(menu, 100);
		TPageView* pages = bar->Pages();
		const Document& doc = pages->Doc();
		TextStyle style;
		uint32_t paragraph = 0;
		bool text = CaretStyles(pages, style, paragraph);
		std::vector<std::pair<std::string, bool>> applied;
		if (text && pages->EditStyle() >= doc.styles.styles.size()) {
			if (const WPPart* wp = doc.WordPart(pages->fCaret.part)) {
				auto range = pages->TextSelectionRange();
				applied = TextSelectionStyles(doc.styles, *wp, range.first, range.second).applied;
			}
		}
		bool first = true;
		for (uint32_t type : {Sig("Para"), Sig("Basi")}) {
			std::vector<size_t> named;
			for (size_t i = 0; i < doc.styles.styles.size(); i++) {
				const Style& s = doc.styles.styles[i];
				if (!s.name.empty() && s.value10 == type && !(s.value8 & 0x400))
					named.push_back(i);
			}
			// (TStyleIDIterator: the manager's sorted table, by name)
			std::stable_sort(named.begin(), named.end(), [&](size_t a, size_t b) {
				return strcmp(doc.styles.styles[a].name.c_str(),
					doc.styles.styles[b].name.c_str()) < 0;
			});
			for (size_t i : named) {
				const Style& s = doc.styles.styles[i];
				if (s.value10 != type)
					continue;
				if (!first && &s == &doc.styles.styles[named.front()])
					menu->AddSeparatorItem();
				first = false;
				int state = 0;		// 1 applied, 2 to part
				for (const auto& a : applied)
					if (a.first == s.name)
						state = a.second ? 2 : 1;
				BMessage* message = new BMessage(state == 1 ? 'sf0V' : 'sf0L');
				message->AddInt32(kStyleLevelStr, -1);
				message->AddString(kStyleNameStr, s.name.c_str());
				message->AddInt32(kStyleTypeStr, int32(s.value10));
				BMenuItem* item = new BMenuItem(s.name.c_str(), message);
				item->SetMarked(state == 1);
				item->SetEnabled(text);
				menu->AddItem(item);
			}
		}
	}
};


class TNamesElement : public TFormatMenuElement {
public:
	TNamesElement(BPoint where)
		:
		TFormatMenuElement('Name', BRect(where, where), NULL, 0x25)
	{
		tip = GobeResources::IndexString(4, 10).String();
	}

	// TNamesMenuElement::FixMenu: MENU 111 (Name Selection… with cells
	// selected; Edit Names… isn't ported), then the document's names
	// (TSelectionMgr::GetNamedSelections), each sending 'sfN2' with "Name"
	virtual void Fill(BPopUpMenu* menu, TFormatBar* bar)
	{
		AddMenuItems(menu, 111);
		bool cells = bar->Pages()->fSelectedCell.active;
		for (int32 i = 0; i < menu->CountItems(); i++) {
			BMenuItem* item = menu->ItemAt(i);
			item->SetEnabled(item->Message() != NULL && item->Message()->what == 'sfN0' && cells);
		}
		std::vector<std::string> names = SelectionNames(bar->Pages()->Doc());
		if (!names.empty())
			menu->AddSeparatorItem();
		for (const std::string& name : names) {
			BMessage* message = new BMessage('sfN2');
			message->AddString("Name", name.c_str());
			menu->AddItem(new BMenuItem(name.c_str(), message));
		}
	}
};

} // namespace


// TGraphicIndElement: the ink the selection has, in a sunken frame (the
// panel's gray for none, a dash when it differs)
class TInkIndicatorElement : public TRibbonElement {
public:
	TInkIndicatorElement(uint32 id, BRect rect, int32 tipIndex)
		:
		TRibbonElement(id, rect)
	{
		tip = GobeResources::IndexString(4, tipIndex).String();
	}

	virtual void Draw(BView* view, bool down)
	{
		BRect r = rect;
		// TIndicatorElement::DrawFrame
		view->SetHighColor(kWhite);
		view->StrokeLine(BPoint(r.left, r.bottom), BPoint(r.right, r.bottom));
		view->StrokeLine(BPoint(r.right, r.top), BPoint(r.right, r.bottom));
		view->SetHighColor(kBlack);
		view->StrokeLine(BPoint(r.left, r.top), BPoint(r.right - 1, r.top));
		view->StrokeLine(BPoint(r.left, r.top), BPoint(r.left, r.bottom - 1));
		view->SetHighColor(has ? color : kPanel);
		view->FillRect(r.InsetByCopy(1, 1));
		if (mixed) {
			// TGraphicIndElement::DrawMixed
			float y = roundf(r.top + r.Height() * 0.5f);
			view->SetHighColor(kBlack);
			view->StrokeLine(BPoint(r.left + 4, y), BPoint(r.right - 4, y));
		}
		view->SetHighColor(kBlack);
	}

	bool has = false;
	bool mixed = false;
	rgb_color color = kPanel;
};


TFormatBar::TFormatBar(TPageView* pages)
	:
	TRibbon("FormatBar"),
	fPages(pages)
{
	// TFormatBar::LoadAllElements
	BRect r(5, 2, 125, 21);
	fFont = new TFontElement(r);
	AddElement(fFont);
	r.left = r.right + 3;
	r.right = r.left + 30;
	fSize = new TSizeElement(r);
	AddElement(fSize);
	TFaceElement* face = new TFaceElement(BPoint(r.right + 3, 2));
	AddElement(face);
	r.left = face->rect.right + 15;
	r.right = r.left + 20 - 1;
	AddElement(new TInkElement('TEXT', r, 0x14, 'sf02', 3));
	BRect indicator(r.right + 3, 3, r.right + 3 + 17, 20);
	fTextInk = new TInkIndicatorElement('cTXT', indicator, 4);
	AddElement(fTextInk);
	r.left = indicator.right + 15;
	r.right = r.left + 20 - 1;
	// (the fill ink is on for cells; fill and pen for graphics and frames
	// aren't ported yet)
	TInkElement* fill = new TInkElement('FILL', r, 0x17, 'sf03', 5);
	fill->enabled = false;
	AddElement(fill);
	indicator.Set(r.right + 3, 3, r.right + 3 + 17, 20);
	fFillInk = new TInkIndicatorElement('cFIL', indicator, 6);
	fFillInk->enabled = false;
	AddElement(fFillInk);
	r.left = indicator.right + 10;
	r.right = r.left + 20 - 1;
	TInkElement* pen = new TInkElement('PEN ', r, 0x1a, 'sf06', 7);
	pen->enabled = false;
	AddElement(pen);
	indicator.Set(r.right + 3, 3, r.right + 3 + 17, 20);
	fPenInk = new TInkIndicatorElement('cPEN', indicator, 8);
	fPenInk->enabled = false;
	AddElement(fPenInk);
	// TTransparencyMenuElement (MENU 103, 'sf0S': the shapes')
	class TTransparencyElement : public TFormatMenuElement {
	public:
		TTransparencyElement(BPoint where)
			:
			TFormatMenuElement('Tran', BRect(where, where), NULL, 0x20)
		{
			tip = GobeResources::IndexString(4, 11).String();
		}
		virtual void Fill(BPopUpMenu* menu, TFormatBar* bar)
		{
			AddMenuItems(menu, 103);
		}
	};
	TTransparencyElement* transparency = new TTransparencyElement(
		BPoint(indicator.right + 10, 2));
	transparency->enabled = false;
	AddElement(transparency);
	r.left = transparency->rect.right + 15;
	r.right = r.left + 100;
	fStyles = new TStylesElement(r);
	AddElement(fStyles);
	fNames = new TNamesElement(BPoint(r.right + 7, 2));
	AddElement(fNames);
}


// TSeparatorElement 'B2Se': the bar's bottom edge
void
TFormatBar::Draw(BRect updateRect)
{
	TRibbon::Draw(updateRect);
	BRect bounds = Bounds();
	SetHighColor(0xa0, 0xa0, 0xa0);
	StrokeLine(BPoint(bounds.left, bounds.bottom), BPoint(bounds.right, bounds.bottom));
	SetHighColor(0xc8, 0xc8, 0xc8);
	StrokeLine(BPoint(bounds.left, bounds.bottom - 1), BPoint(bounds.right, bounds.bottom - 1));
}


// TFormatBar::UpdateContents: the selection's style name ("+" when the
// text has more than it), font, size and text ink, each the element's
// own label when the selection's differ
void
TFormatBar::Update()
{
	TextStyle style;
	uint32_t paragraph = 0;
	bool text = CaretStyles(fPages, style, paragraph);
	const Document& doc = fPages->Doc();

	std::string font = GobeResources::IndexString(5, 14).String();
	std::string size = GobeResources::IndexString(5, 15).String();
	std::string name = GobeResources::IndexString(5, 16).String();
	bool hasInk = false, mixedInk = false;
	rgb_color ink = kPanel;
	if (text) {
		TextPosition from, to;
		bool selection = fPages->TextSelection(from, to);
		font = style.family;
		size = std::to_string(int(roundf(style.size)));
		hasInk = true;
		ink = {style.color.r, style.color.g, style.color.b, 255};
		if (selection && fPages->EditStyle() >= doc.styles.styles.size()) {
			// the selection's runs: differing values show the label
			TextStyle last = fPages->StyleAt(to);
			if (last.family != style.family)
				font = GobeResources::IndexString(5, 14).String();
			if (last.size != style.size)
				size = GobeResources::IndexString(5, 15).String();
			mixedInk = last.color != style.color;
		}
		// TStyleManager::GetActiveStyle (of the style being edited: it)
		uint32_t edit = fPages->EditStyle();
		if (edit < doc.styles.styles.size())
			name = doc.styles.styles[edit].name;
		else if (const WPPart* wp = doc.WordPart(fPages->fCaret.part)) {
			auto range = fPages->TextSelectionRange();
			name = TextSelectionStyles(doc.styles, *wp, range.first, range.second).active;
		}
	}
	// cells: the active cell's font, size and inks, the fill ink on
	CellStyle cell;
	bool cells = !text && fPages->ActiveCellStyle(cell);
	bool hasFill = false;
	rgb_color fill = kPanel;
	if (cells) {
		font = cell.text.family;
		size = std::to_string(int(roundf(cell.text.size)));
		hasInk = true;
		ink = {cell.text.color.r, cell.text.color.g, cell.text.color.b, 255};
		hasFill = cell.fill.set;
		if (hasFill)
			fill = {cell.fill.color.r, cell.fill.color.g, cell.fill.color.b, 255};
	}
	if (TRibbonElement* element = GetElement('FILL')) {
		if (element->enabled != cells) {
			SetEnabled('FILL', cells);
			SetEnabled('cFIL', cells);
		}
	}
	if (hasFill != fFillInk->has || fill != fFillInk->color) {
		fFillInk->has = hasFill;
		fFillInk->color = fill;
		Invalidate(fFillInk->rect);
	}
	for (auto [element, value] : {std::make_pair(fFont, font), std::make_pair(fSize, size),
			std::make_pair(fStyles, name)}) {
		if (element->label != value) {
			element->label = value;
			Invalidate(element->rect);
		}
	}
	if (hasInk != fTextInk->has || mixedInk != fTextInk->mixed || ink != fTextInk->color) {
		fTextInk->has = hasInk;
		fTextInk->mixed = mixedInk;
		fTextInk->color = ink;
		Invalidate(fTextInk->rect);
	}
}


void
TFormatBar::ChooseColor(int32 index, bool fill)
{
	ColorSet set = LoadColorSet(0);
	if (index < 0 || size_t(index) >= set.colors.size())
		return;
	BMessage message(fill ? 'sf03' : 'sf02');
	message.AddString("ink name", "Standard");
	message.AddData("color", B_RGB_COLOR_TYPE, &set.colors[index], 4);
	message.AddInt32("ink index", index);
	fSynchronous = true;
	Send(&message);
	fSynchronous = false;
}


void
TFormatBar::ChooseStyle(const char* name)
{
	BMessage message('sf0L');
	message.AddInt32(kStyleLevelStr, -1);
	message.AddString(kStyleNameStr, name);
	message.AddInt32(kStyleTypeStr, int32(Sig("Para")));
	fSynchronous = true;
	Send(&message);
	fSynchronous = false;
}


std::string
TFormatBar::NamesMenu(const char* choose)
{
	BPopUpMenu menu("names");
	fNames->Fill(&menu, this);
	std::string shown;
	for (int32 i = 0; i < menu.CountItems(); i++) {
		BMenuItem* item = menu.ItemAt(i);
		if (choose != NULL && strcmp(item->Label(), choose) == 0 && item->Message() != NULL) {
			fSynchronous = true;
			Send(item->Message());
			fSynchronous = false;
		}
		shown += std::string(i > 0 ? "|" : "") + (item->Label()[0] != '\0' ? item->Label() : "-")
			+ (item->IsEnabled() ? "" : "(off)");
	}
	return shown;
}


std::string
TFormatBar::Shown(const std::string& what) const
{
	if (what == "font")
		return fFont->label;
	if (what == "size")
		return fSize->label;
	if (what == "style")
		return fStyles->label;
	if (what == "ink") {
		char text[16];
		snprintf(text, sizeof text, "#%02x%02x%02x", fTextInk->color.red, fTextInk->color.green,
			fTextInk->color.blue);
		return text;
	}
	return "";
}

} // namespace gobe
