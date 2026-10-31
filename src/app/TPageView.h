// TPageView: draws a document as its pages and frames (the root part's
// frames, page parts, and the frames of the parts placed on them), with the
// word processing parts' text laid out and flowed through their frames.
// Spreadsheet parts show their cells with values computed by SSEvaluator;
// image parts their bitmap, clock parts an analog clock, charts a 2D
// rendition of their cached data (the original renders 3D with OpenGL).
// Parts that aren't ported yet are drawn as labelled outlines.
#ifndef GOBE_TPAGEVIEW_H
#define GOBE_TPAGEVIEW_H

#include <Bitmap.h>
#include <Font.h>
#include <Gradient.h>
#include <PopUpMenu.h>
#include <TextView.h>
#include <View.h>

#include <array>
#include <map>
#include <memory>
#include <set>
#include <vector>

#include "Document.h"
#include "Editing.h"
#include "SSEval.h"
#include "SSMove.h"
#include "SSStyle.h"
#include "StyleEdit.h"
#include "Styles.h"
#include "Undo.h"

namespace gobe {

class TWPRuler;

// sent to the window when the slide changes ("slide" int32, "name")
static const uint32 kMsgSlideChanged = 'Gsld';
// sent to the window when the document was edited
static const uint32 kMsgDocumentChanged = 'Gchg';
// the page view to its window: the active part's ribbon to show or hide
static const uint32 kMsgActiveRibbon = 'Grib';
// test hook: run "script" (RunScript) in the window
static const uint32 kMsgRunScript = 'Grun';

class TPageView : public BView {
public:
	TPageView(Document& document);

	virtual void AttachedToWindow();
	virtual void Draw(BRect updateRect);
	virtual void KeyDown(const char* bytes, int32 count);
	virtual void MouseDown(BPoint where);
	virtual void MouseMoved(BPoint where, uint32 transit, const BMessage* dragMessage);
	virtual void MouseUp(BPoint where);
	virtual void MessageReceived(BMessage* message);

	// after the document was saved (and loaded again) or reverted
	void DocumentReloaded();
	// after saving or reverting
	void DocumentSaved();
	void ClearUndo();
	// test hook: scripted clicks, keys and saves (see TPageEdit.cpp)
	void RunScript(const std::string& script, Document& document);
	void Click(BPoint where, int32 clicks, int32 buttons);
	virtual void GetPreferredSize(float* width, float* height);
	virtual BSize MinSize();
	virtual BSize PreferredSize();

	static bool CanShow(const Document& document);

	// presentations: arrows, Page Up/Down, space, Home/End change slides
	void ShowSlide(int index);
	// the SheetBar's tab chosen: that sheet shown (Document::ShowSheet), an
	// undo step unless `step` is false (part of another); false if it can't be
	bool ShowSheet(int index, bool step = true);
	// the document shows another sheet (Delete Sheet): nothing selected,
	// the frames again
	void SheetShown();

	// text editing: the caret's position in a word processing part
	struct Caret {
		bool active = false;
		size_t part = 0, paragraph = 0, offset = 0;
	};

