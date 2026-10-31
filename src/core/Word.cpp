// Word 97 import: [MS-DOC]'s FIB, piece table and FKPs, as TImportWORD reads
// them (WORDTranslator/TImportWORD.c), into a word processing document.
#include "Word.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>

#include "Document.h"
#include "Editing.h"
#include "Excel.h"
#include "Styles.h"
#include "WPPart.h"

namespace gobe {

namespace {

uint16_t
U16(const std::vector<uint8_t>& d, size_t at)
{
	return at + 2 <= d.size() ? uint16_t(d[at] | d[at + 1] << 8) : 0;
}


uint32_t
U32(const std::vector<uint8_t>& d, size_t at)
{
	return at + 4 <= d.size() ? uint32_t(d[at] | d[at + 1] << 8 | d[at + 2] << 16
		| uint32_t(d[at + 3]) << 24) : 0;
}


void
AppendUTF8(std::string& out, uint32_t c)
{
	if (c < 0x80)
		out += char(c);
	else if (c < 0x800) {
		out += char(0xc0 | (c >> 6));
		out += char(0x80 | (c & 0x3f));
	} else {
		out += char(0xe0 | (c >> 12));
		out += char(0x80 | ((c >> 6) & 0x3f));
		out += char(0x80 | (c & 0x3f));
	}
}


// Windows-1252's 0x80-0x9f (the "compressed" pieces' code page)
uint32_t
Cp1252(uint8_t c)
{
	static const uint16_t kHigh[32] = {
		0x20ac, 0x81, 0x201a, 0x192, 0x201e, 0x2026, 0x2020, 0x2021, 0x2c6, 0x2030, 0x160, 0x2039,
		0x152, 0x8d, 0x17d, 0x8f, 0x90, 0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2013, 0x2014,
		0x2dc, 0x2122, 0x161, 0x203a, 0x153, 0x9d, 0x17e, 0x178
	};
	return c >= 0x80 && c < 0xa0 ? kHigh[c - 0x80] : c;
}


// a character's properties, from the CHPX sprms that apply to it
struct CharProps {
	bool bold = false, italic = false, underline = false, strike = false;
	bool superscript = false, subscript = false;
	uint16_t halfPoints = 0;
	int font = -1;
	bool hasColor = false;
	uint8_t r = 0, g = 0, b = 0;
	bool operator==(const CharProps& o) const
	{
		return bold == o.bold && italic == o.italic && underline == o.underline
			&& strike == o.strike && superscript == o.superscript && subscript == o.subscript
			&& halfPoints == o.halfPoints && font == o.font && hasColor == o.hasColor
			&& r == o.r && g == o.g && b == o.b;
	}
};

struct ParaProps {
	int alignment = 0;
	int16_t left = 0, first = 0, right = 0;		// twips (first: relative to left)
	uint16_t before = 0, after = 0;
};


// a sprm's operand size (its spra, the top 3 bits); -1 for the variable
// ones (a size byte first)
int
OperandSize(uint16_t sprm)
{
	switch (sprm >> 13) {
		case 0: case 1: return 1;
		case 2: case 4: case 5: return 2;
		case 3: return 4;
		case 7: return 3;
		default: return -1;
	}
}


// the ico (the 16 colors Word 97 names)
void
IcoColor(int ico, uint8_t& r, uint8_t& g, uint8_t& b)
{
	static const uint8_t kColors[17][3] = {
		{0, 0, 0}, {0, 0, 0}, {0, 0, 255}, {0, 255, 255}, {0, 255, 0}, {255, 0, 255}, {255, 0, 0},
		{255, 255, 0}, {255, 255, 255}, {0, 0, 128}, {0, 128, 128}, {0, 128, 0}, {128, 0, 128},
		{128, 0, 0}, {128, 128, 0}, {128, 128, 128}, {192, 192, 192}
	};
	if (ico < 0 || ico > 16)
		ico = 0;
	r = kColors[ico][0];
	g = kColors[ico][1];
	b = kColors[ico][2];
}


// TCHPXTbl / TPAPXTbl: a grpprl's sprms over the properties
void
ApplyChar(const std::vector<uint8_t>& d, size_t at, size_t end, CharProps& c)
{
	while (at + 2 <= end) {
		uint16_t sprm = U16(d, at);
		at += 2;
		int size = OperandSize(sprm);
		if (size < 0)
			size = at < d.size() ? d[at++] + 0 : 0;
		if (at + size_t(size) > end)
			break;
		uint8_t o = at < d.size() ? d[at] : 0;
		// a toggle: 0 off, 1 on, 0x80 as the style has it, 0x81 the
		// style's opposite (the properties start as the style's)
		auto toggle = [o](bool style) {
			return o == 0x80 ? style : o == 0x81 ? !style : o != 0;
		};
		switch (sprm) {
			case 0x0835: c.bold = toggle(c.bold); break;
			case 0x0836: c.italic = toggle(c.italic); break;
			case 0x0837: c.strike = toggle(c.strike); break;
			case 0x2a3e: c.underline = o != 0; break;
			case 0x4a43: c.halfPoints = U16(d, at); break;
			case 0x2a48: c.superscript = o == 1; c.subscript = o == 2; break;
			case 0x4a4f: c.font = U16(d, at); break;
			case 0x2a42:
				c.hasColor = o != 0;
				IcoColor(o, c.r, c.g, c.b);
				break;
			case 0x6870:	// sprmCCv: a COLORREF
				c.hasColor = true;
				c.r = d[at];
				c.g = d[at + 1];
				c.b = d[at + 2];
				break;
			default:
				break;
		}
		at += size_t(size);
	}
}


void
ApplyPara(const std::vector<uint8_t>& d, size_t at, size_t end, ParaProps& p)
{
	while (at + 2 <= end) {
		uint16_t sprm = U16(d, at);
		at += 2;
		int size = OperandSize(sprm);
		if (sprm == 0xc615) {		// sprmPChgTabs: its own size rules
			size = at < d.size() ? d[at++] : 0;
		} else if (size < 0)
			size = at < d.size() ? d[at++] : 0;
		if (at + size_t(size) > end)
			break;
		switch (sprm) {
			case 0x2403:
			case 0x2461:
				p.alignment = d[at];
				break;
			case 0x840f:
			case 0x845e:
				p.left = int16_t(U16(d, at));
				break;
			case 0x8411:
			case 0x8460:
				p.first = int16_t(U16(d, at));
				break;
			case 0x840e:
			case 0x845d:
				p.right = int16_t(U16(d, at));
				break;
			case 0xa413:
				p.before = U16(d, at);
				break;
			case 0xa414:
				p.after = U16(d, at);
				break;
			default:
				break;
		}
		at += size_t(size);
	}
}

// the style sheet (STSH): each style's kind, base and properties (its
// UPXs' sprms), resolved through the bases; the default font
struct StyleSheet {
	struct Style {
		bool used = false;
		int kind = 0;				// stk: 1 paragraph, 2 character
		uint16_t base = 0x0fff;		// istdBase (0x0fff: none)
		std::vector<uint8_t> papx, chpx;	// the UPXs' sprms
	};
	std::vector<Style> styles;
	uint16_t defaultFont = 0;		// rgftcStandardChpStsh[0]
	bool present = false;

