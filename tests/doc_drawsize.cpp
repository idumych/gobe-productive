// doc_drawsize: Document Size for drawings (DrawingPages, SetDrawingPages)
// and spreadsheets (SetSheetSize), plus save/reload round-trips.
#include <cstdio>
#include <cstdlib>
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


int
main(int argc, char** argv)
{
	(void)argc; (void)argv;

	// ---- Drawing ----

	Document d;
	std::string error;
	Check(CreateBlankDrawing(d, error), "CreateBlankDrawing " + error);

	size_t main = size_t(d.MainPart());
	const float* p = d.sheets.sheets[0].printable;
	float w = p[2] - p[0], h = p[3] - p[1];

	// 1. DrawingPages(default) == 1 x 1
	{
		uint32_t a = 0, b = 0;
		bool ok = d.DrawingPages(main, a, b);
		Check(ok && a == 1 && b == 1, "DrawingPages default: 1x1");
	}

	// 2. SetDrawingPages(2, 3) returns true
	Check(d.SetDrawingPages(main, 2, 3), "SetDrawingPages 2x3");

	// 3. PageCount == 6, PagesAcrossCount == 2
	Check(d.PageCount() == 6, "PageCount == 6");
	Check(d.PagesAcrossCount() == 2, "PagesAcrossCount == 2");

	// 4. GraphicsPart size == w*2, h*3
	const GRPart* gr = d.GraphicsPart(main);
	Check(gr != nullptr, "GraphicsPart not null");
	if (gr) {
		char buf[128];
		snprintf(buf, sizeof buf, "size[0] == %.0f (got %.0f)", float(w * 2), gr->size[0]);
		Check(gr->size[0] == float(w * 2), buf);
		snprintf(buf, sizeof buf, "size[1] == %.0f (got %.0f)", float(h * 3), gr->size[1]);
		Check(gr->size[1] == float(h * 3), buf);
	}

	// 5. DrawingPages now gives 2 and 3
	{
		uint32_t a = 0, b = 0;
		d.DrawingPages(main, a, b);
		Check(a == 2 && b == 3, "DrawingPages after set: 2x3");
	}

	// 6. SetDrawingPages(2, 3) again returns false
	Check(d.SetDrawingPages(main, 2, 3) == false, "SetDrawingPages 2x3 again false");

	// 7. SetDrawingPages(1, 1) returns true; PageCount == 1, PagesAcrossCount == 1
	Check(d.SetDrawingPages(main, 1, 1), "SetDrawingPages 1x1");
	Check(d.PageCount() == 1, "PageCount == 1");
	Check(d.PagesAcrossCount() == 1, "PagesAcrossCount == 1");

	// 8. SetDrawingPages(0, 1) returns false
	Check(d.SetDrawingPages(main, 0, 1) == false, "SetDrawingPages 0x1 false");

	// 9. Set 3x2, Save, LoadDocument
	d.SetDrawingPages(main, 3, 2);
	const char* tmpdir = getenv("TMPDIR");
	if (!tmpdir)
		tmpdir = "/tmp";
	std::string savePath(tmpdir);
	savePath += "/drawsize.gobe";
	Check(d.Save(savePath, error), "Save " + error);

	Document d2;
	Check(LoadDocument(savePath, d2, error), "LoadDocument " + error);
	Check(d2.PageCount() == 6, "reload PageCount == 6");
	Check(d2.PagesAcrossCount() == 3, "reload PagesAcrossCount == 3");
	{
		size_t m2 = size_t(d2.MainPart());
		uint32_t a = 0, b = 0;
		d2.DrawingPages(m2, a, b);
		Check(a == 3 && b == 2, "reload DrawingPages: 3x2");
	}
	remove(savePath.c_str());

	// ---- Spreadsheet ----

	Document s;
	Check(CreateBlankSpreadsheet(s, error), "CreateBlankSpreadsheet " + error);
	size_t smain = size_t(s.MainPart());

	// 10. SpreadsheetPart used: (0,0) to (499,19)
	const SSPart* ss = s.SpreadsheetPart(smain);
	Check(ss != nullptr, "SpreadsheetPart not null");
	if (ss) {
		Check(ss->used.from.a == 0 && ss->used.from.b == 0, "used.from 0,0");
		Check(ss->used.to.a == 499 && ss->used.to.b == 19, "used.to 499,19");
	}

	// 11. SetSheetSize(100, 10) returns true; used updated
	Check(s.SetSheetSize(smain, 100, 10), "SetSheetSize 100x10");
	Check(s.SpreadsheetPart(smain)->used.to.a == 99, "used.to.a == 99");
	Check(s.SpreadsheetPart(smain)->used.to.b == 9, "used.to.b == 9");
	Check(s.SpreadsheetPart(smain)->used.from.a == 0, "used.from.a == 0");
	Check(s.SpreadsheetPart(smain)->used.from.b == 0, "used.from.b == 0");

	// 12. Same call again returns false
	Check(s.SetSheetSize(smain, 100, 10) == false, "SetSheetSize 100x10 again false");

	// 13. SetSheetSize(0, 5) and (5, 0) return false
	Check(s.SetSheetSize(smain, 0, 5) == false, "SetSheetSize 0x5 false");
	Check(s.SetSheetSize(smain, 5, 0) == false, "SetSheetSize 5x0 false");

	// 14. Save and reload: used.to is (99, 9)
	Check(s.Save(savePath, error), "Save ss " + error);
	Document s2;
	Check(LoadDocument(savePath, s2, error), "LoadDocument ss " + error);
	const SSPart* ss2 = s2.SpreadsheetPart(smain);
	Check(ss2 != nullptr, "reload SpreadsheetPart not null");
	if (ss2) {
		char buf[128];
		snprintf(buf, sizeof buf, "reload used.to.a == 99 (got %d)", ss2->used.to.a);
		Check(ss2->used.to.a == 99, buf);
		snprintf(buf, sizeof buf, "reload used.to.b == 9 (got %d)", ss2->used.to.b);
		Check(ss2->used.to.b == 9, buf);
	}
	remove(savePath.c_str());

	printf("drawsize ok %d, failed %d\n", sOk, sFailed);
	return sFailed > 0 || sOk == 0;
}
