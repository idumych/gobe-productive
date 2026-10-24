#include "Document.h"

#include "DocWriter.h"
#include "Editing.h"
#include "WPPart.h"

#include <algorithm>
#include <cstring>

#include <fstream>
#include <iterator>

#include "ResourceFile.h"

namespace gobe {

bool
LoadDocument(const std::string& path, Document& doc, std::string& error)
{
	std::ifstream in(path, std::ios::binary);
	if (!in) {
		error = "cannot open " + path;
		return false;
	}
	std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)),
		std::istreambuf_iterator<char>());
	if (!LoadDocumentBytes(std::move(data), doc, error))
		return false;
	doc.path = path;
	return true;
}


bool
LoadDocumentBytes(std::vector<uint8_t> data, Document& doc, std::string& error)
{
	doc.path.clear();
	doc.bytes = data;
	DocArchive a(std::move(data));
	if (!ReadDocFile(a, doc.file, error))
		return false;

	for (const TocEntry& e : doc.file.toc) {
		try {
			a.Seek(e.offset);
			if (e.name == "PartFactory Table")
				ReadPartFactoryTable(a, doc.factories);
			else if (e.name == "StyleMgr")
				ReadStyleMgr(a, doc.styles);
			else if (e.name == "SelectionMgr")
				ReadSelectionMgr(a, doc.selections);
			else if (e.name == "LinksMgr")
				ReadLinksMgr(a, doc.links);
			else if (e.name == "SheetMgr")
				ReadSheetMgr(a, doc.sheets);
			else if (e.name == "PartMgr")
				ReadPartMgr(a, doc.parts);
		} catch (const ArchiveError& x) {
			doc.warnings.push_back(e.name + ": " + x.what());
		}
	}

	for (size_t i = 0; i < doc.parts.parts.size(); i++) {
		const PartRecord& p = doc.parts.parts[i];
		if (p.kind == Sig("GRFX") || p.kind == Sig("PRES")) {
			DocArchive pa(p.data);
			pa.CopyVersions(a);
			GRPart gr;
			try {
				ReadGRPart(pa, gr);
				doc.graphicsParts.emplace_back(i, std::move(gr));
			} catch (const ArchiveError& x) {
				doc.warnings.push_back("GRFX part " + std::to_string(i) + ": " + x.what());
			}
			continue;
		}
		if (p.kind == Sig("TBLE")) {
			DocArchive pa(p.data);
			pa.CopyVersions(a);
			TablePart table;
			try {
				ReadTablePart(pa, table);
				doc.tableParts.emplace_back(i, std::move(table));
			} catch (const ArchiveError& x) {
				doc.warnings.push_back("TBLE part " + std::to_string(i) + ": " + x.what());
			}
			continue;
		}
		if (p.kind == Sig("chrt")) {
			DocArchive pa(p.data);
			pa.CopyVersions(a);
			ChartPart chart;
			try {
				ReadChartPart(pa, chart);
				doc.chartParts.emplace_back(i, std::move(chart));
			} catch (const ArchiveError& x) {
				doc.warnings.push_back("chrt part " + std::to_string(i) + ": " + x.what());
			}
			continue;
		}
		if (p.kind == Sig("BITS")) {
			DocArchive pa(p.data);
			pa.CopyVersions(a);
			IMPart im;
			try {
				ReadIMPart(pa, im);
				doc.imageParts.emplace_back(i, std::move(im));
			} catch (const ArchiveError& x) {
				doc.warnings.push_back("BITS part " + std::to_string(i) + ": " + x.what());
			}
			continue;
		}
		if (p.kind == Sig("SPRD")) {
			DocArchive pa(p.data);
			pa.CopyVersions(a);
			SSPart ss;
			try {
				ReadSSPart(pa, ss);
				doc.spreadsheetParts.emplace_back(i, std::move(ss));
			} catch (const ArchiveError& x) {
				doc.warnings.push_back("SPRD part " + std::to_string(i) + ": " + x.what());
			}
			continue;
		}
		if (p.kind != Sig("WORD"))
			continue;
		DocArchive pa(p.data);
		pa.CopyVersions(a);
		WPPart wp;
		try {
			ReadWPPart(pa, wp);
			doc.wordParts.emplace_back(i, std::move(wp));
		} catch (const ArchiveError& x) {
			doc.warnings.push_back("WORD part " + std::to_string(i) + ": " + x.what());
		}
	}
	doc.tails.resize(doc.parts.parts.size());
	doc.tailRead.assign(doc.parts.parts.size(), false);
	for (size_t i = 0; i < doc.parts.parts.size(); i++) {
		const PartRecord& p = doc.parts.parts[i];
		if (ReadPartTail(p.data, p.kind, a, doc.tails[i]))
			doc.tailRead[i] = true;
		else
			doc.warnings.push_back("part " + std::to_string(i) + ": frames not read");
	}

	if (doc.parts.parts.empty()) {
		error = "no parts";
		return false;
	}

	// the presentation's slides
	for (const auto& [part, gr] : doc.graphicsParts) {
		if (doc.parts.parts[part].kind != Sig("PRES"))
			continue;
		DocArchive pa(doc.parts.parts[part].data);
		pa.CopyVersions(a);
		try {
			GRPart skip;
			ReadGRPart(pa, skip);
			pa.CheckSig(Sig("prp+"));
			for (int k = 0; k < 3; k++)
				pa.UInt32();
			pa.Bool();
			pa.Bool();
			uint32_t count = pa.UInt32();
			std::vector<Document::Slide> slides;
			for (uint32_t k = 0; k < count; k++) {	// TSlide::Read
				Document::Slide slide;
				slide.view = pa.UInt32();
				slide.id = pa.UInt32();
				slide.transition = pa.UInt32();
				pa.UInt32();
				pa.Bool();
				if (slide.view < gr.views.size())
					slide.name = gr.views[slide.view].name;
				slides.push_back(slide);
			}
			pa.CheckSig(Sig("prp-"));
			doc.presentationPart = int(part);
			doc.slides = std::move(slides);
		} catch (const ArchiveError& x) {
			doc.warnings.push_back("PRES part " + std::to_string(part) + ": " + x.what());
		}
		break;
	}
	if (doc.presentationPart >= 0 && doc.tailRead[doc.presentationPart]
		&& !doc.tails[doc.presentationPart].portals.empty())
		for (size_t k = 0; k < doc.slides.size(); k++)
			if (doc.slides[k].view == doc.tails[doc.presentationPart].portals[0].view)
				doc.currentSlide = int(k);
	doc.ComputeHiddenFrames();
	return true;
}


