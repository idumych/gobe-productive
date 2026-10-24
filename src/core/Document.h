// A Gobe Productive document, loaded far enough to show its content: the
// header and table of contents, the style manager and the parts (word
// processing parts parsed). Sections that aren't needed yet are skipped
// using the table of contents.
#ifndef GOBE_DOCUMENT_H
#define GOBE_DOCUMENT_H

#include <map>
#include <set>
#include <memory>
#include <string>
#include <vector>

#include "DocArchive.h"
#include "DocSections.h"
#include "ChartPart.h"
#include "GRPart.h"
#include "IMPart.h"
#include "PartTail.h"
#include "SSPart.h"
#include "TablePart.h"
#include "WPPart.h"

namespace gobe {

struct Document {
	std::string path;
	std::string name;		// window title ("untitled 1", etc. for new docs)
	std::vector<uint8_t> bytes;		// the file as read (for saving)
	DocFile file;
	std::vector<PartFactoryEntry> factories;
	StyleMgr styles;
	PartMgr parts;
	SelectionMgr selections;
	LinksMgr links;
	SheetMgr sheets;
	// parsed word processing parts, index into parts.parts
	std::vector<std::pair<size_t, WPPart>> wordParts;
	std::vector<std::pair<size_t, GRPart>> graphicsParts;
	std::vector<std::pair<size_t, SSPart>> spreadsheetParts;
	std::vector<std::pair<size_t, IMPart>> imageParts;
	std::vector<std::pair<size_t, ChartPart>> chartParts;
	std::vector<std::pair<size_t, TablePart>> tableParts;
	// references, portals and frames of each part (same index as parts.parts)
	std::vector<PartTail> tails;
	std::vector<bool> tailRead;
	std::vector<std::string> warnings;
	std::vector<std::pair<size_t, int>> hiddenFrames;	// see FrameHidden

	// A presentation (TPartPR::Read: 'prp+' ... the slides 'prp-'); each
	// slide is shown by switching the part's portal to the slide's view.
	struct Slide {
		uint32_t view;				// index into the part's graphics views
		uint32_t id = 0;
		uint32_t transition = 0;	// TPartPRFactory::FindTransition
		std::string name;			// the view's name
	};
	int presentationPart = -1;
	std::vector<Slide> slides;
	int currentSlide = 0;
	// shows slide `index` (recomputes the hidden frames)
	void ShowSlide(int index);
	void ComputeHiddenFrames();
	std::map<size_t, uint32_t> viewOverride;	// part -> view shown

	// editing: parts changed since loading (written by Save)
	std::set<size_t> dirtyParts;
	bool stylesChanged = false;		// the StyleMgr (written by Save)
	bool selectionsChanged = false;	// spreadsheet selections/names moved
	WPPart* EditableWordPart(size_t part);
	SSPart* EditableSpreadsheetPart(size_t part);
	// writes the document (changed parts written, everything else copied)
	// and loads it again from `path`
	bool Save(const std::string& path, std::string& error);

	const WPPart* WordPart(size_t part) const;
	const GRPart* GraphicsPart(size_t part) const;
	const SSPart* SpreadsheetPart(size_t part) const;
	const IMPart* ImagePart(size_t part) const;
	const ChartPart* Chart(size_t part) const;
	const TablePart* Table(size_t part) const;
	// a part's frame by its number (ReadFramePtr: part, frame)
	const Frame* FrameOf(uint32_t part, uint32_t index) const;

	// The part's frames with their position in root coordinates (the
	// container chain resolved), in flow order.
	struct PlacedFrame {
		size_t part;
		const Frame* frame;
		float x, y, width, height;
		int depth;			// nesting level (root = 0)
		int index;			// the frame's number among its part's frames
	};
	std::vector<PlacedFrame> PlacedFrames() const;

	// Graphics views show only some layers (TGRLayerInfo flag 0: visible;
	// presentations keep one view per slide): a shape layer of a graphics
	// part hidden in the view its portal shows, and frames placed by FRAM
	// shapes on such layers (and everything inside them).
	bool LayerHidden(size_t part, size_t layer) const;
	bool FrameHidden(size_t part, int index) const;

	// A named reference (formula names like "Products Table"): the
	// reference of that name in any part's REFT and its selection
	const Selection* NamedSelection(const std::string& name, int fromPart = -1) const;
	// the part and reference index a name refers to, seen from a formula in
	// `fromPart` (TDoc::FindLink): a link from that part first, then the
	// part's own references, then any part's
	bool FindName(const std::string& name, int fromPart, size_t& part, size_t& reference) const;

	// index of the root part (PROT/BROT), or -1
	int RootPart() const;
	// the first sheet's main part (what Save As exports), or -1
	int MainPart() const;
};

bool LoadDocument(const std::string& path, Document& doc, std::string& error);

// Creates a blank WORD document: header + all 13 sections, 1 WORD part with
// an empty paragraph, "Bas" and "GRFX" styles, proper TOC.  On success the
// document is fully populated and `path` is empty (it's unnamed).  On failure
// returns false and sets `error`.
bool CreateBlankDocument(Document& doc, std::string& error);

} // namespace gobe

#endif
