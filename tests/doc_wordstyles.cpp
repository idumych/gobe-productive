// doc_wordstyles: a Word 97 document's style sheet (STSH) applied when
// it's read: Normal (11 points, Arial), Heading based on it (centered,
// bold, 16 points) and a character style Emph (italic). "Title" is a
// Heading paragraph with no formatting of its own; "Body emph bold" is a
// Normal paragraph, indented, with "emph" in Emph (sprmCIstd) and "bold"
// made the opposite of the style's bold (sprmCFBold 0x81).
// Arguments: none (documents given are ignored).
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "Excel.h"
#include "Word.h"

using namespace gobe;

static int sOk = 0, sFailed = 0;

static void
Check(bool ok, const std::string& what)
{
	if (ok)
		sOk++;
	else {
		sFailed++;
		printf("FAIL %s\n", what.c_str());
	}
}


static void
Put16(std::vector<uint8_t>& d, size_t at, uint16_t v)
{
	if (d.size() < at + 2)
		d.resize(at + 2, 0);
	d[at] = uint8_t(v);
	d[at + 1] = uint8_t(v >> 8);
}


static void
Put32(std::vector<uint8_t>& d, size_t at, uint32_t v)
{
	Put16(d, at, uint16_t(v));
	Put16(d, at + 2, uint16_t(v >> 16));
}


static void
Add16(std::vector<uint8_t>& d, uint16_t v)
{
	Put16(d, d.size(), v);
}


// an STD: stk, istdBase, its name, its UPXs (each padded to even)
static std::vector<uint8_t>
Std(int stk, uint16_t base, const char* name, const std::vector<std::vector<uint8_t>>& upxs)
{
	std::vector<uint8_t> d;
	Add16(d, 0);								// sti, flags
	Add16(d, uint16_t(base << 4 | stk));		// stk, istdBase
	Add16(d, uint16_t(upxs.size()));			// cupx, istdNext 0
	Add16(d, 0);								// bchUpe
	Add16(d, 0);								// grfstd
	Add16(d, uint16_t(strlen(name)));
	for (const char* c = name; *c; c++)
		Add16(d, uint16_t(*c));
	Add16(d, 0);
	for (const auto& upx : upxs) {
		Add16(d, uint16_t(upx.size()));
		d.insert(d.end(), upx.begin(), upx.end());
		if (upx.size() % 2)
			d.push_back(0);
	}
	return d;
}


