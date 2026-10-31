#include "Styles.h"

#include <algorithm>

#include <zlib.h>

#include "DocWriter.h"

namespace gobe {

// TPenInk::Read into an Ink: solid -> TColorPattern; otherwise a fill:
// kind, size, data ('FGRD' -> TGradientFactory::ReadGradient)
bool
ReadInk(DocArchive& a, Ink& ink)
{
	ink.set = true;
	if (a.Bool()) {
		ink.color.r = a.UInt8();
		ink.color.g = a.UInt8();
		ink.color.b = a.UInt8();
		ink.color.a = a.UInt8();
		ink.low.r = a.UInt8();
		ink.low.g = a.UInt8();
		ink.low.b = a.UInt8();
		ink.low.a = a.UInt8();
		for (uint8_t& b : ink.pattern)
			b = a.UInt8();
		return true;
	}
	ink.gradient = true;
	uint32_t kind = a.UInt32();
	uint32_t size = a.UInt32();
	size_t start = a.Position();
	if (kind == Sig("FGRD")) {
		a.CheckSig(Sig("GRAD"));
		ink.gradient = true;
		ink.gradientType = a.UInt32();
		ink.colorCount = std::max(2, std::min(4, int(a.UInt16())));
		ink.echo = a.Float();
		for (Color& c : ink.colors) {
			c.r = a.UInt8();
			c.g = a.UInt8();
			c.b = a.UInt8();
			c.a = a.UInt8();
		}
		ink.color = ink.colors[0];
		if (ink.gradientType == Sig("GDdr"))
			ink.angle = a.Float();
		else if (ink.gradientType == Sig("GDrl")) {
			a.Point(ink.center);
			if (a.Position() + 4 <= start + size)
				ink.angle = a.Float();
		}
	}
	else if (kind == Sig("FPAT")) {
		// TFillPattern: 'TPAT', a byte, the bitmap (TOutputArchive::
		// WriteBitmap: 'BMAP', its size, a bool, the color space, bytes
		// per row, the bounds, compressed, the data's size, the data,
		// 'BmaP'; TInputArchive::Read starts more than 0xfff bytes at a
		// 4-byte boundary of the file). The fill's size is a few bytes off
		// in the ink sets: read through it; the pixels kept as they are.
		// (a property's fill ends the data: its size is right)
		if (start + size != a.Data().size()) {
			try {
				a.CheckSig(Sig("TPAT"));
				a.UInt8();
				a.CheckSig(Sig("BMAP"));
				uint32_t bytes = a.UInt32();
				if (bytes != 0) {
					a.Bool();
					a.UInt32();
					a.UInt32();
					float bounds[4];
					a.Rect(bounds);
					bool compressed = a.FileFormatVersion() > 0x15 && a.Bool();
					if (compressed)
						bytes = a.UInt32();
					if (bytes > 0xfff)
						a.Seek((a.Position() + 3) & ~size_t(3));
					a.Bytes(bytes);
				}
				a.CheckSig(Sig("BmaP"));
				size_t end = a.Position();
				a.Seek(start - 8);
				ink.otherFill = a.Bytes(end - (start - 8));
				return true;
			} catch (const ArchiveError&) {
			}
		}
		a.Seek(start - 8);
		ink.otherFill = a.Bytes(size_t(size) + 8);
	} else {
		a.Seek(start - 8);
		ink.otherFill = a.Bytes(size_t(size) + 8);
	}
	// the size counts from after itself; skip whatever wasn't read
	if (a.Position() < start + size)
		a.Seek(start + size);
	return true;
}


std::vector<uint8_t>
InkBytes(const Ink& ink)
{
	DocWriter w;
	auto color = [&](const Color& c) {
		w.UInt8(c.r);
		w.UInt8(c.g);
		w.UInt8(c.b);
		w.UInt8(c.a);
	};
	if (!ink.otherFill.empty()) {
		w.Bool(false);
		w.Bytes(ink.otherFill);
		return w.Data();
	}
	if (!ink.gradient) {
		w.Bool(true);
		color(ink.color);
		color(ink.low);
		w.Bytes(ink.pattern, 8);
		return w.Data();
	}
	DocWriter g;
	g.Sig(Sig("GRAD"));
	g.UInt32(ink.gradientType);
	g.UInt16(uint16_t(ink.colorCount));
	g.Float(ink.echo);
	for (const Color& c : ink.colors) {
		g.UInt8(c.r);
		g.UInt8(c.g);
		g.UInt8(c.b);
		g.UInt8(c.a);
	}
	if (ink.gradientType == Sig("GDrl"))
		g.Point(ink.center);
	g.Float(ink.angle);
	w.Bool(false);
	w.Sig(Sig("FGRD"));
	w.UInt32(uint32_t(g.Data().size()));
	w.Bytes(g.Data());
	return w.Data();
}


Property
InkProperty(uint32_t type, const Ink& ink)
{
	Property p;
	p.type = type;
	p.data = InkBytes(ink);
	return p;
}


bool
ApplyShapeProperty(const Property& property, ShapeStyle& style)
{
	DocArchive a(property.data);
	a.SetVersions(2, 0, 0x1e);
	try {
		switch (property.type) {
			case Sig("FCol"):
				return ReadInk(a, style.fill);
			case Sig("PCol"):
				return ReadInk(a, style.pen);
			case Sig("BCol"):
				return ReadInk(a, style.background);
			case Sig("PENz"):
				style.penSize = a.Float();
				return true;
			case Sig("FTra"):
				style.fillOpacity = a.Float();
				return true;
			case Sig("PTra"):
				style.penOpacity = a.Float();
				return true;
			case Sig("BTra"):
				style.backgroundOpacity = a.Float();
				return true;
			case Sig("SWra"):
				return ReadWrapProperty(property, style.wrap);
		}
	} catch (const ArchiveError&) {
	}
	return false;
}


ShapeStyle
ResolveShapeStyle(const StyleMgr& styles, uint32_t index)
{
	ShapeStyle style;
	std::vector<const Style*> chain;
	for (int depth = 0; index < styles.styles.size() && depth < 32; depth++) {
		const Style& s = styles.styles[index];
		chain.push_back(&s);
		index = s.links[0];
	}
	for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
		for (uint32_t p : (*it)->props)
			if (p < styles.properties.size())
				ApplyShapeProperty(styles.properties[p], style);
	}
	return style;
}


