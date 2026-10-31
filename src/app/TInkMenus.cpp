#include "TInkMenus.h"

#include <Gradient.h>
#include <GradientLinear.h>
#include <GradientRadial.h>
#include <FindDirectory.h>
#include <InterfaceDefs.h>
#include <Path.h>

#include <algorithm>
#include <cmath>

#include "DocArchive.h"
#include "DocWriter.h"
#include "GobeResources.h"
#include "InkSets.h"
#include "TRibbon.h"

namespace gobe {

ColorSet
LoadColorSet(int32 id)
{
	ColorSet set;
	const ResourceFile::Resource* r = GobeResources::File().Find('COLR', id);
	if (r == NULL)
		return set;
	const std::vector<uint8_t>& d = r->data;
	auto u32 = [&](size_t at) {
		return uint32(d[at + 3]) << 24 | uint32(d[at + 2]) << 16 | uint32(d[at + 1]) << 8 | d[at];
	};
	if (d.size() < 12)
		return set;
	set.columns = std::max<int32>(1, int32(u32(0)));
	set.cell = int32(u32(4));
	set.gap = int32(u32(8));
	for (size_t at = 12; at + 12 <= d.size(); at += 12)
		set.colors.push_back({uint8(u32(at)), uint8(u32(at + 4)), uint8(u32(at + 8)), 255});
	return set;
}


namespace {

std::vector<InkSetEntry> sSets;
bool sSetsLoaded = false;
std::string sActiveSet[3];

std::vector<InkSetEntry>&
AppInkSets()
{
	if (!sSetsLoaded) {
		sSetsLoaded = true;
		sSets = AvailableInkSets(&GobeResources::File(), InkSetFolders());
	}
	return sSets;
}

} // namespace


std::vector<std::string>
InkSetFolders()
{
	std::vector<std::string> folders;
	BPath original(GobeResources::Path().String());
	if (original.InitCheck() == B_OK && original.GetParent(&original) == B_OK
			&& original.Append("Ink Sets") == B_OK)
		folders.push_back(original.Path());
	BPath settings;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &settings) == B_OK
			&& settings.Append("Gobe Productive port/Ink Sets") == B_OK)
		folders.push_back(settings.Path());
	return folders;
}


std::vector<InkSetRef>
InkSetsOfKind(int kind, const SetMgr* doc)
{
	std::vector<InkSetRef> sets;
	for (const InkSetEntry& entry : AppInkSets())
		if (entry.set.kind == kind)
			sets.push_back({entry.set, entry.path, false});
	if (doc != NULL && kind >= 0 && kind <= 2)
		for (const InkSet& set : doc->sets[kind])
			sets.push_back({set, "", true});
	return sets;
}


bool
FindInkSet(int kind, const std::string& name, const SetMgr* doc, InkSetRef& ref)
{
	for (const InkSetRef& set : InkSetsOfKind(kind, doc))
		if (set.set.name == name) {
			ref = set;
			return true;
		}
	return false;
}


std::string
ActiveInkSetName(int kind, const SetMgr* doc)
{
	if (kind < 0 || kind > 2)
		return "";
	InkSetRef ref;
	if (!sActiveSet[kind].empty() && FindInkSet(kind, sActiveSet[kind], doc, ref))
		return sActiveSet[kind];
	std::vector<InkSetRef> sets = InkSetsOfKind(kind, doc);
	return sets.empty() ? "" : sets.front().set.name;
}


void
SetActiveInkSet(int kind, const std::string& name)
{
	if (kind >= 0 && kind <= 2)
		sActiveSet[kind] = name;
}


bool
ActiveInkSet(int kind, const SetMgr* doc, InkSetRef& ref)
{
	return FindInkSet(kind, ActiveInkSetName(kind, doc), doc, ref);
}


bool
ChangeInkSet(const InkSetRef& ref, std::string& error)
{
	for (InkSetEntry& entry : AppInkSets())
		if (entry.set.kind == ref.set.kind && entry.path == ref.path
				&& (entry.set.name == ref.set.name || !ref.path.empty())) {
			entry.set = ref.set;
			return ref.path.empty() || SaveInkSetFile(ref.path, ref.set, error);
		}
	error = "no such set";
	return false;
}


void
ReloadInkSets()
{
	sSetsLoaded = false;
}


BMessage*
InkSetMessage(const BMessage& model, const InkSet& set, size_t index)
{
	BMessage* message = new BMessage(model);
	message->AddString("ink name", set.name.c_str());
	if (index < set.inks.size()) {
		const Ink& ink = set.inks[index];
		if (set.kind == kColorSet) {
			rgb_color c = {ink.color.r, ink.color.g, ink.color.b, 255};
			message->AddData("color", B_RGB_COLOR_TYPE, &c, 4);
		} else {
			std::vector<uint8_t> bytes = InkBytes(ink);
			message->AddData("ink", B_RAW_TYPE, bytes.data(), ssize_t(bytes.size()));
		}
	}
	message->AddInt32("ink index", int32(index));
	return message;
}


