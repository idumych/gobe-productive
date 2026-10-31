// doc_htmlin: HTML opened as text (ImportHTMLText, TImportHTML/HTMLTree):
// a page written by hand gives the paragraphs, faces, sizes, colors,
// alignment, list marks and indents, entities and white space the
// translator makes; every sample's text exported as HTML and read back
// gives the same words.
// Arguments: the sample documents.
#include <cstdio>
#include <string>

#include "Document.h"
#include "HTMLExport.h"
#include "HTMLImport.h"
#include "Fields.h"
#include "SSEval.h"
#include "Styles.h"
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


// the paragraph's text without its end
static std::string
Text(const WPPart& wp, size_t paragraph)
{
	if (paragraph >= wp.paragraphs.size())
		return "<none>";
	std::string t = wp.paragraphs[paragraph].text;
	while (!t.empty() && (t.back() == '\n' || t.back() == 0x0c))
		t.pop_back();
	return t;
}


// the style at an offset of a paragraph, resolved with its paragraph's
static TextStyle
StyleAt(const Document& doc, const WPPart& wp, size_t paragraph, size_t offset)
{
	const WPParagraph& p = wp.paragraphs[paragraph];
	uint32_t style = p.parStyle;
	for (const WPStyleRun& r : p.runs)
		if (r.position <= offset)
			style = r.style;
	TextStyle t = ResolveTextStyle(doc.styles, p.parStyle);
	TextStyle run = ResolveTextStyle(doc.styles, style);
	// the run's own character properties over the paragraph's
	if (style != p.parStyle) {
		const Style& s = doc.styles.styles[style];
		for (uint32_t prop : s.props)
			ApplyProperty(doc.styles.properties[prop], t);
	}
	(void)run;
	return t;
}


// the words of a text (letters and digits)
static std::string
Words(const std::string& text)
{
	std::string out;
	bool space = false;
	for (unsigned char c : text) {
		if (isalnum(c) || c >= 0xc3) {
			if (space && !out.empty())
				out += ' ';
			out += char(c);
			space = false;
		} else
			space = true;
	}
	return out;
}


