#include "Document.h"

#include "BlankParts.h"
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
			else if (e.name == "Document Misc") {
				DocumentMisc misc;
				ReadDocumentMisc(a, misc);
				doc.documentFlags = misc.value70;
			}
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
	size_t sheet = size_t(CurrentSheet());
	if (sheet < sheets.sheets.size() && sheets.sheets[sheet].mainPart < parts.parts.size())
		return int(sheets.sheets[sheet].mainPart);
	return -1;
}


int
Document::AddSpreadsheetSheet(const std::string& name, std::string& error)
{
	if (sheets.sheets.empty()) {
		error = "no sheets";
		return -1;
	}
	uint32_t base = kNoPart;
	for (size_t i = 0; i < styles.styles.size(); i++)
		if (styles.styles[i].name == "Default" && styles.styles[i].value10 == Sig("Basi"))
			base = uint32_t(i);
	if (base == kNoPart) {
		error = "no Default style";
		return -1;
	}
	// TPart::TPart: the next part ids
	uint32_t id = 0;
	for (const PartRecord& p : parts.parts)
		if (p.data.size() >= 20)
			id = std::max(id, uint32_t(p.data[16]) << 24 | uint32_t(p.data[17]) << 16
				| uint32_t(p.data[18]) << 8 | p.data[19]);
	SheetStyles sheetStyles = AddSheetStyles(styles, base);
	stylesChanged = true;
	// the parts' selections: the drawing's (no shapes), the sheet's (A1)
	uint32_t main = uint32_t(parts.parts.size()), graphics = main + 1;
	uint32_t drawingSelection = uint32_t(selections.selections.size());
	uint32_t cellSelection = drawingSelection + 1;
	{
		DocWriter body;
		body.Sig(Sig("GRFX"));
		body.UInt32(graphics);
		body.UInt32(drawingSelection);
		body.UInt32(0);
		DocWriter record;
		record.UInt32(uint32_t(body.Data().size()));
		record.Bytes(body.Data());
		addedSelections.push_back(record.Data());
		Selection s;
		s.id = drawingSelection;
		s.kind = Sig("GRFX");
		s.part = graphics;
		s.value = drawingSelection;
		selections.selections.push_back(s);
	}
	{
		DocWriter body;
		body.Sig(Sig("SPRD"));
		body.UInt32(main);
		body.UInt32(cellSelection);
		body.Sig(Sig("CELL"));
		body.UInt16(0);
		body.UInt16(0);
		body.UInt32(1);
		for (int i = 0; i < 4; i++)
			body.UInt16(0);
		body.Bool(true);
		DocWriter record;
		record.UInt32(uint32_t(body.Data().size()));
		record.Bytes(body.Data());
		addedSelections.push_back(record.Data());
		Selection s;
		s.id = cellSelection;
		s.kind = Sig("SPRD");
		s.part = main;
		s.value = cellSelection;
		s.ranges.push_back({0, 0, 0, 0});
		s.hasCell = true;
		selections.selections.push_back(s);
	}
	// the records, parsed as LoadDocument does
	DocArchive versions(std::vector<uint8_t>{});
	versions.SetVersions(file.header.version.values[0], file.header.version.values[3],
		file.header.version.values[2]);
	parts.parts.push_back({Sig("SPRD"), 0,
		SheetPartRecord(id + 1, cellSelection, sheetStyles, false, 0)});
	parts.parts.push_back({Sig("GRFX"), 0,
		SheetGraphicsRecord(id + 2, drawingSelection, false, 0)});
	try {
		DocArchive pa(parts.parts[main].data);
		pa.CopyVersions(versions);
		SSPart ss;
		ReadSSPart(pa, ss);
		spreadsheetParts.emplace_back(main, std::move(ss));
		DocArchive ga(parts.parts[graphics].data);
		ga.CopyVersions(versions);
		GRPart gr;
		ReadGRPart(ga, gr);
		graphicsParts.emplace_back(graphics, std::move(gr));
	} catch (const ArchiveError& x) {
		error = std::string("new sheet: ") + x.what();
		return -1;
	}
	tails.resize(parts.parts.size());
	tailRead.resize(parts.parts.size(), false);
	for (uint32_t part : {main, graphics}) {
		tailRead[part] = ReadPartTail(parts.parts[part].data, parts.parts[part].kind, versions,
			tails[part]);
		dirtyTails.insert(part);
	}
	// factories for kinds the document hasn't had
	for (const auto& [kind, label] : {std::make_pair(Sig("SPRD"), "Spreadsheet"),
			std::make_pair(Sig("GRFX"), "Graphics")}) {
		bool known = false;
		for (const PartFactoryEntry& f : factories)
			known |= f.kind == kind;
		if (!known) {
			factories.push_back({kind, 1, label});
			factoriesChanged = true;
		}
	}
	// the sheet: the shown sheet's paper and rulers, not in page view
	LeafSheet sheet = sheets.sheets[size_t(CurrentSheet())];
	sheet.name = name;
	if (sheet.name.empty())
		sheet.name = "Sheet " + std::to_string(SheetCount() + 1);
	sheet.mainPart = main;
	sheet.graphicsPart = graphics;
	sheet.headerPart = sheet.footerPart = kNoPart;
	sheet.frameSheet = false;
	sheet.pageView = false;
	// TSheetMgr::NewSheet: after those in use, before the deleted ones
	size_t at = SheetCount();
	sheets.sheets.insert(sheets.sheets.begin() + long(at), sheet);
	sheets.sheetCount = uint32_t(at + 1);
	sheetsChanged = true;
	return int(at);
}


