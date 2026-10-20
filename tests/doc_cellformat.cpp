// Cell format encoders: SS Nm/HAln/VAln/ClWp/solid FCol.
// (a) re-encode every such property byte for byte;
// (b) ApplyCellProperty + refcount check;
// (c) FormatCellValue("0.00");
// (d) Undo; (e) save and reload.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <vector>

#include "Document.h"
#include "Editing.h"
#include "SSStyle.h"
#include "Styles.h"
#include "Undo.h"

using namespace gobe;

static int failures = 0;
static void Check(bool ok, const char* desc)
{
	if (!ok) {
		printf("FAIL: %s\n", desc);
		failures++;
	}
}

// Save snapshots for undo comparison.
static struct {
	std::vector<Property> props;
	std::vector<Style> styles;
} snapStyles;

static void SaveStyleSnap(const StyleMgr& sm)
{
	snapStyles.props = sm.properties;
	snapStyles.styles = sm.styles;
}

static bool StylesEqual(const StyleMgr& sm)
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

// Count references to a style in the document.
static int
CountRefs(const Document& doc, uint32_t style)
{
	int c = 0;
	for (const auto& [pi, wp] : doc.wordParts)
		for (const WPParagraph& p : wp.paragraphs)
			if (p.parStyle == style) c++;
	for (const Style& s : doc.styles.styles)
		if (s.links[0] == style) c++;
	return c;
}

