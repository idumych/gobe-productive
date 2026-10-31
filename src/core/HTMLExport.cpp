// HTML export: TExportHTML (writeHeader, writeBody, WriteWord, BeginParagraph,
// NextRun, PreCheckCharStyles, PostCheckCharStyles, convertToHTMLBuffer).
#include "HTMLExport.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>

#include <sys/stat.h>

#include "DocArchive.h"
#include "Document.h"
#include "Fields.h"
#include "PNG.h"
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


// what the export writes to: the text, the pictures (none collected
// when `images` is null), the file's name for their folder
struct Output {
	SSBook& book;
	std::string& out;
	std::string title;
	std::vector<HTMLImage>* images;
};

void WriteWord(const Document& doc, size_t part, const WPPart& wp, Output& o);


// the part of a word processing part's text from `start`, `length`
// characters (a table cell's frame): its paragraphs cut to it, their runs
// and specials moved with them
WPPart
TextRange(const WPPart& wp, uint32_t start, uint32_t length)
{
	WPPart out;
	uint32_t end = start + length, at = 0;
	for (const WPParagraph& p : wp.paragraphs) {
		uint32_t from = std::max(at, start), to = std::min(at + uint32_t(p.text.size()), end);
		if (from < to) {
			WPParagraph cut;
			cut.parStyle = p.parStyle;
			cut.text = p.text.substr(from - at, to - from);
			uint32_t offset = from - at;
			for (size_t r = 0; r < p.runs.size(); r++) {
				uint32_t next = r + 1 < p.runs.size() ? p.runs[r + 1].position
					: uint32_t(p.text.size());
				if (next <= offset || p.runs[r].position >= to - at)
					continue;
				cut.runs.push_back({p.runs[r].position > offset ? p.runs[r].position - offset : 0,
					p.runs[r].style});
			}
			for (const WPMiscInfo& m : p.misc)
				if (m.position >= offset && m.position < to - at) {
					cut.misc.push_back(m);
					cut.misc.back().position -= offset;
				}
			out.paragraphs.push_back(cut);
		}
		at += uint32_t(p.text.size());
	}
	return out;
}


// the first frame that shows `part` (TPart's first frame: TTableExport
// takes the table's size and style from it)
const Frame*
FirstFrameOf(const Document& doc, size_t part)
{
	for (size_t i = 0; i < doc.parts.parts.size(); i++)
		for (uint32_t k = 0;; k++) {
			const Frame* f = doc.FrameOf(uint32_t(i), k);
			if (f == nullptr)
				break;
			if (f->part == part)
				return f;
		}
	return nullptr;
}


// a style's own fill color (TTranslatorStyle::GetFillColor), as BGCOLOR
std::string
FillAttribute(const StyleMgr& styles, uint32_t style)
{
	const Property* fill = Own(styles, style, Sig("FCol"));
	if (fill == nullptr || fill->data.size() < 4 || fill->data[0] == 0)
		return "";
	Color c;
	c.r = fill->data[1];
	c.g = fill->data[2];
	c.b = fill->data[3];
	return ColorAttribute("BGCOLOR", c);
}


// TExportHTML::writeBitmap: the frame's picture as a file in the
// "<name>.images" folder ("image.N", from 0), then <IMG SRC WIDTH HEIGHT>.
// The original draws the frame into a bitmap and writes it as GIF or JPEG
// through the system's translators; the port writes an image part's
// bitmap (the rectangle the frame shows) as PNG, and nothing for other
// frames.
void
WritePicture(const Document& doc, const Frame& frame, Output& o)
{
	const IMPart* image = doc.ImagePart(frame.part);
	if (image == nullptr || o.images == nullptr)
		return;
	std::vector<uint8_t> png = BitmapPNG(image->image, frame.sheetRect);
	if (png.empty())
		return;
	int width = 0, height = 0;
	if (png.size() > 24) {
		width = png[16] << 24 | png[17] << 16 | png[18] << 8 | png[19];
		height = png[20] << 24 | png[21] << 16 | png[22] << 8 | png[23];
	}
	std::string name = "image." + std::to_string(o.images->size()) + ".png";
	o.images->push_back({name, png});
	char size[64];
	snprintf(size, sizeof size, "\" WIDTH=\"%i\" HEIGHT=\"%i\">", width, height);
	o.out += "<IMG SRC=\"" + o.title + ".images/" + name + size;
}


