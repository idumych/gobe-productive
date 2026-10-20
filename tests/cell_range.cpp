// RangeInput, ClearRange and PasteInput on spreadsheet parts:
// round-trip through paste, clear+restore, and a synthetic paste.
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "Document.h"
#include "Editing.h"

using namespace gobe;

static int failures = 0;

static void
Check(bool ok, const char* what)
{
	if (!ok) {
		printf("FAIL: %s\n", what);
		failures++;
	}
}


int main(int argc, char** argv)
{
	// --- document tests ---
	for (int i = 1; i < argc; i++) {
		Document doc;
		std::string error;
		if (!LoadDocument(argv[i], doc, error))
			continue;

		auto isName = [&](const std::string& n) { return doc.NamedSelection(n, -1) != nullptr; };
		for (auto& [p, ss] : doc.spreadsheetParts) {
			// round-trip: extract, paste into copy, compare
			std::string s = RangeInput(ss, 0, 0, 9, 5);
			SSPart copy = ss;
			uint16_t rows, columns;
			Check(PasteInput(copy, 0, 0, s, isName, error, rows, columns),
				"paste round-trip");
			Check(RangeInput(copy, 0, 0, 9, 5) == s, "paste round-trip content");

			// clear then paste back
			SSPart clear = ss;
			bool content = s.find_first_not_of("\t\n") != std::string::npos;
			Check(ClearRange(clear, 0, 0, 9, 5) == content, "clear changes a block with content");
			std::string cleared = RangeInput(clear, 0, 0, 9, 5);
			bool ok = true;
			for (char c : cleared)
				if (c != '\t' && c != '\n') {
					ok = false;
					break;
				}
			Check(ok, "cleared range is only tabs and newlines");
			uint16_t r2, c2;
			Check(PasteInput(clear, 0, 0, s, isName, error, r2, c2),
				"paste into cleared");
			Check(RangeInput(clear, 0, 0, 9, 5) == s, "paste into cleared content");
		}
	}

	// synthetic: an empty part
	{
		SSPart part;
		std::string error;
		const char* paste_text = "1\t2\n=A1+B1\r\nx\n";
		uint16_t rows, columns;
		Check(PasteInput(part, 0, 0, paste_text, nullptr, error, rows, columns)
				&& rows == 3 && columns == 2,
			"synthetic paste dims");
		if (!error.empty())
			printf("error: %s\n", error.c_str());

		std::string s3 = RangeInput(part, 0, 0, 2, 1);
		std::string expected = "1\t2\n=A1+B1\t\nx\t";
		Check(s3 == expected, "synthetic paste content");
		if (s3 != expected)
			printf("got: \"%s\"\n", s3.c_str());
	}

	printf(failures ? "%d failures\n" : "cell_range: ok\n", failures);
	return failures != 0;
}
