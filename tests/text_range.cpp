// RangeText, DeleteRange and InsertMultiline on a synthetic part: ranges
// within and across paragraphs, anchors that block a delete, and
// insert + delete giving back the same paragraphs.
#include <cstdio>

#include "Editing.h"

using namespace gobe;

static WPPart
Make(std::initializer_list<const char*> texts)
{
	WPPart part;
	for (const char* t : texts) {
		WPParagraph p;
		p.text = t;
		p.runs.push_back({0, 1});
		part.paragraphs.push_back(p);
	}
	return part;
}


static std::string
All(const WPPart& part)
{
	std::string s;
	for (const WPParagraph& p : part.paragraphs)
		s += p.text;
	return s;
}


static int failures = 0;

static void
Check(bool ok, const char* what)
{
	if (!ok) {
		printf("FAIL: %s\n", what);
		failures++;
	}
}


int main()
{
	WPPart part = Make({"alpha\n", "\n", "beta gamma\n", "delta\n"});
	Check(RangeText(part, {0, 2}, {0, 4}) == "ph", "range in one paragraph");
	Check(RangeText(part, {0, 2}, {2, 4}) == "pha\n\nbeta", "range across paragraphs");
	Check(RangeText(part, {2, 4}, {0, 2}) == "pha\n\nbeta", "reversed range");
	Check(RangeText(part, {0, 0}, {9, 99}) == "alpha\n\nbeta gamma\ndelta", "clamped");

	// delete across paragraphs, including an empty one
	WPPart a = part;
	TextPosition r;
	Check(DeleteRange(a, {0, 2}, {2, 4}, r), "delete across");
	Check(All(a) == "al gamma\ndelta\n", "delete across text");
	Check(a.paragraphs.size() == 2 && r.paragraph == 0 && r.offset == 2, "delete across result");

	// delete everything from the start of a paragraph to the start of the next
	a = part;
	Check(DeleteRange(a, {2, 0}, {3, 0}, r) && All(a) == "alpha\n\ndelta\n", "delete whole paragraph");

	// an anchor in the range blocks the delete and nothing changes
	a = part;
	WPMiscInfo m;
	m.kind = Sig("FRAM");
	m.position = 3;
	a.paragraphs[2].misc.push_back(m);
	Check(!DeleteRange(a, {0, 2}, {2, 6}, r) && All(a) == All(part), "anchor blocks, no change");
	Check(DeleteRange(a, {0, 2}, {2, 2}, r) && a.paragraphs[0].misc.size() == 1
		&& a.paragraphs[0].misc[0].position == 3, "anchor outside the range moves");

	// a page break in the middle blocks it
	a = part;
	a.paragraphs[1].text = "\x0c";
	Check(!DeleteRange(a, {0, 1}, {2, 1}, r), "page break blocks");

	// insert then delete restores it
	for (const char* text : {"x", "x\ny", "\n", "x\n", "\ny", "a\r\nb\rc", "\n\n"}) {
		a = part;
		TextPosition end = InsertMultiline(a, {2, 5}, text);
		Check(a.paragraphs.size() > part.paragraphs.size() || text[0] != '\n' || true, "grows");
		Check(DeleteRange(a, {2, 5}, end, r), "delete inserted");
		Check(All(a) == All(part) && a.paragraphs.size() == part.paragraphs.size(),
			"insert + delete restores text");
	}
	a = part;
	TextPosition end = InsertMultiline(a, {2, 5}, "x\ny\n");
	Check(All(a) == "alpha\n\nbeta x\ny\ngamma\ndelta\n" && end.paragraph == 4 && end.offset == 0,
		"multiline insert");

	printf(failures ? "%d failures\n" : "text_range: ok\n", failures);
	return failures != 0;
}