	// text editing state (public for TDocWindow)
	void TextChanged();
	// TStyleManager::DoPasteStyle: copied or imported styles into the
	// document; a copied one with a style's name replaced, renamed or
	// skipped (TDialogStyleReplaceRename) unless `replace`
	void PasteStyleDefinitions(std::vector<StyleDefinition> styles, bool replace);
	// Edit Styles' File > Export Styles… and Import Styles…
	// (TStyleManager::DoExportStyles, DoImportStyles)
	void ExportStyles(BMessage* message);
	void ImportStyles(BMessage* message);
	// the page geometry changed (Page Setup, Margins): frames, layout
	void GeometryChanged();
	// the menu commands MessageReceived handles (the window passes them on)
	static bool HandlesCommand(uint32 what);
	// TSSFillCommand::IsLocked (and its alert)
	bool FillLocked(FillKind kind, int r0, int c0, int r1, int c1);
	bool RangeLocked(int r0, int c0, int r1, int c1);
	// TPartSS::SetUpContextMenu
	BPopUpMenu* CellContextMenu(size_t part, int row, int column);
	// TParser::ParseReference for Go To Cell
	bool ParseCellReference(size_t part, const std::string& text, uint16& row,
		uint16& column) const;
	// Document > Insert/Remove Header (0) or Footer (1)
	void ToggleHeaderFooter(int which);
	// the width of the frame the caret is in (TWPPart::RulerBounds)
	float CaretFrameWidth() const;
	// the text ruler (TWPRuler::setRulerSize): the edges of the text the
	// caret is in and the page it is on, in view coordinates; false if no
	// text has the caret
	bool RulerFrame(BRect& text, BRect& page) const;
	// the ruler's changes to the selected paragraphs (TParFormatCmd): their
	// tabs at `removeTabs` taken off, then `add` set; `newStep` starts an
	// undo step called `label`
	// TPart::CreateFormatCommand: while Edit Styles has a style chosen
	// (TDoc::EditingStyle), formatting changes that style (TFormatCommand),
	// else the text; EditStyle is that style or 0xffffffff
	uint32_t EditStyle() const;
	bool ApplyCharacter(TextPosition from, TextPosition to, const Property& property);
	bool ApplyParagraph(size_t first, size_t last, const Property& property);
	bool RemoveTab(size_t first, size_t last, float position);
	void FormatParagraphs(const std::vector<float>& removeTabs,
		const std::vector<Property>& add, const std::string& label, bool newStep);
	// Document > Page View (TDocWindow::PageView, SetPageView)
	bool PageView() const { return !fBrowse; }
	void SetPageView(bool on);
	// the zoom (TDocWindow::Scale, 1/64 to 64)
	float Zoom() const { return fZoom; }
	void SetZoom(float zoom);
	// the window's text ruler (updated as the view draws), or NULL
	TWPRuler* fRuler = NULL;
	class TRibbon* fPartBar = NULL;
	class TRibbon* fFormatBar = NULL;
	class TEntryBar* fEntryBar = NULL;	// a spreadsheet's (TEntryBar)
	bool fCellsActive = false;	// what the window last heard (kMsgActiveRibbon)
	class TPageBar* fPageBar = NULL;
	class TRibbon* fSpellBar = NULL;
	// TDocWindow (TPageBar::SetPageNumbers): the pages in view, from 1
	// (-1 for none)
	void VisiblePages(int32& first, int32& last) const;
	// Document > Fit to Window (TWPPart::FitToWindow): the zoom that shows
	// a page with 20 points around it
	float FitZoom() const;
	const Document& Doc() const { return fDocument; }
	// the ToolBar's tool (TDocWindow +0x1c0): 'ARRW' or a shape tool
	// (Drawing.h); a shape tool's shape is dragged out in a drawing
	uint32 Tool() const { return fTool; }
	void SetTool(uint32 tool);
	class TRibbon* fToolBar = NULL;
	// Show Invisibles: the marks on line `index`
	void DrawInvisibles(size_t index);
	// printing: page `index`'s area the printer prints (`printable` is the
	// job's PrintableRect on the paper), in view coordinates
	BRect PageArea(int32 index, BRect printable) const;
	bool TextSelection(TextPosition& from, TextPosition& to) const;
	// TWPTextIterator::SelectText (TTextAccessor: and
	// TDocWindow::ScrollActiveOntoScreen): [from, to) of a paragraph of
	// text part `part` selected, in view
	void SelectText(size_t part, TextPosition from, size_t to);
	// TWPTextIterator::ReplaceText (TWPReplaceTextCmd "Replace Text"):
	// [from, to) replaced by `text`, which is selected
	void ReplaceText(size_t part, TextPosition from, size_t to, const std::string& text);
	// the same by positions in the part's text (PartPosition), across
	// paragraphs too
	void SelectRange(size_t part, uint32_t start, uint32_t end);
	void ReplaceRange(size_t part, uint32_t start, uint32_t end, const std::string& text);
	// the caret's line scrolled into view
	void ScrollCaretIntoView();
	// Edit > Copy Format has kept a format (OurApp +0x184 not empty)
	static bool HasCopiedFormat();
	// cells copied as cells (Paste Special has something to paste)
	static bool HasCopiedCells();
	// Show Clipboard (TClipboard, a document of its own): a new document
	// holding what was copied last (the cells, else the text); false if
	// nothing was
	static bool MakeClipboardDocument(Document& doc);
	// TWPPart::SetUpContextMenu: the menu for a context click at `where`
	// (document coordinates) in text (NULL elsewhere); the caret moves
	// there unless it's in the selection
	BPopUpMenu* ContextMenu(BPoint where);
	Caret fCaret;
	Caret fAnchor;
	UndoStack fUndo;
	bool fShowUndoSteps = false;	// THistory +0x1d (History's Options)
	UndoStack::ViewState CurrentViewState() const;
	// test hooks used by RunScript
	TextStyle StyleAt(TextPosition at) const;
	std::pair<TextPosition, TextPosition> TextSelectionRange() const;
	// the caret's x position in view coordinates (public for tab menu)
	float CaretX() const;

