// PNG files of the documents' bitmaps (pictures in HTML export).
#ifndef GOBE_PNG_H
#define GOBE_PNG_H

#include <cstdint>
#include <vector>

#include "IMPart.h"

namespace gobe {

// A PNG (8-bit RGBA, no filter, zlib) of `width` x `height` pixels given
// as R, G, B, A bytes row after row.
std::vector<uint8_t> EncodePNG(int width, int height, const std::vector<uint8_t>& rgba);

// A bitmap of a document as a PNG: B_RGBA32 (B, G, R, A in memory) and
// B_RGB32 (alpha taken as opaque); the rectangle `crop` (left, top, right,
// bottom, inclusive) of it if right >= left. Empty for other color spaces
// or an empty bitmap.
std::vector<uint8_t> BitmapPNG(const ArchiveBitmap& bitmap, const float crop[4]);

} // namespace gobe

#endif
