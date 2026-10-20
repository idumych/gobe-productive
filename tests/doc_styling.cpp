// Encoders (inverse of ApplyProperty), ApplyCharacterProperty,
// ApplyParagraphProperty, reference counts and undo.
#include <cstdio>
#include <set>
#include <cstring>
#include <map>
#include <vector>

#include "Document.h"
#include "Editing.h"
#include "Styles.h"
#include "Undo.h"
#include "DocSections.h"

using namespace gobe;

static int failures = 0;
static void Check(bool ok, const char* desc)
{
	if (!ok) {
		printf("FAIL: %s\n", desc);
		failures++;
	}
}

// Count how many paragraphs, runs, and child-styles refer to style X.
static int
CountRefs(const Document& doc, uint32_t style)
{
	int c = 0;
	for (const auto& [pi, wp] : doc.wordParts) {
		for (const WPParagraph& p : wp.paragraphs)
			if (p.parStyle == style) c++;
		for (const WPParagraph& p : wp.paragraphs)
			for (const WPStyleRun& r : p.runs)
				if (r.style == style) c++;
	}
	for (const Style& s : doc.styles.styles)
		if (s.links[0] == style) c++;
	return c;
}

// Save a snapshot of styles for comparison.
static struct {
	std::vector<Property> props;
	std::vector<Style> styles;
} snapStyles;

static void
SaveStyleSnap(const StyleMgr& sm)
{
	snapStyles.props = sm.properties;
	snapStyles.styles = sm.styles;
}

static bool
StylesEqual(const StyleMgr& sm)
{
	if (sm.properties.size() != snapStyles.props.size()) return false;
	for (size_t i = 0; i < sm.properties.size(); i++)
		if (sm.properties[i].type != snapStyles.props[i].type
			 || sm.properties[i].data != snapStyles.props[i].data)
			return false;
	if (sm.styles.size() != snapStyles.styles.size()) return false;
	for (size_t i = 0; i < sm.styles.size(); i++) {
		const Style& a = sm.styles[i], b = snapStyles.styles[i];
		if (a.props != b.props) return false;
		if (a.value4 != b.value4) return false;
		for (int j = 0; j < 3; j++)
			if (a.links[j] != b.links[j]) return false;
	}
	return true;
}

// Save a snapshot of word part runs.
static std::vector<WPPart> snapWPs;

static void
SaveWPSnap(const Document& doc)
{
	snapWPs.clear();
	for (const auto& [idx, wp] : doc.wordParts)
		snapWPs.push_back(wp);
}

static bool
WPEqual(const Document& doc)
{
	if (doc.wordParts.size() != snapWPs.size()) return false;
	for (size_t i = 0; i < snapWPs.size(); i++) {
		const WPPart& a = doc.wordParts[i].second, b = snapWPs[i];
		if (a.paragraphs.size() != b.paragraphs.size()) return false;
		for (size_t j = 0; j < a.paragraphs.size(); j++) {
			if (a.paragraphs[j].runs.size() != b.paragraphs[j].runs.size()) return false;
			for (size_t k = 0; k < a.paragraphs[j].runs.size(); k++) {
				if (a.paragraphs[j].runs[k].position != b.paragraphs[j].runs[k].position)
					return false;
				if (a.paragraphs[j].runs[k].style != b.paragraphs[j].runs[k].style)
					return false;
			}
		}
	}
	return true;
}


