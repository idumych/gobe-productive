// Import and Export Styles: TStyle::Archive / Restore, the property
// classes' Archive, the 'STYL' archive file (TStyleManager::DoExportStyles,
// DoImportStyles).
#include "StyleArchive.h"

#include <algorithm>

#include "DocArchive.h"
#include "DocWriter.h"

namespace gobe {

void
ArchiveMessage::AddInt32(const std::string& name, int32_t value)
{
	Value v;
	v.integer = value;
	AddValue(name, kArchiveInt32, v);
}


void
ArchiveMessage::AddInt16(const std::string& name, int16_t value)
{
	Value v;
	v.integer = value;
	AddValue(name, kArchiveInt16, v);
}


void
ArchiveMessage::AddBool(const std::string& name, bool value)
{
	Value v;
	v.integer = value ? 1 : 0;
	AddValue(name, kArchiveBool, v);
}


void
ArchiveMessage::AddFloat(const std::string& name, float value)
{
	Value v;
	v.numbers[0] = value;
	AddValue(name, kArchiveFloat, v);
}


void
ArchiveMessage::AddString(const std::string& name, const std::string& value)
{
	Value v;
	v.bytes = value;
	AddValue(name, kArchiveString, v);
}


void
ArchiveMessage::AddData(const std::string& name, uint32_t type, const std::string& data)
{
	Value v;
	v.bytes = data;
	AddValue(name, type, v);
}


void
ArchiveMessage::AddPoint(const std::string& name, float x, float y)
{
	Value v;
	v.numbers[0] = x;
	v.numbers[1] = y;
	AddValue(name, kArchivePoint, v);
}


void
ArchiveMessage::AddRect(const std::string& name, const float rect[4])
{
	Value v;
	for (int i = 0; i < 4; i++)
		v.numbers[i] = rect[i];
	AddValue(name, kArchiveRect, v);
}


void
ArchiveMessage::AddMessage(const std::string& name, const ArchiveMessage& message)
{
	Value v;
	v.message.push_back(message);
	AddValue(name, kArchiveMessage, v);
}


void
ArchiveMessage::AddValue(const std::string& name, uint32_t type, const Value& value)
{
	for (Field& f : fields)
		if (f.name == name && f.type == type) {
			f.values.push_back(value);
			return;
		}
	fields.push_back({name, type, {value}});
}


const ArchiveMessage::Value*
ArchiveMessage::Find(const std::string& name, uint32_t type, size_t index) const
{
	for (const Field& f : fields)
		if (f.name == name && f.type == type)
			return index < f.values.size() ? &f.values[index] : nullptr;
	return nullptr;
}


size_t
ArchiveMessage::Count(const std::string& name) const
{
	for (const Field& f : fields)
		if (f.name == name)
			return f.values.size();
	return 0;
}


namespace {

const char* const kStyleDefinition = "Style Definition";
const char* const kStyleProperty = "Style Property";
const char* const kPropertyType = "Property Type";
const char* const kPropertyData = "Property Data";	// the port's: raw archive bytes
const char* const kValue = "value";

enum PropertyClass {
	kRaw, kBool, kShort, kFloat, kFace, kFont, kSpace, kNumFormat, kInk, kTab, kLabel, kPen
};


// the TProperty subclass each type is (TPropertyManager::CreateProperty)
PropertyClass
ClassOf(uint32_t type)
{
	switch (type) {
		case Sig("PWid"): case Sig("LTog"): case Sig("PNxt"): case Sig("PgBB"):
		case Sig("PHyp"): case Sig("PChk"): case Sig("PHid"): case Sig("PExp"):
			return kBool;
		case Sig("TSiz"): case Sig("PAlg"): case Sig("Arrw"): case Sig("HAln"):
		case Sig("VAln"):
			return kShort;
		case Sig("PLef"): case Sig("PRig"): case Sig("PInd"): case Sig("PENz"):
		case Sig("PTra"): case Sig("FTra"): case Sig("TTra"): case Sig(" Tra"):
			return kFloat;
		case Sig("TFac"):
			return kFace;
		case Sig("TFon"):
			return kFont;
		case Sig("SpaB"): case Sig("SpaA"): case Sig("LSpa"):
			return kSpace;
		case Sig("SSNm"):
			return kNumFormat;
		case Sig("TCol"): case Sig("PCol"): case Sig("FCol"): case Sig("BCol"):
			return kInk;
		case Sig("PTab"):
			return kTab;
		case Sig("PLab"):
			return kLabel;
		case Sig("PEN "):
			return kPen;
		default:
			return kRaw;
	}
}


std::string
Bytes(const std::vector<uint8_t>& b)
{
	return std::string(b.begin(), b.end());
}


// TColor::Archive: "TColor", B_RGB_COLOR_TYPE, its four bytes
const uint32_t kRGBColorType = 0x52474243;	// 'RGBC'
const uint32_t kPatternType = 0x5041544e;	// 'PATN'

// TPen::PenType, in the order of the pens' archive kinds
const uint32_t kPenKinds[4] = {Sig("NORM"), Sig("PDOT"), Sig("DASH"), Sig("HASH")};


// the property's class fields, as its Archive adds them; false if its
// data isn't what the class reads (then it's kept raw). A PTab property
// holding several tabs gives a message per tab.
bool
ArchiveFields(const Property& p, std::vector<ArchiveMessage>& out)
{
	DocArchive a(p.data);
	auto start = [&]() -> ArchiveMessage& {
		out.emplace_back();
		out.back().what = Sig("PROP");
		out.back().AddInt32(kPropertyType, int32_t(p.type));
		return out.back();
	};
	try {
		switch (ClassOf(p.type)) {
			case kBool:
				start().AddBool(kValue, a.Bool());
				break;
			case kShort:
				start().AddInt16(kValue, a.Int16());
				break;
			case kFloat:
				start().AddFloat(kValue, a.Float());
				break;
			case kFace: {
				ArchiveMessage& m = start();
				m.AddInt32("Text Face", a.Int32());
				m.AddBool("Text Value", a.Bool());
				break;
			}
			case kFont: {
				ArchiveMessage& m = start();
				m.AddString("font", a.String());
				m.AddString("style", a.String());
				break;
			}
			case kSpace: {
				// TParSpaceProperty: in the archive, lines first
				bool lines = a.Bool();
				ArchiveMessage& m = start();
				m.AddFloat(kValue, a.Float());
				m.AddBool("lines", lines);
				break;
			}
			case kNumFormat:
				start().AddString("format", a.String());
				break;
			case kInk: {
				// TPenInk::Archive: a solid ink's TColorPattern (high and low
				// color, the pattern); fills aren't mapped
				if (!a.Bool())
					return false;
				std::vector<uint8_t> high = a.Bytes(4), low = a.Bytes(4), pattern = a.Bytes(8);
				ArchiveMessage colors;
				colors.AddData("TColor", kRGBColorType, Bytes(high));
				colors.AddData("TColor", kRGBColorType, Bytes(low));
				colors.AddData("Pattern", kPatternType, Bytes(pattern));
				ArchiveMessage ink;
				ink.what = Sig("_INK");
				ink.AddBool("isSimple", true);
				ink.AddMessage("TColorPattern", colors);
				start().AddMessage("TInk", ink);
				break;
			}
			case kTab:
				// TPropParTab: one tab stop each
				while (a.Position() < a.Size()) {
					float position = a.Float();
					int32_t type = a.UInt16();
					int32_t fill = a.UInt16();
					int32_t align = a.Int32();
					int32_t fillChar = a.Int32();
					ArchiveMessage& m = start();
					m.AddInt32("tabType", type);
					m.AddInt32("fillType", fill);
					m.AddInt32("align", align);
					m.AddInt32("fillChar", fillChar);
					m.AddFloat(kValue, position);
				}
				if (out.empty())
					return false;
				break;
			case kLabel: {
				// TPropParLabel: its type, the child and parent styles' names
				ArchiveMessage& m = start();
				m.AddInt32("type", a.Int16());
				m.AddString("child", a.String());
				m.AddString("parent", a.String());
				break;
			}
			case kPen: {
				// TPropPenType: TPen::Archive and its subclass's (the pen's
				// kind, its data's size, TPen::ReadPenData and the rest)
				ArchiveMessage& m = start();
				if (a.UInt32() == 0)
					break;
				uint32_t kind = a.UInt32();
				int type = int(std::find(kPenKinds, kPenKinds + 4, kind) - kPenKinds);
				if (type == 4)
					throw ArchiveError("unknown pen", a.Position());
				uint32_t size = a.UInt32();
				size_t end = a.Position() + size;
				m.AddInt32("Pen Type", type);
				m.AddString("Pen Name", a.String());
				m.AddBool("Pen Print", a.Bool());
				if (kind == Sig("DASH"))
					m.AddFloat("Dash Width", a.Float());
				if (kind != Sig("NORM") && a.Position() < end)
					m.AddFloat("Gap Width", a.Float());
				if (kind == Sig("PDOT") && a.Position() < end)
					m.AddBool("FillIt", a.Bool());
				if (a.Position() != end)
					throw ArchiveError("pen size", a.Position());
				break;
			}
			case kRaw:
				return false;
		}
	} catch (const ArchiveError&) {
		out.clear();
		return false;
	}
	if (a.Position() != a.Size()) {
		out.clear();
		return false;
	}
	return true;
}


// TPropertyManager's RestoreProperty: the property's archive data from its
// class fields; false if the message lacks them
bool
RestoreProperty(const ArchiveMessage& m, Property& p)
{
	const ArchiveMessage::Value* type = m.Find(kPropertyType, kArchiveInt32);
	if (type == nullptr)
		return false;
	p.type = uint32_t(type->integer);
	if (const ArchiveMessage::Value* raw = m.Find(kPropertyData, kArchiveRaw)) {
		p.data.assign(raw->bytes.begin(), raw->bytes.end());
		return true;
	}
	DocWriter w;
	auto value = [&](const char* name, uint32_t kind) -> const ArchiveMessage::Value& {
		const ArchiveMessage::Value* v = m.Find(name, kind);
		if (v == nullptr)
			throw false;
		return *v;
	};
	try {
		switch (ClassOf(p.type)) {
			case kBool:
				w.Bool(value(kValue, kArchiveBool).integer != 0);
				break;
			case kShort:
				w.UInt16(uint16_t(value(kValue, kArchiveInt16).integer));
				break;
			case kFloat:
				w.Float(value(kValue, kArchiveFloat).numbers[0]);
				break;
			case kFace:
				w.UInt32(uint32_t(value("Text Face", kArchiveInt32).integer));
				w.Bool(value("Text Value", kArchiveBool).integer != 0);
				break;
			case kFont:
				w.String(value("font", kArchiveString).bytes);
				w.String(value("style", kArchiveString).bytes);
				break;
			case kSpace:
				w.Bool(value("lines", kArchiveBool).integer != 0);
				w.Float(value(kValue, kArchiveFloat).numbers[0]);
				break;
			case kNumFormat:
				w.String(value("format", kArchiveString).bytes);
				break;
			case kInk: {
				const ArchiveMessage::Value& ink = value("TInk", kArchiveMessage);
				if (ink.message.empty())
					return false;
				const ArchiveMessage::Value* simple = ink.message[0].Find("isSimple", kArchiveBool);
				const ArchiveMessage::Value* colors
					= ink.message[0].Find("TColorPattern", kArchiveMessage);
				if (simple == nullptr || simple->integer == 0 || colors == nullptr
						|| colors->message.empty())
					return false;
				const ArchiveMessage& c = colors->message[0];
				const ArchiveMessage::Value* high = c.Find("TColor", kRGBColorType, 0);
				const ArchiveMessage::Value* low = c.Find("TColor", kRGBColorType, 1);
				const ArchiveMessage::Value* pattern = c.Find("Pattern", kPatternType);
				if (high == nullptr || high->bytes.size() != 4 || low == nullptr
						|| low->bytes.size() != 4 || pattern == nullptr
						|| pattern->bytes.size() != 8)
					return false;
				w.Bool(true);
				for (const std::string* b : {&high->bytes, &low->bytes, &pattern->bytes})
					w.Bytes(reinterpret_cast<const uint8_t*>(b->data()), b->size());
				break;
			}
			case kTab:
				w.Float(value(kValue, kArchiveFloat).numbers[0]);
				w.UInt16(uint16_t(value("tabType", kArchiveInt32).integer));
				w.UInt16(uint16_t(value("fillType", kArchiveInt32).integer));
				w.UInt32(uint32_t(value("align", kArchiveInt32).integer));
				w.UInt32(uint32_t(value("fillChar", kArchiveInt32).integer));
				break;
			case kLabel:
				w.UInt16(uint16_t(value("type", kArchiveInt32).integer));
				w.String(value("child", kArchiveString).bytes);
				w.String(value("parent", kArchiveString).bytes);
				break;
			case kPen: {
				const ArchiveMessage::Value* type = m.Find("Pen Type", kArchiveInt32);
				if (type == nullptr) {
					w.UInt32(0);
					break;
				}
				if (type->integer < 0 || type->integer > 3)
					return false;
				uint32_t kind = kPenKinds[type->integer];
				DocWriter pen;
				pen.String(value("Pen Name", kArchiveString).bytes);
				pen.Bool(value("Pen Print", kArchiveBool).integer != 0);
				if (kind == Sig("DASH")) {
					pen.Float(value("Dash Width", kArchiveFloat).numbers[0]);
					pen.Float(value("Gap Width", kArchiveFloat).numbers[0]);
				} else if (kind != Sig("NORM")) {
					pen.Float(value("Gap Width", kArchiveFloat).numbers[0]);
					if (kind == Sig("PDOT"))
						pen.Bool(value("FillIt", kArchiveBool).integer != 0);
				}
				w.UInt32(0xffffffff);
				w.UInt32(kind);
				w.UInt32(uint32_t(pen.Data().size()));
				w.Bytes(pen.Data());
				break;
			}
			case kRaw:
				return false;
		}
	} catch (bool) {
		return false;
	}
	p.data = w.Data();
	return true;
}


std::string
StringField(const ArchiveMessage& m, const char* name)
{
	const ArchiveMessage::Value* v = m.Find(name, kArchiveString);
	return v != nullptr ? v->bytes : std::string();
}

} // namespace


ArchiveMessage
ArchiveStyles(const std::vector<StyleDefinition>& styles)
{
	ArchiveMessage all;
	all.what = Sig("sMB4");
	for (const StyleDefinition& s : styles) {
		ArchiveMessage m;
		m.what = Sig("STYL");
		m.AddString("Style Name", s.name);
		m.AddInt32("Style Type", int32_t(s.type));
		m.AddInt16("Style Level", int16_t(s.level));
		m.AddInt32("Style Flags", int32_t(s.flags));
		m.AddInt32("style_index", 0);
		m.AddString("Style Base", s.base);
		m.AddString("Style Parent", s.parent);
		m.AddString("Style Owner", s.owner);
		for (const Property& p : s.properties) {
			std::vector<ArchiveMessage> fields;
			if (!ArchiveFields(p, fields)) {
				ArchiveMessage raw;
				raw.what = Sig("PROP");
				raw.AddInt32(kPropertyType, int32_t(p.type));
				raw.AddData(kPropertyData, kArchiveRaw, Bytes(p.data));
				fields = {raw};
			}
			for (const ArchiveMessage& f : fields)
				m.AddMessage(kStyleProperty, f);
		}
		all.AddMessage(kStyleDefinition, m);
	}
	return all;
}


std::vector<StyleDefinition>
RestoreStyles(const ArchiveMessage& message)
{
	std::vector<StyleDefinition> styles;
	for (size_t i = 0; i < message.Count(kStyleDefinition); i++) {
		const ArchiveMessage::Value* v = message.Find(kStyleDefinition, kArchiveMessage, i);
		if (v == nullptr || v->message.empty())
			continue;
		const ArchiveMessage& m = v->message[0];
		const ArchiveMessage::Value* name = m.Find("Style Name", kArchiveString);
		const ArchiveMessage::Value* type = m.Find("Style Type", kArchiveInt32);
		const ArchiveMessage::Value* flags = m.Find("Style Flags", kArchiveInt32);
		if (name == nullptr || type == nullptr || flags == nullptr)
			continue;
		StyleDefinition s;
		s.name = name->bytes;
		s.type = uint32_t(type->integer);
		s.flags = uint32_t(flags->integer);
		if (const ArchiveMessage::Value* level = m.Find("Style Level", kArchiveInt16))
			s.level = uint16_t(level->integer);
		s.base = StringField(m, "Style Base");
		s.parent = StringField(m, "Style Parent");
		s.owner = StringField(m, "Style Owner");
		for (size_t k = 0; k < m.Count(kStyleProperty); k++) {
			const ArchiveMessage::Value* p = m.Find(kStyleProperty, kArchiveMessage, k);
			Property property;
			if (p != nullptr && !p->message.empty() && RestoreProperty(p->message[0], property))
				s.properties.push_back(property);
		}
		styles.push_back(s);
	}
	return styles;
}


std::vector<uint8_t>
StylesFileBytes(const std::vector<uint8_t>& header, const std::vector<uint8_t>& flattened)
{
	DocWriter w;
	std::vector<uint8_t> head(header.begin(), header.begin() + std::min<size_t>(98, header.size()));
	head.resize(98, 0);
	// TArchiveHeader: the identity, the archive's type 'STYL' (a
	// document's is 'DOC!')
	static const char kIdentity[] = "GOBE SCOTT/01/02";
	std::copy(kIdentity, kIdentity + 16, head.begin());
	head[16] = 'S';
	head[17] = 'T';
	head[18] = 'Y';
	head[19] = 'L';
	w.Bytes(head);
	w.UInt32(1);
	w.UInt32(uint32_t(flattened.size()));
	w.Sig(Sig("MESG"));
	w.Bytes(flattened);
	w.Sig(Sig("MesG"));
	w.Sig(Sig("STYL"));
	return w.Data();
}


bool
ReadStylesFile(const std::vector<uint8_t>& file, std::vector<uint8_t>& flattened, bool& newer,
	std::string& error)
{
	newer = false;
	DocArchive a(file);
	try {
		std::vector<uint8_t> id = a.Bytes(16);
		if (std::string(id.begin(), id.end()) != "GOBE SCOTT/01/02" || a.UInt32() != Sig("STYL")) {
			error = "not a Gobe Productive styles file";
			return false;
		}
		a.Seek(98);
		if (a.Int32() > 1) {
			newer = true;
			return false;
		}
		flattened = a.Message();
		a.CheckSig(Sig("STYL"));
	} catch (const ArchiveError& e) {
		error = e.what();
		return false;
	}
	return true;
}

} // namespace gobe
