#include "TDocView.h"

#include <stdlib.h>
#include <string.h>

#include <LayoutBuilder.h>
#include <ScrollView.h>
#include <TextView.h>

#include "ResourceFile.h"

namespace gobe {

BFont
TDocView::MapFont(const TextStyle& style)
{
	const std::string& f = style.family;
	auto has = [&f](const char* s) { return f.find(s) != std::string::npos; };

	// BeOS's Bitstream fonts were Times (Dutch801), Helvetica (Swiss721) and
	// Courier (Courier10) designs; the Liberation fonts (pkgman install
	// liberation_fonts) have exactly their advance widths, so text breaks
	// where it did in Gobe. Noto when they aren't installed.
	enum { kSans, kSerif, kMono } kind = kSans;
	if (has("Courier") || has("Mono") || has("Monaco"))
		kind = kMono;
	else if (has("Dutch") || has("Baskerville") || has("Times") || has("Serif")
			|| has("Roman") || has("Bodoni") || has("Garamond") || has("Schoolbook")
			|| has("Georgia"))
		kind = kSerif;
	static const char* kPreferred[3][2] = {
		{"Liberation Sans", "Noto Sans"},
		{"Liberation Serif", "Noto Serif"},
		{"Liberation Mono", "Noto Sans Mono"}};
	static int sAvailable = -1;		// 1: the Liberation fonts are installed
	if (sAvailable < 0) {
		sAvailable = 0;
		int32 count = count_font_families();
		for (int32 i = 0; i < count; i++) {
			font_family name;
			uint32 flags;
			if (get_font_family(i, &name, &flags) == B_OK
				&& strcmp(name, "Liberation Sans") == 0) {
				sAvailable = 1;
				break;
			}
		}
	}
	const char* family = kPreferred[kind][sAvailable ? 0 : 1];

	// TPropTextFace: 1 = plain; otherwise style bits (bit 0 cleared)
	uint16 face = B_REGULAR_FACE;
	if (style.faceFlags != 1 && style.faceFlags != 0) {
		face = 0;
		if (style.faceFlags & 0x2)
			face |= B_BOLD_FACE;
		if (style.faceFlags & 0x4)
			face |= B_ITALIC_FACE;
		if (style.faceFlags & 0x8)
			face |= B_UNDERSCORE_FACE;
		if (style.faceFlags & 0x10)
			face |= B_STRIKEOUT_FACE;
		if (face == 0)
			face = B_REGULAR_FACE;
	}

	BFont font(be_plain_font);
	font.SetFamilyAndFace(family, face);
	font.SetSize(style.size > 0 ? style.size : 12);
	return font;
}


TDocView::TDocView(const Document& document)
	:
	BView("TDocView", 0)
{
	SetViewUIColor(B_PANEL_BACKGROUND_COLOR);

	fText = new BTextView("text");
	fText->SetStylable(true);
	fText->SetWordWrap(true);
	fText->SetInsets(24, 16, 24, 16);

	std::string all;
	struct Run {
		int32 offset;
		TextStyle style;
	};
	std::vector<Run> runs;

	for (const auto& [index, wp] : document.wordParts) {
		if (!all.empty()) {
			runs.push_back({(int32)all.size(), TextStyle()});
			all += "\n";
		}
		for (const WPParagraph& p : wp.paragraphs) {
			int32 base = (int32)all.size();
			std::string text = p.text;
			for (char& c : text)
				if (c == '\r')
					c = '\n';
			all += text;
			for (const WPStyleRun& r : p.runs) {
				if (r.position > p.length)
					continue;
				runs.push_back({base + (int32)r.position,
					ResolveTextStyle(document.styles, r.style)});
			}
		}
	}
	if (all.empty())
		all = "(this document has no word processing text)";

	text_run_array* array = (text_run_array*)malloc(sizeof(text_run_array)
		+ sizeof(text_run) * (runs.size() + 1));
	array->count = 0;
	for (const Run& r : runs) {
		text_run& tr = array->runs[array->count++];
		tr.offset = r.offset;
		tr.font = MapFont(r.style);
		tr.color = make_color(r.style.color.r, r.style.color.g, r.style.color.b);
	}
	if (array->count == 0) {
		array->runs[0].offset = 0;
		array->runs[0].font = *be_plain_font;
		array->runs[0].color = make_color(0, 0, 0);
		array->count = 1;
	}
	fText->SetText(all.c_str(), array);
	free(array);
	fText->MakeEditable(false);

	BScrollView* scroll = new BScrollView("scroll", fText, 0, false, true);
	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.Add(scroll);
}

} // namespace gobe