bool
ApplyProperty(const Property& property, TextStyle& style)
{
	DocArchive a(property.data);
	try {
		switch (property.type) {
			case Sig("TSiz"):	// TPropTextSize: TPropShort
				style.size = a.UInt16();
				break;
			case Sig("TFon"):	// TPropTextFont: family, style
				style.family = a.String();
				style.face = a.String();
				break;
			case Sig("TFac"):	// TPropTextFace: face bits, a bool
				style.faceFlags = a.UInt32();
				a.Bool();
				break;
			case Sig("TCol"):	// TPropTextColor: TPropInk
			{
				Ink ink;
				ReadInk(a, ink);
				style.color = ink.color;
				break;
			}
			case Sig("TTra"):	// TPropTextTransparent: TPropFloat
				style.opacity = a.Float();
				break;
			case Sig("PAlg"):	// TPropParAlign: TPropShort
				style.alignment = a.UInt16();
				break;
			case Sig("PLef"):	// TPropIndent: TPropFloat
				style.leftIndent = a.Float();
				break;
			case Sig("PInd"):
				style.firstIndent = a.Float();
				break;
			case Sig("PRig"):
				style.rightIndent = a.Float();
				break;
			case Sig("SpaB"):	// TParSpaceProperty: in lines?, value
				style.spaceBeforeLines = a.Bool();
				style.spaceBefore = a.Float();
				break;
			case Sig("SpaA"):
				style.spaceAfterLines = a.Bool();
				style.spaceAfter = a.Float();
				break;
			case Sig("LSpa"):	// TPropParLineSpacing: multiple?, value
				style.lineSpacingMultiple = a.Bool();
				style.lineSpacing = a.Float();
				break;
			case Sig("PTab"): {	// TPropParTab::ReadPropData
				TabStop tab;
				tab.position = a.Float();
				tab.type = a.UInt16();
				tab.fill = a.UInt16();
				tab.alignChar = a.UInt32();
				tab.fillChar = a.UInt32();
				auto it = std::find_if(style.tabs.begin(), style.tabs.end(),
					[&](const TabStop& t) { return t.position == tab.position; });
				if (it != style.tabs.end())
					*it = tab;
				else
					style.tabs.push_back(tab);
				std::sort(style.tabs.begin(), style.tabs.end(),
					[](const TabStop& x, const TabStop& y) { return x.position < y.position; });
				break;
			}
			default:
				return false;
		}
	} catch (const ArchiveError&) {
		return false;
	}
	return a.Position() == a.Size();
}


