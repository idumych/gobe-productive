// Text Wrap (TPropShapeWrap, TDialogTextWrap, TTextWrapMgr): a shape or a
// frame in a drawing the text flows around.
#ifndef GOBE_TEXTWRAP_H
#define GOBE_TEXTWRAP_H

#include <cstdint>
#include <vector>

#include "DocSections.h"

namespace gobe {

struct Document;

// TPropShapeWrap: the kind (0 None, 1 Regular, 2 Tight, 3 Top & Bottom),
// the side the text goes to (0 Both Sides, 1 Left, 2 Right, 3 Largest
// Side) and the distance from the text (a BRect: left, top, right,
// bottom). A property made without its message is Regular, both sides.
struct ShapeWrap {
	int32_t kind = 2;
	int32_t to = 0;
	float margins[4] = {0, 0, 0, 0};
};

// TShape +0x24: the shape (a frame: its FRAM shape's) wraps text
const uint32_t kShapeWraps = 0x40;

// the 'SWra' property (TPropShapeWrap::WritePropData: a BMessage 'Wrap'
// with "Property Type", "Wrap Kind", "Wrap To" and "margins", flattened as
// BeOS R5 did, between its size and 'MESG' and 'MesG')
Property WrapProperty(const ShapeWrap& wrap);
// TPropShapeWrap::ReadPropData/Restore; false if it isn't one
bool ReadWrapProperty(const Property& property, ShapeWrap& wrap);

// the flattened message itself (BMessage::Flatten, R5's format) and back
// (the int32, float and rect fields; R5's or its byte-swapped format)
struct FlatField {
	std::string name;
	uint32_t type = 0;
	std::vector<uint8_t> data;		// one item
};
std::vector<uint8_t> FlattenR5Message(uint32_t what, const std::vector<FlatField>& fields);
bool UnflattenR5Message(const std::vector<uint8_t>& data, uint32_t& what,
	std::vector<FlatField>& fields);

// TGRFormatCommand with an SWra: the shapes' wrap set (flag 0x40 on unless
// it's None), each shape's style given the property
bool SetShapeWrap(Document& doc, size_t part, size_t layer, size_t index, const ShapeWrap& wrap);
// the same for a frame (TFrame::SetStyle; its FRAM shape's flags)
bool SetFrameWrap(Document& doc, const uint32_t pointer[3], const ShapeWrap& wrap);

// TTextWrapMgr: where a line's text goes, from `left` to `right` between
// `top` and `bottom`, past the things it wraps around (their rectangles,
// margins added). Top & Bottom leaves no text beside one; Both Sides is
// laid out as the larger side (the port's lines have one run). False: no
// room on this line, `next` is how far down to try again.
struct WrapObstacle {
	float rect[4];		// left, top, right, bottom, the margins added
	int32_t kind;
	int32_t to;
};
bool WrapLine(const std::vector<WrapObstacle>& obstacles, float top, float bottom,
	float& left, float& right, float minimum, float& next);

} // namespace gobe

#endif