int
main(int argc, char** argv)
{
	{
		const char* html =
			"<HTML><HEAD><TITLE>Ignored</TITLE><STYLE>p { x }</STYLE></HEAD>\n"
			"<BODY>\n<H1>Big   heading</H1>\n"
			"<P ALIGN=center>A <B>bold</B> and <I>leaning</I> word &amp; caf&eacute; &#169;</P>\n"
			"<P>Line one<BR>line two <FONT SIZE=5 COLOR=\"#ff0000\">red</FONT></P>\n"
			"<UL><LI>first<LI>second</UL>\n"
			"<OL TYPE=I><LI>alpha</LI><LI>beta</LI></OL>\n"
			"<!-- a comment <B>not bold</B> -->"
			"<PRE>  keep   spaces\nnext</PRE><HR><P>end</BODY></HTML>";
		Document doc;
		std::string error;
		Check(ImportHTMLText(html, doc, error), "import " + error);
		const WPPart* wp = doc.WordPart(size_t(doc.MainPart()));
		Check(wp != nullptr, "a text part");
		if (wp != nullptr) {
			for (size_t k = 0; k < wp->paragraphs.size(); k++)
				printf("  %zu: [%s]\n", k, Text(*wp, k).c_str());
			Check(Text(*wp, 0) == "Big heading", "the heading, its spaces collapsed: " + Text(*wp, 0));
			TextStyle h = StyleAt(doc, *wp, 0, 0);
			Check(h.size == 24 && (h.faceFlags & 2), "H1: 24 points, bold");
			size_t centered = 1;	// (a blank line goes before a heading, none after)
			Check(Text(*wp, centered) == "A bold and leaning word & caf\xc3\xa9 \xc2\xa9",
				"entities: " + Text(*wp, centered));
			Check(StyleAt(doc, *wp, centered, 0).alignment == 1, "ALIGN=center");
			Check(StyleAt(doc, *wp, centered, 2).faceFlags & 2, "bold");
			Check(StyleAt(doc, *wp, centered, 11).faceFlags & 4, "italic");
			Check(Text(*wp, 2) == "Line one\x01line two red", "a BR as a line break: " + Text(*wp, 2));
			TextStyle red = StyleAt(doc, *wp, 2, 18);
			Check(red.size == 18 && red.color.r == 255 && red.color.g == 0, "FONT SIZE=5 COLOR");
			// the lists (a blank line before each)
			size_t at = 3;
			while (at < wp->paragraphs.size() && Text(*wp, at).empty())
				at++;
			Check(Text(*wp, at) == "\xe2\x80\xa2\tfirst" && Text(*wp, at + 1) == "\xe2\x80\xa2\tsecond",
				"bullets: " + Text(*wp, at));
			Check(StyleAt(doc, *wp, at, 0).leftIndent == 54
				&& StyleAt(doc, *wp, at, 0).firstIndent == 36, "a list's indents");
			at += 2;
			while (at < wp->paragraphs.size() && Text(*wp, at).empty())
				at++;
			Check(Text(*wp, at) == "I.\talpha" && Text(*wp, at + 1) == "II.\tbeta",
				"roman numbers: " + Text(*wp, at) + " / " + Text(*wp, at + 1));
			at += 2;
			while (at < wp->paragraphs.size() && Text(*wp, at).empty())
				at++;
			Check(Text(*wp, at) == "  keep   spaces" && Text(*wp, at + 1) == "next", "PRE: "
				+ Text(*wp, at));
			Check(StyleAt(doc, *wp, at, 2).family == "Courier10 BT", "PRE in the fixed font");
			bool rule = false, end = false;
			for (size_t k = at; k < wp->paragraphs.size(); k++) {
				for (const WPMiscInfo& m : wp->paragraphs[k].misc)
					rule |= m.kind == Sig("LINE");
				end |= Text(*wp, k) == "end";
			}
			Check(rule && end, "HR a separator line, then the last paragraph");
			std::string all;
			for (size_t k = 0; k < wp->paragraphs.size(); k++)
				all += Text(*wp, k);
			Check(all.find("Ignored") == std::string::npos && all.find("not bold") == std::string::npos
				&& all.find("p {") == std::string::npos, "head, style and comments aren't text");
		}
	}
	int documents = 0;
	for (int i = 1; i < argc; i++) {
		Document doc;
		std::string error;
		if (!LoadDocument(argv[i], doc, error))
			continue;
		// (fields as their text, as the export writes them)
		std::string words;
		SSBook book(doc);
		for (size_t s = 0; s < doc.SheetCount(); s++)
			if (const WPPart* wp = doc.WordPart(doc.sheets.sheets[s].mainPart))
				for (const WPParagraph& p : wp->paragraphs) {
					std::string text = p.text;
					for (size_t m = p.misc.size(); m > 0; m--) {
						const WPMiscInfo& f = p.misc[m - 1];
						if (f.position < text.size() && f.kind != Sig("FRAM") && f.kind != Sig("LINE"))
							text.replace(f.position, 1, " " + FieldText(book,
								doc.sheets.sheets[s].mainPart, f, -1) + " ");
					}
					words += text + " ";
				}
		if (words.empty())
			continue;
		documents++;
		Document back;
		Check(ImportHTMLText(HTMLText(doc, "x"), back, error), std::string(argv[i]) + ": read back " + error);
		std::string again;
		if (const WPPart* wp = back.WordPart(size_t(back.MainPart())))
			for (const WPParagraph& p : wp->paragraphs)
				again += p.text + " ";
		// (fields come back as their text, '?' placeholders as letters)
		std::string a = Words(words), b = Words(again);
		size_t diff = 0;
		while (diff < a.size() && diff < b.size() && a[diff] == b[diff])
			diff++;
		Check(a == b, std::string(argv[i]) + ": the same words, apart at "
			+ std::to_string(diff) + ": [" + a.substr(diff, 40) + "] / [" + b.substr(diff, 40) + "]");
	}
	printf("doc_htmlin: documents %d, ok %d, failed %d\n", documents, sOk, sFailed);
	return sFailed != 0 || documents == 0;
}