	// [MS-DOC] STSH: cbStshi, the STSHI (cstd, cbSTDBaseInFile, ...,
	// rgftcStandardChpStsh), then each style's cbStd and STD (the base
	// part, xstzName, the UPXs, each padded to an even length)
	void Read(const std::vector<uint8_t>& t, uint32_t fc, uint32_t lcb)
	{
		if (lcb < 4 || size_t(fc) + lcb > t.size())
			return;
		size_t end = size_t(fc) + lcb;
		uint16_t cbStshi = U16(t, fc);
		size_t stshi = size_t(fc) + 2;
		uint16_t cstd = U16(t, stshi);
		uint16_t cbBase = U16(t, stshi + 2);
		if (cbStshi >= 14)
			defaultFont = U16(t, stshi + 12);
		size_t at = stshi + cbStshi;
		present = true;
		for (uint16_t i = 0; i < cstd && at + 2 <= end; i++) {
			uint16_t cbStd = U16(t, at);
			at += 2;
			Style style;
			if (cbStd >= 10 && at + cbStd <= end) {
				style.used = true;
				style.kind = U16(t, at + 2) & 0xf;
				style.base = U16(t, at + 2) >> 4;
				int cupx = U16(t, at + 4) & 0xf;
				size_t q = at + cbBase;
				size_t name = U16(t, q);
				q += 2 + name * 2 + 2;
				size_t stdEnd = at + cbStd;
				for (int u = 0; u < cupx && q + 2 <= stdEnd; u++) {
					size_t cb = U16(t, q);
					q += 2;
					if (q + cb > stdEnd)
						break;
					std::vector<uint8_t> upx(t.begin() + long(q), t.begin() + long(q + cb));
					if (style.kind == 1 && u == 0)
						style.papx.assign(upx.size() >= 2 ? upx.begin() + 2 : upx.end(), upx.end());
					else if ((style.kind == 1 && u == 1) || (style.kind == 2 && u == 0))
						style.chpx = upx;
					q += cb + (cb & 1);
				}
			}
			styles.push_back(style);
			at += cbStd;
		}
	}

