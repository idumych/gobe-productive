#include "ImageSelect.h"

#include <algorithm>
#include <cmath>

#include "DocArchive.h"
#include "DocWriter.h"

namespace gobe {

// IMToolSelection

void
IMSelection::Redefine(int newWidth, int newHeight)
{
	width = std::max(0, newWidth);
	height = std::max(0, newHeight);
	bits.assign(size_t(width) * size_t(height), 0);
	x = y = 0;
	bounds = IMRect();
	isRect = false;
}


void
IMSelection::Clear()
{
	std::fill(bits.begin(), bits.end(), 0);
	x = y = 0;
	bounds = IMRect();
	isRect = true;
}


void
IMSelection::SetRect(int left, int top, int right, int bottom)
{
	int w = right - left + 1, h = bottom - top + 1;
	// (the mask grown to hold the rectangle at 0, 0)
	if (w > width || h > height) {
		int newWidth = std::max(width, w), newHeight = std::max(height, h);
		std::vector<uint8_t> grown(size_t(newWidth) * size_t(newHeight), 0);
		width = newWidth;
		height = newHeight;
		bits.swap(grown);
	}
	Clear();
	int dx = 0, dy = 0;
	if (left < 0 || top < 0) {
		dx = left;
		dy = top;
		right -= left;
		bottom -= top;
		left = top = 0;
	}
	// AddRect: FillRect, kept to the mask
	IMRect filled;
	for (int py = std::max(0, top); py <= std::min(bottom, height - 1); py++)
		for (int px = std::max(0, left); px <= std::min(right, width - 1); px++) {
			bits[size_t(py) * size_t(width) + size_t(px)] = 1;
			filled.Include(px, py);
		}
	bounds = filled;
	x = dx;
	y = dy;
	isRect = !Empty();
}


void
IMSelection::SetBits(const std::vector<uint8_t>& other, bool add)
{
	size_t n = std::min(bits.size(), other.size());
	for (size_t i = 0; i < n; i++)
		bits[i] = add ? uint8_t(bits[i] | other[i]) : other[i];
	isRect = false;
	CalcBounds();
}


bool
IMSelection::Contains(int px, int py) const
{
	int u = px - x, v = py - y;
	if (u < 0 || v < 0 || u >= width || v >= height)
		return false;
	return bits[size_t(v) * size_t(width) + size_t(u)] != 0;
}


void
IMSelection::CalcBounds()
{
	bounds = IMRect();
	for (int v = 0; v < height; v++)
		for (int u = 0; u < width; u++)
			if (bits[size_t(v) * size_t(width) + size_t(u)])
				bounds.Include(u, v);
}


IMRect
IMSelection::Mask() const
{
	IMRect r = bounds;
	if (r.Valid()) {
		r.left += x;
		r.right += x;
		r.top += y;
		r.bottom += y;
	}
	return r;
}


void
IMSelection::Clip(int imageWidth, int imageHeight)
{
	if (x == 0 && y == 0 && width == imageWidth && height == imageHeight)
		return;
	IMSelection clipped;
	clipped.Redefine(imageWidth, imageHeight);
	IMRect m = Mask();
	for (int py = std::max(0, m.top); py <= std::min(m.bottom, imageHeight - 1); py++)
		for (int px = std::max(0, m.left); px <= std::min(m.right, imageWidth - 1); px++)
			if (Contains(px, py)) {
				clipped.bits[size_t(py) * size_t(imageWidth) + size_t(px)] = 1;
				clipped.bounds.Include(px, py);
			}
	clipped.isRect = isRect && !clipped.Empty();
	*this = std::move(clipped);
}


void
IMSelection::Invert()
{
	for (uint8_t& b : bits)
		b = b ? 0 : 1;
	isRect = false;
	CalcBounds();
}


namespace {

// the other mask's pixel at this mask's (u, v): byte by byte when they're the
// same size, as the original does (it only ever combines copies of one
// selection), else where it lies in the image
template<typename Op>
void
Combine(IMSelection& self, const IMSelection& other, Op op)
{
	bool same = other.width == self.width && other.height == self.height
		&& other.x == self.x && other.y == self.y;
	for (int v = 0; v < self.height; v++)
		for (int u = 0; u < self.width; u++) {
			size_t i = size_t(v) * size_t(self.width) + size_t(u);
			bool on = same ? other.bits[i] != 0 : other.Contains(u + self.x, v + self.y);
			self.bits[i] = op(self.bits[i] != 0, on) ? 1 : 0;
		}
	self.isRect = false;
	self.CalcBounds();
}

} // namespace


void
IMSelection::Add(const IMSelection& other)
{
	Combine(*this, other, [](bool a, bool b) { return a || b; });
}


void
IMSelection::Subtract(const IMSelection& other)
{
	Combine(*this, other, [](bool a, bool b) { return a && !b; });
}


void
IMSelection::Intersect(const IMSelection& other)
{
	Combine(*this, other, [](bool a, bool b) { return a && b; });
}


IMMask
IMSelection::ToolMask(int bitmapWidth, int bitmapHeight, int left, int top) const
{
	IMMask mask;
	if (Empty())
		return mask;
	mask.width = bitmapWidth;
	mask.height = bitmapHeight;
	mask.bits.assign(size_t(bitmapWidth) * size_t(bitmapHeight), 0);
	for (int v = 0; v < bitmapHeight; v++)
		for (int u = 0; u < bitmapWidth; u++)
			mask.bits[size_t(v) * size_t(bitmapWidth) + size_t(u)]
				= Contains(u + left, v + top) ? 1 : 0;
	// (a mask with nothing in it lets nothing through)
	if (mask.bits.empty())
		mask.bits.push_back(0);
	return mask;
}


bool
IMSelection::operator==(const IMSelection& o) const
{
	return width == o.width && height == o.height && x == o.x && y == o.y
		&& isRect == o.isRect && bits == o.bits;
}


IMSelection
ImageSelection(int imageWidth, int imageHeight, int left, int top, int right, int bottom)
{
	IMSelection s;
	s.Redefine(imageWidth, imageHeight);
	if (left <= right && top <= bottom)
		s.SetRect(left, top, right, bottom);
	return s;
}


// TIMSelection's archive

namespace {

int
RoundHalfAway(float v)
{
	return int(v > 0 ? floorf(v + 0.5f) : ceilf(v - 0.5f));
}

} // namespace


void
ReadIMSelection(DocArchive& a, IMSelection& out)
{
	out = IMSelection();
	out.Redefine(1, 1);
	if (a.FileFormatVersion() < 0x16) {
		float r[4];
		a.Rect(r);
		if (!(r[0] <= r[2] && r[1] <= r[3])) {
			out.Clear();
			out.isRect = false;
			return;
		}
		int left = RoundHalfAway(r[0]), top = RoundHalfAway(r[1]);
		int right = RoundHalfAway(r[2] - 1), bottom = RoundHalfAway(r[3] - 1);
		out.Redefine(right - left + 1, bottom - top + 1);
		out.SetRect(0, 0, right - left, bottom - top);
		out.x = left;
		out.y = top;
		return;
	}
	if (!a.Bool()) {
		float p[2];
		a.Point(p);
		ArchiveBitmap mask;
		ReadBitmap(a, mask);
		out.Redefine(mask.Width(), mask.Height());
		for (int v = 0; v < out.height; v++)
			for (int u = 0; u < out.width; u++) {
				size_t at = size_t(v) * mask.bytesPerRow + size_t(u >> 3);
				if (at < mask.bits.size() && (mask.bits[at] >> (7 - (u & 7)) & 1))
					out.bits[size_t(v) * size_t(out.width) + size_t(u)] = 1;
			}
		out.CalcBounds();
		out.x = RoundHalfAway(p[0]);
		out.y = RoundHalfAway(p[1]);
		return;
	}
	float definition[4], mask[4];
	a.Rect(definition);
	out.Redefine(RoundHalfAway(definition[2] - definition[0]) + 1,
		RoundHalfAway(definition[3] - definition[1]) + 1);
	a.Rect(mask);
	out.SetRect(0, 0, RoundHalfAway(mask[2] - mask[0]), RoundHalfAway(mask[3] - mask[1]));
	out.x = RoundHalfAway(mask[0]);
	out.y = RoundHalfAway(mask[1]);
}


void
WriteIMSelection(DocWriter& w, const IMSelection& s)
{
	w.Bool(s.isRect);
	if (!s.isRect) {
		float p[2] = {float(s.x), float(s.y)};
		w.Point(p);
		ArchiveBitmap mask;
		mask.colorSpace = 0x0001;	// B_GRAY1
		mask.bytesPerRow = (uint32_t(s.width) + 31) / 32 * 4;
		mask.bounds[2] = float(s.width - 1);
		mask.bounds[3] = float(s.height - 1);
		mask.bits.assign(size_t(mask.bytesPerRow) * size_t(s.height), 0);
		for (int v = 0; v < s.height; v++)
			for (int u = 0; u < s.width; u++)
				if (s.bits[size_t(v) * size_t(s.width) + size_t(u)])
					mask.bits[size_t(v) * mask.bytesPerRow + size_t(u >> 3)]
						|= uint8_t(0x80 >> (u & 7));
		WriteBitmap(w, mask);
		return;
	}
	float definition[4] = {0, 0, float(s.width - 1), float(s.height - 1)};
	w.Rect(definition);
	IMRect m = s.Mask();
	float mask[4] = {float(m.left), float(m.top), float(m.right), float(m.bottom)};
	w.Rect(mask);
}


IMSelection
CombineSelection(uint32_t modifiers, const IMSelection& old, const IMSelection& tool)
{
	if ((modifiers & (kIMShift | kIMCommand)) == 0)
		return tool;
	IMSelection result = old;
	if (modifiers & kIMShift) {
		if (modifiers & kIMCommand)
			result.Intersect(tool);
		else
			result.Add(tool);
	} else
		result.Subtract(tool);
	return result;
}


void
TrimToBackground(const ArchiveBitmap& image, IMSelection& selection, IMColor back)
{
	std::vector<uint8_t> kept(selection.bits.size(), 0);
	IMRect m = selection.Mask();
	for (int py = m.top; py <= m.bottom; py++)
		for (int px = m.left; px <= m.right; px++)
			if (selection.Contains(px, py) && PixelAt(image, px, py) != back)
				kept[size_t(py - selection.y) * size_t(selection.width)
					+ size_t(px - selection.x)] = 1;
	selection.SetBits(kept, false);
}


void
MarqueeRect(float anchorX, float anchorY, float x, float y, int& left, int& top, int& right,
	int& bottom)
{
	float ax = anchorX - 0.5f, ay = anchorY - 0.5f, px = x - 0.5f, py = y - 0.5f;
	left = int(floorf(std::min(ax, px) + 0.5f));
	top = int(floorf(std::min(ay, py) + 0.5f));
	right = int(floorf(std::max(ax, px) + 0.5f));
	bottom = int(floorf(std::max(ay, py) + 0.5f));
}


void
LassoFill(const std::vector<std::array<int, 2>>& points, IMSelection& selection)
{
	std::vector<uint8_t> mask(selection.bits.size(), 0);
	int w = selection.width, h = selection.height;
	auto put = [&](int px, int py) {
		px -= selection.x;
		py -= selection.y;
		if (px >= 0 && py >= 0 && px < w && py < h)
			mask[size_t(py) * size_t(w) + size_t(px)] = 1;
	};
	size_t n = points.size();
	// StrokePolygon (closed): a pixel wide line from each point to the next
	for (size_t i = 0; i < n; i++) {
		int x0 = points[i][0], y0 = points[i][1];
		int x1 = points[(i + 1) % n][0], y1 = points[(i + 1) % n][1];
		int dx = std::abs(x1 - x0), dy = -std::abs(y1 - y0);
		int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx + dy;
		while (true) {
			put(x0, y0);
			if (x0 == x1 && y0 == y1)
				break;
			int e2 = 2 * err;
			if (e2 >= dy) {
				err += dy;
				x0 += sx;
			}
			if (e2 <= dx) {
				err += dx;
				y0 += sy;
			}
		}
	}
	// FillPolygon: the pixels whose centers are inside (even-odd)
	if (n >= 3) {
		int top = points[0][1], bottom = points[0][1];
		for (const auto& p : points) {
			top = std::min(top, p[1]);
			bottom = std::max(bottom, p[1]);
		}
		std::vector<float> crossings;
		for (int py = top; py <= bottom; py++) {
			float cy = float(py) + 0.5f;
			crossings.clear();
			for (size_t i = 0; i < n; i++) {
				float x0 = float(points[i][0]) + 0.5f, y0 = float(points[i][1]) + 0.5f;
				float x1 = float(points[(i + 1) % n][0]) + 0.5f;
				float y1 = float(points[(i + 1) % n][1]) + 0.5f;
				if ((y0 <= cy && y1 > cy) || (y1 <= cy && y0 > cy))
					crossings.push_back(x0 + (cy - y0) * (x1 - x0) / (y1 - y0));
			}
			std::sort(crossings.begin(), crossings.end());
			for (size_t k = 0; k + 1 < crossings.size(); k += 2)
				for (int px = int(ceilf(crossings[k] - 0.5f));
						float(px) + 0.5f <= crossings[k + 1]; px++)
					put(px, py);
		}
	}
	selection.SetBits(mask, false);
}


bool
MagicWand(const ArchiveBitmap& image, int x, int y, float distance, IMSelection& selection)
{
	int width = image.Width(), height = image.Height();
	if (image.bits.empty() || x < 0 || y < 0 || x >= width || y >= height)
		return false;
	selection.Redefine(width, height);
	std::vector<uint8_t>& done = selection.bits;
	IMColor seed = PixelAt(image, x, y);
	auto take = [&](int px, int py) {
		size_t i = size_t(py) * size_t(width) + size_t(px);
		if (done[i] || !ColorsAlike(seed, PixelAt(image, px, py), distance))
			return false;
		done[i] = 1;
		return true;
	};
	// Heckbert's seed fill, as the bucket's (at most 40000 spans)
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
			if (px > s.right + 1 && stack.size() < 40000 && row - s.dy >= 0
					&& row - s.dy < height)
				stack.push_back({row, s.right + 1, px - 1, -s.dy});
skip:
			for (px++; px <= s.right && !take(px, row); px++)
				;
			start = px;
		} while (px <= s.right);
	}
	selection.CalcBounds();
	return true;
}


