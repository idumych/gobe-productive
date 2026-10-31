// TInkSet's pattern and gradient set resources (TRsrHelper: little-endian
// u32s, a color as four of them, floats as text).
#include "InkSets.h"

#include <cstdlib>
#include <string>

#include "DocArchive.h"

namespace gobe {

namespace {

struct Reader {
	const std::vector<uint8_t>& d;
	size_t at = 0;
	bool ok = true;

	uint32_t U32()
	{
		if (at + 4 > d.size()) {
			ok = false;
			return 0;
		}
		uint32_t v = d[at] | d[at + 1] << 8 | d[at + 2] << 16 | uint32_t(d[at + 3]) << 24;
		at += 4;
		return v;
	}

	Color GetColor()
	{
		Color c;
		c.r = uint8_t(U32());
		c.g = uint8_t(U32());
		c.b = uint8_t(U32());
		c.a = uint8_t(U32());
		return c;
	}

	// TRsrHelper::GetFloatStr: a NUL-terminated number, 0 if empty
	float FloatString()
	{
		std::string text;
		while (at < d.size() && d[at] != 0)
			text += char(d[at++]);
		if (at >= d.size()) {
			ok = false;
			return 0;
		}
		at++;
		return text.empty() ? 0 : float(atof(text.c_str()));
	}
};

} // namespace


bool
ParsePatternSet(const std::vector<uint8_t>& resource, InkSet& set)
{
	set = InkSet();
	if (resource.size() < 8 || (resource.size() - 8) % 64 != 0)
		return false;
	Reader r{resource};
	set.columns = int(r.U32());
	set.cell = int(r.U32());
	while (r.at < resource.size()) {
		Ink ink;
		ink.set = true;
		ink.color = r.GetColor();
		ink.low = r.GetColor();
		for (uint8_t& b : ink.pattern)
			b = uint8_t(r.U32());
		set.inks.push_back(ink);
	}
	return r.ok;
}


bool
ParseGradientSet(const std::vector<uint8_t>& resource, InkSet& set)
{
	set = InkSet();
	Reader r{resource};
	set.columns = int(r.U32());
	set.cell = int(r.U32());
	while (r.ok) {
		uint32_t type = r.U32();
		if (!r.ok || type == Sig("gEND"))
			break;
		Ink ink;
		ink.set = true;
		ink.gradient = true;
		ink.gradientType = type;
		ink.colorCount = int(r.U32());
		for (Color& c : ink.colors)
			c = r.GetColor();
		ink.color = ink.colors[0];
		// TGradient::SetAngle takes the u32 as a float
		ink.angle = float(int32_t(r.U32()));
		for (int i = 0; i < 4; i++)
			r.U32();		// a rectangle
		float x = r.FloatString(), y = r.FloatString();
		if (type == Sig("GDrl")) {
			ink.center[0] = x;
			ink.center[1] = y;
		}
		if (r.ok)
			set.inks.push_back(ink);
	}
	return r.ok && !set.inks.empty();
}

} // namespace gobe
