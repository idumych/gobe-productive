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
// TOutputArchive::WriteBitmap: 'BMAP', the size of the bits, an unused
// bool, the color space, bytes per row, the bounds, compressed (zlib, its
// size, the data), 'BmaP'; an empty one is 'BMAP', 0, 'BmaP'
class DocWriter;
void WriteBitmap(DocWriter& w, const ArchiveBitmap& bitmap);

// TColor: red, green, blue, alpha
struct IMColor {
	uint8_t r = 0, g = 0, b = 0, a = 255;
	bool operator==(const IMColor& o) const
		{ return r == o.r && g == o.g && b == o.b && a == o.a; }
	bool operator!=(const IMColor& o) const { return !(*this == o); }
};

// IMToolSelection's mask: what the tools may change (empty: all of it)
struct IMMask {
	int width = 0, height = 0;
	std::vector<uint8_t> bits;		// a byte a pixel, nonzero inside
	bool Empty() const { return bits.empty(); }
	bool Contains(int x, int y) const;
};

// the pixels a tool changed (inclusive), none if left > right
struct IMRect {
	int left = 0, top = 0, right = -1, bottom = -1;
	bool Valid() const { return left <= right && top <= bottom; }
	void Include(int x, int y);
	void Include(const IMRect& r);
};

// IMToolSelection: a one-bit mask (a B_GRAY1 bitmap, its "definition") put
// at a point of the image; which of its pixels are on, and whether it was
// set as a rectangle
struct IMSelection {
	int width = 0, height = 0;		// the mask bitmap's size
	std::vector<uint8_t> bits;		// a byte a pixel, nonzero on
	int x = 0, y = 0;				// +0x18: where it is in the image
	IMRect bounds;					// +0x04: the pixels on, in its own coordinates
	bool isRect = false;			// +0x16: set as a rectangle

	// Redefine(BRect): a mask that size with nothing on, at 0, 0
	void Redefine(int newWidth, int newHeight);
	// Set(BRect): only that rectangle on (the mask grown to hold it; a
	// rectangle left of or above 0 sets the mask's place there)
	void SetRect(int left, int top, int right, int bottom);
	// Set(BBitmap const*, bool): another mask's pixels (the same size), or
	// added to these
	void SetBits(const std::vector<uint8_t>& other, bool add);
	void Clear();
	// Contains(int, int): in image coordinates
	bool Contains(int px, int py) const;
	bool Empty() const { return !bounds.Valid(); }
	void CalcBounds();
	// Mask(): the pixels on, in the image
	IMRect Mask() const;
	// Clip(BRect): the mask made the image's size, at 0, 0, with only what
	// lies inside it (nothing if it's that already)
	void Clip(int imageWidth, int imageHeight);
	void Invert();
	// operator+=, -=, &=: masks of the same size, byte by byte
	void Add(const IMSelection& other);
	void Subtract(const IMSelection& other);
	void Intersect(const IMSelection& other);
	void OffsetBy(int dx, int dy) { x += dx; y += dy; }
	// what the tools may change in a bitmap whose top left is at (left,
	// top) of the image: none (all of it) when nothing is selected
	IMMask ToolMask(int bitmapWidth, int bitmapHeight, int left = 0, int top = 0) const;
	bool operator==(const IMSelection& o) const;
	bool operator!=(const IMSelection& o) const { return !(*this == o); }
};

struct IMPart {
	PartHeader header;
	float size[2] = {0, 0};
	ArchiveBitmap image;
	ArchiveBitmap floating;
	float rect[4] = {0, 0, -1, -1};
	uint8_t colors[2][4] = {};		// TColor: foreground, background
	// the part's selection (TPart +0x1c: a TIMSelection in the SelectionMgr)
	IMSelection selection;
	// where 'imp+' starts and 'imp-' ends in the part record
	size_t sectionStart = 0, sectionEnd = 0;
};

// `a` is positioned at the part record; reads up to and including 'imp-'.
void ReadIMPart(DocArchive& a, IMPart& out);
// TPartIM::Write's own data: 'imp+' to 'imp-'
std::vector<uint8_t> WriteIMSection(const IMPart& part);

struct Document;
// TIMSetPartSizeCommand::Perform (Document Size…): image part `part` made
// width by height points: a new bitmap of its color space filled with the
// background color, the image drawn at its top left (cut off if smaller),
// its frames as large; for the document's own image, its drawing's frame
// too (the root's stays). False if it isn't an image or nothing changed.
bool SetImageSize(Document& doc, size_t part, float width, float height);

} // namespace gobe

#endif
