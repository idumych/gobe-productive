// HTML export: TExportHTML (writeHeader, writeBody, WriteWord, BeginParagraph,
// NextRun, PreCheckCharStyles, PostCheckCharStyles, convertToHTMLBuffer).
#include "HTMLExport.h"

#include <cmath>
#include <cstdio>
#include <fstream>

#include "DocArchive.h"
#include "Document.h"
#include "Fields.h"
#include "SSEval.h"
#include "Styles.h"
#include "WPPart.h"

namespace gobe {

namespace {

// ISOlat1table (the translator's, 0xa0 to 0xff; it names 0xa8 "die",
// which browsers don't know: "uml" here)
const char* const kLatin1[96] = {
	"nbsp", "iexcl", "cent", "pound", "curren", "yen", "brvbar", "sect",
	"uml", "copy", "ordf", "laquo", "not", "shy", "reg", "macr",
	"deg", "plusmn", "sup2", "sup3", "acute", "micro", "para", "middot",
	"cedil", "sup1", "ordm", "raquo", "frac14", "frac12", "frac34", "iquest",
	"Agrave", "Aacute", "Acirc", "Atilde", "Auml", "Aring", "AElig", "Ccedil",
	"Egrave", "Eacute", "Ecirc", "Euml", "Igrave", "Iacute", "Icirc", "Iuml",
	"ETH", "Ntilde", "Ograve", "Oacute", "Ocirc", "Otilde", "Ouml", "times",
	"Oslash", "Ugrave", "Uacute", "Ucirc", "Uuml", "Yacute", "THORN", "szlig",
	"agrave", "aacute", "acirc", "atilde", "auml", "aring", "aelig", "ccedil",
	"egrave", "eacute", "ecirc", "euml", "igrave", "iacute", "icirc", "iuml",
	"eth", "ntilde", "ograve", "oacute", "ocirc", "otilde", "ouml", "divide",
	"oslash", "ugrave", "uacute", "ucirc", "uuml", "yacute", "thorn", "yuml"
};


// convertToHTMLBuffer: '&', '<' and '>' as entities, a line break (0x01)
// as <BR>, other characters past ASCII as their names or numbers
std::string
Escaped(const std::string& text)
{
	std::string out;
	for (size_t i = 0; i < text.size();) {
		unsigned char c = (unsigned char)text[i];
		if (c < 0x80) {
			if (c == '&')
				out += "&amp;";
			else if (c == '<')
				out += "&lt;";
			else if (c == '>')
				out += "&gt;";
			else if (c == 0x01)
				out += "<BR>";
			else
				out += char(c);
			i++;
			continue;
		}
		// one UTF-8 character
		int length = c >= 0xf0 ? 4 : c >= 0xe0 ? 3 : c >= 0xc0 ? 2 : 1;
		uint32_t code = length == 1 ? c : c & (0x3f >> (length - 1));
		for (int k = 1; k < length && i + size_t(k) < text.size(); k++)
			code = code << 6 | ((unsigned char)text[i + size_t(k)] & 0x3f);
		i += size_t(length);
		char buffer[16];
		if (code >= 0xa0 && code <= 0xff)
			snprintf(buffer, sizeof buffer, "&%s;", kLatin1[code - 0xa0]);
		else
			snprintf(buffer, sizeof buffer, "&#%u;", code);
		out += buffer;
	}
	return out;
}


const Property*
Own(const StyleMgr& styles, uint32_t style, uint32_t type)
{
	if (style >= styles.styles.size())
		return nullptr;
	for (uint32_t p : styles.styles[style].props)
		if (p < styles.properties.size() && styles.properties[p].type == type)
			return &styles.properties[p];
	return nullptr;
}


std::string
ColorAttribute(const char* name, Color color)
{
	char buffer[48];
	snprintf(buffer, sizeof buffer, " %s=\"#%02x%02x%02x\"", name, color.r, color.g, color.b);
	return buffer;
}


// PreCheckCharStyles / PostCheckCharStyles: the run style's own size and
// color as <FONT>, its faces as tags; the closing tags in reverse
void
RunTags(const StyleMgr& styles, uint32_t style, std::string& open, std::string& close)
{
	open.clear();
	close.clear();
	const Property* color = Own(styles, style, Sig("TCol"));
	const Property* size = Own(styles, style, Sig("TSiz"));
	if (color != nullptr || size != nullptr) {
		open += "<FONT";
		if (color != nullptr) {
			TextStyle t;
			ApplyProperty(*color, t);
			open += ColorAttribute("COLOR", t.color);
		}
		if (size != nullptr) {
			TextStyle t;
			ApplyProperty(*size, t);
			int html = t.size <= 9 ? 1 : t.size <= 10 ? 2 : t.size <= 12 ? 3 : t.size <= 14 ? 4
				: t.size <= 18 ? 5 : t.size <= 24 ? 6 : 7;
			open += " SIZE=" + std::to_string(html);
		}
		open += ">";
		close = "</FONT>";
	}
	if (const Property* p = Own(styles, style, Sig("TFac"))) {
		// TTranslatorStyle::GetTextFace: the flags, or plain when they're
		// turned off
		DocArchive a(p->data);
		uint32_t flags = a.UInt32();
		bool on = a.Bool();
		uint32_t face = on ? flags : 0;
		const struct {
			uint32_t bits;
			const char* tag;
		} kTags[] = {{0x02, "B"}, {0x04, "I"}, {0x18, "U"}, {0x20, "STRIKE"}, {0x40, "SUP"},
			{0x80, "SUB"}};
		for (const auto& t : kTags)
			if (face & t.bits) {
				open += std::string("<") + t.tag + ">";
				close = std::string("</") + t.tag + ">" + close;
			}
	}
}


// WPTranslateTable: what control characters become in the translator's
// text (0x01 stays a line break, tabs stay, breaks end the line)
char
Translated(unsigned char c)
{
	static const uint8_t kTranslate[32] = {
		0x20, 0x01, 0x0a, 0x0a, 0x3f, 0x20, 0x20, 0x20,
		0x20, 0x09, 0x0a, 0x0a, 0x0a, 0x20, 0x20, 0x20,
		0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
		0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
	};
	return c < 0x20 ? char(kTranslate[c]) : char(c);
}


// WriteWord for one part
void
WriteWord(const Document& doc, size_t part, const WPPart& wp, SSBook& book, std::string& out)
{
	const StyleMgr& styles = doc.styles;
	for (const WPParagraph& p : wp.paragraphs) {
		// BeginParagraph: the paragraph style's own alignment
		std::string begin = "<P>";
		if (const Property* a = Own(styles, p.parStyle, Sig("PAlg"))) {
			TextStyle t;
			ApplyProperty(*a, t);
			static const char* const kAlign[4] = {"left", "center", "right", "justify"};
			if (t.alignment >= 0 && t.alignment < 4)
				begin = std::string("<P ALIGN=\"") + kAlign[t.alignment] + "\">";
		}
		std::string text = p.text;
		// the paragraph's own end (its LF) isn't part of the runs
		size_t end = text.size();
		while (end > 0 && (text[end - 1] == '\n' || text[end - 1] == 0x0c || text[end - 1] == 0x02
				|| text[end - 1] == 0x03 || text[end - 1] == 0x0b))
			end--;
		uint32_t firstStyle = p.runs.empty() ? kNoPart : p.runs[0].style;
		std::string open, close;
		if (end == 0 && p.misc.empty()) {
			// an empty paragraph: "&nbsp;" in its first run's style
			RunTags(styles, firstStyle, open, close);
			out += begin + open + "&nbsp;" + close + "</P>\n";
			continue;
		}
		out += begin;
		// NextRun: runs end where the character style changes or at a
		// special character (a paragraph info)
		size_t at = 0, run = 0;
		while (at < end) {
			while (run + 1 < p.runs.size() && p.runs[run + 1].position <= at)
				run++;
			const WPMiscInfo* special = nullptr;
			for (const WPMiscInfo& m : p.misc)
				if (m.position == at)
					special = &m;
			uint32_t style = run < p.runs.size() ? p.runs[run].style : kNoPart;
			RunTags(styles, style, open, close);
			if (special != nullptr) {
				// a separator line as a rule; fields as their text; frames
				// (pictures, tables) aren't written
				if (special->kind == Sig("LINE"))
					out += "\n<HR>\n";
				else if (special->kind != Sig("FRAM"))
					out += open + Escaped(FieldText(book, part, *special, -1)) + close;
				at++;
				continue;
			}
			size_t stop = end;
			if (run + 1 < p.runs.size() && p.runs[run + 1].position < stop)
				stop = p.runs[run + 1].position;
			for (const WPMiscInfo& m : p.misc)
				if (m.position > at && m.position < stop)
					stop = m.position;
			std::string segment;
			for (size_t i = at; i < stop; i++)
				segment += Translated((unsigned char)text[i]);
			out += open + Escaped(segment) + close;
			at = stop;
		}
		out += "</P>\n";
	}
}

} // namespace


std::string
HTMLText(const Document& doc, const std::string& title)
{
	std::string out = "<HTML>\n<HEAD>\n<TITLE>" + Escaped(title) + "</TITLE>\n</HEAD>\n<BODY";
	// writeBody: the "Text Background" style's own fill
	for (size_t i = 0; i < doc.styles.styles.size(); i++)
		if (doc.styles.styles[i].name == "Text Background") {
			if (const Property* fill = Own(doc.styles, uint32_t(i), Sig("FCol")))
				if (fill->data.size() >= 4) {
					Color c;
					c.r = fill->data[1];
					c.g = fill->data[2];
					c.b = fill->data[3];
					out += ColorAttribute("BGCOLOR", c);
				}
			break;
		}
	out += ">\n";
	SSBook book(doc);
	for (size_t s = 0; s < doc.SheetCount(); s++) {
		uint32_t main = doc.sheets.sheets[s].mainPart;
		if (const WPPart* wp = doc.WordPart(main))
			WriteWord(doc, main, *wp, book, out);
	}
	out += "</BODY>\n</HTML>\n";
	return out;
}


bool
ExportHTML(const Document& doc, const std::string& path, const std::string& title,
	std::string& error)
{
	std::string text = HTMLText(doc, title);
	std::ofstream file(path, std::ios::binary | std::ios::trunc);
	if (!file) {
		error = "cannot write " + path;
		return false;
	}
	file.write(text.data(), std::streamsize(text.size()));
	return bool(file);
}

} // namespace gobe
