#include "TPageView.h"
#include "TPreferences.h"
#include "TPageBar.h"
#include "TRibbon.h"
#include "TWPRuler.h"

#include <math.h>
#include <stdio.h>
#include <time.h>

#include <algorithm>
#include <map>
#include <cstdint>

#include <AffineTransform.h>
#include <GradientLinear.h>
#include <GradientRadial.h>
#include <Region.h>
#include <Window.h>
#include <Shape.h>

#include "Fields.h"
#include "Notes.h"
#include "TextImport.h"
#include "ResourceFile.h"
#include "TDocView.h"

namespace gobe {

static const float kDesk = 24;


bool
TPageView::CanShow(const Document& document)
{
	return document.RootPart() >= 0 && !document.PlacedFrames().empty();
}


TPageView::TPageView(Document& document)
	:
	BView("TPageView", B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE | B_NAVIGABLE),
	fMargin(kDesk),
	fBook(new SSBook(document)),
	fDocument(document)
{
	SetViewColor(B_TRANSPARENT_COLOR);
	// TDoc: the history keeps the Undo Levels preference's steps
	fUndo.SetLimit(AppPreferences().unlimitedUndo ? 0
		: size_t(std::max<int32>(1, AppPreferences().undoCount)));
	// TLeafSheet +0x2d: the sheet shown in page view (a paged document
	// with it off opens in browse view)
	fBrowse = fDocument.PagePart() >= 0 && fDocument.WordPart(size_t(std::max(0,
		fDocument.MainPart()))) != NULL && !fDocument.sheets.sheets.empty()
		&& !fDocument.sheets.sheets[0].pageView;
	Rebuild();
	// a word processing document opens with the insertion point in its
	// main text, where its selection was (at the start of a new one)
	int main = fDocument.MainPart();
	if (const WPPart* wp = main >= 0 ? fDocument.WordPart(size_t(main)) : NULL) {
		TextPosition at{0, 0};
		const Selection* selection = fDocument.selections.Find(wp->header.selection);
		if (selection != NULL && !selection->textRanges.empty())
			at = PositionAt(*wp, selection->textRanges[0].first);
		fCaret = {true, size_t(main), at.paragraph, at.offset};
		fAnchor = fCaret;
	}
}


// the visible frames, the bounds and the text layout (again after a slide
// change)
void
TPageView::Rebuild()
{
	RebuildFrames();
	LayoutText();
}


void
TPageView::RebuildFrames()
{
	fFrames.clear();
	fLines.clear();
	fRules.clear();
	// notes are laid out with their references' text
	fNoteParts.clear();
	for (const auto& [part, wp] : fDocument.wordParts)
		for (const WPParagraph& p : wp.paragraphs)
			for (const WPMiscInfo& m : p.misc) {
				int note = NotePart(fDocument, m);
				if (note >= 0)
					fNoteParts.insert(size_t(note));
			}
	for (const auto& f : fDocument.PlacedFrames())
		if (!fDocument.FrameHidden(f.part, f.index) && !fNoteParts.count(f.part))
			fFrames.push_back(f);
	// a presentation's frame shows the whole slide (the saved window may
	// have shown part of it, scrolled)
	if (fDocument.presentationPart >= 0) {
		const GRPart* gr = fDocument.GraphicsPart(fDocument.presentationPart);
		for (auto& f : fFrames) {
			if (gr == NULL || (int(f.part) != fDocument.presentationPart
					&& fDocument.parts.parts[f.part].kind != Sig("BROT")))
				continue;
			f.width = std::max(f.width, gr->size[0]);
			f.height = std::max(f.height, gr->size[1]);
		}
	}
	fMargin = kDesk;
	if (fBrowse) {
		// TBrowseRootFrame, TWPPortal: the main text in one frame growing
		// with its text, as wide as the sheet's printable area (468 points
		// without one), 10 points in from the view's edges (TWPFrame::
		// LogicalDataSize: 20 more)
		float data = 468;
		if (!fDocument.sheets.sheets.empty()) {
			const float* printable = fDocument.sheets.sheets[0].printable;
			data = printable[2] - printable[0];
		}
		float width = data + 20;
		fBrowseFrame = gobe::Frame();
		fBrowseFrame.values[3] = 8;
		fFrames.clear();
		Document::PlacedFrame f{};
		f.part = size_t(fDocument.MainPart());
		f.frame = &fBrowseFrame;
		f.x = 10 - kTextPad;
		f.y = 10;
		f.width = data + 2 * kTextPad;
		f.height = 20;
		f.depth = 1;
		f.index = 0;
		f.page = -1;
		fFrames.push_back(f);
		fMargin = 0;
		fBounds.Set(0, 0, width, 40);
		return;
	}
	fBounds = BRect();
	bool first = true;
	for (const auto& f : fFrames) {
		if (fDocument.parts.parts[f.part].kind == Sig("PROT")
			|| fDocument.parts.parts[f.part].kind == Sig("BROT"))
			continue;	// the root frame is the window, not content
		BRect r = RotatedBounds(BRect(f.x, f.y, f.x + f.width, f.y + f.height),
			f.frame->angle) & ContainerClip(f);
		if (!r.IsValid())
			continue;
		fBounds = first ? r : (fBounds | r);
		first = false;
	}
	if (first)
		fBounds.Set(0, 0, 612, 792);
}


void
TPageView::AttachedToWindow()
{
	BView::AttachedToWindow();
	MakeFocus(true);
}


void
TPageView::ShowSlide(int index)
{
	if (fDocument.slides.empty())
		return;
	index = std::max(0, std::min(int(fDocument.slides.size()) - 1, index));
	if (index == fDocument.currentSlide && !fFrames.empty())
		return;
	fDocument.ShowSlide(index);
	Rebuild();
	Invalidate();
	if (Window() != NULL) {
		BMessage message(kMsgSlideChanged);
		message.AddInt32("slide", index);
		message.AddString("name", fDocument.slides[index].name.c_str());
		Window()->PostMessage(&message);
	}
}


BRect
TPageView::ContainerClip(const Document::PlacedFrame& f) const
{
	BRect clip(-1e6, -1e6, 1e6, 1e6);
	uint32_t container = f.frame->container;
	for (int depth = 0; depth < 16 && container < fDocument.parts.parts.size(); depth++) {
		uint32_t kind = fDocument.parts.parts[container].kind;
		if (kind == Sig("PROT") || kind == Sig("BROT"))
			break;
		BRect all;
		bool any = false;
		uint32_t next = kNoPart;
		for (const auto& c : fFrames) {
			if (c.part != container)
				continue;
			BRect r = RotatedBounds(BRect(c.x, c.y, c.x + c.width, c.y + c.height),
				c.frame->angle);
			all = any ? (all | r) : r;
			any = true;
			next = c.frame->container;
		}
		if (!any)
			break;
		clip = clip & all;
		container = next;
	}
	return clip;
}


// Gobe angles are degrees counterclockwise; Haiku's y axis points down, so
// that is a negative rotation here.
static BAffineTransform
FrameRotation(BPoint center, float angle)
{
	BAffineTransform t;
	t.RotateBy(center, -angle * M_PI / 180);
	return t;
}


BRect
TPageView::RotatedBounds(BRect r, float angle)
{
	if (angle == 0)
		return r;
	BAffineTransform t = FrameRotation(
		r.LeftTop() + BPoint(r.Width() / 2, r.Height() / 2), angle);
	BPoint corners[4] = {r.LeftTop(), r.RightTop(), r.LeftBottom(), r.RightBottom()};
	t.Apply(corners, 4);
	BRect b(corners[0], corners[0]);
	for (const BPoint& p : corners) {
		b.left = std::min(b.left, p.x);
		b.top = std::min(b.top, p.y);
		b.right = std::max(b.right, p.x);
		b.bottom = std::max(b.bottom, p.y);
	}
	return b;
}


BSize
TPageView::MinSize()
{
	return BSize(200, 200);
}


BSize
TPageView::PreferredSize()
{
	return BSize((fBounds.Width() + 2 * fMargin) * fZoom, (fBounds.Height() + 2 * fMargin) * fZoom);
}


void
TPageView::VisiblePages(int32& first, int32& last) const
{
	first = last = -1;
	if (fBrowse)
		return;
	int page = fDocument.PagePart();
	BRect bounds = Bounds();
	BRect visible(bounds.left / fZoom, bounds.top / fZoom, bounds.right / fZoom,
		bounds.bottom / fZoom);
	for (const auto& f : fFrames) {
		if (int(f.part) != page)
			continue;
		BRect r(f.x - fBounds.left + fMargin, f.y - fBounds.top + fMargin, 0, 0);
		r.right = r.left + f.width;
		r.bottom = r.top + f.height;
		if (!r.Intersects(visible))
			continue;
		if (first < 0 || f.index + 1 < first)
			first = f.index + 1;
		last = std::max(last, int32(f.index + 1));
	}
}


// TWPPart::FitToWindow, TDocWindow::CalcScaleFromSize: the paper and 20
// points (one page across), as large as fits the view
float
TPageView::FitZoom() const
{
	BRect bounds = Bounds();
	float width = 612, height = 792;
	if (!fDocument.sheets.sheets.empty()) {
		const float* paper = fDocument.sheets.sheets[0].paper;
		width = paper[2] - paper[0];
		height = paper[3] - paper[1];
	}
	width += 20;
	height += 20;
	if (bounds.Width() / bounds.Height() < width / height)
		return bounds.Width() / width;
	return bounds.Height() / height;
}


// TDocWindow::SetPageView: the pages, or the main text as wide as the
// window (TBrowseRootFrame)
void
TPageView::SetPageView(bool on)
{
	if (on == !fBrowse || fDocument.PagePart() < 0)
		return;
	fBrowse = !on;
	Rebuild();
	InvalidateLayout();
	if (Parent() != NULL)
		Parent()->Layout(false);
	ScrollTo(0, 0);
	Invalidate();
}


// TDocWindow::ScaleTo: the view's middle stays on the same point of the
// document
void
TPageView::SetZoom(float zoom)
{
	zoom = std::max(0.015625f, std::min(64.0f, zoom));
	if (zoom == fZoom)
		return;
	BRect bounds = Bounds();
	BPoint middle((bounds.left + bounds.Width() / 2) / fZoom,
		(bounds.top + bounds.Height() / 2) / fZoom);
	fZoom = zoom;
	InvalidateLayout();
	if (Parent() != NULL)
		Parent()->Layout(false);
	ScrollTo(std::max(0.0f, middle.x * fZoom - bounds.Width() / 2),
		std::max(0.0f, middle.y * fZoom - bounds.Height() / 2));
	Invalidate();
}


void
TPageView::GetPreferredSize(float* width, float* height)
{
	BSize s = PreferredSize();
	*width = s.width;
	*height = s.height;
}


void
TPageView::LayoutText()
{
	bool paginated = false;
	LayoutResult main;
	// the styles may have changed since the last layout
	fStyleCache.clear();
	for (int pass = 0;; pass++) {
		fLines.clear();
		fRules.clear();
		// group each word processing part's frames, in flow order
		std::vector<std::pair<size_t, std::vector<FrameGeometry>>> flows;
		// a frame on several pages (the header, the footer) is laid out once
		// and shown on each: its other places, as offsets
		struct Repeat {
			size_t part;
			float dx, dy;
			int page;
		};
		std::vector<Repeat> repeats;
		for (const auto& f : fFrames) {
			if (fDocument.parts.parts[f.part].kind != Sig("WORD"))
				continue;
			BRect r(f.x - fBounds.left + fMargin, f.y - fBounds.top + fMargin, 0, 0);
			r.right = r.left + f.width;
			// TWPPart::MaxChildFrameRect: flag 8, the frame grows with its text
			r.bottom = r.top + ((f.frame->values[3] & 8) != 0 ? 1e6f : f.height);
			auto it = std::find_if(flows.begin(), flows.end(),
				[&](const auto& fl) { return fl.first == f.part; });
			FrameGeometry g{r, f.frame->angle, ContainerClip(f), f.index, f.page};
			if (it == flows.end())
				flows.push_back({f.part, {g}});
			else {
				auto same = std::find_if(it->second.begin(), it->second.end(),
					[&](const FrameGeometry& o) { return o.index == f.index; });
				if (same != it->second.end())
					repeats.push_back({f.part, r.left - same->rect.left, r.top - same->rect.top,
						f.page});
				else
					it->second.push_back(g);
			}
		}
		main = LayoutResult();
		std::map<size_t, std::pair<size_t, size_t>> laidOut;	// part: its lines
		for (const auto& [part, frames] : flows) {
			size_t first = fLines.size();
			LayoutResult result = LayoutPart(part, frames);
			if (int(part) == fDocument.MainPart())
				main = result;
			size_t last = fLines.size();
			laidOut[part] = {first, last};
			for (const Repeat& repeat : repeats) {
				if (repeat.part != part)
					continue;
				for (size_t k = first; k < last; k++) {
					Line line = fLines[k];
					line.frame.OffsetBy(repeat.dx, repeat.dy);
					line.baseline += repeat.dy;
					line.center += BPoint(repeat.dx, repeat.dy);
					line.clip.OffsetBy(repeat.dx, repeat.dy);
					line.textFrame.OffsetBy(repeat.dx, repeat.dy);
					// a page number shows this page's
					for (Line::Piece& piece : line.pieces)
						if (piece.field == Sig("PAGE"))
							piece.text = repeat.page < 0 ? "#" : std::to_string(repeat.page + 1);
					fLines.push_back(line);
				}
			}
		}
		// the header and footer, once edited, are as high as their text
		bool resized = false;
		for (int which = 0; which < 2; which++) {
			int part = fDocument.HeaderPart(which);
			if (part < 0 || !fDocument.dirtyParts.count(size_t(part)))
				continue;
			auto flow = std::find_if(flows.begin(), flows.end(),
				[&](const auto& fl) { return fl.first == size_t(part); });
			if (flow == flows.end() || flow->second.empty())
				continue;
			float top = flow->second[0].rect.top, bottom = top;
			auto [first, last] = laidOut[size_t(part)];
			for (size_t k = first; k < last; k++)
				bottom = std::max(bottom, fLines[k].frame.bottom);
			resized |= fDocument.SetHeaderHeight(which, bottom - top);
		}
		if (resized && pass < 2000) {
			RebuildFrames();
			continue;
		}
		// a runaway document stops at 2000 pages
		if (pass >= 2000 || !Paginate(main))
			break;
		paginated = true;
		RebuildFrames();
	}
	if (fBrowse) {
		// the view as high as the text
		float bottom = 40;
		for (const Line& line : fLines)
			bottom = std::max(bottom, line.frame.bottom + 10);
		if (bottom != fBounds.bottom) {
			fBounds.bottom = bottom;
			InvalidateLayout();
		}
		return;
	}
	// once pages came or went or the frames were made anew (Columns), the
	// main text's frames show what the layout put in them
	int mainIndex = fDocument.MainPart();
	if (!paginated && (mainIndex < 0 || !fDocument.dirtyTails.count(size_t(mainIndex))))
		return;
	int mainPart = fDocument.MainPart();
	uint32_t total = 0;
	if (const WPPart* wp = fDocument.WordPart(size_t(mainPart)))
		for (const WPParagraph& p : wp->paragraphs)
			total += uint32_t(p.text.size());
	for (size_t k = 0; k < main.starts.size(); k++) {
		uint32_t end = total;
		for (size_t n = k + 1; n < main.starts.size(); n++)
			if (main.starts[n] != UINT32_MAX) {
				end = main.starts[n];
				break;
			}
		uint32_t start = main.starts[k] == UINT32_MAX ? end : main.starts[k];
		fDocument.SetFrameText(size_t(mainPart), k, start, end > start ? end - start : 0);
	}
	InvalidateLayout();
	if (Window() != NULL)
		Window()->PostMessage(kMsgDocumentChanged);
}


// Pages follow the main text once it has been edited (TWPPart: a page more
// when the text runs past the last page, the last page goes when the text
// ends before it); an unedited document keeps the pages it was saved with.
// True if a page was added or removed.
bool
TPageView::Paginate(const LayoutResult& main)
{
	if (fBrowse)
		return false;
	int mainPart = fDocument.MainPart();
	int pagePart = fDocument.PagePart();
	if (mainPart < 0 || pagePart < 0 || !fDocument.dirtyParts.count(size_t(mainPart)))
		return false;
	if (main.overflow)
		return fDocument.AddPage();
	if (fDocument.PageCount() < 2)
		return false;
	// the main text's frames on the last page
	const gobe::Frame& last = fDocument.tails[pagePart].portals[0].frames.back();
	int first = INT32_MAX;
	for (const gobe::Frame::Pointer& c : last.children)
		if (int(c.part) == mainPart)
			first = std::min(first, int(c.frame));
	if (first != INT32_MAX && main.lastFrame < first)
		return fDocument.RemoveLastPage();
	return false;
}


// a style resolved and its font, kept for one layout
const std::pair<TextStyle, BFont>&
TPageView::CachedStyle(uint32_t style)
{
	auto known = fStyleCache.find(style);
	if (known != fStyleCache.end())
		return known->second;
	TextStyle resolved = ResolveTextStyle(fDocument.styles, style);
	BFont font = TDocView::MapFont(resolved);
	return fStyleCache.emplace(style, std::make_pair(resolved, font)).first->second;
}


// Break characters (TWPPart::IsFrameBreak etc.): 0x02 and 0x05 end the
// frame (column break), 0x0c ends the page, '\n' ends a paragraph, 0x01 is a
// line break inside a paragraph.
static bool
IsFrameBreak(char c)
{
	return c == 0x02 || c == 0x05 || c == 0x0c;
}


// Greedy line layout: runs split into words, lines filled to the frame
// width, paragraphs aligned and indented by their paragraph style; when a
// frame is full, or a paragraph ends with a frame/page break, the text
// continues in the next frame. Tabs go to the next half inch for now (the
// paragraph's PTab stops aren't decoded yet).
TPageView::LayoutResult
TPageView::LayoutPart(size_t part, const std::vector<FrameGeometry>& geometry)
{
	LayoutResult result;
	const WPPart* wp = fDocument.WordPart(part);
	if (wp == NULL || geometry.empty())
		return result;
	std::vector<BRect> frames;
	int maxIndex = 0;
	for (const FrameGeometry& g : geometry) {
		frames.push_back(g.rect);
		maxIndex = std::max(maxIndex, g.index);
	}
	result.starts.assign(size_t(maxIndex + 1), UINT32_MAX);
	uint32_t paragraphStart = 0;

	size_t frameIndex = 0;
	float y = frames[0].top;
	const float pad = kTextPad;
	const float tabStop = 36;

	struct Token {
		enum Kind { kWord, kTab, kLineBreak, kRule } kind;
		std::string text;
		BFont font;
		rgb_color color;
		bool space;			// a space follows
		size_t start, end;	// bytes of the paragraph's text it shows
		uint32_t field = 0;	// a field: its kind
		const WPMiscInfo* misc = NULL;	// a field's or note's paragraph info
		float size = 0;		// the size its line is measured with
		float dy = 0;		// superscript, subscript: off the baseline
	};

	// footnotes (TWPFrame::CalcFootnoteHeight, PlaceFootnotes): the notes of
	// the lines in a frame go under them, 14 points down, each as high as
	// its text; the frame's lines leave room for them
	struct Note {
		size_t part;
		float height;
	};
	std::vector<Note> frameNotes;
	float notesHeight = 0;		// 14 + their heights, 0 for none
	std::map<size_t, float> noteHeights;
	auto noteHeight = [&](size_t note, float width) {
		auto known = noteHeights.find(note);
		if (known != noteHeights.end())
			return known->second;
		size_t mark = fLines.size();
		LayoutPart(note, {FrameGeometry{BRect(0, 0, width, 1e6), 0,
			BRect(-1e6, -1e6, 1e6, 1e6), 0, -1}});
		float height = 0;
		for (size_t k = mark; k < fLines.size(); k++)
			height = std::max(height, fLines[k].frame.bottom);
		fLines.resize(mark);
		noteHeights[note] = height;
		return height;
	};
	// the separator 6 above the first note, 144 long (pen 0.45), then the
	// notes from `top`
	auto placeNotes = [&](const std::vector<Note>& notes, float top, size_t index) {
		const BRect& frame = frames[index];
		const FrameGeometry& g = geometry[index];
		fRules.push_back({BPoint(frame.left, top - 6), BPoint(frame.left + 144, top - 6), 0.45f,
			g.clip});
		for (const Note& n : notes) {
			LayoutPart(n.part, {FrameGeometry{BRect(frame.left, top, frame.right, top + 1e6),
				g.angle, g.clip, 0, g.page}});
			top += n.height;
		}
	};
	// leaving frame `index` with its text ending at `bottom`
	auto finishFrame = [&](size_t index, float bottom) {
		if (!frameNotes.empty())
			placeNotes(frameNotes, std::max(bottom, frames[index].bottom - notesHeight) + 14,
				index);
		frameNotes.clear();
		notesHeight = 0;
	};

	for (const WPParagraph& par : wp->paragraphs) {
		if (frameIndex >= frames.size()) {
			result.overflow = true;
			return result;
		}
		const TextStyle& parStyle = CachedStyle(par.parStyle).first;

		std::string text = par.text;
		char terminator = 0;
		if (!text.empty() && (text.back() == '\n' || text.back() == '\r'
				|| IsFrameBreak(text.back()))) {
			terminator = text.back();
			text.pop_back();
		}

		std::vector<Token> tokens;
		BFont lineFont = CachedStyle(par.parStyle).second;
		for (size_t r = 0; r < std::max<size_t>(1, par.runs.size()); r++) {
			size_t start = par.runs.empty() ? 0 : par.runs[r].position;
			size_t end = r + 1 < par.runs.size() ? par.runs[r + 1].position : text.size();
			const auto& cached = CachedStyle(par.runs.empty() ? par.parStyle : par.runs[r].style);
			const TextStyle& st = cached.first;
			BFont font = cached.second;
			if (r == 0)
				lineFont = font;	// empty lines take the first run's font
			// superscript (0x40), subscript (0x80): 0.66 of the size
			// (TStyle::FontInfo), half the small ascent up or half its
			// descent down (TLineRun::Draw); the line measured with the size
			float size = font.Size(), dy = 0;
			if (st.faceFlags & 0xc0) {
				font.SetSize(size * 0.66f);
				dy = (st.faceFlags & 0x40) ? -(font.Size() * 0.95f) / 2 : font.Size() * 0.25f / 2;
			}
			if (start >= text.size())
				break;
			end = std::min(end, text.size());
			rgb_color color = make_color(st.color.r, st.color.g, st.color.b);
			std::string word;
			size_t wordStart = start;
			size_t i = start;
			auto flush = [&](bool space) {
				if (!word.empty() || space) {
					tokens.push_back({Token::kWord, word, font, color, space, wordStart, i, 0, NULL,
						size, dy});
					word.clear();
				}
				wordStart = i + 1;
			};
			for (; i < end; i++) {
				char c = text[i];
				if (c == ' ') {
					flush(true);
				} else if (c == '\t') {
					flush(false);
					tokens.push_back({Token::kTab, "", font, color, false, i, i + 1, 0, NULL, size,
						dy});
				} else if (c == 0x01) {
					flush(false);
					tokens.push_back({Token::kLineBreak, "", font, color, false, i, i + 1, 0, NULL,
						size, dy});
				} else if (c == 0x04) {
					// a field (TParagraph's misc info at this offset): what it
					// shows, or a line across the frame
					flush(false);
					const WPMiscInfo* misc = NULL;
					for (const WPMiscInfo& m : par.misc)
						if (m.position == i)
							misc = &m;
					if (misc != NULL && misc->kind == Sig("LINE"))
						tokens.push_back({Token::kRule, "", font, color, false, i, i + 1,
							misc->kind, misc, size, dy});
					else if (misc != NULL && (misc->kind == Sig("FORM")
							|| misc->kind == Sig("PAGE")))
						tokens.push_back({Token::kWord,
							FieldText(*fBook, part, *misc, geometry[0].page), font, color, false,
							i, i + 1, misc->kind, misc, size, dy});
					// a note's reference shows its mark, the note its
					// reference's (TMiscFootnoteRef, TMiscFootnoteMark)
					else if (misc != NULL && misc->kind == Sig("FREF"))
						tokens.push_back({Token::kWord, NoteMark(*misc), font, color, false, i,
							i + 1, misc->kind, misc, size, dy});
					else if (misc != NULL && misc->kind == Sig("FMRK"))
						tokens.push_back({Token::kWord, NoteMarkOf(fDocument, part), font, color,
							false, i, i + 1, misc->kind, misc, size, dy});
					wordStart = i + 1;
				} else if ((unsigned char)c >= 0x20) {
					if (word.empty())
						wordStart = i;
					word += c;
				}
			}
			flush(false);
		}

		size_t t = 0;
		bool firstLine = true;
		size_t paragraphIndex = &par - &wp->paragraphs[0];
		std::vector<size_t> paragraphLines;
		// a line moved to the next frame is laid out there (an empty one too)
		bool moved = false;
		do {
			moved = false;
			if (frameIndex >= frames.size()) {
				result.overflow = true;
				return result;
			}
			const BRect& frame = frames[frameIndex];
			// PInd, like PLef, counts from the text's edge (the User's
			// Guide: "the distance, from the left margin, that the first
			// line of the paragraph is indented")
			float left = frame.left + pad
				+ (firstLine ? parStyle.firstIndent : parStyle.leftIndent);
			float right = frame.right - pad - parStyle.rightIndent;
			float avail = std::max(10.0f, right - left);

			// fill a line: (token index, x)
			std::vector<std::pair<size_t, float>> placed;
			std::vector<Line::Piece> leaders;	// tab fill characters
			float x = 0;
			// tab positions count from the frame's text edge
			float indent = left - (frame.left + pad);
			auto segmentWidth = [&](size_t from, uint32_t alignChar, bool toAlign) {
				float w = 0;
				for (size_t k = from; k < tokens.size(); k++) {
					const Token& tk = tokens[k];
					if (tk.kind != Token::kWord)
						break;
					if (toAlign) {
						size_t at = tk.text.find(char(alignChar));
						if (at != std::string::npos)
							return w + TDocView::TextWidth(tk.font, tk.text.substr(0, at));
					}
					w += TDocView::TextWidth(tk.font, tk.text)
						+ (tk.space ? TDocView::TextWidth(tk.font, " ") : 0);
				}
				return w;
			};
			float ascent = 0, descent = 0, leading = 0;
			// Line metrics as with the BeOS Bitstream fonts Gobe documents
			// were laid out with (about 1.2 x size). Haiku's Noto fonts are
			// taller (~1.36), which would push text out of its frames.
			auto measure = [&](const BFont& f, float size = 0) {
				if (size <= 0)
					size = f.Size();
				ascent = std::max(ascent, size * 0.95f);
				descent = std::max(descent, size * 0.25f);
			};
			bool forced = false;
			while (t < tokens.size()) {
				const Token& tk = tokens[t];
				// TMiscLineInfo::Width: the frame's width, a line of its own
				if (tk.kind == Token::kRule) {
					if (!placed.empty())
						break;
					placed.push_back({t, x});
					x = avail;
					measure(tk.font, tk.size);
					t++;
					break;
				}
				if (tk.kind == Token::kLineBreak) {
					measure(tk.font, tk.size);
					t++;
					forced = true;
					break;
				}
				if (tk.kind == Token::kTab) {
					// the paragraph's stops (PTab), then every half inch
					const TabStop* stop = NULL;
					for (const TabStop& ts : parStyle.tabs)
						if (ts.position - indent > x + 0.5f) {
							stop = &ts;
							break;
						}
					// TParagraph::GetTabInfo: before the larger indent a tab
					// goes there (a hanging indent) unless a stop comes first
					float hang = std::max(parStyle.leftIndent, parStyle.firstIndent);
					float target;
					if (x + indent < hang && (stop == NULL || stop->position >= hang))
						x = hang - indent;
					else if (stop != NULL) {
						target = stop->position - indent;
						float seg = 0;
						if (stop->type == 1)
							seg = segmentWidth(t + 1, 0, false) / 2;
						else if (stop->type == 2)
							seg = segmentWidth(t + 1, 0, false);
						else if (stop->type == 3)
							seg = segmentWidth(t + 1, stop->alignChar, true);
						float start = std::max(x, target - seg);
						if (stop->fill != 0 && stop->fillChar >= 0x20 && stop->fillChar < 0x7f) {
							char c[2] = {char(stop->fillChar), 0};
							float cw = std::max(1.0f, TDocView::TextWidth(tk.font, c));
							int n = int((start - x - cw / 2) / cw);
							if (n > 0)
								leaders.push_back({std::string(n, c[0]), tk.font, tk.color,
									start - n * cw});
						}
						x = start;
					} else
						x = (floorf((x + indent) / tabStop) + 1) * tabStop - indent;
					t++;
					continue;
				}
				float ww = TDocView::TextWidth(tk.font, tk.text);
				// the padding is the port's, not Gobe's: words may use it; a
				// word at a tab stop inside the frame stays on the line
				bool atTab = t > 0 && tokens[t - 1].kind == Token::kTab && x <= avail;
				if (!placed.empty() && !atTab && x + ww > avail + 2 * pad)
					break;
				placed.push_back({t, x});
				x += ww + (tk.space ? TDocView::TextWidth(tk.font, " ") : 0);
				measure(tk.font, tk.size);
				t++;
			}
			if (placed.empty())
				measure(lineFont);
			// TParagraph line spacing (LSpa): a multiple adds leading, an
			// exact height splits into ascent/descent
			if (parStyle.lineSpacingMultiple)
				leading = std::max(leading, (parStyle.lineSpacing - 1) * ascent);
			else if (parStyle.lineSpacing > 0) {
				float v = parStyle.lineSpacing;
				if (ascent + descent <= v)
					ascent = v - descent;
				else {
					ascent = roundf(v * 0.66f);
					descent = v - ascent;
				}
				leading = 0;
			}
			// TWPFrame: space before goes above the paragraph's first
			// line, space after below its last; in lines, the value times
			// that line's ascent + descent
			float lineHeight = ascent + descent;
			if (firstLine)
				ascent += parStyle.spaceBeforeLines
					? parStyle.spaceBefore * lineHeight : parStyle.spaceBefore;
			if (t >= tokens.size())
				descent += parStyle.spaceAfterLines
					? parStyle.spaceAfter * lineHeight : parStyle.spaceAfter;
			float height = ascent + descent + leading;
			// the footnotes of the line's references need room too
			std::vector<Note> lineNotes;
			float lineNotesHeight = 0;
			for (const auto& placement : placed) {
				const Token& tk = tokens[placement.first];
				if (tk.field != Sig("FREF") || tk.misc == NULL || tk.misc->endnote)
					continue;
				int note = NotePart(fDocument, *tk.misc);
				if (note < 0 || fDocument.WordPart(size_t(note)) == NULL)
					continue;
				lineNotes.push_back({size_t(note), noteHeight(size_t(note), frame.Width())});
				lineNotesHeight += lineNotes.back().height;
			}
			if (!lineNotes.empty() && frameNotes.empty())
				lineNotesHeight += 14;
			if (y + height + notesHeight + lineNotesHeight > frame.bottom && y > frame.top) {
				// frame full: continue in the next frame
				finishFrame(frameIndex, y);
				frameIndex++;
				if (frameIndex >= frames.size()) {
					result.overflow = true;
					return result;
				}
				y = frames[frameIndex].top;
				if (!placed.empty())
					t = placed.front().first;
				moved = true;
				continue;
			}

			float width = x;
			float shift = 0, extra = 0;
			// TLineRun: 2 right, 1 centered
			if (parStyle.alignment == 2)
				shift = avail - width;
			else if (parStyle.alignment == 1)
				shift = (avail - width) / 2;
			else if (parStyle.alignment == 3 && t < tokens.size() && !forced
					&& placed.size() > 1)
				extra = (avail - width) / (placed.size() - 1);

			Line line;
			line.part = part;
			line.paragraph = paragraphIndex;
			line.from = placed.empty() ? (t < tokens.size() ? tokens[t].start : text.size())
				: tokens[placed.front().first].start;
			line.frame.Set(left, y, right, y + height);
			line.baseline = y + ascent;
			line.angle = geometry[frameIndex].angle;
			line.center = frame.LeftTop() + BPoint(frame.Width() / 2, frame.Height() / 2);
			line.clip = geometry[frameIndex].clip;
			line.textFrame = frame;
			for (size_t k = 0; k < placed.size(); k++) {
				const Token& tk = tokens[placed[k].first];
				Line::Piece piece{tk.text, tk.font, tk.color,
					shift + placed[k].second + extra * k, tk.start, tk.end, tk.field, 0, tk.dy};
				// a page number: the page this frame is on
				if (tk.field == Sig("PAGE")) {
					int page = geometry[frameIndex].page;
					piece.text = page < 0 ? "#" : std::to_string(page + 1);
				}
				// TMiscLineInfo::Draw: from 10 past where it is to 10 before
				// the frame's right edge
				if (tk.kind == Token::kRule) {
					piece.x = placed[k].second + 10;
					piece.ruleEnd = frame.right - 10 - left;
				}
				line.pieces.push_back(piece);
			}
			for (Line::Piece& leader : leaders) {
				leader.x += shift;
				line.pieces.push_back(std::move(leader));
			}
			// where each frame's text starts, the last frame with text
			int index = geometry[frameIndex].index;
			uint32_t at = paragraphStart + uint32_t(firstLine ? 0 : line.from);
			result.starts[index] = std::min(result.starts[index], at);
			result.lastFrame = std::max(result.lastFrame, index);
			paragraphLines.push_back(fLines.size());
			fLines.push_back(std::move(line));
			frameNotes.insert(frameNotes.end(), lineNotes.begin(), lineNotes.end());
			notesHeight += lineNotesHeight;
			y += height;
			firstLine = false;
		} while (t < tokens.size() || moved);
		// each line runs to where the next one starts (notes laid out in
		// between are other parts')
		for (size_t n = 0; n < paragraphLines.size(); n++) {
			Line& line = fLines[paragraphLines[n]];
			if (n + 1 < paragraphLines.size())
				line.to = std::max(line.from, fLines[paragraphLines[n + 1]].from);
			else
				line.to = text.size();
			if (n == 0)
				line.from = 0;
		}

		// (browse view: one frame, the text goes on in it)
		if (IsFrameBreak(terminator) && !fBrowse) {
			finishFrame(frameIndex, y);
			frameIndex++;
			if (frameIndex < frames.size())
				y = frames[frameIndex].top;
		}
		paragraphStart += uint32_t(par.text.size());
	}
	// the last frame's footnotes; then the endnotes (TWPPortal::
	// PlaceEndnotes): after the text, 14 down, above those footnotes, in
	// the next frames when they don't fit
	float footnotes = notesHeight;
	if (frameIndex < frames.size())
		finishFrame(frameIndex, y);
	std::vector<Note> endnotes;
	for (const WPParagraph& par : wp->paragraphs)
		for (const WPMiscInfo& m : par.misc) {
			int note = m.endnote ? NotePart(fDocument, m) : -1;
			if (note >= 0 && fDocument.WordPart(size_t(note)) != NULL)
				endnotes.push_back({size_t(note), 0});
		}
	if (!endnotes.empty()) {
		if (frameIndex >= frames.size()) {
			result.overflow = true;
			return result;
		}
		size_t index = frameIndex;
		float top = y > frames[index].top ? y + 14 : frames[index].top;
		float limit = frames[index].bottom - footnotes;
		float start = top;
		std::vector<Note> here;
		for (Note& n : endnotes) {
			n.height = noteHeight(n.part, frames[index].Width());
			if (top + n.height > limit && (!here.empty() || top > frames[index].top)) {
				if (!here.empty())
					placeNotes(here, start, index);
				here.clear();
				if (++index >= frames.size()) {
					result.overflow = true;
					return result;
				}
				top = start = frames[index].top;
				limit = frames[index].bottom;
			}
			here.push_back(n);
			top += n.height;
			result.lastFrame = std::max(result.lastFrame, geometry[index].index);
		}
		placeNotes(here, start, index);
	}
	return result;
}


// Gobe's colors carry an alpha byte, but BeOS drew them in copy mode,
// ignoring it (gradients store black as 00000000); only the opacity
// properties make things translucent.
static rgb_color
ToColor(const Color& c, float opacity)
{
	opacity = std::max(0.0f, std::min(1.0f, opacity));
	return make_color(c.r, c.g, c.b, uint8(255 * opacity));
}


// Sets the high color (or a gradient) for `ink`; false if nothing would show.
bool
TPageView::SetInk(const Ink& ink, float opacity, BRect rect, BGradient** gradient)
{
	*gradient = NULL;
	if (!ink.set || opacity <= 0)
		return false;
	if (ink.gradient) {
		BGradient* g;
		if (ink.gradientType == Sig("GDrl")) {
			BPoint center(rect.left + rect.Width() * ink.center[0],
				rect.top + rect.Height() * ink.center[1]);
			g = new BGradientRadial(center, std::max(rect.Width(), rect.Height()) / 2);
		} else {
			// directional: across the rect at the gradient's angle
			float a = ink.angle * M_PI / 180;
			BPoint c = rect.LeftTop() + BPoint(rect.Width() / 2, rect.Height() / 2);
			BPoint d(cosf(a) * rect.Width() / 2, -sinf(a) * rect.Height() / 2);
			g = new BGradientLinear(c - d, c + d);
		}
		for (int i = 0; i < ink.colorCount; i++)
			g->AddColor(ToColor(ink.colors[i], opacity), 255 * i / (ink.colorCount - 1));
		*gradient = g;
		return true;
	}
	SetHighColor(ToColor(ink.color, opacity));
	return true;
}


void
TPageView::DrawFrameBackground(const Document::PlacedFrame& f, BRect r)
{
	ShapeStyle style = ResolveShapeStyle(fDocument.styles, f.frame->style);
	BGradient* gradient;
	if (!SetInk(style.background, style.backgroundOpacity, r, &gradient))
		return;
	SetDrawingMode(B_OP_ALPHA);
	if (gradient != NULL) {
		FillRect(r, *gradient);
		delete gradient;
	} else
		FillRect(r);
	SetDrawingMode(B_OP_COPY);
}


void
TPageView::FillInk(BRect rect, const Ink& ink, float opacity, BRect inkRect)
{
	BGradient* gradient;
	if (!SetInk(ink, opacity, inkRect, &gradient))
		return;
	SetDrawingMode(opacity >= 0.999f ? B_OP_COPY : B_OP_ALPHA);
	if (gradient != NULL) {
		FillRect(rect, *gradient);
		delete gradient;
	} else
		FillRect(rect);
	SetDrawingMode(B_OP_COPY);
}


const CellStyle&
TPageView::SpreadsheetStyle(const std::vector<uint32_t>& stack)
{
	auto found = fCellStyles.find(stack);
	if (found != fCellStyles.end())
		return found->second;
	return fCellStyles[stack] = ResolveCellStyle(fDocument.styles, stack);
}


// A spreadsheet frame (TSSDisplayManager: DrawColumnFills/DrawRowFills,
// DrawFills, DrawGrid, DrawCells with DrawCellBorders). The frame shows the
// sheet from its first cell (TSSFrame range) on; columns and rows start at
// rounded positions (RecutCells).
TPageView::SheetGeometry
TPageView::SheetLayout(const Document::PlacedFrame& f, BRect r) const
{
	const SSPart& ss = *fDocument.SpreadsheetPart(f.part);
	SheetGeometry g;
	// headers (only shown when the options say so)
	g.headerHeight = ss.options[3] ? ss.defaultHeight : 0;
	g.headerWidth = ss.options[4] ? 36 : 0;
	// visible columns and rows, with their view positions
	g.firstRow = f.frame->cells[0];
	g.firstColumn = f.frame->cells[1];
	for (float x = r.left + g.headerWidth; g.firstColumn + int(g.xs.size()) <= 0x2be; ) {
		g.xs.push_back(x);
		if (x > r.right)
			break;
		x += roundf(ss.ColumnWidth(g.firstColumn + g.xs.size() - 1));
	}
	for (float y = r.top + g.headerHeight; g.firstRow + int(g.ys.size()) <= 0x3fff; ) {
		g.ys.push_back(y);
		if (y > r.bottom)
			break;
		y += roundf(ss.RowHeight(g.firstRow + g.ys.size() - 1));
	}
	return g;
}


void
TPageView::DrawSpreadsheet(const Document::PlacedFrame& f, BRect r)
{
	const SSPart& ss = *fDocument.SpreadsheetPart(f.part);
	SSEvaluator& eval = *fBook->For(f.part);
	PushState();
	BRegion clip(r);
	ConstrainClippingRegion(&clip);

	SheetGeometry geometry = SheetLayout(f, r);
	bool columnHeaders = ss.options[3], rowHeaders = ss.options[4];
	float headerHeight = geometry.headerHeight, headerWidth = geometry.headerWidth;
	int firstRow = geometry.firstRow, firstColumn = geometry.firstColumn;
	const std::vector<float>& xs = geometry.xs;
	const std::vector<float>& ys = geometry.ys;
	int columns = int(xs.size()) - 1, rows = int(ys.size()) - 1;
	auto cellRect = [&](int row, int column) {
		return BRect(xs[column - firstColumn], ys[row - firstRow],
			xs[column - firstColumn + 1], ys[row - firstRow + 1]);
	};
	auto rangeRect = [&](const SSRange& range, BRect& out) {
		int r0 = std::max<int>(range.from.a, firstRow);
		int r1 = std::min<int>(range.to.a, firstRow + rows - 1);
		int c0 = std::max<int>(range.from.b, firstColumn);
		int c1 = std::min<int>(range.to.b, firstColumn + columns - 1);
		if (r0 > r1 || c0 > c1)
			return false;
		out = cellRect(r0, c0) | cellRect(r1, c1);
		return true;
	};
	auto stackFor = [&](int row, int column, const SSCell* cell) {
		std::vector<uint32_t> stack = {ss.style};
		if (const SSHeader* h = ss.Column(column))
			stack.push_back(h->cell.style);
		if (const SSHeader* h = ss.Row(row))
			stack.push_back(h->cell.style);
		if (cell != NULL)
			stack.push_back(cell->style);
		return stack;
	};

	// DrawColumnFills / DrawRowFills: a header cell's style fills its whole
	// column or row (one gradient over it); then the cells' own fills
	BRect cells(xs.front(), ys.front(), xs.back(), ys.back());
	for (int c = 0; c < columns; c++) {
		const SSHeader* h = ss.Column(firstColumn + c);
		if (h == NULL)
			continue;
		const CellStyle& style = SpreadsheetStyle({ss.style, h->cell.style});
		BRect cr(xs[c], cells.top, xs[c + 1], cells.bottom);
		if (style.fill.set && style.fillOpacity > 0)
			FillInk(cr, style.fill, style.fillOpacity, cr);
	}
	for (int k = 0; k < rows; k++) {
		const SSHeader* h = ss.Row(firstRow + k);
		if (h == NULL)
			continue;
		const CellStyle& style = SpreadsheetStyle({ss.style, h->cell.style});
		BRect rr(cells.left, ys[k], cells.right, ys[k + 1]);
		if (style.fill.set && style.fillOpacity > 0)
			FillInk(rr, style.fill, style.fillOpacity, rr);
	}
	for (const SSCell& cell : ss.cells) {
		int row = cell.id.a, column = cell.id.b;
		if (row < firstRow || row >= firstRow + rows || column < firstColumn
			|| column >= firstColumn + columns)
			continue;
		const CellStyle& style = SpreadsheetStyle({ss.style, cell.style});
		BRect cr = cellRect(row, column);
		if (style.fill.set && style.fillOpacity > 0)
			FillInk(cr, style.fill, style.fillOpacity, cr);
	}
	// TSSFillManager: fills over ranges (a gradient spans all its ranges)
	for (const SSFill& fill : ss.fills) {
		const CellStyle& style = SpreadsheetStyle({fill.style});
		if (!style.fill.set || style.fillOpacity <= 0 || fill.cells.empty())
			continue;
		BRect span;
		bool any = false;
		for (const SSRange& range : fill.cells) {
			BRect rr;
			if (rangeRect(range, rr)) {
				span = any ? (span | rr) : rr;
				any = true;
			}
		}
		if (!any)
			continue;
		for (const SSRange& range : fill.cells) {
			BRect rr;
			if (rangeRect(range, rr))
				FillInk(rr, style.fill, style.fillOpacity,
					style.fill.gradient ? span : rr);
		}
	}

	// grid lines (GridStyle's pen)
	if (ss.options[0]) {
		const CellStyle& grid = SpreadsheetStyle({ss.displayStyles[0]});
		rgb_color color = grid.pen.set ? make_color(grid.pen.color.r, grid.pen.color.g,
			grid.pen.color.b) : make_color(200, 200, 200);
		SetHighColor(color);
		SetPenSize(1);
		for (float x : xs)
			StrokeLine(BPoint(x, ys.front()), BPoint(x, ys.back()));
		for (float y : ys)
			StrokeLine(BPoint(xs.front(), y), BPoint(xs.back(), y));
	}

	// headers
	if (columnHeaders || rowHeaders) {
		const CellStyle& header = SpreadsheetStyle({ss.displayStyles[1]});
		BFont font = TDocView::MapFont(header.text);
		SetFont(&font);
		auto drawHeader = [&](BRect hr, const std::string& text) {
			FillInk(hr, header.fill.set ? header.fill : Ink(), 1, hr);
			SetHighColor(0, 0, 0);
			StrokeRect(hr);
			float w = font.StringWidth(text.c_str());
			SetDrawingMode(B_OP_OVER);
			DrawString(text.c_str(), BPoint(hr.left + (hr.Width() - w) / 2,
				hr.bottom - font.Size() * 0.3f));
			SetDrawingMode(B_OP_COPY);
		};
		for (int c = 0; columnHeaders && c < columns; c++) {
			std::string name = CellName({0, uint16_t(firstColumn + c)});
			drawHeader(BRect(xs[c], r.top, xs[c + 1], r.top + headerHeight),
				name.substr(0, name.find_first_of("0123456789")));
		}
		for (int k = 0; rowHeaders && k < rows; k++)
			drawHeader(BRect(r.left, ys[k], r.left + headerWidth, ys[k + 1]),
				std::to_string(firstRow + k + 1));
		// the selected columns' (rows') headings darkened (DarkGrayInk,
		// B_OP_MIN)
		if (fSelectedCell.active && fSelectedCell.part == f.part
				&& fSelectedCell.kind != SelectionKind::cells) {
			bool byColumn = fSelectedCell.kind == SelectionKind::columns;
			int from = byColumn ? std::min(fSelectedCell.column, fSelectedCell.anchorColumn)
				: std::min(fSelectedCell.row, fSelectedCell.anchorRow);
			int to = byColumn ? std::max(fSelectedCell.column, fSelectedCell.anchorColumn)
				: std::max(fSelectedCell.row, fSelectedCell.anchorRow);
			SetDrawingMode(B_OP_MIN);
			SetHighColor(112, 112, 112);
			for (int c = 0; byColumn && columnHeaders && c < columns; c++)
				if (firstColumn + c >= from && firstColumn + c <= to)
					FillRect(BRect(xs[c], r.top, xs[c + 1], r.top + headerHeight));
			for (int k = 0; !byColumn && rowHeaders && k < rows; k++)
				if (firstRow + k >= from && firstRow + k <= to)
					FillRect(BRect(r.left, ys[k], r.left + headerWidth, ys[k + 1]));
			SetDrawingMode(B_OP_COPY);
		}
	}

	// cell text; text spills into empty neighbours (TOverlapManager): laid
	// out first, so borders inside an overflow area can be left out
	auto isEmpty = [&](int row, int column) {
		const SSCell* c = ss.Cell(row, column);
		return c == NULL || c->kind == Sig("NONE")
			|| eval.Value(row, column).type == SSValue::kNone;
	};
	struct Text {
		std::string text;
		BFont font;
		rgb_color color;
		BPoint at;
		BRect area;					// view rect it may draw in
		int r0, r1, c0, c1;			// the cells it covers
	};
	std::vector<Text> texts;
	for (const SSCell& cell : ss.cells) {
		int row = cell.id.a, column = cell.id.b;
		if (row < firstRow || row >= firstRow + rows || column < firstColumn
			|| column >= firstColumn + columns)
			continue;
		SSValue value = eval.Value(row, column);
		// Show Formulas (TSSDisplayManager::CellText): a formula cell shows
		// itself, its formula, not its result
		if (ss.options[2] && cell.kind == Sig("Fmla"))
			value = SSValue::String(CellInput(ss, uint16_t(row), uint16_t(column)));
		if (value.type == SSValue::kNone)
			continue;
		const CellStyle& style = SpreadsheetStyle(stackFor(row, column, &cell));
		bool red;
		std::string text = FormatCellValue(value, style.format, &red);
		// Mark Circular Refs (TSSDisplayManager::CellText): a formula in a
		// circle of references (TFormulaCell::Circular) between bullets
		if (ss.options[5] && cell.kind == Sig("Fmla") && (cell.flags & 4))
			text = "\xe2\x80\xa2 " + text + " \xe2\x80\xa2";
		if (text.empty())
			continue;
		Text t;
		t.font = TDocView::MapFont(style.text);
		float width = t.font.StringWidth(text.c_str());
		BRect box = cellRect(row, column);
		int align = CellHAlign(style, value);
		int c0 = column, c1 = column, r0 = row, r1 = row;
		// left-aligned text grows right, right-aligned left, centered both
		if (value.type == SSValue::kString) {
			auto need = [&]() {
				return cellRect(row, c1).right - cellRect(row, c0).left - 8 < width;
			};
			bool canRight = align != 3, canLeft = align != 1;
			while (need() && (canRight || canLeft)) {
				if (canRight) {
					if (c1 + 1 < firstColumn + columns && isEmpty(row, c1 + 1))
						c1++;
					else
						canRight = false;
				}
				if (canLeft && need()) {
					if (c0 - 1 >= firstColumn && isEmpty(row, c0 - 1))
						c0--;
					else
						canLeft = false;
				}
				if (align == 2 && (!canLeft || !canRight))
					break;	// centered: only as far as both sides go
			}
		}
		BRect area = cellRect(row, c0) | cellRect(row, c1);
		float x;
		if (align == 2)
			x = box.left + (box.Width() - width) / 2;
		else if (align == 3)
			x = area.right - width - 4;
		else
			x = area.left + 4;
		float size = t.font.Size();
		float ascent = size * 0.95f, descent = size * 0.25f;
		// taller than its row: grows down (top/center aligned) or up
		auto rowEmpty = [&](int rw) {
			for (int c = c0; c <= c1; c++)
				if (!isEmpty(rw, c))
					return false;
			return true;
		};
		if (ascent + descent > area.Height()) {
			if (style.vAlign == 3) {
				while (r0 - 1 >= firstRow && ascent + descent > area.Height()
					&& rowEmpty(r0 - 1))
					area.top = cellRect(--r0, column).top;
			} else {
				while (r1 + 1 < firstRow + rows && ascent + descent > area.Height()
					&& rowEmpty(r1 + 1))
					area.bottom = cellRect(++r1, column).bottom;
			}
			if (style.vAlign != 3)
				box.bottom = std::max(box.bottom, std::min(area.bottom,
					box.top + ascent + descent));
			else
				box.top = std::min(box.top, std::max(area.top, box.bottom - ascent - descent));
		}
		float y;
		if (style.vAlign == 2)
			y = box.top + (box.Height() + ascent - descent) / 2;
		else if (style.vAlign == 1)
			y = box.top + ascent;
		else
			y = box.bottom - descent;
		t.text = text;
		t.color = red ? make_color(255, 0, 0)
			: make_color(style.text.color.r, style.text.color.g, style.text.color.b);
		t.at = BPoint(x, y);
		t.area = area & r;
		t.r0 = r0;
		t.r1 = r1;
		t.c0 = c0;
		t.c1 = c1;
		texts.push_back(std::move(t));
	}
	// which overflow area covers a cell (none: -1)
	std::map<std::pair<int, int>, int> covered;
	for (size_t i = 0; i < texts.size(); i++)
		if (texts[i].r0 != texts[i].r1 || texts[i].c0 != texts[i].c1)
			for (int rw = texts[i].r0; rw <= texts[i].r1; rw++)
				for (int c = texts[i].c0; c <= texts[i].c1; c++)
					covered[{rw, c}] = int(i);

	// the selected range, under borders and text (the active cell keeps its
	// background)
	if (fSelectedCell.active && fSelectedCell.part == f.part) {
		int r0 = std::max(firstRow, std::min(fSelectedCell.row, fSelectedCell.anchorRow));
		int r1 = std::min(firstRow + rows - 1,
			std::max(fSelectedCell.row, fSelectedCell.anchorRow));
		int c0 = std::max(firstColumn, std::min(fSelectedCell.column, fSelectedCell.anchorColumn));
		int c1 = std::min(firstColumn + columns - 1,
			std::max(fSelectedCell.column, fSelectedCell.anchorColumn));
		SetHighColor(180, 205, 255);
		for (int r = r0; r <= r1; r++)
			for (int c = c0; c <= c1; c++)
				if (r != fSelectedCell.row || c != fSelectedCell.column)
					FillRect(cellRect(r, c));
		// the other ranges of a selection of several
		for (const SSRange& range : fSelectedCell.extra) {
			int a0 = std::max<int>(firstRow, range.from.a);
			int a1 = std::min<int>(firstRow + rows - 1, range.to.a);
			int b0 = std::max<int>(firstColumn, range.from.b);
			int b1 = std::min<int>(firstColumn + columns - 1, range.to.b);
			for (int r = a0; r <= a1; r++)
				for (int c = b0; c <= b1; c++)
					if (r != fSelectedCell.row || c != fSelectedCell.column)
						FillRect(cellRect(r, c));
		}
	}

	// borders (TSSDisplayManager::DrawCellBorders): left/top edges are
	// skipped on the frame's first column/row and inside overflow areas
	for (int row = firstRow; row < firstRow + rows; row++) {
		for (int column = firstColumn; column < firstColumn + columns; column++) {
			const CellStyle& style = SpreadsheetStyle(stackFor(row, column,
				ss.Cell(row, column)));
			BRect b = cellRect(row, column);
			auto over = covered.find({row, column});
			const Text* area = over != covered.end() ? &texts[over->second] : NULL;
			for (int side = 0; side < 4; side++) {
				const CellStyle::Border& border = style.borders[side];
				if (!border.shown || !border.ink.set)
					continue;
				if ((side == 0 && column == firstColumn) || (side == 1 && row == firstRow))
					continue;
				if (area != NULL && ((side == 0 && column > area->c0)
						|| (side == 2 && column < area->c1)
						|| (side == 1 && row > area->r0)
						|| (side == 3 && row < area->r1)))
					continue;
				SetHighColor(border.ink.color.r, border.ink.color.g, border.ink.color.b);
				SetPenSize(std::max(1.0f, border.size));
				BPoint a, e;
				switch (side) {
					case 0: a = b.LeftTop(); e = b.LeftBottom(); break;
					case 1: a = b.LeftTop(); e = b.RightTop(); break;
					case 2: a = b.RightTop(); e = b.RightBottom(); break;
					default: a = b.LeftBottom(); e = b.RightBottom(); break;
				}
				StrokeLine(a, e);
			}
		}
	}

	SetDrawingMode(B_OP_OVER);
	for (const Text& t : texts) {
		PushState();
		BRegion cellClip(t.area);
		ConstrainClippingRegion(&cellClip);
		SetFont(&t.font);
		SetHighColor(t.color);
		DrawString(t.text.c_str(), t.at);
		PopState();
	}
	SetDrawingMode(B_OP_COPY);
	// the locked cells shaded (TSSDisplayManager::DrawCells, ShowLockedCells):
	// each locked range filled over the cells with LockStyle's fill, in
	// B_OP_OVER
	if (ss.options[1]) {
		const CellStyle& lock = SpreadsheetStyle({ss.displayStyles[4]});
		for (const SSRange& range : ss.locked) {
			BRect rr;
			BGradient* gradient = NULL;
			if (!lock.fill.set || lock.fillOpacity <= 0 || !rangeRect(range, rr)
					|| !SetInk(lock.fill, lock.fillOpacity, rr, &gradient))
				continue;
			SetDrawingMode(lock.fillOpacity >= 0.999f ? B_OP_OVER : B_OP_ALPHA);
			// (its pattern, the low color's pixels left out in B_OP_OVER)
			pattern stipple;
			for (int i = 0; i < 8; i++)
				stipple.data[i] = lock.fill.pattern[i];
			if (gradient != NULL) {
				FillRect(rr, *gradient);
				delete gradient;
			} else
				FillRect(rr, stipple);
			SetDrawingMode(B_OP_COPY);
		}
	}
	SetPenSize(1);
	// the cell being edited / selected
	if (fSelectedCell.active && fSelectedCell.part == f.part
		&& fSelectedCell.row >= firstRow && fSelectedCell.row < firstRow + rows
		&& fSelectedCell.column >= firstColumn && fSelectedCell.column < firstColumn + columns) {
		int r0 = std::min(fSelectedCell.row, fSelectedCell.anchorRow);
		int r1 = std::max(fSelectedCell.row, fSelectedCell.anchorRow);
		int c0 = std::min(fSelectedCell.column, fSelectedCell.anchorColumn);
		int c1 = std::max(fSelectedCell.column, fSelectedCell.anchorColumn);
		bool singleCell = r0 == r1 && c0 == c1;
		// 1px black outline around the whole visible range when it has more than
		// one cell
		if (!singleCell) {
			BRect rangeRect = cellRect(r0, c0) | cellRect(r1, c1);
			SetHighColor(0, 0, 0);
			SetPenSize(1);
			StrokeRect(rangeRect);
		}
		// 2px outline around the active cell
		SetHighColor(0, 0, 0);
		SetPenSize(2);
		StrokeRect(cellRect(fSelectedCell.row, fSelectedCell.column));
		SetPenSize(1);
	}
	PopState();
}


// An image part (TFrameIM): the frame shows its rectangle of the bitmap.
void
TPageView::DrawImage(const Document::PlacedFrame& f, BRect r)
{
	const IMPart& im = *fDocument.ImagePart(f.part);
	if (im.image.Empty())
		return;
	auto found = fImages.find(f.part);
	if (found == fImages.end()) {
		BRect bounds(0, 0, im.image.Width() - 1, im.image.Height() - 1);
		BBitmap* bitmap = new BBitmap(bounds, B_RGBA32);
		if (bitmap->InitCheck() != B_OK || bitmap->ImportBits(im.image.bits.data(),
				im.image.bits.size(), im.image.bytesPerRow, 0,
				color_space(im.image.colorSpace)) != B_OK) {
			delete bitmap;
			bitmap = NULL;
		}
		found = fImages.emplace(f.part, std::unique_ptr<BBitmap>(bitmap)).first;
	}
	if (!found->second)
		return;
	const float* c = f.frame->sheetRect;
	BRect source = c[2] >= c[0] ? BRect(c[0], c[1], c[2], c[3]) : found->second->Bounds();
	BRect dest(r.left, r.top, r.left + source.Width(), r.top + source.Height());
	PushState();
	BRegion clip(r);
	ConstrainClippingRegion(&clip);
	SetDrawingMode(B_OP_ALPHA);
	SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
	DrawBitmap(found->second.get(), source, dest);
	PopState();
}


// TClockFrame::DrawContent: the hours at 0.9 of the radius, and second,
// minute and hour hands (0.9, 0.75, 0.5 long; 1, 3, 5 wide) for now.
void
TPageView::DrawClock(const Document::PlacedFrame& f, BRect r)
{
	TextStyle text = ResolveTextStyle(fDocument.styles, f.frame->style);
	ShapeStyle shape = ResolveShapeStyle(fDocument.styles, f.frame->style);
	BFont font = TDocView::MapFont(text);
	BPoint center(r.left + r.Width() / 2, r.top + r.Height() / 2);
	float rx = 0.9f * r.Width() / 2, ry = 0.9f * r.Height() / 2;
	// angles clockwise from 3 o'clock (y down)
	auto at = [&](float degrees, float scale) {
		float a = degrees * M_PI / 180;
		return BPoint(center.x + cosf(a) * rx * scale, center.y + sinf(a) * ry * scale);
	};
	PushState();
	SetFont(&font);
	SetDrawingMode(B_OP_OVER);
	SetHighColor(text.color.r, text.color.g, text.color.b);
	float ascent = font.Size() * 0.7f;
	for (int hour = 1; hour <= 12; hour++) {
		BString label;
		label << hour;
		float w = font.StringWidth(label.String());
		BPoint p = at((hour - 3) * 30.0f, 1);
		DrawString(label.String(), BPoint(p.x - w / 2, p.y + ascent / 2));
	}
	time_t now = time(NULL);
	struct tm tm;
	localtime_r(&now, &tm);
	rgb_color pen = shape.pen.set ? make_color(shape.pen.color.r, shape.pen.color.g,
		shape.pen.color.b) : make_color(0, 0, 0);
	SetHighColor(pen);
	SetLineMode(B_ROUND_CAP, B_ROUND_JOIN);
	SetPenSize(1);
	StrokeLine(center, at(tm.tm_sec * 6.0f + 270, 0.9f));
	SetPenSize(3);
	StrokeLine(center, at((tm.tm_sec / 60.0f + tm.tm_min) * 6.0f + 270, 0.75f));
	SetPenSize(5);
	StrokeLine(center, at((tm.tm_min / 60.0f + tm.tm_hour) * 30.0f + 270, 0.5f));
	PopState();
}


// A table (TTable::Draw): each cell's fill (its text frame's style), the
// cell outlines in the table frame's pen, then the cells' own borders (their
// frames' pens). The cells' text is drawn with their word processing frames.
void
TPageView::DrawTable(const Document::PlacedFrame& f, BRect r)
{
	const TablePart& table = *fDocument.Table(f.part);
	auto cellRect = [&](const TableCell& c) {
		return BRect(r.left + c.origin[0], r.top + c.origin[1],
			r.left + c.origin[0] + c.size[0], r.top + c.origin[1] + c.size[1]);
	};
	auto cellStyle = [&](const TableCell& c) {
		const gobe::Frame* frame = fDocument.FrameOf(c.frame[0], c.frame[1]);
		return ResolveShapeStyle(fDocument.styles, frame != NULL ? frame->style : 0);
	};
	PushState();
	for (const TableRow& row : table.rows)
		for (const TableCell& c : row.cells) {
			ShapeStyle style = cellStyle(c);
			if (style.fill.set && style.fillOpacity > 0)
				FillInk(cellRect(c), style.fill, style.fillOpacity, cellRect(c));
		}
	ShapeStyle grid = ResolveShapeStyle(fDocument.styles, f.frame->style);
	auto stroke = [&](const ShapeStyle& style, BRect cr) {
		if (!style.pen.set || style.penOpacity <= 0 || style.penSize <= 0)
			return;
		SetDrawingMode(style.penOpacity >= 0.999f ? B_OP_COPY : B_OP_ALPHA);
		SetHighColor(ToColor(style.pen.color, style.penOpacity));
		SetPenSize(style.penSize);
		StrokeRect(cr);
	};
	for (const TableRow& row : table.rows)
		for (const TableCell& c : row.cells)
			stroke(grid, cellRect(c));
	for (const TableRow& row : table.rows)
		for (const TableCell& c : row.cells)
			stroke(cellStyle(c), cellRect(c));
	PopState();
}


// A chart: the plot in the renderer piece's rectangle (grouped bars for bar
// charts, lines otherwise), the legend and the title pieces. Category
// labels that the chart keeps as text frames are drawn with those.
void
TPageView::DrawChart(const Document::PlacedFrame& f, BRect r)
{
	const ChartPart& chart = *fDocument.Chart(f.part);
	BRect plot = r, legend, title;
	std::string titleText;
	bool hasLegend = false, hasTitle = false, labelFrames = false;
	uint32_t titleStyle = 0, legendStyle = 0;
	for (const GRLayer& layer : chart.graphics.layers) {
		for (const Shape& s : layer.shapes) {
			BRect sr(r.left + s.origin[0], r.top + s.origin[1],
				r.left + s.origin[0] + s.size[0], r.top + s.origin[1] + s.size[1]);
			if (s.kind == Sig("Rndr"))
				plot = sr;
			else if (s.kind == Sig("Lgnd")) {
				legend = sr;
				legendStyle = s.style;
				hasLegend = true;
			} else if (s.kind == Sig("Titl")) {
				title = sr;
				titleText = s.text;
				titleStyle = s.style;
				hasTitle = true;
			} else if (s.kind == Sig("FRAM"))
				labelFrames = true;
		}
	}
	size_t categories = chart.Categories(), seriesCount = chart.SeriesCount();
	if (categories == 0 || seriesCount == 0)
		return;
	auto seriesColor = [&](size_t s) {
		if (s < chart.series.size()) {
			ShapeStyle st = ResolveShapeStyle(fDocument.styles, chart.series[s].style);
			if (st.fill.set)
				return make_color(st.fill.color.r, st.fill.color.g, st.fill.color.b);
		}
		static const rgb_color kDefault[] = {{255, 0, 0, 255}, {0, 200, 0, 255},
			{0, 0, 255, 255}, {255, 200, 0, 255}};
		return kDefault[s % 4];
	};

	PushState();
	BRegion clip(r);
	ConstrainClippingRegion(&clip);

	float low = 0, high = 0;
	for (size_t s = 0; s < seriesCount; s++)
		for (size_t c = 0; c < categories; c++) {
			low = std::min(low, chart.Value(s, c));
			high = std::max(high, chart.Value(s, c));
		}
	if (high <= low)
		high = low + 1;
	// a round step for about 5 grid lines
	float step = powf(10, floorf(log10f((high - low) / 5)));
	for (float m : {1.0f, 2.0f, 5.0f, 10.0f})
		if ((high - low) / (step * m) <= 6) {
			step *= m;
			break;
		}
	high = ceilf(high / step) * step;
	low = floorf(low / step) * step;

	BFont font(be_plain_font);
	font.SetSize(9);
	SetFont(&font);
	BRect area = plot.InsetByCopy(8, 8);
	area.left += 36;
	area.bottom -= labelFrames ? 4 : 16;
	bool bars = chart.chartType == Sig("BAR3") || chart.chartType == Sig("BAR2")
		|| (chart.chartType >> 8) == (Sig("BAR ") >> 8);
	if (hasLegend && legend.left > area.left + 40 && legend.left < area.right
		&& legend.top < area.bottom && legend.bottom > area.top)
		area.right = legend.left - 10;	// the 3D plot doesn't reach the legend
	float depth = chart.chartType == Sig("BAR3") ? 6 : 0;
	area.top += depth;
	area.right -= depth;
	auto yFor = [&](float v) {
		return area.bottom - (v - low) / (high - low) * area.Height();
	};

	SetDrawingMode(B_OP_OVER);
	SetPenSize(1);
	for (float v = low; v <= high + step / 2; v += step) {
		float y = yFor(v);
		SetHighColor(210, 210, 210);
		StrokeLine(BPoint(area.left, y), BPoint(area.right, y));
		if (depth > 0)
			StrokeLine(BPoint(area.left, y), BPoint(area.left + depth, y - depth));
		char label[32];
		snprintf(label, sizeof label, "%g", v);
		SetHighColor(0, 0, 0);
		DrawString(label, BPoint(area.left - 4 - font.StringWidth(label), y + 3));
	}
	SetHighColor(0, 0, 0);
	StrokeLine(area.LeftTop(), area.LeftBottom());
	StrokeLine(area.LeftBottom(), area.RightBottom());

	float slot = area.Width() / categories;
	for (size_t c = 0; c < categories; c++) {
		float x0 = area.left + c * slot;
		if (bars) {
			float gap = slot * 0.15f;
			float w = (slot - 2 * gap) / seriesCount;
			for (size_t s = 0; s < seriesCount; s++) {
				float v = chart.Value(s, c);
				BRect bar(x0 + gap + s * w, std::min(yFor(v), yFor(0)),
					x0 + gap + (s + 1) * w - 1, std::max(yFor(v), yFor(0)));
				rgb_color color = seriesColor(s);
				if (depth > 0) {
					// top and side faces, a little darker
					BPoint top[4] = {bar.LeftTop(), bar.RightTop(),
						bar.RightTop() + BPoint(depth, -depth), bar.LeftTop() + BPoint(depth, -depth)};
					BPoint side[4] = {bar.RightTop(), bar.RightBottom(),
						bar.RightBottom() + BPoint(depth, -depth), bar.RightTop() + BPoint(depth, -depth)};
					SetHighColor(tint_color(color, B_DARKEN_1_TINT));
					FillPolygon(top, 4);
					SetHighColor(tint_color(color, B_DARKEN_2_TINT));
					FillPolygon(side, 4);
				}
				SetHighColor(color);
				FillRect(bar);
				SetHighColor(tint_color(color, B_DARKEN_3_TINT));
				StrokeRect(bar);
			}
		}
		if (!labelFrames && c < chart.data.names1.size()) {
			const char* label = chart.data.names1[c].c_str();
			SetHighColor(0, 0, 0);
			DrawString(label, BPoint(x0 + (slot - font.StringWidth(label)) / 2,
				area.bottom + 12));
		}
	}
	if (!bars) {
		SetPenSize(2);
		for (size_t s = 0; s < seriesCount; s++) {
			SetHighColor(seriesColor(s));
			for (size_t c = 0; c < categories; c++) {
				BPoint p(area.left + (c + 0.5f) * slot, yFor(chart.Value(s, c)));
				if (c > 0)
					StrokeLine(BPoint(area.left + (c - 0.5f) * slot,
						yFor(chart.Value(s, c - 1))), p);
				FillRect(BRect(p.x - 2, p.y - 2, p.x + 2, p.y + 2));
			}
		}
		SetPenSize(1);
	}

	if (hasLegend) {
		TextStyle ts = ResolveTextStyle(fDocument.styles, legendStyle);
		BFont lf = TDocView::MapFont(ts);
		SetFont(&lf);
		float lineHeight = std::max(10.0f, legend.Height() / std::max<size_t>(1,
			chart.series.size()));
		for (size_t s = 0; s < chart.series.size(); s++) {
			float y = legend.top + s * lineHeight;
			SetHighColor(seriesColor(s));
			FillRect(BRect(legend.left + 2, y + lineHeight / 2 - 4, legend.left + 10,
				y + lineHeight / 2 + 4));
			SetHighColor(ts.color.r, ts.color.g, ts.color.b);
			DrawString(chart.series[s].name.c_str(),
				BPoint(legend.left + 14, y + lineHeight / 2 + lf.Size() * 0.35f));
		}
	}
	if (hasTitle && !titleText.empty()) {
		TextStyle ts = ResolveTextStyle(fDocument.styles, titleStyle);
		BFont tf = TDocView::MapFont(ts);
		SetFont(&tf);
		SetHighColor(ts.color.r, ts.color.g, ts.color.b);
		float w = tf.StringWidth(titleText.c_str());
		DrawString(titleText.c_str(), BPoint(title.left + (title.Width() - w) / 2,
			title.bottom - tf.Size() * 0.3f));
	}
	SetDrawingMode(B_OP_COPY);
	PopState();
}


// A graphics part's shapes, in its frame's coordinates (the same space its
// embedded frames use).
void
TPageView::DrawGraphics(const Document::PlacedFrame& f, float dx, float dy)
{
	const GRPart* gr = fDocument.GraphicsPart(f.part);
	SetDrawingMode(B_OP_ALPHA);
	SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
	for (size_t l = 0; l < gr->layers.size(); l++) {
		if (fDocument.LayerHidden(f.part, l))
			continue;
		for (const Shape& s : gr->layers[l].shapes) {
			if (s.kind == Sig("FRAM"))
				continue;	// another part's frame: drawn with that part
			ShapeStyle style = ResolveShapeStyle(fDocument.styles, s.style);
			BPoint o(f.x + dx + s.origin[0], f.y + dy + s.origin[1]);
			BRect r(o.x, o.y, o.x + s.size[0], o.y + s.size[1]);
			if (r.left > r.right)
				std::swap(r.left, r.right);
			if (r.top > r.bottom)
				std::swap(r.top, r.bottom);
			if (s.angle != 0) {
				PushState();
				SetTransform(FrameRotation(
					r.LeftTop() + BPoint(r.Width() / 2, r.Height() / 2), s.angle));
			}

			BShape* path = NULL;
			if (s.kind == Sig("BEZI")) {
				path = new BShape();
				for (const BezierPath& p : s.paths) {
					if (p.vertices.empty())
						continue;
					auto at = [&](const float* v) { return BPoint(o.x + v[0], o.y + v[1]); };
					path->MoveTo(at(p.vertices[0].point));
					size_t n = p.vertices.size();
					for (size_t i = 1; i < n + (p.closed ? 1 : 0); i++) {
						const Vertex& a = p.vertices[i - 1];
						const Vertex& b = p.vertices[i % n];
						BPoint c[3] = {at(a.control2), at(b.control1), at(b.point)};
						path->BezierTo(c);
					}
					if (p.closed)
						path->Close();
				}
			}

			BGradient* gradient;
			// opaque fills in copy mode: gradients in alpha mode come out
			// striped in offscreen bitmaps
			SetDrawingMode(style.fillOpacity >= 0.999f ? B_OP_COPY : B_OP_ALPHA);
			if (s.kind != Sig("LINE")
				&& SetInk(style.fill, style.fillOpacity, r, &gradient)) {
				auto fill = [&](auto&& draw) {
					if (gradient != NULL) {
						draw(gradient);
						delete gradient;
					} else
						draw((BGradient*)NULL);
				};
				fill([&](BGradient* g) {
					if (s.kind == Sig("RECT"))
						g ? FillRect(r, *g) : FillRect(r);
					else if (s.kind == Sig("RRCT"))
						g ? FillRoundRect(r, s.radius[0], s.radius[1], *g)
							: FillRoundRect(r, s.radius[0], s.radius[1]);
					else if (s.kind == Sig("ELPS"))
						g ? FillEllipse(r, *g) : FillEllipse(r);
					else if (s.kind == Sig("ARC "))
						g ? FillArc(r, s.arcAngles[0], s.arcAngles[1], *g)
							: FillArc(r, s.arcAngles[0], s.arcAngles[1]);
					else if (path != NULL)
						g ? FillShape(path, *g) : FillShape(path);
				});
			}
			SetDrawingMode(style.penOpacity >= 0.999f ? B_OP_COPY : B_OP_ALPHA);
			if (style.penSize > 0 && SetInk(style.pen, style.penOpacity, r, &gradient)) {
				delete gradient;	// pens: solid color only for now
				SetPenSize(style.penSize);
				if (s.kind == Sig("RECT"))
					StrokeRect(r);
				else if (s.kind == Sig("RRCT"))
					StrokeRoundRect(r, s.radius[0], s.radius[1]);
				else if (s.kind == Sig("ELPS"))
					StrokeEllipse(r);
				else if (s.kind == Sig("ARC "))
					StrokeArc(r, s.arcAngles[0], s.arcAngles[1]);
				else if (s.kind == Sig("LINE"))
					StrokeLine(o, o + BPoint(s.size[0], s.size[1]));
				else if (path != NULL)
					StrokeShape(path);
				SetPenSize(1);
			}
			delete path;
			if (s.angle != 0)
				PopState();
		}
	}
	SetDrawingMode(B_OP_COPY);
}


void
TPageView::Draw(BRect updateRect)
{
	// the zoom (TDocWindow::Scale): drawn larger or smaller; the update
	// rectangle in the document's coordinates (printing at 100%)
	float zoom = IsPrinting() ? 1 : fZoom;
	// (restored when done: Invalidate() and the children follow the scale)
	PushState();
	SetScale(zoom);
	updateRect.Set(updateRect.left / zoom, updateRect.top / zoom, updateRect.right / zoom,
		updateRect.bottom / zoom);
	BRect bounds = Bounds();
	bounds.Set(bounds.left / zoom, bounds.top / zoom, bounds.right / zoom, bounds.bottom / zoom);
	fDrawBounds = bounds;

	// printing (TPageFrame::Draw): the pages' contents only
	if (!IsPrinting()) {
		// (browse view: the text on white)
		if (fBrowse)
			SetHighColor(255, 255, 255);
		else
			SetHighColor(152, 152, 152);
		FillRect(updateRect);
	}

	float dx = fMargin - fBounds.left;
	float dy = fMargin - fBounds.top;

	// pages and other frames, outermost first
	std::vector<Document::PlacedFrame> order = fFrames;
	std::stable_sort(order.begin(), order.end(),
		[](const auto& a, const auto& b) { return a.depth < b.depth; });
	for (const auto& f : order) {
		uint32_t kind = fDocument.parts.parts[f.part].kind;
		BRect r(f.x + dx, f.y + dy, f.x + dx + f.width, f.y + dy + f.height);
		PushState();
		BRegion frameClip(ContainerClip(f).OffsetByCopy(dx, dy) & bounds);
		ConstrainClippingRegion(&frameClip);
		DrawFrame(f, r, kind, dx, dy);
		PopState();
	}
	DrawLines(updateRect, dx, dy);
	PopState();
	if (fRuler != NULL && !IsPrinting())
		fRuler->Update();
	if (fPartBar != NULL && !IsPrinting())
		fPartBar->Update();
	if (fFormatBar != NULL && !IsPrinting())
		fFormatBar->Update();
	if (fSpellBar != NULL && !IsPrinting())
		fSpellBar->Update();
	if (fPageBar != NULL && !IsPrinting()) {
		int32 first, last;
		VisiblePages(first, last);
		fPageBar->SetPageNumbers(first, last);
	}
}


void
TPageView::DrawFrame(const Document::PlacedFrame& f, BRect r, uint32_t kind, float dx,
	float dy)
{
	if (kind == Sig("BROT")) {
		// browse mode (no pages): the window's white background
		SetHighColor(255, 255, 255);
		FillRect(r);
		return;
	}
	if (kind == Sig("PROT"))
		return;
	if (kind == Sig("PAGE")) {
		if (IsPrinting())
			return;
		SetHighColor(96, 96, 96);
		FillRect(r.OffsetByCopy(3, 3));
		SetHighColor(255, 255, 255);
		FillRect(r);
		DrawPageGuides(f, r);
		return;
	}
	DrawFrameBackground(f, r);
	if ((kind == Sig("GRFX") || kind == Sig("PRES"))
		&& fDocument.GraphicsPart(f.part) != NULL) {
		DrawGraphics(f, dx, dy);
		return;
	}
	if (kind == Sig("SPRD") && fDocument.SpreadsheetPart(f.part) != NULL) {
		DrawSpreadsheet(f, r);
		return;
	}
	if (kind == Sig("BITS") && fDocument.ImagePart(f.part) != NULL) {
		DrawImage(f, r);
		return;
	}
	if (kind == Sig("TBLE") && fDocument.Table(f.part) != NULL) {
		DrawTable(f, r);
		return;
	}
	if (kind == Sig("chrt") && fDocument.Chart(f.part) != NULL) {
		DrawChart(f, r);
		return;
	}
	if (kind == Sig("CLOK")) {
		DrawClock(f, r);
		return;
	}
	if (kind == Sig("WORD"))
		return;
	// not ported yet: an outline (graphics parts are often backgrounds,
	// so they get no label)
	SetHighColor(200, 200, 210);
	SetPenSize(1);
	StrokeRect(r, B_MIXED_COLORS);
	if (kind == Sig("GRFX") || kind == Sig("PRES"))
		return;
	SetHighColor(150, 150, 170);
	SetFont(be_plain_font);
	SetFontSize(9);
	BString label;
	label << FourCCString(kind).c_str() << " part";
	DrawString(label.String(), BPoint(r.left + 3, r.top + 11));
}


void
TPageView::DrawLines(BRect updateRect, float dx, float dy)
{
	// the selection highlight goes under the text
	if (!IsPrinting())
		DrawSelection();
	SetDrawingMode(B_OP_OVER);
	for (const Line& line : fLines) {
		if (line.angle == 0 && !line.frame.Intersects(updateRect))
			continue;
		PushState();
		BRegion lineClip(line.clip.OffsetByCopy(dx, dy) & fDrawBounds);
		ConstrainClippingRegion(&lineClip);
		if (line.angle != 0)
			SetTransform(FrameRotation(line.center, line.angle));
		for (const Line::Piece& p : line.pieces) {
			SetFont(&p.font);
			SetHighColor(p.color);
			if (p.field == Sig("LINE")) {
				// half the ascent above the baseline
				float y = line.baseline - p.font.Size() * 0.95f / 2;
				StrokeLine(BPoint(line.frame.left + p.x, y),
					BPoint(line.frame.left + p.ruleEnd, y));
				continue;
			}
			DrawString(p.text.c_str(), BPoint(line.frame.left + p.x, line.baseline + p.dy));
		}
		if ((fDocument.documentFlags & 4) && !IsPrinting())
			DrawInvisibles(size_t(&line - &fLines[0]));
		PopState();
	}
	for (const Rule& rule : fRules) {
		PushState();
		BRegion clip(rule.clip.OffsetByCopy(dx, dy) & fDrawBounds);
		ConstrainClippingRegion(&clip);
		SetHighColor(0, 0, 0);
		SetPenSize(rule.width);
		StrokeLine(rule.from, rule.to);
		PopState();
	}
	SetDrawingMode(B_OP_COPY);
	if (!IsPrinting())
		DrawCaret();
}


// Show Invisibles (TLineRun::drawSpaceInvisibles, drawInvisible): in dark
// gray, a dot halfway across each space at the middle of the text's height
// (a small square for a non-breaking space), an arrow halfway across each
// tab, and at the end of a line the mark of what ends it: a return arrow
// for a paragraph (shorter for a line break), a down arrow for a frame or
// page break, a square for 0x05
void
TPageView::DrawInvisibles(size_t index)
{
	const Line& line = fLines[index];
	const WPPart* wp = fDocument.WordPart(line.part);
	if (wp == NULL || line.paragraph >= wp->paragraphs.size())
		return;
	const std::string& text = wp->paragraphs[line.paragraph].text;
	float size = line.pieces.empty() ? 12 : line.pieces[0].font.Size();
	float y = line.baseline, mid = line.baseline - 0.35f * size;
	SetHighColor(96, 96, 96);
	SetPenSize(1);
	size_t end = std::min(line.to, text.size());
	for (size_t o = line.from; o < end; o++) {
		unsigned char c = text[o];
		if (c == ' ') {
			float x = (OffsetX(index, o) + OffsetX(index, o + 1)) / 2;
			FillRect(BRect(floorf(x), floorf(mid), floorf(x), floorf(mid)));
		} else if (c == 0xc2 && o + 1 < end && (unsigned char)text[o + 1] == 0xa0) {
			float x = (OffsetX(index, o) + OffsetX(index, o + 2)) / 2;
			FillRect(BRect(x - 1, mid - 1, x + 1, mid + 1));
		} else if (c == '\t') {
			float m = (OffsetX(index, o) + OffsetX(index, o + 1)) / 2;
			StrokeLine(BPoint(m - 2, y - 3), BPoint(m + 2, y - 3));
			StrokeLine(BPoint(m, y - 5), BPoint(m + 2, y - 3));
			StrokeLine(BPoint(m, y - 1), BPoint(m + 2, y - 3));
		}
	}
	// what ends the line: the paragraph's terminator after its last line
	// (the lines' ranges stop before it), or a line break in it
	size_t at;
	if (end + 1 == text.size() && end < text.size())
		at = end;
	else if (end > 0 && text[end - 1] == 0x01)
		at = end - 1;
	else
		return;
	unsigned char last = text[at];
	float x = OffsetX(index, at);
	switch (last) {
		case 0x01:
		case '\n':
			StrokeLine(BPoint(x, y - 2), BPoint(x + 2, y - 4));
			StrokeLine(BPoint(x, y - 2), BPoint(x + 2, y));
			StrokeLine(BPoint(x, y - 2), BPoint(x + 4, y - 2));
			StrokeLine(BPoint(x + 4, y - (last == 0x01 ? 4 : 6)), BPoint(x + 4, y - 2));
			break;
		case 0x02:
		case 0x0c:
			StrokeLine(BPoint(x, y - 2), BPoint(x + 2, y));
			StrokeLine(BPoint(x + 4, y - 2), BPoint(x + 2, y));
			StrokeLine(BPoint(x + 2, y - (last == 0x02 ? 4 : 6)), BPoint(x + 2, y));
			break;
		case 0x05:
			StrokeRect(BRect(x, y - 4, x + 4, y));
			break;
	}
}


// TPageFrame::Draw with "Show Page Guides": gray lines round the printable
// area, under the header and over the footer, and round each text column
// (unless a single column fills the page)
void
TPageView::DrawPageGuides(const Document::PlacedFrame& f, BRect r)
{
	if (fDocument.sheets.sheets.empty() || !fDocument.sheets.sheets[0].flags[1])
		return;
	const LeafSheet& sheet = fDocument.sheets.sheets[0];
	float left = sheet.printable[0] - sheet.paper[0], top = sheet.printable[1] - sheet.paper[1];
	BRect page(r.left + left, r.top + top,
		r.left + sheet.printable[2] - sheet.paper[0], r.top + sheet.printable[3] - sheet.paper[1]);
	SetHighColor(192, 192, 192);
	SetPenSize(1);
	StrokeRect(page.InsetByCopy(-1, -1));
	const gobe::Frame& frame = *f.frame;
	auto childFrame = [&](size_t i) -> const gobe::Frame* {
		if (i >= frame.children.size())
			return NULL;
		const gobe::Frame::Pointer& c = frame.children[i];
		if (c.part == kNoPart || c.part >= fDocument.tails.size()
				|| c.portal >= fDocument.tails[c.part].portals.size()
				|| c.frame >= fDocument.tails[c.part].portals[c.portal].frames.size())
			return NULL;
		return &fDocument.tails[c.part].portals[c.portal].frames[c.frame];
	};
	if (const gobe::Frame* header = childFrame(0)) {
		float y = page.top + 1 + header->extent[1];
		StrokeLine(BPoint(page.left, y), BPoint(page.right, y));
	}
	if (const gobe::Frame* footer = childFrame(1)) {
		float y = page.bottom - 1 - footer->extent[1];
		StrokeLine(BPoint(page.left, y), BPoint(page.right, y));
	}
	size_t columns = frame.children.size() > 3 ? frame.children.size() - 3 : 0;
	for (size_t i = 0; i < columns; i++) {
		const gobe::Frame* column = childFrame(3 + i);
		if (column == NULL)
			continue;
		BRect c(r.left + column->origin[0], r.top + column->origin[1],
			r.left + column->origin[0] + column->extent[0],
			r.top + column->origin[1] + column->extent[1]);
		if (columns == 1 && c == page)
			break;
		StrokeRect(c.InsetByCopy(-1, -1));
	}
}


void
TPageView::GeometryChanged()
{
	RebuildFrames();
	LayoutText();
	InvalidateLayout();
	Invalidate();
	if (Window() != NULL)
		Window()->PostMessage(kMsgDocumentChanged);
}


BRect
TPageView::PageArea(int32 index, BRect printable) const
{
	int page = fDocument.PagePart();
	for (const auto& f : fFrames) {
		if (int(f.part) != page || f.index != index)
			continue;
		float x = f.x - fBounds.left + fMargin + printable.left;
		float y = f.y - fBounds.top + fMargin + printable.top;
		return BRect(x, y, x + printable.Width(), y + printable.Height());
	}
	return BRect();
}

} // namespace gobe
