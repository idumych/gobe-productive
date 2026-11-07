#include "ImageTools.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace gobe {

bool
IMMask::Contains(int x, int y) const
{
	if (Empty())
		return true;
	if (x < 0 || y < 0 || x >= width || y >= height)
		return false;
	return bits[size_t(y) * size_t(width) + size_t(x)] != 0;
}


void
IMRect::Include(int x, int y)
{
	if (!Valid()) {
		left = right = x;
		top = bottom = y;
		return;
	}
	left = std::min(left, x);
	right = std::max(right, x);
	top = std::min(top, y);
	bottom = std::max(bottom, y);
}


void
IMRect::Include(const IMRect& r)
{
	if (!r.Valid())
		return;
	Include(r.left, r.top);
	Include(r.right, r.bottom);
}


namespace {

bool
Inside(const ArchiveBitmap& bitmap, int x, int y)
{
	return x >= 0 && y >= 0 && x < bitmap.Width() && y < bitmap.Height() && !bitmap.bits.empty();
}


// a pixel set where the selection allows it
void
Put(ArchiveBitmap& bitmap, const IMMask& mask, int x, int y, IMColor color, IMRect& changed)
{
	if (!Inside(bitmap, x, y) || !mask.Contains(x, y))
		return;
	SetPixel(bitmap, x, y, color);
	changed.Include(x, y);
}

} // namespace


IMColor
PixelAt(const ArchiveBitmap& bitmap, int x, int y)
{
	IMColor c;
	if (!Inside(bitmap, x, y))
		return c;
	const uint8_t* p = &bitmap.bits[size_t(y) * bitmap.bytesPerRow + size_t(x) * 4];
	c.b = p[0];
	c.g = p[1];
	c.r = p[2];
	c.a = bitmap.colorSpace == 0x0008 ? 255 : p[3];
	return c;
}


void
SetPixel(ArchiveBitmap& bitmap, int x, int y, IMColor color)
{
	if (!Inside(bitmap, x, y))
		return;
	uint8_t* p = &bitmap.bits[size_t(y) * bitmap.bytesPerRow + size_t(x) * 4];
	p[0] = color.b;
	p[1] = color.g;
	p[2] = color.r;
	p[3] = color.a;
}


float
ColorDistance(IMColor a, IMColor b)
{
	int dr = int(a.r) - int(b.r), dg = int(a.g) - int(b.g), db = int(a.b) - int(b.b);
	int mean = (int(b.r) + int(a.r)) >> 1;
	// (as the original's integers: (mean + 512) >> 8, (767 - mean) >> 8)
	int blueWeight = -mean + 0x2ff;
	if (blueWeight < 0)
		blueWeight = -mean + 0x3fe;
	return sqrtf(float((blueWeight >> 8) * db * db + dg * 4 * dg + ((mean + 0x200) >> 8) * dr * dr));
}


float
ToleranceDistance(float percent)
{
	return percent * 722.0f / 100.0f;
}


bool
ColorsAlike(IMColor a, IMColor b, float distance)
{
	return ColorDistance(a, b) <= distance;
}


IMRect
PencilDown(ArchiveBitmap& bitmap, const IMMask& mask, int x, int y, IMColor fore, IMColor back,
	IMColor& used)
{
	IMRect changed;
	used = fore;
	if (!mask.Contains(x, y))
		return changed;
	IMColor under = PixelAt(bitmap, x, y);
	if (under.r == fore.r && under.g == fore.g && under.b == fore.b && under.a == fore.a)
		used = back;
	Put(bitmap, mask, x, y, used, changed);
	return changed;
}


IMRect
PencilLine(ArchiveBitmap& bitmap, const IMMask& mask, int x0, int y0, int x1, int y1,
	IMColor color)
{
	// (BView::StrokeLine a pixel wide: from one end to the other, both in)
	IMRect changed;
	int dx = std::abs(x1 - x0), dy = -std::abs(y1 - y0);
	int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
	int error = dx + dy;
	for (;;) {
		Put(bitmap, mask, x0, y0, color, changed);
		if (x0 == x1 && y0 == y1)
			break;
		int twice = 2 * error;
		if (twice >= dy) {
			error += dy;
			x0 += sx;
		}
		if (twice <= dx) {
			error += dx;
			y0 += sy;
		}
	}
	return changed;
}


