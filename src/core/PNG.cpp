// PNG encoding ([PNG] 2nd edition): the signature, IHDR, one IDAT, IEND.
#include "PNG.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>

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

bool
DecodePNG(const std::vector<uint8_t>& png, ArchiveBitmap& bitmap)
{
	static const uint8_t kSignature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
	if (png.size() < 8 || !std::equal(kSignature, kSignature + 8, png.begin()))
		return false;
	auto big = [&](size_t at) {
		return uint32_t(png[at]) << 24 | png[at + 1] << 16 | png[at + 2] << 8 | png[at + 3];
	};
	uint32_t width = 0, height = 0;
	int depth = 0, type = -1;
	std::vector<uint8_t> deflated, palette, transparency;
	for (size_t at = 8; at + 12 <= png.size();) {
		uint32_t length = big(at);
		if (at + 12 + length > png.size())
			return false;
		std::string chunk(png.begin() + long(at + 4), png.begin() + long(at + 8));
		const uint8_t* data = &png[at + 8];
		if (chunk == "IHDR" && length >= 13) {
			width = big(at + 8);
			height = big(at + 12);
			depth = data[8];
			type = data[9];
			if (data[12] != 0)
				return false;	// interlaced
		} else if (chunk == "PLTE")
			palette.assign(data, data + length);
		else if (chunk == "tRNS")
			transparency.assign(data, data + length);
		else if (chunk == "IDAT")
			deflated.insert(deflated.end(), data, data + length);
		else if (chunk == "IEND")
			break;
		at += 12 + length;
	}
	int channels = type == 0 ? 1 : type == 2 ? 3 : type == 3 ? 1 : type == 4 ? 2 : type == 6 ? 4 : 0;
	bool small = (type == 0 || type == 3) && (depth == 1 || depth == 2 || depth == 4);
	if (width == 0 || height == 0 || width > 16384 || height > 16384 || channels == 0
			|| !(depth == 8 || small) || (type == 3 && palette.empty()))
		return false;
	size_t bits = size_t(channels) * size_t(depth);
	size_t row = (size_t(width) * bits + 7) / 8, step = std::max<size_t>(1, bits / 8);
	std::vector<uint8_t> raw((row + 1) * height);
	uLongf size = uLongf(raw.size());
	if (uncompress(raw.data(), &size, deflated.data(), uLong(deflated.size())) != Z_OK
			|| size != raw.size())
		return false;
	// the filters (None, Sub, Up, Average, Paeth), row by row
	std::vector<uint8_t> pixels(row * height), previous(row, 0);
	for (uint32_t y = 0; y < height; y++) {
		uint8_t filter = raw[y * (row + 1)];
		uint8_t* line = &pixels[y * row];
		const uint8_t* in = &raw[y * (row + 1) + 1];
		for (size_t x = 0; x < row; x++) {
			int a = x >= step ? line[x - step] : 0, b = previous[x];
			int c = x >= step ? previous[x - step] : 0;
			int value = in[x];
			switch (filter) {
				case 0: break;
				case 1: value += a; break;
				case 2: value += b; break;
				case 3: value += (a + b) / 2; break;
				case 4: {
					int p = a + b - c, pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);
					value += pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
					break;
				}
				default: return false;
			}
			line[x] = uint8_t(value);
		}
		std::copy(line, line + row, previous.begin());
	}
	bitmap = ArchiveBitmap();
	bitmap.colorSpace = 0x2008;
	bitmap.bytesPerRow = width * 4;
	bitmap.bounds[2] = float(width - 1);
	bitmap.bounds[3] = float(height - 1);
	bitmap.bits.resize(size_t(width) * height * 4);
	for (uint32_t y = 0; y < height; y++) {
		const uint8_t* line = &pixels[y * row];
		for (uint32_t x = 0; x < width; x++) {
			uint8_t r, g, b, alpha = 255;
			if (small) {
				int shift = 8 - depth - int((x * depth) % 8);
				int v = (line[x * depth / 8] >> shift) & ((1 << depth) - 1);
				if (type == 3) {
					size_t i = size_t(v) * 3;
					if (i + 2 >= palette.size())
						return false;
					r = palette[i];
					g = palette[i + 1];
					b = palette[i + 2];
					if (size_t(v) < transparency.size())
						alpha = transparency[size_t(v)];
				} else
					r = g = b = uint8_t(v * 255 / ((1 << depth) - 1));
			} else {
				const uint8_t* p = line + size_t(x) * size_t(channels);
				switch (type) {
					case 0: r = g = b = p[0]; break;
					case 4: r = g = b = p[0]; alpha = p[1]; break;
					case 2: r = p[0]; g = p[1]; b = p[2]; break;
					case 6: r = p[0]; g = p[1]; b = p[2]; alpha = p[3]; break;
					default: {
						size_t i = size_t(p[0]) * 3;
						if (i + 2 >= palette.size())
							return false;
						r = palette[i];
						g = palette[i + 1];
						b = palette[i + 2];
						if (p[0] < transparency.size())
							alpha = transparency[p[0]];
					}
				}
			}
			uint8_t* out = &bitmap.bits[(size_t(y) * width + x) * 4];
			out[0] = b;
			out[1] = g;
			out[2] = r;
			out[3] = alpha;
		}
	}
	return true;
}


bool
ReadPNGFile(const std::string& path, ArchiveBitmap& bitmap)
{
	std::ifstream file(path, std::ios::binary);
	if (!file)
		return false;
	std::vector<uint8_t> png((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
	return DecodePNG(png, bitmap);
}

} // namespace gobe
