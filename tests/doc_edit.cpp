// Editing: formulas unparsed and parsed again give the same tokens; text
// edits undone by their inverse (insert/delete, split/join) write the same
// part record; cell input round trips through CellInput.
#include <cctype>
#include <cstdio>

#include "Document.h"
#include "Editing.h"
#include "SSEval.h"

using namespace gobe;

static bool
SameTokens(const std::vector<FormulaToken>& a, const std::vector<FormulaToken>& b)
{
	if (a.size() != b.size())
		return false;
	for (size_t i = 0; i < a.size(); i++) {
		const FormulaToken& x = a[i];
		const FormulaToken& y = b[i];
		bool number = x.kind >= 0x13 && x.kind <= 0x16 && y.kind >= 0x13 && y.kind <= 0x16;
		if (x.kind != y.kind && !number)
			return false;
		if (number ? x.number != y.number : x.value != y.value)
			return false;
		if (x.strings.size() != y.strings.size() || x.refs.size() != y.refs.size())
			return false;
		for (size_t k = 0; k < x.strings.size(); k++) {
			std::string s = x.strings[k], t = y.strings[k];
			if (x.kind == 0x1c)
				for (auto* str : {&s, &t})
					for (char& c : *str)
						c = toupper((unsigned char)c);
			if (s != t)
				return false;
		}
		for (size_t k = 0; k < x.refs.size(); k++) {
			const CellRef& r = x.refs[k];
			const CellRef& q = y.refs[k];
			if (r.cell.a != q.cell.a || r.cell.b != q.cell.b || (r.flags & 7) != (q.flags & 7))
				return false;
		}
	}
	return true;
}

int main(int argc, char** argv)
{
	int formulas = 0, formulaBad = 0, edits = 0, editBad = 0, inputs = 0, inputBad = 0;
	for (int i = 1; i < argc; i++) {
		Document doc;
		std::string error;
		if (!LoadDocument(argv[i], doc, error))
			continue;
		auto isName = [&](const std::string& n) { return doc.NamedSelection(n, -1) != nullptr; };
		for (auto& [p, ss] : doc.spreadsheetParts) {
			for (const SSCell& c : ss.cells) {
				if (c.kind != Sig("Fmla"))
					continue;
				formulas++;
				std::string text = FormulaText(c.formula, c.id);
				std::vector<FormulaToken> tokens;
				if (!ParseFormula(text, c.id, isName, tokens, error)
						|| !SameTokens(tokens, c.formula)) {
					if (formulaBad++ < 10)
						printf("%s %s: =%s -> %s\n", argv[i], CellName(c.id).c_str(),
							text.c_str(), error.c_str());
				}
			}
			// cell input round trip on a copy
			SSPart copy = ss;
			for (const SSCell& c : ss.cells) {
				if (c.kind == Sig("NONE") || c.kind == Sig("FmEr"))
					continue;
				inputs++;
				std::string input = CellInput(ss, c.id.a, c.id.b);
				if (!SetCellInput(copy, c.id.a, c.id.b, input, isName, error)
						|| CellInput(copy, c.id.a, c.id.b) != input) {
					if (inputBad++ < 10)
						printf("%s %s: input '%s' -> '%s' %s\n", argv[i], CellName(c.id).c_str(),
							input.c_str(), CellInput(copy, c.id.a, c.id.b).c_str(), error.c_str());
				}
			}
		}
		for (auto& [p, wp] : doc.wordParts) {
			const std::vector<uint8_t>& record = doc.parts.parts[p].data;
			for (size_t k = 0; k < wp.paragraphs.size(); k++) {
				size_t len = EditableLength(wp.paragraphs[k]);
				size_t mid = 0;
				while (mid < len / 2)
					mid = NextCharacter(wp.paragraphs[k].text, mid);
				WPPart a = wp;
				TextPosition at = InsertText(a, {k, mid}, "XYZ");
				DeleteText(a, {k, mid}, at.offset);
				WPPart b = wp;
				TextPosition next = SplitParagraph(b, {k, mid});
				TextPosition joined;
				JoinWithPrevious(b, next.paragraph, joined);
				edits += 2;
				if (WriteWPPart(a, record) != record) {
					if (editBad++ < 10)
						printf("%s part %zu par %zu: insert/delete differs\n", argv[i], p, k);
				}
				if (WriteWPPart(b, record) != record) {
					if (editBad++ < 10)
						printf("%s part %zu par %zu: split/join differs\n", argv[i], p, k);
				}
			}
		}
	}
	printf("formulas %d, reparsed differently %d; text edits %d, differ %d; "
		"cell inputs %d, differ %d\n", formulas, formulaBad, edits, editBad, inputs, inputBad);
	return formulaBad || editBad || inputBad ? 1 : 0;
}
