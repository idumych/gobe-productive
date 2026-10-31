// PNG encoding ([PNG] 2nd edition): the signature, IHDR, one IDAT, IEND.
#include "PNG.h"

#include <algorithm>
#include <cmath>

#include <zlib.h>

namespace gobe {

namespace {

void
Add32(std::vector<uint8_t>& out, uint32_t v)
{
	out.push_back(uint8_t(v >> 24));
	out.push_back(uint8_t(v >> 16));
	out.push_back(uint8_t(v >> 8));
	out.push_back(uint8_t(v));
}


void
Chunk(std::vector<uint8_t>& out, const char* type, const std::vector<uint8_t>& data)
{
	Add32(out, uint32_t(data.size()));
	size_t start = out.size();
	out.insert(out.end(), type, type + 4);
	out.insert(out.end(), data.begin(), data.end());
	Add32(out, uint32_t(crc32(0, &out[start], uInt(out.size() - start))));
}

} // namespace


std::vector<uint8_t>
EncodePNG(int width, int height, const std::vector<uint8_t>& rgba)
{
	if (width <= 0 || height <= 0 || rgba.size() < size_t(width) * size_t(height) * 4)
		return {};
	std::vector<uint8_t> out = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
	std::vector<uint8_t> header;
	Add32(header, uint32_t(width));
	Add32(header, uint32_t(height));
	header.insert(header.end(), {8, 6, 0, 0, 0});	// 8 bits, RGBA, deflate, no filter, no interlace
	Chunk(out, "IHDR", header);
	// each row: filter type 0, then its pixels
	std::vector<uint8_t> raw;
	size_t row = size_t(width) * 4;
	raw.reserve((row + 1) * size_t(height));
	for (int y = 0; y < height; y++) {
		raw.push_back(0);
		raw.insert(raw.end(), rgba.begin() + long(row * size_t(y)),
			rgba.begin() + long(row * size_t(y + 1)));
	}
	uLongf size = compressBound(uLong(raw.size()));
	std::vector<uint8_t> deflated(size);
	if (compress2(deflated.data(), &size, raw.data(), uLong(raw.size()), 9) != Z_OK)
		return {};
	deflated.resize(size);
	Chunk(out, "IDAT", deflated);
	Chunk(out, "IEND", {});
	return out;
}


std::vector<uint8_t>
BitmapPNG(const ArchiveBitmap& bitmap, const float crop[4])
{
	const uint32_t kRGB32 = 0x0008, kRGBA32 = 0x2008;
	if (bitmap.Empty() || (bitmap.colorSpace != kRGB32 && bitmap.colorSpace != kRGBA32))
		return {};
	int left = 0, top = 0, right = bitmap.Width() - 1, bottom = bitmap.Height() - 1;
	if (crop != nullptr && crop[2] >= crop[0] && crop[3] >= crop[1]) {
		left = std::max(left, int(std::lround(crop[0])));
		top = std::max(top, int(std::lround(crop[1])));
		right = std::min(right, int(std::lround(crop[2])));
		bottom = std::min(bottom, int(std::lround(crop[3])));
	}
	int width = right - left + 1, height = bottom - top + 1;
	if (width <= 0 || height <= 0)
		return {};
	std::vector<uint8_t> rgba;
	rgba.reserve(size_t(width) * size_t(height) * 4);
	for (int y = top; y <= bottom; y++)
		for (int x = left; x <= right; x++) {
			size_t at = size_t(y) * bitmap.bytesPerRow + size_t(x) * 4;
			if (at + 4 > bitmap.bits.size())
				return {};
			rgba.push_back(bitmap.bits[at + 2]);
			rgba.push_back(bitmap.bits[at + 1]);
			rgba.push_back(bitmap.bits[at]);
			rgba.push_back(bitmap.colorSpace == kRGBA32 ? bitmap.bits[at + 3] : 255);
		}
	return EncodePNG(width, height, rgba);
}

} // namespace gobe