// TExportHTML::WriteTable, WriteTableRow, WriteTableCell, with what
// TTableExport::DoExport gives the translator: the first frame's width
// and height (WIDTH only when it isn't 0), a BORDER of its pen's size
// (none if the pen is transparent), the table's cell inset as
// CELLPADDING, CELLSPACING 2, the frame style's fill. Each cell: its
// width and height, COLSPAN and ROWSPAN 1, its text frame's fill, ALIGN
// from its first paragraph's own alignment or else the frame style's
// horizontal alignment, VALIGN from its vertical one; its text.
void
WriteTable(const Document& doc, size_t part, Output& o)
{
	std::string& out = o.out;
	const TablePart* table = doc.Table(part);
	if (table == nullptr)
		return;
	const StyleMgr& styles = doc.styles;
	char buffer[64];
	int width = 0, height = 0, border = 0;
	std::string fill;
	if (const Frame* frame = FirstFrameOf(doc, part)) {
		width = int(std::lround(frame->extent[0]));
		height = int(std::lround(frame->extent[1]));
		ShapeStyle shape = ResolveShapeStyle(styles, frame->style);
		if (shape.penOpacity > 0)
			border = shape.penSize != 1 ? int(std::lround(shape.penSize)) : 1;
		fill = FillAttribute(styles, frame->style);
	}
	out += "<TABLE";
	if (width != 0) {
		snprintf(buffer, sizeof buffer, " WIDTH=%d", width);
		out += buffer;
	}
	snprintf(buffer, sizeof buffer, " HEIGHT=%d BORDER=%d CELLPADDING=%d CELLSPACING=2", height,
		border, int(std::lround(table->value20)));
	out += buffer + fill + ">";
	const WPPart* text = doc.WordPart(table->textPart);
	for (const TableRow& row : table->rows) {
		out += "<TR>";
		for (const TableCell& cell : row.cells) {
			out += "<TD";
			int w = int(std::lround(cell.size[0])), h = int(std::lround(cell.size[1]));
			if (w != 0) {
				snprintf(buffer, sizeof buffer, " WIDTH=%d", w);
				out += buffer;
			}
			if (h != 0) {
				snprintf(buffer, sizeof buffer, " HEIGHT=%d", h);
				out += buffer;
			}
			out += " COLSPAN=1 ROWSPAN=1";
			const Frame* frame = doc.FrameOf(cell.frame[0], cell.frame[1]);
			WPPart cellText;
			if (frame != nullptr && text != nullptr)
				cellText = TextRange(*text, frame->textStart, frame->textLength);
			if (frame != nullptr) {
				out += FillAttribute(styles, frame->style);
				// 1 left, 2 center, 3 right (TTableCell's alignment)
				int align = 0, valign = 0;
				if (!cellText.paragraphs.empty())
					if (const Property* a = Own(styles, cellText.paragraphs[0].parStyle,
							Sig("PAlg"))) {
						TextStyle t;
						ApplyProperty(*a, t);
						align = t.alignment == 1 ? 2 : t.alignment == 2 ? 3 : 1;
					}
				auto alignment = [&](uint32_t type) {
					const Property* p = Own(styles, frame->style, type);
					if (p == nullptr || p->data.size() < 2)
						return 0;
					int v = int16_t(p->data[0] << 8 | p->data[1]);
					return v == 2 ? 2 : v == 3 ? 3 : 1;
				};
				if (align == 0)
					align = alignment(Sig("HAln"));
				valign = alignment(Sig("VAln"));
				if (align != 0)
					out += align == 2 ? " ALIGN=CENTER" : align == 3 ? " ALIGN=RIGHT" : " ALIGN=LEFT";
				if (valign != 0)
					out += valign == 2 ? " VALIGN=MIDDLE" : valign == 3 ? " VALIGN=BOTTOM"
						: " VALIGN=TOP";
			}
			out += ">";
			WriteWord(doc, table->textPart, cellText, o);
			out += "</TD>";
		}
		out += "</TR>";
	}
	out += "</TABLE>";
}


