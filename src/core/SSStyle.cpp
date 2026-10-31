#include "SSStyle.h"

#include <cmath>
#include "DocWriter.h"
#include <cstdio>
#include <cstring>
#include <ctime>

namespace gobe {

namespace {

void
ApplyCellProperty(const Property& p, CellStyle& style)
{
	if (ApplyProperty(p, style.text))
		return;
	DocArchive a(p.data);
	a.SetVersions(2, 0, 0x1e);
	try {
		switch (p.type) {
			case Sig("FCol"):
			case Sig("FTra"):
			case Sig("PCol"):
			case Sig("PTra"): {
				ShapeStyle shape;
				shape.fill = style.fill;
				shape.fillOpacity = style.fillOpacity;
				shape.pen = style.pen;
				shape.penOpacity = style.penOpacity;
				ApplyShapeProperty(p, shape);
				style.fill = shape.fill;
				style.fillOpacity = shape.fillOpacity;
				style.pen = shape.pen;
				style.penOpacity = shape.penOpacity;
				break;
			}
			case Sig("HAln"):
				style.hAlign = a.Int16();
				break;
			case Sig("VAln"):
				style.vAlign = a.Int16();
				break;
			case Sig("SSNm"):	// TPropNumberFormat: a TString
				style.format = a.String();
				break;
			case Sig("ClWp"):
				style.wrap = a.Bool();
				break;
			case Sig("BCol"): {	// TPropBorderColor: an ink and the side
				ShapeStyle shape;
				ApplyShapeProperty(p, shape);
				if (p.data.size() >= 4) {
					size_t n = p.data.size();
					uint32_t side = uint32_t(p.data[n - 4]) << 24 | p.data[n - 3] << 16
						| p.data[n - 2] << 8 | p.data[n - 1];
					if (side < 4) {
						style.borders[side].ink = shape.background;
						style.borders[side].shown = true;
					}
				}
				break;
			}
			case Sig("BTra"): {	// TPropBorderTransparency: opacity, side
				float opacity = a.Float();
				uint32_t side = a.UInt32();
				if (side < 4 && opacity <= 0)
					style.borders[side].shown = false;
				break;
			}
			case Sig("BSiz"): {
				float size = a.Float();
				uint32_t side = a.UInt32();
				if (side < 4)
					style.borders[side].size = size;
				break;
			}
		}
	} catch (const ArchiveError&) {
	}
}


void
ApplyStyle(const StyleMgr& styles, uint32_t index, CellStyle& style)
{
	std::vector<const Style*> chain;
	for (int depth = 0; index < styles.styles.size() && depth < 32; depth++) {
		const Style& s = styles.styles[index];
		chain.push_back(&s);
		index = s.links[0];
	}
	for (auto it = chain.rbegin(); it != chain.rend(); ++it)
		for (uint32_t p : (*it)->props)
			if (p < styles.properties.size())
				ApplyCellProperty(styles.properties[p], style);
}


std::vector<std::string>
SplitSections(const std::string& format)
{
	std::vector<std::string> sections(1);
	bool quoted = false;
	for (char c : format) {
		if (c == '"')
			quoted = !quoted;
		if (c == ';' && !quoted)
			sections.emplace_back();
		else
			sections.back() += c;
	}
	return sections;
}


bool
IsDateFormat(const std::string& f)
{
	bool quoted = false, bracket = false;
	for (char c : f) {
		if (c == '"')
			quoted = !quoted;
		else if (c == '[' && !quoted)
			bracket = true;		// a color: [Red]
		else if (c == ']' && !quoted)
			bracket = false;
		else if (!quoted && !bracket && strchr("mdyhsMDYHS", c))
			return true;
	}
	return false;
}


// m, mm, mmm, mmmm, d, dd, ddd, dddd, yy, yyyy, h, hh, mm (after h), ss,
// AM/PM
std::string
FormatDate(int64_t seconds, const std::string& f)
{
	time_t t = time_t(seconds);
	struct tm tm;
	gmtime_r(&t, &tm);
	static const char* kMonths[] = {"January", "February", "March", "April", "May",
		"June", "July", "August", "September", "October", "November", "December"};
	static const char* kDays[] = {"Sunday", "Monday", "Tuesday", "Wednesday",
		"Thursday", "Friday", "Saturday"};
	bool twelveHour = f.find("AM") != std::string::npos || f.find("PM") != std::string::npos
		|| f.find("am") != std::string::npos || f.find("pm") != std::string::npos;
	std::string out;
	bool lastWasHour = false;
	for (size_t i = 0; i < f.size();) {
		char c = f[i];
		size_t n = 1;
		while (i + n < f.size() && tolower(f[i + n]) == tolower(c))
			n++;
		char lc = tolower(c);
		char buf[32];
		if (c == '"') {
			size_t end = f.find('"', i + 1);
			if (end == std::string::npos)
				end = f.size();
			out += f.substr(i + 1, end - i - 1);
			i = end + 1;
			continue;
		}
		if ((f.compare(i, 2, "AM") == 0 || f.compare(i, 2, "PM") == 0
				|| f.compare(i, 2, "am") == 0 || f.compare(i, 2, "pm") == 0)) {
			bool lower = islower(f[i]);
			out += tm.tm_hour < 12 ? (lower ? "am" : "AM") : (lower ? "pm" : "PM");
			i += f.compare(i, 5, "AM/PM") == 0 || f.compare(i, 5, "am/pm") == 0 ? 5 : 2;
			continue;
		}
		if (lc == 'm' && lastWasHour && n <= 2) {
			snprintf(buf, sizeof buf, n == 2 ? "%02d" : "%d", tm.tm_min);
			out += buf;
		} else if (lc == 'm') {
			if (n >= 4)
				out += kMonths[tm.tm_mon];
			else if (n == 3)
				out += std::string(kMonths[tm.tm_mon], 3);
			else {
				snprintf(buf, sizeof buf, n == 2 ? "%02d" : "%d", tm.tm_mon + 1);
				out += buf;
			}
		} else if (lc == 'd') {
			if (n >= 4)
				out += kDays[tm.tm_wday];
			else if (n == 3)
				out += std::string(kDays[tm.tm_wday], 3);
			else {
				snprintf(buf, sizeof buf, n == 2 ? "%02d" : "%d", tm.tm_mday);
				out += buf;
			}
		} else if (lc == 'y') {
			snprintf(buf, sizeof buf, n >= 3 ? "%04d" : "%02d",
				n >= 3 ? tm.tm_year + 1900 : tm.tm_year % 100);
			out += buf;
		} else if (lc == 'h') {
			int h = tm.tm_hour;
			if (twelveHour) {
				h %= 12;
				if (h == 0)
					h = 12;
			}
			snprintf(buf, sizeof buf, n == 2 ? "%02d" : "%d", h);
			out += buf;
		} else if (lc == 's') {
			snprintf(buf, sizeof buf, "%02d", tm.tm_sec);
			out += buf;
		} else
			out.append(n, c);
		if (lc != ':' && lc != ' ')
			lastWasHour = lc == 'h';
		i += n;
	}
	return out;
}


// A number section: literal text around one numeric pattern of #, 0, ',',
// '.'; '%' multiplies by 100
std::string
FormatNumber(double n, const std::string& f)
{
	size_t first = f.find_first_of("#0.,");
	if (first == std::string::npos)
		return f;
	size_t last = f.find_last_of("#0.,");
	std::string prefix = f.substr(0, first), suffix = f.substr(last + 1);
	std::string pattern = f.substr(first, last - first + 1);
	if (f.find('%') != std::string::npos)
		n *= 100;
	size_t dot = pattern.find('.');
	std::string intPart = pattern.substr(0, dot);
	std::string fracPart = dot == std::string::npos ? "" : pattern.substr(dot + 1);
	int decimals = 0, optionalDecimals = 0;
	for (char c : fracPart) {
		if (c == '0')
			decimals++;
		else if (c == '#')
			optionalDecimals++;
	}
	bool thousands = intPart.find(',') != std::string::npos;
	int minInt = 0;
	for (char c : intPart)
		if (c == '0')
			minInt++;
	char buf[64];
	snprintf(buf, sizeof buf, "%.*f", decimals + optionalDecimals, fabs(n));
	std::string digits = buf;
	std::string ip = digits.substr(0, digits.find('.'));
	std::string fp = digits.find('.') == std::string::npos ? "" : digits.substr(digits.find('.') + 1);
	while (fp.size() > size_t(decimals) && fp.back() == '0')
		fp.pop_back();
	if (ip == "0" && minInt == 0)
		ip.clear();
	while (ip.size() < size_t(minInt))
		ip = "0" + ip;
	if (thousands && ip.size() > 3) {
		std::string grouped;
		for (size_t i = 0; i < ip.size(); i++) {
			if (i && (ip.size() - i) % 3 == 0)
				grouped += ',';
			grouped += ip[i];
		}
		ip = grouped;
	}
	std::string body = ip;
	if (dot != std::string::npos && (!fp.empty() || decimals > 0 || optionalDecimals == 0))
		body += "." + fp;
	if (body.empty() || body == ".")
		body = "0";
	return prefix + body + suffix;
}


std::string
StripColor(std::string section, bool* red)
{
	size_t open = section.find('[');
	if (open != std::string::npos) {
		size_t close = section.find(']', open);
		if (close != std::string::npos) {
			std::string color = section.substr(open + 1, close - open - 1);
			if (red && (color == "Red" || color == "red" || color == "RED"))
				*red = true;
			section.erase(open, close - open + 1);
		}
	}
	return section;
}

} // namespace


CellStyle
ResolveCellStyle(const StyleMgr& styles, const std::vector<uint32_t>& stack)
{
	CellStyle style;
	for (uint32_t index : stack)
		ApplyStyle(styles, index, style);
	return style;
}


bool
IsDateNumberFormat(const std::string& format)
{
	return IsDateFormat(format);
}


std::string
FormatCellValue(const SSValue& value, const std::string& format, bool* red)
{
	if (red)
		*red = false;
	if (value.type == SSValue::kError)
		return ErrorText(value.error);
	if (value.type == SSValue::kNone)
		return "";
	if (value.type == SSValue::kString || value.type == SSValue::kBool) {
		std::string s;
		value.GetString(s);
		return s;
	}
	bool general = format.empty() || format == "General";
	if (value.type == SSValue::kTime) {
		if (general || !IsDateFormat(format))
			return FormatDate(value.time, "m/d/yy h:mm");
		return FormatDate(value.time, StripColor(SplitSections(format)[0], red));
	}
	// a number
	if (general) {
		std::string s;
		value.GetString(s);
		return s;
	}
	if (IsDateFormat(format))
		return FormatDate(int64_t(llround(value.number * 86400.0)),
			StripColor(SplitSections(format)[0], red));
	std::vector<std::string> sections = SplitSections(format);
	double n = value.number;
	if (n < 0 && sections.size() >= 2)
		return FormatNumber(-n, StripColor(sections[1], red));
	if (n == 0 && sections.size() >= 3)
		return FormatNumber(n, StripColor(sections[2], red));
	std::string s = FormatNumber(n, StripColor(sections[0], red));
	if (n < 0)
		s = "-" + s;
	return s;
}


int
CellHAlign(const CellStyle& style, const SSValue& value)
{
	if (style.hAlign != 0)
		return style.hAlign;
	switch (value.type) {
		case SSValue::kNumber:
		case SSValue::kTime:
			return 3;
		case SSValue::kBool:
		case SSValue::kError:
			return 2;
		default:
			return 1;
	}
}

// --- cell property encoders (inverse of ApplyCellProperty) ---

Property
NumberFormatProperty(const std::string& format)
{
	DocWriter w;
	w.String(format);
	Property p;
	p.type = Sig("SSNm");
	p.data = w.Data();
	return p;
}


Property
CellHAlignProperty(int align)
{
	DocWriter w;
	w.UInt16(uint16_t(align));
	Property p;
	p.type = Sig("HAln");
	p.data = w.Data();
	return p;
}


Property
CellVAlignProperty(int align)
{
	DocWriter w;
	w.UInt16(uint16_t(align));
	Property p;
	p.type = Sig("VAln");
	p.data = w.Data();
	return p;
}


Property
CellWrapProperty(bool wrap)
{
	DocWriter w;
	w.Bool(wrap);
	Property p;
	p.type = Sig("ClWp");
	p.data = w.Data();
	return p;
}


// FCol: solid ink — 1 byte bool(true), 4 bytes RGBA, 4 bytes 'wtw\0',
// 8 bytes 0xff.
static const uint8_t kFColTail[12] = {
	0x77, 0x74, 0x77, 0x00,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
};

Property
CellFillProperty(Color color)
{
	DocWriter w;
	w.Bool(true);		// solid ink
	w.UInt8(color.r);
	w.UInt8(color.g);
	w.UInt8(color.b);
	w.UInt8(color.a);
	w.Bytes(kFColTail, sizeof(kFColTail));
	Property p;
	p.type = Sig("FCol");
	p.data = w.Data();
	return p;
}


Property
FillTransparencyProperty(float opacity)
{
	DocWriter w;
	w.Float(opacity);
	Property p;
	p.type = Sig("FTra");
	p.data = w.Data();
	return p;
}


Property
BorderColorProperty(int side, Color color)
{
	Property p = CellFillProperty(color);
	DocWriter w;
	w.Bytes(p.data.data(), p.data.size());
	w.UInt32(uint32_t(side));
	p.type = Sig("BCol");
	p.data = w.Data();
	return p;
}


Property
BorderTransparencyProperty(int side, float opacity)
{
	DocWriter w;
	w.Float(opacity);
	w.UInt32(uint32_t(side));
	Property p;
	p.type = Sig("BTra");
	p.data = w.Data();
	return p;
}


int
BorderSide(const Property& p)
{
	if ((p.type != Sig("BCol") && p.type != Sig("BTra") && p.type != Sig("BSiz")
			&& p.type != Sig("BPen")) || p.data.size() < 4)
		return -1;
	size_t n = p.data.size();
	return int(uint32_t(p.data[n - 4]) << 24 | uint32_t(p.data[n - 3]) << 16
		| uint32_t(p.data[n - 2]) << 8 | p.data[n - 1]);
}

} // namespace gobe
