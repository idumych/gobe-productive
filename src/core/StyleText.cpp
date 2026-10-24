#include "StyleText.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "DocArchive.h"
#include "Measure.h"

namespace gobe {

namespace {

struct TypeName {
	char type[5];
	int32_t id;
	int index;
};

// each TProp class's GetTypeName: STR# 11 (TPropertyManager's) or 1304
// (the text part's)
const TypeName kTypeNames[] = {
	{"TCol", 11, 0}, {"PCol", 11, 1}, {"FCol", 11, 2}, {"PENz", 11, 3}, {"TSiz", 11, 4},
	{"TFac", 11, 5}, {"TFon", 11, 6}, {"PTra", 11, 17}, {"FTra", 11, 18}, {"Arrw", 11, 19},
	{"HAln", 11, 24}, {"VAln", 11, 25}, {"SSNm", 11, 35}, {"TTra", 11, 36}, {" Tra", 11, 37},
	{"PAlg", 1304, 0}, {"PLef", 1304, 11}, {"PRig", 1304, 12}, {"PInd", 1304, 13},
	{"LSpa", 1304, 14}, {"SpaB", 1304, 15}, {"SpaA", 1304, 16}, {"PLab", 1304, 17},
	{"PWid", 1304, 18}, {"LTog", 1304, 19}, {"PNxt", 1304, 20}, {"PgBB", 1304, 21},
	{"PHyp", 1304, 22}, {"PChk", 1304, 23}, {"PHid", 1304, 24}, {"PExp", 1304, 25},
};


std::vector<std::string>
UnitNames(const StringLookup& strings)
{
	std::vector<std::string> names;
	for (int i = 0; i < 6; i++)
		names.push_back(strings(7, i));
	return names;
}


// TString::SetNum(float, 0)
std::string
Number(float value)
{
	return MeasureText(value, -1, 0, {});
}


// TMeasure(points, kPoints).GetText(unit, 3)
std::string
Length(float points, const StringLookup& strings, int unit)
{
	return MeasureText(points, unit, kMeasureWithUnit | kMeasureTwoPlaces, UnitNames(strings));
}


// TPenInk::Description: a solid color (a pattern whose colors are the same,
// or whose back color is B_TRANSPARENT_32_BIT) as "R:%d G:%d B:%d", else
// "Color pattern" or (a fill) "Color fill"
std::string
InkText(DocArchive& a, const StringLookup& strings)
{
	if (!a.Bool())
		return strings(13, 13);
	uint8_t fore[4], back[4];
	for (uint8_t& c : fore)
		c = a.UInt8();
	for (uint8_t& c : back)
		c = a.UInt8();
	static const uint8_t kTransparent[4] = {0x77, 0x74, 0x77, 0x00};
	bool same = true, transparent = true;
	for (int i = 0; i < 4; i++) {
		same = same && fore[i] == back[i];
		transparent = transparent && back[i] == kTransparent[i];
	}
	if (!same && !transparent)
		return strings(13, 12);
	char text[128];
	snprintf(text, sizeof text, strings(13, 11).c_str(), fore[0], fore[1], fore[2]);
	return text;
}


// TPropPenType: the pen (TPenFactory::ReadPen: its kind, size and
// TPen::ReadPenData, a name first) or none; its STR# 10 kind index (-1:
// none)
int
ReadPen(DocArchive& a, std::string& name)
{
	if (a.UInt32() == 0)
		return -1;
	static const char kKinds[4][5] = {"NORM", "PDOT", "DASH", "HASH"};
	uint32_t kind = a.UInt32();
	a.UInt32();
	for (int i = 0; i < 4; i++) {
		if (kind == Sig(kKinds[i])) {
			name = a.String();
			return i;
		}
	}
	return -1;
}


// TPropShort::GetDataStr
std::string
ShortText(DocArchive& a)
{
	return std::to_string(int(int16_t(a.UInt16())));
}

} // namespace


std::string
PropertyTypeName(const Property& property, const StringLookup& strings)
{
	// TPropParTab: its type, its fill, " Tab"
	if (property.type == Sig("PTab")) {
		DocArchive a(property.data);
		try {
			a.Float();
			int type = a.UInt16();
			int fill = a.UInt16();
			static const int kTypeName[5] = {1, 3, 2, 5, 6};
			std::string name;
			if (type >= 0 && type < 5)
				name = strings(1304, kTypeName[type]);
			if (fill == 1 || fill == 2)
				name += strings(1304, fill == 1 ? 7 : 8);
			return name + strings(1304, 9);
		} catch (const ArchiveError&) {
			return "";
		}
	}
	if (property.type == Sig("PEN ")) {
		DocArchive a(property.data);
		std::string name;
		try {
			int kind = ReadPen(a, name);
			return kind >= 0 ? strings(10, kind) : "";
		} catch (const ArchiveError&) {
			return "";
		}
	}
	for (const TypeName& name : kTypeNames)
		if (property.type == Sig(name.type))
			return strings(name.id, name.index);
	return "";
}


std::string
PropertyDataText(const Property& property, const StringLookup& strings, int measureUnit)
{
	DocArchive a(property.data);
	try {
		switch (property.type) {
			case Sig("TCol"):
			case Sig("PCol"):
			case Sig("FCol"):
				return InkText(a, strings);
			case Sig("TSiz"):
			case Sig("TFac"):
			{
				if (property.type == Sig("TSiz"))
					return ShortText(a);
				// TPropTextFace: "not " if it's off, then the first face
				// bit's name
				uint32_t face = a.UInt32();
				bool on = a.Bool();
				std::string text = on ? "" : strings(13, 6);
				int index = 7;
				for (uint32_t bit = 1; bit < 0x80 && !(face & bit); bit <<= 1)
					index++;
				return text + strings(11, index);
			}
			case Sig("TFon"):
			{
				std::string family = a.String();
				std::string face = a.String();
				return face.empty() ? family : family + " " + face;
			}
			case Sig("PENz"):
			{
				// TPropPenSize: 0 is a hairline
				float size = a.Float();
				return size == 0 ? strings(11, 34) : Number(size);
			}
			case Sig("PTra"):
			case Sig("FTra"):
			case Sig("TTra"):
			case Sig(" Tra"):
			{
				// TPropTransparent: 0 transparent, 1 opaque, else a percent
				float value = a.Float();
				if (value == 0)
					return strings(11, 15);
				if (value == 1)
					return strings(11, 16);
				char text[128];
				snprintf(text, sizeof text, strings(11, 46).c_str(),
					int(floor(double(value) * 100.0 + 0.5)));
				return text;
			}
			case Sig("Arrw"):
			{
				int value = int16_t(a.UInt16());
				return value >= 0 && value < 4 ? strings(11, 20 + value) : "";
			}
			case Sig("HAln"):
			case Sig("VAln"):
			{
				static const int kH[5] = {26, 27, 28, 29, 30};
				static const int kV[5] = {26, 31, 28, 32, 33};
				int value = int16_t(a.UInt16());
				if (value < 0 || value > 4)
					return "";
				return strings(11, property.type == Sig("HAln") ? kH[value] : kV[value]);
			}
			case Sig("PAlg"):
			{
				static const int kName[4] = {1, 3, 2, 4};
				int value = int16_t(a.UInt16());
				return value >= 0 && value < 4 ? strings(1304, kName[value]) : "";
			}
			case Sig("PLef"):
			case Sig("PRig"):
			case Sig("PInd"):
				return Length(a.Float(), strings, measureUnit);
			case Sig("PTab"):
				return Length(a.Float(), strings, measureUnit);
			case Sig("LSpa"):
			case Sig("SpaB"):
			case Sig("SpaA"):
			{
				// TParSpaceProperty: lines, or a length
				bool lines = a.Bool();
				float value = a.Float();
				if (lines)
					return Number(value) + strings(1304, 10);
				return Length(value, strings, measureUnit);
			}
			case Sig("PLab"):
				return strings(1305, int16_t(a.UInt16()));
			case Sig("PEN "):
			{
				std::string name;
				ReadPen(a, name);
				return name;
			}
			case Sig("SSNm"):
			{
				// TPropNumFormat: the format, or "General"
				std::string format = a.String();
				return format.empty() ? strings(5, 11) : format;
			}
			case Sig("PWid"):
			case Sig("LTog"):
			case Sig("PNxt"):
			case Sig("PgBB"):
			case Sig("PHyp"):
			case Sig("PChk"):
			case Sig("PHid"):
			case Sig("PExp"):
				// TPropBool: On or Off
				return strings(5, a.Bool() ? 7 : 8);
		}
	} catch (const ArchiveError&) {
	}
	return "";
}


bool
PropertyIsInk(const Property& property)
{
	return property.type == Sig("TCol") || property.type == Sig("PCol")
		|| property.type == Sig("FCol") || property.type == Sig("BCol");
}



namespace {

const uint32_t kNone = 0xffffffff;

bool
Valid(const StyleMgr& styles, uint32_t style)
{
	return style != kNone && style < styles.styles.size();
}


// TStyleManager::AddStyleNames: the named styles `style` is or comes from
// (its links 2, 0 and 1 first), each once, the last added last
bool
AddStyleNames(const StyleMgr& styles, uint32_t style, std::vector<std::string>& names,
	int depth = 0)
{
	if (!Valid(styles, style) || depth > 64)
		return false;
	const Style& s = styles.styles[style];
	bool any = AddStyleNames(styles, s.links[2], names, depth + 1);
	any = AddStyleNames(styles, s.links[0], names, depth + 1) || any;
	any = AddStyleNames(styles, s.links[1], names, depth + 1) || any;
	if (s.name.empty())
		return any;
	names.erase(std::remove(names.begin(), names.end(), s.name), names.end());
	names.push_back(s.name);
	return true;
}


// TStyleManager::AddAppliedStyleNames: the named styles applied to `style`
// or what it comes from (links[1], along links[0])
bool
AddAppliedStyleNames(const StyleMgr& styles, uint32_t style, std::vector<std::string>& names,
	int depth = 0)
{
	if (!Valid(styles, style) || depth > 64)
		return false;
	const Style& s = styles.styles[style];
	bool any = AddAppliedStyleNames(styles, s.links[0], names, depth + 1);
	if (Valid(styles, s.links[1]) && !styles.styles[s.links[1]].name.empty()) {
		const std::string& name = styles.styles[s.links[1]].name;
		names.erase(std::remove(names.begin(), names.end(), name), names.end());
		names.push_back(name);
		return true;
	}
	return any;
}


// TStyleManager::CheckIncrementalProps: whether `style` settles it (it is
// `name`, has its own properties -- `plus` -- or `name` is applied to
// it), else what it comes from
bool
CheckIncrementalProps(const StyleMgr& styles, uint32_t style, const std::string& name,
	bool& plus, int depth = 0)
{
	if (!Valid(styles, style) || depth > 64)
		return false;
	const Style& s = styles.styles[style];
	if (s.name == name)
		return true;
	if (!s.props.empty()) {
		plus = true;
		return true;
	}
	if (Valid(styles, s.links[1]) && styles.styles[s.links[1]].name == name)
		return true;
	return CheckIncrementalProps(styles, s.links[0], name, plus, depth + 1);
}


// TStyleManager::MarkSelStyle: `style` and what it comes from
void
MarkStyles(const StyleMgr& styles, uint32_t style, std::vector<bool>& marked)
{
	if (!Valid(styles, style) || marked[style])
		return;
	marked[style] = true;
	for (uint32_t link : styles.styles[style].links)
		MarkStyles(styles, link, marked);
}

} // namespace


SelectionStyles
TextSelectionStyles(const StyleMgr& styles, const WPPart& part, TextPosition from,
	TextPosition to)
{
	// TWPSelectionStyleIterator: each position's style stack, the
	// paragraph's style and the run's on it
	std::vector<std::vector<uint32_t>> stacks;
	auto runAt = [](const WPParagraph& p, size_t offset) {
		uint32_t style = kNone;
		for (const WPStyleRun& r : p.runs)
			if (r.position <= offset || style == kNone)
				style = r.style;
		return style;
	};
	if (from.paragraph < part.paragraphs.size()) {
		size_t last = std::min(to.paragraph, part.paragraphs.size() - 1);
		bool caret = from.paragraph == to.paragraph && from.offset == to.offset;
		for (size_t i = from.paragraph; i <= last; i++) {
			const WPParagraph& p = part.paragraphs[i];
			size_t start = i == from.paragraph ? from.offset : 0;
			size_t end = i == to.paragraph ? to.offset : p.text.size();
			if (caret) {
				stacks.push_back({p.parStyle, runAt(p, start)});
				break;
			}
			for (size_t r = 0; r < p.runs.size(); r++) {
				size_t runStart = p.runs[r].position;
				size_t runEnd = r + 1 < p.runs.size() ? p.runs[r + 1].position : p.text.size();
				if (runStart < end && runEnd > start)
					stacks.push_back({p.parStyle, p.runs[r].style});
			}
			if (p.runs.empty() && start < end)
				stacks.push_back({p.parStyle});
		}
		if (stacks.empty())
			stacks.push_back({part.paragraphs[from.paragraph].parStyle,
				runAt(part.paragraphs[from.paragraph], from.offset)});
	}

	SelectionStyles result;
	std::vector<std::string> common;
	bool differ = false;
	std::vector<Property> firstFormat;
	for (size_t i = 0; i < stacks.size(); i++) {
		std::vector<std::string> names, applied;
		for (uint32_t style : stacks[i]) {
			AddStyleNames(styles, style, names);
			AddAppliedStyleNames(styles, style, applied);
		}
		// TStyleManager::CurrentProps: the text's properties
		std::vector<Property> format;
		for (uint32_t style : stacks[i]) {
			if (!Valid(styles, style))
				continue;
			for (const Property& p : style == stacks[i][0] ? ParagraphFormat(styles, style)
					: CharacterFormat(styles, style))
				format.push_back(p);
		}
		if (i == 0) {
			common = names;
			firstFormat = format;
			for (const std::string& name : applied)
				result.applied.emplace_back(name, false);
			continue;
		}
		if (!differ) {
			differ = format.size() != firstFormat.size();
			for (size_t k = 0; !differ && k < format.size(); k++)
				differ = format[k].type != firstFormat[k].type
					|| format[k].data != firstFormat[k].data;
		}
		common.erase(std::remove_if(common.begin(), common.end(), [&](const std::string& n) {
				return std::find(names.begin(), names.end(), n) == names.end();
			}), common.end());
		for (const std::string& name : applied) {
			bool known = false;
			for (const auto& a : result.applied)
				known = known || a.first == name;
			if (!known)
				result.applied.emplace_back(name, true);
		}
		for (auto& a : result.applied)
			if (std::find(applied.begin(), applied.end(), a.first) == applied.end())
				a.second = true;
	}

	// kDefaultStyleNameStr: the basic style every document has
	result.active = "Default";
	if (!result.applied.empty()) {
		if (result.applied.back().second)
			return result;
		result.active = result.applied.back().first;
	} else if (!common.empty())
		result.active = common.back();

	// TStyleManager::HasIncrementalProps, GetIncrementalProps
	bool plus = differ;
	for (size_t i = 0; i < stacks.size() && !plus; i++)
		for (size_t k = stacks[i].size(); k-- > 0;)
			if (CheckIncrementalProps(styles, stacks[i][k], result.active, plus))
				break;
	if (plus)
		result.active += "+";
	std::vector<bool> marked(styles.styles.size(), false);
	for (const auto& stack : stacks)
		for (uint32_t style : stack)
			MarkStyles(styles, style, marked);
	for (size_t i = 0; i < styles.styles.size(); i++)
		if (marked[i] && styles.styles[i].name.empty())
			result.incremental.insert(result.incremental.end(), styles.styles[i].props.begin(),
				styles.styles[i].props.end());
	return result;
}

} // namespace gobe
