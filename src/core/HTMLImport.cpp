// HTML import: TImportHTML's parser (tags, attributes, entities, comments)
// and HTMLTree::doElementPreChildren / doElementPostChildren / doAttribute's
// text state, with ForceLineBreak and insertBlankLine.
#include "HTMLImport.h"

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

#include "Document.h"
#include "Editing.h"
#include "Styles.h"
#include "WPPart.h"

namespace gobe {

namespace {

// ISOlat1table and the ASCII entities
struct Entity {
	const char* name;
	uint32_t code;
};
const Entity kEntities[] = {
	{"quot", 34}, {"amp", 38}, {"lt", 60}, {"gt", 62}, {"nbsp", 160}, {"iexcl", 161},
	{"cent", 162}, {"pound", 163}, {"curren", 164}, {"yen", 165}, {"brvbar", 166},
	{"sect", 167}, {"die", 168}, {"uml", 168}, {"copy", 169}, {"ordf", 170}, {"laquo", 171},
	{"not", 172}, {"shy", 173}, {"reg", 174}, {"macr", 175}, {"deg", 176}, {"plusmn", 177},
	{"sup2", 178}, {"sup3", 179}, {"acute", 180}, {"micro", 181}, {"para", 182},
	{"middot", 183}, {"cedil", 184}, {"sup1", 185}, {"ordm", 186}, {"raquo", 187},
	{"frac14", 188}, {"frac12", 189}, {"frac34", 190}, {"iquest", 191}, {"Agrave", 192},
	{"Aacute", 193}, {"Acirc", 194}, {"Atilde", 195}, {"Auml", 196}, {"Aring", 197},
	{"AElig", 198}, {"Ccedil", 199}, {"Egrave", 200}, {"Eacute", 201}, {"Ecirc", 202},
	{"Euml", 203}, {"Igrave", 204}, {"Iacute", 205}, {"Icirc", 206}, {"Iuml", 207},
	{"ETH", 208}, {"Ntilde", 209}, {"Ograve", 210}, {"Oacute", 211}, {"Ocirc", 212},
	{"Otilde", 213}, {"Ouml", 214}, {"times", 215}, {"Oslash", 216}, {"Ugrave", 217},
	{"Uacute", 218}, {"Ucirc", 219}, {"Uuml", 220}, {"Yacute", 221}, {"THORN", 222},
	{"szlig", 223}, {"agrave", 224}, {"aacute", 225}, {"acirc", 226}, {"atilde", 227},
	{"auml", 228}, {"aring", 229}, {"aelig", 230}, {"ccedil", 231}, {"egrave", 232},
	{"eacute", 233}, {"ecirc", 234}, {"euml", 235}, {"igrave", 236}, {"iacute", 237},
	{"icirc", 238}, {"iuml", 239}, {"eth", 240}, {"ntilde", 241}, {"ograve", 242},
	{"oacute", 243}, {"ocirc", 244}, {"otilde", 245}, {"ouml", 246}, {"divide", 247},
	{"oslash", 248}, {"ugrave", 249}, {"uacute", 250}, {"ucirc", 251}, {"uuml", 252},
	{"yacute", 253}, {"thorn", 254}, {"yuml", 255}
};


void
AppendUTF8(std::string& out, uint32_t code)
{
	if (code < 0x80)
		out += char(code);
	else if (code < 0x800) {
		out += char(0xc0 | (code >> 6));
		out += char(0x80 | (code & 0x3f));
	} else if (code < 0x10000) {
		out += char(0xe0 | (code >> 12));
		out += char(0x80 | ((code >> 6) & 0x3f));
		out += char(0x80 | (code & 0x3f));
	} else {
		out += char(0xf0 | (code >> 18));
		out += char(0x80 | ((code >> 12) & 0x3f));
		out += char(0x80 | ((code >> 6) & 0x3f));
		out += char(0x80 | (code & 0x3f));
	}
}


// rgbTextToColor: "#rrggbb" (or without '#') or a color name
bool
ParseColor(const std::string& text, Color& color)
{
	static const struct {
		const char* name;
		uint8_t r, g, b;
	} kNames[] = {
		{"black", 0, 0, 0}, {"silver", 192, 192, 192}, {"gray", 128, 128, 128},
		{"white", 255, 255, 255}, {"maroon", 128, 0, 0}, {"red", 255, 0, 0},
		{"purple", 128, 0, 128}, {"fuchsia", 255, 0, 255}, {"green", 0, 128, 0},
		{"lime", 0, 255, 0}, {"olive", 128, 128, 0}, {"yellow", 255, 255, 0},
		{"navy", 0, 0, 128}, {"blue", 0, 0, 255}, {"teal", 0, 128, 128}, {"aqua", 0, 255, 255}
	};
	std::string lower;
	for (char c : text)
		lower += char(tolower((unsigned char)c));
	for (const auto& n : kNames)
		if (lower == n.name) {
			color.r = n.r;
			color.g = n.g;
			color.b = n.b;
			return true;
		}
	std::string hex = lower[0] == '#' ? lower.substr(1) : lower;
	if (hex.size() != 6 || hex.find_first_not_of("0123456789abcdef") != std::string::npos)
		return false;
	unsigned long v = strtoul(hex.c_str(), nullptr, 16);
	color.r = uint8_t(v >> 16);
	color.g = uint8_t(v >> 8);
	color.b = uint8_t(v);
	return true;
}


// HTMLTree's text state (+0x2d8 to +0x2f4, pushed per element)
struct State {
	std::string family = "Dutch801 Rm BT", face = "Roman";
	float size = 12;
	uint32_t faces = 1;			// TFac: 1 plain, 2 bold, 4 italic, 8 underline, ...
	Color color;
	float left = 0, first = 0;	// indents
	int alignment = 0;			// 0 left, 1 center, 2 right
	int listKind = 0;			// UL 1, OL 2 (the TYPE: 'A', 'a', 'I', 'i' or '1')
	char listType = '1';
	int listCount = 0;
	bool pre = false;
	bool hidden = false;		// inside HEAD, TITLE, SCRIPT, STYLE
};

// a run of text in the state it was added in
struct Run {
	std::string text;
	State state;
};

struct Paragraph {
	std::vector<Run> runs;
	int alignment = 0;
	float left = 0, first = 0;
	bool rule = false;			// a separator line (HR)
};


class Builder {
public:
	Builder() { fParagraphs.emplace_back(); }

