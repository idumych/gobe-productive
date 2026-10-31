// Word 97 export: TExportWORD's file ([MS-DOC]): the FIB, the text at
// 0x400, the CHPX and PAPX FKPs and a SEPX in the WordDocument stream; the
// style sheet, the section table, the piece table, the bin tables, the
// font table and the DOP in the 1Table stream.
#include "Word.h"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <fstream>

#include "Document.h"
#include "Excel.h"
#include "Fields.h"
#include "SSEval.h"
#include "Styles.h"
#include "WPPart.h"

namespace gobe {

namespace {

void
Put16(std::vector<uint8_t>& d, size_t at, uint16_t v)
{
	if (d.size() < at + 2)
		d.resize(at + 2, 0);
	d[at] = uint8_t(v);
	d[at + 1] = uint8_t(v >> 8);
}


void
Put32(std::vector<uint8_t>& d, size_t at, uint32_t v)
{
	Put16(d, at, uint16_t(v));
	Put16(d, at + 2, uint16_t(v >> 16));
}


void
Add16(std::vector<uint8_t>& d, uint16_t v)
{
	Put16(d, d.size(), v);
}


void
Add32(std::vector<uint8_t>& d, uint32_t v)
{
	Put32(d, d.size(), v);
}


// UTF-8 to code points (a bad byte as itself)
std::vector<uint32_t>
CodePoints(const std::string& s)
{
	std::vector<uint32_t> out;
	for (size_t i = 0; i < s.size();) {
		uint8_t c = uint8_t(s[i]);
		size_t extra = c >= 0xf0 ? 3 : c >= 0xe0 ? 2 : c >= 0xc0 ? 1 : 0;
		uint32_t v = extra == 0 ? c : c & (0x3f >> extra);
		size_t k = 1;
		for (; k <= extra && i + k < s.size() && (uint8_t(s[i + k]) & 0xc0) == 0x80; k++)
			v = v << 6 | (uint8_t(s[i + k]) & 0x3f);
		if (k <= extra) {
			out.push_back(c);
			i++;
			continue;
		}
		out.push_back(v);
		i += extra + 1;
	}
	return out;
}


// a code point as a Windows-1252 byte; -1 if it has none
int
Cp1252Byte(uint32_t c)
{
	static const uint16_t kHigh[32] = {
		0x20ac, 0x81, 0x201a, 0x192, 0x201e, 0x2026, 0x2020, 0x2021, 0x2c6, 0x2030, 0x160, 0x2039,
		0x152, 0x8d, 0x17d, 0x8f, 0x90, 0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2013, 0x2014,
		0x2dc, 0x2122, 0x161, 0x203a, 0x153, 0x9d, 0x17e, 0x178
	};
	if (c < 0x80 || (c >= 0xa0 && c < 0x100))
		return int(c);
	for (int i = 0; i < 32; i++)
		if (kHigh[i] == c)
			return 0x80 + i;
	return -1;
}


// TFontBE::setFontFamilyCode: BeOS's fonts by family (FFID's ff: 1
// roman, 2 swiss, 3 modern, 4 script, 5 decorative); the families'
// defaults are written as Windows names (TFontTblWORD's first fonts)
struct WordFont {
	std::string name;
	uint8_t family;
};

WordFont
FontFor(const std::string& be)
{
	if (be == "Dutch801 Rm BT")
		return {"Times New Roman", 1};
	if (be == "Swis721 BT")
		return {"Arial", 2};
	if (be == "Baskerville" || be == "ClassGarmnd BT")
		return {be, 1};
	if (be == "Humnst777 BT" || be == "Monospac821 BT" || be == "ProFont"
			|| be == "ProFontISOLatin 1" || be == "Swis911 XCm BT" || be == "VAGRounded BT")
		return {be, 2};
	if (be == "Courier 10 BT")
		return {be, 3};
	if (be == "CommercialScript" || be == "Embassy")
		return {be, 4};
	if (be == "CopprplGoth Bd BT")
		return {be, 5};
	return {be, 0};
}


// the font table: Times New Roman and Arial first, then each other font
struct FontTable {
	std::vector<WordFont> fonts{{"Times New Roman", 1}, {"Arial", 2}};