namespace {

// TStyle::UpdateUnionSet's order: the base's, the applied style's and the
// owner's properties (each with what they inherit), then its own
void
ApplyStyleChain(const StyleMgr& styles, uint32_t index, TextStyle& style, int depth)
{
	if (index >= styles.styles.size() || depth > 32)
		return;
	const Style& s = styles.styles[index];
	for (uint32_t link : s.links)
		if (link != index)
			ApplyStyleChain(styles, link, style, depth + 1);
	for (uint32_t p : s.props)
		if (p < styles.properties.size())
			ApplyProperty(styles.properties[p], style);
}

} // namespace


TextStyle
ResolveTextStyle(const StyleMgr& styles, uint32_t index)
{
	TextStyle style;
	ApplyStyleChain(styles, index, style, 0);
	return style;
}


// --- encoders (inverse of ApplyProperty) ---

Property
TextSizeProperty(float size)
{
	DocWriter w;
	w.UInt16(uint16_t(size));
	Property p;
	p.type = Sig("TSiz");
	p.data = w.Data();
	return p;
}

Property
TextFontProperty(const std::string& family, const std::string& face)
{
	DocWriter w;
	w.String(family);
	w.String(face);
	Property p;
	p.type = Sig("TFon");
	p.data = w.Data();
	return p;
}

Property
TextFaceProperty(uint32_t faceFlags)
{
	DocWriter w;
	w.UInt32(faceFlags);
	w.Bool(true);		// always set in the documents
	Property p;
	p.type = Sig("TFac");
	p.data = w.Data();
	return p;
}