	void Text(const std::string& text, const State& state)
	{
		if (state.hidden || text.empty())
			return;
		std::string out;
		for (char c : text) {
			if (!state.pre && isspace((unsigned char)c)) {
				// runs of white space as one space, none at a line's start
				if ((out.empty() && (fLineStart || fLastSpace)) || (!out.empty() && out.back() == ' '))
					continue;
				out += ' ';
			} else if (state.pre && c == '\n') {
				Append(out, state);
				out.clear();
				NewLine(state);
				continue;
			} else
				out += c;
		}
		Append(out, state);
	}

	// ForceLineBreak: a new paragraph unless at a line's start
	void LineBreak()
	{
		if (!fLineStart) {
			fParagraphs.emplace_back();
			fLineStart = true;
			fBlank = false;
			fLastSpace = false;
		}
	}

	// insertBlankLine: an empty paragraph before what follows (once)
	void BlankLine()
	{
		if (fBlank)
			return;
		LineBreak();
		if (!fParagraphs.empty() && fParagraphs.size() > 1) {
			fParagraphs.emplace_back();
		}
		fBlank = true;
	}

	// text as it is (list marks, cell tabs)
	void Raw(const std::string& text, const State& state)
	{
		if (!state.hidden)
			Append(text, state);
	}

	// BR: a line break inside the paragraph
	void SoftBreak(const State& state)
	{
		Append("\x01", state);
		fLineStart = true;
		fLastSpace = false;
	}

