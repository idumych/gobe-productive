#include "TPageView.h"

#include <math.h>
#include <stdio.h>
#include <time.h>

#include <algorithm>

#include <AffineTransform.h>
#include <GradientLinear.h>
#include <GradientRadial.h>
#include <Region.h>
#include <Window.h>
#include <Shape.h>

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
	fDocument(document),
	fBook(new SSBook(document)),
	fMargin(kDesk)
{
	SetViewColor(B_TRANSPARENT_COLOR);
	Rebuild();
}


// the visible frames, the bounds and the text layout (again after a slide
// change)
void
TPageView::Rebuild()
{
	fFrames.clear();
	fLines.clear();
	for (const auto& f : fDocument.PlacedFrames())
		if (!fDocument.FrameHidden(f.part, f.index))
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
	LayoutText();
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
	return BSize(fBounds.Width() + 2 * fMargin, fBounds.Height() + 2 * fMargin);
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
	// group each word processing part's frames, in flow order
	std::vector<std::pair<size_t, std::vector<FrameGeometry>>> flows;
	for (const auto& f : fFrames) {
		if (fDocument.parts.parts[f.part].kind != Sig("WORD"))
			continue;
		BRect r(f.x - fBounds.left + fMargin, f.y - fBounds.top + fMargin, 0, 0);
		r.right = r.left + f.width;
		r.bottom = r.top + f.height;
		auto it = std::find_if(flows.begin(), flows.end(),
			[&](const auto& fl) { return fl.first == f.part; });
		FrameGeometry g{r, f.frame->angle, ContainerClip(f)};
		if (it == flows.end())
			flows.push_back({f.part, {g}});
		else
			it->second.push_back(g);
	}
	for (const auto& [part, frames] : flows)
		LayoutPart(part, frames);
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
void
TPageView::LayoutPart(size_t part, const std::vector<FrameGeometry>& geometry)
{
	const WPPart* wp = fDocument.WordPart(part);
	if (wp == NULL || geometry.empty())
		return;
	std::vector<BRect> frames;
	for (const FrameGeometry& g : geometry)
		frames.push_back(g.rect);

	size_t frameIndex = 0;
	float y = frames[0].top;
	const float pad = 2;
	const float tabStop = 36;

	struct Token {
		enum Kind { kWord, kTab, kLineBreak } kind;
		std::string text;
		BFont font;
		rgb_color color;
		bool space;			// a space follows
		size_t start, end;	// bytes of the paragraph's text it shows
	};

	for (const WPParagraph& par : wp->paragraphs) {
		if (frameIndex >= frames.size())
			return;
		TextStyle parStyle = ResolveTextStyle(fDocument.styles, par.parStyle);

		std::string text = par.text;
		char terminator = 0;
		if (!text.empty() && (text.back() == '\n' || text.back() == '\r'
				|| IsFrameBreak(text.back()))) {
			terminator = text.back();
			text.pop_back();
		}

		std::vector<Token> tokens;
		BFont lineFont = TDocView::MapFont(parStyle);
		for (size_t r = 0; r < std::max<size_t>(1, par.runs.size()); r++) {
			size_t start = par.runs.empty() ? 0 : par.runs[r].position;
			size_t end = r + 1 < par.runs.size() ? par.runs[r + 1].position : text.size();
			TextStyle st = par.runs.empty() ? parStyle
				: ResolveTextStyle(fDocument.styles, par.runs[r].style);
			BFont font = TDocView::MapFont(st);
			if (r == 0)
				lineFont = font;	// empty lines take the first run's font
			if (start >= text.size())
				break;
			end = std::min(end, text.size());
			rgb_color color = make_color(st.color.r, st.color.g, st.color.b);
			std::string word;
			size_t wordStart = start;
			size_t i = start;
			auto flush = [&](bool space) {
				if (!word.empty() || space) {
					tokens.push_back({Token::kWord, word, font, color, space, wordStart, i});
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
					tokens.push_back({Token::kTab, "", font, color, false, i, i + 1});
				} else if (c == 0x01) {
					flush(false);
					tokens.push_back({Token::kLineBreak, "", font, color, false, i, i + 1});
				} else if ((unsigned char)c >= 0x20) {
					if (word.empty())
						wordStart = i;
					word += c;
				}
			}
			flush(false);
		}

		y += parStyle.spaceBefore;
		size_t t = 0;
		bool firstLine = true;
		size_t paragraphIndex = &par - &wp->paragraphs[0];
		size_t firstLineOfParagraph = fLines.size();
		do {
			if (frameIndex >= frames.size())
				return;
			const BRect& frame = frames[frameIndex];
			float left = frame.left + pad + parStyle.leftIndent
				+ (firstLine ? parStyle.firstIndent : 0);
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
							return w + tk.font.StringWidth(tk.text.substr(0, at).c_str());
					}
					w += tk.font.StringWidth(tk.text.c_str())
						+ (tk.space ? tk.font.StringWidth(" ") : 0);
				}
				return w;
			};
			float ascent = 0, descent = 0, leading = 0;
			// Line metrics as with the BeOS Bitstream fonts Gobe documents
			// were laid out with (about 1.2 x size). Haiku's Noto fonts are
			// taller (~1.36), which would push text out of its frames.
			auto measure = [&](const BFont& f) {
				float size = f.Size();
				ascent = std::max(ascent, size * 0.95f);
				descent = std::max(descent, size * 0.25f);
			};
			bool forced = false;
			while (t < tokens.size()) {
				const Token& tk = tokens[t];
				if (tk.kind == Token::kLineBreak) {
					measure(tk.font);
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
					float target;
					if (stop != NULL) {
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
							float cw = std::max(1.0f, tk.font.StringWidth(c));
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
				float ww = tk.font.StringWidth(tk.text.c_str());
				// the padding is the port's, not Gobe's: words may use it; a
				// word at a tab stop inside the frame stays on the line
				bool atTab = t > 0 && tokens[t - 1].kind == Token::kTab && x <= avail;
				if (!placed.empty() && !atTab && x + ww > avail + 2 * pad)
					break;
				placed.push_back({t, x});
				x += ww + (tk.space ? tk.font.StringWidth(" ") : 0);
				measure(tk.font);
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
			float height = ascent + descent + leading;
			if (y + height > frame.bottom && y > frame.top) {
				// frame full: continue in the next frame
				frameIndex++;
				if (frameIndex >= frames.size())
					return;
				y = frames[frameIndex].top;
				if (!placed.empty())
					t = placed.front().first;
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
			for (size_t k = 0; k < placed.size(); k++) {
				const Token& tk = tokens[placed[k].first];
				line.pieces.push_back({tk.text, tk.font, tk.color,
					shift + placed[k].second + extra * k, tk.start, tk.end});
			}
			for (Line::Piece& leader : leaders) {
				leader.x += shift;
				line.pieces.push_back(std::move(leader));
			}
			fLines.push_back(std::move(line));
			y += height;
			firstLine = false;
		} while (t < tokens.size());
		y += parStyle.spaceAfter;
		// each line runs to where the next one starts
		for (size_t k = firstLineOfParagraph; k < fLines.size(); k++) {
			if (k + 1 < fLines.size())
				fLines[k].to = std::max(fLines[k].from, fLines[k + 1].from);
			else
				fLines[k].to = text.size();
			if (k == firstLineOfParagraph)
				fLines[k].from = 0;
		}

		if (IsFrameBreak(terminator)) {
			frameIndex++;
			if (frameIndex < frames.size())
				y = frames[frameIndex].top;
		}
	}
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
	const gobe::Frame& frame = *f.frame;

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
		if (value.type == SSValue::kNone)
			continue;
		const CellStyle& style = SpreadsheetStyle(stackFor(row, column, &cell));
		bool red;
		std::string text = FormatCellValue(value, style.format, &red);
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
	SetHighColor(152, 152, 152);
	FillRect(updateRect);

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
		BRegion frameClip(ContainerClip(f).OffsetByCopy(dx, dy) & Bounds());
		ConstrainClippingRegion(&frameClip);
		DrawFrame(f, r, kind, dx, dy);
		PopState();
	}
	DrawLines(updateRect, dx, dy);
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
		SetHighColor(96, 96, 96);
		FillRect(r.OffsetByCopy(3, 3));
		SetHighColor(255, 255, 255);
		FillRect(r);
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
	DrawSelection();
	SetDrawingMode(B_OP_OVER);
	for (const Line& line : fLines) {
		if (line.angle == 0 && !line.frame.Intersects(updateRect))
			continue;
		PushState();
		BRegion lineClip(line.clip.OffsetByCopy(dx, dy) & Bounds());
		ConstrainClippingRegion(&lineClip);
		if (line.angle != 0)
			SetTransform(FrameRotation(line.center, line.angle));
		for (const Line::Piece& p : line.pieces) {
			SetFont(&p.font);
			SetHighColor(p.color);
			DrawString(p.text.c_str(), BPoint(line.frame.left + p.x, line.baseline));
		}
		PopState();
	}
	SetDrawingMode(B_OP_COPY);
	DrawCaret();
}

} // namespace gobe
