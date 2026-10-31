#include "InkEdit.h"

#include <algorithm>
#include <cmath>

namespace gobe {

PatternGrid
PatternFromInk(const Ink& ink)
{
	PatternGrid grid;
	for (int row = 0; row < 8; row++)
		for (int col = 0; col < 8; col++)
			grid.pixels[size_t(row * 8 + col)]
				= (ink.pattern[row] & (0x80 >> col)) != 0 ? ink.color : ink.low;
	return grid;
}


void
ResizePattern(PatternGrid& grid, int cols, int rows)
{
	if (cols < 1 || rows < 1 || (cols == grid.cols && rows == grid.rows))
		return;
	std::vector<Color> pixels(size_t(cols * rows));
	for (int row = 0; row < rows; row++)
		for (int col = 0; col < cols; col++)
			pixels[size_t(row * cols + col)]
				= grid.pixels[size_t((row % grid.rows) * grid.cols + col % grid.cols)];
	grid.cols = cols;
	grid.rows = rows;
	grid.pixels = std::move(pixels);
}


void
ReplacePatternColor(PatternGrid& grid, Color from, Color to)
{
	for (Color& c : grid.pixels)
		if (c == from)
			c = to;
}


bool
OptimizePattern(const PatternGrid& grid, Ink& ink)
{
	if (grid.cols != 8 || grid.rows != 8 || grid.pixels.size() != 64)
		return false;
	Color high = grid.At(0);
	Color low = high;
	for (const Color& c : grid.pixels) {
		if (c == high)
			continue;
		if (low != high && c != low)
			return false;		// a third color
		low = c;
	}
	Ink made;
	made.set = true;
	made.color = high;
	made.low = low;
	for (int row = 0; row < 8; row++) {
		uint8_t bits = 0;
		for (int col = 0; col < 8; col++)
			if (grid.At(row * 8 + col) == high)
				bits |= uint8_t(0x80 >> col);
		made.pattern[row] = bits;
	}
	ink = made;
	return true;
}


Ink
NewGradient()
{
	Ink ink;
	ink.set = true;
	ink.gradient = true;
	ink.gradientType = Sig("GDdr");
	ink.colorCount = 2;
	ink.colors[0] = Color{0, 0, 0, 255};
	for (int i = 1; i < 4; i++)
		ink.colors[i] = Color{255, 255, 255, 255};
	ink.color = ink.colors[0];
	return ink;
}


void
SetGradientType(Ink& ink, uint32_t type)
{
	if (type != Sig("GDdr") && type != Sig("GDrl"))
		return;
	ink.gradientType = type;
	if (type == Sig("GDrl") && ink.center[0] == 0 && ink.center[1] == 0)
		ink.center[0] = ink.center[1] = 0.5f;
}


void
GradientClick(Ink& ink, float x, float y, float width, float height)
{
	if (width <= 0 || height <= 0)
		return;
	if (ink.gradientType == Sig("GDrl")) {
		ink.center[0] = std::max(0.0f, std::min(1.0f, x / width));
		ink.center[1] = std::max(0.0f, std::min(1.0f, y / height));
		return;
	}
	float dx = x - width / 2, dy = height / 2 - y;
	if (dx == 0 && dy == 0)
		return;
	float angle = std::atan2(dy, dx) * 180 / float(M_PI);
	if (angle < 0)
		angle += 360;
	ink.angle = std::round(angle * 10) / 10;
}

} // namespace gobe
