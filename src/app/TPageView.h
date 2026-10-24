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
#include <TextView.h>
#include <View.h>

#include <map>
#include <memory>
#include <vector>

#include "Document.h"
#include "Editing.h"
#include "SSEval.h"
#include "SSStyle.h"
#include "Styles.h"
#include "Undo.h"

namespace gobe {

// sent to the window when the slide changes ("slide" int32, "name")
static const uint32 kMsgSlideChanged = 'Gsld';
// sent to the window when the document was edited
static const uint32 kMsgDocumentChanged = 'Gchg';

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

	// text editing: the caret's position in a word processing part
	struct Caret {
		bool active = false;
		size_t part = 0, paragraph = 0, offset = 0;
	};

	// text editing state (public for TDocWindow)
	void TextChanged();
	// the page geometry changed (Page Setup, Margins): frames, layout
	void GeometryChanged();
	// printing: page `index`'s area the printer prints (`printable` is the
	// job's PrintableRect on the paper), in view coordinates
	BRect PageArea(int32 index, BRect printable) const;
	bool TextSelection(TextPosition& from, TextPosition& to) const;
	Caret fCaret;
	Caret fAnchor;
	UndoStack fUndo;
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
	} fSelectedCell;
	BTextView* fCellEditor = NULL;
	BRect fBounds;					// everything, in document coordinates
	float fMargin;
	std::unique_ptr<SSBook> fBook;	// spreadsheet values

private:
	struct FrameGeometry {
		BRect rect;					// in view coordinates, unrotated
		float angle;				// degrees, counterclockwise, about the center
		BRect clip;					// the containing frames (document coordinates)
		int index;					// the frame's number in its part
	};
	// what laying out a flow found: text left over, the last frame used,
	// where each frame's text starts (UINT32_MAX: no text)
	struct LayoutResult {
		bool overflow = false;
		int lastFrame = -1;
		std::vector<uint32_t> starts;
	};
	void RebuildFrames();
	bool Paginate(const LayoutResult& main);

	struct Line {
		BRect frame;				// in view coordinates
		float baseline;
		float angle;				// of the frame it is in
		BPoint center;				// rotation center
		BRect clip;					// document coordinates
		// the text it shows: part, paragraph, byte offsets [from, to)
		size_t part = 0, paragraph = 0, from = 0, to = 0;
		struct Piece {
			std::string text;
			BFont font;
			rgb_color color;
			float x;				// relative to the line
			size_t start = SIZE_MAX, end = SIZE_MAX;	// in the paragraph (none: tab leader)
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
	bool fScriptShift = false;		// RunScript: shift held
};

} // namespace gobe

#endif