// WriteWord for one part (or a cell's text): NextRun's runs, cut at
// style changes and specials; a paragraph begins (<P>, BeginParagraph)
// with its first text, so a table at its start comes before it; a frame
// showing a table is written by WriteTable, other frames (pictures) not;
// a separator line as <HR>; a paragraph with nothing written as &nbsp;
void
WriteWord(const Document& doc, size_t part, const WPPart& wp, Output& o)
{
	std::string& out = o.out;
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
		bool begun = false;
		auto beginParagraph = [&]() {
			if (!begun)
				out += begin;
			begun = true;
		};
		std::string text = p.text;
		// the paragraph's own end (its LF) isn't part of the runs
		size_t end = text.size();
		while (end > 0 && (text[end - 1] == '\n' || text[end - 1] == 0x0c || text[end - 1] == 0x02
				|| text[end - 1] == 0x03 || text[end - 1] == 0x0b))
			end--;
		std::string open, close;
		size_t at = 0, run = 0;
		uint32_t style = p.runs.empty() ? kNoPart : p.runs[0].style;
		while (at < end) {
			while (run + 1 < p.runs.size() && p.runs[run + 1].position <= at)
				run++;
			const WPMiscInfo* special = nullptr;
			for (const WPMiscInfo& m : p.misc)
				if (m.position == at)
					special = &m;
			style = run < p.runs.size() ? p.runs[run].style : kNoPart;
			RunTags(styles, style, open, close);
			if (special != nullptr) {
				if (special->kind == Sig("LINE"))
					out += "\n<HR>\n";
				else if (special->kind == Sig("FRAM")) {
					const Frame* frame = doc.FrameOf(special->frame[0], special->frame[1]);
					if (frame != nullptr && doc.Table(frame->part) != nullptr)
						WriteTable(doc, frame->part, o);
					else {
						beginParagraph();
						if (frame != nullptr)
							WritePicture(doc, *frame, o);
					}
				} else {
					beginParagraph();
					out += open + Escaped(FieldText(o.book, part, *special, -1)) + close;
				}
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
			beginParagraph();
			out += open + Escaped(segment) + close;
			at = stop;
		}
		if (begun)
			out += "</P>\n";
		else {
			// an empty paragraph: "&nbsp;" in its last run's style
			RunTags(styles, style, open, close);
			out += begin + open + "&nbsp;" + close + "</P>\n";
		}
	}
}

} // namespace


std::string
HTMLText(const Document& doc, const std::string& title, std::vector<HTMLImage>* images)
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
	Output o{book, out, title, images};
	for (size_t s = 0; s < doc.SheetCount(); s++) {
		uint32_t main = doc.sheets.sheets[s].mainPart;
		if (const WPPart* wp = doc.WordPart(main))
			WriteWord(doc, main, *wp, o);
		else if (doc.Table(main) != nullptr)
			WriteTable(doc, main, o);
	}
	out += "</BODY>\n</HTML>\n";
	return out;
}


bool
ExportHTML(const Document& doc, const std::string& path, const std::string& title,
	std::string& error)
{
	std::vector<HTMLImage> images;
	std::string text = HTMLText(doc, title, &images);
	std::ofstream file(path, std::ios::binary | std::ios::trunc);
	if (!file) {
		error = "cannot write " + path;
		return false;
	}
	file.write(text.data(), std::streamsize(text.size()));
	if (!file)
		return false;
	if (images.empty())
		return true;
	// TExportHTML::CreateImagesDirectory: "<name>.images" next to the file
	size_t slash = path.rfind('/');
	std::string folder = (slash == std::string::npos ? std::string() : path.substr(0, slash + 1))
		+ title + ".images";
	mkdir(folder.c_str(), 0755);
	for (const HTMLImage& image : images) {
		std::ofstream picture(folder + "/" + image.name, std::ios::binary | std::ios::trunc);
		picture.write(reinterpret_cast<const char*>(image.bytes.data()),
			std::streamsize(image.bytes.size()));
		if (!picture) {
			error = "cannot write " + folder + "/" + image.name;
			return false;
		}
	}
	return true;
}

} // namespace gobe
