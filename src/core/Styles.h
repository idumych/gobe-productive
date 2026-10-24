// Text styles: decoding the properties a style holds and resolving the
// inheritance chain (a style's own properties override its parent's,
// links[0]). Property payloads follow each TProp* class's ReadPropData.
#ifndef GOBE_STYLES_H
#define GOBE_STYLES_H

#include <cstdint>
#include <string>
#include <vector>

#include "DocSections.h"

namespace gobe {

struct Color {
	uint8_t r = 0, g = 0, b = 0, a = 255;
	bool operator==(const Color& o) const
		{ return r == o.r && g == o.g && b == o.b && a == o.a; }
	bool operator!=(const Color& o) const
		{ return !(*this == o); }
	bool operator<(const Color& o) const
		{ return r < o.r || (r == o.r && g < o.g)
			|| (r == o.r && g == o.g && b < o.b)
			|| (r == o.r && g == o.g && b == o.b && a < o.a); }
};

// TPropParTab: one tab stop (a style can hold several; one at the same
// position replaces another)
struct TabStop {
	float position = 0;			// from the text's left edge
	int type = 0;				// 0 left, 1 center, 2 right, 3 align on alignChar
	int fill = 0;				// TabFillType: 0 none, otherwise leader of fillChar
	uint32_t alignChar = '.';
	uint32_t fillChar = '.';
};

// The attributes the text renderer needs; `has*` say which were set.
struct TextStyle {
	std::string family = "Dutch801 Rm BT";		// TFon (Gobe's default)
	std::string face;
	float size = 12;							// TSiz
	uint32_t faceFlags = 0;						// TFac: bold/italic/... bits
	Color color;								// TCol
	int alignment = 0;							// PAlg: 0 left, 1 center, 2 right, 3 justify (TLineRun)
	float leftIndent = 0, firstIndent = 0, rightIndent = 0;	// PLef, PInd, PRig
	float spaceBefore = 0, spaceAfter = 0;		// SpaB, SpaA
	// TParSpaceProperty: the value is in lines (of the first or last line's
	// height) when set, as in every document, else in points
	bool spaceBeforeLines = true, spaceAfterLines = true;
	// LSpa (TPropParLineSpacing): a multiple of the line height (adds
	// (multiple - 1) x ascent of leading) or an exact height in points
	float lineSpacing = 1;
	bool lineSpacingMultiple = true;
	std::vector<TabStop> tabs;					// PTab, sorted by position
};

// An ink (TPenInk): a solid color, or a gradient fill (TFillGradient).
struct Ink {
	bool set = false;
	bool gradient = false;
	Color color;					// solid, or the gradient's first color
	Color colors[4];				// gradient colors
	int colorCount = 2;				// how many of them are used
	uint32_t gradientType = 0;		// 'GDdr' directional, 'GDrl' radial
	float angle = 0;				// directional: degrees
	float center[2] = {0.5f, 0.5f};	// radial
};

// Shape/frame drawing attributes.
struct ShapeStyle {
	Ink fill;						// FCol
	Ink pen;						// PCol
	Ink background;					// BCol (frames)
	float penSize = 1;				// PENz
	// FTra/PTra/BTra ("transparent" properties) hold the opacity: 1 drawn,
	// 0 invisible (page frames have FTra 0; the newsletter's dark bars 1)
	float fillOpacity = 1;			// FTra
	float penOpacity = 1;			// PTra
	float backgroundOpacity = 0;	// BTra
};

// ReadInk: parse an ink (solid color or gradient) from a DocArchive.
bool ReadInk(DocArchive& a, Ink& ink);

bool ApplyShapeProperty(const Property& property, ShapeStyle& style);
ShapeStyle ResolveShapeStyle(const StyleMgr& styles, uint32_t index);

// Applies one property to `style`; returns false if the type isn't decoded
// (or the payload doesn't match what the type should hold).
bool ApplyProperty(const Property& property, TextStyle& style);

// Properties for text styling, encoded as the documents store them
// (the inverse of ApplyProperty).
Property TextSizeProperty(float size);
Property TextFontProperty(const std::string& family, const std::string& face);
Property TextFaceProperty(uint32_t faceFlags);
Property TextColorProperty(Color color);
Property AlignmentProperty(int alignment);
Property IndentProperty(uint32_t type, float value);	// 'PLef', 'PInd' or 'PRig'
Property SpacingProperty(uint32_t type, float value, bool lines = true);	// 'SpaB' or 'SpaA'
Property LineSpacingProperty(float value, bool multiple);
Property TabsProperty(const std::vector<TabStop>& tabs);
// One tab stop: a style holds one PTab property per tab (what TabsProperty
// gives for a single tab).
Property TabProperty(const TabStop& tab);

// The effective style for a style index (TStyleID), following parents.
TextStyle ResolveTextStyle(const StyleMgr& styles, uint32_t index);

} // namespace gobe

#endif