namespace {

// EraserTool's square at a point: from 7 before to 8 after it, over the
// scale, rounded (TRect::Round), both ends in
void
EraserSquare(int x, int y, float scale, int& left, int& top, int& right, int& bottom)
{
	left = int(roundf(float(x) - 7.0f / scale));
	top = int(roundf(float(y) - 7.0f / scale));
	right = int(roundf(8.0f / scale + float(x)));
	bottom = int(roundf(8.0f / scale + float(y)));
}

} // namespace


IMRect
EraserDown(ArchiveBitmap& bitmap, const IMMask& mask, int x, int y, float scale, IMColor back)
{
	IMRect changed;
	int left, top, right, bottom;
	EraserSquare(x, y, scale, left, top, right, bottom);
	for (int py = top; py <= bottom; py++)
		for (int px = left; px <= right; px++)
			Put(bitmap, mask, px, py, back, changed);
	return changed;
}


IMRect
EraserMove(ArchiveBitmap& bitmap, const IMMask& mask, int x0, int y0, int x1, int y1,
	float scale, IMColor back)
{
	// (EraserTool::CalcPoly: the outline around both squares, filled: the
	// square swept along the line)
	IMRect changed;
	int dx = std::abs(x1 - x0), dy = -std::abs(y1 - y0);
	int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
	int error = dx + dy;
	for (;;) {
		changed.Include(EraserDown(bitmap, mask, x0, y0, scale, back));
		if (x0 == x1 && y0 == y1)
			break;
		int twice = 2 * error;
		if (twice >= dy) {
			error += dy;
			x0 += sx;
		}
		if (twice <= dx) {
			error += dx;
			y0 += sy;
		}
	}
	return changed;
}


IMRect
BucketFill(ArchiveBitmap& bitmap, const IMMask& mask, int x, int y, IMColor fore, float distance)
{
	IMRect changed;
	int width = bitmap.Width(), height = bitmap.Height();
	if (!Inside(bitmap, x, y) || !mask.Contains(x, y))
		return changed;
	IMColor seed = PixelAt(bitmap, x, y);
	// the pixels done (the original's second bitmap)
	std::vector<uint8_t> done(size_t(width) * size_t(height), 0);
	auto take = [&](int px, int py) {
		if (done[size_t(py) * size_t(width) + size_t(px)] || !mask.Contains(px, py)
				|| !ColorsAlike(seed, PixelAt(bitmap, px, py), distance))
			return false;
		done[size_t(py) * size_t(width) + size_t(px)] = 1;
		SetPixel(bitmap, px, py, fore);
		changed.Include(px, py);
		return true;
	};
	// Heckbert's seed fill: spans (y, from, to, direction), at most 40000
	struct Span {
		int y, left, right, dy;
	};
	std::vector<Span> stack;
	if (y + 1 < height)
		stack.push_back({y, x, x, 1});
	stack.push_back({y + 1, x, x, -1});
	while (!stack.empty()) {
		Span s = stack.back();
		stack.pop_back();
		int row = s.y + s.dy;
		if (row < 0 || row >= height)
			continue;
		int px = s.left;
		while (px >= 0 && take(px, row))
			px--;
		int start = px + 1;
		if (px >= s.left)
			goto skip;
		if (start < s.left && stack.size() < 40000 && row - s.dy >= 0 && row - s.dy < height)
			stack.push_back({row, start, s.left - 1, -s.dy});
		px = s.left + 1;
		do {
			while (px < width && take(px, row))
				px++;
			if (stack.size() < 40000 && row + s.dy >= 0 && row + s.dy < height)
				stack.push_back({row, start, px - 1, s.dy});
			if (px > s.right + 1 && stack.size() < 40000 && row - s.dy >= 0 && row - s.dy < height)
				stack.push_back({row, s.right + 1, px - 1, -s.dy});
skip:
			for (px++; px <= s.right && !take(px, row); px++)
				;
			start = px;
		} while (px <= s.right);
	}
	return changed;
}


