#include "Styles.h"

#include <algorithm>

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
		a.Bytes(4);
		a.Bytes(8);
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
		a.Float();		// midpoint/bias
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
			if (a.MajorUserVersion() < 2)
				a.Float();
		}
	}
	// the size counts from after itself; skip whatever wasn't read
	if (a.Position() < start + size)
		a.Seek(start + size);
	return true;
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
			case Sig("SpaB"):	// TParSpaceProperty: bool, value
				a.Bool();
				style.spaceBefore = a.Float();
				break;
			case Sig("SpaA"):
				a.Bool();
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


TextStyle
ResolveTextStyle(const StyleMgr& styles, uint32_t index)
{
	TextStyle style;
	// parents first, so a style's own properties win
	std::vector<const Style*> chain;
	for (int depth = 0; index < styles.styles.size() && depth < 32; depth++) {
		const Style& s = styles.styles[index];
		chain.push_back(&s);
		index = s.links[0];
	}
	for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
		for (uint32_t p : (*it)->props)
			if (p < styles.properties.size())
				ApplyProperty(styles.properties[p], style);
	}
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
SpacingProperty(uint32_t type, float value)
{
	DocWriter w;
	w.Bool(true);		// always set in the documents
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

} // namespace gobe
