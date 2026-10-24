// Pages of a paged document: the PAGE part's frames (TPagePortal), one per
// page, each listing the frames on it (TPageFrame: header, footer, the
// sheet's drawing, the text columns).
#include "Document.h"

#include "DocWriter.h"
#include "Editing.h"
#include "WPPart.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace gobe {

namespace {

// TPagedRootPart +0x6c: pages across (TPagedRootPart::Write: the sheet,
// the page part, a value, pages across, true after the part header)
uint32_t
PagesAcross(const Document& doc)
{
	int root = doc.RootPart();
	if (root < 0 || doc.parts.parts[root].kind != Sig("PROT"))
		return 1;
	try {
		DocArchive a(doc.parts.parts[root].data);
		a.CheckSig(Sig("PART"));
		a.UInt32();
		PartHeader header;
		ReadPartHeader(a, header);
		a.UInt32();		// the sheet
		a.UInt32();		// the page part
		a.UInt32();
		uint32_t across = a.UInt32();
		return across > 0 ? across : 1;
	} catch (const ArchiveError&) {
		return 1;
	}
}

// TPagePortal::Write: the main part and its portal, the drawing part and
// its portal, then the header and footer frames (part, portal, frame)
const size_t kHeaderFrameAt = 16;


void
PutUInt32(std::vector<uint8_t>& raw, size_t at, uint32_t v)
{
	for (int i = 0; i < 4; i++)
		raw[at + i] = uint8_t(v >> (24 - 8 * i));
}


void
PutPointer(std::vector<uint8_t>& raw, size_t at, const Frame::Pointer& p)
{
	PutUInt32(raw, at, p.part);
	PutUInt32(raw, at + 4, p.portal);
	PutUInt32(raw, at + 8, p.frame);
}


uint32_t
NamedStyle(const StyleMgr& styles, uint32_t type, const std::string& name)
{
	for (size_t i = 0; i < styles.styles.size(); i++)
		if (styles.styles[i].value10 == type && styles.styles[i].name == name)
			return uint32_t(i);
	return kNoPart;
}

} // namespace


int
Document::PagePart() const
{
	int root = RootPart();
	if (root < 0 || parts.parts[root].kind != Sig("PROT"))
		return -1;
	for (size_t i = 0; i < parts.parts.size(); i++)
		if (parts.parts[i].kind == Sig("PAGE") && tailRead[i] && !tails[i].portals.empty()
				&& tails[i].portals[0].part == uint32_t(root))
			return int(i);
	return -1;
}


size_t
Document::PageCount() const
{
	int page = PagePart();
	return page < 0 ? 0 : tails[page].portals[0].frames.size();
}