	uint16_t Index(const std::string& name)
	{
		WordFont font = FontFor(name);
		for (size_t i = 0; i < fonts.size(); i++)
			if (fonts[i].name == font.name)
				return uint16_t(i);
		fonts.push_back(font);
		return uint16_t(fonts.size() - 1);
	}

	// SttbfFfn: cData, cbExtra, then each FFN (cbFfnM1, FFID, wWeight,
	// chs, ixchSzAlt, panose, fs, the name: from byte 40)
	std::vector<uint8_t> Bytes() const
	{
		std::vector<uint8_t> out;
		Add16(out, uint16_t(fonts.size()));
		Add16(out, 0);
		for (const WordFont& f : fonts) {
			std::vector<uint8_t> ffn(40, 0);
			// FFID: prq 2 (variable pitch), fTrueType, ff
			ffn[1] = uint8_t(0x02 | 0x04 | (f.family << 4));
			Put16(ffn, 2, 400);
			for (uint32_t c : CodePoints(f.name))
				Add16(ffn, uint16_t(c));
			Add16(ffn, 0);
			ffn[0] = uint8_t(ffn.size() - 1);
			out.insert(out.end(), ffn.begin(), ffn.end());
		}
		return out;
	}
};


// a sprm and its operand
void
Sprm(std::vector<uint8_t>& g, uint16_t sprm, uint32_t operand)
{
	Add16(g, sprm);
	switch (sprm >> 13) {
		case 0: case 1:
			g.push_back(uint8_t(operand));
			break;
		case 2: case 4: case 5:
			Add16(g, uint16_t(operand));
			break;
		case 3:
			Add32(g, operand);
			break;
	}
}


// a run's CHPX grpprl: the faces that are on, then always its size, its
// font and its color (sprmCHps, sprmCRgFtc0, sprmCCv)
std::vector<uint8_t>
Chpx(const WordRun& r, FontTable& fonts)
{
	std::vector<uint8_t> g;
	if (r.bold)
		Sprm(g, 0x0835, 1);
	if (r.italic)
		Sprm(g, 0x0836, 1);
	if (r.strike)
		Sprm(g, 0x0837, 1);
	if (r.underline)
		Sprm(g, 0x2a3e, 1);
	if (r.superscript || r.subscript)
		Sprm(g, 0x2a48, r.superscript ? 1 : 2);
	if (r.size > 0)
		Sprm(g, 0x4a43, uint32_t(std::lround(r.size * 2)));
	if (!r.font.empty())
		Sprm(g, 0x4a4f, fonts.Index(r.font));
	if (r.hasColor)
		Sprm(g, 0x6870, uint32_t(r.r) | uint32_t(r.g) << 8 | uint32_t(r.b) << 16);
	return g;
}


uint16_t
Twips(float points)
{
	return uint16_t(int16_t(std::lround(points * 20)));
}


// a paragraph's PAPX: the istd (Normal), then sprmPJc, the indents
// (sprmPDxaLeft1 the first line's from the left) and the spacing
std::vector<uint8_t>
Papx(const WordParagraph& p)
{
	std::vector<uint8_t> g;
	Add16(g, 0);
	if (p.alignment != 0)
		Sprm(g, 0x2403, uint32_t(p.alignment));
	if (p.left != 0)
		Sprm(g, 0x840f, Twips(p.left));
	if (p.first != p.left)
		Sprm(g, 0x8411, Twips(p.first - p.left));
	if (p.right != 0)
		Sprm(g, 0x840e, Twips(p.right));
	if (p.before != 0)
		Sprm(g, 0xa413, Twips(p.before));
	if (p.after != 0)
		Sprm(g, 0xa414, Twips(p.after));
	return g;
}


// a range of the text (FCs) and its properties' bytes
struct Range {
	uint32_t from, to;
	std::vector<uint8_t> props;
};


// the FKPs ([MS-DOC] ChpxFkp, PapxFkp), page after page at the end of
// `word` (a multiple of 512); their bin table (PlcBteChpx, PlcBtePapx)
// to `table`. Each page: the FCs, an offset (in words) per range (a PAPX's
// with a zero PHE), the properties from the page's end down, the count.
void
WriteFkps(std::vector<uint8_t>& word, const std::vector<Range>& ranges, bool paragraphs,
	std::vector<uint8_t>& table)
{
	const size_t entry = paragraphs ? 13 : 1;
	std::vector<uint32_t> firsts, pages;
	for (size_t i = 0; i < ranges.size();) {
		size_t page = word.size();
		word.resize(page + 512, 0);
		size_t low = 511;
		std::vector<std::pair<uint32_t, uint8_t>> entries;	// FC, offset
		while (i < ranges.size() && entries.size() < 0xff) {
			const Range& r = ranges[i];
			// a CHPX: cb, grpprl (none: the default properties); a
			// PapxInFkp: an odd grpprl as cb (2 cb - 1 bytes), an even one
			// as 0 and cb' (2 cb' bytes)
			std::vector<uint8_t> props;
			if (paragraphs) {
				if (r.props.size() % 2)
					props.push_back(uint8_t((r.props.size() + 1) / 2));
				else {
					props.push_back(0);
					props.push_back(uint8_t(r.props.size() / 2));
				}
			} else if (!r.props.empty())
				props.push_back(uint8_t(r.props.size()));
			if (!props.empty())
				props.insert(props.end(), r.props.begin(), r.props.end());
			size_t n = entries.size() + 1;
			size_t header = 4 * (n + 1) + entry * n;
			size_t at = props.empty() ? low : (low - std::min(low, props.size())) & ~size_t(1);
			if (props.size() > low || at < header)
				break;
			if (!props.empty()) {
				std::copy(props.begin(), props.end(), word.begin() + long(page + at));
				low = at;
			}
			entries.push_back({r.from, props.empty() ? uint8_t(0) : uint8_t(at / 2)});
			i++;
		}
		if (entries.empty())
			break;		// (a range's properties larger than a page: not possible here)
		size_t count = entries.size();
		for (size_t k = 0; k < count; k++) {
			Put32(word, page + k * 4, entries[k].first);
			word[page + (count + 1) * 4 + k * entry] = entries[k].second;
		}
		Put32(word, page + count * 4, ranges[i - 1].to);
		word[page + 511] = uint8_t(count);
		firsts.push_back(entries[0].first);
		pages.push_back(uint32_t(page / 512));
	}
	for (uint32_t fc : firsts)
		Add32(table, fc);
	Add32(table, ranges.empty() ? 0 : ranges.back().to);
	for (uint32_t pn : pages)
		Add32(table, pn);
}


// a DTTM ([MS-DOC] 2.9.66) for now
uint32_t
Dttm()
{
	time_t now = time(nullptr);
	struct tm t;
	localtime_r(&now, &t);
	return uint32_t(t.tm_min) | uint32_t(t.tm_hour) << 6 | uint32_t(t.tm_mday) << 11
		| uint32_t(t.tm_mon + 1) << 16 | uint32_t(t.tm_year) << 20 | uint32_t(t.tm_wday) << 29;
}


// the STSH: an STSHI (cstd 15, cbSTDBaseInFile 10, the names written,
// istdMaxFixedWhenSaved 15) and the Normal style (sti 0, a paragraph
// style with no base, its next itself, an empty PAPX and CHPX); the
// other 14 fixed slots empty
std::vector<uint8_t>
StyleSheet()
{
	std::vector<uint8_t> out;
	Add16(out, 18);			// cbStshi
	Add16(out, 15);			// cstd
	Add16(out, 10);			// cbSTDBaseInFile
	Add16(out, 1);			// fStdStylenamesWritten
	Add16(out, 15);			// stiMaxWhenSaved
	Add16(out, 15);			// istdMaxFixedWhenSaved
	Add16(out, 0);			// nVerBuiltInNamesWhenSaved
	Add16(out, 0);			// rgftcStandardChpStsh: Times New Roman
	Add16(out, 0);
	Add16(out, 0);
	std::vector<uint8_t> normal;
	Add16(normal, 0);		// sti 0 (Normal)
	Add16(normal, 0xfff1);	// stk 1 (paragraph), istdBase none
	Add16(normal, 0x0002);	// cupx 2, istdNext 0
	Add16(normal, 0);		// bchUpe (below)
	Add16(normal, 0);		// grfstd
	Add16(normal, 6);		// xstzName "Normal"
	for (char c : std::string("Normal"))
		Add16(normal, uint16_t(c));
	Add16(normal, 0);
	Add16(normal, 2);		// the PAPX: istd 0
	Add16(normal, 0);
	Add16(normal, 0);		// the CHPX: none
	Put16(normal, 6, uint16_t(normal.size()));
	Add16(out, uint16_t(normal.size()));
	out.insert(out.end(), normal.begin(), normal.end());
	for (int i = 1; i < 15; i++)
		Add16(out, 0);
	return out;
}


// the DOP: TExportWORD::initDOP's values (facing pages off, widow
// control, footnotes from 1, a 0.5 inch default tab, a 0.25 inch hot
// zone), created and revised now, revision 1; the rest zero (500 bytes,
// StaticDop's size)
std::vector<uint8_t>
Dop()
{
	std::vector<uint8_t> dop(500, 0);
	Put16(dop, 0x00, 0x22);
	Put16(dop, 0x02, 4);
	Put16(dop, 0x04, 0x831);
	Put16(dop, 0x06, 0x1888);
	Put16(dop, 0x0a, 720);	// dxaTab
	Put16(dop, 0x0e, 360);	// dxaHotZ
	uint32_t now = Dttm();
	Put32(dop, 0x14, now);	// dttmCreated
	Put32(dop, 0x18, now);	// dttmRevised
	Put16(dop, 0x20, 1);	// nRevision
	return dop;
}

} // namespace


std::vector<WordParagraph>
WordParagraphsOf(const Document& doc, size_t part)
{
	std::vector<WordParagraph> out;
	const WPPart* wp = doc.WordPart(part);
	if (wp == nullptr)
		return out;
	const StyleMgr& styles = doc.styles;
	SSBook book(doc);
	const uint32_t kNoStyle = 0xffffffff;
	auto runOf = [&](uint32_t style, const TextStyle& paragraph) {
		TextStyle t = style == kNoStyle ? paragraph : ResolveTextStyle(styles, style);
		WordRun r;
		r.bold = t.faceFlags & 2;
		r.italic = t.faceFlags & 4;
		r.underline = t.faceFlags & 8;
		r.strike = t.faceFlags & 0x20;
		r.superscript = t.faceFlags & 0x40;
		r.subscript = t.faceFlags & 0x80;
		r.size = t.size;
		r.font = t.family;
		r.hasColor = true;
		r.r = t.color.r;
		r.g = t.color.g;
		r.b = t.color.b;
		return r;
	};
	auto same = [](const WordRun& a, const WordRun& b) {
		return a.bold == b.bold && a.italic == b.italic && a.underline == b.underline
			&& a.strike == b.strike && a.superscript == b.superscript
			&& a.subscript == b.subscript && a.size == b.size && a.font == b.font
			&& a.hasColor == b.hasColor && a.r == b.r && a.g == b.g && a.b == b.b;
	};
	for (const WPParagraph& p : wp->paragraphs) {
		TextStyle ps = ResolveTextStyle(styles, p.parStyle);
		WordParagraph w;
		w.alignment = ps.alignment;
		w.left = ps.leftIndent;
		w.first = ps.firstIndent;
		w.right = ps.rightIndent;
		// spacing in lines taken as lines of the paragraph style's size
		w.before = ps.spaceBeforeLines ? ps.spaceBefore * ps.size : ps.spaceBefore;
		w.after = ps.spaceAfterLines ? ps.spaceAfter * ps.size : ps.spaceAfter;
		std::string text = p.text;
		if (!text.empty() && text.back() == '\n')
			text.pop_back();
		if (!text.empty() && (text.back() == 0x0c || text.back() == 0x02)) {
			w.pageBreak = text.back() == 0x0c;
			text.pop_back();
		}
		size_t run = 0;
		auto add = [&](const WordRun& props, const std::string& s) {
			if (s.empty())
				return;
			if (w.runs.empty() || !same(w.runs.back(), props)) {
				w.runs.push_back(props);
				w.runs.back().text.clear();
			}
			w.runs.back().text += s;
		};
		for (size_t at = 0; at < text.size();) {
			while (run + 1 < p.runs.size() && p.runs[run + 1].position <= at)
				run++;
			WordRun props = runOf(run < p.runs.size() ? p.runs[run].style : kNoStyle, ps);
			const WPMiscInfo* special = nullptr;
			for (const WPMiscInfo& m : p.misc)
				if (m.position == at)
					special = &m;
			if (special != nullptr) {
				// fields as their text; frames and separator lines aren't
				// written
				if (special->kind != Sig("FRAM") && special->kind != Sig("LINE"))
					add(props, FieldText(book, part, *special, -1));
				at++;
				continue;
			}
			size_t stop = text.size();
			if (run + 1 < p.runs.size() && p.runs[run + 1].position < stop)
				stop = p.runs[run + 1].position;
			for (const WPMiscInfo& m : p.misc)
				if (m.position > at && m.position < stop)
					stop = m.position;
			add(props, text.substr(at, stop - at));
			at = stop;
		}
		if (w.runs.empty()) {
			// an empty paragraph: its mark in the first run's style
			w.runs.push_back(runOf(p.runs.empty() ? kNoStyle : p.runs[0].style, ps));
			w.runs.back().text.clear();
		}
		out.push_back(w);
	}
	return out;
}


std::vector<uint8_t>
WordFileBytes(const std::vector<WordParagraph>& paragraphs)
{
	FontTable fonts;
	// the text, each character with its run's CHPX; a paragraph's mark
	// with its last run's
	std::vector<uint32_t> chars;
	std::vector<Range> runs, paras;
	std::vector<std::vector<uint8_t>> charProps;
	for (const WordParagraph& p : paragraphs) {
		size_t paraStart = chars.size();
		std::vector<uint8_t> last;
		for (const WordRun& r : p.runs) {
			std::vector<uint8_t> props = Chpx(r, fonts);
			for (uint32_t c : CodePoints(r.text)) {
				chars.push_back(c == 0x01 ? 0x0b : c);
				charProps.push_back(props);
			}
			last = props;
		}
		chars.push_back(p.pageBreak ? 0x0c : 0x0d);
		charProps.push_back(last);
		paras.push_back({uint32_t(paraStart), uint32_t(chars.size()), Papx(p)});
	}
	if (chars.empty()) {
		chars.push_back(0x0d);
		charProps.push_back({});
		paras.push_back({0, 1, Papx(WordParagraph())});
	}
	bool compressed = std::all_of(chars.begin(), chars.end(),
		[](uint32_t c) { return Cp1252Byte(c) >= 0; });
	const uint32_t kText = 0x400;
	const uint32_t width = compressed ? 1 : 2;
	auto fc = [&](uint32_t cp) { return kText + cp * width; };

	std::vector<uint8_t> word(kText, 0);
	for (uint32_t c : chars) {
		if (compressed)
			word.push_back(uint8_t(Cp1252Byte(c)));
		else
			Add16(word, uint16_t(c > 0xffff ? '?' : c));
	}
	uint32_t textEnd = uint32_t(word.size());
	// the character runs: characters with the same CHPX together
	for (size_t i = 0; i < chars.size();) {
		size_t j = i + 1;
		while (j < chars.size() && charProps[j] == charProps[i])
			j++;
		runs.push_back({fc(uint32_t(i)), fc(uint32_t(j)), charProps[i]});
		i = j;
	}
	for (Range& p : paras) {
		p.from = fc(p.from);
		p.to = fc(p.to);
	}
	word.resize((word.size() + 511) / 512 * 512, 0);

	std::vector<uint8_t> table;
	// the style sheet
	uint32_t stshAt = uint32_t(table.size());
	std::vector<uint8_t> stsh = StyleSheet();
	table.insert(table.end(), stsh.begin(), stsh.end());
	uint32_t stshSize = uint32_t(stsh.size());
	// the FKPs and their bin tables
	uint32_t chpxAt = uint32_t(table.size());
	WriteFkps(word, runs, false, table);
	uint32_t chpxSize = uint32_t(table.size()) - chpxAt;
	uint32_t papxAt = uint32_t(table.size());
	WriteFkps(word, paras, true, table);
	uint32_t papxSize = uint32_t(table.size()) - papxAt;
	// one section, its SEPX (no sprms) after the FKPs
	uint32_t sepxAt = uint32_t(word.size());
	Add16(word, 0);
	uint32_t sedAt = uint32_t(table.size());
	Add32(table, 0);
	Add32(table, uint32_t(chars.size()));
	Add16(table, 0);			// fn
	Add32(table, sepxAt);		// fcSepx
	Add16(table, 0);			// fnMpr
	Add32(table, 0xffffffff);	// fcMpr
	uint32_t sedSize = uint32_t(table.size()) - sedAt;
	// the piece table: one piece
	uint32_t clxAt = uint32_t(table.size());
	table.push_back(2);
	Add32(table, 4 + 4 + 8);
	Add32(table, 0);
	Add32(table, uint32_t(chars.size()));
	Add16(table, 0);
	Add32(table, compressed ? (kText * 2) | 0x40000000 : kText);
	Add16(table, 0);
	uint32_t clxSize = uint32_t(table.size()) - clxAt;
	// the fonts (all used by now), the DOP
	uint32_t fontsAt = uint32_t(table.size());
	std::vector<uint8_t> ffn = fonts.Bytes();
	table.insert(table.end(), ffn.begin(), ffn.end());
	uint32_t fontsSize = uint32_t(table.size()) - fontsAt;
	uint32_t dopAt = uint32_t(table.size());
	std::vector<uint8_t> dop = Dop();
	table.insert(table.end(), dop.begin(), dop.end());

	// both streams at least 4096 bytes, out of the mini stream, as Word's
	// are (readers such as antiword refuse a smaller WordDocument)
	word.resize(std::max<size_t>(word.size(), 4096), 0);
	table.resize(std::max<size_t>(table.size(), 4096), 0);

	// the FIB: FibBase, 14 FibRgW words, 22 FibRgLw longs, 93 FC/LCB
	// pairs (FibRgFcLcb97), no FibRgCswNew
	Put16(word, 0x00, 0xa5ec);		// wIdent
	Put16(word, 0x02, 0xc1);		// nFib
	Put16(word, 0x06, 0x0409);		// lid
	Put16(word, 0x0a, 0x1200);		// fWhichTblStm, fExtChar
	Put16(word, 0x0c, 0xbf);		// nFibBack
	Put32(word, 0x18, kText);		// fcMin
	Put32(word, 0x1c, textEnd);		// fcMac
	Put16(word, 0x20, 14);			// csw
	Put16(word, 0x3c, 0x0409);		// lidFE
	Put16(word, 0x3e, 22);			// cslw
	Put32(word, 0x40, uint32_t(word.size()));	// cbMac
	Put32(word, 0x4c, uint32_t(chars.size()));	// ccpText
	Put16(word, 0x98, 0x5d);		// cbRgFcLcb
	auto pair = [&](int index, uint32_t at, uint32_t size) {
		Put32(word, 0x9a + size_t(index) * 8, at);
		Put32(word, 0x9a + size_t(index) * 8 + 4, size);
	};
	pair(1, stshAt, stshSize);		// fcStshf
	pair(6, sedAt, sedSize);		// fcPlcfSed
	pair(12, chpxAt, chpxSize);		// fcPlcfBteChpx
	pair(13, papxAt, papxSize);		// fcPlcfBtePapx
	pair(15, fontsAt, fontsSize);	// fcSttbfFfn
	pair(31, dopAt, uint32_t(dop.size()));	// fcDop
	pair(33, clxAt, clxSize);		// fcClx
	Put16(word, 0x9a + 93 * 8, 0);	// cswNew
	return MakeCompoundFile({{"WordDocument", word}, {"1Table", table}});
}


std::vector<uint8_t>
WordBytes(const Document& doc)
{
	// TExportWORD::DoTranslate: the active sheet's main part
	int main = doc.MainPart();
	return WordFileBytes(WordParagraphsOf(doc, main >= 0 ? size_t(main) : 0));
}


bool
ExportWord(const Document& doc, const std::string& path, std::string& error)
{
	int main = doc.MainPart();
	if (main < 0 || doc.WordPart(size_t(main)) == nullptr) {
		error = "not a word processing document";
		return false;
	}
	std::vector<uint8_t> bytes = WordBytes(doc);
	std::ofstream out(path, std::ios::binary);
	if (!out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()))) {
		error = "cannot write " + path;
		return false;
	}
	return true;
}

} // namespace gobe