void
FindColor(const ArchiveBitmap& image, IMColor fore, float distance, IMSelection& selection)
{
	int width = image.Width(), height = image.Height();
	bool all = selection.Empty();
	IMSelection found;
	found.Redefine(width, height);
	for (int py = 0; py < height; py++)
		for (int px = 0; px < width; px++)
			if ((all || selection.Contains(px, py))
					&& ColorsAlike(fore, PixelAt(image, px, py), distance))
				found.bits[size_t(py) * size_t(width) + size_t(px)] = 1;
	found.CalcBounds();
	selection = std::move(found);
}


namespace {

// CheckConstraintAngle: the line through the start at `angle` degrees (and
// at -angle) the point is nearest, measured along y (`alongY`) or along x
void
CheckConstraintAngle(float angle, bool& alongY, float& best, float& nearest, float dx, float dy)
{
	for (float a : {angle, -angle}) {
		float t = float(tan(double(a * 3.1415927f / 180.0f)));
		float d = fabsf(dy - t * dx);
		if (d < nearest) {
			nearest = d;
			best = a;
			alongY = true;
		}
		if (t != 0) {
			d = fabsf(dx - dy / t);
			if (d < nearest) {
				nearest = d;
				best = a;
				alongY = false;
			}
		}
	}
}

} // namespace


