// The graphics rulers (TGRRuler, Window > Show Graphics Ruler 'sMF8'): in
// a drawing, a ruler 16 pixels high along the top of the document (over
// the left ruler's corner and the vertical scroll bar) and one 16 wide
// down its left side, in the sheet's unit. Each page of the drawing's top
// row (or first column) gets its ticks, the numbering going on from page
// to page (TRuler::DrawNumbersAndHashes keeps its count). The right
// button gives the unit menu (MENU 241, as the text ruler's).
#ifndef GOBE_TGRRULER_H
#define GOBE_TGRRULER_H

#include <View.h>

#include <utility>
#include <vector>

namespace gobe {

class TPageView;

class TGRRuler : public BView {
public:
	// TGRRuler(13) along the top, TGRRuler(14) down the left
	TGRRuler(TPageView* pages, bool horizontal);

	virtual void Draw(BRect updateRect);
	virtual void MouseDown(BPoint where);

	// redrawn if the pages, zoom, scrolling or unit changed
	void Update();
	// test hook: "spans" prints where each page's ticks start and end
	void RunScript(const char* command);

	// the ruler's thickness (TGRRuler: a 0 to 15 rectangle)
	static const float kSize;

private:
	// the page spans in this ruler's pixels
	void Spans(std::vector<std::pair<float, float>>& spans) const;

	TPageView* fPages;
	bool fHorizontal;
	// what Update last saw
	std::vector<std::pair<float, float>> fSpans;
	float fZoom = 0;
	int fUnit = -1;
	int32 fDivisions = -1;
};

} // namespace gobe

#endif
