#include "TWPRuler.h"

#include <MenuItem.h>
#include <PopUpMenu.h>
#include <Window.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "Document.h"
#include "Editing.h"
#include "GobeResources.h"
#include "TMenu.h"
#include "TPageView.h"
#include "TWPDialogs.h"

namespace gobe {

namespace {

// TRuler::UnitSize: points per unit (in, cm, mm, pc, pt, li); "lines"
// count as points on the ruler
const float kUnitSize[6] = {72.0f, 28.3464f, 2.83464f, 12.0f, 1.0f, 1.0f};

// TRuler::drawSeparator, TWPRuler::Draw
const rgb_color kGray = {0x70, 0x70, 0x70, 255};
const rgb_color kBlack = {0, 0, 0, 255};

// the tab menu's commands (MENU 120)
enum {
	kCmdLeftTab = 3, kCmdAlignTab = 6, kCmdDeleteTab = 8, kCmdEditTab = 9
};

// GridTo(float, float): to the nearest multiple of `grid`
float
SnapTo(float value, float grid)
{
	float r = fmodf(value, grid);
	if (r < grid * 0.5f)
		return value - r;
	return grid - r + value;
}


// TPropParTab::GetTypeName: "Left Tab", "Center Char Fill Tab", ...
std::string
TabName(const TabStop& tab)
{
	static const int kTypeName[5] = {1, 3, 2, 5, 6};
	std::string name;
	if (tab.type >= 0 && tab.type < 5)
		name = GobeResources::IndexString(1304, kTypeName[tab.type]).String();
	if (tab.fill == 1 || tab.fill == 2)
		name += GobeResources::IndexString(1304, tab.fill == 1 ? 7 : 8).String();
	return name + GobeResources::IndexString(1304, 9).String();
}


// TFormatCommand::CommandName for one property set or cleared: "Set " or
// "Clear " (STR# 5) and its name
std::string
CommandName(bool set, const std::string& name)
{
	return std::string(GobeResources::IndexString(5, set ? 5 : 6).String()) + name;
}


// ...and for more than one: "Set Format" (STR# 3)
std::string
FormatName()
{
	return GobeResources::IndexString(3, 42).String();
}


bool
SameTab(const TabStop& a, const TabStop& b)
{
	return a.position == b.position && a.type == b.type && a.fill == b.fill
		&& a.alignChar == b.alignChar && a.fillChar == b.fillChar;
}

} // namespace


TWPRuler::TWPRuler(TPageView* pages)
	:
	BView("WPRuler", B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE),
	fPages(pages)
{
	SetViewColor(B_TRANSPARENT_COLOR);
	// TWPRuler::TWPRuler: a left tab aligning on '.', filled with '-'
	fDefaultTab.type = 0;
	fDefaultTab.alignChar = '.';
	fDefaultTab.fill = 0;
	fDefaultTab.fillChar = '-';
	BFont font(be_fixed_font);
	font.SetSize(12);
	SetFont(&font);
}


// TActivePartRibbon: 0 to 25 high
BSize
TWPRuler::MinSize()
{
	return BSize(50, 25);
}


BSize
TWPRuler::MaxSize()
{
	return BSize(B_SIZE_UNLIMITED, 25);
}


BSize
TWPRuler::PreferredSize()
{
	return BSize(400, 25);
}


int
TWPRuler::Unit() const
{
	const Document& doc = fPages->Doc();
	int unit = doc.sheets.sheets.empty() ? 0 : int(doc.sheets.sheets[0].unit);
	return unit >= 0 && unit < 6 ? unit : 0;
}


int32
TWPRuler::Divisions() const
{
	const Document& doc = fPages->Doc();
	int32 divisions = doc.sheets.sheets.empty() ? 8 : int32(doc.sheets.sheets[0].divisions);
	return std::max<int32>(1, divisions);
}


void
TWPRuler::Update()
{
	if (fTracking >= 0)
		return;		// (TWPRuler +0xd8: not while dragging)
	BRect text, page;
	bool active = fPages->RulerFrame(text, page);
	float scroll = fPages->Bounds().left;
	TextStyle style;
	if (active) {
		const Document& doc = fPages->Doc();
		const WPPart* wp = doc.WordPart(fPages->fCaret.part);
		if (wp == NULL || fPages->fCaret.paragraph >= wp->paragraphs.size())
			active = false;
		else
			style = ResolveTextStyle(doc.styles, wp->paragraphs[fPages->fCaret.paragraph].parStyle);
	}
	bool sameTabs = style.tabs.size() == fStyle.tabs.size();
	for (size_t i = 0; sameTabs && i < style.tabs.size(); i++)
		sameTabs = SameTab(style.tabs[i], fStyle.tabs[i]);
	if (active == fActive && scroll == fScroll && Unit() == fUnit && Divisions() == fDivisions
		&& (!active || (text.left == fStart && text.right == fEnd && page.left == fPageLeft
			&& page.right == fPageRight && style.leftIndent == fStyle.leftIndent
			&& style.firstIndent == fStyle.firstIndent
			&& style.rightIndent == fStyle.rightIndent && sameTabs)))
		return;

	fActive = active;
	fScroll = scroll;
	fUnit = Unit();
	fDivisions = Divisions();
	fStyle = style;
	fElements.clear();
	if (active) {
		fStart = text.left;
		fEnd = text.right;
		fPageLeft = page.left;
		fPageRight = page.right;
		// TWPRuler::updatePositions: the right indent kept 18 points right
		// of the others, they 18 points left of it
		float left = fStart + style.leftIndent;
		float indent = fStart + style.firstIndent;
		float right = std::max(fEnd - style.rightIndent, std::max(left, indent) + 18);
		left = std::min(left, right - 18);
		indent = std::min(indent, right - 18);
		fElements.push_back({kLeft, left, TabStop(), false});
		fElements.push_back({kIndent, indent, TabStop(), false});
		fElements.push_back({kRight, right, TabStop(), false});
		for (const TabStop& tab : style.tabs)
			fElements.push_back({kTab, fStart + tab.position, tab, false});
	}
	Invalidate();
}


float
TWPRuler::RulerX(float position) const
{
	return fStart + position - fScroll;
}


// TWPRulerElement::FixRect: 10 wide about the position, 16 to 21 high
// (the first line indent to 24); a tab dragged off 3 lower
BRect
TWPRuler::ElementRect(const Element& e) const
{
	float x = e.x - fScroll - 5;
	BRect r(x, 16, x + 10, e.kind == kIndent ? 24 : 21);
	if (e.deleteState)
		r.OffsetBy(0, 3);
	return r;
}


// TRibbon::FindElement: the first whose rectangle has the point
int
TWPRuler::FindElement(BPoint where) const
{
	for (size_t i = 0; i < fElements.size(); i++)
		if (ElementRect(fElements[i]).Contains(where))
			return int(i);
	return -1;
}


void
TWPRuler::DrawElement(const Element& e)
{
	BRect r = ElementRect(e);
	float l = r.left, t = r.top, rt = r.right, b = r.bottom, mid = l + 5;
	BPoint triangle[3] = {BPoint(l, b), BPoint(rt, b), BPoint(mid, t)};
	switch (e.kind) {
		case kLeft:
		case kRight:
			// TLeftElement, TRightElement::DrawElement
			SetHighColor(kBlack);
			FillPolygon(triangle, 3);
			break;
		case kIndent:
			// TIndentElement::DrawElement: a bar under a stem
			SetHighColor(kBlack);
			SetPenSize(2);
			StrokeLine(BPoint(l, b), BPoint(rt, b));
			SetPenSize(1);
			StrokeLine(BPoint(mid, t), BPoint(mid, b));
			break;
		case kTab:
			// TTabElement::DrawElement: an open triangle (gray when it
			// would be deleted), the side it aligns on thicker
			SetHighColor(e.deleteState ? kGray : kBlack);
			StrokePolygon(triangle, 3, true);
			switch (e.tab.type) {
				case 0:
					StrokeLine(BPoint(mid, t + 1), BPoint(l + 1, b));
					StrokeLine(BPoint(l + 2, b), BPoint(mid, t + 2));
					break;
				case 1:
					StrokeLine(BPoint(l + 1, b - 1), BPoint(rt - 1, b - 1));
					break;
				case 2:
					StrokeLine(BPoint(rt - 1, b), BPoint(mid, t + 1));
					StrokeLine(BPoint(rt - 1, b), BPoint(mid, t + 2));
					break;
				case 3:
					StrokeLine(BPoint(mid, t + 1), BPoint(mid, t + 1));
					StrokeLine(BPoint(mid - 1, t + 2), BPoint(mid + 1, t + 2));
					break;
			}
			SetHighColor(kBlack);
			break;
	}
}


// TRuler::CalcTicks: labels `count` units apart at least 40 pixels, each
// split in `divisions`
void
TWPRuler::Ticks(float zoom, float& major, float& minor, int32& count, int32& divisions) const
{
	int32 sheetDivisions = Divisions();
	float unit = kUnitSize[Unit()] * zoom;
	count = 1;
	if (count * unit < 40) {
		do {
			if (count < 2)
				count = 2;
			else if (count < 5)
				count = 5;
			else if (count < 10)
				count = 10;
			else if (count < 20)
				count = 20;
			else if (count < 50)
				count = 50;
			else if (count < 100)
				count = 100;
			else
				count *= 2;
		} while (count * unit < 40);
	}
	major = count * unit;
	if (count == 1) {
		divisions = sheetDivisions;
		while (major < divisions * 5.0f) {
			if (divisions % 2 == 0)
				divisions /= 2;
			else if (divisions % 3 == 0)
				divisions /= 3;
			else if (divisions % 5 == 0)
				divisions /= 5;
			else if (divisions % 7 == 0)
				divisions /= 7;
			else if (divisions % 11 == 0)
				divisions /= 11;
			else if (divisions % 13 == 0)
				divisions /= 13;
			else {
				divisions = 1;
				break;
			}
		}
	} else if (count % 5 == 0)
		divisions = 5;
	else {
		divisions = major > 60 ? 8 : 4;
		if (sheetDivisions * count < divisions)
			divisions = sheetDivisions * count;
	}
	minor = major / divisions;
}


// TRuler::DrawNumbersAndHashes for a page from `start` to `end`: a gray
// line at 15, ticks hanging from it (labelled ones to the top), the
// labels right of them
void
TWPRuler::DrawTicks(float start, float end)
{
	float major, minor;
	int32 count, divisions;
	Ticks(1, major, minor, count, divisions);
	BRect bounds = Bounds();
	float limit = std::min(bounds.right + major, end);

	SetHighColor(kGray);
	StrokeLine(BPoint(std::max(start, 0.0f), 15), BPoint(limit + 1, 15));
	SetHighColor(kBlack);

	// TRuler::drawHash: from 14 up to `height` x 14
	auto hash = [&](float x, float height) {
		if (x >= 0 && x <= limit + 1)
			StrokeLine(BPoint(x, 14), BPoint(x, height * 14));
	};
	// TRuler::drawMajorLabel: "%i" at x + 3, baseline 10, if it fits
	auto label = [&](float x, int32 value) {
		if (x < 0 || x > limit)
			return;
		char text[16];
		snprintf(text, sizeof text, "%i", int(value));
		if (limit < x + StringWidth(text))
			return;
		DrawString(text, BPoint(x + 3, 10));
	};
	// TRuler::drawCurrentHashType: the ticks between labels
	auto tick = [&](float x, int32 index) {
		float height = 0.55f;
		switch (divisions) {
			case 2:
				if (index != 1)
					return;
				break;
			case 3:
			case 5:
				height = 0.75f;
				break;
			case 4:
				if (index != 2)
					height = 0.75f;
				break;
			case 8:
				if (index % 2 == 1)
					height = 0.85f;
				else if (index != 4)
					height = 0.75f;
				break;
			default:
				if (divisions % 2 != 0 || index != divisions / 2)
					height = 0.85f;
				break;
		}
		hash(x, height);
	};

	hash(start, 0);
	int32 index = 0, value = 0;
	float x = start;
	label(x, value);
	value += count;
	index = index < divisions - 1 ? index + 1 : 0;
	x += minor;
	while (x <= limit && x <= bounds.right) {
		if (index == 0) {
			hash(x, 0);
			label(x, value);
			value += count;
		} else
			tick(x, index);
		index = index < divisions - 1 ? index + 1 : 0;
		x += minor;
	}
	hash(end + 1, 0);
}


void
TWPRuler::Draw(BRect updateRect)
{
	BRect bounds = Bounds();
	SetHighColor(255, 255, 255);
	FillRect(bounds);
	if (fActive) {
		DrawTicks(fPageLeft - fScroll, fPageRight - fScroll);
		for (const Element& e : fElements)
			DrawElement(e);
	}
	SetHighColor(kGray);
	StrokeLine(BPoint(bounds.left, bounds.bottom), BPoint(bounds.right, bounds.bottom));
}


// TWPRuler::gridTo: on the minor ticks (at 100%) of the document
float
TWPRuler::GridTo(float x) const
{
	float major, minor;
	int32 count, divisions;
	Ticks(1, major, minor, count, divisions);
	float offset = fPages->fBounds.left - fPages->fMargin;
	return SnapTo(x + offset, minor) - offset;
}


// TWPRulerElement::Pin: where an element may go
void
TWPRuler::Pin(const Element& e, float& x) const
{
	float minimum = fStart, maximum = fEnd;
	float left = fElements[kLeft].x, indent = fElements[kIndent].x,
		right = fElements[kRight].x;
	switch (e.kind) {
		case kLeft:
			maximum = right - 18;
			// the first line indent moves along (not with Option)
			if ((modifiers() & B_OPTION_KEY) == 0) {
				if (indent <= left)
					minimum += left - indent;
				else
					maximum -= indent - left;
			}
			break;
		case kIndent:
			maximum = right - 18;
			break;
		case kRight:
			minimum = left + 18;
			break;
		case kTab:
			minimum = std::min(fStart, e.x);
			maximum = std::max(fEnd, e.x);
			break;
	}
	if (x < minimum)
		x = minimum;
	if (maximum < x)
		x = maximum;
}


// TWPRulerElement::SetElementPosition: the indents stay 18 apart from the
// right one (else false, or pinned there when `force` and `pin`)
bool
TWPRuler::SetElementPosition(int index, float x, bool force, bool pin)
{
	Element& e = fElements[index];
	if (e.kind == kLeft || e.kind == kIndent) {
		float limit = fElements[kRight].x - 18;
		if (limit < x) {
			if (!force)
				return false;
			if (pin)
				x = limit;
		}
	} else if (e.kind == kRight) {
		float limit = std::max(fElements[kLeft].x, fElements[kIndent].x) + 18;
		if (x < limit) {
			if (!force)
				return false;
			if (pin)
				x = limit;
		}
	}
	Invalidate(ElementRect(e).InsetByCopy(-2, -2));
	e.x = x;
	Invalidate(ElementRect(e).InsetByCopy(-2, -2));
	return true;
}


void
TWPRuler::Apply(const std::vector<float>& removeTabs, const std::vector<Property>& add,
	const std::string& label)
{
	fPages->FormatParagraphs(removeTabs, add, label, true);
}


// TWPRuler::AddTab: the default tab there
void
TWPRuler::AddTab(float x)
{
	TabStop tab = fDefaultTab;
	tab.position = x - fStart;
	Element e = {kTab, fStart, tab, false};
	fElements.push_back(e);
	SetElementPosition(int(fElements.size()) - 1, x, true, true);
	Apply({}, {TabProperty(tab)}, CommandName(true, TabName(tab)));
}


// TWPRuler::ModifyTab: the tab changed, and what clicks add from now on
void
TWPRuler::ModifyTab(int index, const TabStop& tab)
{
	if (index < 0 || index >= int(fElements.size()) || fElements[index].kind != kTab)
		return;
	TabStop old = fElements[index].tab;
	if (SameTab(old, tab))
		return;
	SetElementPosition(index, fStart + tab.position, true, true);
	fElements[index].tab = tab;
	fDefaultTab = tab;
	Apply({old.position}, {TabProperty(tab)}, FormatName());
}


// TWPRuler::RemoveTab
void
TWPRuler::RemoveTab(int index)
{
	if (index < 0 || index >= int(fElements.size()) || fElements[index].kind != kTab)
		return;
	TabStop tab = fElements[index].tab;
	fElements.erase(fElements.begin() + index);
	Invalidate();
	Apply({tab.position}, {}, CommandName(false, TabName(tab)));
}


// TWPRuler::startTabEditDialog: Edit Tab, the position from the
// document's edge
void
TWPRuler::EditTab(int index)
{
	float offset = fPages->fBounds.left - fPages->fMargin;
	(new TDialogTabEdit(Window(), this, index, fElements[index].tab, Unit(),
		fStart + offset, fEnd + offset))->RunDialog();
}


void
TWPRuler::MouseDown(BPoint where)
{
	if (!fActive || fTracking >= 0)
		return;
	int32 clicks = 1;
	uint32 buttons = 0;
	if (BMessage* message = Window()->CurrentMessage()) {
		message->FindInt32("clicks", &clicks);
		message->FindInt32("buttons", (int32*)&buttons);
	}
	if (buttons == 0)
		buttons = B_PRIMARY_MOUSE_BUTTON;
	Drag(where, where, clicks, buttons, modifiers(), true);
	if (fTracking >= 0)
		SetMouseEventMask(B_POINTER_EVENTS, B_LOCK_WINDOW_FOCUS | B_NO_POINTER_HISTORY);
}


void
TWPRuler::MouseMoved(BPoint where, uint32 transit, const BMessage* dragMessage)
{
	if (fTracking >= 0)
		Track(where, modifiers());
}


void
TWPRuler::MouseUp(BPoint where)
{
	if (fTracking >= 0) {
		Track(where, modifiers());
		EndTrack();
	}
}


// TWPRuler::MouseDown, MouseTrack: a double click on a tab edits it; with
// the primary button an element is dragged, or a click elsewhere on the
// text's width adds a tab (on the ticks, unless Command is held); the
// secondary button gives a menu. A `live` drag follows the mouse until it
// is let go, else it goes from `from` to `to` at once.
void
TWPRuler::Drag(BPoint from, BPoint to, int32 clicks, uint32 buttons, uint32 mods, bool live)
{
	if (!fActive)
		return;
	int index = FindElement(from);
	if (index >= 0 && fElements[index].kind == kTab && clicks > 1) {
		EditTab(index);
		return;
	}
	if (buttons == B_PRIMARY_MOUSE_BUTTON) {
		if (index < 0) {
			float x = from.x + fScroll;
			if ((mods & B_COMMAND_KEY) == 0)
				x = GridTo(x);
			if (fStart <= x && x <= fEnd)
				AddTab(x);
		} else {
			fTracking = index;
			fLast = fElements[index].x;
			fStepStarted = false;
			Track(from, mods);
			if (!live) {
				Track(to, mods);
				EndTrack();
			}
		}
	}
	if ((buttons & B_SECONDARY_MOUSE_BUTTON) != 0) {
		if (index >= 0 && fElements[index].kind == kTab)
			TabPopUp(index, from);
		else
			UnitPopUp(from);
	}
}


// one step of TWPRuler::MouseTrack: the element follows the mouse, and the
// paragraphs with it
void
TWPRuler::Track(BPoint where, uint32 mods)
{
	if (fTracking < 0 || fTracking >= int(fElements.size()))
		return;
	Element& e = fElements[fTracking];
	float x = where.x + fScroll;
	if ((mods & B_COMMAND_KEY) == 0)
		x = GridTo(x);
	Pin(e, x);
	bool stateChanged = false;
	if (e.kind == kTab) {
		bool off = where.y > Bounds().bottom;
		if (off != e.deleteState) {
			Invalidate(ElementRect(e).InsetByCopy(-2, -2));
			e.deleteState = off;
			stateChanged = true;
		}
	}
	if (x == fLast && !stateChanged)
		return;
	if (SetElementPosition(fTracking, x, false, true)) {
		std::vector<float> removeTabs;
		std::vector<Property> add;
		std::string label;
		switch (e.kind) {
			case kLeft:
				add.push_back(IndentProperty(Sig("PLef"), x - fStart));
				label = CommandName(true, GobeResources::IndexString(1304, 11).String());
				if ((mods & B_OPTION_KEY) == 0) {
					float indent = x - fLast + fElements[kIndent].x;
					SetElementPosition(kIndent, indent, true, true);
					add.push_back(IndentProperty(Sig("PInd"), indent - fStart));
					label = FormatName();
				}
				break;
			case kIndent:
				add.push_back(IndentProperty(Sig("PInd"), x - fStart));
				label = CommandName(true, GobeResources::IndexString(1304, 13).String());
				break;
			case kRight:
				add.push_back(IndentProperty(Sig("PRig"), fEnd - x));
				label = CommandName(true, GobeResources::IndexString(1304, 12).String());
				break;
			case kTab:
			{
				// the tab as it was off, the moved one on (unless off the
				// ruler)
				TabStop moved = e.tab;
				float before = moved.position;
				moved.position = x - fStart;
				e.tab = moved;
				removeTabs.push_back(before);
				if (!e.deleteState)
					add.push_back(TabProperty(moved));
				label = e.deleteState ? CommandName(false, TabName(moved)) : FormatName();
				break;
			}
		}
		fPages->FormatParagraphs(removeTabs, add, label, !fStepStarted);
		fStepStarted = true;
	}
	fLast = x;
}


void
TWPRuler::EndTrack()
{
	if (fTracking < 0)
		return;
	// a tab let go off the ruler is gone
	if (fTracking < int(fElements.size()) && fElements[fTracking].kind == kTab
		&& fElements[fTracking].deleteState) {
		fElements.erase(fElements.begin() + fTracking);
		Invalidate();
	}
	fTracking = -1;
	Update();
}


// the tab menu (MENU 120), the tab's type marked
void
TWPRuler::TabPopUp(int index, BPoint where)
{
	BPopUpMenu menu("tab", false, false);
	TMenu source(120, true);
	while (source.CountItems() > 0)
		menu.AddItem(source.RemoveItem((int32)0));
	if (BMenuItem* item = menu.ItemAt(fElements[index].tab.type))
		item->SetMarked(true);
	BMenuItem* chosen = menu.Go(ConvertToScreen(where), false, false, false);
	if (chosen != NULL && chosen->Message() != NULL)
		HandleTabCommand(index, chosen->Message()->what);
}


// TWPRuler::handleTabPopup
void
TWPRuler::HandleTabCommand(int index, uint32 command)
{
	if (index < 0 || index >= int(fElements.size()) || fElements[index].kind != kTab)
		return;
	if (command == kCmdDeleteTab)
		RemoveTab(index);
	else if (command == kCmdEditTab)
		EditTab(index);
	else if (command >= kCmdLeftTab && command <= kCmdAlignTab
		&& uint32(fElements[index].tab.type) + kCmdLeftTab != command) {
		TabStop tab = fElements[index].tab;
		tab.type = int(command - kCmdLeftTab);
		ModifyTab(index, tab);
	}
}


// the unit menu (MENU 241, its last item Set Ruler Divisions…), the
// sheet's unit marked: a unit goes to the window as 's00L'
void
TWPRuler::UnitPopUp(BPoint where)
{
	BPopUpMenu menu("unit", false, false);
	TMenu source(241, true);
	int32 count = source.CountItems();
	if (count > 0)
		source.ItemAt(count - 1)->SetLabel(GobeResources::IndexString(2, 25).String());
	if (BMenuItem* item = source.FindValue(Unit()))
		item->SetMarked(true);
	while (source.CountItems() > 0)
		menu.AddItem(source.RemoveItem((int32)0));
	BMenuItem* chosen = menu.Go(ConvertToScreen(where), false, false, false);
	if (chosen == NULL || chosen->Message() == NULL)
		return;
	if (menu.IndexOf(chosen) < 5) {
		BMessage unit(*chosen->Message());
		unit.what = 's00L';
		Window()->PostMessage(&unit);
	} else {
		BString divisions;
		divisions << Divisions();
		(new TDialogSetRulerDivisions(Window(), GobeResources::IndexString(2, 23).String(),
			GobeResources::IndexString(2, 24).String(), divisions.String()))->RunDialog();
	}
}


void
TWPRuler::MessageReceived(BMessage* message)
{
	if (message->what == kMsgModifyTab) {
		TabStop tab;
		tab.position = message->GetFloat("position", 0);
		tab.type = message->GetInt32("type", 0);
		tab.alignChar = uint32(message->GetInt32("alignChar", '.'));
		tab.fill = message->GetInt32("fill", 0);
		tab.fillChar = uint32(message->GetInt32("fillChar", '-'));
		ModifyTab(message->GetInt32("tab", -1), tab);
		return;
	}
	BView::MessageReceived(message);
}


void
TWPRuler::RunScript(const std::string& command)
{
	Update();
	char name[32] = "";
	float a = 0, b = 0;
	unsigned mods = 0;
	int index = -1, value = 0;
	if (command == "markers") {
		if (!fActive) {
			printf("ruler inactive\n");
			fflush(stdout);
			return;
		}
		printf("ruler left %g indent %g right %g", fElements[kLeft].x - fStart,
			fElements[kIndent].x - fStart, fElements[kRight].x - fStart);
		for (size_t i = kTab; i < fElements.size(); i++)
			printf(" tab %g %d", fElements[i].tab.position, fElements[i].tab.type);
		printf("\n");
		fflush(stdout);
	} else if (sscanf(command.c_str(), "drag %31s %f %f %u", name, &a, &b, &mods) >= 2) {
		std::string which = name;
		if (which == "left")
			index = kLeft;
		else if (which == "indent")
			index = kIndent;
		else if (which == "right")
			index = kRight;
		else if (which.compare(0, 3, "tab") == 0)
			index = kTab + atoi(which.c_str() + 3);
		if (!fActive || index < 0 || index >= int(fElements.size()))
			return;
		// from the middle of the marker (the first line indent's bar)
		BPoint from(fElements[index].x - fScroll, index == kIndent ? 23 : 18);
		Drag(from, from + BPoint(a, b), 1, B_PRIMARY_MOUSE_BUTTON, mods, false);
	} else if (sscanf(command.c_str(), "click %f %u", &a, &mods) >= 1) {
		BPoint where(RulerX(a), 10);
		Drag(where, where, 1, B_PRIMARY_MOUSE_BUTTON, mods, false);
	} else if (sscanf(command.c_str(), "edit %d", &index) == 1) {
		if (fActive && kTab + index < int(fElements.size())) {
			BPoint where(fElements[kTab + index].x - fScroll, 18);
			Drag(where, where, 2, B_PRIMARY_MOUSE_BUTTON, 0, false);
		}
	} else if (sscanf(command.c_str(), "tabmenu %d %d", &index, &value) == 2)
		HandleTabCommand(kTab + index, uint32(value));
	else if (sscanf(command.c_str(), "unit %d", &value) == 1) {
		BMessage unit('s00L');
		unit.AddInt32(kValueString, value);
		Window()->PostMessage(&unit);
	} else if (sscanf(command.c_str(), "divisions %d", &value) == 1) {
		BMessage divisions('s00L');
		divisions.AddInt32("rulerDivisions", value);
		Window()->PostMessage(&divisions);
	}
}

} // namespace gobe