	// a style's character properties through its bases (none: the
	// defaults, 10 points in the default font)
	CharProps Char(uint16_t istd, int depth = 0) const
	{
		CharProps c;
		if (!present)
			return c;
		if (istd >= styles.size() || !styles[istd].used || depth > 16) {
			c.halfPoints = 20;
			c.font = defaultFont;
			return c;
		}
		const Style& s = styles[istd];
		c = Char(s.base, depth + 1);
		ApplyChar(s.chpx, 0, s.chpx.size(), c);
		return c;
	}

	ParaProps Para(uint16_t istd, int depth = 0) const
	{
		ParaProps p;
		if (!present || istd >= styles.size() || !styles[istd].used || depth > 16)
			return p;
		const Style& s = styles[istd];
		p = Para(s.base, depth + 1);
		ApplyPara(s.papx, 0, s.papx.size(), p);
		return p;
	}
};

} // namespace


bool
ReadWord(const std::vector<uint8_t>& file, std::vector<WordParagraph>& paragraphs,
	std::string& error)
{
	CompoundFile ole;
	if (!ole.Open(file, error))
		return false;
	std::vector<uint8_t> word, table;
	if (!ole.Stream("WordDocument", word)) {
		error = "no WordDocument stream";
		return false;
	}
	if (U16(word, 0) != 0xa5ec || U16(word, 2) < 0xc1) {
		error = "only Word 97 and later documents are read";
		return false;
	}
	uint16_t flags = U16(word, 0x0a);
	if (flags & 0x0100) {
		error = "the document is encrypted";
		return false;
	}
	if (!ole.Stream((flags & 0x0200) ? "1Table" : "0Table", table)) {
		error = "no table stream";
		return false;
	}
	uint32_t ccpText = U32(word, 0x4c);
	uint32_t fcClx = U32(word, 0x1a2), lcbClx = U32(word, 0x1a6);
	uint32_t fcChpx = U32(word, 0xfa), lcbChpx = U32(word, 0xfe);
	uint32_t fcPapx = U32(word, 0x102), lcbPapx = U32(word, 0x106);
	uint32_t fcFonts = U32(word, 0x112), lcbFonts = U32(word, 0x116);
	StyleSheet stsh;
	stsh.Read(table, U32(word, 0xa2), U32(word, 0xa6));
	// the fonts' names (SttbfFfn: FFNs, the name from byte 40)
	std::vector<std::string> fonts;
	if (lcbFonts > 4 && fcFonts + lcbFonts <= table.size()) {
		uint16_t count = U16(table, fcFonts);
		size_t at = fcFonts + 4;
		for (uint16_t i = 0; i < count && at < fcFonts + lcbFonts; i++) {
			size_t length = size_t(table[at]) + 1;
			std::string name;
			for (size_t k = at + 40; k + 1 < at + length; k += 2) {
				uint16_t c = U16(table, k);
				if (c == 0)
					break;
				AppendUTF8(name, c);
			}
			fonts.push_back(name);
			at += length;
		}
	}
	// the piece table: the main text's characters with their FCs
	struct Piece {
		uint32_t cp, cpEnd, fc;
		bool compressed;
	};
	std::vector<Piece> pieces;
	for (size_t at = fcClx; at < fcClx + lcbClx && at < table.size();) {
		uint8_t clxt = table[at];
		if (clxt == 1) {
			at += 3 + U16(table, at + 1);
			continue;
		}
		if (clxt != 2)
			break;
		uint32_t lcb = U32(table, at + 1);
		size_t plc = at + 5;
		size_t n = (lcb - 4) / 12;
		for (size_t i = 0; i < n; i++) {
			uint32_t fc = U32(table, plc + (n + 1) * 4 + i * 8 + 2);
			pieces.push_back({U32(table, plc + i * 4), U32(table, plc + (i + 1) * 4),
				(fc & 0x40000000) ? (fc & ~0x40000000u) / 2 : fc, (fc & 0x40000000) != 0});
		}
		break;
	}
	if (pieces.empty()) {
		error = "no piece table";
		return false;
	}
	// the text with each character's FC
	std::vector<uint32_t> chars, fcs;
	for (const Piece& p : pieces) {
		for (uint32_t cp = p.cp; cp < p.cpEnd && cp < ccpText; cp++) {
			uint32_t fc = p.fc + (cp - p.cp) * (p.compressed ? 1 : 2);
			if (p.compressed) {
				if (fc >= word.size())
					break;
				chars.push_back(Cp1252(word[fc]));
			} else {
				if (fc + 1 >= word.size())
					break;
				chars.push_back(U16(word, fc));
			}
			fcs.push_back(fc);	// (the FKPs' FCs are the bytes' offsets)
		}
	}
	// the FKPs: FC ranges and their CHPX / PAPX
	auto fkpEntries = [&](uint32_t fc, uint32_t lcb, bool paragraphs,
			std::vector<std::pair<std::pair<uint32_t, uint32_t>, size_t>>& out) {
		if (lcb < 8 || fc + lcb > table.size())
			return;
		size_t n = (lcb - 4) / 8;
		for (size_t i = 0; i < n; i++) {
			uint32_t pn = U32(table, fc + (n + 1) * 4 + i * 4) & 0x3fffff;
			size_t page = size_t(pn) * 512;
			if (page + 512 > word.size())
				continue;
			uint8_t crun = word[page + 511];
			for (size_t k = 0; k < crun; k++) {
				uint32_t from = U32(word, page + k * 4), to = U32(word, page + (k + 1) * 4);
				size_t offsetAt = page + (size_t(crun) + 1) * 4 + k * (paragraphs ? 13 : 1);
				size_t offset = size_t(word[offsetAt]) * 2;
				out.push_back({{from, to}, offset == 0 ? 0 : page + offset});
			}
		}
	};
	std::vector<std::pair<std::pair<uint32_t, uint32_t>, size_t>> chpx, papx;
	fkpEntries(fcChpx, lcbChpx, false, chpx);
	fkpEntries(fcPapx, lcbPapx, true, papx);
	// a paragraph's PAPX (its mark's FC): where its istd and sprms are
	auto papxOf = [&](uint32_t fc, size_t& from, size_t& to) {
		for (const auto& e : papx)
			if (fc >= e.first.first && fc < e.first.second) {
				if (e.second == 0 || e.second >= word.size())
					return false;
				size_t q = e.second;
				size_t cb = word[q++];
				size_t length = cb != 0 ? cb * 2 - 1 : size_t(word[q++]) * 2;
				from = q;
				to = std::min(word.size(), q + length);
				return to >= from + 2;
			}
		return false;
	};
	auto styleOf = [&](uint32_t markFC) {
		size_t from, to;
		return papxOf(markFC, from, to) ? U16(word, from) : uint16_t(0);
	};
	// a character's properties: its paragraph style's, then its
	// character style's (sprmCIstd), then its own
	auto charProps = [&](uint32_t fc, uint16_t paragraphStyle) {
		CharProps c = stsh.Char(paragraphStyle);
		for (const auto& e : chpx)
			if (fc >= e.first.first && fc < e.first.second) {
				if (e.second != 0 && e.second < word.size()) {
					size_t from = e.second + 1, to = e.second + 1 + word[e.second];
					for (size_t q = from; q + 4 <= to; ) {
						uint16_t sprm = U16(word, q);
						if (sprm == 0x4a30) {
							uint16_t istd = U16(word, q + 2);
							if (stsh.present && istd < stsh.styles.size()) {
								const auto& cs = stsh.styles[istd];
								ApplyChar(cs.chpx, 0, cs.chpx.size(), c);
							}
							break;
						}
						int size = OperandSize(sprm);
						q += 2 + size_t(size < 0 ? word[q + 2] + 1 : size);
					}
					ApplyChar(word, from, to, c);
				}
				break;
			}
		return c;
	};
	auto paraProps = [&](uint32_t fc) {
		size_t from, to;
		if (!papxOf(fc, from, to))
			return stsh.Para(0);
		// the istd first, then the sprms
		ParaProps p = stsh.Para(U16(word, from));
		ApplyPara(word, from + 2, to, p);
		return p;
	};
	// each character's paragraph style (from the paragraph's mark)
	std::vector<uint16_t> paragraphStyles(chars.size(), 0);
	for (size_t i = chars.size(), style = 0; i-- > 0;) {
		if (chars[i] == 0x0d || chars[i] == 0x0c || chars[i] == 0x07)
			style = styleOf(fcs[i]);
		paragraphStyles[i] = uint16_t(style);
	}

	// the paragraphs: text, runs, properties (from the paragraph mark)
	paragraphs.clear();
	paragraphs.emplace_back();
	CharProps current;
	bool haveCurrent = false;
	int field = 0;				// inside a field's code (until its separator)
	std::vector<int> fieldStack;
	size_t charIndex = 0;		// the character being added (its paragraph's style)
	auto addChar = [&](uint32_t c, uint32_t fc) {
		CharProps props = charProps(fc, charIndex < paragraphStyles.size()
			? paragraphStyles[charIndex] : 0);
		WordParagraph& p = paragraphs.back();
		if (!haveCurrent || !(props == current) || p.runs.empty()) {
			WordRun run;
			run.bold = props.bold;
			run.italic = props.italic;
			run.underline = props.underline;
			run.strike = props.strike;
			run.superscript = props.superscript;
			run.subscript = props.subscript;
			run.size = props.halfPoints / 2.0f;
			if (props.font >= 0 && size_t(props.font) < fonts.size())
				run.font = fonts[size_t(props.font)];
			run.hasColor = props.hasColor;
			run.r = props.r;
			run.g = props.g;
			run.b = props.b;
			p.runs.push_back(run);
			current = props;
			haveCurrent = true;
		}
		AppendUTF8(p.runs.back().text, c);
	};
	for (size_t i = 0; i < chars.size(); i++) {
		uint32_t c = chars[i];
		charIndex = i;
		// fields: 0x13 begin, 0x14 the result follows, 0x15 end
		if (c == 0x13) {
			fieldStack.push_back(1);
			field++;
			continue;
		}
		if (c == 0x14) {
			if (!fieldStack.empty() && fieldStack.back() == 1) {
				fieldStack.back() = 2;
				field--;
			}
			continue;
		}
		if (c == 0x15) {
			if (!fieldStack.empty()) {
				if (fieldStack.back() == 1)
					field--;
				fieldStack.pop_back();
			}
			continue;
		}
		if (field > 0)
			continue;
		if (c == 0x0d || c == 0x0c || c == 0x07) {
			// the paragraph mark carries the paragraph's properties
			ParaProps props = paraProps(fcs[i]);
			WordParagraph& p = paragraphs.back();
			p.alignment = props.alignment;
			p.left = props.left / 20.0f;
			p.first = (props.left + props.first) / 20.0f;
			p.right = props.right / 20.0f;
			p.before = props.before / 20.0f;
			p.after = props.after / 20.0f;
			if (c == 0x07) {
				// a table cell mark: cells side by side, a tab between
				addChar('\t', fcs[i]);
				continue;
			}
			p.pageBreak = c == 0x0c;
			paragraphs.emplace_back();
			haveCurrent = false;
			continue;
		}
		if (c == 0x0b) {
			addChar(0x01, fcs[i]);		// a line break
			continue;
		}
		if (c == 0x1e) {
			addChar('-', fcs[i]);		// a non-breaking hyphen
			continue;
		}
		if (c < 0x20 && c != '\t')
			continue;					// pictures (1), footnote marks (2), optional hyphens
		addChar(c, fcs[i]);
	}
	if (paragraphs.back().runs.empty())
		paragraphs.pop_back();
	return true;
}


bool
ImportWord(const std::string& path, Document& doc, std::string& error)
{
	std::ifstream in(path, std::ios::binary);
	if (!in) {
		error = "cannot open " + path;
		return false;
	}
	std::vector<uint8_t> file((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	std::vector<WordParagraph> paragraphs;
	if (!ReadWord(file, paragraphs, error))
		return false;
	if (!CreateBlankDocument(doc, error))
		return false;
	size_t part = doc.wordParts.empty() ? 0 : doc.wordParts[0].first;
	WPPart* wp = doc.EditableWordPart(part);
	if (wp == nullptr) {
		error = "no word processing part";
		return false;
	}
	std::string all;
	for (size_t k = 0; k < paragraphs.size(); k++) {
		for (const WordRun& r : paragraphs[k].runs)
			all += r.text;
		if (k + 1 < paragraphs.size())
			all += '\n';
	}
	InsertMultiline(*wp, {0, 0}, all);
	for (size_t k = 0; k < paragraphs.size() && k < doc.WordPart(part)->paragraphs.size(); k++) {
		const WordParagraph& p = paragraphs[k];
		size_t offset = 0;
		for (const WordRun& r : p.runs) {
			TextPosition from{k, offset}, to{k, offset + r.text.size()};
			offset += r.text.size();
			uint32_t faces = (r.bold ? 2 : 0) | (r.italic ? 4 : 0) | (r.underline ? 8 : 0)
				| (r.strike ? 0x20 : 0) | (r.superscript ? 0x40 : 0) | (r.subscript ? 0x80 : 0);
			if (faces != 0)
				ApplyCharacterProperty(doc, part, from, to, TextFaceProperty(faces));
			if (r.size > 0 && r.size != 12)
				ApplyCharacterProperty(doc, part, from, to, TextSizeProperty(r.size));
			if (!r.font.empty())
				ApplyCharacterProperty(doc, part, from, to, TextFontProperty(r.font, ""));
			if (r.hasColor && (r.r || r.g || r.b)) {
				Color color;
				color.r = r.r;
				color.g = r.g;
				color.b = r.b;
				ApplyCharacterProperty(doc, part, from, to, TextColorProperty(color));
			}
		}
		if (p.alignment != 0)
			ApplyParagraphProperty(doc, part, k, k, AlignmentProperty(std::min(p.alignment, 3)));
		if (p.left != 0)
			ApplyParagraphProperty(doc, part, k, k, IndentProperty(Sig("PLef"), p.left));
		if (p.first != 0)
			ApplyParagraphProperty(doc, part, k, k, IndentProperty(Sig("PInd"), p.first));
		if (p.right != 0)
			ApplyParagraphProperty(doc, part, k, k, IndentProperty(Sig("PRig"), p.right));
		if (p.before != 0)
			ApplyParagraphProperty(doc, part, k, k, SpacingProperty(Sig("SpaB"), p.before, false));
		if (p.after != 0)
			ApplyParagraphProperty(doc, part, k, k, SpacingProperty(Sig("SpaA"), p.after, false));
	}
	return true;
}

} // namespace gobe