bool
Document::AddPage()
{
	int pagePart = PagePart();
	if (pagePart < 0 || sheets.sheets.empty())
		return false;
	Portal& pages = tails[pagePart].portals[0];
	if (pages.frames.empty())
		return false;
	const LeafSheet& sheet = sheets.sheets[0];
	size_t index = pages.frames.size();
	uint32_t across = PagesAcross(*this);
	// TPagedRootPart::GetPaperPosition
	float width = sheet.paper[2] - sheet.paper[0];
	float height = sheet.paper[3] - sheet.paper[1];
	Frame page = pages.frames.back();
	page.origin[0] = std::nearbyint((width + 20) * float(index % across));
	page.origin[1] = std::nearbyint((height + 20) * float(index / across));
	page.extent[0] = width;
	page.extent[1] = height;
	uint32_t textLength = 0;
	for (size_t i = 0; i < page.children.size(); i++) {
		Frame::Pointer& child = page.children[i];
		// the header and footer: the page portal's frames, on every page
		// (TPageFrame::TPageFrame)
		if (i < 2 || child.part == kNoPart || child.part >= tails.size() || !tailRead[child.part]
				|| child.portal >= tails[child.part].portals.size())
			continue;
		Portal& portal = tails[child.part].portals[child.portal];
		if (child.frame >= portal.frames.size())
			continue;
		Frame frame = portal.frames[child.frame];
		uint32_t kind = parts.parts[child.part].kind;
		if (kind == Sig("GRFX")) {
			// TPageFrame::GraphicsDataRect: the drawing's rectangle for
			// this page
			float w = frame.sheetRect[2] - frame.sheetRect[0];
			float h = frame.sheetRect[3] - frame.sheetRect[1];
			frame.sheetRect[0] = std::nearbyint(float(index % across) * w);
			frame.sheetRect[1] = std::nearbyint(float(index / across) * h);
			frame.sheetRect[2] = frame.sheetRect[0] + w;
			frame.sheetRect[3] = frame.sheetRect[1] + h;
		} else if (int(child.part) == MainPart()) {
			// a new text column: no text yet, from the end
			if (textLength == 0)
				if (const WPPart* wp = WordPart(child.part))
					for (const WPParagraph& p : wp->paragraphs)
						textLength += uint32_t(p.text.size());
			frame.textStart = textLength;
			frame.textLength = 0;
		}
		portal.frames.push_back(frame);
		portal.values[2] = uint32_t(portal.frames.size());	// the frame count
		child.frame = uint32_t(portal.frames.size() - 1);
		dirtyTails.insert(child.part);
	}
	pages.frames.push_back(page);
	pages.values[2] = uint32_t(pages.frames.size());
	dirtyTails.insert(size_t(pagePart));
	ComputeHiddenFrames();
	return true;
}


bool
Document::RemoveLastPage()
{
	int pagePart = PagePart();
	if (pagePart < 0)
		return false;
	Portal& pages = tails[pagePart].portals[0];
	if (pages.frames.size() < 2)
		return false;
	const Frame& page = pages.frames.back();
	// last first: a page's text columns follow each other in their
	// portal; the header and footer stay (every page shows them)
	for (size_t i = page.children.size(); i-- > 2; ) {
		const Frame::Pointer& child = page.children[i];
		if (child.part == kNoPart || child.part >= tails.size() || !tailRead[child.part]
				|| child.portal >= tails[child.part].portals.size())
			continue;
		Portal& portal = tails[child.part].portals[child.portal];
		// the page's frames are the last of their portals (pages are added
		// at the end)
		if (child.frame + 1 != portal.frames.size())
			continue;
		portal.frames.pop_back();
		portal.values[2] = uint32_t(portal.frames.size());
		dirtyTails.insert(child.part);
	}
	pages.frames.pop_back();
	pages.values[2] = uint32_t(pages.frames.size());
	dirtyTails.insert(size_t(pagePart));
	ComputeHiddenFrames();
	return true;
}


void
Document::SetFrameText(size_t part, size_t frame, uint32_t start, uint32_t length)
{
	if (part >= tails.size() || !tailRead[part])
		return;
	size_t n = 0;
	for (Portal& portal : tails[part].portals)
		for (Frame& f : portal.frames)
			if (n++ == frame && (f.textStart != start || f.textLength != length)) {
				f.textStart = start;
				f.textLength = length;
				dirtyTails.insert(part);
			}
	layoutRanges.insert(part);
}

void
Document::Margins(float margins[4]) const
{
	margins[0] = margins[1] = margins[2] = margins[3] = 0;
	if (sheets.sheets.empty())
		return;
	const LeafSheet& s = sheets.sheets[0];
	margins[0] = s.printable[0] - s.paper[0];
	margins[1] = s.printable[1] - s.paper[1];
	margins[2] = s.paper[2] - s.printable[2];
	margins[3] = s.paper[3] - s.printable[3];
}


