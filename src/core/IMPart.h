// The image processing part's data (TPartIM::TPartIM(TInputArchive&)):
// 'imp+', the image size, the image and a second (selection/float) bitmap,
// a rectangle, two colors, 'imp-'.
#ifndef GOBE_IM_PART_H
#define GOBE_IM_PART_H

#include <cstdint>
#include <vector>

#include "DocArchive.h"
#include "WPPart.h"

namespace gobe {

// TInputArchive::ReadBitmap: 'BMAP', the size in bytes; if not 0: a bool,
// the color space (B_RGBA32 = 0x2008, ...), bytes per row, the bounds,
// (format > 0x15) compressed: zlib data with its size; 'BmaP'. Pixels are
// as BeOS x86 kept them (B_RGBA32: B, G, R, A).
struct ArchiveBitmap {
	uint32_t colorSpace = 0;
	uint32_t bytesPerRow = 0;
	float bounds[4] = {0, 0, -1, -1};
	std::vector<uint8_t> bits;

	int Width() const { return int(bounds[2] - bounds[0] + 1); }
	int Height() const { return int(bounds[3] - bounds[1] + 1); }
	bool Empty() const { return bits.empty(); }
};
void ReadBitmap(DocArchive& a, ArchiveBitmap& out);

struct IMPart {
	PartHeader header;
	float size[2] = {0, 0};
	ArchiveBitmap image;
	ArchiveBitmap floating;
	float rect[4] = {0, 0, -1, -1};
	uint8_t colors[2][4] = {};		// TColor: foreground, background
};

// `a` is positioned at the part record; reads up to and including 'imp-'.
void ReadIMPart(DocArchive& a, IMPart& out);

} // namespace gobe

#endif