void
Document::ComputeHiddenFrames()
{
	hiddenFrames.clear();
	// frames on hidden layers, then frames inside parts with no visible frame
	for (const auto& [part, gr] : graphicsParts)
		for (size_t l = 0; l < gr.layers.size(); l++)
			if (LayerHidden(part, l))
				for (const Shape& s : gr.layers[l].shapes)
					if (s.kind == Sig("FRAM"))
						hiddenFrames.emplace_back(s.frame[0], int(s.frame[1]));
	for (int pass = 0; pass < 8; pass++) {
		std::vector<PlacedFrame> placed = PlacedFrames();
		bool changed = false;
		for (size_t part = 0; part < parts.parts.size(); part++) {
			bool any = false, visible = false;
			for (const auto& f : placed)
				if (f.part == part) {
					any = true;
					visible |= !FrameHidden(f.part, f.index);
				}
			if (!any || visible)
				continue;
			for (const auto& f : placed)
				if (f.frame->container == part && !FrameHidden(f.part, f.index)) {
					hiddenFrames.emplace_back(f.part, f.index);
					changed = true;
				}
		}
		if (!changed)
			break;
	}
}


void
Document::ShowSlide(int index)
{
	if (presentationPart < 0 || slides.empty())
		return;
	index = std::max(0, std::min(int(slides.size()) - 1, index));
	currentSlide = index;
	viewOverride[presentationPart] = slides[index].view;
	ComputeHiddenFrames();
}


bool
Document::LayerHidden(size_t part, size_t layer) const
{
	const GRPart* gr = GraphicsPart(part);
	if (gr == nullptr || part >= tails.size() || !tailRead[part]
		|| tails[part].portals.empty())
		return false;
	uint32_t view = tails[part].portals[0].view;
	auto over = viewOverride.find(part);
	if (over != viewOverride.end())
		view = over->second;
	if (view >= gr->views.size())
		return false;
	for (const GRView::LayerInfo& info : gr->views[view].layers)
		if (info.layer == layer)
			return !info.flags[0];
	return true;	// not in the view
}


