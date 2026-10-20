// Print spreadsheet cells (and formulas as text) of documents; with -f only
// list the functions used.
#include <cstdio>
#include <cstring>
#include <map>

#include "Document.h"
#include "ResourceFile.h"
#include "SSEval.h"
#include "SSStyle.h"
#include "SSPart.h"

using namespace gobe;

int main(int argc, char** argv)
{
	bool functions = argc > 1 && !strcmp(argv[1], "-f");
	std::map<std::string, int> used;
	for (int i = 1 + functions; i < argc; i++) {
		Document doc;
		std::string error;
		if (!LoadDocument(argv[i], doc, error))
			continue;
		SSBook book(doc);
		for (const auto& [p, ss] : doc.spreadsheetParts) {
			if (!functions)
				printf("== %s part %zu: %zu cells, width %g height %g, %zu cols %zu rows\n",
					argv[i], p, ss.cells.size(), ss.defaultWidth, ss.defaultHeight,
					ss.columns.size(), ss.rows.size());
			SSEvaluator& eval = *book.For(p);
			for (const SSCell& c : ss.cells) {
				if (functions) {
					for (auto& t : c.formula)
						if (t.kind == 0x1c)
							used[t.strings[0]]++;
					continue;
				}
				if (c.kind == Sig("NONE"))
					continue;
				SSValue v = eval.Value(c.id.a, c.id.b);
				std::vector<uint32_t> stack = {ss.style};
				for (auto& h : ss.columns)
					if (h.index == c.id.b)
						stack.push_back(h.cell.style);
				for (auto& h : ss.rows)
					if (h.index == c.id.a)
						stack.push_back(h.cell.style);
				stack.push_back(c.style);
				CellStyle cs = ResolveCellStyle(doc.styles, stack);
				bool red = false;
				std::string text = FormatCellValue(v, cs.format, &red);
				if (red)
					text += " (red)";
				printf("%s %s ", CellName(c.id).c_str(), FourCCString(c.kind).c_str());
				if (c.kind == Sig("Fmla"))
					printf("=%s", FormulaText(c.formula, c.id).c_str());
				printf(" -> [%s] style %d\n", text.c_str(), int(c.style));
			}
		}
	}
	for (auto& [f, n] : used)
		printf("%s %d\n", f.c_str(), n);
}
