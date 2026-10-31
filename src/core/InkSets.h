// The application's ink sets (TInkSet::LoadPatternSetResource,
// LoadGradientSetResource): the FormatBar's pattern and gradient menus.
#ifndef GOBE_INK_SETS_H
#define GOBE_INK_SETS_H

#include <cstdint>
#include <vector>

#include "Styles.h"

namespace gobe {

struct InkSet {
	int columns = 1;
	int cell = 10;			// a cell's size in the menu
	std::vector<Ink> inks;
};

// A 'BPAT' resource: columns and cell size, then per pattern its high and
// low colors (four little-endian u32s each, R, G, B, A) and its 8 pattern
// bytes (a u32 each); the data after the header a multiple of 64 bytes.
// False if it isn't.
bool ParsePatternSet(const std::vector<uint8_t>& resource, InkSet& set);

// A 'GRAD' resource: columns and cell size, then gradients until 'gEND':
// the type ('GDdr', 'GDrl'; little-endian u32s throughout), the number of
// colors, four colors, the angle, a rectangle (not used), then two floats
// as NUL-terminated text, a radial gradient's center. False if it's cut
// short.
bool ParseGradientSet(const std::vector<uint8_t>& resource, InkSet& set);

} // namespace gobe

#endif
