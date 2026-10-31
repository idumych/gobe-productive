// doc_html: word processing text as HTML (HTMLText, TExportHTML): a new
// document with text typed and formatted gives the translator's tags (a
// <P> per paragraph with its alignment, the runs' size, color and faces,
// entities, &nbsp; for an empty paragraph, <BR> for a line break); every
// sample's word processing sheets give balanced paragraphs and tags.
// Arguments: the sample documents.
#include <cstdio>
#include <string>

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


static int
Count(const std::string& text, const std::string& what)
{
	int n = 0;
	for (size_t at = text.find(what); at != std::string::npos; at = text.find(what, at + 1))
		n++;
	return n;
}


int
main(int argc, char** argv)
{
	{
		Document doc;
		std::string error;
		Check(CreateBlankDocument(doc, error), "blank " + error);
		WPPart* wp = doc.EditableWordPart(0);
		// "Hello Bold & <big>" / "" / "Café — end" with a line break
		TextPosition at = InsertMultiline(*wp, {0, 0}, "Hello Bold & <big>\n\nCaf\xc3\xa9 \xe2\x80\x94 x");
		(void)at;
		InsertText(*wp, {2, 5}, "\x01");	// after the 2-byte é
		ApplyCharacterProperty(doc, 0, {0, 6}, {0, 10}, TextFaceProperty(2));
		ApplyCharacterProperty(doc, 0, {0, 13}, {0, 18}, TextSizeProperty(18));
		Color red;
		red.r = 255;
		ApplyCharacterProperty(doc, 0, {0, 13}, {0, 18}, TextColorProperty(red));
		ApplyParagraphProperty(doc, 0, 2, 2, AlignmentProperty(1));
		std::string html = HTMLText(doc, "test.html");
		Check(html.find("<HTML>\n<HEAD>\n<TITLE>test.html</TITLE>\n</HEAD>\n") == 0,
			"the head");
		Check(html.find("<BODY BGCOLOR=\"#ffffff\">\n") != std::string::npos, "the body's color");
		Check(html.find("Hello <B>Bold</B> &amp; <FONT COLOR=\"#ff0000\" SIZE=5>&lt;big&gt;</FONT></P>\n")
			!= std::string::npos, "faces, size, color, entities");
		Check(html.find("<P>&nbsp;</P>\n") != std::string::npos, "an empty paragraph");
		Check(html.find("<P ALIGN=\"center\">Caf&eacute;<BR> &#8212; x</P>\n") != std::string::npos,
			"centered, a Latin-1 entity, a line break, a numeric entity");
		Check(html.size() > 20 && html.compare(html.size() - 16, 16, "</BODY>\n</HTML>\n") == 0,
			"the end");
	}
	int documents = 0, paragraphs = 0;
	for (int i = 1; i < argc; i++) {
		Document doc;
		std::string error;
		if (!LoadDocument(argv[i], doc, error))
			continue;
		size_t expected = 0;
		for (size_t s = 0; s < doc.SheetCount(); s++)
			if (const WPPart* wp = doc.WordPart(doc.sheets.sheets[s].mainPart))
				expected += wp->paragraphs.size();
		if (expected == 0)
			continue;
		documents++;
		std::string html = HTMLText(doc, argv[i]);
		int opened = Count(html, "<P>") + Count(html, "<P ALIGN=");
		paragraphs += opened;
		std::string name = argv[i];
		Check(size_t(opened) == expected && Count(html, "</P>") == opened, name + ": "
			+ std::to_string(opened) + " paragraphs of " + std::to_string(expected));
		for (const char* tag : {"B", "I", "U", "STRIKE", "SUP", "SUB", "FONT"})
			Check(Count(html, std::string("<") + tag + ">") + Count(html, std::string("<") + tag + " ")
				== Count(html, std::string("</") + tag + ">"), name + ": <" + tag + "> balanced");
		Check(html.find('\x01') == std::string::npos && html.find("&die;") == std::string::npos,
			name + ": no raw breaks");
	}
	printf("doc_html: documents %d, paragraphs %d, ok %d, failed %d\n", documents, paragraphs, sOk,
		sFailed);
	return sFailed != 0 || documents == 0;
}
