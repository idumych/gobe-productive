// TRuler's tick math, shared by the text ruler (TWPRuler) and the
// graphics rulers (TGRRuler): how far apart the labels and ticks are, and
// how long each tick is.
#ifndef GOBE_RULER_H
#define GOBE_RULER_H

#include <cstdint>

namespace gobe {

// TRuler::UnitSize: points per unit (in, cm, mm, pc, pt, li); "lines"
// count as points on the ruler
extern const float kRulerUnitSize[6];

struct RulerTicks {
	float major;		// pixels between labels
	float minor;		// pixels between ticks
	int32_t count;		// units between labels
	int32_t divisions;	// ticks per label
};

// TRuler::CalcTicks: labels `count` units apart, at least 40 pixels
// (`unitPixels`: one unit at the zoom), each split in `divisions`, from the
// sheet's ruler divisions
RulerTicks CalcRulerTicks(float unitPixels, int32_t sheetDivisions);

// TRuler::drawCurrentHashType: the length of tick `index` (0 to
// divisions - 1, 0 the labelled one) as a fraction of the ruler's 14
// pixels counted from the top (0: full length, 0.55 the longest of the
// others, 0.85 the shortest); -1: no tick
float RulerHashHeight(int32_t divisions, int32_t index);

} // namespace gobe

#endif
