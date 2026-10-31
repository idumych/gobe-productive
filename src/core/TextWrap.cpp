#include "TextWrap.h"

#include <algorithm>
#include <cstring>

#include "DocArchive.h"
#include "DocWriter.h"
#include "Document.h"
#include "Drawing.h"
#include "Editing.h"
#include "InlineParts.h"

namespace gobe {

namespace {

// BMessage's R5 format (MessageAdapter: r5_message_header, the field flags)
const uint32_t kFormatR5 = 0x464f4231;			// 'FOB1'
const uint8_t kFieldValid = 0x01, kFieldMini = 0x02, kFieldFixed = 0x04,
	kFieldSingle = 0x08;
const uint32_t kInt32Type = 0x4c4f4e47;			// B_INT32_TYPE 'LONG'
const uint32_t kRectType = 0x52454354;			// B_RECT_TYPE 'RECT'


void
Put32(std::vector<uint8_t>& out, uint32_t v)
{
	for (int i = 0; i < 4; i++)
		out.push_back(uint8_t(v >> (8 * i)));
}


uint32_t
Get32(const std::vector<uint8_t>& in, size_t at, bool swap)
{
	uint32_t v = 0;
	for (int i = 0; i < 4; i++)
		v |= uint32_t(in[at + i]) << (8 * (swap ? 3 - i : i));
	return v;
}


FlatField
Int32Field(const char* name, int32_t value)
{
	FlatField f{name, kInt32Type, {}};
	Put32(f.data, uint32_t(value));
	return f;
}


uint32_t
FloatBits(float v)
{
	uint32_t bits;
	std::memcpy(&bits, &v, 4);
	return bits;
}


float
BitsFloat(uint32_t bits)
{
	float v;
	std::memcpy(&v, &bits, 4);
	return v;
}

} // namespace


std::vector<uint8_t>
FlattenR5Message(uint32_t what, const std::vector<FlatField>& fields)
{
	std::vector<uint8_t> out;
	Put32(out, kFormatR5);
	Put32(out, 0);				// the checksum
	Put32(out, 0);				// the size
	Put32(out, what);
	out.push_back(1);			// R5_MESSAGE_FLAG_VALID
	for (const FlatField& f : fields) {
		// one fixed-size item, its size and name in a byte each
		bool mini = f.data.size() <= 255;
		out.push_back(uint8_t(kFieldValid | kFieldFixed | kFieldSingle | (mini ? kFieldMini : 0)));
		Put32(out, f.type);
		if (mini)
			out.push_back(uint8_t(f.data.size()));
		else
			Put32(out, uint32_t(f.data.size()));
		size_t length = std::min<size_t>(f.name.size(), 255);
		out.push_back(uint8_t(length));
		out.insert(out.end(), f.name.begin(), f.name.begin() + length);
		out.insert(out.end(), f.data.begin(), f.data.end());
	}
	out.push_back(0);
	uint32_t size = uint32_t(out.size());
	for (int i = 0; i < 4; i++)
		out[8 + i] = uint8_t(size >> (8 * i));
	// CalculateChecksum over the header after the checksum: its words
	// big-endian, then the bytes left
	uint32_t sum = 0, rest = 0;
	sum += uint32_t(out[8]) << 24 | uint32_t(out[9]) << 16 | uint32_t(out[10]) << 8 | out[11];
	sum += uint32_t(out[12]) << 24 | uint32_t(out[13]) << 16 | uint32_t(out[14]) << 8 | out[15];
	rest = out[16];
	sum += rest;
	for (int i = 0; i < 4; i++)
		out[4 + i] = uint8_t(sum >> (8 * i));
	return out;
}


bool
UnflattenR5Message(const std::vector<uint8_t>& data, uint32_t& what,
	std::vector<FlatField>& fields)
{
	if (data.size() < 17)
		return false;
	uint32_t magic = Get32(data, 0, false);
	bool swap;
	if (magic == kFormatR5)
		swap = false;
	else if (magic == 0x31424f46)
		swap = true;
	else
		return false;
	what = Get32(data, 12, swap);
	uint8_t flags = data[16];
	size_t at = 17;
	if (flags & 0x02)
		at += 4;		// the target
	if (flags & 0x04)
		at += 16;		// the reply: port, target, team, four flags
	fields.clear();
	while (at < data.size()) {
		uint8_t fieldFlags = data[at++];
		if ((fieldFlags & kFieldValid) == 0)
			return true;
		if (at + 4 > data.size())
			return false;
		uint32_t type = Get32(data, at, swap);
		at += 4;
		bool mini = (fieldFlags & kFieldMini) != 0;
		uint32_t count = 1;
		if ((fieldFlags & kFieldSingle) == 0) {
			if (mini)
				count = data.at(at++);
			else {
				count = Get32(data, at, swap);
				at += 4;
			}
		}
		uint32_t size;
		if (mini)
			size = data.at(at++);
		else {
			if (at + 4 > data.size())
				return false;
			size = Get32(data, at, swap);
			at += 4;
		}
		uint8_t length = data.at(at++);
		if (at + length + size > data.size())
			return false;
		std::string name(data.begin() + at, data.begin() + at + length);
		at += length;
		// a fixed-size field's first item; a variable one's after its size
		FlatField f{name, type, {}};
		if (fieldFlags & kFieldFixed) {
			uint32_t item = count > 0 ? size / count : 0;
			f.data.assign(data.begin() + at, data.begin() + at + item);
		} else if (size >= 4) {
			uint32_t item = std::min(Get32(data, at, swap), size - 4);
			f.data.assign(data.begin() + at + 4, data.begin() + at + 4 + item);
		}
		// (numbers in the reader's order)
		if (swap && (type == kInt32Type || type == kRectType))
			for (size_t k = 0; k + 4 <= f.data.size(); k += 4)
				std::reverse(f.data.begin() + k, f.data.begin() + k + 4);
		fields.push_back(std::move(f));
		at += size;
	}
	return true;
}


Property
WrapProperty(const ShapeWrap& wrap)
{
	FlatField margins{"margins", kRectType, {}};
	for (float v : wrap.margins)
		Put32(margins.data, FloatBits(v));
	std::vector<uint8_t> message = FlattenR5Message(0x57726170,		// 'Wrap'
		{Int32Field("Property Type", int32_t(Sig("SWra"))), Int32Field("Wrap Kind", wrap.kind),
		 Int32Field("Wrap To", wrap.to), margins});
	Property p;
	p.type = Sig("SWra");
	DocWriter w;
	w.UInt32(uint32_t(message.size()));
	w.Sig(Sig("MESG"));
	w.Bytes(message);
	w.Sig(Sig("MesG"));
	p.data = w.Data();
	return p;
}


bool
ReadWrapProperty(const Property& property, ShapeWrap& wrap)
{
	if (property.type != Sig("SWra"))
		return false;
	wrap = ShapeWrap();
	try {
		DocArchive a(property.data);
		uint32_t size = a.UInt32();
		if (size == 0)
			return true;
		a.CheckSig(Sig("MESG"));
		std::vector<uint8_t> bytes = a.Bytes(size);
		uint32_t what;
		std::vector<FlatField> fields;
		if (!UnflattenR5Message(bytes, what, fields))
			return false;
		// TPropShapeWrap::Restore: what's missing is Regular, both sides
		for (const FlatField& f : fields) {
			if (f.name == "Wrap Kind" && f.type == kInt32Type && f.data.size() == 4)
				wrap.kind = int32_t(Get32(f.data, 0, false));
			else if (f.name == "Wrap To" && f.type == kInt32Type && f.data.size() == 4)
				wrap.to = int32_t(Get32(f.data, 0, false));
			else if (f.name == "margins" && f.type == kRectType && f.data.size() == 16)
				for (int i = 0; i < 4; i++)
					wrap.margins[i] = BitsFloat(Get32(f.data, 4 * i, false));
		}
	} catch (const ArchiveError&) {
		return false;
	}
	return true;
}


bool
SetShapeWrap(Document& doc, size_t part, size_t layer, size_t index, const ShapeWrap& wrap)
{
	if (!SetShapeProperty(doc, part, layer, index, WrapProperty(wrap)))
		return false;
	Shape& shape = doc.EditableGraphicsPart(part)->layers[layer].shapes[index];
	if (wrap.kind != 0)
		shape.values[0] |= kShapeWraps;
	else
		shape.values[0] &= ~kShapeWraps;
	return true;
}


bool
SetFrameWrap(Document& doc, const uint32_t pointer[3], const ShapeWrap& wrap)
{
	gobe::Frame* frame = const_cast<gobe::Frame*>(FrameAt(doc, pointer));
	if (frame == nullptr)
		return false;
	uint32_t to = StyleWithProperty(doc.styles, frame->style, WrapProperty(wrap));
	SetCellStyle(doc.styles, frame->style, to);
	if (wrap.kind != 0)
		frame->shape[0] |= kShapeWraps;
	else
		frame->shape[0] &= ~kShapeWraps;
	doc.stylesChanged = true;
	doc.dirtyTails.insert(pointer[0]);
	return true;
}


bool
WrapLine(const std::vector<WrapObstacle>& obstacles, float top, float bottom, float& left,
	float& right, float minimum, float& next)
{
	next = bottom;
	bool fits = true;
	float lowest = 1e30f;		// the first of them to end
	for (const WrapObstacle& o : obstacles) {
		if (o.kind == 0 || o.rect[3] <= top || o.rect[1] >= bottom || o.rect[2] <= left
				|| o.rect[0] >= right)
			continue;
		lowest = std::min(lowest, o.rect[3]);
		if (o.kind == 3) {
			fits = false;
			continue;
		}
		float leftRoom = o.rect[0] - left, rightRoom = right - o.rect[2];
		bool toLeft = o.to == 1 || ((o.to == 0 || o.to == 3) && leftRoom >= rightRoom);
		if (toLeft)
			right = std::min(right, o.rect[0]);
		else
			left = std::max(left, o.rect[2]);
	}
	if (fits && right - left >= minimum)
		return true;
	next = std::max(lowest < 1e30f ? lowest : bottom, top + 1);
	return false;
}

} // namespace gobe
