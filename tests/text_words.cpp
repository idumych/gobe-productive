// text_words: the original's word rule (TParagraph::CharKind, WordBounds,
// SpaceBounds), for double clicks, Option/Control-arrow moves and the
// context menu's word. Expected kinds follow the decompiled CharKind's
// ranges: ASCII punctuation, U+2000-203F and U+3000-303F are 2, digits
// go with letters (0), blanks and breaks are 1, CJK ranges 3-7.
// Arguments: none.
#include <cstdio>
#include <string>

#include "Editing.h"

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


static void
Bounds(const std::string& text, size_t offset, bool before, size_t from, size_t to)
{
	size_t f, t;
	WordBounds(text, offset, before, f, t);
	Check(f == from && t == to, "\"" + text + "\" at " + std::to_string(offset)
		+ (before ? " (before)" : "") + ": [" + std::to_string(f) + ", " + std::to_string(t)
		+ "), expected [" + std::to_string(from) + ", " + std::to_string(to) + ")");
}


int
main()
{
	static const struct {
		const char* text;
		int kind;
	} kKinds[] = {
		{"a", 0}, {"5", 0}, {"\xc3\xa9", 0}, {",", 2}, {"-", 2}, {" ", 1}, {"\n", 1},
		{"\t", 1}, {"\x01", 1}, {"\xe2\x80\x94", 2}, {"\xe3\x80\x81", 2},
		{"\xe3\x81\x82", 5}, {"\xe3\x82\xa2", 6}, {"\xe6\xbc\xa2", 7}, {"\xef\xbc\xa1", 4},
		{"\xe3\x88\xb1", 3}, {"\xef\xa4\x80", 7}, {"\xe2\x82\xac", 0}
	};
	for (const auto& k : kKinds)
		Check(CharKind(k.text, 0) == k.kind, std::string("kind of \"") + k.text + "\": "
			+ std::to_string(CharKind(k.text, 0)) + ", expected " + std::to_string(k.kind));
	Check(CharKind("ab", 2) == 1, "past the end: 1");

	Bounds("hello, world", 2, false, 0, 5);
	Bounds("hello, world", 5, false, 5, 6);
	Bounds("hello, world", 6, false, 6, 7);
	Bounds("hello, world", 6, true, 5, 6);
	Bounds("a...b", 2, false, 1, 4);
	Bounds("abc12 x", 1, false, 0, 5);
	Bounds("word\n", 4, true, 0, 4);
	Bounds("word\n", 0, true, 0, 4);
	Bounds("  x", 0, true, 0, 2);

	size_t from, to;
	SpaceBounds("ab  cd", 3, from, to);
	Check(from == 2 && to == 4, "the blanks around 3");
	SpaceBounds("ab\t\x01" "cd", 2, from, to);
	Check(from == 2 && to == 4, "tabs and line breaks are blanks");
	printf("text_words: ok %d, failed %d\n", sOk, sFailed);
	return sFailed > 0;
}