void
ShiftConstrain(float startX, float startY, float& x, float& y, float step)
{
	float dx = x - startX, dy = y - startY;
	float nearest = INFINITY, best = 0;
	bool alongY = false;
	int s = int(lroundf(step));
	if (unsigned(s - 1) > 44)
		s = 45;
	for (int a = 0, b = 90; a < 46; a += s, b -= s) {
		CheckConstraintAngle(float(a), alongY, best, nearest, dx, dy);
		CheckConstraintAngle(float(b), alongY, best, nearest, dx, dy);
	}
	float t = float(tan(double(best * 3.1415927f / 180.0f)));
	if (alongY)
		y = t * dx + startY;
	else
		x = dy / t + startX;
}


IMColor
BlendOver(IMColor d, IMColor s)
{
	int alpha = s.a, keep = 255 - s.a;
	IMColor out;
	out.r = uint8_t((s.r * alpha + d.r * keep) / 255);
	out.g = uint8_t((s.g * alpha + d.g * keep) / 255);
	out.b = uint8_t((s.b * alpha + d.b * keep) / 255);
	out.a = uint8_t(std::max(int(d.a), std::min(255, alpha + int(d.a))));
	return out;
}


namespace {

ArchiveBitmap
BlankLike(const ArchiveBitmap& image, int width, int height)
{
	ArchiveBitmap b;
	b.colorSpace = image.colorSpace;
	b.bytesPerRow = uint32_t(std::max(0, width)) * 4;
	b.bounds[0] = b.bounds[1] = 0;
	b.bounds[2] = float(width - 1);
	b.bounds[3] = float(height - 1);
	b.bits.assign(size_t(b.bytesPerRow) * size_t(std::max(0, height)), 0);
	return b;
}


bool
InImage(const ArchiveBitmap& b, int x, int y)
{
	return x >= 0 && y >= 0 && x < b.Width() && y < b.Height() && !b.bits.empty();
}


// a pixel's bytes as they are (B_RGB32's alpha byte too)
void
CopyPixel(const ArchiveBitmap& from, int fx, int fy, ArchiveBitmap& to, int tx, int ty)
{
	const uint8_t* s = &from.bits[size_t(fy) * from.bytesPerRow + size_t(fx) * 4];
	uint8_t* d = &to.bits[size_t(ty) * to.bytesPerRow + size_t(tx) * 4];
	std::copy(s, s + 4, d);
}


// DrawBitmapMasked: the floating bitmap at the mask's top left, where the
// selection is on
void
DrawFloating(ArchiveBitmap& image, const ArchiveBitmap& floating, const IMSelection& s,
	bool alpha)
{
	IMRect m = s.Mask();
	if (!m.Valid() || floating.bits.empty())
		return;
	for (int fy = 0; fy < floating.Height(); fy++)
		for (int fx = 0; fx < floating.Width(); fx++) {
			int px = m.left + fx, py = m.top + fy;
			if (!InImage(image, px, py) || !s.Contains(px, py))
				continue;
			if (alpha)
				SetPixel(image, px, py, BlendOver(PixelAt(image, px, py),
					PixelAt(floating, fx, fy)));
			else
				CopyPixel(floating, fx, fy, image, px, py);
		}
}

} // namespace