bool
Document::SetPageGeometry(const float paper[4], const float printable[4])
{
	if (sheets.sheets.empty())
		return false;
	LeafSheet& sheet = sheets.sheets[0];
	if (std::equal(paper, paper + 4, sheet.paper)
			&& std::equal(printable, printable + 4, sheet.printable))
		return false;
	std::copy(paper, paper + 4, sheet.paper);
	std::copy(printable, printable + 4, sheet.printable);
	sheetsChanged = true;
	RepositionPages();
	return true;
}


void
Document::RepositionPages()
{
	int pagePart = PagePart();
	if (pagePart < 0 || sheets.sheets.empty())
		return;
	const float* paper = sheets.sheets[0].paper;
	const float* printable = sheets.sheets[0].printable;
	uint32_t across = PagesAcross(*this);
	float width = paper[2] - paper[0];
	float height = paper[3] - paper[1];
	// TPageFrame::PageRect: the printable rectangle, on the page
	float left = printable[0] - paper[0], top = printable[1] - paper[1];
	float right = printable[2] - paper[0], bottom = printable[3] - paper[1];
	Portal& pages = tails[pagePart].portals[0];
	int mainPart = MainPart();
	// the main part's kind decides its frames' rectangles (its portal's
	// ColumnRect and GraphicsRect): text columns between header and footer,
	// a drawing over the whole printable area; for spreadsheets (frames that
	// follow the cells, the drawing a point inside) they stay as they are
	// (not ported yet)
	uint32_t mainKind = mainPart >= 0 ? parts.parts[mainPart].kind : 0;
	for (size_t k = 0; k < pages.frames.size(); k++) {
		Frame& page = pages.frames[k];
		page.origin[0] = std::nearbyint((width + 20) * float(k % across));
		page.origin[1] = std::nearbyint((height + 20) * float(k / across));
		page.extent[0] = width;
		page.extent[1] = height;
		auto child = [&](size_t i) -> Frame* {
			if (i >= page.children.size())
				return nullptr;
			const Frame::Pointer& c = page.children[i];
			if (c.part == kNoPart || c.part >= tails.size() || !tailRead[c.part]
					|| c.portal >= tails[c.part].portals.size()
					|| c.frame >= tails[c.part].portals[c.portal].frames.size())
				return nullptr;
			dirtyTails.insert(c.part);
			return &tails[c.part].portals[c.portal].frames[c.frame];
		};
		// the header at the top, the footer at the bottom (their heights
		// stay); TPageFrame::ChildHeight: 1 + the height
		float textTop = top, textBottom = bottom;
		if (Frame* header = child(0)) {
			header->origin[0] = left;
			header->origin[1] = top;
			header->extent[0] = right - left;
			textTop += 1 + header->extent[1];
		}
		if (Frame* footer = child(1)) {
			footer->origin[0] = left;
			footer->origin[1] = bottom - footer->extent[1];
			footer->extent[0] = right - left;
			textBottom -= 1 + footer->extent[1];
		}
		// the sheet's drawing over the printable area, showing its page k
		Frame* drawing = mainKind == Sig("WORD") ? child(2) : nullptr;
		if (drawing != nullptr) {
			drawing->origin[0] = left;
			drawing->origin[1] = top;
			drawing->extent[0] = right - left;
			drawing->extent[1] = bottom - top;
			float w = right - left, h = bottom - top;
			drawing->sheetRect[0] = std::nearbyint(float(k % across) * w);
			drawing->sheetRect[1] = std::nearbyint(float(k / across) * h);
			drawing->sheetRect[2] = drawing->sheetRect[0] + w;
			drawing->sheetRect[3] = drawing->sheetRect[1] + h;
		}
		// the text columns: TWPPortal's column rectangles (the section's
		// columns as fractions of the area between header and footer)
		size_t columns = page.children.size() > 3 ? page.children.size() - 3 : 0;
		if (mainKind == Sig("GRFX") && columns == 1) {
			if (Frame* main = child(3)) {
				float w = right - left, h = bottom - top;
				main->origin[0] = left;
				main->origin[1] = top;
				main->extent[0] = w;
				main->extent[1] = h;
				main->sheetRect[0] = std::nearbyint(float(k % across) * w);
				main->sheetRect[1] = std::nearbyint(float(k / across) * h);
				main->sheetRect[2] = main->sheetRect[0] + w;
				main->sheetRect[3] = main->sheetRect[1] + h;
			}
			columns = 0;
		} else if (mainKind != Sig("WORD"))
			columns = 0;
		const WPPart* wp = mainPart >= 0 ? WordPart(size_t(mainPart)) : nullptr;
		const WPSection* section = wp != nullptr && !wp->sections.empty()
			? &wp->sections[0] : nullptr;
		for (size_t i = 0; i < columns; i++) {
			Frame* column = child(3 + i);
			if (column == nullptr)
				continue;
			float f[4] = {float(i) / float(columns), 0, float(i + 1) / float(columns), 1};
			if (section != nullptr && section->columns.size() == columns)
				std::copy(section->columns[i].begin(), section->columns[i].end(), f);
			float w = right - left, h = textBottom - textTop;
			column->origin[0] = left + f[0] * w;
			column->origin[1] = textTop + f[1] * h;
			column->extent[0] = (f[2] - f[0]) * w;
			column->extent[1] = (f[3] - f[1]) * h;
		}
	}
	dirtyTails.insert(size_t(pagePart));
	// the text is laid out again in the new frames
	if (mainPart >= 0 && WordPart(size_t(mainPart)) != nullptr)
		dirtyParts.insert(size_t(mainPart));
	ComputeHiddenFrames();
}