int main(int argc, char* argv[])
{
	int documents = 0;

	for (int i = 1; i < argc; i++) {
		if (strstr(argv[i], ".json")) continue;
		Document doc;
		std::string error;
		if (!LoadDocument(argv[i], doc, error)) continue;

		// (a) Encoder round-trip
		int ident = 0, differ = 0;
		for (const Property& prop : doc.styles.properties) {
			switch (prop.type) {
				case Sig("TSiz"):
				case Sig("TFon"):
				case Sig("TFac"):
				case Sig("TCol"):
				case Sig("PAlg"):
				case Sig("PLef"):
				case Sig("PInd"):
				case Sig("PRig"):
				case Sig("SpaB"):
				case Sig("SpaA"):
				case Sig("LSpa"): {
					TextStyle ts;
					if (ApplyProperty(prop, ts)) {
						Property enc;
						switch (prop.type) {
							case Sig("TSiz"):
								enc = TextSizeProperty(ts.size); break;
							case Sig("TFon"):
								enc = TextFontProperty(ts.family, ts.face); break;
							case Sig("TFac"):
								enc = TextFaceProperty(ts.faceFlags); break;
							case Sig("TCol"):
								enc = TextColorProperty(ts.color); break;
							case Sig("PAlg"):
								enc = AlignmentProperty(ts.alignment); break;
							case Sig("PLef"):
								enc = IndentProperty(Sig("PLef"), ts.leftIndent); break;
							case Sig("PInd"):
								enc = IndentProperty(Sig("PInd"), ts.firstIndent); break;
							case Sig("PRig"):
								enc = IndentProperty(Sig("PRig"), ts.rightIndent); break;
							case Sig("SpaB"):
								enc = SpacingProperty(Sig("SpaB"), ts.spaceBefore); break;
							case Sig("SpaA"):
								enc = SpacingProperty(Sig("SpaA"), ts.spaceAfter); break;
							case Sig("LSpa"):
								enc = LineSpacingProperty(ts.lineSpacing,
									ts.lineSpacingMultiple); break;
						}
						if (enc.data == prop.data)
							ident++;
						else {
							differ++;
							static std::set<uint32_t> shown;
							if (shown.insert(prop.type).second) {
								printf("encoder differs: %.4s orig", (const char*)&prop.type);
								for (uint8_t b : prop.data) printf(" %02x", b);
								printf(" | enc");
								for (uint8_t b : enc.data) printf(" %02x", b);
								printf("\n");
							}
						}
					}
					break;
				}
			}
		}
		printf("properties re-encoded identically %d, differ %d\n", ident, differ);
		Check(differ == 0, "properties re-encoded identically");

		// Find first WP part with a paragraph >= 10 editable chars.
		// Store both vector index (for direct access) and part ID (for editing API).
		int wpVecIdx = -1;
		size_t wpPartID = 0;
		size_t k = 0;
		for (size_t vi = 0; vi < doc.wordParts.size(); vi++) {
			const auto& [pi, wp] = doc.wordParts[vi];
			for (size_t p = 0; p < wp.paragraphs.size(); p++) {
				size_t el = EditableLength(wp.paragraphs[p]);
				if (el >= 10) {
					wpVecIdx = int(vi);
					wpPartID = pi;
					k = p;
					goto foundWP;
				}
			}
		}
	foundWP:
		if (wpVecIdx < 0) continue;	// not a "tested" doc
		documents++;

		WPPart& part = const_cast<WPPart&>(doc.wordParts[size_t(wpVecIdx)].second);

		// Save pre-(b) state for undo test later.
		SaveStyleSnap(doc.styles);
		SaveWPSnap(doc);
		size_t textLen = EditableLength(part.paragraphs[k]);
		struct PreStyle { std::string family; float size; uint32_t faceFlags; Color color; };
		std::vector<PreStyle> preStyles;
		for (size_t off = 0; off < textLen; off++) {
			size_t ri = 0;
			for (size_t j = 0; j < part.paragraphs[k].runs.size(); j++)
				if (part.paragraphs[k].runs[j].position <= off)
					ri = j;
			const WPStyleRun& run = part.paragraphs[k].runs[ri];
			TextStyle ts = ResolveTextStyle(doc.styles, run.style);
			preStyles.push_back({ts.family, ts.size, ts.faceFlags, ts.color});
		}
		// (c) Ref-counts BEFORE styling.
		std::map<uint32_t, int> countBefore;
		std::map<uint32_t, int> v4Before;
		for (uint32_t si = 0; si < doc.styles.styles.size(); si++) {
			v4Before[si] = int(doc.styles.styles[si].value4);
			countBefore[si] = CountRefs(doc, si);
		}
		const uint32_t stylesBefore = uint32_t(doc.styles.styles.size());

		// Apply bold to [2, 6) — bold bit from TDocView is 0x2.
		ApplyCharacterProperty(doc, size_t(wpPartID), {k, 2}, {k, 6},
			TextFaceProperty(2));

		// (b) check: offsets 2..5 have bold; 0..1, 6..end unchanged.
		for (size_t off = 0; off < textLen; off++) {
			size_t ri = 0;
			for (size_t j = 0; j < part.paragraphs[k].runs.size(); j++)
				if (part.paragraphs[k].runs[j].position <= off)
					ri = j;
			TextStyle ts = ResolveTextStyle(doc.styles, part.paragraphs[k].runs[ri].style);
			if (off >= 2 && off < 6) {
				Check((ts.faceFlags & 0x2) != 0,
					("bold at offset " + std::to_string(off)).c_str());
			} else {
				Check(ts.faceFlags == preStyles[off].faceFlags,
					("no faceFlags change at offset " + std::to_string(off)).c_str());
				Check(ts.family == preStyles[off].family,
					("family same at offset " + std::to_string(off)).c_str());
				Check(ts.size == preStyles[off].size,
					("size same at offset " + std::to_string(off)).c_str());
				Check(ts.color == preStyles[off].color,
					("color same at offset " + std::to_string(off)).c_str());
			}
		}

		// (c) Ref-counts AFTER.
		std::map<uint32_t, int> countAfter;
		std::map<uint32_t, int> v4After;
		for (uint32_t si = 0; si < doc.styles.styles.size(); si++) {
			v4After[si] = int(doc.styles.styles[si].value4);
			countAfter[si] = CountRefs(doc, si);
		}
		std::map<uint32_t, int> allStyles;
		for (auto& kv : countBefore) allStyles[kv.first] = 0;
		for (auto& kv : countAfter)  allStyles[kv.first] = 0;
		for (auto& kv : allStyles) {
			int s = kv.first;
			bool isNew = uint32_t(s) >= stylesBefore;
			// a new style: one reference for the manager plus its users
			int v4d = isNew ? v4After[s] - 1 : v4After[s] - v4Before[s];
			int cd  = isNew ? countAfter[s] : countAfter[s] - countBefore[s];
			Check(v4d == cd,
				("refcount delta for style " + std::to_string(s) + " v4 " + std::to_string(v4Before[s]) + "->" + std::to_string(v4After[s]) + " uses " + std::to_string(countBefore[s]) + "->" + std::to_string(countAfter[s])).c_str());
		}

		// (d) Paragraph property: alignment = 2 (center) on paragraph k.
		std::vector<int> alignBefore;
		for (const WPParagraph& par : part.paragraphs)
			alignBefore.push_back(ResolveTextStyle(doc.styles, par.parStyle).alignment);
		ApplyParagraphProperty(doc, size_t(wpPartID), k, k, AlignmentProperty(2));
		TextStyle tsPar = ResolveTextStyle(doc.styles, part.paragraphs[k].parStyle);
		Check(tsPar.alignment == 2, "paragraph k alignment = 2");

		// other paragraphs unchanged — check alignment.
		for (size_t i = 0; i < part.paragraphs.size(); i++) {
			if (i == k) continue;
			TextStyle t = ResolveTextStyle(doc.styles, part.paragraphs[i].parStyle);
			Check(t.alignment == alignBefore[i],
				("paragraph " + std::to_string(i) + " alignment unchanged").c_str());
		}

		// (c) refcount after (d) — same rule.
		countAfter.clear();
		v4After.clear();
		for (uint32_t si = 0; si < doc.styles.styles.size(); si++) {
			v4After[si] = int(doc.styles.styles[si].value4);
			countAfter[si] = CountRefs(doc, si);
		}
		for (auto& kv : allStyles) {
			int s = kv.first;
			bool isNew = uint32_t(s) >= stylesBefore;
			// a new style: one reference for the manager plus its users
			int v4d = isNew ? v4After[s] - 1 : v4After[s] - v4Before[s];
			int cd  = isNew ? countAfter[s] : countAfter[s] - countBefore[s];
			Check(v4d == cd,
				("refcount delta after align for style " + std::to_string(s) + (isNew ? " new" : "") + " v4 " + std::to_string(v4Before[s]) + "->" + std::to_string(v4After[s]) + " uses " + std::to_string(countBefore[s]) + "->" + std::to_string(countAfter[s])).c_str());
		}

		// (e) Undo: restore to pre-(b) state, redo edits in Begin/Undo, undo.
		// Restore to pre-(b).
		doc.styles.properties = snapStyles.props;
		doc.styles.styles = snapStyles.styles;
		doc.stylesChanged = false;
		{
			size_t si = 0;
			for (auto& [pi, wp] : doc.wordParts)
				wp = snapWPs[si++];
		}
		{
			UndoStack undo;
			UndoStack::ViewState dummy;
			undo.Begin(doc, size_t(wpPartID), "Style", {});
			ApplyCharacterProperty(doc, size_t(wpPartID), {k, 2}, {k, 6},
				TextFaceProperty(2));
			ApplyParagraphProperty(doc, size_t(wpPartID), k, k, AlignmentProperty(2));
			undo.Undo(doc, dummy);
		}

		Check(StylesEqual(doc.styles), "undo styles equal");
		Check(WPEqual(doc), "undo runs equal");
		Check(!doc.stylesChanged, "undo stylesChanged false");

	}

	printf(failures ? "styling ok %d, failed %d\n" : "styling ok %d, failed 0\n",
		documents, failures);
	return failures != 0;
}