size_t
Document::SheetCount() const
{
	if (sheets.sheetCount == 0 || sheets.sheetCount > sheets.sheets.size())
		return sheets.sheets.size();
	return sheets.sheetCount;
}


bool
Document::DeleteSheet(size_t sheet, std::string& error)
{
	size_t count = SheetCount();
	if (sheet >= count || count < 2) {
		error = "the last sheet can't be deleted";
		return false;
	}
	if (int(sheet) == CurrentSheet() && !ShowSheet(sheet == 0 ? 1 : sheet - 1, error))
		return false;
	int shown = CurrentSheet();
	LeafSheet moved = sheets.sheets[sheet];
	sheets.sheets.erase(sheets.sheets.begin() + long(sheet));
	sheets.sheets.push_back(moved);
	sheets.sheetCount = uint32_t(count - 1);
	sheetsChanged = true;
	// the shown sheet's index after the move
	if (shown > int(sheet)) {
		int root = RootPart();
		std::vector<uint8_t>& record = parts.parts[size_t(root)].data;
		for (int i = 0; i < 4; i++)
			record[32 + i] = uint8_t(uint32_t(shown - 1) >> (24 - 8 * i));
		dirtyTails.insert(size_t(root));
	}
	return true;
}


bool
Document::MoveSheet(size_t from, size_t to)
{
	size_t count = SheetCount();
	if (from >= count || to >= count)
		return false;
	if (from == to)
		return true;
	int shown = CurrentSheet();
	LeafSheet moved = sheets.sheets[from];
	sheets.sheets.erase(sheets.sheets.begin() + long(from));
	sheets.sheets.insert(sheets.sheets.begin() + long(to), moved);
	sheetsChanged = true;
	// TSheetMgr::FixIndex: the shown sheet's new index
	int now = shown == int(from) ? int(to)
		: shown > int(from) && shown <= int(to) ? shown - 1
		: shown < int(from) && shown >= int(to) ? shown + 1 : shown;
	int root = RootPart();
	if (now != shown && root >= 0) {
		std::vector<uint8_t>& record = parts.parts[size_t(root)].data;
		for (int i = 0; i < 4; i++)
			record[32 + i] = uint8_t(uint32_t(now) >> (24 - 8 * i));
		dirtyTails.insert(size_t(root));
	}
	return true;
}


int
Document::CurrentSheet() const
{
	int root = RootPart();
	if (root < 0)
		return 0;
	// TRootPart::Write: the sheet after TPart's fields (32 bytes)
	const std::vector<uint8_t>& d = parts.parts[size_t(root)].data;
	if (d.size() < 36)
		return 0;
	uint32_t sheet = uint32_t(d[32]) << 24 | uint32_t(d[33]) << 16 | uint32_t(d[34]) << 8 | d[35];
	return sheet < sheets.sheets.size() ? int(sheet) : 0;
}


