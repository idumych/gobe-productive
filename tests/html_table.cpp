// html_table: tables in HTML (TExportHTML::WriteTable, WriteTableRow,
// WriteTableCell). A new document is given, by hand, a table (one row,
// two cells, a cell inset of 3) shown in a frame in its text (110 x 40,
// a 2-point pen, a green fill), the cells' text frames (one red with a
// centered paragraph, one aligned right and to the bottom). The table
// must come before the paragraph's text, with the attributes the
// original writes; then a table sheet's table, written on its own.
// Arguments: none (documents given are ignored).
#include <cstdio>
#include <string>

#include "DocWriter.h"
#include "Document.h"
#include "Editing.h"
#include "HTMLExport.h"
#include "Styles.h"

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


// a style with these properties and no links
static uint32_t
AddStyle(Document& doc, const std::vector<Property>& properties)
{
	Style style;
	for (uint32_t& link : style.links)
		link = 0xffffffff;
	for (const Property& p : properties) {
		style.props.push_back(uint32_t(doc.styles.properties.size()));
		doc.styles.properties.push_back(p);
	}
	doc.styles.styles.push_back(style);
	return uint32_t(doc.styles.styles.size() - 1);
}


static Property
Fill(uint8_t r, uint8_t g, uint8_t b)
{
	// a solid ink: on, the color, the low color, the pattern
	Property p{Sig("FCol"), {1, r, g, b, 255, 255, 255, 255, 0}};
	p.data.resize(17, 0xff);
	return p;
}


static Property
Float(uint32_t type, float value)
{
	DocWriter w;
	w.Float(value);
	return Property{type, w.Data()};
}


static Property
Short(uint32_t type, uint16_t value)
{
	DocWriter w;
	w.UInt16(value);
	return Property{type, w.Data()};
}


int
main()
{
	Document doc;
	std::string error;
	Check(CreateBlankDocument(doc, error), "a new document " + error);
	size_t main = size_t(doc.MainPart());

	// the parts: the table (T), its text (X), one holding the table's frame (D)
	size_t T = doc.parts.parts.size(), X = T + 1, D = T + 2;
	doc.parts.parts.resize(D + 1, PartRecord{Sig("TBLE"), 0, {}});
	doc.tails.resize(D + 1);
	doc.tailRead.resize(D + 1, true);

	WPPart text;
	WPParagraph a1, b1;
	a1.text = "A1\n";
	a1.parStyle = AddStyle(doc, {Short(Sig("PAlg"), 1)});		// centered
	b1.text = "B1\n";
	b1.parStyle = AddStyle(doc, {});
	text.paragraphs = {a1, b1};
	doc.wordParts.emplace_back(X, text);

	TablePart table;
	table.value20 = 3;
	table.textPart = uint32_t(X);
	TableRow row;
	TableCell c0, c1;
	c0.size[0] = 50;
	c0.size[1] = 20;
	c0.frame[0] = uint32_t(X);
	c0.frame[1] = 0;
	c1.size[0] = 60.4f;
	c1.size[1] = 20;
	c1.frame[0] = uint32_t(X);
	c1.frame[1] = 0;		// the portal (its id)
	c1.frame[2] = 1;		// the frame (its id, TFrame +0x38)
	row.cells = {c0, c1};
	table.rows = {row};
	doc.tableParts.emplace_back(T, table);

	Portal cells;
	Frame f0, f1;
	f0.part = f1.part = uint32_t(X);
	f0.textStart = 0;
	f0.textLength = 3;
	f0.style = AddStyle(doc, {Fill(255, 0, 0)});
	f1.values[1] = 1;
	f1.textStart = 3;
	f1.textLength = 3;
	// HAln 3 right, VAln 3 bottom (TPropHAlign, TPropVAlign)
	f1.style = AddStyle(doc, {Short(Sig("HAln"), 3), Short(Sig("VAln"), 3)});
	cells.frames = {f0, f1};
	doc.tails[X].portals = {cells};

	Portal holder;
	Frame tableFrame;
	tableFrame.part = uint32_t(T);
	tableFrame.extent[0] = 110;
	tableFrame.extent[1] = 40;
	tableFrame.style = AddStyle(doc, {Fill(0, 255, 0), Float(Sig("PENz"), 2), Float(Sig("PTra"), 1)});
	holder.frames = {tableFrame};
	doc.tails[D].portals = {holder};

	// the main text: the table's frame, then "after"
	WPPart* wp = doc.EditableWordPart(main);
	Check(wp != nullptr && !wp->paragraphs.empty(), "the main text");
	if (wp == nullptr || wp->paragraphs.empty())
		return 1;
	InsertMultiline(*wp, {0, 0}, "#after");
	WPMiscInfo frame;
	frame.kind = Sig("FRAM");
	frame.position = 0;
	frame.frame[0] = uint32_t(D);
	frame.frame[1] = 0;
	wp->paragraphs[0].misc.push_back(frame);

	std::string html = HTMLText(doc, "t");
	const std::string kTable = "<TABLE WIDTH=110 HEIGHT=40 BORDER=2 CELLPADDING=3 CELLSPACING=2"
		" BGCOLOR=\"#00ff00\"><TR>"
		"<TD WIDTH=50 HEIGHT=20 COLSPAN=1 ROWSPAN=1 BGCOLOR=\"#ff0000\" ALIGN=CENTER>"
		"<P ALIGN=\"center\">A1</P>\n</TD>"
		"<TD WIDTH=60 HEIGHT=20 COLSPAN=1 ROWSPAN=1 ALIGN=RIGHT VALIGN=BOTTOM><P>B1</P>\n</TD>"
		"</TR></TABLE>";
	Check(html.find(kTable + "<P>after</P>\n") != std::string::npos,
		"the table, then the paragraph's text:\n" + html);

	// a table alone in a paragraph: then the paragraph is empty
	wp->paragraphs[0].text = "#\n";
	html = HTMLText(doc, "t");
	Check(html.find(kTable + "<P>&nbsp;</P>\n") != std::string::npos,
		"the table and an empty paragraph:\n" + html);

	// a transparent pen: no border; without a frame showing it, no size
	doc.styles.properties[doc.styles.styles[tableFrame.style].props[2]] = Float(Sig("PTra"), 0);
	html = HTMLText(doc, "t");
	Check(html.find("<TABLE WIDTH=110 HEIGHT=40 BORDER=0 ") != std::string::npos,
		"no border with a transparent pen");
	doc.tails[D].portals.clear();
	wp->paragraphs[0].misc.clear();
	doc.sheets.sheets[0].mainPart = uint32_t(T);
	html = HTMLText(doc, "t");
	Check(html.find("<BODY BGCOLOR=\"#ffffff\">\n<TABLE HEIGHT=0 BORDER=0 CELLPADDING=3"
		" CELLSPACING=2><TR><TD WIDTH=50") != std::string::npos, "a table sheet:\n" + html);

	printf("html_table: ok %d, failed %d\n", sOk, sFailed);
	return sFailed ? 1 : 0;
}
