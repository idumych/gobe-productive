// Pages of a paged document: the PAGE part's frames (TPagePortal), one per
// page, each listing the frames on it (TPageFrame: header, footer, the
// sheet's drawing, the text columns).
#include "Document.h"

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

} // namespace gobe
