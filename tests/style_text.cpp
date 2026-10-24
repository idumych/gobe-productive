// PropertyTypeName and PropertyDataText: properties made with the encoders
// and by hand, against the original's strings (STR# 5, 7, 11, 13, 1304,
// 1305, as `python3 tools/rsrc.py`-style decoding of the binary shows them)
// and the formats in the decompiled GetDataStr methods; then every
// property of the documents' paragraph and basic styles has a name.
#include <cstdio>
#include <filesystem>
#include <string>

#include "DocWriter.h"
#include "Document.h"
#include "ResourceFile.h"
#include "StyleText.h"
#include "Styles.h"

using namespace gobe;
namespace fs = std::filesystem;

static int failures = 0;
static int checks = 0;

static void
Check(const std::string& got, const std::string& expected, const std::string& what)
{
	checks++;
	if (got != expected) {
		printf("FAIL: %s\n  got:      \"%s\"\n  expected: \"%s\"\n", what.c_str(), got.c_str(),
			expected.c_str());
		failures++;
	}
}


static Property
Make(const char (&type)[5], const DocWriter& w)
{
	Property p;
	p.type = Sig(type);
	p.data = w.Data();
	return p;
}


int
main(int argc, char** argv)
{
	if (argc < 2) {
		printf("style_text: no documents, ok 0\n");
		return 1;
	}
	fs::path install = fs::path(argv[1]).parent_path().parent_path();
	if (!fs::exists(install / "Gobe Productive"))
		install = fs::path(argv[argc - 1]).parent_path().parent_path();
	ResourceFile resources;
	if (!resources.Load((install / "Gobe Productive").string())) {
		printf("FAIL: the binary's resources don't load\nok 0\n");
		return 1;
	}
	StringLookup strings = [&](int32_t id, int index) {
		return IndexString(resources, id, index);
	};
	auto name = [&](const Property& p) { return PropertyTypeName(p, strings); };
	auto value = [&](const Property& p, int unit = 0) { return PropertyDataText(p, strings, unit); };

	// TPropShort: "%d"
	Check(name(TextSizeProperty(12)), "Text Size", "TSiz name");
	Check(value(TextSizeProperty(12)), "12", "TSiz 12");
	// TPropTextFont: family, a space and the style if there is one
	Check(name(TextFontProperty("Dutch801 Rm BT", "Roman")), "Text Font", "TFon name");
	Check(value(TextFontProperty("Dutch801 Rm BT", "Roman")), "Dutch801 Rm BT Roman", "TFon");
	Check(value(TextFontProperty("Swiss911 XCm BT", "")), "Swiss911 XCm BT", "TFon, no style");
	// TPenInk::Description
	Check(name(TextColorProperty({255, 0, 128, 255})), "Text Color", "TCol name");
	Check(value(TextColorProperty({255, 0, 128, 255})), "R:255 G:0 B:128", "TCol solid");
	{
		// a pattern of two colors, then one of the same color twice
		DocWriter w;
		w.Bool(true);
		const uint8_t colors[8] = {0, 0, 0, 255, 255, 255, 255, 255};
		w.Bytes(colors, 8);
		const uint8_t pattern[8] = {0xaa, 0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa, 0x55};
		w.Bytes(pattern, 8);
		Check(value(Make("FCol", w)), "Color pattern", "FCol pattern");
		Check(name(Make("FCol", w)), "Fill Color", "FCol name");
		DocWriter same;
		same.Bool(true);
		const uint8_t twice[8] = {10, 20, 30, 255, 10, 20, 30, 255};
		same.Bytes(twice, 8);
		same.Bytes(pattern, 8);
		Check(value(Make("PCol", same)), "R:10 G:20 B:30", "PCol, both colors the same");
		DocWriter fill;
		fill.Bool(false);
		fill.UInt32(Sig("FGRD"));
		fill.UInt32(0);
		Check(value(Make("FCol", fill)), "Color fill", "FCol fill");
	}
	// TPropTextFace: the first face bit's name (STR# 11 from 7, bit 1 first;
	// none: 14), "not " when it's off
	Check(name(TextFaceProperty(2)), "Text Style", "TFac name");
	Check(value(TextFaceProperty(2)), "Bold", "TFac 2");
	Check(value(TextFaceProperty(4 | 8)), "Italic", "TFac 12");
	Check(value(TextFaceProperty(0)), "Subscript", "TFac 0");
	{
		DocWriter w;
		w.UInt32(8);
		w.Bool(false);
		Check(value(Make("TFac", w)), "not Underline", "TFac 8 off");
	}
	// TPropParAlign: 0 Left, 1 Center, 2 Right, 3 Full (STR# 1304 1, 3, 2, 4)
	Check(name(AlignmentProperty(1)), "Align Paragraph", "PAlg name");
	Check(value(AlignmentProperty(0)), "Left", "PAlg 0");
	Check(value(AlignmentProperty(1)), "Center", "PAlg 1");
	Check(value(AlignmentProperty(2)), "Right", "PAlg 2");
	Check(value(AlignmentProperty(3)), "Full", "PAlg 3");
	// TPropIndent: TMeasure GetText(unit, 3): "%.2f", zeros dropped, unit
	// python3 -c "print(36*0.03527785)"  -> 1.270 cm
	Check(name(IndentProperty(Sig("PLef"), 36)), "Left Indent", "PLef name");
	Check(name(IndentProperty(Sig("PRig"), 36)), "Right Indent", "PRig name");
	Check(name(IndentProperty(Sig("PInd"), 36)), "First Line Indent", "PInd name");
	Check(value(IndentProperty(Sig("PLef"), 36), 0), "0.5 in", "PLef 36pt in inches");
	Check(value(IndentProperty(Sig("PLef"), 36), 4), "36 pt", "PLef 36pt in points");
	Check(value(IndentProperty(Sig("PInd"), 36), 1), "1.27 cm", "PInd 36pt in cm");
	// TParSpaceProperty: "%.12g lines", or a length
	Check(name(SpacingProperty(Sig("SpaB"), 1.5f, true)), "Space Before", "SpaB name");
	Check(name(SpacingProperty(Sig("SpaA"), 1.5f, true)), "Space After", "SpaA name");
	Check(value(SpacingProperty(Sig("SpaB"), 1.5f, true)), "1.5 lines", "SpaB 1.5 lines");
	Check(value(SpacingProperty(Sig("SpaA"), 6, false), 4), "6 pt", "SpaA 6 points");
	Check(name(LineSpacingProperty(2, true)), "Line Spacing", "LSpa name");
	Check(value(LineSpacingProperty(2, true)), "2 lines", "LSpa 2 lines");
	// TPropParTab: type, fill, " Tab"; the position as a length
	TabStop tab;
	tab.position = 72;
	tab.type = 1;
	tab.fill = 1;
	Check(name(TabProperty(tab)), "Center Pen Fill Tab", "PTab center, pen fill");
	Check(value(TabProperty(tab)), "1 in", "PTab at 72pt");
	tab.type = 3;
	tab.fill = 2;
	Check(name(TabProperty(tab)), "Align Char Fill Tab", "PTab align, char fill");
	tab.type = 2;
	tab.fill = 0;
	Check(name(TabProperty(tab)), "Right Tab", "PTab right");
	// TPropPenSize: 0 a hairline, else TPropFloat
	{
		DocWriter zero, two;
		zero.Float(0);
		two.Float(2.5f);
		Check(name(Make("PENz", zero)), "Pen Size", "PENz name");
		Check(value(Make("PENz", zero)), "Hairline", "PENz 0");
		Check(value(Make("PENz", two)), "2.5", "PENz 2.5");
	}
	// TPropTransparent: 0, 1, else "%d Percent" of round(value x 100)
	{
		DocWriter zero, one, part;
		zero.Float(0);
		one.Float(1);
		part.Float(0.375f);
		Check(name(Make("FTra", zero)), "Fill Transparency", "FTra name");
		Check(value(Make("FTra", zero)), "Transparent", "FTra 0");
		Check(value(Make("PTra", one)), "Opaque", "PTra 1");
		Check(value(Make("TTra", part)), "38 Percent", "TTra 0.375");
	}
	// TPropParLabel: STR# 1305; TPropBool: On / Off; TPropArrows
	{
		DocWriter label;
		label.UInt16(2);
		label.String("");
		label.String("");
		Check(name(Make("PLab", label)), "Paragraph Label", "PLab name");
		Check(value(Make("PLab", label)), "Bullet", "PLab 2");
		DocWriter on;
		on.Bool(true);
		Check(name(Make("PNxt", on)), "Keep With Next", "PNxt name");
		Check(value(Make("PNxt", on)), "On", "PNxt on");
		DocWriter arrows;
		arrows.UInt16(3);
		Check(value(Make("Arrw", arrows)), "Both Ends", "Arrw 3");
	}

	// TextSelectionStyles on made-up styles: 0 "Default" (basic), 1 "A"
	// based on it, 2 "B" (paragraph styles); 3 an unnamed paragraph style
	// based on A, 4 one based on A with B applied (links[1]); 5 and 6 run
	// styles on 3 and 4, 7 a run style on 3 with a property of its own
	{
		StyleMgr m;
		m.properties.push_back(TextSizeProperty(14));
		auto add = [&](const char* name, uint32_t type, uint32_t base, uint32_t applied,
				bool props) {
			Style st;
			st.name = name;
			st.value10 = type;
			st.links[0] = base;
			st.links[1] = applied;
			st.links[2] = 0xffffffff;
			if (props)
				st.props.push_back(0);
			m.styles.push_back(st);
		};
		const uint32_t kNo = 0xffffffff;
		add("Default", Sig("Basi"), kNo, kNo, false);
		add("A", Sig("Para"), 0, kNo, false);
		add("B", Sig("Para"), 0, kNo, false);
		add("", Sig("Para"), 1, kNo, false);
		add("", Sig("Para"), 1, 2, false);
		add("", Sig("Basi"), 3, kNo, false);
		add("", Sig("Basi"), 4, kNo, false);
		add("", Sig("Basi"), 3, kNo, true);
		WPPart part;
		WPParagraph p0, p1;
		p0.text = "one two\n";
		p0.parStyle = 3;
		p0.runs = {{0, 5}, {4, 7}};
		p1.text = "three\n";
		p1.parStyle = 4;
		p1.runs = {{0, 6}};
		part.paragraphs = {p0, p1};
		auto at = [](size_t paragraph, size_t offset) {
			TextPosition position;
			position.paragraph = paragraph;
			position.offset = offset;
			return position;
		};
		auto active = [&](TextPosition a, TextPosition b) {
			return TextSelectionStyles(m, part, a, b).active;
		};
		// the last named style it comes from (AddStyleNames: Default, A)
		Check(active(at(0, 1), at(0, 1)), "A", "caret in a run of A");
		// a run style with a property of its own: "+"
		Check(active(at(0, 5), at(0, 5)), "A+", "caret in a run with a property");
		Check(std::to_string(TextSelectionStyles(m, part, at(0, 5), at(0, 5)).incremental.size()),
			"1", "the run's own property is incremental");
		// B applied (links[1]) to the second paragraph
		SelectionStyles second = TextSelectionStyles(m, part, at(1, 2), at(1, 2));
		Check(second.active, "B", "caret where B is applied");
		Check(std::to_string(second.applied.size()) + (second.applied.empty() ? ""
			: second.applied[0].first + (second.applied[0].second ? " part" : " all")),
			"1B all", "B applied to all of it");
		// across both paragraphs: B on part of it -- "Default"; the runs'
		// properties differ -- not that it matters then
		SelectionStyles both = TextSelectionStyles(m, part, at(0, 0), at(1, 3));
		Check(both.active, "Default", "B applied to part of the selection");
		Check(std::to_string(both.applied.size()) + (both.applied.empty() ? ""
			: both.applied[0].first + (both.applied[0].second ? " part" : " all")),
			"1B part", "B applied to part of the selection");
		// the first run only, then both runs of the first paragraph (one
		// with a property, so the text's properties differ: "+")
		Check(active(at(0, 0), at(0, 3)), "A", "the first run selected");
		Check(active(at(0, 0), at(0, 6)), "A+", "both runs selected");
	}

	// the documents: each paragraph or basic style's own and inherited
	// properties of the text part's and the shared kinds have a name
	int named = 0;
	for (int i = 1; i < argc; i++) {
		Document doc;
		std::string error;
		if (!LoadDocument(argv[i], doc, error))
			continue;
		for (const Style& style : doc.styles.styles) {
			if (style.value10 != Sig("Para") && style.value10 != Sig("Basi"))
				continue;
			for (const std::vector<uint32_t>* set : {&style.props, &style.props2}) {
				for (uint32_t p : *set) {
					if (p >= doc.styles.properties.size())
						continue;
					const Property& property = doc.styles.properties[p];
					// an add-on's: the frames' background, the chart's
					if (property.type == Sig("BCol") || property.type == Sig("BTra")
							|| property.type == Sig("chSC") || property.type == Sig("chSM")
							|| property.type == Sig("chSS"))
						continue;
					checks++;
					named++;
					if (name(property).empty()) {
						printf("FAIL: %s: no name for %s\n", argv[i],
							FourCCString(property.type).c_str());
						failures++;
					} else if (!PropertyIsInk(property) && value(property).empty()) {
						printf("FAIL: %s: no value for %s\n", argv[i],
							FourCCString(property.type).c_str());
						failures++;
					}
				}
			}
		}
	}
	if (named == 0) {
		printf("FAIL: no style properties in the documents\n");
		failures++;
	}

	printf(failures ? "%d failures\n" : "style_text: ok %d\n", failures ? failures : checks);
	return failures != 0;
}