bool
Document::FrameHidden(size_t part, int index) const
{
	for (const auto& [p, i] : hiddenFrames)
		if (p == part && i == index)
			return true;
	return false;
}

const WPPart*
Document::WordPart(size_t part) const
{
	for (const auto& [index, wp] : wordParts)
		if (index == part)
			return &wp;
	return nullptr;
}


const GRPart*
Document::GraphicsPart(size_t part) const
{
	for (const auto& [index, gr] : graphicsParts)
		if (index == part)
			return &gr;
	return nullptr;
}


int
Document::MainPart() const
{
	if (!sheets.sheets.empty() && sheets.sheets[0].mainPart < parts.parts.size())
		return int(sheets.sheets[0].mainPart);
	return -1;
}


int
Document::RootPart() const
{
	for (size_t i = 0; i < parts.parts.size(); i++) {
		uint32_t k = parts.parts[i].kind;
		if (k == Sig("PROT") || k == Sig("BROT"))
			return int(i);
	}
	return -1;
}


std::vector<Document::PlacedFrame>
Document::PlacedFrames() const
{
	// each part's first frame gives its origin in its container
	struct Placement {
		bool known = false;
		float x = 0, y = 0;
		int depth = 0;
	};
	std::vector<Placement> origin(parts.parts.size());
	int root = RootPart();
	if (root >= 0)
		origin[root].known = true;

	// resolve containers outward-in; a few passes cover any nesting order
	for (int pass = 0; pass < 8; pass++) {
		for (size_t i = 0; i < tails.size(); i++) {
			if (origin[i].known || !tailRead[i])
				continue;
			for (const Portal& portal : tails[i].portals) {
				for (const Frame& f : portal.frames) {
					uint32_t c = f.container;
					if (c < origin.size() && origin[c].known) {
						origin[i] = {true, origin[c].x + f.origin[0],
							origin[c].y + f.origin[1], origin[c].depth + 1};
					}
					break;
				}
				if (origin[i].known)
					break;
			}
		}
	}

	// frames placed on a page are relative to their page's frame
	// (TPageFrame), not the page part's first one; the header and footer
	// frames are on every page
	int pagePart = PagePart();
	struct PageAt {
		float x, y;
		int page;
	};
	auto pagesOf = [&](size_t part, size_t portal, size_t frame) {
		std::vector<PageAt> at;
		if (pagePart < 0 || root < 0 || !origin[root].known)
			return at;
		const std::vector<Frame>& pages = tails[pagePart].portals[0].frames;
		for (size_t k = 0; k < pages.size(); k++)
			for (const Frame::Pointer& c : pages[k].children)
				if (c.part == part && c.portal == portal && c.frame == frame) {
					at.push_back({origin[root].x + pages[k].origin[0],
						origin[root].y + pages[k].origin[1], int(k)});
					break;
				}
		return at;
	};

	std::vector<PlacedFrame> placed;
	for (size_t i = 0; i < tails.size(); i++) {
		if (!tailRead[i])
			continue;
		int index = 0;
		for (size_t pi = 0; pi < tails[i].portals.size(); pi++) {
			const Portal& portal = tails[i].portals[pi];
			for (size_t fi = 0; fi < portal.frames.size(); fi++) {
				const Frame& f = portal.frames[fi];
				uint32_t c = f.container;
				float bx = 0, by = 0;
				int depth = 0;
				std::vector<PageAt> pages;
				if (int(c) == pagePart)
					pages = pagesOf(i, pi, fi);
				if (!pages.empty()) {
					for (const PageAt& p : pages)
						placed.push_back({i, &f, p.x + f.origin[0], p.y + f.origin[1],
							f.extent[0], f.extent[1], origin[c].depth + 1, index, p.page});
					index++;
					continue;
				} else if (c < origin.size() && origin[c].known) {
					bx = origin[c].x;
					by = origin[c].y;
					depth = origin[c].depth + 1;
				} else if (int(i) != root)
					continue;	// not placed anywhere we know
				placed.push_back({i, &f, bx + f.origin[0], by + f.origin[1],
					f.extent[0], f.extent[1], depth, index++});
			}
		}
	}
	return placed;
}