AirbrushBrush
BuildAirbrush(IMColor color, const AirbrushSettings& s)
{
	AirbrushBrush brush;
	brush.width = 2 * s.xRadius + 1;
	brush.height = 2 * s.yRadius + 1;
	brush.pixels.assign(size_t(brush.width) * size_t(brush.height), IMColor{0, 0, 0, 0});
	double maxAlpha = std::min(double(s.flowRate) * 255.0 / 100.0, 255.0);
	float a = std::max(0.0f, float(s.xRadius) - 1.5f);
	float b = std::max(0.0f, float(s.yRadius) - 1.5f);
	float initial = float(std::min(s.xRadius, s.yRadius));
	float cx = float(s.xRadius), cy = float(s.yRadius);
	for (float r = initial; r > -0.01f; r -= 1, a -= 1, b -= 1) {
		IMColor ring = color;
		if (int(initial) == 0)
			ring.a = 255;
		else {
			int alpha = int((initial - r * float(100 - s.focus) / 100.0f + 1) / initial
				* float(maxAlpha) + 0.5);
			ring.a = uint8_t(std::min(255, alpha));
		}
		if (a < 0 || b < 0)
			continue;
		// (BView::FillEllipse: the pixels of its rectangle inside it)
		for (int y = 0; y < brush.height; y++)
			for (int x = 0; x < brush.width; x++) {
				float dx = (float(x) - cx) / (a + 0.5f), dy = (float(y) - cy) / (b + 0.5f);
				if (dx * dx + dy * dy <= 1)
					brush.pixels[size_t(y) * size_t(brush.width) + size_t(x)] = ring;
			}
	}
	return brush;
}


IMRect
AirbrushStamp(ArchiveBitmap& bitmap, const IMMask& mask, const AirbrushBrush& brush, int x, int y)
{
	IMRect changed;
	int left = x - (brush.width - 1) / 2, top = y - (brush.height - 1) / 2;
	for (int by = 0; by < brush.height; by++)
		for (int bx = 0; bx < brush.width; bx++) {
			int px = left + bx, py = top + by;
			const IMColor& s = brush.pixels[size_t(by) * size_t(brush.width) + size_t(bx)];
			if (s.a == 0 || !Inside(bitmap, px, py) || !mask.Contains(px, py))
				continue;
			IMColor d = PixelAt(bitmap, px, py);
			int alpha = s.a, keep = 255 - s.a;
			IMColor out;
			out.r = uint8_t((s.r * alpha + d.r * keep) / 255);
			out.g = uint8_t((s.g * alpha + d.g * keep) / 255);
			out.b = uint8_t((s.b * alpha + d.b * keep) / 255);
			out.a = uint8_t(std::max(int(d.a), std::min(255, alpha + int(d.a))));
			SetPixel(bitmap, px, py, out);
			changed.Include(px, py);
		}
	return changed;
}


std::vector<std::array<int, 2>>
AirbrushPath(int x0, int y0, int x1, int y1, const AirbrushSettings& s, bool timeUp,
	bool& moved)
{
	std::vector<std::array<int, 2>> points;
	moved = false;
	if (x0 == x1 && y0 == y1) {
		if (timeUp) {
			points.push_back({x1, y1});
			moved = true;
		}
		return points;
	}
	float spacing = float((s.yRadius + s.xRadius) * s.spacing) / 100.0f;
	float vx = float(x1 - x0), vy = float(y1 - y0);
	float distance = sqrtf(vx * vx + vy * vy);
	vx /= distance;
	vy /= distance;
	if (distance < spacing) {
		if (timeUp)
			points.push_back({x0, y0});
		return points;
	}
	float px = float(x0), py = float(y0);
	for (float along = spacing; along < distance; along += spacing) {
		px += vx * spacing;
		py += vy * spacing;
		points.push_back({int(px), int(py)});
	}
	points.push_back({x1, y1});
	moved = true;
	return points;
}

} // namespace gobe
