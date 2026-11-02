// table_new: Insert Table… (TTablePart::TTablePart(TDoc*, rows, columns))
// in a new word processing document: MakeNewTable's sizes, the table part
// and its text part as a table made in Gobe has them (the Avery label
// templates: a cell break per cell, then an empty paragraph; the cells'
// frames inset 2 across and 1 down, a character each), shown inline at
// the start of the text, saved and read back. No arguments.
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "Document.h"
#include "InlineParts.h"
#include "TablePart.h"
#include "WPPart.h"

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


static const TablePart*
TableOf(const Document& doc, size_t part)
{
	for (const auto& [index, table] : doc.tableParts)
		if (index == part)
			return &table;
	return nullptr;
}


// the table part's structure and its text part's, in `doc`
static void
CheckTable(const Document& doc, size_t part, const std::string& when)
{
	const TablePart* table = TableOf(doc, part);
	Check(table != nullptr, when + ": a table part");
	if (table == nullptr)
		return;
	Check(table->size[0] == 400 && table->size[1] == 51,
		when + ": 400 x 51 (" + std::to_string(table->size[0]) + " x "
			+ std::to_string(table->size[1]) + ")");
	Check(table->value20 == 1, when + ": gutter 1");
	Check(table->rows.size() == 3, when + ": 3 rows");
	size_t index = 0;
	for (size_t r = 0; r < table->rows.size(); r++) {
		const TableRow& row = table->rows[r];
		Check(row.top == 17 && row.height == 17, when + ": row heights 17");
		Check(row.cells.size() == 4, when + ": 4 cells a row");
		for (size_t c = 0; c < row.cells.size(); c++, index++) {
			const TableCell& cell = row.cells[c];
			Check(cell.row == r && cell.column == c, when + ": cell location");
			Check(cell.origin[0] == 100 * c && cell.origin[1] == 17 * r
				&& cell.size[0] == 100 && cell.size[1] == 17, when + ": cell rectangle");
			Check(cell.values[0] == 1 && cell.values[1] == 1, when + ": spans 1");
			Check(cell.frame[0] == table->textPart && cell.frame[2] == index,
				when + ": cell frame pointer");
		}
	}
	const WPPart* wp = doc.WordPart(table->textPart);
	Check(wp != nullptr && wp->paragraphs.size() == 13, when + ": 12 cells + 1 paragraphs");
	if (wp != nullptr)
		for (size_t p = 0; p < wp->paragraphs.size(); p++)
			Check(wp->paragraphs[p].text == (p < 12 ? std::string("\x05") : std::string()),
				when + ": paragraph " + std::to_string(p));
	const std::vector<Portal>& portals = doc.tails[table->textPart].portals;
	Check(portals.size() == 1 && portals[0].frames.size() == 12, when + ": 12 cell frames");
	if (portals.size() == 1)
		for (size_t i = 0; i < portals[0].frames.size(); i++) {
			const Frame& f = portals[0].frames[i];
			size_t r = i / 4, c = i % 4;
			Check(f.origin[0] == 100 * c + 2 && f.origin[1] == 17 * r + 1
				&& f.extent[0] == 96 && f.extent[1] == 15, when + ": frame inset 2, 1");
			Check(f.textStart == i && f.textLength == 1, when + ": frame text a cell break");
			Check(f.values[1] == i && f.values[3] == 72, when + ": frame values");
		}
}


int
main()
{
	NewTable spec = MakeNewTable(3, 4);
	Check(spec.columns.size() == 4 && spec.columns[0] == 100, "400 / 4 points a column");
	Check(spec.rows.size() == 3 && spec.rows[0] == 17, "17 points a row");
	Check(spec.inset == 1 && spec.cellBreaks, "gutter 1, cell breaks");
	Check(MakeNewTable(0, 3).columns.empty(), "no rows: no table");

	Document doc;
	std::string error;
	Check(CreateBlankDocument(doc, error), "blank document: " + error);
	int main = doc.MainPart();
	Check(main >= 0 && doc.WordPart(size_t(main)) != nullptr, "a text main part");
	if (main < 0 || doc.WordPart(size_t(main)) == nullptr) {
		printf("table ok %d, failed %d\n", sOk, sFailed);
		return 1;
	}
	size_t text = 0;
	int part = AddTablePart(doc, size_t(main), spec, text, error);
	Check(part >= 0, "AddTablePart: " + error);
	if (part < 0) {
		printf("table ok %d, failed %d\n", sOk, sFailed);
		return 1;
	}
	InsertInlineFrame(doc, size_t(main), {0, 0}, size_t(part));
	CheckTable(doc, size_t(part), "made");

	const char* dir = getenv("TMPDIR");
	std::string path = std::string(dir != nullptr ? dir : "/tmp") + "/table_new.gobe";
	Check(doc.Save(path, error), "save: " + error);
	Document back;
	Check(LoadDocument(path, back, error), "load: " + error);
	CheckTable(back, size_t(part), "read back");
	const WPPart* body = back.WordPart(size_t(main));
	Check(body != nullptr && !body->paragraphs.empty()
		&& body->paragraphs[0].text.size() > 0 && body->paragraphs[0].text[0] == 0x04,
		"the table inline at the start of the text");

	printf("table ok %d, failed %d\n", sOk, sFailed);
	return sFailed > 0 ? 1 : 0;
}
