// Word Count.
#include "WordCount.h"

#include <algorithm>

#include "Document.h"

namespace gobe {

namespace {

// a UTF-8 character's length from its first byte
size_t
CharLength(const std::string& s, size_t at)
{
	uint8_t c = uint8_t(s[at]);
	size_t n = c < 0xc0 ? 1 : c < 0xe0 ? 2 : c < 0xf0 ? 3 : 4;
	return std::min(n, s.size() - at);
}


size_t
Previous(const std::string& s, size_t at)
{
	do
		at--;
	while (at > 0 && (uint8_t(s[at]) & 0xc0) == 0x80);
	return at;
}


// IsCharLetter(p, true): ASCII and Latin-1 letters (Latin1_CharClass bit 1;
// other characters beyond Latin-1 are not)
bool
IsLetter(const std::string& s, size_t at)
{
	uint8_t c = uint8_t(s[at]);
	if (c < 0x80)
		return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
	if (at + 1 >= s.size())
		return false;
	uint8_t next = uint8_t(s[at + 1]);
	if (c == 0xc2)	// ª µ º
		return next == 0xaa || next == 0xb5 || next == 0xba;
	if (c == 0xc3)	// À-ÿ but × ÷
		return (next | 0x60) != 0xf7;
	return false;
}


// Latin1_CharClass bit 2 (ASCII only)
bool
IsDigit(const std::string& s, size_t at)
{
	uint8_t c = uint8_t(s[at]);
	return c >= '0' && c <= '9';
}


bool
WordChar(const std::string& s, size_t at)
{
	return IsLetter(s, at) || IsDigit(s, at) || s[at] == '\'';
}


// GetNextWordInBlock: the next word from `at` (false if none); the
// characters it went over are counted
bool
NextWord(const std::string& s, size_t& at, size_t& length, int64_t& characters)
{
	while (at < s.size() && !IsLetter(s, at) && !IsDigit(s, at)) {
		at += CharLength(s, at);
		characters++;
	}
	if (at >= s.size())
		return false;
	size_t start = at, q = at;
	int letters = 0;
	bool dotted = false;
	while (q < s.size()) {
		char c = s[q];
		if (c == '-' || c == '.') {
			// between word characters only
			if (!WordChar(s, Previous(s, q)) || q + CharLength(s, q) >= s.size()
					|| !WordChar(s, q + CharLength(s, q)))
				break;
			dotted = dotted || c == '.';
		} else if (!WordChar(s, q))
			break;
		q += CharLength(s, q);
		letters++;
		characters++;
	}
	// an abbreviation takes its last period (not counted); a plural's
	// apostrophe is left for the next word (and counted again)
	if (dotted && q < s.size() && s[q] == '.')
		q++;
	if (letters > 1) {
		size_t last = Previous(s, q);
		if (s[last] == '\'' && last > start) {
			size_t before = Previous(s, last);
			if (s[before] == 's' || s[before] == 'S')
				q = last;
		}
	}
	length = q - start;
	at = start;
	return true;
}

} // namespace


void
CountBlock(const std::string& text, WordCount& count)
{
	// HasCJKChars(p, n, false, false): U+3040 up to U+A000
	for (size_t i = 0; i < text.size(); i += CharLength(text, i)) {
		uint8_t c = uint8_t(text[i]);
		if (c < 0xe0 || c >= 0xf0 || i + 2 >= text.size())
			continue;
		uint32_t cp = (c & 0x0f) << 12 | (uint8_t(text[i + 1]) & 0x3f) << 6
			| (uint8_t(text[i + 2]) & 0x3f);
		if (cp >= 0x3040 && cp < 0xa000)
			count.words++;
	}
	size_t at = 0, length;
	while (NextWord(text, at, length, count.characters)) {
		count.words++;
		at += length;
	}
}


WordCount
CountDocument(const Document& doc)
{
	WordCount count;
	for (const auto& [part, wp] : doc.wordParts) {
		// parts not shown (a removed header) aren't counted
		if (part >= doc.tails.size() || !doc.tailRead[part] || doc.tails[part].portals.empty())
			continue;
		for (const WPParagraph& p : wp.paragraphs) {
			CountBlock(p.text, count);
			count.paragraphs++;
		}
	}
	count.pages = std::max<int64_t>(1, int64_t(doc.PageCount()));
	return count;
}

} // namespace gobe