namespace {

BMenu*
InkSetMatrix(int kind, const BMessage& model, const SetMgr* doc)
{
	InkSetRef ref;
	if (!ActiveInkSet(kind, doc, ref))
		return NULL;
	const InkSet& set = ref.set;
	BMenu* palette = new BMenu("", B_ITEMS_IN_MATRIX);
	int32 step = set.cell + set.gap;
	int32 columns = std::max(1, set.columns);
	for (size_t k = 0; k < set.inks.size(); k++) {
		float x = set.gap + (k % columns) * step;
		float y = set.gap + (k / columns) * step;
		palette->AddItem(new TInkMenuItem(set.inks[k], set.cell, InkSetMessage(model, set, k)),
			BRect(x, y, x + set.cell - 1, y + set.cell - 1));
	}
	return palette;
}

} // namespace


BMenu*
ColorSetMenu(const BMessage& model, const SetMgr* doc)
{
	BMenu* menu = InkSetMatrix(kColorSet, model, doc);
	return menu != NULL ? menu : new BMenu("");
}


BMenu*
InkSetMenu(uint32 type, const BMessage& model, const SetMgr* doc)
{
	return InkSetMatrix(type == 'BPAT' ? kPatternSet : kGradientSet, model, doc);
}


TInkMenuItem::TInkMenuItem(rgb_color color, int32 cell, BMessage* message)
	:
	BMenuItem("", message),
	fCell(cell)
{
	fInk.set = true;
	fInk.color = {color.red, color.green, color.blue, 255};
}


TInkMenuItem::TInkMenuItem(const Ink& ink, int32 cell, BMessage* message)
	:
	BMenuItem("", message),
	fInk(ink),
	fCell(cell)
{
}


void
DrawInkCell(BView* view, const Ink& ink, BRect r, bool selected)
{
	auto color = [](const Color& c) { return make_color(c.r, c.g, c.b, 255); };
	if (ink.gradient) {
		// as TPageView::SetInk draws them
		BGradient* g;
		if (ink.gradientType == 'GDrl') {
			BPoint center(r.left + r.Width() * ink.center[0], r.top + r.Height() * ink.center[1]);
			g = new BGradientRadial(center, r.Width() / 2);
		} else {
			float a = ink.angle * M_PI / 180;
			BPoint c(r.left + r.Width() / 2, r.top + r.Height() / 2);
			BPoint d(cosf(a) * r.Width() / 2, -sinf(a) * r.Height() / 2);
			g = new BGradientLinear(c - d, c + d);
		}
		int count = std::max(2, std::min(4, ink.colorCount));
		for (int i = 0; i < count; i++)
			g->AddColor(color(ink.colors[i]), 255 * i / (count - 1));
		view->FillRect(r, *g);
		delete g;
	} else {
		pattern p;
		for (int i = 0; i < 8; i++)
			p.data[i] = ink.pattern[i];
		rgb_color low = view->LowColor();
		view->SetHighColor(color(ink.color));
		view->SetLowColor(color(ink.low));
		view->FillRect(r, p);
		view->SetLowColor(low);
	}
	if (selected)
		view->SetHighColor(0, 0, 0);
	else
		view->SetHighColor(0xa0, 0xa0, 0xa0);
	view->StrokeRect(r);
	view->SetHighColor(0, 0, 0);
}


void
TInkMenuItem::DrawContent()
{
	BRect r = Frame();
	r.right = r.left + fCell - 1;
	r.bottom = r.top + fCell - 1;
	DrawInkCell(Menu(), fInk, r, IsSelected());
}


void
TInkMenuItem::GetContentSize(float* width, float* height)
{
	*width = fCell;
	*height = fCell;
}


TIconMenuItem::TIconMenuItem(int32 micn, BMenu* menu, const ResourceFile* resources)
	:
	BMenuItem(menu),
	fIcon(TMICNElement::LoadIcon(micn, resources))
{
}


void
TIconMenuItem::DrawContent()
{
	if (fIcon == NULL)
		return;
	BMenu* menu = Menu();
	if (menu->Window() != NULL)
		fScreenFrame = menu->ConvertToScreen(Frame());
	menu->SetDrawingMode(B_OP_OVER);
	menu->DrawBitmap(fIcon.get(), ContentLocation());
	menu->SetDrawingMode(B_OP_COPY);
}


void
TIconMenuItem::GetContentSize(float* width, float* height)
{
	*width = 16;
	*height = 16;
}


bool
TIconMenuItem::ReleasedOver() const
{
	BPoint where;
	uint32 buttons = 0;
	return fScreenFrame.IsValid() && get_mouse(&where, &buttons) == B_OK && buttons == 0
		&& fScreenFrame.Contains(where);
}