Property
TextColorProperty(Color color)
{
	DocWriter w;
	w.Bool(true);		// solid ink
	w.UInt8(color.r);
	w.UInt8(color.g);
	w.UInt8(color.b);
	w.UInt8(color.a);
	// as in every document: 'wtw' and a solid pattern
	static const uint8_t kTail[12] = {0x77, 0x74, 0x77, 0x00,
		0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
	w.Bytes(kTail, 12);
	Property p;
	p.type = Sig("TCol");
	p.data = w.Data();
	return p;
}

Property
AlignmentProperty(int alignment)
{
	DocWriter w;
	w.UInt16(uint16_t(alignment));
	Property p;
	p.type = Sig("PAlg");
	p.data = w.Data();
	return p;
}

Property
OpacityProperty(uint32_t type, float opacity)
{
	DocWriter w;
	w.Float(opacity);
	Property p;
	p.type = type;
	p.data = w.Data();
	return p;
}

Property
IndentProperty(uint32_t type, float value)
{
	DocWriter w;
	w.Float(value);
	Property p;
	p.type = type;
	p.data = w.Data();
	return p;
}

Property
SpacingProperty(uint32_t type, float value, bool lines)
{
	DocWriter w;
	w.Bool(lines);		// set in all the documents
	w.Float(value);
	Property p;
	p.type = type;
	p.data = w.Data();
	return p;
}

Property
LineSpacingProperty(float value, bool multiple)
{
	DocWriter w;
	w.Bool(multiple);
	w.Float(value);
	Property p;
	p.type = Sig("LSpa");
	p.data = w.Data();
	return p;
}


Property
TabsProperty(const std::vector<TabStop>& tabs)
{
	DocWriter w;
	// Encode each tab stop exactly as ApplyProperty reads it:
	// position (float), type (uint16), fill (uint16), alignChar (uint32),
	// fillChar (uint32) = 20 bytes each
	for (const TabStop& tab : tabs) {
		w.Float(tab.position);
		w.UInt16(uint16_t(tab.type));
		w.UInt16(uint16_t(tab.fill));
		w.UInt32(tab.alignChar);
		w.UInt32(tab.fillChar);
	}
	Property p;
	p.type = Sig("PTab");
	p.data = w.Data();
	return p;
}

Property
TabProperty(const TabStop& tab)
{
	return TabsProperty({tab});
}

bool
FillPatternPixels(const Ink& ink, int& width, int& height, std::vector<uint8_t>& rgba)
{
	// 'FPAT', its size, 'TPAT', solid (a color pattern, not a bitmap),
	// 'BMAP', the pixels' size, a bool, the color space, bytes per row,
	// the bounds, compressed, the packed size, the data, 'BmaP'
	const std::vector<uint8_t>& d = ink.otherFill;
	if (d.size() < 14)
		return false;
	DocArchive a(d);
	a.SetVersions(2, 0, 0x1e);
	try {
		if (a.UInt32() != Sig("FPAT"))
			return false;
		a.UInt32();
		a.CheckSig(Sig("TPAT"));
		if (a.UInt8() != 0)
			return false;
		a.CheckSig(Sig("BMAP"));
		uint32_t size = a.UInt32();
		if (size == 0)
			return false;
		a.Bool();
		uint32_t space = a.UInt32();
		uint32_t bytesPerRow = a.UInt32();
		float bounds[4];
		a.Rect(bounds);
		width = int(bounds[2] - bounds[0] + 1);
		height = int(bounds[3] - bounds[1] + 1);
		// B_RGB32 and B_RGBA32: B, G, R, A
		if ((space != 0x0008 && space != 0x2008) || width <= 0 || height <= 0
				|| bytesPerRow < uint32_t(width) * 4 || size < bytesPerRow * uint32_t(height))
			return false;
		std::vector<uint8_t> bits;
		if (a.Bool()) {
			uint32_t packed = a.UInt32();
			// (the file's 4-byte boundary isn't known here: up to 3 zero
			// bytes before the zlib data, whose first byte never is)
			const std::vector<uint8_t>& all = a.Data();
			size_t at = a.Position();
			for (int pad = 0; pad < 3 && at < all.size() && all[at] == 0; pad++)
				at++;
			if (at + packed > all.size())
				return false;
			// (some streams in the ink sets end early, their pixels all
			// there: inflate as far as it goes)
			bits.resize(size);
			z_stream z = {};
			if (inflateInit(&z) != Z_OK)
				return false;
			z.next_in = const_cast<Bytef*>(all.data() + at);
			z.avail_in = packed;
			z.next_out = bits.data();
			z.avail_out = size;
			int result = inflate(&z, Z_FINISH);
			size_t out = size - z.avail_out;
			inflateEnd(&z);
			if (result != Z_STREAM_END && out < size_t(bytesPerRow) * height)
				return false;
		} else
			bits = a.Bytes(size);
		rgba.resize(size_t(width) * height * 4);
		bool alpha = space == 0x2008;
		for (int y = 0; y < height; y++)
			for (int x = 0; x < width; x++) {
				const uint8_t* p = &bits[size_t(y) * bytesPerRow + size_t(x) * 4];
				uint8_t* q = &rgba[(size_t(y) * width + x) * 4];
				q[0] = p[2];
				q[1] = p[1];
				q[2] = p[0];
				q[3] = alpha ? p[3] : 255;
			}
	} catch (const ArchiveError&) {
		return false;
	}
	return true;
}

} // namespace gobe
