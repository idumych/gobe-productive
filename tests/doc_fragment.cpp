// doc_fragment: copying and pasting styled text (CopyFragment,
// PasteFragment): the text with its runs' character properties, across
// paragraphs, into another document; field characters left out.
// Arguments: none.
#include <cstdio>
#include <string>

#include "Document.h"
#include "Editing.h"
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


static TextStyle
At(const Document& d, size_t paragraph, size_t offset)
{
	const WPParagraph& p = d.WordPart(0)->paragraphs[paragraph];
	uint32_t style = p.parStyle;
	for (const WPStyleRun& r : p.runs)
		if (r.position <= offset)
			style = r.style;
	return ResolveTextStyle(d.styles, style);
}


int
main()
{
	Document doc;
	std::string error;
	CreateBlankDocument(doc, error);
	WPPart* wp = doc.EditableWordPart(0);
	TextPosition at = InsertText(*wp, {0, 0}, "Hello bold world");
	at = SplitParagraph(*wp, at);
	InsertText(*wp, at, "Second");
	ApplyCharacterProperty(doc, 0, {0, 6}, {0, 10}, TextFaceProperty(2));
	ApplyCharacterProperty(doc, 0, {0, 11}, {0, 16}, TextSizeProperty(18));
	TextFragment f = CopyFragment(doc, 0, {0, 6}, {1, 3});
	Check(f.text == "bold world\nSec", "the text: \"" + f.text + "\"");
	bool bold = false, big = false;
	for (const TextFragment::Run& r : f.runs)
		for (const Property& p : r.properties) {
			TextStyle s;
			ApplyProperty(p, s);
			if (p.type == Sig("TFac") && s.faceFlags == 2)
				bold = bold || (r.paragraph == 0 && r.from == 0 && r.to == 4);
			if (p.type == Sig("TSiz") && s.size == 18)
				big = big || (r.paragraph == 0 && r.from == 5 && r.to == 10);
		}
	Check(bold && big, "the runs: bold [0, 4), 18 point [5, 10)");

	// pasted into another document, in the middle of a word
	Document other;
	CreateBlankDocument(other, error);
	InsertText(*other.EditableWordPart(0), {0, 0}, "ABCD");
	TextPosition after = PasteFragment(other, 0, {0, 2}, f);
	Check(after.paragraph == 1 && after.offset == 3, "after the paste");
	const WPPart* o = other.WordPart(0);
	Check(o->paragraphs.size() == 2 && o->paragraphs[0].text == "ABbold world\n"
		&& o->paragraphs[1].text == "SecCD", "the pasted text");
	Check(At(other, 0, 2).faceFlags == 2 && At(other, 0, 5).faceFlags == 2
		&& At(other, 0, 6).faceFlags != 2 && At(other, 0, 7).size == 18
		&& At(other, 0, 0).faceFlags != 2 && At(other, 0, 0).size == 12
		&& At(other, 1, 0).size == 12, "the pasted styles");
	Check(other.Save("/tmp/doc_fragment.gobe", error), "save " + error);
	Check(At(other, 0, 3).faceFlags == 2 && At(other, 0, 8).size == 18, "saved and read back");
	remove("/tmp/doc_fragment.gobe");

	// whole paragraphs take their paragraph formats along (all but the
	// first, which joins the paragraph pasted into)
	ApplyParagraphProperty(doc, 0, 1, 1, AlignmentProperty(1));
	f = CopyFragment(doc, 0, {0, 0}, {1, 6});
	Document third;
	CreateBlankDocument(third, error);
	PasteFragment(third, 0, {0, 0}, f);
	const WPPart* t = third.WordPart(0);
	Check(t->paragraphs.size() == 2
		&& ResolveTextStyle(third.styles, t->paragraphs[1].parStyle).alignment == 1
		&& ResolveTextStyle(third.styles, t->paragraphs[0].parStyle).alignment == 0,
		"the second paragraph comes centered");

	// a page number field isn't copied
	WPMiscInfo page{};
	page.kind = Sig("PAGE");
	InsertField(*doc.EditableWordPart(0), {1, 6}, page);
	f = CopyFragment(doc, 0, {1, 0}, {1, 7});
	Check(f.text == "Second", "without the field: \"" + f.text + "\"");
	printf("fragment ok %d, failed %d\n", sOk, sFailed);
	return sFailed > 0 ? 1 : 0;
}
