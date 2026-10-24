// doc_fields: what fields in text show (TMiscFormulaInfo::Text,
// TMiscPageInfo::GetPageStr). Each formula field evaluated again shows what
// the original showed when it saved the document (the result saved with
// it, in its number format); Now() shows the current year; a page number is
// its page's from 1, "#" off the pages. Inserting fields
// (TWPPart::InsertMiscCharPrim: a 0x04 character, a separator line in a
// paragraph of its own) and deleting them. Arguments: the sample documents.
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

#include "Document.h"
#include "Editing.h"
#include "Fields.h"
#include "SSEval.h"
#include "SSStyle.h"

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


int
main(int argc, char** argv)
{
	int fields = 0, now = 0;
	time_t t = time(NULL);
	std::string year = std::to_string(localtime(&t)->tm_year + 1900);
	for (int i = 1; i < argc; i++) {
		Document d;
		std::string error;
		if (!LoadDocument(argv[i], d, error))
			continue;
		SSBook book(d);
		const char* leaf = strrchr(argv[i], '/') + 1;
		for (const auto& [part, wp] : d.wordParts)
			for (const WPParagraph& p : wp.paragraphs)
				for (const WPMiscInfo& m : p.misc) {
					if (m.kind != Sig("FORM"))
						continue;
					std::string shown = FieldText(book, part, m, 0);
					Check(m.command == Sig("sM53") || m.command == Sig("sM54")
						|| m.command == Sig("sM55"), std::string(leaf) + ": the command");
					if (m.text == "=Now()") {
						Check(shown.find(year) != std::string::npos, std::string(leaf)
							+ ": " + m.text + " shows " + shown);
						now++;
						continue;
					}
					std::string saved = FormatCellValue(StoredValue(m.expression.result),
						m.expression.format);
					Check(shown == saved, std::string(leaf) + ": " + m.text + " shows \""
						+ shown + "\", the original \"" + saved + "\"");
					fields++;
				}
	}
	Check(fields >= 30 && now >= 2, "fields: " + std::to_string(fields) + ", Now(): "
		+ std::to_string(now));
	// page numbers
	Document empty;
	SSBook none(empty);
	WPMiscInfo page{};
	page.kind = Sig("PAGE");
	Check(FieldText(none, 0, page, 0) == "1" && FieldText(none, 0, page, 11) == "12"
		&& FieldText(none, 0, page, -1) == "#", "page numbers");
	WPMiscInfo line{};
	line.kind = Sig("LINE");
	Check(FieldText(none, 0, line, 0).empty(), "a separator line shows no text");

	// a new date field is written as the original wrote TheWidget's,
	// but for the time it was evaluated and the formula's pointer word
	int compared = 0;
	for (int i = 1; i < argc; i++) {
		Document d;
		std::string error;
		if (strstr(argv[i], "TheWidget") == nullptr || !LoadDocument(argv[i], d, error))
			continue;
		SSBook book(d);
		for (const auto& [part, w] : d.wordParts)
			for (const WPParagraph& p : w.paragraphs)
				for (const WPMiscInfo& m : p.misc) {
					if (m.kind != Sig("FORM"))
						continue;
					WPMiscInfo made;
					Check(FormulaField(d, book, part, "=Now()", "mmmm d, yyyy", Sig("sM53"), false,
						true, false, made, error), "FormulaField " + error);
					std::vector<uint8_t> expected = m.raw;
					std::string bytes(expected.begin(), expected.end());
					size_t time = bytes.find("Time"), format = bytes.find("yyyy");
					bool found = time != std::string::npos && format != std::string::npos
						&& made.raw.size() == expected.size();
					if (found) {
						for (size_t k = time + 4; k < time + 8; k++)
							expected[k] = made.raw[k];
						for (size_t k = format + 5; k < format + 9; k++)
							expected[k] = made.raw[k];
					}
					Check(found && made.raw == expected, "the date field's bytes");
					Check(FieldText(book, part, made, 0) == FieldText(book, part, m, 0),
						"it shows what TheWidget's shows");
					compared++;
				}
	}
	Check(compared == 1, "TheWidget's date field");

	// the samples' formula fields made again from their formulas: the
	// same tokens, links (to the names' references) and results
	int remade = 0;
	for (int i = 1; i < argc; i++) {
		Document d;
		std::string error;
		if (!LoadDocument(argv[i], d, error))
			continue;
		SSBook book(d);
		size_t links = d.links.links.size();
		const char* leaf = strrchr(argv[i], '/') + 1;
		std::vector<std::pair<size_t, WPMiscInfo>> fields;
		for (const auto& [part, w] : d.wordParts)
			for (const WPParagraph& p : w.paragraphs)
				for (const WPMiscInfo& m : p.misc)
					if (m.kind == Sig("FORM") && m.command == Sig("sM55"))
						fields.emplace_back(part, m);
		for (const auto& [part, m] : fields) {
			WPMiscInfo made;
			bool ok = FormulaField(d, book, part, m.text, m.expression.format, m.command,
				m.isStatic, m.displayResult, m.displayFormula, made, error);
			const Expression& a = made.expression;
			const Expression& b = m.expression;
			bool same = ok && a.formula.size() == b.formula.size() && a.links == b.links
				&& a.result.type == b.result.type && a.result.text == b.result.text
				&& a.result.number == b.result.number;
			for (size_t k = 0; same && k < a.formula.size(); k++)
				same = a.formula[k].kind == b.formula[k].kind
					&& a.formula[k].number == b.formula[k].number
					&& a.formula[k].value == b.formula[k].value
					&& a.formula[k].strings == b.formula[k].strings;
			Check(same, std::string(leaf) + ": " + m.text + " made again " + error);
			remade++;
		}
		Check(d.links.links.size() == links && !d.linksChanged,
			std::string(leaf) + ": the links were there");
	}
	Check(remade >= 30, "formula fields made again: " + std::to_string(remade));

	// inserting: "Page #" and a separator line in the middle of "abcd"
	Document doc;
	std::string error;
	CreateBlankDocument(doc, error);
	WPPart* wp = doc.EditableWordPart(0);
	TextPosition at = InsertText(*wp, {0, 0}, "Page ");
	at = InsertField(*wp, at, page);
	Check(wp->paragraphs[0].text == "Page \x04" && at.offset == 6
		&& wp->paragraphs[0].misc.size() == 1 && wp->paragraphs[0].misc[0].position == 5,
		"page number at 5");
	at = SplitParagraph(*wp, at);
	at = InsertText(*wp, at, "abcd");
	at = InsertField(*wp, {1, 2}, line);
	Check(wp->paragraphs.size() == 3 && wp->paragraphs[1].text == "ab\n"
		&& wp->paragraphs[2].text == "\x04" "cd" && wp->paragraphs[2].misc.size() == 1
		&& wp->paragraphs[2].misc[0].position == 0 && at.paragraph == 2 && at.offset == 1,
		"the separator line starts a paragraph");
	InsertText(*wp, {0, 0}, "My ");
	Check(wp->paragraphs[0].misc[0].position == 8, "text before a field moves it");
	std::string path = "/tmp/doc_fields.gobe";
	Check(doc.Save(path, error), "save " + error);
	const WPPart* back = doc.WordPart(0);
	Check(back != nullptr && back->paragraphs[0].misc.size() == 1
		&& back->paragraphs[0].misc[0].kind == Sig("PAGE")
		&& back->paragraphs[0].misc[0].position == 8 && back->paragraphs[2].misc.size() == 1
		&& back->paragraphs[2].misc[0].kind == Sig("LINE"), "saved and read back");
	wp = doc.EditableWordPart(0);
	Check(DeleteText(*wp, {0, 7}, 9) && wp->paragraphs[0].text == "My Page\n"
		&& wp->paragraphs[0].misc.empty(), "deleting a field's character deletes the field");
	remove(path.c_str());
	// an embedded frame's anchor isn't deleted (that would take the frame)
	int anchors = 0;
	for (int i = 1; i < argc && anchors == 0; i++) {
		Document d;
		if (!LoadDocument(argv[i], d, error))
			continue;
		for (auto& [part, w] : d.wordParts)
			for (size_t k = 0; k < w.paragraphs.size() && anchors == 0; k++)
				for (const WPMiscInfo& m : w.paragraphs[k].misc)
					if (m.kind == Sig("FRAM")) {
						WPPart* e = d.EditableWordPart(part);
						std::string before = e->paragraphs[k].text;
						Check(!DeleteText(*e, {k, m.position}, m.position + 1)
							&& e->paragraphs[k].text == before, "a frame's anchor stays");
						anchors++;
						break;
					}
	}
	Check(anchors == 1, "a sample with an embedded frame");
	printf("fields ok %d, failed %d\n", sOk, sFailed);
	return sFailed > 0 ? 1 : 0;
}
