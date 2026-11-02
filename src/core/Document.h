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
#include "InkSets.h"
#include "Pens.h"
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
	PenMgr pens;			// the document's pens ("PenMgr")
	SetMgr inkSets;			// the document's own ink sets ("SetMgr")
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
	// slide is shown by switching the part's portal to the slide's view. A
	// slide's layers are its view's layer infos with flag 4 on, in order
	// (TSlide::Layer); its name is the view's (TSlide::Name).
	struct Slide {
		uint32_t view;				// index into the part's graphics views
		uint32_t layers = 0;		// TSlide +0: how many layers it has
		uint32_t transition = 0;	// TPartPRFactory::FindTransition
		uint32_t options = 0;		// TSlide +0x14: the transition's options
		bool collapsed = false;		// TSlide +0x18: its layers hidden in the list
		std::string name;			// the view's name (kept in step with it)
	};
	// TPartPR +0xc0, +0xc4, +0xc8, +0xa9, +0xaa
	struct PresentationSettings {
		uint32_t slide = 0;			// the current slide
		uint32_t layer = 0;			// the current layer
		uint32_t scroll = 0;		// the Presentation window's list (ListScrollOrigin)
		bool autoStart = false;		// Options > Auto-start
		bool cursor = false;		// Options > Cursor (visible in the slide show)
	};
	int presentationPart = -1;
	std::vector<Slide> slides;
	PresentationSettings presentation;
	size_t presentationEnd = 0;		// in the part's record, just after 'prp-'
	int currentSlide = 0;
	// shows slide `index` (recomputes the hidden frames)
	void ShowSlide(int index);

	// Presentation.cpp: the slides and their layers (TPartPR, TSlide and the
	// TPR...Commands' work). Slide layers count from the slide's first
	// (TSlide::Layer); part layers are the graphics part's.
	const GRPart* PresentationGraphics() const;
	std::string SlideName(size_t slide) const;
	// the infos of the slide's view that are its layers, in order
	std::vector<size_t> SlideLayers(size_t slide) const;
	// TSlide::IDForLayer: the part layer of its layer `layer`, -1 if none;
	// TSlide::IndexForLayer the other way
	int SlideLayerPart(size_t slide, size_t layer) const;
	int SlideLayerIndex(size_t slide, size_t partLayer) const;
	// the slides' names and layer counts from their views again
	void SyncSlides();
	// TPartPR::SetCurrentSlide: its layers shown, all but the first (the
	// background) editable unless it's alone, the last active; the portal
	// shows its view
	bool SetCurrentSlide(size_t slide);
	// TPartPR::ActivateLayer: the slide current with its layer `layer`
	// alone shown and edited, or (-1) all of them as SetCurrentSlide
	bool ActivateSlideLayer(size_t slide, int layer);
	// TSlide::TSlide: a slide named `name` with a view of its own (every
	// layer, none in the slide), at `at`; its index
	size_t AddSlide(size_t at, const std::string& name);
	// TSlide::AddLayer: part layer `partLayer` into the slide before its
	// layer `position` (after the last if past them); RemoveLayer: its
	// layer `layer` out of it
	bool SlideAddLayer(size_t slide, size_t partLayer, size_t position);
	bool SlideRemoveLayer(size_t slide, size_t layer);
	// TPartPR::RemoveSlide (with its view; not the last slide), moving one,
	// TSlide::SetName (its view's name), SetTransition and SetOptions;
	// false if nothing changed
	bool RemoveSlide(size_t slide);
	bool MoveSlide(size_t from, size_t to);
	bool RenameSlide(size_t slide, const std::string& name);
	bool SetSlideTransition(size_t slide, uint32_t type, uint32_t options);
	// Options > Auto-start (`cursor` false) and Cursor
	bool SetPresentationFlag(bool cursor, bool on);
	// a layer of the presentation's drawing, in every view and no slide
	// (TGRLayerNewOrDeleteCommand); deleted from every slide and view (not
	// the last); renamed
	size_t AddPresentationLayer(const std::string& name);
	bool DeletePresentationLayer(size_t partLayer);
	bool RenamePresentationLayer(size_t partLayer, const std::string& name);
	// how many slides have the layer
	int PresentationLayerUses(size_t partLayer) const;
	// the TPR...Commands that do more than one of those (each ends on the
	// slide, made current): New Slide (at `at`; the previous slide's
	// background shared; `newLayer`, if named, added after), Duplicate
	// Slide (after it; its layers, or copies of them with their shapes but
	// the background when shared; `newLayer` when not copying), New Layer
	// (after the slide's layer `after`), Duplicate Layer (a copy, CopyName,
	// right after it) and moving a slide's layer
	size_t NewSlide(size_t at, const std::string& name, const std::string& newLayer,
		bool shareBackground);
	size_t DuplicateSlide(size_t slide, const std::string& name, bool duplicateLayers,
		const std::string& newLayer, bool shareBackground, std::string& error);
	bool NewSlideLayer(size_t slide, size_t after, const std::string& name);
	bool DuplicateSlideLayer(size_t slide, size_t layer, std::string& error);
	bool MoveSlideLayer(size_t slide, size_t from, size_t to);
	// TPartPR::Write's block, 'prp+' to 'prp-'
	std::vector<uint8_t> WritePresentationBlock() const;
	void ComputeHiddenFrames();
	std::map<size_t, uint32_t> viewOverride;	// part -> view shown

	// editing: parts changed since loading (written by Save)
	std::set<size_t> dirtyParts;
	// parts whose frames changed (pages added or removed): their tails are
	// written anew; for word processing parts in `layoutRanges` the frames'
	// text ranges are the layout's, not mapped through the edits
	std::set<size_t> dirtyTails;
	std::set<size_t> layoutRanges;
	bool stylesChanged = false;		// the StyleMgr (written by Save)
	bool sheetsChanged = false;		// the SheetMgr (page setup, margins)
	bool factoriesChanged = false;	// the PartFactory Table (a sheet of a new kind)
	bool selectionsChanged = false;	// spreadsheet selections/names moved
	bool linksChanged = false;		// links added (the LinksMgr, written by Save)
	bool pensChanged = false;		// pens made, edited or deleted (the PenMgr)
	bool inkSetsChanged = false;	// its ink sets changed (the SetMgr)
	// TDoc +0x70 ("Document Misc"): bit 2 shows invisibles in text
	uint32_t documentFlags = 0;
	bool documentFlagsChanged = false;
	WPPart* EditableWordPart(size_t part);
	SSPart* EditableSpreadsheetPart(size_t part);
	// a graphics part to change (marked dirty: its 'GRAP' data is written
	// anew on save)
	GRPart* EditableGraphicsPart(size_t part);
	// writes the document (changed parts written, everything else copied)
	// and loads it again from `path`
	bool Save(const std::string& path, std::string& error);

	const WPPart* WordPart(size_t part) const;
	const GRPart* GraphicsPart(size_t part) const;
	const SSPart* SpreadsheetPart(size_t part) const;
	const IMPart* ImagePart(size_t part) const;
	const ChartPart* Chart(size_t part) const;
	// a chart part to change (marked dirty: its chart section is written
	// anew on save)
	ChartPart* EditableChart(size_t part);
	const TablePart* Table(size_t part) const;
	// the table part whose cells' text is part `textPart`, -1 if none
	int TableOfText(size_t textPart) const;
	// a part's frame by its number (ReadFramePtr: part, frame)
	const Frame* FrameOf(uint32_t part, uint32_t index) const;

	// The part's frames with their position in root coordinates (the
	// container chain resolved), in flow order.
	struct PlacedFrame {
		size_t part;
		const Frame* frame;
		float x, y, width, height;
		int depth;			// nesting level (root = 0)
		int index;			// the frame's number among its part's frames (a header or
							// footer frame comes once per page, the same number)
		int page = -1;		// the page it is on (from 0), -1 if not on a page
	};
	std::vector<PlacedFrame> PlacedFrames() const;

	// Graphics views show only some layers (TGRLayerInfo flag 0: visible;
	// presentations keep one view per slide): a shape layer of a graphics
	// part hidden in the view its portal shows, and frames placed by FRAM
	// shapes on such layers (and everything inside them).
	bool LayerHidden(size_t part, size_t layer) const;
	bool FrameHidden(size_t part, int index) const;

	// Views & Layers (Layers.cpp; TDialogLayers and TPartGR's commands).
	// A drawing part's active layer (TPartGR +0x94, GRPart values[0]), the
	// view its first portal shows (tails[part].portals[0].view), and
	// TGRLayerInfo's flags: 0 visible, 1 editable (TPartGR::SetActiveLayer
	// makes the active layer the only one, of those not locked), 2 locked,
	// 3 printable.
	size_t ActiveLayer(size_t part) const;
	uint32_t ShownView(size_t part) const;
	// TGRLayerSwitchCommand / TGRViewSwitchCommand; false if out of range
	// or unchanged
	bool SetActiveLayer(size_t part, size_t layer);
	bool ShowView(size_t part, uint32_t view);
	// TGRLayerNewOrDeleteCommand: New Layer (named CopyName of the last
	// layer's name, added last, in every view with flags {1,0,0,1,0}, made
	// active); Delete Layer (needs two layers or more and a layer without
	// 'FRAM' shapes, else false with `error` set: its shapes go, each view
	// drops its info and renumbers the later layers, the active layer
	// becomes layer - 1, or 0)
	bool NewLayer(size_t part);
	bool DeleteLayer(size_t part, size_t layer, std::string& error);
	// TGRViewNewOrDeleteCommand: New View (named CopyName of the last view's
	// name, every layer in it with flags {1,0,0,1,0}, added last, shown);
	// Delete View (needs two views or more: the portal shows view - 1, or
	// view 1 when deleting view 0, renumbered after the removal)
	bool NewView(size_t part);
	bool DeleteView(size_t part, uint32_t view);
	// TGRLayerRenameCommand / TGRViewRenameCommand; false if out of range,
	// empty or unchanged
	bool RenameLayer(size_t part, size_t layer, const std::string& name);
	bool RenameView(size_t part, uint32_t view, const std::string& name);
	// TGRLayerSetAttrCommand: flag `attr` (0..4) of the shown view's
	// `row`th layer info set to `on`; attr 2 (locked) also clears flag 1.
	// False if out of range or nothing changed.
	bool SetLayerFlag(size_t part, size_t row, int attr, bool on);

	// A named reference (formula names like "Products Table"): the
	// reference of that name in any part's REFT and its selection
	const Selection* NamedSelection(const std::string& name, int fromPart = -1) const;
	// the part and reference index a name refers to, seen from a formula in
	// `fromPart` (TDoc::FindLink): a link from that part first, then the
	// part's own references, then any part's
	bool FindName(const std::string& name, int fromPart, size_t& part, size_t& reference) const;

	// index of the root part (PROT/BROT), or -1
	int RootPart() const;

	// Pages (Pages.cpp): a paged root shows a PAGE part with one frame per
	// page (TPagePortal), each listing the frames on it (TPageFrame).
	int PagePart() const;			// -1 if not paged
	size_t PageCount() const;
	// TPagePortal::NewPage: a page after the last, at TPagedRootPart::
	// GetPaperPosition, with frames like the last page's for the header,
	// the footer, the sheet's drawing (showing the drawing's next page) and
	// the text columns (empty). False if the document isn't paged.
	bool AddPage();
	// TPagePortal::DeletePage of the last page and its frames; false if
	// there is only one page.
	bool RemoveLastPage();
	// the text range a word processing frame shows (TWPFrame +0x4c, +0x50),
	// as laid out
	void SetFrameText(size_t part, size_t frame, uint32_t start, uint32_t length);
	// The first sheet's margins (TLeafSheet::Margins: left, top, right,
	// bottom, from the paper to the printable rectangle).
	void Margins(float margins[4]) const;
	// Page Setup and Margins (TLeafSheet::PageSetup, TDocMarginsCommand):
	// the sheet's paper and printable rectangles; the pages and the frames
	// on them move to match (TPageFrame::RepositionChildFrames) and the
	// text is laid out again. False if nothing changed.
	bool SetPageGeometry(const float paper[4], const float printable[4]);
	// TPageFrame::RepositionChildFrames on every page: the header at the
	// top of the printable area, the footer at its bottom, the drawing over
	// it and the text columns between header and footer
	void RepositionPages();
	// Document > Document Size… for text (TDialogWPDocSize,
	// TWPSetDocSizeCmd): the pages across (TPagedRootPart +0x6c) and the
	// text part's Title Page (TPart +0x58 flag 8); setting them moves the
	// pages (TPagedRootPart::GetPaperPosition). False if nothing changed
	// or the document isn't paged.
	uint32_t PagesAcrossCount() const;
	bool TitlePage(size_t part) const;
	bool SetDocumentSize(size_t part, uint32_t across, bool titlePage);
	// for a drawing (TPartGR::GetPages, TGRSetDocSizeCommand with TPartGR::
	// SetPages): the pages its size spans across and down (the size less a
	// point over the first sheet's printable size, rounded up; at least 1),
	// and setting them: the size becomes the printable size times the
	// pages, the paged root as many pages across and pages in all
	// (TPart::NewDataSize). False if nothing changed.
	bool DrawingPages(size_t part, uint32_t& across, uint32_t& down) const;
	bool SetDrawingPages(size_t part, uint32_t across, uint32_t down);
	// for a spreadsheet (TSSSetDocSizeCommand::SetAndSave): the used range
	// becomes rows by columns from A1. False if nothing changed.
	bool SetSheetSize(size_t part, int rows, int columns);
	// Document > Insert Header / Insert Footer (TDocHeaderFooterCommand;
	// `which` 0 header, 1 footer): the sheet's header or footer part, -1 if
	// none; inserting makes a new text part in a paragraph style based on
	// "Header"/"Footer" with one frame every page shows; removing takes it
	// off the pages (the part stays, as the original keeps it for undo).
	// False if the document isn't paged or there's nothing to do.
	int HeaderPart(int which) const;
	bool InsertHeaderFooter(int which);
	bool RemoveHeaderFooter(int which);
	// Format > Columns… (TWPSetColumnsCmd, TColumnData::SetColumnCount): a
	// section of a text part gets `count` columns (1-10), equally wide with
	// 1/30 of the width between them; for the main text's first section
	// every page gets that many text frames (TPart::PaginateAllPortals(2):
	// the pages laid out anew). False if nothing changed.
	bool SetColumnCount(size_t part, size_t section, int count);
	// the header or footer frame's height (it grows with its text); the
	// pages' frames move to match. False if it didn't change.
	bool SetHeaderHeight(int which, float height);
	// A new style (TStyleManager::NewStyle) with its own properties; the
	// parent gets a user. The paragraph style named `name`, made (based on
	// "WP Default", with `properties`) if the document doesn't have it
	// (TWPPartFactory::InitStyles makes "Header", "Footer", "Footnote";
	// documents keep only those used).
	uint32_t NewStyle(uint32_t type, const std::string& name, uint32_t flags, uint32_t parent,
		const std::vector<Property>& properties = {});
	uint32_t NamedParagraphStyle(const std::string& name,
		const std::vector<Property>& properties = {});
	// A new text part (TWPPart::TWPPart: one empty paragraph in a style based
	// on `basedOn`, +0x98 `kind`: 1 footnote, 3 header or footer) with its
	// selection, its portal in part `container` and one frame there (flag 8:
	// it grows with its text). The part's index, -1 if it can't be made.
	int NewTextPart(uint16_t kind, uint32_t basedOn, uint32_t container, const float origin[2],
		const float extent[2], uint32_t frameStyle);
	// selections added since loading (records written at the end of the
	// SelectionMgr by Save)
	std::vector<std::vector<uint8_t>> addedSelections;
	// the shown sheet's main part (what Save As exports), or -1
	int MainPart() const;
	// the sheet the root part shows (TRootPart: its index in the SheetMgr)
	int CurrentSheet() const;
	// the sheets in use (TSheetMgr +0x24): the first ones; deleted sheets
	// follow them (TSheetMgr::SuspendSheet)
	size_t SheetCount() const;
	// Delete Sheet (TDocSheetNewOrDeleteCommand, TSheetMgr::SuspendSheet):
	// with another sheet left, the sheet moves past those in use (its parts
	// stay, as the original keeps them for Undo); the one before it (or
	// after it, for the first) is shown if it was. False if it can't be.
	bool DeleteSheet(size_t sheet, std::string& error);
	// TSheetMgr::MoveSheet (the SheetBar's tab dragged, 's00N'): sheet
	// `from` taken out and put back at `to`, among those in use; the shown
	// sheet stays shown. False if either isn't a sheet in use.
	bool MoveSheet(size_t from, size_t to);
	// TDocWindow::SetSheetPtr for a browse root (TDocWindow::NewRootFrame
	// makes a new root for the sheet): the shown sheet's main and graphics
	// parts lose their frames in the root, sheet `sheet`'s get frames of the
	// same size, the root frame points at them (TBrowseRootFrame) and the
	// root part names the sheet; the tails are written by Save. False (and
	// nothing changed) for a paged root or a sheet whose parts can't be
	// shown this way.
	bool ShowSheet(size_t sheet, std::string& error);
	// New Sheet… for a spreadsheet (TSheetMgr::NewSheet, TLeafSheet::
	// TLeafSheet): a new spreadsheet part (TPartSS::TPartSS) and its
	// drawing, without frames until the sheet is shown, their selections,
	// the styles they use (AddSheetStyles), a factory for kinds the document
	// hasn't had, and the sheet after the others, named `name` ("Sheet N"
	// for "", STR# 5, 24 with the count). Returns the sheet's index, or -1.
	int AddSpreadsheetSheet(const std::string& name, std::string& error)
		{ return AddSheet(Sig("SPRD"), name, error); }
	// The same for `kind` 'SPRD' or 'WORD' (TWPPart::TWPPart: one empty
	// paragraph, AddWordStyles; shown in browse view).
	int AddSheet(uint32_t kind, const std::string& name, std::string& error);
};

