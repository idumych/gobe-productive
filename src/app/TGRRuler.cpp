#include "TGRRuler.h"

#include <Window.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "Ruler.h"
#include "TPageView.h"
#include "TWPRuler.h"

namespace gobe {

namespace {

// TRuler::drawSeparator, TGRRuler::Draw
const rgb_color kGray = {0x70, 0x70, 0x70, 255};
const rgb_color kBlack = {0, 0, 0, 255};

} // namespace


const float TGRRuler::kSize = 15;


TGRRuler::TGRRuler(TPageView* pages, bool horizontal)
	:
	BView("GRRuler", B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE),
	fPages(pages),
	fHorizontal(horizontal)
{
	SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	SetLowUIColor(B_PANEL_BACKGROUND_COLOR);
	// TRuler::TRuler: the fixed font at 12, turned a quarter for the
	// vertical ruler's numbers
	BFont font(be_fixed_font);
	font.SetSize(12);
	if (!horizontal)
		font.SetRotation(90);
	SetFont(&font);
}


void
TGRRuler::Spans(std::vector<std::pair<float, float>>& spans) const
{
	fPages->DrawingRulerSpans(fHorizontal, spans);
	// the top ruler starts over the left ruler's corner
	if (fHorizontal)
		for (auto& span : spans) {
			span.first += kSize + 1;
			span.second += kSize + 1;
		}
}


void
TGRRuler::Update()
{
	std::vector<std::pair<float, float>> spans;
	Spans(spans);
	const Document& doc = fPages->Doc();
	if (spans == fSpans && fPages->Zoom() == fZoom && RulerUnit(doc) == fUnit
			&& RulerDivisions(doc) == fDivisions)
		return;
	fSpans = spans;
	fZoom = fPages->Zoom();
	fUnit = RulerUnit(doc);
	fDivisions = RulerDivisions(doc);
	Invalidate();
}


// TGRRuler::Draw: TRuler::DrawNumbersAndHashes for each page, without
// resetting its count between them
void
TGRRuler::Draw(BRect updateRect)
{
	BRect bounds = Bounds();
	SetHighColor(kGray);
	if (fHorizontal)
		StrokeLine(BPoint(0, 0), BPoint(0, kSize));
	else
		StrokeLine(BPoint(0, 0), BPoint(kSize, 0));

	std::vector<std::pair<float, float>> spans;
	Spans(spans);
	const Document& doc = fPages->Doc();
	RulerTicks t = CalcRulerTicks(kRulerUnitSize[RulerUnit(doc)] * fPages->Zoom(),
		RulerDivisions(doc));
	float edge = fHorizontal ? bounds.right : bounds.bottom;
	// TRuler +0x10, +0x1c, +0x18: the tick's index, the next label's
	// number, how far past the page's end the next tick fell
	int32 index = 0, value = 0;
	float carry = 0;
	for (const auto& [start, end] : spans) {
		float limit = std::min(edge + t.major, end);
		auto line = [&](float along, float from, float to) {
			if (fHorizontal)
				StrokeLine(BPoint(along, from), BPoint(along, to));
			else
				StrokeLine(BPoint(from, along), BPoint(to, along));
		};
		// TRuler::drawSeparator: along the inner edge
		SetHighColor(kGray);
		if (fHorizontal)
			StrokeLine(BPoint(std::max(start, 0.0f), kSize), BPoint(limit + 1, kSize));
		else
			StrokeLine(BPoint(kSize, std::max(start, 0.0f)), BPoint(kSize, limit + 1));
		SetHighColor(kBlack);
		// TRuler::drawHash: from 14 out to `height` x 14
		auto hash = [&](float x, float height) {
			if (x >= 0 && x <= limit + 1)
				line(x, 14, height * 14);
		};
		// TRuler::drawMajorLabel: "%i" right of the tick along the top;
		// down the left, the next number ending above the next label's
		// tick
		auto label = [&](float x, int32 number) {
			if (!fHorizontal)
				x += t.major;
			if (x < 0 || x > limit)
				return;
			if (!fHorizontal)
				number += t.count;
			char text[16];
			snprintf(text, sizeof text, "%i", int(number));
			float width = StringWidth(text);
			if (fHorizontal) {
				if (limit < x + width)
					return;
				DrawString(text, BPoint(x + 3, 10));
			} else {
				if (x - width < start)
					return;
				DrawString(text, BPoint(10, x - 2));
			}
		};

		hash(start, 0);
		float x = start;
		bool labelFirst = carry == 0;
		if (!labelFirst)
			x = start + carry;
		while (true) {
			if (!labelFirst) {
				if (!(x <= limit && x <= edge))
					break;
				float height = index == 0 ? 0 : RulerHashHeight(t.divisions, index);
				if (height >= 0)
					hash(x, height);
			}
			labelFirst = false;
			if (index == 0) {
				label(x, value);
				value += t.count;
			}
			index = index < t.divisions - 1 ? index + 1 : 0;
			x += t.minor;
		}
		hash(end + 1, 0);
		carry = x - end;
	}
}


// TGRRuler::MouseDown: the right button's unit menu
void
TGRRuler::MouseDown(BPoint where)
{
	uint32 buttons = 0;
	if (Window() != NULL && Window()->CurrentMessage() != NULL)
		Window()->CurrentMessage()->FindInt32("buttons", (int32*)&buttons);
	if ((buttons & B_SECONDARY_MOUSE_BUTTON) == 0)
		return;
	const Document& doc = fPages->Doc();
	RulerUnitPopUp(this, where, RulerUnit(doc), RulerDivisions(doc));
}


void
TGRRuler::RunScript(const char* command)
{
	if (strcmp(command, "spans") != 0)
		return;
	std::vector<std::pair<float, float>> spans;
	Spans(spans);
	printf("grruler %s %s", fHorizontal ? "top" : "left", IsHidden() ? "hidden" : "shown");
	for (const auto& [start, end] : spans)
		printf(" %g-%g", start, end);
	printf("\n");
	fflush(stdout);
}

} // namespace gobe