bool
Document::ShowSheet(size_t sheet, std::string& error)
{
	int root = RootPart();
	if (sheet >= SheetCount()) {
		error = "no such sheet";
		return false;
	}
	if (int(sheet) == CurrentSheet())
		return true;
	if (root < 0 || parts.parts[size_t(root)].kind != Sig("BROT") || !tailRead[size_t(root)]
			|| tails[size_t(root)].portals.size() != 1
			|| tails[size_t(root)].portals[0].frames.size() != 1) {
		error = "the sheets aren't shown in a browse root";
		return false;
	}
	const LeafSheet& from = sheets.sheets[size_t(CurrentSheet())];
	const LeafSheet& to = sheets.sheets[sheet];
	auto usable = [&](uint32_t part, bool main) {
		if (part == kNoPart)
			return !main;
		if (part >= parts.parts.size() || !tailRead[part])
			return false;
		uint32_t kind = parts.parts[part].kind;
		return kind == Sig("SPRD") || kind == Sig("WORD") || kind == Sig("GRFX")
			|| kind == Sig("chrt");
	};
	if (!usable(to.mainPart, true) || !usable(to.graphicsPart, false)) {
		error = "the sheet's parts can't be shown";
		return false;
	}
	// the shown sheet's frames in the root: their sizes and styles, then gone
	struct Shown {
		bool found = false;
		Frame frame;
	} oldMain, oldGraphics;
	auto takeOut = [&](uint32_t part, Shown& shown) {
		if (part >= tails.size() || !tailRead[part])
			return;
		std::vector<Portal>& portals = tails[part].portals;
		for (size_t i = 0; i < portals.size(); i++)
			if (portals[i].part == uint32_t(root) && !portals[i].frames.empty()) {
				shown.found = true;
				shown.frame = portals[i].frames[0];
				portals.erase(portals.begin() + i);
				dirtyTails.insert(part);
				return;
			}
	};
	takeOut(from.mainPart, oldMain);
	takeOut(from.graphicsPart, oldGraphics);
	const Frame& rootFrame = tails[size_t(root)].portals[0].frames[0];
	if (!oldMain.found) {
		oldMain.frame.extent[0] = rootFrame.extent[0];
		oldMain.frame.extent[1] = rootFrame.extent[1];
		oldMain.frame.style = rootFrame.style;
	}
	if (!oldGraphics.found)
		oldGraphics.frame = oldMain.frame;

	// a portal in the root with one frame (TPortal: 0, its flags, 1)
	auto bring = [&](uint32_t part, const Frame& like, uint32_t flags) -> uint32_t {
		if (part == kNoPart)
			return kNoPart;
		uint32_t kind = parts.parts[part].kind;
		Portal portal;
		portal.part = uint32_t(root);
		portal.values[0] = 0;
		portal.values[1] = flags;
		portal.values[2] = 1;
		Frame f;
		f.origin[0] = like.origin[0];
		f.origin[1] = like.origin[1];
		f.extent[0] = like.extent[0];
		f.extent[1] = like.extent[1];
		f.style = like.style;
		f.shape[0] = 0;
		f.shape[1] = f.shape[2] = kNoPart;
		f.part = part;
		f.container = uint32_t(root);
		DocWriter raw, portalRaw;
		float rect[4] = {0, 0, f.extent[0], f.extent[1]};
		if (kind == Sig("SPRD")) {
			// TSSFrame: the cells shown (SetSize sets them again), the rectangle
			raw.UInt16(0);
			raw.UInt16(0);
			raw.UInt16(uint16_t(f.extent[1] / 16));
			raw.UInt16(uint16_t(f.extent[0] / 72));
			raw.Rect(rect);
			f.cells[2] = uint16_t(f.extent[1] / 16);
			f.cells[3] = uint16_t(f.extent[0] / 72);
			std::copy(rect, rect + 4, f.sheetRect);
		} else if (kind == Sig("WORD")) {
			// TWPFrame: the text it shows (all of it), the heights (none yet)
			uint32_t length = 0;
			if (const WPPart* wp = WordPart(part))
				for (const WPParagraph& p : wp->paragraphs)
					length += uint32_t(p.text.size());
			f.textStart = 0;
			f.textLength = length;
			f.textAt = 1;
			raw.UInt32(0);
			raw.UInt32(length);
			for (int i = 0; i < 4; i++)
				raw.Float(0);
			portalRaw.Float(0);				// TWPPortal
		} else {
			raw.Rect(rect);					// TFrameGR
			std::copy(rect, rect + 4, f.sheetRect);
			portalRaw.UInt32(0);			// TGRPortal: view 0
			portal.view = 0;
		}
		f.raw = raw.Data();
		portal.raw = portalRaw.Data();
		portal.frames.push_back(f);
		tails[part].portals.push_back(portal);
		dirtyTails.insert(part);
		return uint32_t(tails[part].portals.size() - 1);
	};
	uint32_t mainPortal = bring(to.mainPart, oldMain.frame, 12);
	uint32_t graphicsPortal = bring(to.graphicsPart, oldGraphics.frame, 4);
	// TBrowseRootFrame: the main frame and the graphics frame
	DocWriter pointers;
	pointers.UInt32(to.mainPart);
	pointers.UInt32(mainPortal);
	pointers.UInt32(0);
	pointers.UInt32(to.graphicsPart);
	pointers.UInt32(graphicsPortal);
	pointers.UInt32(to.graphicsPart == kNoPart ? kNoPart : 0);
	tails[size_t(root)].portals[0].frames[0].raw = pointers.Data();
	dirtyTails.insert(size_t(root));
	std::vector<uint8_t>& record = parts.parts[size_t(root)].data;
	for (int i = 0; i < 4; i++)
		record[32 + i] = uint8_t(sheet >> (24 - 8 * i));
	ComputeHiddenFrames();
	return true;
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
	// a dead reference's (a deleted name's, TReference::SetDead: detached
	// from the links) isn't found
	auto nameOf = [&](size_t p, size_t r) -> const std::string* {
		if (p >= tails.size() || !tailRead[p] || r >= tails[p].refs.size()
				|| tails[p].refs[r].flag1)
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
			if (nameOf(size_t(fromPart), r) != nullptr && tails[fromPart].refs[r].name == name) {
				part = size_t(fromPart);
				reference = r;
				return true;
			}
	for (size_t p = 0; p < tails.size(); p++) {
		if (!tailRead[p])
			continue;
		for (size_t r = 0; r < tails[p].refs.size(); r++)
			if (nameOf(p, r) != nullptr && tails[p].refs[r].name == name) {
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
	if (factoriesChanged)
		changes.sections["PartFactory Table"] = WriteFactoryTable(factories);
	// the flags after the window count, the rest as read
	for (size_t s = 0; documentFlagsChanged && s < file.toc.size(); s++) {
		if (file.toc[s].name != "Document Misc")
			continue;
		size_t start = file.toc[s].offset;
		size_t end = s + 1 < file.toc.size() ? file.toc[s + 1].offset : file.tocOffset;
		std::vector<uint8_t> section(bytes.begin() + start, bytes.begin() + end);
		for (int i = 0; i < 4 && section.size() >= 8; i++)
			section[4 + i] = uint8_t(documentFlags >> (24 - 8 * i));
		changes.sections["Document Misc"] = std::move(section);
	}
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
			// spreadsheet selections after cells moved: their active cell,
			// ranges and flag, put in after the in-place changes (last first)
			std::vector<std::pair<std::pair<size_t, size_t>, std::vector<uint8_t>>> splices;
			for (const Selection& sel : selections.selections) {
				if (selectionsChanged && sel.kind == Sig("SPRD") && sel.cellAt > start
						&& sel.cellsEnd > sel.cellAt) {
					std::vector<uint8_t> record;
					auto add = [&](uint32_t v, int bytes) {
						for (int i = bytes - 1; i >= 0; i--)
							record.push_back(uint8_t(v >> (8 * i)));
					};
					add(sel.cell[0], 2);
					add(sel.cell[1], 2);
					add(uint32_t(sel.ranges.size()), 4);
					for (const auto& r : sel.ranges)
						for (uint16_t v : r)
							add(v, 2);
					add(sel.hasCell ? 1 : 0, 1);
					splices.push_back({{sel.cellAt - start, sel.cellsEnd - start},
						std::move(record)});
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
			std::sort(splices.begin(), splices.end(),
				[](const auto& x, const auto& y) { return x.first.first > y.first.first; });
			for (const auto& [where, record] : splices) {
				section.erase(section.begin() + where.first, section.begin() + where.second);
				section.insert(section.begin() + where.first, record.begin(), record.end());
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