// CopyName: the name with its trailing number one higher ("Layer 1" ->
// "Layer 2", "View 9" -> "View 10"), or " 2" added ("Background" ->
// "Background 2"). Trailing digits and spaces are the number part: the
// base is the name up to them, the result "%s %d" of base and number + 1.
std::string CopyName(const std::string& name);

bool LoadDocument(const std::string& path, Document& doc, std::string& error);
// The same for a document's bytes (its path stays empty).
bool LoadDocumentBytes(std::vector<uint8_t> data, Document& doc, std::string& error);

// File > New for word processing (BlankDocument.cpp): the document the
// original makes for a new WORD sheet, laid out like its saved documents.
// `path` is empty and `name` is "untitled N".
bool CreateBlankDocument(Document& doc, std::string& error);
// File > New for spreadsheets: a SPRD sheet's document (an empty
// spreadsheet, rows 1-500 and columns A-T, its graphics part, a browse
// root), named the same way.
bool CreateBlankSpreadsheet(Document& doc, std::string& error);
// File > New for drawings: a GRFX sheet's document (an empty drawing,
// "Layer 1" and "View 1", framed on the page of a paged root, as the
// original's drawing documents have it), named the same way.
bool CreateBlankDrawing(Document& doc, std::string& error);
// File > New for presentations (TPartPRFactory::NewPart): a presentation
// the screen's size (`width` x `height`) with a "Background" layer and
// one slide, "untitled slide 1", with its own layer, in a browse root.
bool CreateBlankPresentation(Document& doc, std::string& error, float width = 800,
	float height = 600);

} // namespace gobe

#endif