	// spreadsheet editing state (public for TDocWindow)
	struct {
		bool active = false;
		size_t part = 0;
		int row = 0, column = 0;
		int anchorRow = 0, anchorColumn = 0;
		// TSSSelection's kind: cells, or whole rows or columns (chosen by
		// their headings; they span the sheet, row and column 0 active)
		SelectionKind kind = SelectionKind::cells;
		// more ranges (TSSSelection's TCellsList): Command-click adds the
		// range so far here and starts another
		std::vector<SSRange> extra;
	} fSelectedCell;
	// the selected ranges: the extra ones, then the one being made
	std::vector<SSRange> SelectedRanges() const;
	// TPartSS::IsLocked for every selected range (and its alert)
	bool SelectionLocked();
	// the active cell's appearance (its part's, column's, row's and own
	// styles); false without cells selected
	bool ActiveCellStyle(CellStyle& style) const;
	// a FormatBar property for the selected cells (TSSFormatCommand on the
	// selection): false without cells selected (and no text caret)
	bool FormatCells(const Property& property, const char* label);
	// TPartSS::FlushEntryBar: the EntryBar's text in the active cell
	bool FlushEntryBar(const char* text);
	// the copied cells pasted at the active cell (TSSPasteCommand)
	void PasteCopiedCells(bool data, bool formulas, bool formatting);
	// the selection's name for the PartBar's address: the active cell, or
	// the ranges ("A1..B3,D4")
	std::string SelectionName() const;
	// TStyleManager::CountProperty for the alignment menus: the selected
	// cells' horizontal (0-3) and vertical (1-3) alignment and wrap (0/1)
	// if they all have the same, else -1
	void SelectedCellsAlignment(int32& h, int32& v, int32& wrap) const;
	BTextView* fCellEditor = NULL;
	BRect fBounds;					// everything, in document coordinates
	float fMargin;
	std::unique_ptr<SSBook> fBook;	// spreadsheet values

private:
	// the drawing tools (TPageDraw.cpp): a shape dragged out, moved or
	// resized, the shape selected
	struct ShapeRef {
		bool active = false;
		size_t part = 0, layer = 0, index = 0;
	};
	struct ShapeDrag {
		int mode = 0;			// 1 creating, 2 moving, 3 resizing
		size_t part = 0;
		float x = 0, y = 0;		// the drawing's origin in the view
		std::vector<std::array<float, 2>> points;	// creating: drawing coordinates
		float start[2] = {0, 0}, now[2] = {0, 0};
		float bounds[4] = {0, 0, 0, 0};
		int handle = -1;
	};
	// a drawing frame under `where` (the topmost), its part and origin
	bool DrawingAt(BPoint where, size_t& part, float& x, float& y) const;
	bool DrawingClick(BPoint where, int32 clicks);
	bool DrawingMoved(BPoint where);
	bool DrawingUp(BPoint where);
	bool DrawingKey(const char* bytes, int32 count);
	void FinishShape();
	void DrawShapeSelection(float dx, float dy);
	uint32 fTool = 0x41525257;	// 'ARRW'
	ShapeRef fShape;
	ShapeDrag fShapeDrag;
	struct FrameGeometry {
		BRect rect;					// in view coordinates, unrotated
		float angle;				// degrees, counterclockwise, about the center
		BRect clip;					// the containing frames (document coordinates)
		int index;					// the frame's number in its part
		int page;					// the page it is on (-1: none)
	};
	// what laying out a flow found: text left over, the last frame used,
	// where each frame's text starts (UINT32_MAX: no text)
	struct LayoutResult {
		bool overflow = false;
		int lastFrame = -1;
		std::vector<uint32_t> starts;
	};
	void RebuildFrames();
	const Document::PlacedFrame* CaretFrame() const;
	float fZoom = 1;
	bool fBrowse = false;			// browse view: no pages
	gobe::Frame fBrowseFrame;		// its frame for the main text
	BRect fDrawBounds;				// what Draw shows, in the document's coordinates
	// the port's room between a frame's edges and its text
	static constexpr float kTextPad = 2;
	bool Paginate(const LayoutResult& main);
	// styles resolved during a layout (LayoutText starts afresh)
	const std::pair<TextStyle, BFont>& CachedStyle(uint32_t style);
	std::map<uint32_t, std::pair<TextStyle, BFont>> fStyleCache;
	// footnote and endnote parts (laid out under their references' lines,
	// not as frames of their own)
	std::set<size_t> fNoteParts;
	// lines drawn with the text: the footnotes' separators (TWPFrame::
	// DrawContent)
	struct Rule {
		BPoint from, to;
		float width;
		BRect clip;					// document coordinates
	};
	std::vector<Rule> fRules;