int
Document::HeaderPart(int which) const
{
	if (sheets.sheets.empty() || which < 0 || which > 1)
		return -1;
	uint32_t part = which == 0 ? sheets.sheets[0].headerPart : sheets.sheets[0].footerPart;
	return part < parts.parts.size() ? int(part) : -1;
}


bool
Document::InsertHeaderFooter(int which)
{
	int pagePart = PagePart();
	if (pagePart < 0 || which < 0 || which > 1 || HeaderPart(which) >= 0)
		return false;
	Portal& pages = tails[pagePart].portals[0];
	if (pages.frames.empty() || pages.raw.size() < kHeaderFrameAt + 24)
		return false;
	bool wordFactory = false;
	for (const PartFactoryEntry& f : factories)
		wordFactory = wordFactory || f.kind == Sig("WORD");
	if (!wordFactory)
		return false;	// (a document without text parts: not ported)

	// the styles: "Header"/"Footer" (TWPPartFactory::InitStyles: based on
	// "WP Default", nothing of its own; documents keep it only once used),
	// the paragraph's style based on it, the run's with no parent
	auto newStyle = [&](uint32_t type, const char* name, uint32_t flags, uint32_t parent) {
		Style s;
		s.id = Sig("STYL");
		s.value4 = 1;		// the manager
		s.value8 = flags;
		s.valueC = 0xffff;
		s.value10 = type;
		s.name = name;
		s.links[0] = parent;
		s.links[1] = s.links[2] = kNoPart;
		if (parent < styles.styles.size())
			styles.styles[parent].value4++;
		s.props2 = StyleUnionSet(styles, s);
		styles.styles.push_back(s);
		return uint32_t(styles.styles.size() - 1);
	};
	const char* name = which == 0 ? "Header" : "Footer";
	uint32_t named = NamedStyle(styles, Sig("Para"), name);
	if (named == kNoPart) {
		uint32_t wpDefault = NamedStyle(styles, Sig("Para"), "WP Default");
		if (wpDefault == kNoPart)
			return false;
		named = newStyle(Sig("Para"), name, 0x822, wpDefault);
	}
	uint32_t paragraphStyle = newStyle(Sig("Para"), "", 0x20, named);
	uint32_t runStyle = newStyle(Sig("Basi"), "", 0x20, kNoPart);
	styles.styles[paragraphStyle].value4++;
	styles.styles[runStyle].value4++;
	// the frame in the main text frames' style
	uint32_t frameStyle = kNoPart;
	for (const Frame::Pointer& c : pages.frames[0].children)
		if (int(c.part) == MainPart() && c.portal < tails[c.part].portals.size()
				&& c.frame < tails[c.part].portals[c.portal].frames.size()) {
			frameStyle = tails[c.part].portals[c.portal].frames[c.frame].style;
			break;
		}
	if (frameStyle >= styles.styles.size())
		return false;
	styles.styles[frameStyle].value4++;
	stylesChanged = true;

	// its selection (TWPPart: no text selected)
	uint32_t selection = uint32_t(selections.selections.size());
	size_t part = parts.parts.size();
	DocWriter sel;
	sel.UInt32(18);
	sel.Sig(Sig("WORD"));
	sel.UInt32(uint32_t(part));
	sel.UInt32(selection);
	sel.UInt32(0);
	sel.Bool(false);
	sel.Bool(false);
	addedSelections.push_back(sel.Data());
	Selection s;
	s.id = selection;
	s.kind = Sig("WORD");
	s.part = uint32_t(part);
	s.value = selection;
	selections.selections.push_back(s);

	// the part (TWPPart::TWPPart: one empty paragraph, one section; +0x98
	// is 3 for a header or footer, as the command sets it), its portal in
	// the page part and the one frame every page shows (TPagePortal: at
	// the top of the printable area, as wide, no height until its text is
	// laid out; flag 8: it grows with its text)
	const LeafSheet& sheet = sheets.sheets[0];
	DocWriter w;
	w.Sig(Sig("PART"));
	w.Sig(Sig("WORD"));
	w.UInt32(0);
	w.UInt32(2);
	w.UInt32(parts.value14++);	// TPartMgr::NewPartID
	w.UInt32(0xffffffff);
	w.UInt32(1);
	w.UInt32(selection);
	w.Sig(Sig("wp++"));
	w.UInt16(3);
	w.UInt16(1);
	w.UInt32(0);
	w.UInt32(0xffffffff);
	w.UInt32(0);
	w.UInt32(1);
	w.Sig(Sig("par+"));
	w.UInt32(1);
	w.UInt32(0);
	w.UInt32(0);
	w.UInt16(0);
	w.UInt32(paragraphStyle);
	w.UInt32(1);
	w.UInt32(0);
	w.UInt32(runStyle);
	w.UInt32(0);
	w.Sig(Sig("par-"));
	w.Sig(Sig("txt+"));
	w.Sig(Sig("txt-"));
	w.Sig(Sig("sct+"));
	w.UInt32(1);
	w.UInt32(0);
	w.UInt16(0);
	w.UInt16(1);
	w.Bool(false);
	w.UInt32(1);
	const float whole[4] = {0, 0, 1, 1};
	w.Rect(whole);
	w.Sig(Sig("sct-"));
	w.Sig(Sig("wp--"));
	w.Sig(Sig("REFT"));
	w.UInt32(1);
	w.UInt32(0);
	w.Sig(Sig("RefT"));
	w.UInt32(1);
	w.Sig(Sig("PORT"));
	w.UInt32(uint32_t(pagePart));
	w.UInt32(0);
	w.UInt32(0);
	w.UInt32(1);
	w.UInt32(1);
	w.Sig(Sig("FRAM"));
	w.Float(0);
	const float origin[2] = {sheet.printable[0] - sheet.paper[0],
		sheet.printable[1] - sheet.paper[1]};
	w.Point(origin);
	w.Float(sheet.printable[2] - sheet.printable[0]);
	w.Float(0);
	w.UInt32(frameStyle);
	w.UInt32(0);
	w.UInt32(0xffffffff);
	w.UInt32(0xffffffff);
	w.UInt32(uint32_t(part));
	w.UInt32(uint32_t(pagePart));
	w.UInt32(0);
	w.UInt32(0);
	w.UInt32(0);
	w.UInt32(8);
	w.UInt32(0);			// TWPFrame: no text yet
	w.UInt32(0);
	for (int i = 0; i < 4; i++)
		w.Float(0);
	w.Sig(Sig("FraM"));
	w.Float(0);				// TWPPortal
	w.Sig(Sig("PorT"));
	w.Sig(Sig("ParT"));

	DocArchive versions(bytes);
	DocFile file;
	std::string error;
	if (!ReadDocFile(versions, file, error))
		return false;
	PartRecord record{Sig("WORD"), 0, w.Data()};
	DocArchive a(record.data);
	a.CopyVersions(versions);
	WPPart wp;
	try {
		ReadWPPart(a, wp);
	} catch (const ArchiveError&) {
		return false;
	}
	PartTail tail;
	if (!ReadPartTail(record.data, record.kind, versions, tail))
		return false;
	parts.parts.push_back(std::move(record));
	wordParts.emplace_back(part, std::move(wp));
	tails.push_back(std::move(tail));
	tailRead.push_back(true);

	Frame::Pointer frame{uint32_t(part), 0, 0};
	for (Frame& page : pages.frames)
		if (page.children.size() > size_t(which))
			page.children[which] = frame;
	PutPointer(pages.raw, kHeaderFrameAt + 12 * which, frame);
	(which == 0 ? sheets.sheets[0].headerPart : sheets.sheets[0].footerPart) = uint32_t(part);
	sheetsChanged = true;
	dirtyParts.insert(part);
	dirtyTails.insert(part);
	dirtyTails.insert(size_t(pagePart));
	RepositionPages();
	return true;
}