	void Rule()
	{
		LineBreak();
		fParagraphs.back().rule = true;
		fParagraphs.emplace_back();
		fLineStart = true;
		fBlank = false;
	}

	void NewLine(const State& state)
	{
		(void)state;
		fParagraphs.emplace_back();
		fLineStart = true;
		fBlank = false;
		fLastSpace = false;
	}

	std::vector<Paragraph>& Paragraphs() { return fParagraphs; }

private:
	void Append(const std::string& text, const State& state)
	{
		if (text.empty())
			return;
		Paragraph& p = fParagraphs.back();
		if (p.runs.empty()) {
			p.alignment = state.alignment;
			p.left = state.left;
			p.first = state.first;
		}
		p.runs.push_back({text, state});
		fLineStart = false;
		fBlank = false;
		fLastSpace = text.back() == ' ';
	}

	std::vector<Paragraph> fParagraphs;
	bool fLineStart = true;		// +0xbd
	bool fBlank = true;			// +0xbc (no blank line at the very start)
	bool fLastSpace = false;
};


std::string
Upper(std::string s)
{
	for (char& c : s)
		c = char(toupper((unsigned char)c));
	return s;
}


// the elements with no end tag (GIMappingTable's 'E')
bool
Empty(const std::string& name)
{
	static const char* const kEmpty[] = {"AREA", "BASE", "BASEFONT", "BR", "HR", "IMG", "INPUT",
		"ISINDEX", "LINK", "META", "PARAM"};
	for (const char* e : kEmpty)
		if (name == e)
			return true;
	return false;
}


// convertToRoman
std::string
Roman(int n, bool upper)
{
	static const struct {
		int value;
		const char* text;
	} kDigits[] = {{1000, "m"}, {900, "cm"}, {500, "d"}, {400, "cd"}, {100, "c"}, {90, "xc"},
		{50, "l"}, {40, "xl"}, {10, "x"}, {9, "ix"}, {5, "v"}, {4, "iv"}, {1, "i"}};
	std::string out;
	for (const auto& d : kDigits)
		while (n >= d.value) {
			out += d.text;
			n -= d.value;
		}
	return upper ? Upper(out) : out;
}


// the size a step up or down (BIG, SMALL; HTMLTree's 9, 10, 12, 14, 18,
// 24, 36)
float
SizeStep(float size, int step)
{
	static const float kSizes[] = {9, 10, 12, 14, 18, 24, 36};
	int at = 0;
	for (int i = 0; i < 7; i++)
		if (size >= kSizes[i])
			at = i;
	at = std::max(0, std::min(6, at + step));
	return kSizes[at];
}

} // namespace


bool
ImportHTMLText(const std::string& html, Document& doc, std::string& error)
{
	Builder out;
	State state;
	struct Open {
		std::string name;
		State saved;
	};
	std::vector<Open> open;
	Color linkColor;
	linkColor.b = 255;

	auto close = [&](const std::string& name) {
		// back to the element's start (if it's open), each post-children
		// effect on the way
		size_t at = open.size();
		while (at > 0 && open[at - 1].name != name)
			at--;
		if (at == 0)
			return;
		while (open.size() >= at) {
			std::string n = open.back().name;
			state = open.back().saved;
			open.pop_back();
			// doElementPostChildren: lists and paragraphs end their line;
			// headings and blocks too
			if (n == "P" || n == "UL" || n == "OL" || n == "MENU" || n == "DIR" || n == "DL"
					|| n == "CENTER" || n == "DIV" || n == "PRE" || n == "LISTING" || n == "XMP"
					|| n == "BLOCKQUOTE" || n == "ADDRESS" || (n.size() == 2 && n[0] == 'H'
						&& n[1] >= '1' && n[1] <= '6') || n == "TR" || n == "TABLE")
				out.LineBreak();
		}
	};

	size_t i = 0;
	std::string text;
	auto flush = [&]() {
		out.Text(text, state);
		text.clear();
	};
	while (i < html.size()) {
		char c = html[i];
		if (c == '&') {
			// an entity: named, &#n; or &#xh;, else the '&' itself
			size_t end = html.find_first_of("; \t\n\r<&", i + 1);
			std::string name = html.substr(i + 1, end == std::string::npos ? std::string::npos
				: end - i - 1);
			uint32_t code = 0;
			if (!name.empty() && name[0] == '#')
				code = uint32_t(name.size() > 1 && (name[1] == 'x' || name[1] == 'X')
					? strtoul(name.c_str() + 2, nullptr, 16) : strtoul(name.c_str() + 1, nullptr, 10));
			else
				for (const Entity& e : kEntities)
					if (name == e.name)
						code = e.code;
			if (code != 0) {
				AppendUTF8(text, code == 160 && !state.pre ? 0xa0 : code);
				i = end != std::string::npos && html[end] == ';' ? end + 1 : end;
			} else {
				text += '&';
				i++;
			}
			continue;
		}
		if (c != '<') {
			text += c;
			i++;
			continue;
		}
		// a comment, a declaration, a tag
		if (html.compare(i, 4, "<!--") == 0) {
			size_t end = html.find("-->", i + 4);
			i = end == std::string::npos ? html.size() : end + 3;
			continue;
		}
		if (i + 1 < html.size() && (html[i + 1] == '!' || html[i + 1] == '?')) {
			size_t end = html.find('>', i);
			i = end == std::string::npos ? html.size() : end + 1;
			continue;
		}
		size_t j = i + 1;
		bool ending = j < html.size() && html[j] == '/';
		if (ending)
			j++;
		size_t nameStart = j;
		while (j < html.size() && isalnum((unsigned char)html[j]))
			j++;
		if (j == nameStart) {
			text += c;		// a '<' that starts no tag
			i++;
			continue;
		}
		std::string name = Upper(html.substr(nameStart, j - nameStart));
		// attributes: NAME, NAME=value, NAME="value"
		std::vector<std::pair<std::string, std::string>> attributes;
		while (j < html.size() && html[j] != '>') {
			while (j < html.size() && (isspace((unsigned char)html[j]) || html[j] == '/'))
				j++;
			size_t a = j;
			while (j < html.size() && !isspace((unsigned char)html[j]) && html[j] != '='
					&& html[j] != '>')
				j++;
			std::string attribute = Upper(html.substr(a, j - a));
			std::string value;
			while (j < html.size() && isspace((unsigned char)html[j]))
				j++;
			if (j < html.size() && html[j] == '=') {
				j++;
				while (j < html.size() && isspace((unsigned char)html[j]))
					j++;
				if (j < html.size() && (html[j] == '"' || html[j] == '\'')) {
					char quote = html[j++];
					size_t end = html.find(quote, j);
					value = html.substr(j, end == std::string::npos ? std::string::npos : end - j);
					j = end == std::string::npos ? html.size() : end + 1;
				} else {
					size_t v = j;
					while (j < html.size() && !isspace((unsigned char)html[j]) && html[j] != '>')
						j++;
					value = html.substr(v, j - v);
				}
			}
			if (!attribute.empty())
				attributes.push_back({attribute, value});
		}
		i = j < html.size() ? j + 1 : html.size();
		auto attribute = [&](const char* key) -> const std::string* {
			for (const auto& a : attributes)
				if (a.first == key)
					return &a.second;
			return nullptr;
		};
		flush();
		if (ending) {
			close(name);
			continue;
		}
		// a P, LI, DT or DD closes the one open (InitMapping's closed
		// elements)
		if (name == "P" || name == "LI" || name == "DT" || name == "DD")
			for (size_t k = open.size(); k > 0; k--) {
				const std::string& n = open[k - 1].name;
				if (n == name || (name == "LI" && n == "LI") || ((name == "DT" || name == "DD")
						&& (n == "DT" || n == "DD"))) {
					close(n);
					break;
				}
				if (n == "UL" || n == "OL" || n == "DL" || n == "TD" || n == "TABLE")
					break;
			}
		State saved = state;
		// doElementPreChildren
		if (name == "B" || name == "STRONG")
			state.faces = (state.faces | 2) & ~1u;
		else if (name == "I" || name == "EM" || name == "CITE" || name == "VAR" || name == "DFN"
				|| name == "ADDRESS")
			state.faces = (state.faces | 4) & ~1u;
		else if (name == "U")
			state.faces = (state.faces | 8) & ~1u;
		else if (name == "STRIKE" || name == "S")
			state.faces = (state.faces | 0x20) & ~1u;
		else if (name == "SUB")
			state.faces = (state.faces | 0x80) & ~1u;
		else if (name == "SUP")
			state.faces = (state.faces | 0x40) & ~1u;
		else if (name == "TT" || name == "CODE" || name == "KBD" || name == "SAMP" || name == "PRE"
				|| name == "LISTING" || name == "XMP") {
			if (name == "PRE" || name == "LISTING" || name == "XMP") {
				out.BlankLine();
				state.pre = true;
			}
			state.family = "Courier10 BT";
			state.face = "Roman";
			state.size = 12;
			state.faces = 1;
			state.color = Color();
		} else if (name.size() == 2 && name[0] == 'H' && name[1] >= '1' && name[1] <= '6') {
			static const float kHeading[6] = {24, 18, 14, 12, 10, 9};
			out.BlankLine();
			state.faces = (state.faces | 2) & ~1u;
			state.size = kHeading[name[1] - '1'];
		} else if (name == "BIG")
			state.size = SizeStep(state.size, 1);
		else if (name == "SMALL")
			state.size = SizeStep(state.size, -1);
		else if (name == "CENTER") {
			out.LineBreak();
			state.alignment = 1;
		} else if (name == "P" || name == "DIV" || name == "BLOCKQUOTE" || name == "DL") {
			out.LineBreak();
			if (name == "BLOCKQUOTE")
				state.left += 36;
		} else if (name == "UL" || name == "OL" || name == "MENU" || name == "DIR") {
			out.BlankLine();
			state.left += 54;
			state.first += 36;
			state.listKind = name == "OL" ? 2 : 1;
			state.listCount = 1;
			const std::string* type = attribute("TYPE");
			state.listType = type != nullptr && !type->empty() ? (*type)[0] : '1';
		} else if (name == "LI") {
			out.LineBreak();
			// the item's mark: a bullet, or its number in the list's TYPE
			std::string mark;
			if (state.listKind == 2) {
				int n = state.listCount;
				char t = state.listType;
				mark = t == 'A' ? std::string(1, char('A' + (n - 1) % 26))
					: t == 'a' ? std::string(1, char('a' + (n - 1) % 26))
					: t == 'I' || t == 'i' ? Roman(n, t == 'I') : std::to_string(n);
				mark += ".\t";
			} else
				mark = "\xe2\x80\xa2\t";
			out.Raw(mark, state);
		} else if (name == "DD")
			state.left += 36;
		else if (name == "TD" || name == "TH") {
			// (tables aren't made: a tab between cells)
			out.Raw("\t", state);
			if (name == "TH")
				state.faces = (state.faces | 2) & ~1u;
		} else if (name == "TR")
			out.LineBreak();
		else if (name == "HEAD" || name == "TITLE" || name == "SCRIPT" || name == "STYLE")
			state.hidden = true;
		else if (name == "BR")
			out.SoftBreak(state);
		else if (name == "HR")
			out.Rule();
		else if (name == "IMG") {
			// (pictures aren't made: their ALT text)
			if (const std::string* alt = attribute("ALT"))
				out.Text(*alt, state);
		} else if (name == "BODY") {
			if (const std::string* link = attribute("LINK"))
				ParseColor(*link, linkColor);
		}
		// doAttribute
		if (const std::string* align = attribute("ALIGN")) {
			std::string a = Upper(*align);
			if (name == "P" || name == "DIV" || (name.size() == 2 && name[0] == 'H'))
				state.alignment = a == "CENTER" ? 1 : a == "RIGHT" ? 2 : 0;
		}
		if (name == "FONT") {
			if (const std::string* size = attribute("SIZE")) {
				int n = atoi(size->c_str());
				if (!size->empty() && ((*size)[0] == '+' || (*size)[0] == '-')) {
					static const float kSizes[] = {9, 10, 12, 14, 18, 24, 36};
					int current = 1;
					for (int k = 0; k < 7; k++)
						if (state.size >= kSizes[k])
							current = k + 1;
					n = current + n;
				}
				static const float kFontSizes[] = {9, 10, 12, 14, 18, 24, 36};
				state.size = kFontSizes[std::max(1, std::min(7, n)) - 1];
			}
			if (const std::string* color = attribute("COLOR"))
				ParseColor(*color, state.color);
		}
		if (name == "A" && attribute("HREF") != nullptr) {
			state.color = linkColor;
			state.faces = (state.faces | 8) & ~1u;
		}
		// (the next item's number: the state the item goes back to)
		if (name == "LI")
			saved.listCount++;
		if (!Empty(name))
			open.push_back({name, saved});
	}
	flush();

	// the document: the text, then each run's and paragraph's properties
	if (!CreateBlankDocument(doc, error))
		return false;
	size_t part = doc.wordParts.empty() ? 0 : doc.wordParts[0].first;
	WPPart* wp = doc.EditableWordPart(part);
	if (wp == nullptr) {
		error = "no word processing part";
		return false;
	}
	std::vector<Paragraph>& paragraphs = out.Paragraphs();
	while (!paragraphs.empty() && paragraphs.back().runs.empty() && !paragraphs.back().rule)
		paragraphs.pop_back();
	std::string all;
	for (size_t k = 0; k < paragraphs.size(); k++) {
		for (const Run& r : paragraphs[k].runs)
			all += r.text;
		if (k + 1 < paragraphs.size())
			all += '\n';
	}
	InsertMultiline(*wp, {0, 0}, all);
	State plain;
	for (size_t k = 0; k < paragraphs.size() && k < doc.WordPart(part)->paragraphs.size(); k++) {
		const Paragraph& p = paragraphs[k];
		size_t offset = 0;
		for (const Run& r : p.runs) {
			TextPosition from{k, offset}, to{k, offset + r.text.size()};
			offset += r.text.size();
			const State& s = r.state;
			if (s.family != plain.family || s.face != plain.face)
				ApplyCharacterProperty(doc, part, from, to, TextFontProperty(s.family, s.face));
			if (s.size != plain.size)
				ApplyCharacterProperty(doc, part, from, to, TextSizeProperty(s.size));
			if (s.faces != plain.faces)
				ApplyCharacterProperty(doc, part, from, to, TextFaceProperty(s.faces));
			if (s.color != plain.color)
				ApplyCharacterProperty(doc, part, from, to, TextColorProperty(s.color));
		}
		if (p.alignment != 0)
			ApplyParagraphProperty(doc, part, k, k, AlignmentProperty(p.alignment));
		if (p.left != 0)
			ApplyParagraphProperty(doc, part, k, k, IndentProperty(Sig("PLef"), p.left));
		if (p.first != 0)
			ApplyParagraphProperty(doc, part, k, k, IndentProperty(Sig("PInd"), p.first));
		if (p.rule) {
			WPMiscInfo line{};
			line.kind = Sig("LINE");
			InsertField(*doc.EditableWordPart(part), {k, 0}, line);
		}
	}
	return true;
}


bool
ImportHTML(const std::string& path, Document& doc, std::string& error)
{
	std::ifstream file(path, std::ios::binary);
	if (!file) {
		error = "cannot open " + path;
		return false;
	}
	std::string html((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
	return ImportHTMLText(html, doc, error);
}

} // namespace gobe