int
main()
{
	std::string text = "Title\rBody emph bold\r";
	const uint32_t textAt = 0x800;
	std::vector<uint8_t> word(textAt, 0);
	Put16(word, 0, 0xa5ec);
	Put16(word, 2, 0xc1);
	Put16(word, 0x0a, 0x0200);
	Put32(word, 0x4c, uint32_t(text.size()));
	word.insert(word.end(), text.begin(), text.end());

	// the CHPX FKP: "emph" (11-15) Emph, "bold" (16-20) bold 0x81
	size_t chpxPage = 0xa00;
	word.resize(chpxPage + 512, 0);
	uint32_t runs[] = {0, 11, 15, 16, 20, uint32_t(text.size())};
	for (int k = 0; k < 6; k++)
		Put32(word, chpxPage + size_t(k) * 4, textAt + runs[k]);
	std::vector<uint8_t> emph = {0x30, 0x4a, 2, 0};			// sprmCIstd 2
	std::vector<uint8_t> bold = {0x35, 0x08, 0x81};			// sprmCFBold: the opposite
	word[chpxPage + 0x1c0] = uint8_t(emph.size());
	memcpy(&word[chpxPage + 0x1c1], emph.data(), emph.size());
	word[chpxPage + 0x1d0] = uint8_t(bold.size());
	memcpy(&word[chpxPage + 0x1d1], bold.data(), bold.size());
	size_t rgb = chpxPage + 6 * 4;
	word[rgb + 1] = 0x1c0 / 2;
	word[rgb + 3] = 0x1d0 / 2;
	word[chpxPage + 511] = 5;

	// the PAPX FKP: "Title" Heading (istd 1), the rest Normal indented
	size_t papxPage = 0xc00;
	word.resize(papxPage + 512, 0);
	uint32_t paras[] = {0, 6, uint32_t(text.size())};
	for (int k = 0; k < 3; k++)
		Put32(word, papxPage + size_t(k) * 4, textAt + paras[k]);
	std::vector<uint8_t> heading = {1, 0};						// istd 1
	std::vector<uint8_t> normal = {0, 0, 0x0f, 0x84, 0xd0, 0x02};	// istd 0, left 720
	auto papx = [&](size_t at, const std::vector<uint8_t>& p) {
		size_t q = papxPage + at;
		if (p.size() % 2)
			word[q++] = uint8_t((p.size() + 1) / 2);
		else {
			word[q++] = 0;
			word[q++] = uint8_t(p.size() / 2);
		}
		memcpy(&word[q], p.data(), p.size());
	};
	papx(0x1a0, heading);
	papx(0x1b0, normal);
	word[papxPage + 3 * 4] = 0x1a0 / 2;
	word[papxPage + 3 * 4 + 13] = 0x1b0 / 2;
	word[papxPage + 511] = 2;

	// the table stream: the STSH, the CLX, the bin tables, the fonts
	std::vector<uint8_t> table;
	size_t stshAt = table.size();
	Add16(table, 18);
	for (uint16_t v : {3, 10, 1, 3, 3, 0, 0, 0, 0})	// cstd 3, cbSTDBaseInFile 10, ...
		Add16(table, v);
	auto addStd = [&](const std::vector<uint8_t>& std) {
		Add16(table, uint16_t(std.size()));
		table.insert(table.end(), std.begin(), std.end());
	};
	// Normal: 11 points (22 half points), font 1 (Arial)
	addStd(Std(1, 0x0fff, "Normal", {{0, 0}, {0x43, 0x4a, 22, 0, 0x4f, 0x4a, 1, 0}}));
	// Heading: centered; bold, 16 points
	addStd(Std(1, 0, "Heading", {{1, 0, 0x03, 0x24, 1}, {0x35, 0x08, 1, 0x43, 0x4a, 32, 0}}));
	// Emph: italic
	addStd(Std(2, 0x0fff, "Emph", {{0x36, 0x08, 1}}));
	uint32_t stshLength = uint32_t(table.size() - stshAt);
	size_t clxAt = table.size();
	table.push_back(2);
	Put32(table, table.size(), 16);
	Put32(table, table.size(), 0);
	Put32(table, table.size(), uint32_t(text.size()));
	Put16(table, table.size(), 0);
	Put32(table, table.size(), (textAt * 2) | 0x40000000);
	Put16(table, table.size(), 0);
	uint32_t clxLength = uint32_t(table.size() - clxAt);
	size_t chpxAt = table.size();
	Put32(table, table.size(), textAt);
	Put32(table, table.size(), textAt + uint32_t(text.size()));
	Put32(table, table.size(), uint32_t(chpxPage / 512));
	size_t papxAt = table.size();
	Put32(table, table.size(), textAt);
	Put32(table, table.size(), textAt + uint32_t(text.size()));
	Put32(table, table.size(), uint32_t(papxPage / 512));
	size_t fontsAt = table.size();
	Add16(table, 2);
	Add16(table, 0);
	for (const char* name : {"Times New Roman", "Arial"}) {
		std::vector<uint8_t> ffn(40, 0);
		for (const char* c = name; *c; c++)
			Add16(ffn, uint16_t(*c));
		Add16(ffn, 0);
		ffn[0] = uint8_t(ffn.size() - 1);
		table.insert(table.end(), ffn.begin(), ffn.end());
	}
	Put32(word, 0xa2, uint32_t(stshAt));
	Put32(word, 0xa6, stshLength);
	Put32(word, 0x1a2, uint32_t(clxAt));
	Put32(word, 0x1a6, clxLength);
	Put32(word, 0xfa, uint32_t(chpxAt));
	Put32(word, 0xfe, 12);
	Put32(word, 0x102, uint32_t(papxAt));
	Put32(word, 0x106, 12);
	Put32(word, 0x112, uint32_t(fontsAt));
	Put32(word, 0x116, uint32_t(table.size() - fontsAt));

	std::vector<WordParagraph> ps;
	std::string error;
	Check(ReadWord(MakeCompoundFile({{"WordDocument", word}, {"1Table", table}}), ps, error),
		"read " + error);
	Check(ps.size() == 2, "two paragraphs: " + std::to_string(ps.size()));
	if (ps.size() == 2) {
		// Title: the Heading style's, from Normal's font
		Check(ps[0].alignment == 1, "Title centered (Heading)");
		Check(ps[0].runs.size() == 1 && ps[0].runs[0].text == "Title" && ps[0].runs[0].bold
			&& ps[0].runs[0].size == 16 && ps[0].runs[0].font == "Arial",
			"Title bold, 16 points, Arial");
		const auto& r = ps[1].runs;
		Check(ps[1].alignment == 0 && ps[1].left == 36, "Body: Normal, indented");
		Check(r.size() == 4, "four runs: " + std::to_string(r.size()));
		if (r.size() == 4) {
			Check(r[0].text == "Body " && !r[0].bold && !r[0].italic && r[0].size == 11
				&& r[0].font == "Arial", "Body: Normal's 11 points, Arial");
			Check(r[1].text == "emph" && r[1].italic && !r[1].bold && r[1].size == 11,
				"emph: the Emph style's italic");
			Check(r[2].text == " " && !r[2].italic, "a plain space");
			Check(r[3].text == "bold" && r[3].bold && !r[3].italic,
				"bold: the opposite of Normal's (not bold)");
		}
	}
	printf("doc_wordstyles: ok %d, failed %d\n", sOk, sFailed);
	return sFailed ? 1 : 0;
}
