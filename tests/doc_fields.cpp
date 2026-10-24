// doc_fields: what fields in text show (TMiscFormulaInfo::Text,
// TMiscPageInfo::GetPageStr). Each formula field evaluated again shows what
// the original showed when it saved the document (the result saved with
// it, in its number format); Now() shows the current year; a page number is
// its page's from 1, "#" off the pages. Arguments: the sample documents.
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>

#include "Document.h"
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
	printf("fields ok %d, failed %d\n", sOk, sFailed);
	return sFailed > 0 ? 1 : 0;
}