const TablePart*
Document::Table(size_t part) const
{
	for (const auto& [index, table] : tableParts)
		if (index == part)
			return &table;
	return nullptr;
}


const Frame*
Document::FrameOf(uint32_t part, uint32_t index) const
{
	if (part >= tails.size() || !tailRead[part])
		return nullptr;
	uint32_t n = 0;
	for (const Portal& portal : tails[part].portals)
		for (const Frame& f : portal.frames)
			if (n++ == index)
				return &f;
	return nullptr;
}


const ChartPart*
Document::Chart(size_t part) const
{
	for (const auto& [index, chart] : chartParts)
		if (index == part)
			return &chart;
	return nullptr;
}


const IMPart*
Document::ImagePart(size_t part) const
{
	for (const auto& [index, im] : imageParts)
		if (index == part)
			return &im;
	return nullptr;
}


const SSPart*
Document::SpreadsheetPart(size_t part) const
{
	for (const auto& [index, ss] : spreadsheetParts)
		if (index == part)
			return &ss;
	return nullptr;
}


bool
Document::FindName(const std::string& name, int fromPart, size_t& part,
	size_t& reference) const
{
	auto nameOf = [&](size_t p, size_t r) -> const std::string* {
		if (p >= tails.size() || !tailRead[p] || r >= tails[p].refs.size())
			return nullptr;
		return &tails[p].refs[r].name;
	};
	if (fromPart >= 0)
		for (const Link& l : links.links) {
			const std::string* n = nameOf(l.sourcePart, l.reference);
			if (l.dependentPart == uint32_t(fromPart) && n != nullptr && *n == name) {
				part = l.sourcePart;
				reference = l.reference;
				return true;
			}
		}
	if (fromPart >= 0 && size_t(fromPart) < tails.size() && tailRead[fromPart])
		for (size_t r = 0; r < tails[fromPart].refs.size(); r++)
			if (tails[fromPart].refs[r].name == name) {
				part = size_t(fromPart);
				reference = r;
				return true;
			}
	for (size_t p = 0; p < tails.size(); p++) {
		if (!tailRead[p])
			continue;
		for (size_t r = 0; r < tails[p].refs.size(); r++)
			if (tails[p].refs[r].name == name) {
				part = p;
				reference = r;
				return true;
			}
	}
	return false;
}


const Selection*
Document::NamedSelection(const std::string& name, int fromPart) const
{
	size_t part, reference;
	if (!FindName(name, fromPart, part, reference))
		return nullptr;
	return selections.Find(tails[part].refs[reference].selection);
}



WPPart*
Document::EditableWordPart(size_t part)
{
	for (auto& [index, wp] : wordParts)
		if (index == part) {
			dirtyParts.insert(part);
			return &wp;
		}
	return nullptr;
}


SSPart*
Document::EditableSpreadsheetPart(size_t part)
{
	for (auto& [index, ss] : spreadsheetParts)
		if (index == part) {
			dirtyParts.insert(part);
			return &ss;
		}
	return nullptr;
}


