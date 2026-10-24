// text_url: ParseURL, the Open URL of a text part's context menu. The
// expected URLs follow the decompiled ParseURL by hand: a click expands to
// the run between spaces (an opening bracket kept, its closer reached);
// a bracket pair, else a final period, comes off; "URL:" comes off; a URL
// needs a ':' between two characters.
// Arguments: none.
#include <cstdio>
#include <string>

#include "UrlParse.h"

using namespace gobe;

static int sOk = 0, sFailed = 0;

static void
Expect(const std::string& text, size_t start, size_t end, bool ok, const std::string& expected)
{
	std::string url;
	bool result = ParseURL(text, start, end, url);
	if (result == ok && (!ok || url == expected))
		sOk++;
	else {
		sFailed++;
		printf("FAIL \"%s\" [%zu, %zu): %d \"%s\", expected %d \"%s\"\n", text.c_str(), start, end,
			result, url.c_str(), ok, expected.c_str());
	}
}


int
main()
{
	// a click in the middle of a URL; the period after it is a sentence's
	std::string s = "See http://www.gobe.com. Then";
	Expect(s, 10, 10, true, "http://www.gobe.com");
	// at its first character, at its end (the period)
	Expect(s, 4, 4, true, "http://www.gobe.com");
	Expect(s, 23, 23, true, "http://www.gobe.com");
	// a word without a colon
	Expect(s, 26, 26, false, "");
	// in angle brackets: kept with the run, then taken off as a pair
	Expect("mail <mailto:info@gobe.com> now", 10, 10, true, "mailto:info@gobe.com");
	// "URL:" comes off
	Expect("<URL:http://a.b/c>", 6, 6, true, "http://a.b/c");
	// a selection is taken as it is (blanks off its ends)
	Expect("go  ftp://x.y/z  there", 2, 17, true, "ftp://x.y/z");
	// a colon with a space after it
	Expect("Note: this", 2, 2, false, "");
	// parentheses: the closer reached brings the run back to its opener,
	// so the pair holds more than the URL and it has no colon left
	Expect("(see www.gobe.com)", 8, 8, false, "");
	Expect("(http://gobe.com)", 5, 5, true, "http://gobe.com");
	// curly quotes are a pair too
	Expect("\xe2\x80\x9chttp://q.r\xe2\x80\x9d", 6, 6, true, "http://q.r");
	// a line break inside a selection goes, with the blanks before it and
	// the spaces after it
	Expect("http://a.b/ \n  c", 0, 16, true, "http://a.b/c");
	// out of range
	Expect("abc", 2, 1, false, "");
	printf("text_url: ok %d, failed %d\n", sOk, sFailed);
	return sFailed > 0;
}
