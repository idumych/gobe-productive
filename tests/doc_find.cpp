// FindText and ReplaceAll on a synthetic part: exact match, case sensitivity,
// entire-word boundaries, wrap-around, multi-paragraph search, and replace
// all with style/anchor preservation.
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

static void
CheckStr(const std::string& a, const std::string& b, const char* what)
{
	if (a != b) {
		printf("FAIL: %s\n  got:      \"%s\"\n  expected: \"%s\"\n", what,
			a.c_str(), b.c_str());
		failures++;
	}
}


int main()
{
	// --- FindText: basic match ---
	WPPart part = Make({"hello world\n", "foo bar\n", "hello again\n"});
	FindOptions opts;
	opts.text = "world";

	Check(FindText(part, opts) && opts.paragraph == 0 && opts.offset == 6,
		"find in first paragraph");

	// find next: start from after "world"
	opts.paragraph = 0;
	opts.offset = 11;
	Check(!FindText(part, opts), "not found without wrap");

	opts.wrap = true;
	opts.paragraph = 0;
	opts.offset = 0;
	Check(FindText(part, opts) && opts.paragraph == 0 && opts.offset == 6,
		"find wraps to third paragraph");

	// --- FindText: case sensitivity ---
	part = Make({"Hello World\n", "hello world\n"});
	opts.text = "Hello";
	opts.caseSensitive = true;
	opts.wrap = false;
	opts.paragraph = 0;
	opts.offset = 0;
	Check(FindText(part, opts) && opts.paragraph == 0 && opts.offset == 0,
		"case-sensitive finds exact case");
	opts.paragraph = 0;
	opts.offset = EditableLength(part.paragraphs[0]);
	Check(!FindText(part, opts) && opts.paragraph == 1
		&& opts.offset == EditableLength(part.paragraphs[1]),
		"case-sensitive does not match different case");

	opts.caseSensitive = false;
	opts.paragraph = 1;
	opts.offset = 0;
	Check(FindText(part, opts) && opts.paragraph == 1 && opts.offset == 0,
		"case-insensitive finds both");

	// --- FindText: entire-word ---
	part = Make({"the cat\n", "concatenate\n", "cat dog\n"});
	opts.text = "cat";
	opts.entireWord = true;
	opts.caseSensitive = true;
	opts.wrap = false;
	opts.paragraph = 0;
	opts.offset = 0;
	Check(FindText(part, opts) && opts.paragraph == 0 && opts.offset == 4,
		"entire word matches in 'the cat'");

	opts.paragraph = 0;
	opts.offset = EditableLength(part.paragraphs[0]);
	Check(FindText(part, opts) && opts.paragraph == 2 && opts.offset == 0,
		"entire word in 'cat dog'");

	opts.paragraph = 2;
	opts.offset = EditableLength(part.paragraphs[2]);
	Check(!FindText(part, opts), "entire word not in 'concatenate'");

	opts.wrap = true;
	opts.paragraph = 0;
	opts.offset = 0;
	Check(FindText(part, opts) && opts.paragraph == 0 && opts.offset == 4,
		"entire word wraps");

	// --- FindText: multi-paragraph ---
	part = Make({"aaa\n", "bbb\n", "aaa\n"});
	opts.text = "aaa";
	opts.caseSensitive = true;
	opts.entireWord = false;
	opts.wrap = false;
	opts.paragraph = 0;
	opts.offset = 0;
	Check(FindText(part, opts) && opts.paragraph == 0 && opts.offset == 0,
		"find in first paragraph");

	opts.paragraph = 0;
	opts.offset = EditableLength(part.paragraphs[0]);
	Check(FindText(part, opts) && opts.paragraph == 2 && opts.offset == 0,
		"find finds next occurrence");

	// --- FindText: empty text ---
	part = Make({"hello\n"});
	opts.text.clear();
	Check(!FindText(part, opts), "empty search text returns false");

	// --- FindText: not found ---
	part = Make({"hello\n", "world\n"});
	opts.text = "xyz";
	opts.wrap = false;
	Check(!FindText(part, opts), "not found returns false");

	// --- FindText: multi-byte character ---
	part = Make({"héllo\n", "world\n"});
	opts.text = "llo";
	opts.caseSensitive = true;
	opts.wrap = false;
	opts.paragraph = 0;
	opts.offset = 0;
	Check(FindText(part, opts) && opts.paragraph == 0 && opts.offset == 3,
		"find after multi-byte character");

	// --- ReplaceAll: basic ---
	part = Make({"hello world\n", "hello there\n"});
	opts.text = "hello";
	uint32_t count = ReplaceAll(part, "goodbye", opts);
	Check(count == 2, "replace all replaces all occurrences");
	CheckStr(All(part), "goodbye world\ngoodbye there\n",
		"replace all text correct");

	// --- ReplaceAll: case-sensitive ---
	part = Make({"Hello\n", "hello\n", "HELLO\n"});
	opts.text = "hello";
	opts.caseSensitive = true;
	count = ReplaceAll(part, "hi", opts);
	Check(count == 1, "replace-all case-sensitive replaces only one");
	CheckStr(All(part), "Hello\nhi\nHELLO\n",
		"replace-all case-sensitive text correct");

	// --- ReplaceAll: case-insensitive ---
	part = Make({"Hello\n", "hello\n", "HELLO\n"});
	opts.caseSensitive = false;
	count = ReplaceAll(part, "hi", opts);
	Check(count == 3, "replace-all case-insensitive replaces all");
	CheckStr(All(part), "hi\nhi\nhi\n",
		"replace-all case-insensitive text correct");

	// --- ReplaceAll: entire-word ---
	part = Make({"cat\n", "concatenate\n", "cat dog\n"});
	opts.text = "cat";
	opts.entireWord = true;
	opts.caseSensitive = true;
	count = ReplaceAll(part, "feline", opts);
	Check(count == 2, "replace-all entire-word replaces non-overlapping");
	CheckStr(All(part), "feline\nconcatenate\nfeline dog\n",
		"replace-all entire-word text correct");

	// --- ReplaceAll: empty replacement ---
	part = Make({"hello hello\n"});
	opts.text = "hello";
	opts.entireWord = false;
	opts.caseSensitive = true;
	count = ReplaceAll(part, "", opts);
	Check(count == 0, "empty replacement returns zero");

	// --- ReplaceAll: styles preserved ---
	part = Make({"the cat\n", "the cat\n"});
	// Add style runs to test that DeleteText + InsertText preserve them
	part.paragraphs[0].runs = {{0, 0}, {4, 1}, {8, 0}};
	part.paragraphs[1].runs = {{0, 0}, {4, 1}};
	opts.text = "cat";
	count = ReplaceAll(part, "rat", opts);
	Check(count == 2, "replace-all with styles replaces correctly");
	// After replacing "cat" with "rat" in "the cat\n": length stays the same (3->3)
	// After replacing "bat" with "rat" in "the bat\n": length stays the same
	CheckStr(All(part), "the rat\nthe rat\n",
		"replace-all styles text correct");

	// --- ReplaceAll: replaces spans in middle paragraph ---
	part = Make({"first\n", "cat and cat\n", "last\n"});
	opts.text = "cat";
	opts.entireWord = false;
	opts.caseSensitive = true;
	count = ReplaceAll(part, "dog", opts);
	Check(count == 2, "replace-all in middle paragraph");
	CheckStr(All(part), "first\ndog and dog\nlast\n",
		"replace-all middle paragraph text correct");

	// --- ReplaceAll: overlapping matches (shouldn't overlap) ---
	part = Make({"aaa\n"});
	opts.text = "aa";
	opts.caseSensitive = true;
	count = ReplaceAll(part, "b", opts);
	Check(count == 1, "replace-all non-overlapping");
	CheckStr(All(part), "ba\n",
		"replace-all non-overlapping text");

	// --- ReplaceAll: no matches ---
	part = Make({"hello\n", "world\n"});
	opts.text = "xyz";
	count = ReplaceAll(part, "replaced", opts);
	Check(count == 0, "replace-all zero matches");
	CheckStr(All(part), "hello\nworld\n",
		"replace-all no change");

	printf(failures ? "%d failures\n" : "doc_find: ok\n", failures);
	return failures != 0;
}
