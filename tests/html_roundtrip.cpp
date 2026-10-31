// html_roundtrip: tables and pictures opened from HTML (TImportHTML,
// HTMLTree's tables, TTableImport). A document with a table and a picture
// in its text written as HTML (with the picture's file) and opened again
// must have the same table, cells and picture, and write the same HTML;
// hand-written HTML: ragged rows, TH, a table in a cell, a picture whose
// file isn't there (its ALT text), text around them; saved and opened
// again.
// Arguments: documents (LogoLetter's logo is used).
#include <cstdio>
#include <string>
#include <sys/stat.h>

#include "Document.h"
#include "Editing.h"
#include "HTMLExport.h"
#include "HTMLImport.h"
#include "InlineParts.h"

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


static std::string
CellText(const Document& doc, size_t table, size_t row, size_t column)
{
	int p = CellParagraph(doc, table, row, column);
	const TablePart* t = doc.Table(table);
	if (p < 0 || t == nullptr)
		return "?";
	std::string text = doc.WordPart(t->textPart)->paragraphs[size_t(p)].text;
	if (!text.empty() && text.back() == '\n')
		text.pop_back();
	return text;
}


// the document's text: its paragraphs, a FRAM as "[TBLE]" or "[BITS]"
static std::string
MainText(const Document& doc)
{
	std::string out;
	const WPPart* wp = doc.WordPart(size_t(doc.MainPart()));
	for (const WPParagraph& p : wp->paragraphs) {
		for (size_t i = 0; i < p.text.size(); i++) {
			const WPMiscInfo* frame = nullptr;
			for (const WPMiscInfo& m : p.misc)
				if (m.position == i && m.kind == Sig("FRAM"))
					frame = &m;
			if (frame != nullptr) {
				const Frame* f = FrameAt(doc, frame->frame);
				uint32_t kind = f != nullptr ? doc.parts.parts[f->part].kind : 0;
				out += kind == Sig("TBLE") ? "[TBLE]" : kind == Sig("BITS") ? "[BITS]" : "[?]";
			} else
				out += p.text[i];
		}
	}
	return out;
}


