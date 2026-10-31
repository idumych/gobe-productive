// File > New for word processing: the document the original makes for a new
// WORD sheet (TApp::NewDoc: TDoc::TDoc, TSheetMgr::NewSheet, TLeafSheet,
// TWPPart::TWPPart(TDoc*)), written the way it saves documents. Values the
// decompiled code doesn't settle come from the saved documents, which agree
// on them (Resume for the records of the same kinds; most sections are the
// same bytes in all 44 documents).
#include "Document.h"

#include "DocWriter.h"
#include "Editing.h"

#include <algorithm>
#include <cstring>

namespace gobe {

namespace {

// the parts: the sheet's main part, its graphics part (TLeafSheet makes one
// when the main part isn't a drawing), the paged root and its page
enum { kWordPart = 0, kGraphicsPart = 1, kRootPart = 2, kPagePart = 3 };

// US Letter, 1 inch margins (TLeafSheet's paper and printable rectangles)
const float kPaper[4] = {0, 0, 612, 792};
const float kPrintable[4] = {72, 72, 540, 720};
const float kTextWidth = kPrintable[2] - kPrintable[0];
const float kTextHeight = kPrintable[3] - kPrintable[1];


uint32_t
Code(const char* s)
{
	return uint32_t(uint8_t(s[0])) << 24 | uint32_t(uint8_t(s[1])) << 16
		| uint32_t(uint8_t(s[2])) << 8 | uint8_t(s[3]);
}


std::vector<uint8_t>
Hex(const char* hex)
{
	std::vector<uint8_t> out;
	for (size_t i = 0; hex[i] != '\0' && hex[i + 1] != '\0'; i += 2) {
		char byte[3] = {hex[i], hex[i + 1], 0};
		out.push_back(uint8_t(strtoul(byte, nullptr, 16)));
	}
	return out;
}


// the 98-byte header of the documents (TArchiveHeader): identity, 'DOC!',
// the table of contents' offset (patched in), the type sizes and byte order
// test values, the version 2.0 file format 30 build 3, padding
std::vector<uint8_t>
Header()
{
	return Hex("474f4245205343""4f54542f30312f3032"
		"444f432100000000000000010000000100000002000000040000000400000008"
		"01021112131470""1eaab44603d55686ffd753"	// (36 documents: ...53, 8: ...52)
		"00020000001e00030000000000000000"
		"00000000000000000000000000000000");
}


// TPart::Write: the message (none), then (0 for roots and pages, 2 for
// parts shown in frames), the part's id, -1, 1, and its selection
void
PartHeader(DocWriter& w, uint32_t kind, uint32_t level, uint32_t id, uint32_t selection)
{
	w.Sig(Sig("PART"));
	w.Sig(kind);
	w.UInt32(0);
	w.UInt32(level);
	w.UInt32(id);
	w.UInt32(0xffffffff);
	w.UInt32(1);
	w.UInt32(selection);
}


// TFrame::Write (TShape: angle, origin, extent, style, shape; the part
// shown, its container, four values)
void
Frame(DocWriter& w, const float origin[2], float width, float height, uint32_t style,
	uint32_t part, uint32_t container)
{
	w.Sig(Sig("FRAM"));
	w.Float(0);
	w.Point(origin);
	w.Float(width);
	w.Float(height);
	w.UInt32(style);
	w.UInt32(0);
	w.UInt32(0xffffffff);
	w.UInt32(0xffffffff);
	w.UInt32(part);
	w.UInt32(container);
	for (int i = 0; i < 4; i++)
		w.UInt32(0);
}


// TPortal: the part it's shown in, (0, flags, frame count), the frames.
// Flags: 12 for the sheet's main part, 4 for its graphics part.
void
PortalStart(DocWriter& w, uint32_t shownIn, uint32_t flags)
{
	w.Sig(Sig("REFT"));
	w.UInt32(1);
	w.UInt32(0);		// no references
	w.Sig(Sig("RefT"));
	w.UInt32(1);		// one portal
	w.Sig(Sig("PORT"));
	w.UInt32(shownIn);
	w.UInt32(0);
	w.UInt32(flags);
	w.UInt32(1);
	w.UInt32(1);		// one frame
}


// the styles (see StyleMgr below)
enum {
	kDefaultStyle, kTextBackgroundStyle, kRunStyle, kFrameStyle, kTextFrameStyle,
	kDocumentStyle, kGRDefaultStyle, kBodyStyle, kWPDefaultStyle, kParagraphStyle
};


std::vector<uint8_t>
WordPart()
{
	// TWPPart::TWPPart(TDoc*): one empty paragraph in a new style based on
	// "Body", its run in a new style with no parent, one section
	DocWriter w;
	PartHeader(w, Sig("WORD"), 2, 1, 1);
	w.Sig(Sig("wp++"));
	w.UInt16(0);			// +0x98
	w.UInt16(1);			// +0x9a
	w.UInt32(0);			// +0x88: the text's length
	w.UInt32(0xffffffff);	// +0x90: no style
	w.UInt32(0);			// +0x8c
	w.UInt32(1);			// +0x94
	w.Sig(Sig("par+"));
	w.UInt32(1);
	w.UInt32(0);			// start
	w.UInt32(0);			// length
	w.UInt16(0);
	w.UInt32(kParagraphStyle);
	w.UInt32(1);			// one run
	w.UInt32(0);
	w.UInt32(kRunStyle);
	w.UInt32(0);			// no paragraph infos
	w.Sig(Sig("par-"));
	w.Sig(Sig("txt+"));
	w.Sig(Sig("txt-"));
	w.Sig(Sig("sct+"));		// TSection(0): one column, the whole frame
	w.UInt32(1);
	w.UInt32(0);
	w.UInt16(0);
	w.UInt16(1);
	w.Bool(false);
	w.UInt32(1);
	w.Float(0);
	w.Float(0);
	w.Float(1);
	w.Float(1);
	w.Sig(Sig("sct-"));
	w.Sig(Sig("wp--"));
	PortalStart(w, kPagePart, 12);
	Frame(w, kPrintable, kTextWidth, kTextHeight, kTextFrameStyle, kWordPart, kPagePart);
	// TWPFrame: the text it shows (none yet), three floats (the third is
	// the height the text fills: 0 for none), a float
	w.UInt32(0);
	w.UInt32(0);
	w.Float(0);
	w.Float(0);
	w.Float(0);
	w.Float(0);
	w.Sig(Sig("FraM"));
	w.Float(0);				// TWPPortal
	w.Sig(Sig("PorT"));
	w.Sig(Sig("ParT"));
	return w.Data();
}


std::vector<uint8_t>
GraphicsPart()
{
	// TPartGR: the grid, the drawing's size, one empty layer "Layer 1", one
	// view "View 1" showing it (as in the documents)
	DocWriter w;
	PartHeader(w, Sig("GRFX"), 2, 2, 0);
	w.Sig(Sig("GRAP"));
	w.Bytes(Hex("00000000" "41100000" "0000" "4974240049742400" "00000000000000000000000000000000" "00"));
	w.Sig(Sig("lyr+"));
	w.UInt32(1);
	w.String("Layer 1");
	w.UInt32(0);			// no shapes
	w.Sig(Sig("lyr-"));
	w.Sig(Sig("vew+"));
	w.UInt32(1);
	w.String("View 1");
	w.UInt32(1);			// it shows layer 0
	w.UInt32(0);
	w.Bytes(Hex("0101000100"));	// TGRLayerInfo flags
	w.Sig(Sig("vew-"));
	w.Sig(Sig("GraP"));
	PortalStart(w, kPagePart, 4);
	Frame(w, kPrintable, kTextWidth, kTextHeight, kFrameStyle, kGraphicsPart, kPagePart);
	float drawing[4] = {0, 0, kTextWidth, kTextHeight};
	w.Rect(drawing);		// TFrameGR
	w.Sig(Sig("FraM"));
	w.UInt32(0);			// TGRPortal: view 0
	w.Sig(Sig("PorT"));
	w.Sig(Sig("ParT"));
	return w.Data();
}


std::vector<uint8_t>
RootPart()
{
	DocWriter w;
	PartHeader(w, Sig("PROT"), 0, 3, 0xffffffff);
	w.UInt32(0);			// TRootPart: the sheet
	w.UInt32(kPagePart);	// TPagedRootPart: the page part
	w.UInt32(0);
	w.UInt32(1);			// one page
	w.Bool(true);
	PortalStart(w, 0xffffffff, 0);
	const float origin[2] = {0, 0};
	// the window's view (as the documents have it)
	Frame(w, origin, 609, 416, kFrameStyle, kRootPart, 0xffffffff);
	w.Float(0);				// TPagedRootFrame: a point
	w.Float(-0.0f);
	w.Sig(Sig("FraM"));
	w.Sig(Sig("PorT"));
	w.Sig(Sig("ParT"));
	return w.Data();
}


void
FramePtr(DocWriter& w, uint32_t part, uint32_t portal, uint32_t frame)
{
	w.UInt32(part);
	w.UInt32(portal);
	w.UInt32(frame);
}


std::vector<uint8_t>
PagePart()
{
	DocWriter w;
	PartHeader(w, Sig("PAGE"), 0, 4, 0xffffffff);
	w.UInt32(0);			// TPagePart
	PortalStart(w, kRootPart, 0);
	const float origin[2] = {0, 0};
	Frame(w, origin, kPaper[2], kPaper[3], kFrameStyle, kPagePart, kRootPart);
	// TPageFrame: the frames on the page: header, footer (none), the
	// sheet's drawing, the text
	w.UInt32(4);
	FramePtr(w, 0xffffffff, 0xffffffff, 0xffffffff);
	FramePtr(w, 0xffffffff, 0xffffffff, 0xffffffff);
	FramePtr(w, kGraphicsPart, 0, 0);
	FramePtr(w, kWordPart, 0, 0);
	w.Sig(Sig("FraM"));
	// TPagePortal: four values, the header and footer frames, two bools
	w.UInt32(0);
	w.UInt32(0);
	w.UInt32(1);
	w.UInt32(0);
	FramePtr(w, 0xffffffff, 0xffffffff, 0xffffffff);
	FramePtr(w, 0xffffffff, 0xffffffff, 0xffffffff);
	w.Bool(true);
	w.Bool(true);
	w.Sig(Sig("PorT"));
	w.Sig(Sig("ParT"));
	return w.Data();
}


std::vector<uint8_t>
StyleSection()
{
	// TDoc::TDoc: "GR Default" (from the app's styles) and a new style
	// based on it (the document's default, TDoc +0x6c); the WP part brings
	// "Text Background", "WP Default" and "Body" (TWPPartFactory::
	// InitStyles) and its paragraph and run styles; the frames' styles.
	// Ordered as the original indexes them.
	StyleMgr styles;
	auto add = [&](const char* type, const char* hex) {
		styles.properties.push_back({Code(type), Hex(hex)});
		return uint32_t(styles.properties.size() - 1);
	};
	const char* white = "01ffffffff77747700ffffffffffffffff";
	const char* black = "01000000ff77747700ffffffffffffffff";
	uint32_t arrows = add("Arrw", "0000");
	uint32_t fill = add("FCol", white);
	uint32_t fillOpaque = add("FTra", "3f800000");
	uint32_t pen = add("PCol", black);
	uint32_t penType = add("PEN ", "ffffffff4e4f524d0000000800044e6f6e650001");
	uint32_t penSize = add("PENz", "3f800000");
	uint32_t penOpaque = add("PTra", "3f800000");
	uint32_t textColor = add("TCol", black);
	uint32_t face = add("TFac", "0000000101");
	uint32_t font = add("TFon", "000e447574636838303120526d20425400000000");	// Dutch801 Rm BT
	uint32_t size = add("TSiz", "000c");
	uint32_t fillClear = add("FTra", "00000000");
	uint32_t penNone = add("PENz", "00000000");
	uint32_t penClear = add("PTra", "00000000");
	uint32_t lineSpacing = add("LSpa", "013f800000");
	uint32_t align = add("PAlg", "0000");
	uint32_t first = add("PInd", "00000000");
	uint32_t label = add("PLab", "0000000000000000");
	uint32_t left = add("PLef", "00000000");
	uint32_t right = add("PRig", "00000000");
	uint32_t after = add("SpaA", "0100000000");
	uint32_t before = add("SpaB", "0100000000");

	const uint32_t none = 0xffffffff;
	auto style = [&](const char* type, const char* name, uint32_t flags, uint32_t users,
			uint32_t parent, uint32_t apply, std::vector<uint32_t> props) {
		Style s;
		s.id = Sig("STYL");
		s.value4 = users + 1;	// and the manager
		s.value8 = flags;
		s.valueC = 0xffff;
		s.value10 = Code(type);
		s.name = name;
		s.links[0] = parent;
		s.links[1] = apply;
		s.links[2] = none;
		s.props = std::move(props);
		styles.styles.push_back(std::move(s));
	};
	// (users: child styles, paragraphs, runs, frames, TDoc +0x6c)
	style("Basi", "Default", 0x912, 2, none, none, {arrows, fill, fillOpaque, pen, penType,
		penSize, penOpaque, textColor, face, font, size});
	style("Basi", "Text Background", 0x822, 1, none, none,
		{fill, fillClear, pen, penNone, penClear});
	style("Basi", "", 0x20, 1, none, none, {});						// the run
	style("Basi", "", 0x20, 3, none, none,
		{fill, fillClear, pen, penNone, penClear, textColor});		// frames
	style("Basi", "", 0x20, 1, none, kTextBackgroundStyle, {textColor});	// the text frame
	style("Basi", "", 0x20, 1, kGRDefaultStyle, none, {});			// TDoc +0x6c
	style("GRFX", "GR Default", 0x822, 1, kDefaultStyle, none, {});
	style("Para", "Body", 0x822, 1, kWPDefaultStyle, none, {});
	style("Para", "WP Default", 0x912, 1, kDefaultStyle, none, {lineSpacing, align, first,
		label, left, right, after, before});
	style("Para", "", 0x20, 1, kBodyStyle, none, {});				// the paragraph
	// property lists in TPropsTable's order, and the union sets
	for (Style& s : styles.styles)
		s.props = [&]() {
			std::vector<uint32_t> p = s.props;
			std::stable_sort(p.begin(), p.end(), [&](uint32_t a, uint32_t b) {
				return styles.properties[a].type < styles.properties[b].type;
			});
			return p;
		}();
	for (int pass = 0; pass < 3; pass++)
		for (Style& s : styles.styles)
			s.props2 = StyleUnionSet(styles, s);
	styles.afterSection = kDocumentStyle;
	return WriteStyleMgr(styles);
}


std::vector<uint8_t>
SheetSection()
{
	SheetMgr sheets;
	sheets.sheetCount = 1;
	LeafSheet sheet;
	sheet.name = "Sheet 1";
	sheet.mainPart = kWordPart;
	sheet.graphicsPart = kGraphicsPart;
	sheet.pageView = true;
	std::copy(kPaper, kPaper + 4, sheet.paper);
	std::copy(kPrintable, kPrintable + 4, sheet.printable);
	sheet.flags[0] = sheet.flags[1] = sheet.flags[2] = true;
	sheet.divisions = 8;
	// no print settings until Page Setup is used (not ported yet)
	sheets.sheets.push_back(sheet);
	return WriteSheetMgr(sheets);
}


std::vector<uint8_t>
SelectionSection()
{
	// TSelectionMgr: the drawing's (no shapes) and the text's (no range)
	DocWriter w;
	w.Sig(Sig("SELM"));
	w.UInt32(2);
	w.UInt32(16);
	w.Sig(Sig("GRFX"));
	w.UInt32(kGraphicsPart);
	w.UInt32(0);
	w.UInt32(0);
	w.UInt32(18);
	w.Sig(Sig("WORD"));
	w.UInt32(kWordPart);
	w.UInt32(1);
	w.UInt32(0);
	w.Bool(false);
	w.Bool(false);
	w.Sig(Sig("SelM"));
	return w.Data();
}


std::vector<uint8_t>
PartSection()
{
	PartMgr mgr;
	mgr.parts.push_back({Sig("WORD"), 0, WordPart()});
	mgr.parts.push_back({Sig("GRFX"), 0, GraphicsPart()});
	mgr.parts.push_back({Sig("PROT"), 0, RootPart()});
	mgr.parts.push_back({Sig("PAGE"), 0, PagePart()});
	return WritePartMgrFromRecords(mgr);
}


std::vector<uint8_t>
WindowSection()
{
	// TDocWindow::Write: the sheet, the root frame, the zoom, three
	// values, the active part chain (the text), the window's frame, the
	// ribbons (as the documents have them)
	DocWriter w;
	w.UInt32(0);
	FramePtr(w, kRootPart, 0, 0);
	w.Float(1);
	w.UInt32(1);
	w.UInt32(0);
	w.UInt32(0);
	w.UInt32(1);
	w.UInt32(kWordPart);
	w.UInt32(0);
	const float frame[4] = {9, 28, 665, 584};
	w.Rect(frame);
	w.Bytes(Hex("0000000a00000003000000000001000000010100""00000a01000000080100000006010000"
		"000901574f52a80100000004000000000500"));
	return w.Data();
}


// #pragma mark - spreadsheets

// File > New for spreadsheets (TPartSS::TPartSS(TDoc*), TLeafSheet for a
// SPRD sheet): the sheet's spreadsheet, its graphics part and a browse
// root (a spreadsheet sheet isn't paged), as StockPrice, the smallest
// spreadsheet document, has them.
enum { kSheetPart = 0, kSheetGraphicsPart = 1, kBrowseRootPart = 2 };

// the styles of the spreadsheet document (see SheetStyleSection)
enum {
	kSSBaseStyle, kSSFrameStyle, kSSDocumentStyle, kSSGRDefaultStyle, kSSDefaultStyle,
	kGridStyle, kHeaderStyle, kColumnStyle, kRowStyle, kLockStyle
};

// the window's view, which the root frame and the sheet's frames fill
const float kViewWidth = 609;
const float kViewHeight = 416;


std::vector<uint8_t>
SheetPart()
{
	// TPartSS::TPartSS(TDoc*): rows 0-499 and columns A-T in use, the part's
	// style "SS Default", nothing locked, Auto Calculate on, no cells;
	// TSSDisplayManager: the preferences' row height and column width,
	// the grid, header, column, row and lock styles; TSSDisplayOptions:
	// grid, locked cells, no formulas, column and row headers, circular
	// references
	DocWriter w;
	PartHeader(w, Sig("SPRD"), 2, 1, 1);
	w.Sig(Sig("ss +"));
	w.UInt16(0);
	w.UInt16(0);
	w.UInt16(499);
	w.UInt16(19);
	w.UInt32(kSSDefaultStyle);
	w.UInt32(0);			// locked cells
	w.Bool(true);
	w.Sig(Sig("coli"));
	w.UInt32(0);
	w.Sig(Sig("rowi"));
	w.UInt32(0);
	w.Sig(Sig("data"));
	w.UInt16(0xffff);		// the end
	w.UInt16(0xffff);
	w.Sig(Sig("depm"));
	w.UInt32(0);			// no formulas
	w.UInt32(0);			// no links
	w.Sig(Sig("disp"));
	w.Float(16);
	w.Float(72);
	for (uint32_t style : {kGridStyle, kHeaderStyle, kColumnStyle, kRowStyle, kLockStyle})
		w.UInt32(style);
	w.Sig(Sig("dopt"));
	for (bool option : {true, true, false, true, true, true})
		w.Bool(option);
	w.UInt32(0);			// TSSFillManager: no fills
	for (int i = 0; i < 3; i++)
		w.UInt32(0xffffffff);
	w.Sig(Sig("ss -"));
	PortalStart(w, kBrowseRootPart, 12);
	const float origin[2] = {0, 0};
	Frame(w, origin, kViewWidth, kViewHeight, kSSFrameStyle, kSheetPart, kBrowseRootPart);
	// TSSFrame: the cells shown (TSSFrame::SetSize sets them again) and the
	// sheet's rectangle
	w.UInt16(0);
	w.UInt16(0);
	w.UInt16(uint16_t(kViewHeight / 16));
	w.UInt16(uint16_t(kViewWidth / 72));
	const float rect[4] = {0, 0, kViewWidth, kViewHeight};
	w.Rect(rect);
	w.Sig(Sig("FraM"));
	w.Sig(Sig("PorT"));
	w.Sig(Sig("ParT"));
	return w.Data();
}


std::vector<uint8_t>
SheetGraphicsPart()
{
	// as GraphicsPart, its frame inset by a point in the sheet's
	DocWriter w;
	PartHeader(w, Sig("GRFX"), 2, 2, 0);
	w.Sig(Sig("GRAP"));
	w.Bytes(Hex("00000000" "41100000" "0000" "4974240049742400" "00000000000000000000000000000000" "00"));
	w.Sig(Sig("lyr+"));
	w.UInt32(1);
	w.String("Layer 1");
	w.UInt32(0);			// no shapes
	w.Sig(Sig("lyr-"));
	w.Sig(Sig("vew+"));
	w.UInt32(1);
	w.String("View 1");
	w.UInt32(1);
	w.UInt32(0);
	w.Bytes(Hex("0101000100"));
	w.Sig(Sig("vew-"));
	w.Sig(Sig("GraP"));
	PortalStart(w, kBrowseRootPart, 4);
	const float origin[2] = {1, 1};
	Frame(w, origin, kViewWidth - 1, kViewHeight - 1, kSSFrameStyle, kSheetGraphicsPart,
		kBrowseRootPart);
	float drawing[4] = {0, 0, kViewWidth - 1, kViewHeight - 1};
	w.Rect(drawing);		// TFrameGR
	w.Sig(Sig("FraM"));
	w.UInt32(0);			// TGRPortal: view 0
	w.Sig(Sig("PorT"));
	w.Sig(Sig("ParT"));
	return w.Data();
}


std::vector<uint8_t>
BrowseRootPart()
{
	DocWriter w;
	PartHeader(w, Sig("BROT"), 0, 3, 0xffffffff);
	w.UInt32(0);			// TRootPart: the sheet
	PortalStart(w, 0xffffffff, 0);
	const float origin[2] = {0, 0};
	Frame(w, origin, kViewWidth, kViewHeight, kSSFrameStyle, kBrowseRootPart, 0xffffffff);
	// TBrowseRootFrame: the sheet's frame and its graphics frame
	FramePtr(w, kSheetPart, 0, 0);
	FramePtr(w, kSheetGraphicsPart, 0, 0);
	w.Sig(Sig("FraM"));
	w.Sig(Sig("PorT"));
	w.Sig(Sig("ParT"));
	return w.Data();
}


std::vector<uint8_t>
SheetStyleSection()
{
	// TDoc::TDoc: "Default", "GR Default" and the document's style based on
	// it; TPartSSFactory::AddPartStyles: "SS Default" (General, left,
	// bottom, 10 points, a clear fill); TSSDisplayManager: the grid (a light
	// gray hairline), header (gray, centered, the plain font at 10), column
	// and row (based on the header) and lock (red, patterned) styles; the
	// frames' style. Property values as StockPrice has them.
	StyleMgr styles;
	auto add = [&](const char* type, const char* hex) {
		styles.properties.push_back({Code(type), Hex(hex)});
		return uint32_t(styles.properties.size() - 1);
	};
	const char* white = "01ffffffff77747700ffffffffffffffff";
	const char* black = "01000000ff77747700ffffffffffffffff";
	const char* pen = "ffffffff4e4f524d0000000800044e6f6e650001";
	uint32_t arrows = add("Arrw", "0000");
	uint32_t fill = add("FCol", white);
	uint32_t fillOpaque = add("FTra", "3f800000");
	uint32_t penColor = add("PCol", black);
	uint32_t penType = add("PEN ", pen);
	uint32_t penSize = add("PENz", "3f800000");
	uint32_t penOpaque = add("PTra", "3f800000");
	uint32_t textColor = add("TCol", black);
	uint32_t face = add("TFac", "0000000101");
	uint32_t font = add("TFon", "000e447574636838303120526d20425400000000");	// Dutch801 Rm BT
	uint32_t size = add("TSiz", "000c");
	uint32_t fillClear = add("FTra", "00000000");
	uint32_t penNone = add("PENz", "00000000");
	uint32_t penClear = add("PTra", "00000000");
	uint32_t left = add("HAln", "0000");
	uint32_t bottom = add("VAln", "0003");
	uint32_t general = add("SSNm", "000747656e6572616c00");
	uint32_t size10 = add("TSiz", "000a");
	uint32_t gridColor = add("PCol", "01c8c8c8ff77747700ffffffffffffffff");
	uint32_t headerFill = add("FCol", "01d8d8d8ff77747700ffffffffffffffff");
	uint32_t centered = add("HAln", "0002");
	uint32_t headerFont = add("TFon", "000a5377697337323120425400000000");	// Swis721 BT
	uint32_t lockFill = add("FCol", "01ff0000ff777477008800220088002200");

	const uint32_t none = 0xffffffff;
	auto style = [&](const char* type, const char* name, uint32_t flags, uint32_t users,
			uint32_t parent, std::vector<uint32_t> props) {
		Style s;
		s.id = Sig("STYL");
		s.value4 = users + 1;
		s.value8 = flags;
		s.valueC = 0xffff;
		s.value10 = Code(type);
		s.name = name;
		s.links[0] = parent;
		s.links[1] = none;
		s.links[2] = none;
		s.props = std::move(props);
		styles.styles.push_back(std::move(s));
	};
	// (users: child styles, the part, its display manager, frames, TDoc
	// +0x6c)
	style("Basi", "Default", 0x912, 3, none, {arrows, fill, fillOpaque, penColor, penType,
		penSize, penOpaque, textColor, face, font, size});
	style("Basi", "", 0x20, 3, kSSBaseStyle, {fillClear, penNone, penClear});	// frames
	style("Basi", "", 0x20, 1, kSSGRDefaultStyle, {});					// TDoc +0x6c
	style("GRFX", "GR Default", 0x822, 1, kSSBaseStyle, {});
	style("SPRD", "SS Default", 0x912, 1, kSSBaseStyle, {fillClear, left, general, size10,
		bottom});
	style("SPRD", "GridStyle", 0x822, 1, none, {gridColor, penType, penNone, penOpaque});
	style("SPRD", "HeaderStyle", 0x822, 3, none, {headerFill, centered, penColor, penType,
		penNone, penOpaque, headerFont, size10});
	style("SPRD", "ColumnStyle", 0x822, 1, kHeaderStyle, {});
	style("SPRD", "RowStyle", 0x822, 1, kHeaderStyle, {});
	style("SPRD", "LockStyle", 0x822, 1, none, {lockFill});
	for (Style& s : styles.styles)
		std::stable_sort(s.props.begin(), s.props.end(), [&](uint32_t a, uint32_t b) {
			return styles.properties[a].type < styles.properties[b].type;
		});
	for (int pass = 0; pass < 3; pass++)
		for (Style& s : styles.styles)
			s.props2 = StyleUnionSet(styles, s);
	styles.afterSection = kSSDocumentStyle;
	return WriteStyleMgr(styles);
}


std::vector<uint8_t>
SheetSheetSection()
{
	SheetMgr sheets;
	sheets.sheetCount = 1;
	LeafSheet sheet;
	sheet.name = "Sheet 1";
	sheet.mainPart = kSheetPart;
	sheet.graphicsPart = kSheetGraphicsPart;
	std::copy(kPaper, kPaper + 4, sheet.paper);
	std::copy(kPrintable, kPrintable + 4, sheet.printable);
	sheet.flags[0] = sheet.flags[1] = sheet.flags[2] = true;
	sheet.divisions = 8;
	sheets.sheets.push_back(sheet);
	return WriteSheetMgr(sheets);
}


std::vector<uint8_t>
SheetSelectionSection()
{
	// the drawing's (no shapes) and the sheet's (A1, TSSSelection 'CELL')
	DocWriter w;
	w.Sig(Sig("SELM"));
	w.UInt32(2);
	w.UInt32(16);
	w.Sig(Sig("GRFX"));
	w.UInt32(kSheetGraphicsPart);
	w.UInt32(0);
	w.UInt32(0);
	w.UInt32(33);
	w.Sig(Sig("SPRD"));
	w.UInt32(kSheetPart);
	w.UInt32(1);
	w.Sig(Sig("CELL"));
	w.UInt16(0);
	w.UInt16(0);
	w.UInt32(1);
	for (int i = 0; i < 4; i++)
		w.UInt16(0);
	w.Bool(true);
	w.Sig(Sig("SelM"));
	return w.Data();
}


std::vector<uint8_t>
SheetWindowSection()
{
	// TDocWindow::Write as StockPrice has it: the sheet, the root frame, the
	// zoom, three values, the active part chain, the window's frame, the
	// spreadsheet's ribbons
	DocWriter w;
	w.UInt32(0);
	FramePtr(w, kBrowseRootPart, 0, 0);
	w.Float(1);
	w.UInt32(1);
	w.UInt32(0);
	w.UInt32(0);
	w.Bytes(Hex("00000000" "00000001" "00000000" "ffffffff"));
	const float frame[4] = {9, 28, 665, 584};
	w.Rect(frame);
	w.Bytes(Hex("0000000a00000003000000000001000000010100""00000a01000000080000000006010000"
		"000901535052a80100000004000000000500"));
	return w.Data();
}


// "untitled", "untitled 2", ... (TApp::UntitledDoc); the app counts
int sUntitled = 0;


// the document from its sections, loaded
bool
LoadSections(Document& doc, std::string& error,
	const std::vector<std::pair<const char*, std::vector<uint8_t>>>& sections)
{
	DocWriter out;
	std::vector<TocEntry> toc;
	out.Bytes(Header());
	for (const auto& section : sections) {
		toc.push_back({section.first, uint32_t(out.Position()), 1});
		out.Bytes(section.second);
	}
	uint32_t tocOffset = uint32_t(out.Position());
	out.UInt32(uint32_t(toc.size()));
	for (const TocEntry& e : toc) {
		out.String(e.name);
		out.UInt32(e.offset);
		out.UInt32(e.kind);
	}
	out.Sig(Sig("END!"));
	std::vector<uint8_t>& data = out.Data();
	for (int i = 0; i < 4; i++)
		data[0x14 + i] = (tocOffset >> (24 - 8 * i)) & 0xff;

	doc = Document();
	if (!LoadDocumentBytes(std::move(data), doc, error)) {
		error = "create blank document: " + error;
		return false;
	}
	sUntitled++;
	doc.name = sUntitled == 1 ? "untitled" : "untitled " + std::to_string(sUntitled);
	return true;
}

} // namespace


bool
CreateBlankDocument(Document& doc, std::string& error)
{
	return LoadSections(doc, error, {
		{"VersionMgr", Hex("5645524d0000000100020000001e000300000000000000005665724d")},
		{"PartFactory Table", WriteFactoryTable({
			{Sig("WORD"), 1, "Word Processing"},
			{Sig("GRFX"), 1, "Graphics"},
			{Sig("PROT"), 1, "PagedRootPart"},
			{Sig("PAGE"), 1, "PagePart"}})},
		{"Document Misc", Hex("00000001000000000000000000000000000000000000000000000000")},
		{"SetMgr", Hex("5345544d0000000000000000000000005365744d")},
		{"PenMgr", Hex("50454e4d0000000601000000000050656e4d")},
		{"StyleMgr", StyleSection()},
		{"SheetMgr", SheetSection()},
		{"SelectionMgr", SelectionSection()},
		{"LinksMgr", Hex("4c494e4b00000000004c696e4b")},
		{"PartMgr", PartSection()},
		{"CommandMgr", Hex("434d444d00000000436d644d")},
		{"HistoryMgr", Hex("000000000000000000000014")},
		{"WindowMgr", WindowSection()}});
}


bool
CreateBlankSpreadsheet(Document& doc, std::string& error)
{
	PartMgr parts;
	parts.parts.push_back({Sig("SPRD"), 0, SheetPart()});
	parts.parts.push_back({Sig("GRFX"), 0, SheetGraphicsPart()});
	parts.parts.push_back({Sig("BROT"), 0, BrowseRootPart()});
	return LoadSections(doc, error, {
		{"VersionMgr", Hex("5645524d0000000100020000001e000300000000000000005665724d")},
		{"PartFactory Table", WriteFactoryTable({
			{Sig("SPRD"), 1, "Spreadsheet"},
			{Sig("GRFX"), 1, "Graphics"},
			{Sig("BROT"), 1, "BrowseRootPart"}})},
		{"Document Misc", Hex("00000001000000000000000000000000000000000000000000000000")},
		{"SetMgr", Hex("5345544d0000000000000000000000005365744d")},
		{"PenMgr", Hex("50454e4d0000000601000000000050656e4d")},
		{"StyleMgr", SheetStyleSection()},
		{"SheetMgr", SheetSheetSection()},
		{"SelectionMgr", SheetSelectionSection()},
		{"LinksMgr", Hex("4c494e4b00000000004c696e4b")},
		{"PartMgr", WritePartMgrFromRecords(parts)},
		{"CommandMgr", Hex("434d444d00000000436d644d")},
		{"HistoryMgr", Hex("000000000000000000000014")},
		{"WindowMgr", SheetWindowSection()}});
}

} // namespace gobe