void
LiftBits(IMPart& part, IMSelection& s, bool erase)
{
	IMRect m = s.Mask();
	if (!m.Valid()) {
		part.floating = ArchiveBitmap();
		return;
	}
	ArchiveBitmap floating = BlankLike(part.image, m.right - m.left + 1, m.bottom - m.top + 1);
	IMColor back;
	back.r = part.colors[1][0];
	back.g = part.colors[1][1];
	back.b = part.colors[1][2];
	back.a = part.colors[1][3];
	for (int py = m.top; py <= m.bottom; py++)
		for (int px = m.left; px <= m.right; px++) {
			if (!InImage(part.image, px, py) || !s.Contains(px, py))
				continue;
			CopyPixel(part.image, px, py, floating, px - m.left, py - m.top);
			if (erase)
				SetPixel(part.image, px, py, back);
		}
	part.floating = std::move(floating);
}


void
StampBits(IMPart& part, IMSelection& s, bool alpha, bool keep)
{
	if (part.floating.bits.empty())
		return;
	DrawFloating(part.image, part.floating, s, alpha);
	if (keep)
		return;
	part.floating = ArchiveBitmap();
	s.Clip(part.image.Width(), part.image.Height());
}


ArchiveBitmap
ComposeFloating(const IMPart& part, const IMSelection& s)
{
	ArchiveBitmap shown = part.image;
	DrawFloating(shown, part.floating, s, true);
	return shown;
}