bool
Document::Save(const std::string& to, std::string& error)
{
	DocChanges changes;
	std::map<size_t, uint32_t> textLength;
	// spreadsheets: dependency tables (and possibly new links) first
	bool linksChanged = this->linksChanged;
	for (size_t part : dirtyParts)
		if (SpreadsheetPart(part) != nullptr)
			linksChanged |= UpdateDependencies(*this, part);
	if (linksChanged)
		changes.sections["LinksMgr"] = WriteLinksMgr(links);
	if (stylesChanged)
		changes.sections["StyleMgr"] = WriteStyleMgr(styles);
	if (sheetsChanged)
		changes.sections["SheetMgr"] = WriteSheetMgr(sheets);
	std::set<size_t> written;
	for (size_t part : dirtyParts) written.insert(part);
	for (size_t part : dirtyTails) written.insert(part);
	for (size_t part : written) {
		const std::vector<uint8_t>& record = parts.parts[part].data;
		const WPPart* wp = WordPart(part);
		std::vector<uint8_t> out;
		if (dirtyParts.count(part) && wp != nullptr)
			out = WriteWPPart(*wp, record);
		else if (dirtyParts.count(part) && SpreadsheetPart(part) != nullptr)
			out = WriteSSPart(*SpreadsheetPart(part), record);
		else
			out = record;
		// the tail (it moved with the body's new size): the frames as they
		// are, word processing frames' text ranges after the edits unless
		// the layout set them (TWPFrame +0x4c/+0x50)
		if (tailRead[part] && (wp != nullptr || dirtyTails.count(part))) {
			int64_t shift = int64_t(out.size()) - int64_t(record.size());
			PartTail tail = tails[part];
			if (wp != nullptr && !layoutRanges.count(part))
				for (Portal& portal : tail.portals)
					for (Frame& f : portal.frames) {
						if (f.textAt == 0)
							continue;
						uint32_t start = MapTextOffset(*wp, f.textStart, false);
						uint32_t end = MapTextOffset(*wp, f.textStart + f.textLength, true);
						f.textStart = start;
						f.textLength = end > start ? end - start : 0;
					}
			std::vector<uint8_t> bytes = WritePartTail(tail, parts.parts[part].kind);
			out.resize(size_t(int64_t(tail.offset) + shift));
			out.insert(out.end(), bytes.begin(), bytes.end());
		}
		changes.parts[part] = std::move(out);
		if (dirtyParts.count(part) && wp != nullptr) {
			uint32_t length = 0;
			for (const WPParagraph& p : wp->paragraphs)
				length += uint32_t(p.text.size());
			textLength[part] = length;
		}
	}
	// text selections of edited parts follow the edits
	if (!textLength.empty() || selectionsChanged || !addedSelections.empty()) {
		for (size_t s = 0; s < file.toc.size(); s++) {
			if (file.toc[s].name != "SelectionMgr")
				continue;
			size_t start = file.toc[s].offset;
			size_t end = s + 1 < file.toc.size() ? file.toc[s + 1].offset : file.tocOffset;
			std::vector<uint8_t> section(bytes.begin() + start, bytes.begin() + end);
			auto put16 = [&](size_t at, uint16_t v) {
				section[at] = uint8_t(v >> 8);
				section[at + 1] = uint8_t(v);
			};
			for (const Selection& sel : selections.selections) {
				// spreadsheet selections after rows/columns moved (same count)
				if (selectionsChanged && sel.kind == Sig("SPRD") && sel.cellAt > start) {
					size_t at = sel.cellAt - start;
					put16(at, sel.cell[0]);
					put16(at + 2, sel.cell[1]);
					at += 8;
					for (const auto& r : sel.ranges)
						for (uint16_t v : r) {
							put16(at, v);
							at += 2;
						}
				}
				auto length = textLength.find(sel.part);
				if (sel.kind != Sig("WORD") || length == textLength.end())
					continue;
				for (size_t k = 0; k < sel.textRanges.size(); k++) {
					size_t at = sel.textRangesAt - start + k * 8;
					for (int half = 0; half < 2; half++) {
						uint32_t v = half ? sel.textRanges[k].second : sel.textRanges[k].first;
						// where that text went, and never past the end
						if (const WPPart* wp = WordPart(sel.part))
							v = MapTextOffset(*wp, v, half == 1);
						v = std::min(v, length->second);
						for (int i = 0; i < 4; i++)
							section[at + half * 4 + i] = (v >> (24 - 8 * i)) & 0xff;
					}
				}
			}
			// new parts' selections at the end (TSelectionMgr::Write: the
			// count, the records, 'SelM')
			if (!addedSelections.empty() && section.size() >= 12) {
				uint32_t count = uint32_t(section[4]) << 24 | uint32_t(section[5]) << 16
					| uint32_t(section[6]) << 8 | section[7];
				count += uint32_t(addedSelections.size());
				for (int i = 0; i < 4; i++)
					section[4 + i] = uint8_t(count >> (24 - 8 * i));
				for (const std::vector<uint8_t>& record : addedSelections)
					section.insert(section.end() - 4, record.begin(), record.end());
			}
			changes.sections["SelectionMgr"] = std::move(section);
		}
	}
	std::vector<uint8_t> data = SaveDocument(*this, changes);
	if (!WriteFile(to, data, error))
		return false;
	Document fresh;
	if (!LoadDocument(to, fresh, error))
		return false;
	int slide = currentSlide;
	*this = std::move(fresh);
	if (presentationPart >= 0 && slide != currentSlide)
		ShowSlide(slide);
	return true;
}

} // namespace gobe