	struct Line {
		BRect frame;				// in view coordinates
		float baseline;
		float angle;				// of the frame it is in
		BPoint center;				// rotation center
		BRect clip;					// document coordinates
		BRect textFrame;			// the text frame it is in (view coordinates)
		// the text it shows: part, paragraph, byte offsets [from, to)
		size_t part = 0, paragraph = 0, from = 0, to = 0;
		struct Piece {
			std::string text;
			BFont font;
			rgb_color color;
			float x;				// relative to the line
			size_t start = SIZE_MAX, end = SIZE_MAX;	// in the paragraph (none: tab leader)
			// a field's character (0x04): its paragraph info kind ('FORM',
			// 'PAGE', 'LINE'); the text shows what the field shows, a line
			// (TMiscLineInfo) is drawn from x to `ruleEnd`
			uint32_t field = 0;
			float ruleEnd = 0;
			float dy = 0;			// superscript, subscript: off the baseline
		};
		std::vector<Piece> pieces;
	};

	struct SheetGeometry {
		std::vector<float> xs, ys;	// column/row edges (one more than shown)
		int firstRow = 0, firstColumn = 0;
		float headerHeight = 0, headerWidth = 0;
	};
	SheetGeometry SheetLayout(const Document::PlacedFrame& frame, BRect rect) const;