void
StrokePenType(BView* view, const Outline& outline, const PenType& pen, float size, BPoint offset)
{
	auto at = [&](const DrawPoint& p) { return BPoint(p[0], p[1]) + offset; };
	float was = view->PenSize();
	if (pen.kind == 0 || pen.kind == 3) {
		// (the hash pen's line at a third of its size)
		view->SetPenSize(pen.kind == 3 ? PenTypeSize(pen, size) / 3 : size);
		for (size_t i = 1; i < outline.points.size(); i++)
			view->StrokeLine(at(outline.points[i - 1]), at(outline.points[i]));
		if (outline.closed && outline.points.size() > 2)
			view->StrokeLine(at(outline.points.back()), at(outline.points.front()));
		if (pen.kind == 0) {
			view->SetPenSize(was);
			return;
		}
	}
	PenMarks marks = MarksAlong(outline, pen, size);
	view->SetPenSize(marks.width);
	cap_mode cap = view->LineCapMode();
	join_mode join = view->LineJoinMode();
	view->SetLineMode(B_BUTT_CAP, B_MITER_JOIN);
	for (const auto& segment : marks.segments)
		view->StrokeLine(at(segment.first), at(segment.second));
	// dots: filled ones filled, all outlined with a hairline
	view->SetPenSize(1);
	float r = marks.dotSize / 2;
	for (const DrawPoint& dot : marks.dots) {
		if (pen.filled)
			view->FillEllipse(at(dot), r, r);
		view->StrokeEllipse(at(dot), r, r);
	}
	view->SetLineMode(cap, join);
	view->SetPenSize(was);
}


void
FillArrowHeads(BView* view, const Outline& outline, int arrows, float size, BPoint offset)
{
	const std::vector<DrawPoint>& p = outline.points;
	if (arrows <= 0 || outline.closed || p.size() < 2)
		return;
	auto fill = [&](const DrawPoint& from, const DrawPoint& end) {
		DrawPoint head[3];
		ArrowHead(from, end, size, head);
		view->FillTriangle(BPoint(head[0][0], head[0][1]) + offset,
			BPoint(head[1][0], head[1][1]) + offset, BPoint(head[2][0], head[2][1]) + offset);
	};
	if (arrows & 1)
		fill(p[1], p[0]);
	if (arrows & 2)
		fill(p[p.size() - 2], p.back());
}


TLineSampleItem::TLineSampleItem(const PenType& pen, int arrows, BMessage* message)
	:
	BMenuItem("", message),
	fPen(pen),
	fArrows(arrows)
{
}


void
TLineSampleItem::DrawContent()
{
	BMenu* menu = Menu();
	BPoint where = ContentLocation();
	float y = std::floor(where.y + 8);
	Outline line{{{where.x + 6, y}, {where.x + 58, y}}, false};
	menu->SetHighColor(IsEnabled() ? rgb_color{0, 0, 0, 255} : rgb_color{150, 150, 150, 255});
	StrokePenType(menu, line, fPen, fArrows > 0 ? 1 : 2);
	FillArrowHeads(menu, line, fArrows, 1);
}


void
TLineSampleItem::GetContentSize(float* width, float* height)
{
	*width = 64;
	*height = 16;
}


const std::vector<PenType>&
AppPens()
{
	static std::vector<PenType> pens;
	static bool loaded = false;
	if (!loaded) {
		loaded = true;
		for (const ResourceFile::Resource* r : GobeResources::File().All('PENS')) {
			PenType pen;
			if (ParsePenResource(r->data, pen))
				pens.push_back(pen);
		}
	}
	return pens;
}


void
AddPenToMessage(BMessage* message, const PenType& pen)
{
	DocWriter w;
	WritePen(w, pen);
	message->AddData("pen", B_RAW_TYPE, w.Data().data(), ssize_t(w.Data().size()));
}


bool
PenFromMessage(const BMessage* message, PenType& pen)
{
	const void* data = NULL;
	ssize_t size = 0;
	if (message->FindData("pen", B_RAW_TYPE, &data, &size) != B_OK)
		return false;
	DocArchive a(std::vector<uint8_t>((const uint8_t*)data, (const uint8_t*)data + size));
	try {
		ReadPen(a, pen);
	} catch (const ArchiveError&) {
		return false;
	}
	return true;
}


void
AddPenTypeItems(BMenu* menu, const std::vector<PenType>& pens, const PenType* marked)
{
	for (const PenType& pen : pens) {
		BMessage* message = new BMessage('sf0A');
		AddPenToMessage(message, pen);
		TLineSampleItem* item = new TLineSampleItem(pen, 0, message);
		item->SetMarked(marked != NULL && SamePen(*marked, pen));
		menu->AddItem(item);
	}
}

} // namespace gobe