ArchiveBitmap
MainBits(const ArchiveBitmap& image, const IMRect& r)
{
	ArchiveBitmap bits = BlankLike(image, r.right - r.left + 1, r.bottom - r.top + 1);
	for (int py = r.top; py <= r.bottom; py++)
		for (int px = r.left; px <= r.right; px++)
			if (InImage(image, px, py))
				CopyPixel(image, px, py, bits, px - r.left, py - r.top);
	return bits;
}


std::vector<std::array<int, 4>>
SelectionOutline(const IMSelection& s)
{
	std::vector<std::array<int, 4>> segments;
	if (s.Empty())
		return segments;
	if (s.isRect) {
		IMRect m = s.Mask();
		int l = m.left, t = m.top, r = m.right + 1, b = m.bottom + 1;
		segments.push_back({l, t, r, t});
		segments.push_back({r, t, r, b});
		segments.push_back({r, b, l, b});
		segments.push_back({l, b, l, t});
		return segments;
	}
	auto on = [&](int u, int v) {
		return u >= 0 && v >= 0 && u < s.width && v < s.height
			&& s.bits[size_t(v) * size_t(s.width) + size_t(u)] != 0;
	};
	IMRect b = s.bounds;
	// edges along each row boundary, then along each column boundary
	for (int v = b.top; v <= b.bottom + 1; v++) {
		int run = -1;
		for (int u = b.left; u <= b.right + 1; u++) {
			bool edge = u <= b.right && on(u, v - 1) != on(u, v);
			if (edge && run < 0)
				run = u;
			else if (!edge && run >= 0) {
				segments.push_back({run + s.x, v + s.y, u + s.x, v + s.y});
				run = -1;
			}
		}
	}
	for (int u = b.left; u <= b.right + 1; u++) {
		int run = -1;
		for (int v = b.top; v <= b.bottom + 1; v++) {
			bool edge = v <= b.bottom && on(u - 1, v) != on(u, v);
			if (edge && run < 0)
				run = v;
			else if (!edge && run >= 0) {
				segments.push_back({u + s.x, run + s.y, u + s.x, v + s.y});
				run = -1;
			}
		}
	}
	return segments;
}

} // namespace gobe
