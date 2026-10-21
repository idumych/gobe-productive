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
	doc.path = path;
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

	std::vector<PlacedFrame> placed;
	for (size_t i = 0; i < tails.size(); i++) {
		if (!tailRead[i])
			continue;
		int index = 0;
		for (const Portal& portal : tails[i].portals) {
			for (const Frame& f : portal.frames) {
				uint32_t c = f.container;
				float bx = 0, by = 0;
				int depth = 0;
				if (c < origin.size() && origin[c].known) {
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
	bool linksChanged = false;
	for (size_t part : dirtyParts)
		if (SpreadsheetPart(part) != nullptr)
			linksChanged |= UpdateDependencies(*this, part);
	if (linksChanged)
		changes.sections["LinksMgr"] = WriteLinksMgr(links);
	if (stylesChanged)
		changes.sections["StyleMgr"] = WriteStyleMgr(styles);
	for (size_t part : dirtyParts) {
		const std::vector<uint8_t>& record = parts.parts[part].data;
		if (const WPPart* wp = WordPart(part)) {
			std::vector<uint8_t> written = WriteWPPart(*wp, record);
			// TWPFrame +0x4c/+0x50 in the tail: each frame's text range after
			// the edits (the tail moved with the body's new size)
			int64_t shift = int64_t(written.size()) - int64_t(record.size());
			if (tailRead[part])
				for (const Portal& portal : tails[part].portals)
					for (const Frame& f : portal.frames) {
						if (f.textAt == 0)
							continue;
						uint32_t start = MapTextOffset(*wp, f.textStart, false);
						uint32_t end = MapTextOffset(*wp, f.textStart + f.textLength, true);
						uint32_t values[2] = {start, end > start ? end - start : 0};
						size_t at = size_t(int64_t(f.textAt) + shift);
						for (int k = 0; k < 2; k++)
							for (int b = 0; b < 4; b++)
								written[at + k * 4 + b] = (values[k] >> (24 - 8 * b)) & 0xff;
					}
			changes.parts[part] = std::move(written);
			uint32_t length = 0;
			for (const WPParagraph& p : wp->paragraphs)
				length += uint32_t(p.text.size());
			textLength[part] = length;
		} else if (const SSPart* ss = SpreadsheetPart(part))
			changes.parts[part] = WriteSSPart(*ss, record);
	}
	// text selections of edited parts follow the edits
	if (!textLength.empty() || selectionsChanged) {
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

// Build a minimal WORD part record from scratch: PART, WORD, PartHeader (empty
// BMessage + 4 u32 + selection=0), wp++, header fields, one empty paragraph,
// one section, wp--. Returns the raw part data (after the size field).
static std::vector<uint8_t>
BuildWordPart()
{
	DocWriter w;

	// Part record header: PART signature + kind
	w.Sig(Sig("PART"));
	w.Sig(Sig("WORD"));

	// BMessage (empty, no MESG/MesG markers when size is 0)
	w.UInt32(0);	// message size 0 (no MESG/MesG for empty)

	// PartHeader: 4 u32 values + selection
	for (int i = 0; i < 4; i++)
		w.UInt32(0);
	w.UInt32(0);	// selection = 0

	// wp++ header
	w.Sig(Sig("wp++"));
	w.UInt16(0x0200);	// version98
	w.UInt16(1);		// version9a
	w.UInt32(0);		// value88
	w.UInt32(0);		// style
	w.UInt32(0);		// value8c
	w.UInt32(0);		// value94

	// par+: one empty paragraph
	w.Sig(Sig("par+"));
	w.UInt32(1);	// paragraph count
	// paragraph 0: start=0, length=0, value12=0, parStyle=0
	w.UInt32(0);	// start
	w.UInt32(0);	// length
	w.UInt16(0);	// value12
	w.UInt32(0);	// parStyle
	w.UInt32(0);	// runs count
	w.UInt32(0);	// misc count
	w.Sig(Sig("par-"));

	// txt+ / txt- (empty text)
	w.Sig(Sig("txt+"));
	w.Sig(Sig("txt-"));

	// sct+: one section (default column layout)
	w.Sig(Sig("sct+"));
	w.UInt32(1);	// section count
	w.UInt32(0);	// start
	w.UInt16(0);	// values[0]
	w.UInt16(0);	// values[1]
	w.Bool(false);	// flag
	w.UInt32(0);	// column count
	w.Sig(Sig("sct-"));

	w.Sig(Sig("wp--"));

	// REFT: 0 references (first u32 = size field, second u32 = ref count)
	w.Sig(Sig("REFT"));
	w.UInt32(0);	// size field (0 = no data)
	w.UInt32(0);	// reference count (0 refs)
	w.Sig(Sig("RefT"));
	w.UInt32(1);	// portal count (1 portal)

	// PORT: 1 portal with 1 frame pointing to PAGE (part index 2).
	// container = PAGE (2) so that PlacedFrames resolves PAGE before WORD.
	w.Sig(Sig("PORT"));
	w.UInt32(0xffffffff);	// part = none (this portal belongs to WORD)
	for (int i = 0; i < 3; i++)
		w.UInt32(0);	// values
	w.UInt32(1);	// 1 frame
	w.Sig(Sig("FRAM"));
	w.Float(0);		// angle
	w.Float(0);		// origin.x
	w.Float(0);		// origin.y
	w.Float(612);	// extent.width (page width)
	w.Float(792);	// extent.height (page height)
	w.UInt32(0);	// style
	for (int i = 0; i < 3; i++)
		w.UInt32(0);	// shape
	w.UInt32(2);	// part = PAGE (index 2)
	w.UInt32(2);	// container = PAGE (this part's container is PAGE)
	for (int i = 0; i < 4; i++)
		w.UInt32(0);	// values
	w.UInt32(0);	// textStart
	w.UInt32(0);	// textLength
	w.UInt32(0);	// version field (u32, for version < 0x1e)
	w.UInt32(0);	// version field
	w.Float(0);		// sheetRect[0]
	w.Float(0);		// sheetRect[1]
	w.Sig(Sig("FraM"));
	w.Float(0);		// TWPPortal extra float
	w.Sig(Sig("PorT"));

	// ParT
	w.Sig(Sig("ParT"));

	return w.Data();
}

// Build a PROT (paged root) part record.
// Contains: PART header, empty REFT, 1 PORT with 1 frame pointing to PAGE,
// FraM with BPoint(0,0), ParT.
static std::vector<uint8_t>
BuildProtPart()
{
	DocWriter w;

	// Part header
	w.Sig(Sig("PART"));
	w.Sig(Sig("PROT"));
	w.UInt32(0);	// BMessage size 0
	for (int i = 0; i < 4; i++)
		w.UInt32(0);	// header
	w.UInt32(0);	// selection

	// REFT: 0 references (first u32 = size field, second u32 = ref count)
	w.Sig(Sig("REFT"));
	w.UInt32(0);	// size field (0 = no data)
	w.UInt32(0);	// reference count (0 refs)
	w.Sig(Sig("RefT"));
	w.UInt32(1);	// portal count (1 portal)

	// PORT: 1 portal with 1 frame pointing to PAGE (part index 2)
	w.Sig(Sig("PORT"));
	w.UInt32(0xffffffff);	// part = none
	for (int i = 0; i < 3; i++)
		w.UInt32(0);	// values
	w.UInt32(1);	// 1 frame
	w.Sig(Sig("FRAM"));
	w.Float(0);		// angle
	w.Float(0);		// origin.x
	w.Float(0);		// origin.y
	w.Float(612);	// extent.width
	w.Float(792);	// extent.height
	w.UInt32(0);	// style
	for (int i = 0; i < 3; i++)
		w.UInt32(0);	// shape
	w.UInt32(2);	// part = PAGE (index 2)
	w.UInt32(1);	// container = PROT (this part, index 1)
	for (int i = 0; i < 4; i++)
		w.UInt32(0);	// values
	// TPagedRootFrame data: BPoint (within the frame)
	w.Float(0);		// BPoint.x
	w.Float(0);		// BPoint.y
	w.Sig(Sig("FraM"));
	w.Sig(Sig("PorT"));

	w.Sig(Sig("ParT"));

	return w.Data();
}

// Build a PAGE part record.
// Contains: PART header, empty REFT, 1 PORT with 1 frame pointing to WORD,
// FraM with TPageFrame data (1 frame pointer), ParT.
static std::vector<uint8_t>
BuildPagePart()
{
	DocWriter w;

	// Part header
	w.Sig(Sig("PART"));
	w.Sig(Sig("PAGE"));
	w.UInt32(0);	// BMessage size 0
	for (int i = 0; i < 4; i++)
		w.UInt32(0);	// header
	w.UInt32(0);	// selection

	// REFT: 0 references (first u32 = size field, second u32 = ref count)
	w.Sig(Sig("REFT"));
	w.UInt32(0);	// size field (0 = no data)
	w.UInt32(0);	// reference count (0 refs)
	w.Sig(Sig("RefT"));
	w.UInt32(1);	// portal count (1 portal)

	// PORT: 1 portal with 1 frame pointing to WORD (part index 0)
	w.Sig(Sig("PORT"));
	w.UInt32(0);	// part = WORD (index 0)
	for (int i = 0; i < 3; i++)
		w.UInt32(0);	// values
	w.UInt32(1);	// 1 frame
	w.Sig(Sig("FRAM"));
	w.Float(0);		// angle
	w.Float(0);		// origin.x
	w.Float(0);		// origin.y
	w.Float(612);	// extent.width (page width)
	w.Float(792);	// extent.height (page height)
	w.UInt32(0);	// style
	for (int i = 0; i < 3; i++)
		w.UInt32(0);	// shape
	w.UInt32(0);	// part = WORD (index 0)
	w.UInt32(1);	// container = PROT (root, index 1)
	for (int i = 0; i < 4; i++)
		w.UInt32(0);	// values
	w.UInt32(1);	// frame count (1 frame pointer)
	w.UInt32(0);	// frame pointer 1 (3 u32 each, first = 0)
	w.UInt32(0);	// frame pointer 2
	w.UInt32(0);	// frame pointer 3
	w.Sig(Sig("FraM"));
	// TPagePortal fields: 4 u32 + 2 frame pointers (3 u32 each) + 2 bools
	for (int i = 0; i < 4; i++)
		w.UInt32(0);	// extra values
	for (int i = 0; i < 3; i++)
		w.UInt32(0);	// frame pointer 1
	for (int i = 0; i < 3; i++)
		w.UInt32(0);	// frame pointer 2
	w.Bool(false);	// bool 1
	w.Bool(false);	// bool 2
	w.Sig(Sig("PorT"));

	w.Sig(Sig("ParT"));

	return w.Data();
}

// Build the 98-byte document header with a standard version.
static std::vector<uint8_t>
BuildHeader()
{
	DocWriter w;

	// Identity (16 bytes)
	w.Bytes(reinterpret_cast<const uint8_t*>("GOBE SCOTT/01/02"), 16);

	// File type 'DOC!'
	w.Sig(Sig("DOC!"));

	// 7 more u32s (fields)
	for (int i = 0; i < 7; i++)
		w.UInt32(0);

	// u16 (reserved)
	w.UInt16(0);

	// u32 + float + double (byte-order test values)
	w.UInt32(0);
	w.Float(1.0f);
	w.Double(1.0);

	// Version: 8 u16s (major=2, build=0, format=2, rest default)
	DocVersion version{};
	version.values[0] = 2;	// major user version
	version.values[2] = 2;	// file format version
	version.values[3] = 0;	// build version (read last for < 0x10)
	for (int i = 0; i < 8; i++)
		w.UInt16(version.values[i]);

	// 16 more bytes (padding)
	for (int i = 0; i < 16; i++)
		w.UInt8(0);

	return w.Data();
}

// Generate "untitled N" name (like the original TApp::UntitledDoc + UniqueName).
// Iterates through `existingNames` and appends " N" starting at 2 until unique.
static std::string
UntitledName(const std::vector<std::string>& existingNames)
{
	const std::string base = "untitled";
	// Check if "untitled" (without suffix) is already taken.
	for (const auto& n : existingNames)
		if (n == base)
			return base;  // will be incremented below

	// Find the highest suffix number among "untitled N" names.
	int highest = 0;
	for (const auto& n : existingNames) {
		if (n.rfind(base, 0) == 0) {  // starts with "untitled"
			// check if it's "untitled" followed by a space and number
			if (n.size() > base.size() && n[base.size()] == ' ') {
				const char* numStr = n.c_str() + base.size() + 1;
				char* end;
				long v = strtol(numStr, &end, 10);
				if (*end == '\0' && v > highest)
					highest = int(v);
			}
		}
	}
	return base + " " + std::to_string(highest + 1);
}

bool
CreateBlankDocument(Document& doc, std::string& error)
{
	try {
		// Determine the name from existing document names (empty in production
		// at this stage; the TApp tracks window count for naming).
		static int sUntitledCounter = 0;
		sUntitledCounter++;
		std::vector<std::string> existingNames;
		for (int i = 1; i < sUntitledCounter; i++)
			existingNames.push_back("untitled " + std::to_string(i));
		std::string name = UntitledName(existingNames);

		// Build the full document binary from scratch, tracking section offsets.
		DocWriter out;
		std::vector<TocEntry> toc;
		auto addSection = [&](const std::string& name) {
			TocEntry e;
			e.name = name;
			e.offset = uint32_t(out.Position());
			e.kind = 0;
			toc.push_back(e);
		};

		// 1. Header (98 bytes)
		out.Bytes(BuildHeader());

		// 2. VersionMgr
		{
			DocWriter section;
			section.Sig(Sig("VERM"));
			section.UInt32(1);	// version count
			DocVersion v{};
			v.values[0] = 2;
			v.values[2] = 2;
			v.values[3] = 0;
			for (int i = 0; i < 8; i++)
				section.UInt16(v.values[i]);
			section.Sig(Sig("VerM"));
			addSection("VersionMgr");
			out.Bytes(section.Data());
		}

		// 3. PartFactory Table (4 entries)
		{
			std::vector<PartFactoryEntry> factories = {
				{ Sig("WORD"), 1, "Word Processing" },
				{ Sig("GRFX"), 1, "Graphics" },
				{ Sig("PROT"), 1, "PagedRootPart" },
				{ Sig("PAGE"), 1, "PagePart" }
			};
			addSection("PartFactory Table");
			out.Bytes(WriteFactoryTable(factories));
		}

		// 4. Document Misc (blockCount=0, value70=0, 5 empty messages)
		{
			DocumentMisc misc;
			addSection("Document Misc");
			out.Bytes(WriteDocumentMisc(misc));
		}

		// 5. SetMgr (0 ink sets)
		addSection("SetMgr");
		out.Bytes(WriteSetMgr());

		// 6. PenMgr (0 pens)
		addSection("PenMgr");
		out.Bytes(WritePenMgr());

		// 7. StyleMgr: minimal styles from TDoc constructor.
		// The original TDoc constructor calls NewStyle('Bas', 0x42617369)
		// and FindNamedStyle('GRFX', 0x47524658). We create both with
		// minimal substyles.
		{
			StyleMgr styles;
			// Properties (empty set).
			// Style names table (empty — styles are identified by id).
			// Bas style (the base paragraph style).
			{
				Style bas;
				bas.id = 0x42617369u;	// 'Basi'
				bas.valueC = 0xffff;
				bas.name = "Default";
				styles.styles.push_back(std::move(bas));
			}
			// GRFX style (default ink paragraph style).
			{
				Style grfx;
				grfx.id = 0x47524658u;	// 'GRFX'
				grfx.valueC = 0xffff;
				grfx.name = "GR Default";
				styles.styles.push_back(std::move(grfx));
			}
			addSection("StyleMgr");
			out.Bytes(WriteStyleMgr(styles));
		}

		// 8. SheetMgr (root sheet "Sheet 1")
		addSection("SheetMgr");
		out.Bytes(WriteSheetMgr());

		// 9. SelectionMgr (0 selections)
		{
			SelectionMgr mgr;
			addSection("SelectionMgr");
			out.Bytes(WriteSelectionMgr(mgr));
		}

		// 10. LinksMgr (no links)
		{
			LinksMgr links;
			addSection("LinksMgr");
			out.Bytes(WriteLinksMgr(links));
		}

		// 11. PartMgr (WORD, PROT, PAGE)
		{
			PartMgr mgr;
			mgr.parts.push_back({Sig("WORD"), 0, BuildWordPart()});
			mgr.parts.push_back({Sig("PROT"), 0, BuildProtPart()});
			mgr.parts.push_back({Sig("PAGE"), 0, BuildPagePart()});
			addSection("PartMgr");
			out.Bytes(WritePartMgrFromRecords(mgr));
		}

		// 12. CommandMgr (0 commands)
		addSection("CommandMgr");
		out.Bytes(WriteCommandMgr());

		// 13. HistoryMgr (count 0)
		addSection("HistoryMgr");
		out.Bytes(WriteHistoryMgr());

		// 14. WindowMgr (0 windows)
		addSection("WindowMgr");
		out.Bytes(WriteWindowMgr());

		// 15. TOC entries (forward order, matching SaveDocument)
		uint32_t tocOffset = uint32_t(out.Position());
		out.UInt32(uint32_t(toc.size()));	// entry count (before entries)
		for (const TocEntry& e : toc) {
			out.String(e.name);
			out.UInt32(e.offset);
			out.UInt32(e.kind);
		}

		// 16. END!
		out.Sig(Sig("END!"));

		// Write the TOC offset into the header (big-endian, like the original)
		std::vector<uint8_t>& data = out.Data();
		for (int i = 0; i < 4; i++)
			data[0x14 + i] = (tocOffset >> (24 - 8 * i)) & 0xff;

		// Now parse the document back.
		doc.path.clear();
		doc.bytes = std::move(data);
		DocArchive archive(doc.bytes);
		if (!ReadDocFile(archive, doc.file, error))
			return false;

		for (const TocEntry& e : doc.file.toc) {
			archive.Seek(e.offset);
			if (e.name == "PartFactory Table")
				ReadPartFactoryTable(archive, doc.factories);
			else if (e.name == "StyleMgr")
				ReadStyleMgr(archive, doc.styles);
			else if (e.name == "SelectionMgr")
				ReadSelectionMgr(archive, doc.selections);
			else if (e.name == "LinksMgr")
				ReadLinksMgr(archive, doc.links);
			else if (e.name == "PartMgr")
				ReadPartMgr(archive, doc.parts);
		}

		// Parse the WORD part.
		for (size_t i = 0; i < doc.parts.parts.size(); i++) {
			const PartRecord& p = doc.parts.parts[i];
			if (p.kind != Sig("WORD"))
				continue;
			DocArchive pa(p.data);
			pa.SetVersions(doc.file.header.version.values[0],
				doc.file.header.version.values[3],
				doc.file.header.version.values[2]);
			WPPart wp;
			try {
				ReadWPPart(pa, wp);
				doc.wordParts.emplace_back(i, std::move(wp));
			} catch (const ArchiveError& x) {
				error = "WORD part " + std::to_string(i) + ": " + x.what();
				return false;
			}
		}

		doc.tails.resize(doc.parts.parts.size());
		doc.tailRead.assign(doc.parts.parts.size(), false);
		for (size_t i = 0; i < doc.parts.parts.size(); i++) {
			const PartRecord& p = doc.parts.parts[i];
			if (ReadPartTail(p.data, p.kind, archive, doc.tails[i]))
				doc.tailRead[i] = true;
		}

		// Set the document name ("untitled 1", "untitled 2", etc.)
		doc.name = name;

		return true;

	} catch (const ArchiveError& x) {
		error = "create blank document: " + std::string(x.what());
		return false;
	} catch (const std::exception& x) {
		error = "create blank document: " + std::string(x.what());
		return false;
	}
}

} // namespace gobe