	bool HitText(BPoint where, Caret& caret) const;
	bool CaretLine(const Caret& caret, size_t& line, float& x) const;
	void EditText(const char* bytes, int32 count);
	void MoveCaretVertically(int direction);
	void MoveCaretByPage(int direction);
	void UpdateMarks();
	void DrawCaret();
	// the x of a byte offset in that line
	float OffsetX(size_t lineIndex, size_t offset) const;
	bool DeleteSelection();
	void CopySelection();
	void PasteText();
	void SelectAllText();
	void DrawSelection();
	// spreadsheet editing
	bool HitCell(BPoint where, size_t& part, int& row, int& column, BRect& rect) const;
	// a spreadsheet's headings at `where` (TSSDisplayManager::HandleClick):
	// the corner both meet in (index -1), a column heading (kind columns,
	// its column) or a row heading (rows, its row)
	bool HitHeading(BPoint where, size_t& part, SelectionKind& kind, int& index) const;
	// TSSFrame::ColumnResizeClick / RowResizeClick: within 2 of a heading's
	// right (bottom) edge; `start` is that column's (row's) left (top) edge
	// and `offset` where the click is from its right (bottom) edge
	bool HitResize(BPoint where, size_t& part, bool& columns, int& index, float& start,
		float& offset) const;
	// the innermost spreadsheet frame at `where` and its view rect
	const Document::PlacedFrame* SheetFrameAt(BPoint where, BRect& rect) const;
	// TSSDisplayManager::ColumnClick / RowClick: the whole column (row)
	// `index` selected, or the selection of columns (rows) extended to it
	void SelectLines(size_t part, SelectionKind kind, int index, bool extend);
	void BeginCellEdit(size_t part, int row, int column, BRect rect);
	void EndCellEdit(bool commit, int moveRows = 0, int moveColumns = 0);
	BRect CellViewRect(size_t part, int row, int column) const;

	void Rebuild();
	void LayoutText();
	void DrawFrame(const Document::PlacedFrame& frame, BRect rect, uint32_t kind,
		float dx, float dy);
	void DrawLines(BRect updateRect, float dx, float dy);
	void DrawGraphics(const Document::PlacedFrame& frame, float dx, float dy);
	void DrawFrameBackground(const Document::PlacedFrame& frame, BRect rect);
	void DrawPageGuides(const Document::PlacedFrame& frame, BRect rect);
	void DrawSpreadsheet(const Document::PlacedFrame& frame, BRect rect);
	void DrawImage(const Document::PlacedFrame& frame, BRect rect);
	void DrawClock(const Document::PlacedFrame& frame, BRect rect);
	void DrawChart(const Document::PlacedFrame& frame, BRect rect);
	void DrawTable(const Document::PlacedFrame& frame, BRect rect);
	void FillInk(BRect rect, const Ink& ink, float opacity, BRect inkRect);
	const CellStyle& SpreadsheetStyle(const std::vector<uint32_t>& stack);
	bool SetInk(const Ink& ink, float opacity, BRect rect, BGradient** gradient);
	LayoutResult LayoutPart(size_t part, const std::vector<FrameGeometry>& frames);
	static BRect RotatedBounds(BRect rect, float angle);
	// the area a frame may draw in: its containers' frames intersected
	BRect ContainerClip(const Document::PlacedFrame& frame) const;
	// undo: view state helpers
	void RestoreViewState(const UndoStack::ViewState& v);

	Document& fDocument;
	std::vector<Document::PlacedFrame> fFrames;
	std::vector<Line> fLines;
	std::map<std::vector<uint32_t>, CellStyle> fCellStyles;
	std::map<size_t, std::unique_ptr<BBitmap>> fImages;
	bool fDragging = false;
	bool fDraggingCells = false;	// cell range: drag from first click
	bool fDraggingHeadings = false;	// whole rows or columns: the same
	// TSSDisplayManager::ResizeColumn / ResizeRow: a heading's edge dragged
	struct {
		bool active = false;
		size_t part = 0;
		bool columns = true;
		std::vector<std::pair<int, int>> lines;	// the columns (rows) sized
		float start = 0, offset = 0, size = 0;
	} fResizing;
	int fCursorShown = 0;	// 0 the default, 1 resizing columns, 2 rows
	bool fScriptShift = false;		// RunScript: shift held
	bool fScriptCommand = false;	// RunScript: Command held
	uint32 fScriptModifiers = 0;	// RunScript: Command, Option, Control held
};

} // namespace gobe

#endif