int
main(int argc, char** argv)
{
	const IMPart* logo = nullptr;
	Document source;
	std::string error;
	for (int i = 1; i < argc && logo == nullptr; i++)
		if (std::string(argv[i]).find("LogoLetter") != std::string::npos
				&& LoadDocument(argv[i], source, error) && !source.imageParts.empty())
			logo = &source.imageParts[0].second;
	Check(logo != nullptr, "LogoLetter's logo");

	// a document with a table and a picture, as HTML and back
	Document doc;
	Check(CreateBlankDocument(doc, error), "a new document " + error);
	size_t main = size_t(doc.MainPart());
	InsertMultiline(*doc.EditableWordPart(main), {0, 0}, "Before\nBetween\nAfter");
	NewTable table;
	table.columns = {120, 200};
	table.rows = {20, 20};
	table.cells = {{"Name", "Value"}, {"one", "1"}};
	size_t text = 0;
	int t = AddTablePart(doc, main, table, text, error);
	Check(t >= 0, "the table " + error);
	InsertInlineFrame(doc, main, {1, 0}, size_t(t));
	if (logo != nullptr) {
		int picture = AddImagePart(doc, main, logo->image, error);
		Check(picture >= 0, "the picture " + error);
		InsertInlineFrame(doc, main, {2, 0}, size_t(picture));
	}
	std::string folder = "/tmp/html_roundtrip";
	mkdir(folder.c_str(), 0755);
	Check(ExportHTML(doc, folder + "/page.html", "page", error), "written " + error);
	std::string html = HTMLText(doc, "page");
	Document back;
	Check(ImportHTML(folder + "/page.html", back, error), "read " + error);
	Check(back.tableParts.size() == 1 && back.imageParts.size() == (logo != nullptr ? 1u : 0u),
		"a table and a picture read: " + std::to_string(back.tableParts.size()) + ", "
		+ std::to_string(back.imageParts.size()));
	if (!back.tableParts.empty()) {
		size_t bt = back.tableParts[0].first;
		const TablePart* tp = back.Table(bt);
		Check(tp->rows.size() == 2 && tp->rows[0].cells.size() == 2, "two rows of two cells");
		Check(CellText(back, bt, 0, 0) == "Name" && CellText(back, bt, 0, 1) == "Value"
			&& CellText(back, bt, 1, 0) == "one" && CellText(back, bt, 1, 1) == "1", "the cells");
		// the cells' widths come from the HTML's WIDTH
		Check(tp->rows[0].cells[0].size[0] == 120 && tp->rows[0].cells[1].size[0] == 200,
			"the columns' widths");
	}
	if (logo != nullptr && !back.imageParts.empty())
		Check(back.imageParts[0].second.image.bits == logo->image.bits, "the picture's pixels");
	Check(MainText(back) == "Before\n[TBLE]\nBetween\n[BITS]After" || MainText(back)
		== "Before\n[TBLE]\nBetween\n[BITS]After\n", "the text and frames: " + MainText(back));
	std::string again = HTMLText(back, "page");
	size_t a = html.find("<TR>"), b = again.find("<TR>");
	Check(a != std::string::npos && b != std::string::npos
		&& html.substr(a, html.find("</TABLE>") - a) == again.substr(b, again.find("</TABLE>") - b),
		"the table's rows written the same:\n" + html + "\n---\n" + again);

	// hand-written
	std::string page =
		"<HTML><BODY><P>Intro</P>"
		"<TABLE WIDTH=\"300\" CELLPADDING=3><TR><TH>H1<TH>H2<TH>H3"
		"<TR><TD>a<TD><B>b</B> text"
		"<TR><TD>x<TD><TABLE><TR><TD>inner</TD></TR></TABLE><TD>z</TABLE>"
		"<P>Missing <IMG SRC=\"nothing.png\" ALT=\"[no picture]\"> end</P></BODY></HTML>";
	Document hand;
	Check(ImportHTMLText(page, hand, error, folder), "hand-written " + error);
	Check(hand.tableParts.size() == 2, "two tables (one in a cell): "
		+ std::to_string(hand.tableParts.size()));
	if (hand.tableParts.size() == 2) {
		size_t outer = hand.tableParts[0].first, inner = hand.tableParts[1].first;
		// the outer table is made first, its cell's table inside its text
		const TablePart* o = hand.Table(outer);
		Check(o->rows.size() == 3 && o->rows[0].cells.size() == 3 && o->rows[1].cells.size() == 3
			&& o->value20 == 3, "3 x 3, the second row filled up, CELLPADDING 3");
		float width = 0;
		for (const TableCell& c : o->rows[0].cells)
			width += c.size[0];
		Check(width == 300, "WIDTH 300: " + std::to_string(width));
		Check(CellText(hand, outer, 0, 1) == "H2" && CellText(hand, outer, 1, 1) == "b text"
			&& CellText(hand, outer, 1, 2) == "" && CellText(hand, outer, 2, 2) == "z", "the cells");
		Check(CellText(hand, inner, 0, 0) == "inner", "the inner table");
		const Frame* f = hand.tails[inner].portals.empty() ? nullptr
			: &hand.tails[inner].portals[0].frames[0];
		Check(f != nullptr && f->container == o->textPart, "the inner table in the outer's text");
	}
	Check(MainText(hand) == "Intro\n[TBLE]\nMissing [no picture] end", "the text: " + MainText(hand));
	std::string handHTML = HTMLText(hand, "t");
	Check(handHTML.find("<TD WIDTH=") != std::string::npos
		&& handHTML.find("<B>H1</B>") != std::string::npos, "TH bold in HTML again");
	Check(hand.Save(folder + "/hand.gobe", error), "saved " + error);
	Document reopened;
	Check(LoadDocument(folder + "/hand.gobe", reopened, error) && HTMLText(reopened, "t") == handHTML,
		"the same after saving " + error);

	printf("html_roundtrip: ok %d, failed %d\n", sOk, sFailed);
	return sFailed ? 1 : 0;
}
