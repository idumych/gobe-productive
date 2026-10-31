// Plain text in and out of word processing documents (Gobe Text Translator).
#include "TextImport.h"

#include "Document.h"
#include "Editing.h"
#include "SSEval.h"
#include "SSStyle.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <map>

namespace gobe {

// #pragma mark - fonts

int
FontCatalog::Find(const std::string& family, const std::string& style) const
{
	for (size_t i = 0; i < fonts.size(); i++)
		if (fonts[i].family == family && fonts[i].style == style)
			return int(i);
	return -1;
}


int
FontCatalog::PlainIndex() const
{
	int index = Find(plainFamily, plainStyle);
	return index < 0 ? 0 : index;
}


std::string
FontCatalog::StyleFor(const std::string& family, bool bold, bool italic) const
{
	for (const FontInfo& f : fonts) {
		if (f.family != family)
			continue;
		if ((f.style.find("Bold") != std::string::npos) == bold
				&& (f.style.find("Italic") != std::string::npos) == italic)
			return f.style;
	}
	return "";
}


// #pragma mark - conversions

namespace {

TextConverter sConverter = nullptr;

// B_MAC_ROMAN_CONVERSION, 0x80-0xff (python3: bytes([b]).decode('mac_roman'))
const uint16_t kMacRoman[128] = {
	0x00c4, 0x00c5, 0x00c7, 0x00c9, 0x00d1, 0x00d6, 0x00dc, 0x00e1,
	0x00e0, 0x00e2, 0x00e4, 0x00e3, 0x00e5, 0x00e7, 0x00e9, 0x00e8,
	0x00ea, 0x00eb, 0x00ed, 0x00ec, 0x00ee, 0x00ef, 0x00f1, 0x00f3,
	0x00f2, 0x00f4, 0x00f6, 0x00f5, 0x00fa, 0x00f9, 0x00fb, 0x00fc,
	0x2020, 0x00b0, 0x00a2, 0x00a3, 0x00a7, 0x2022, 0x00b6, 0x00df,
	0x00ae, 0x00a9, 0x2122, 0x00b4, 0x00a8, 0x2260, 0x00c6, 0x00d8,
	0x221e, 0x00b1, 0x2264, 0x2265, 0x00a5, 0x00b5, 0x2202, 0x2211,
	0x220f, 0x03c0, 0x222b, 0x00aa, 0x00ba, 0x03a9, 0x00e6, 0x00f8,
	0x00bf, 0x00a1, 0x00ac, 0x221a, 0x0192, 0x2248, 0x2206, 0x00ab,
	0x00bb, 0x2026, 0x00a0, 0x00c0, 0x00c3, 0x00d5, 0x0152, 0x0153,
	0x2013, 0x2014, 0x201c, 0x201d, 0x2018, 0x2019, 0x00f7, 0x25ca,
	0x00ff, 0x0178, 0x2044, 0x20ac, 0x2039, 0x203a, 0xfb01, 0xfb02,
	0x2021, 0x00b7, 0x201a, 0x201e, 0x2030, 0x00c2, 0x00ca, 0x00c1,
	0x00cb, 0x00c8, 0x00cd, 0x00ce, 0x00cf, 0x00cc, 0x00d3, 0x00d4,
	0xf8ff, 0x00d2, 0x00da, 0x00db, 0x00d9, 0x0131, 0x02c6, 0x02dc,
	0x00af, 0x02d8, 0x02d9, 0x02da, 0x00b8, 0x02dd, 0x02db, 0x02c7,
};


void
AppendUTF8(std::string& out, uint32_t c)
{
	if (c < 0x80) {
		out += char(c);
	} else if (c < 0x800) {
		out += char(0xc0 | (c >> 6));
		out += char(0x80 | (c & 0x3f));
	} else if (c < 0x10000) {
		out += char(0xe0 | (c >> 12));
		out += char(0x80 | ((c >> 6) & 0x3f));
		out += char(0x80 | (c & 0x3f));
	} else {
		out += char(0xf0 | (c >> 18));
		out += char(0x80 | ((c >> 12) & 0x3f));
		out += char(0x80 | ((c >> 6) & 0x3f));
		out += char(0x80 | (c & 0x3f));
	}
}


// the character at `i` (advanced past it); invalid bytes are returned as is
uint32_t
NextUTF8(const std::string& s, size_t& i)
{
	uint8_t b = uint8_t(s[i++]);
	int more = b >= 0xf0 ? 3 : b >= 0xe0 ? 2 : b >= 0xc0 ? 1 : 0;
	uint32_t c = more == 3 ? (b & 0x07) : more == 2 ? (b & 0x0f) : more == 1 ? (b & 0x1f) : b;
	for (int k = 0; k < more && i < s.size() && (uint8_t(s[i]) & 0xc0) == 0x80; k++)
		c = (c << 6) | (uint8_t(s[i++]) & 0x3f);
	return c;
}

} // namespace


void
SetTextConverter(TextConverter converter)
{
	sConverter = converter;
}


int
DetectConversion(const std::string& data)
{
	const uint8_t* p = reinterpret_cast<const uint8_t*>(data.data());
	size_t n = data.size();
	int result = kConversionNone;
	if (n >= 2 && !(p[0] == 0xff && p[1] == 0xfe)) {
		if (p[0] == 0xfe && p[1] == 0xff)
			return kConversionUnicode;
		if (n >= 3 && p[0] == 0xef && p[1] == 0xbb && p[2] == 0xbf)
			return kConversionNone;
	}
	for (size_t i = 0; i < n; i++) {
		uint8_t b = p[i];
		if (b >= 0x80) {
			// 0xe5000000 >> ((b >> 3) & 0x1e) & 3: continuation bytes
			// after a lead byte
			unsigned more = (0xe5000000u >> ((b >> 3) & 0x1e)) & 3;
			if (more >= 1 && more <= 3) {
				if (n - i < more + 1)
					return kConversionISO1;
				bool bad = false;
				for (; more != 0; more--) {
					i++;
					if ((p[i] & 0xc0) != 0x80) {
						bad = true;
						break;
					}
				}
				if (bad)
					result = kConversionISO1;
			} else
				result = kConversionISO1;
		} else if (b == '\r') {
			if (i + 1 < n && p[i + 1] == '\n')
				return kConversionISO1;
			return kConversionMacRoman;
		}
	}
	return result;
}


std::string
ConvertToUTF8(int conversion, const std::string& data, char substitute)
{
	std::string out;
	switch (conversion) {
		case kConversionNone:
			return data;
		case kConversionISO1:
			for (unsigned char c : data)
				AppendUTF8(out, c);
			return out;
		case kConversionMacRoman:
			for (unsigned char c : data)
				AppendUTF8(out, c < 0x80 ? c : kMacRoman[c - 0x80]);
			return out;
		case kConversionUnicode:
			for (size_t i = 0; i + 1 < data.size(); i += 2)
				AppendUTF8(out, (uint8_t(data[i]) << 8) | uint8_t(data[i + 1]));
			return out;
	}
	if (sConverter != nullptr && sConverter(conversion, false, data, out, substitute))
		return out;
	for (unsigned char c : data)
		out += c < 0x80 ? char(c) : substitute;
	return out;
}


std::string
ConvertFromUTF8(int conversion, const std::string& utf8, char substitute)
{
	std::string out;
	if (conversion == kConversionNone)
		return utf8;
	if (conversion != kConversionISO1 && conversion != kConversionMacRoman
			&& conversion != kConversionUnicode) {
		if (sConverter != nullptr && sConverter(conversion, true, utf8, out, substitute))
			return out;
	}
	for (size_t i = 0; i < utf8.size();) {
		uint32_t c = NextUTF8(utf8, i);
		if (conversion == kConversionUnicode) {
			if (c > 0xffff)
				c = uint8_t(substitute);
			out += char(c >> 8);
			out += char(c & 0xff);
		} else if (c < 0x80) {
			out += char(c);
		} else if (conversion == kConversionISO1) {
			out += c < 0x100 ? char(c) : substitute;
		} else if (conversion == kConversionMacRoman) {
			int found = -1;
			for (int k = 0; k < 128; k++)
				if (kMacRoman[k] == c)
					found = k;
			out += found >= 0 ? char(0x80 + found) : substitute;
		} else
			out += substitute;
	}
	return out;
}


bool
LooksLikeText(const std::string& head)
{
	int conversion = DetectConversion(head);
	if (conversion == kConversionNone || conversion == kConversionUnicode)
		return true;
	size_t control = 0, other = 0;
	for (unsigned char c : head) {
		if (c < 9 || c == 0x0b || (c >= 0x0e && c < 0x20))
			control++;
		else
			other++;
	}
	return other > control * 10;
}


char
TableDelimiter(const std::string& head)
{
	size_t tabs = 0, commas = 0, semicolons = 0;
	for (char c : head) {
		tabs += c == '\t';
		commas += c == ',';
		semicolons += c == ';';
	}
	if (commas < tabs && semicolons < tabs)
		return '\t';
	return semicolons < commas ? ',' : ';';
}


float
TableLikeness(const std::string& head)
{
	size_t tabs = 0, lf = 0, cr = 0, commas = 0, semicolons = 0;
	for (char c : head) {
		switch (c) {
			case '\t': tabs++; break;
			case '\n': lf++; break;
			case '\r': cr++; break;
			case ',': commas++; break;
			case ';': semicolons++; break;
		}
	}
	size_t lines = std::max(lf, cr);
	if (lines == 0)
		return 0.01f;
	char delimiter = TableDelimiter(head);
	size_t count = delimiter == '\t' ? tabs : delimiter == ',' ? commas : semicolons;
	size_t perLine = count / lines;
	if (perLine <= 1)
		return 0.01f;
	if (delimiter != '\t' && head.size() / count > 30)
		return 0.3f;
	switch (perLine) {
		case 2: return 0.4f;
		case 3: return 0.5f;
		case 4: return 0.6f;
		case 5: return 0.75f;
	}
	return 0.8f;
}


// #pragma mark - shared with RTF

void
SetPartText(WPPart& part, const std::string& text)
{
	if (part.paragraphs.empty())
		return;
	WPParagraph model = part.paragraphs[0];
	model.text.clear();
	model.misc.clear();
	model.runs.resize(model.runs.empty() ? 0 : 1);
	if (!model.runs.empty())
		model.runs[0].position = 0;
	part.paragraphs.clear();
	WPParagraph p = model;
	for (char c : text) {
		p.text += c;
		// TWPPart::InsertStyledText: these end a paragraph and stay at its end
		if (c == '\n' || c == 0x02 || c == 0x0c || c == 0x05) {
			part.paragraphs.push_back(std::move(p));
			p = model;
		}
	}
	part.paragraphs.push_back(std::move(p));
	// as if typed in at the start: the frames' text ranges follow
	if (!text.empty())
		part.edits.emplace_back(0, int64_t(text.size()));
}


TextPosition
PositionAt(const WPPart& part, size_t offset)
{
	for (size_t i = 0; i < part.paragraphs.size(); i++) {
		size_t length = part.paragraphs[i].text.size();
		if (offset < length || i + 1 == part.paragraphs.size())
			return {i, std::min(offset, length)};
		offset -= length;
	}
	return {};
}


// #pragma mark - import

namespace {

// TTextToBlockStream::FilterText: CR LF and lone CR become LF, tab, LF and FF
// stay, other bytes below 0x21 become spaces; run offsets follow removals
void
FilterText(std::string& text, std::vector<TextRun>* runs)
{
	std::string out;
	out.reserve(text.size());
	for (size_t i = 0; i < text.size(); i++) {
		uint8_t c = uint8_t(text[i]);
		if (c == '\r') {
			if (i + 1 < text.size() && text[i + 1] == '\n') {
				if (runs != nullptr)
					for (TextRun& r : *runs)
						if (size_t(r.offset) > out.size())
							r.offset--;
				i++;
			}
			out += '\n';
		} else if (c == '\n' || c == '\t' || c == '\f') {
			out += char(c);
		} else
			out += c < 0x21 ? ' ' : char(c);
	}
	text = std::move(out);
}

} // namespace


bool
ImportTable(const std::string& path, Document& doc, std::string& error, char delimiter)
{
	std::ifstream file(path, std::ios::binary);
	if (!file) {
		error = "cannot open " + path;
		return false;
	}
	std::string text((std::istreambuf_iterator<char>(file)),
		std::istreambuf_iterator<char>());
	if (delimiter == 0)
		delimiter = TableDelimiter(text.substr(0, 0x8000));
	int conversion = DetectConversion(text);
	if (conversion >= 0)
		text = ConvertToUTF8(conversion, text, '?');
	FilterText(text, nullptr);

	if (!CreateBlankSpreadsheet(doc, error))
		return false;
	size_t part = size_t(doc.MainPart());
	SSPart* ss = doc.EditableSpreadsheetPart(part);
	if (ss == nullptr) {
		error = "no spreadsheet";
		return false;
	}
	// GetNextCell: the delimiters and LFs before a cell move to its column
	// and row, then the cell runs to the next delimiter or LF
	uint32_t row = 0, column = 0;
	size_t at = 0;
	std::string ignored;
	while (at < text.size()) {
		while (at < text.size() && (text[at] == delimiter || text[at] == '\n')) {
			if (text[at] == delimiter)
				column++;
			else {
				row++;
				column = 0;
			}
			at++;
		}
		size_t end = at;
		while (end < text.size() && text[end] != delimiter && text[end] != '\n')
			end++;
		if (end > at && row < 0xffff && column < 0xffff) {
			std::string cell = text.substr(at, end - at);
			uint16_t r = uint16_t(row), c = uint16_t(column);
			if (!SetCellInput(*ss, r, c, cell, nullptr, ignored)) {
				// a formula that doesn't parse: the text as it is
				SetCellInput(*ss, r, c, "x", nullptr, ignored);
				for (SSCell& sc : ss->cells)
					if (sc.id.a == r && sc.id.b == c)
						sc.text = cell;
			}
			if (r > ss->used.to.a)
				ss->used.to.a = r;
			if (c > ss->used.to.b)
				ss->used.to.b = c;
		}
		at = end;
	}
	UpdateDependencies(doc, part);
	return true;
}


bool
ExportTable(const Document& doc, size_t part, const std::string& path, char delimiter,
	std::string& error)
{
	const SSPart* ss = doc.SpreadsheetPart(part);
	if (ss == nullptr) {
		error = "not a spreadsheet";
		return false;
	}
	SSBook book(doc);
	SSEvaluator eval(*ss, &book, int(part));
	// the cells with something to show, by row then column
	std::map<std::pair<uint16_t, uint16_t>, std::string> shown;
	for (const SSCell& cell : ss->cells) {
		if (cell.id.b == 0xffff)
			continue;
		SSValue value = eval.Value(cell.id.a, cell.id.b);
		if (ss->options[2] && cell.kind == Sig("Fmla"))
			value = SSValue::String(CellInput(*ss, cell.id.a, cell.id.b));
		if (value.type == SSValue::kNone)
			continue;
		// the format as the view finds it: the part's style, the column's,
		// the row's, the cell's
		std::vector<uint32_t> stack = {ss->style};
		if (const SSHeader* h = ss->Column(cell.id.b))
			stack.push_back(h->cell.style);
		if (const SSHeader* h = ss->Row(cell.id.a))
			stack.push_back(h->cell.style);
		stack.push_back(cell.style);
		CellStyle style = ResolveCellStyle(doc.styles, stack);
		shown[{cell.id.a, cell.id.b}] = FormatCellValue(value, style.format);
	}
	std::string text;
	uint32_t row = 0, column = 0;
	for (const auto& entry : shown) {
		for (; row < entry.first.first; row++) {
			text += '\n';
			column = 0;
		}
		for (; column < entry.first.second; column++)
			text += delimiter;
		text += entry.second;
	}
	if (!shown.empty())
		text += '\n';
	std::ofstream file(path, std::ios::binary | std::ios::trunc);
	if (!file) {
		error = "cannot write " + path;
		return false;
	}
	file.write(text.data(), std::streamsize(text.size()));
	return bool(file);
}


bool
ImportText(const std::string& path, Document& doc, std::string& error,
	const TextAttributes* attributes)
{
	std::ifstream file(path, std::ios::binary);
	if (!file) {
		error = "cannot open " + path;
		return false;
	}
	std::string text((std::istreambuf_iterator<char>(file)),
		std::istreambuf_iterator<char>());

	// conversion "Automatic"; the attributes only count for UTF-8
	// (TTextToBlockStream::GetStyles: field_0x14 < 0)
	int conversion = DetectConversion(text);
	if (conversion >= 0) {
		attributes = nullptr;
		text = ConvertToUTF8(conversion, text, '?');
	}
	std::vector<TextRun> runs;
	if (attributes != nullptr)
		runs = attributes->runs;
	FilterText(text, attributes != nullptr ? &runs : nullptr);

	if (!CreateBlankDocument(doc, error))
		return false;
	if (doc.wordParts.empty()) {
		error = "no word processing part";
		return false;
	}
	size_t part = doc.wordParts[0].first;
	WPPart* wp = doc.EditableWordPart(part);
	SetPartText(*wp, text);

	// CreateStyles: the "alignment" attribute is one paragraph style for
	// all of the text (BTextView center 2 -> 1, right 1 -> 2)
	if (attributes != nullptr && attributes->hasAlignment) {
		int alignment = attributes->alignment == 2 ? 1 : attributes->alignment == 1 ? 2 : 0;
		ApplyParagraphProperty(doc, part, 0, wp->paragraphs.size() - 1,
			AlignmentProperty(alignment));
	}
	// one character style per run: font, size, color
	for (size_t i = 0; i < runs.size(); i++) {
		size_t from = std::min(size_t(std::max(runs[i].offset, 0)), text.size());
		size_t to = i + 1 < runs.size()
			? std::min(size_t(std::max(runs[i + 1].offset, 0)), text.size()) : text.size();
		if (from >= to)
			continue;
		TextPosition a = PositionAt(*wp, from), b = PositionAt(*wp, to);
		ApplyCharacterProperty(doc, part, a, b, TextFontProperty(runs[i].family, runs[i].style));
		ApplyCharacterProperty(doc, part, a, b, TextSizeProperty(runs[i].size));
		ApplyCharacterProperty(doc, part, a, b, TextColorProperty(runs[i].color));
	}
	return true;
}


// #pragma mark - export

namespace {

// WPTranslateTable (Gobe Productive, 0x300780): what TWPExport writes for
// the control characters
const uint8_t kWPTranslateTable[32] = {
	0x20, 0x01, 0x0a, 0x0a, 0x3f, 0x20, 0x20, 0x20,
	0x20, 0x09, 0x0a, 0x0a, 0x0a, 0x20, 0x20, 0x20,
	0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
	0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
};


// a style's own property of `type`, or null
const Property*
OwnProperty(const StyleMgr& styles, uint32_t style, uint32_t type)
{
	if (style >= styles.styles.size())
		return nullptr;
	for (uint32_t p : styles.styles[style].props)
		if (p < styles.properties.size() && styles.properties[p].type == type)
			return &styles.properties[p];
	return nullptr;
}


// TTranslatorStyle::GetTextFont: the style's own font, else its apply
// style's, else its base style's (TStyle::Export: appI +0x18, basI +0x14)
bool
FontOf(const StyleMgr& styles, uint32_t style, std::string& family, std::string& face,
	int depth = 0)
{
	if (style >= styles.styles.size() || depth > 32)
		return false;
	if (const Property* p = OwnProperty(styles, style, Sig("TFon"))) {
		TextStyle t;
		ApplyProperty(*p, t);
		family = t.family;
		face = t.face;
		return true;
	}
	const Style& s = styles.styles[style];
	return FontOf(styles, s.links[1], family, face, depth + 1)
		|| FontOf(styles, s.links[0], family, face, depth + 1);
}

} // namespace


std::string
ExportedText(const Document& doc, size_t part, TextAttributes* attributes,
	const FontCatalog* fonts)
{
	std::string text;
	const WPPart* wp = doc.WordPart(part);
	if (wp == nullptr)
		return text;
	for (const WPParagraph& p : wp->paragraphs)
		for (unsigned char c : p.text)
			text += c < 0x20 ? char(kWPTranslateTable[c]) : char(c);
	if (attributes == nullptr)
		return text;

	const StyleMgr& styles = doc.styles;
	*attributes = TextAttributes();
	// TBlockStreamToText::ExportWord: the first paragraph style's own
	// alignment (Gobe 1 center, 2 right -> BTextView 2, 1), else left
	attributes->hasAlignment = true;
	if (!wp->paragraphs.empty()) {
		if (const Property* p = OwnProperty(styles, wp->paragraphs[0].parStyle, Sig("PAlg"))) {
			TextStyle t;
			ApplyProperty(*p, t);
			attributes->alignment = t.alignment == 1 ? 2 : t.alignment == 2 ? 1 : 0;
		}
	}
	// TWPExport: the character runs, a new one where the style changes;
	// ConvertTextRun: font (with the face's font style), size, color
	uint32_t start = 0;
	uint32_t previous = 0xffffffff;
	bool first = true;
	for (const WPParagraph& p : wp->paragraphs) {
		for (const WPStyleRun& r : p.runs) {
			if (!first && r.style == previous)
				continue;
			first = false;
			previous = r.style;
			TextRun run;
			run.offset = int32_t(start + r.position);
			if (fonts != nullptr) {
				run.family = fonts->plainFamily;
				run.style = fonts->plainStyle;
			}
			FontOf(styles, r.style, run.family, run.style);
			if (const Property* f = OwnProperty(styles, r.style, Sig("TFac"))) {
				TextStyle t;
				ApplyProperty(*f, t);
				if (t.faceFlags & 1)
					run.style.clear();
				else if ((t.faceFlags & 6) != 0 && fonts != nullptr) {
					std::string s = fonts->StyleFor(run.family, (t.faceFlags & 2) != 0,
						(t.faceFlags & 4) != 0);
					if (!s.empty())
						run.style = s;
				}
			}
			if (const Property* s = OwnProperty(styles, r.style, Sig("TSiz"))) {
				TextStyle t;
				ApplyProperty(*s, t);
				run.size = t.size;
			}
			if (const Property* c = OwnProperty(styles, r.style, Sig("TCol"))) {
				TextStyle t;
				ApplyProperty(*c, t);
				run.color = t.color;
			}
			attributes->runs.push_back(run);
		}
		start += uint32_t(p.text.size());
	}
	return text;
}


bool
ExportText(const Document& doc, size_t part, const std::string& path, std::string& error,
	TextAttributes* attributes, const FontCatalog* fonts)
{
	if (doc.WordPart(part) == nullptr) {
		error = "not a word processing part";
		return false;
	}
	std::string text = ExportedText(doc, part, attributes, fonts);
	std::ofstream file(path, std::ios::binary | std::ios::trunc);
	if (!file) {
		error = "cannot write " + path;
		return false;
	}
	file.write(text.data(), std::streamsize(text.size()));
	if (!file) {
		error = "cannot write " + path;
		return false;
	}
	return true;
}

} // namespace gobe
