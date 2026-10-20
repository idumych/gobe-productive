// Read every spreadsheet part and check it ends where its tail begins.
#include <cstdio>
#include <map>

#include "Document.h"
#include "SSPart.h"
#include "PartTail.h"
#include "SSEval.h"
#include "SSStyle.h"
#include "ResourceFile.h"

using namespace gobe;

int main(int argc, char** argv)
{
	int ok = 0, bad = 0;
	std::map<std::string, int> kinds;
	for (int i = 1; i < argc; i++) {
		Document doc;
		std::string error;
		if (!LoadDocument(argv[i], doc, error))
			continue;
		DocArchive versions({});
		versions.SetVersions(doc.file.header.version.values[0],
			doc.file.header.version.values[3], doc.file.header.version.values[2]);
		for (size_t p = 0; p < doc.parts.parts.size(); p++) {
			const PartRecord& rec = doc.parts.parts[p];
			if (rec.kind != Sig("SPRD"))
				continue;
			DocArchive pa(rec.data);
			pa.CopyVersions(versions);
			SSPart ss;
			try {
				ReadSSPart(pa, ss);
				if (doc.tailRead[p] && pa.Position() == doc.tails[p].offset) {
					ok++;
					for (auto& c : ss.cells)
						kinds[FourCCString(c.kind)]++;
				} else {
					printf("%s part %zu: ends at %zu, tail at %zu\n", argv[i], p,
						pa.Position(), doc.tails[p].offset);
					bad++;
				}
			} catch (const ArchiveError& x) {
				printf("%s part %zu: %s at %zu\n", argv[i], p, x.what(), x.Position());
				bad++;
			}
		}
	}
	// every formula evaluates without an error value, and a few known results
	int formulas = 0, errors = 0;
	struct Known { const char* doc; size_t part; uint16_t row, column; const char* text; };
	const Known known[] = {
		{"Invoice", 0, 34, 6, "$417.54"},			// G35 grand total
		{"Invoice", 0, 11, 2, "Hyper Cadium Cell"},	// C12 VLOOKUP in another part
		{"LoanCalculator", 0, 8, 8, "$1496.93"},	// I9 PMT
		{"StockPrice", 0, 29, 3, "$800.00"},		// D30 ([Red] section)
	};
	for (int i = 1; i < argc; i++) {
		Document doc;
		std::string error;
		if (!LoadDocument(argv[i], doc, error))
			continue;
		SSBook book(doc);
		std::string name = argv[i];
		name = name.substr(name.find_last_of('/') + 1);
		for (const auto& [p, ss] : doc.spreadsheetParts) {
			SSEvaluator& eval = *book.For(p);
			for (const SSCell& c : ss.cells) {
				if (c.kind != Sig("Fmla"))
					continue;
				formulas++;
				if (eval.Value(c.id.a, c.id.b).type == SSValue::kError) {
					errors++;
					printf("%s part %zu %s: =%s is an error\n", argv[i], p,
						CellName(c.id).c_str(), FormulaText(c.formula, c.id).c_str());
				}
			}
			for (const Known& k : known) {
				if (name != k.doc || p != k.part)
					continue;
				const SSCell* c = ss.Cell(k.row, k.column);
				std::vector<uint32_t> stack = {ss.style};
				if (const SSHeader* h = ss.Column(k.column))
					stack.push_back(h->cell.style);
				if (const SSHeader* h = ss.Row(k.row))
					stack.push_back(h->cell.style);
				if (c != NULL)
					stack.push_back(c->style);
				std::string text = FormatCellValue(eval.Value(k.row, k.column),
					ResolveCellStyle(doc.styles, stack).format);
				if (text != k.text) {
					printf("%s %s: '%s', expected '%s'\n", k.doc,
						CellName({k.row, k.column}).c_str(), text.c_str(), k.text);
					bad++;
				}
			}
		}
	}
	printf("formulas %d, errors %d\n", formulas, errors);
	bad += errors;
	printf("spreadsheet parts ok %d, failed %d; cells:", ok, bad);
	for (auto& [k, n] : kinds)
		printf(" %s=%d", k.c_str(), n);
	printf("\n");
	return bad ? 1 : 0;
}
