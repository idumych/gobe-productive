// doc_columns: Format > Columns… (TWPSetColumnsCmd,
// TColumnData::SetColumnCount): equal columns with 1/30 of the width
// between them, and that many text frames on every page. Expected values
// from python3 with the same float32 steps:
//   g=f(0.033333335); w=f(f(1.0/n)-f(f(float(n-1)*g)/n))
//   column i: left f(f(w+g)*i), right f(w+left)
//   n=2: (0, 0.48333332), (0.51666665, 1); x 72, 313.8, width 226.2
//   n=3: right edges 0.31111112, 0.6555556, 1; x 72, 233.2, 394.4
// Arguments: none.
#include <cmath>
#include <cstdio>
#include <string>

#include "Document.h"

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


static bool
Near(float a, float b)
{
	return std::fabs(a - b) < 0.001f;
}


static const Frame*
Column(const Document& d, size_t page, size_t column)
{
	const Frame& p = d.tails[d.PagePart()].portals[0].frames[page];
	if (3 + column >= p.children.size())
		return nullptr;
	const Frame::Pointer& c = p.children[3 + column];
	return &d.tails[c.part].portals[c.portal].frames[c.frame];
}


int
main()
{
	Document doc;
	std::string error, path = "/tmp/doc_columns.gobe";
	CreateBlankDocument(doc, error);
	Check(!doc.SetColumnCount(0, 0, 1), "one column already");
	Check(!doc.SetColumnCount(0, 0, 11) && !doc.SetColumnCount(0, 0, 0), "1 to 10 columns");
	Check(doc.SetColumnCount(0, 0, 2), "two columns");
	const auto& columns = doc.WordPart(0)->sections[0].columns;
	Check(columns.size() == 2 && columns[0][0] == 0 && Near(columns[0][2], 0.48333332f)
		&& Near(columns[1][0], 0.51666665f) && columns[1][2] == 1 && columns[1][1] == 0
		&& columns[1][3] == 1, "the section's columns");
	const Frame* a = Column(doc, 0, 0);
	const Frame* b = Column(doc, 0, 1);
	Check(a != nullptr && b != nullptr && Near(a->origin[0], 72) && Near(b->origin[0], 313.8f)
		&& Near(a->extent[0], 226.2f) && Near(b->extent[0], 226.2f) && Near(b->origin[1], 72)
		&& Near(b->extent[1], 648), "two text frames on the page");
	Check(doc.AddPage() && Column(doc, 1, 1) != nullptr && Column(doc, 1, 2) == nullptr,
		"a new page has two");
	Check(doc.Save(path, error), "save " + error);
	Check(doc.WordPart(0)->sections[0].columns.size() == 2 && doc.PageCount() == 2
		&& doc.tails[0].portals[0].frames.size() == 4 && Column(doc, 1, 1) != nullptr
		&& Near(Column(doc, 1, 1)->origin[0], 313.8f), "saved and read back");
	Check(doc.SetColumnCount(0, 0, 3), "three columns");
	Check(Near(Column(doc, 1, 1)->origin[0], 233.2f) && Near(Column(doc, 1, 2)->origin[0], 394.4f)
		&& Near(Column(doc, 0, 2)->extent[0], 145.6f)
		&& doc.tails[0].portals[0].frames.size() == 6, "three on each page");
	Check(doc.SetColumnCount(0, 0, 1) && Near(Column(doc, 0, 0)->extent[0], 468)
		&& Column(doc, 0, 1) == nullptr && doc.tails[0].portals[0].frames.size() == 2,
		"back to one");
	Check(doc.Save(path, error), "save again " + error);
	remove(path.c_str());
	printf("columns ok %d, failed %d\n", sOk, sFailed);
	return sFailed > 0 ? 1 : 0;
}