bool
Document::RemoveHeaderFooter(int which)
{
	int pagePart = PagePart();
	int part = HeaderPart(which);
	if (pagePart < 0 || part < 0)
		return false;
	Portal& pages = tails[pagePart].portals[0];
	for (Frame& page : pages.frames)
		if (page.children.size() > size_t(which))
			page.children[which] = Frame::Pointer();
	if (pages.raw.size() >= kHeaderFrameAt + 24)
		PutPointer(pages.raw, kHeaderFrameAt + 12 * which, Frame::Pointer());
	(which == 0 ? sheets.sheets[0].headerPart : sheets.sheets[0].footerPart) = kNoPart;
	sheetsChanged = true;
	// its portal goes with the pages it was in (TDocWindow::NewRootFrame),
	// and its frame's hold on its style
	std::vector<Portal>& portals = tails[part].portals;
	for (auto it = portals.begin(); it != portals.end(); ) {
		if (it->part != uint32_t(pagePart)) {
			++it;
			continue;
		}
		for (const Frame& f : it->frames)
			if (f.style < styles.styles.size() && styles.styles[f.style].value4 > 1) {
				styles.styles[f.style].value4--;
				stylesChanged = true;
			}
		it = portals.erase(it);
	}
	dirtyTails.insert(size_t(part));
	dirtyTails.insert(size_t(pagePart));
	RepositionPages();
	return true;
}


bool
Document::SetHeaderHeight(int which, float height)
{
	int pagePart = PagePart();
	int part = HeaderPart(which);
	if (pagePart < 0 || part < 0 || !tailRead[part])
		return false;
	for (Portal& portal : tails[part].portals) {
		if (portal.part != uint32_t(pagePart) || portal.frames.empty())
			continue;
		Frame& frame = portal.frames[0];
		if (std::fabs(frame.extent[1] - height) < 0.01f)
			return false;
		frame.extent[1] = height;
		// TWPFrame: the height its text fills
		if (frame.raw.size() >= 20) {
			uint32_t bits;
			memcpy(&bits, &height, 4);
			PutUInt32(frame.raw, 16, bits);
		}
		dirtyTails.insert(size_t(part));
		RepositionPages();
		return true;
	}
	return false;
}

} // namespace gobe