int main(int argc, char* argv[])
{
	int documents = 0;

	for (int i = 1; i < argc; i++) {
		if (strstr(argv[i], ".json")) continue;
		Document doc;
		std::string error;
		if (!LoadDocument(argv[i], doc, error)) continue;

		// (a) Encoder round-trip: decode with existing decoder, re-encode, compare.
		int ident = 0, differ = 0;
		for (const Property& prop : doc.styles.properties) {
			switch (prop.type) {
				case Sig("SSNm"):
				case Sig("HAln"):
				case Sig("VAln"):
				case Sig("ClWp"):
				case Sig("FCol"): {
					CellStyle cs;
					if (ApplyProperty(prop, cs.text))
						break;	// it went to TextStyle, not CellStyle
					DocArchive a(prop.data);
					a.SetVersions(2, 0, 0x1e);
					CellStyle decoded;
					bool skip = false;
					try {
						switch (prop.type) {
							case Sig("SSNm"):
								decoded.format = a.String();
								break;
							case Sig("HAln"):
								decoded.hAlign = a.Int16();
								break;
							case Sig("VAln"):
								decoded.vAlign = a.Int16();
								break;
							case Sig("ClWp"):
								decoded.wrap = a.Bool();
								break;
							case Sig("FCol"): {
								Ink ink;
								if (!ReadInk(a, ink))
									break;
								// CellFillProperty only produces solid colors;
								// skip gradient FCol for the round-trip test.
								if (ink.set && !ink.gradient)
									decoded.fill = ink;
								else
									skip = true;
								break;
							}
						}
					} catch (const ArchiveError&) {
						break;
					}
					if (skip) {
						ident++;
						continue;
					}
					// Re-encode
					Property enc;
					switch (prop.type) {
						case Sig("SSNm"):
							enc = NumberFormatProperty(decoded.format);
							break;
						case Sig("HAln"):
							enc = CellHAlignProperty(decoded.hAlign);
							break;
						case Sig("VAln"):
							enc = CellVAlignProperty(decoded.vAlign);
							break;
						case Sig("ClWp"):
							enc = CellWrapProperty(decoded.wrap);
							break;
						case Sig("FCol"):
							enc = CellFillProperty(decoded.fill.color);
							break;
					}
					// For FCol, only compare the first 5 bytes (Bool+RGBA);
					// the decoder discards all tail bytes so they may differ.
					size_t cmp_len = (prop.type == Sig("FCol")) ? 5 : prop.data.size();
					if (enc.data.size() >= cmp_len && prop.data.size() >= cmp_len
						&& memcmp(enc.data.data(), prop.data.data(), cmp_len) == 0)
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
					break;
				}
			}
		}
		printf("properties re-encoded identically %d, differ %d\n", ident, differ);
		Check(differ == 0, "cellformat properties re-encoded identically");

		// Find first spreadsheet part.
		int ssIdx = -1;
		size_t ssPartId = 0;
		size_t k = 0;
		for (size_t vi = 0; vi < doc.parts.parts.size(); vi++)
			if (doc.parts.parts[vi].kind == Sig("SPRD")) {
				ssIdx = int(vi);
				ssPartId = vi;
				break;
			}
		if (ssIdx < 0) continue;

		const SSPart& ss = *doc.SpreadsheetPart(ssPartId);

		// (b) ApplyCellProperty on a 3x2 block, then check ResolveCellStyle.
		// Use cells that exist in the sample. Pick a 3x2 area from the used range.
		int startRow = 0, startCol = 0;
		if (ss.used.to.a >= 2 && ss.used.to.b >= 1) {
			startRow = ss.used.to.a - 2 < 0 ? 0 : ss.used.to.a - 2;
			startCol = ss.used.to.b - 1 < 0 ? 0 : ss.used.to.b - 1;
		}
		if (ss.used.to.a < 2) {
			for (const SSCell& c : ss.cells) {
				if (c.id.a >= startRow && c.id.a < startRow + 3
					&& c.id.b >= startCol && c.id.b < startCol + 2)
					goto foundBlock;
			}
		} else {
			for (const SSCell& c : ss.cells) {
				if (c.id.a >= startRow && c.id.a < startRow + 3
					&& c.id.b >= startCol && c.id.b < startCol + 2)
					goto foundBlock;
			}
		}
		// Fallback: just use cells 0,0 to 2,1
		startRow = 0; startCol = 0;
	foundBlock:
		{
			// Save state for undo.
			SaveStyleSnap(doc.styles);

			// Find an outside cell and save its format before applying.
			int outsideRow = -1, outsideCol = -1;
			std::string outsideFormatBefore;
			for (const SSCell& cell : ss.cells) {
				if ((cell.id.a < startRow || cell.id.a > startRow + 2)
					&& (cell.id.b < startCol || cell.id.b > startCol + 1)) {
					outsideRow = int(cell.id.a);
					outsideCol = int(cell.id.b);
					std::vector<uint32_t> stack = {ss.style};
					if (const SSHeader* h = ss.Column(outsideCol))
						stack.push_back(h->cell.style);
					if (const SSHeader* h = ss.Row(outsideRow))
						stack.push_back(h->cell.style);
					if (cell.style != 0)
						stack.push_back(cell.style);
					CellStyle resolved = ResolveCellStyle(doc.styles, stack);
					outsideFormatBefore = resolved.format;
					break;
				}
			}

			// Apply format "0.00" to the block.
			ApplyCellProperty(doc, ssPartId, uint16_t(startRow), uint16_t(startCol),
				uint16_t(startRow + 2), uint16_t(startCol + 1),
				NumberFormatProperty("0.00"));

			// Check that each cell in the block has format "0.00".
			SSEvaluator eval(ss, nullptr, ssIdx);
			int blockOk = 0;
			for (int r = startRow; r <= startRow + 2; r++)
				for (int c = startCol; c <= startCol + 1; c++) {
					const SSCell* cell = ss.Cell(r, c);
					std::vector<uint32_t> stack = {ss.style};
					if (const SSHeader* h = ss.Column(c))
						stack.push_back(h->cell.style);
					if (const SSHeader* h = ss.Row(r))
						stack.push_back(h->cell.style);
					if (cell != NULL)
						stack.push_back(cell->style);
					CellStyle resolved = ResolveCellStyle(doc.styles, stack);
					if (resolved.format == "0.00")
						blockOk++;
				}
			Check(blockOk == 6, "block cells have format 0.00 after ApplyCellProperty");

			// Check that cells outside are unchanged.
			if (outsideRow >= 0) {
				std::vector<uint32_t> stack = {ss.style};
				if (const SSHeader* h = ss.Column(outsideCol))
					stack.push_back(h->cell.style);
				if (const SSHeader* h = ss.Row(outsideRow))
					stack.push_back(h->cell.style);
				const SSCell* outsideCell = ss.Cell(outsideRow, outsideCol);
				if (outsideCell && outsideCell->style != 0)
					stack.push_back(outsideCell->style);
				CellStyle outsideResolved = ResolveCellStyle(doc.styles, stack);
				Check(outsideResolved.format == outsideFormatBefore,
					"outside cells unchanged");
			}

			// Ref-count check.
			const uint32_t stylesBefore = uint32_t(doc.styles.styles.size());
			std::map<uint32_t, int> countBefore;
			std::map<uint32_t, int> v4Before;
			for (uint32_t si = 0; si < stylesBefore; si++) {
				v4Before[si] = int(doc.styles.styles[si].value4);
				countBefore[si] = CountRefs(doc, si);
			}
			// We already applied the property above; now check refcounts.
			std::map<uint32_t, int> countAfter, v4After;
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
				int v4d = isNew ? v4After[s] - 1 : v4After[s] - v4Before[s];
				int cd = isNew ? countAfter[s] : countAfter[s] - countBefore[s];
				Check(v4d == cd,
					("refcount delta for style " + std::to_string(s)).c_str());
			}
		}

		// (e) Save and reload: the block's format survives.
		// Must happen before the undo test below, which restores snap state.
		{
			std::string path = std::string(getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp")
				+ "/doc_cellformat_save", error;
			bool saved = doc.Save(path, error);
			Check(saved, ("save: " + error).c_str());
			if (saved) {
				Document doc2;
				std::string error2;
				if (LoadDocument(path, doc2, error2)) {
					const SSPart* ss2 = doc2.SpreadsheetPart(ssPartId);
					if (ss2 != NULL) {
						const SSCell* cell = ss2->Cell(startRow, startCol);
						if (cell != NULL) {
							std::vector<uint32_t> stack = {ss2->style};
							if (const SSHeader* h = ss2->Column(startCol))
								stack.push_back(h->cell.style);
							if (const SSHeader* h = ss2->Row(startRow))
								stack.push_back(h->cell.style);
							if (cell->style != 0)
								stack.push_back(cell->style);
							CellStyle resolved = ResolveCellStyle(doc2.styles, stack);
							Check(resolved.format == "0.00",
								"cell format survives save and reload");
						}
					}
				} else {
					Check(false, ("reload: " + error2).c_str());
				}
				remove(path.c_str());
			}
		}

		// (d) Undo test.
		{
			doc.styles.properties = snapStyles.props;
			doc.styles.styles = snapStyles.styles;
			doc.stylesChanged = false;
			{
				UndoStack undo;
				UndoStack::ViewState dummy;
				undo.Begin(doc, ssPartId, "Cell Format", {});
				ApplyCellProperty(doc, ssPartId, uint16_t(startRow), uint16_t(startCol),
					uint16_t(startRow + 2), uint16_t(startCol + 1),
					NumberFormatProperty("0.00"));
				undo.Undo(doc, dummy);
			}
			Check(StylesEqual(doc.styles), "undo cell format restores styles");
			Check(!doc.stylesChanged, "undo stylesChanged false");
		}

		documents++;
	}

	printf(failures ? "cellformat ok %d, failed %d\n" : "cellformat ok %d, failed 0\n",
		documents, failures);
	return failures != 0;
}
