// Pages of a paged document: the PAGE part's frames (TPagePortal), one per
// page, each listing the frames on it (TPageFrame: header, footer, the
// sheet's drawing, the text columns).
#include "Document.h"

#include <algorithm>
#include <cmath>

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
	for (Frame::Pointer& child : page.children) {
		if (child.part == kNoPart || child.part >= tails.size() || !tailRead[child.part]
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
	// last first: a page's text columns follow each other in their portal
	for (auto it = page.children.rbegin(); it != page.children.rend(); ++it) {
		const Frame::Pointer& child = *it;
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

	int pagePart = PagePart();
	if (pagePart < 0)
		return true;
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
	return true;
}

} // namespace gobe
