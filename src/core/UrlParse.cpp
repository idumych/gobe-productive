// ParseURL.
#include "UrlParse.h"

#include <cstdint>
#include <strings.h>

namespace gobe {

namespace {

// the sets (global constructors keyed to ParseURL); an opener and its
// closer are at the same byte offset of theirs (FUN_001d7c80)
const std::string kStartStop = " <[({\"'\xe2\x80\x98\xe2\x80\x9c\t\r\n";	// DAT_004595b8
const std::string kOpeners = "<[({\"'\xe2\x80\x98\xe2\x80\x9c`";			// DAT_004595b4
const std::string kEndStop = " >]))\"'\xe2\x80\x99\xe2\x80\x9d\t\r\n";		// DAT_004595b0
const std::string kClosers = ">])}\"'\xe2\x80\x99\xe2\x80\x9d'";			// DAT_004595ac
const std::string kBlanks = "\t ";											// DAT_004595a8
const std::string kSpace = " \t\r\n";										// DAT_004595a4


size_t
CharLength(uint8_t c)
{
	return c < 0xc0 ? 1 : c < 0xe0 ? 2 : c < 0xf0 ? 3 : 4;
}


// TString::FindChar: the byte offset in `set` of the character at `at`
// (npos if it isn't there)
size_t
FindChar(const std::string& set, const std::string& text, size_t at)
{
	if (at >= text.size())
		return std::string::npos;
	size_t n = CharLength(uint8_t(text[at]));
	for (size_t i = 0; i < set.size(); i += CharLength(uint8_t(set[i])))
		if (set.compare(i, CharLength(uint8_t(set[i])), text, at, n) == 0)
			return i;
	return std::string::npos;
}


bool
In(const std::string& set, const std::string& text, size_t at)
{
	return FindChar(set, text, at) != std::string::npos;
}


// FUN_001d7c80: the character an opener closes with, or a closer opens
// with ("" if neither)
std::string
Matching(const std::string& text, size_t at)
{
	size_t i = FindChar(kOpeners, text, at);
	const std::string* other = &kClosers;
	if (i == std::string::npos) {
		i = FindChar(kClosers, text, at);
		other = &kOpeners;
	}
	if (i == std::string::npos || i >= other->size())
		return "";
	return other->substr(i, CharLength(uint8_t((*other)[i])));
}


// FUN_001d7ce0: the run around a place
bool
Expand(const std::string& text, size_t& start, size_t& end)
{
	while (start != 0) {
		if (In(kStartStop, text, start - 1)) {
			if (In(kOpeners, text, start - 1))
				start--;
			break;
		}
		start--;
	}
	while (end < text.size()) {
		if (In(kEndStop, text, end)) {
			if (In(kClosers, text, end))
				end++;
			break;
		}
		end++;
	}
	if (In(kOpeners, text, start)) {
		// on to its closer
		std::string close = Matching(text, start);
		bool found = false;
		for (; end >= 1 && end - 1 < text.size(); end++)
			if (text.compare(end - 1, close.size(), close) == 0) {
				found = true;
				break;
			}
		if (!found)
			return false;
	}
	if (end != 0 && In(kClosers, text, end - 1)) {
		// back to its opener
		std::string open = Matching(text, end - 1);
		while (text.compare(start, open.size(), open) != 0) {
			if (start == 0)
				break;
			start--;
		}
	}
	return true;
}


// FUN_001d80e8: line breaks out, with the blanks before them and the
// spaces after
void
JoinLines(std::string& s)
{
	for (size_t i = 0; i < s.size(); i++) {
		size_t from = i;
		if (s[i] == '\r' || s[i] == '\n') {
			while (from > 0 && In(kBlanks, s, from - 1))
				from--;
			while (i < s.size() && In(kSpace, s, i))
				i++;
		}
		if (from < i) {
			s.erase(from, i - from);
			i = from + 1;
		}
	}
}


// FUN_001d81ac: a bracket or quote pair around it off
bool
StripPair(std::string& s)
{
	if (s.empty())
		return false;
	std::string match = Matching(s, 0);
	if (match.empty())
		return false;
	size_t last = s.size() - 1;
	while (last > 0 && (uint8_t(s[last]) & 0xc0) == 0x80)
		last--;
	// (strncmp for the last character's bytes: the run may end inside a
	// closing quote, a byte at a time)
	size_t n = s.size() - last;
	if (s.compare(last, n, match, 0, n) != 0)
		return false;
	size_t first = CharLength(uint8_t(s[0]));
	s = last > first ? s.substr(first, last - first) : "";
	return true;
}


// FUN_001d82a8: a ':' with a character on each side
bool
HasScheme(const std::string& s)
{
	for (size_t i = 0; i < s.size() && i < 0x100; i++)
		if (s[i] == ':' && i > 0 && int8_t(s[i - 1]) > ' ')
			return i + 1 < s.size() && int8_t(s[i + 1]) > ' ';
	return false;
}

} // namespace


bool
ParseURL(const std::string& text, size_t start, size_t end, std::string& url)
{
	if (start > text.size() || end > text.size() || end < start)
		return false;
	if (start == end && !Expand(text, start, end))
		return false;
	// FUN_001d8070
	while (start < text.size() && In(kBlanks, text, start))
		start++;
	while (end != 0 && In(kBlanks, text, end - 1))
		end--;
	url = end > start ? text.substr(start, end - start) : "";
	JoinLines(url);
	if (!StripPair(url) && !url.empty() && url.back() == '.')
		url.pop_back();
	if (strncasecmp(url.c_str(), "URL:", 4) == 0 || strncasecmp(url.c_str(), "URI:", 4) == 0)
		url.erase(0, 4);
	return HasScheme(url);
}

} // namespace gobe
