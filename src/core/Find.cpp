// Find/Change.
#include "Find.h"

#include <algorithm>
#include <cstring>

#include "Document.h"
#include "Editing.h"
#include "WordCount.h"

namespace gobe {

namespace {

// a UTF-8 character's length by its first byte (0xe5000000 >> ... & 3)
size_t
LeadLength(uint8_t c)
{
	return ((0xe5000000u >> ((c >> 3) & 0x1e)) & 3) + 1;
}


// IsWordBreakChar
bool
IsWordBreak(char c)
{
	return uint8_t(c) < 0x31;
}

} // namespace


void
ToUpperUTF8(std::string& text)
{
	for (size_t i = 0; i < text.size(); i += LeadLength(uint8_t(text[i]))) {
		uint8_t c = uint8_t(text[i]);
		if (c < 0x80) {
			if (c >= 'a' && c <= 'z')
				text[i] = char(c - 0x20);
			continue;
		}
		if (LeadLength(c) != 2 || i + 1 >= text.size())
			continue;
		uint8_t b = uint8_t(text[i + 1]);
		auto down = [&]() { text[i + 1] = char(b - 1); };
		switch (c) {
			case 0xc3:
				if (b >= 0xa0 && b <= 0xbe && b != 0xb7)
					text[i + 1] = char(b - 0x20);
				else if (b == 0xbf) {
					text[i] = char(0xc5);
					text[i + 1] = char(0xb8);
				}
				break;
			case 0xc4:
				if (b >= 0x81 && b <= 0xbe && b != 0xb8)
					down();
				else if (b == 0xbf) {
					text[i] = char(0xc5);
					text[i + 1] = char(0x80);
				}
				break;
			case 0xc5:
				if (b >= 0x81 && b <= 0xbe && b != 0x89 && b != 0xb8)
					down();
				break;
			case 0xc7:
				if ((b >= 0x8e && b <= 0x9c) || (b >= 0x9f && b <= 0xaf) || (b >= 0xbb && b <= 0xbf))
					down();
				break;
			case 0xc8:
				if (b >= 0x81 && b <= 0x97)
					down();
				break;
		}
	}
}


bool
CharsMatchUTF8(const char* pattern, const char* text, bool caseSensitive)
{
	size_t n = LeadLength(uint8_t(pattern[0]));
	if (n != LeadLength(uint8_t(text[0])))
		return false;
	if (uint8_t(pattern[0]) >= 0x80) {
		if (caseSensitive)
			return memcmp(pattern, text, n) == 0;
		std::string upper(text, n);
		ToUpperUTF8(upper);
		return memcmp(pattern, upper.data(), n) == 0;
	}
	if (pattern[0] == text[0])
		return true;
	if (!caseSensitive) {
		uint8_t c = uint8_t(text[0]);
		return uint8_t(pattern[0]) == (c >= 'a' && c <= 'z' ? c - 0x20 : c);
	}
	return false;
}


// #pragma mark - FindReplace


void
FindReplace::SetFind(const std::string& text)
{
	if (text == find)
		return;
	find = text;
	fPattern = find;
	if (!caseSensitive)
		ToUpperUTF8(fPattern);
}


void
FindReplace::SetCaseSensitive(bool on)
{
	if (on == caseSensitive)
		return;
	caseSensitive = on;
	fPattern = find;
	if (!caseSensitive)
		ToUpperUTF8(fPattern);
}


bool
FindReplace::FindInBlock(const char* text, size_t length, char before, bool& continued,
	size_t& matched, long& at) const
{
	size_t n = fPattern.size();
	const char* pattern = fPattern.c_str();
	for (size_t start = 0; start < length; start++) {
		size_t i = continued ? matched : 0;
		size_t j = start;
		while (i < n && j < length) {
			if (!CharsMatchUTF8(pattern + i, text + j, caseSensitive))
				break;
			i += LeadLength(uint8_t(pattern[i]));
			j += LeadLength(uint8_t(text[j]));
		}
		char previous = start == 0 ? before : text[start - 1];
		if (i == n) {
			if (!wholeWord) {
				at = long(j) - long(n);
				return true;
			}
			bool open = continued || IsWordBreak(previous);
			bool close = j >= length || IsWordBreak(text[j]);
			if (open && close) {
				at = long(j) - long(n);
				return true;
			}
		} else if (j == length && i > 0
				&& CharsMatchUTF8(pattern + i - 1, text + length - 1, caseSensitive)) {
			// under way at the block's end
			if (!wholeWord || continued || IsWordBreak(previous)) {
				continued = true;
				matched = i;
				return false;
			}
		}
		continued = false;
	}
	return false;
}


// #pragma mark - TextAccessor


TextAccessor::TextAccessor(const Document& doc)
	:
	fDoc(&doc),
	fParts(ShownTextParts(doc))
{
}


const WPPart*
TextAccessor::CurrentPart() const
{
	return fPartIndex < fParts.size() ? fDoc->WordPart(fParts[fPartIndex]) : nullptr;
}


size_t
TextAccessor::Part() const
{
	return fPartIndex < fParts.size() ? fParts[fPartIndex] : 0;
}


const char*
TextAccessor::Text() const
{
	const WPPart* wp = CurrentPart();
	return wp != nullptr && fParagraph < wp->paragraphs.size()
		? wp->paragraphs[fParagraph].text.c_str() + fOffset : "";
}


char
TextAccessor::Before() const
{
	const WPPart* wp = CurrentPart();
	if (wp == nullptr || fParagraph >= wp->paragraphs.size())
		return 0;
	if (fOffset > 0)
		return wp->paragraphs[fParagraph].text[fOffset - 1];
	if (fParagraph == 0)
		return 0;
	const std::string& last = wp->paragraphs[fParagraph - 1].text;
	return last.empty() ? 0 : last.back();
}


uint32_t
TextAccessor::Position() const
{
	const WPPart* wp = CurrentPart();
	return wp != nullptr ? PartPosition(*wp, {fParagraph, fOffset}) : 0;
}


// TTextAccessor::StartFromTop: the first part with a block
bool
TextAccessor::StartFromTop()
{
	fStarted = fFromPosition = fWrap = fAtStartPart = fDone = false;
	for (fPartIndex = 0; fPartIndex < fParts.size(); fPartIndex++) {
		const WPPart* wp = CurrentPart();
		if (wp != nullptr && !wp->paragraphs.empty()) {
			fInPart = true;
			fBlock = 0;
			fParagraph = 0;
			fOffset = 0;
			fLength = wp->paragraphs[0].text.size();
			fStarted = true;
			return true;
		}
	}
	return false;
}


// TTextAccessor::StartFromPosition, TWPTextIterator::StartFromPosition:
// the block the position is in (its end too), from there
bool
TextAccessor::StartFromPosition(size_t part, uint32_t position, bool wrap)
{
	fStarted = fFromPosition = fAtStartPart = fDone = false;
	auto it = std::find(fParts.begin(), fParts.end(), part);
	if (it != fParts.end()) {
		fPartIndex = size_t(it - fParts.begin());
		const WPPart* wp = CurrentPart();
		uint32_t start = 0;
		for (size_t i = 0; wp != nullptr && i < wp->paragraphs.size(); i++) {
			size_t length = wp->paragraphs[i].text.size();
			if (position <= start + length) {
				fInPart = true;
				fParagraph = i;
				fBlock = long(i);
				fOffset = position - start;
				fLength = length - fOffset;
				fWrap = wrap;
				fStarted = fFromPosition = true;
				fStartPartIndex = fPartIndex;
				fStartBlock = fBlock;
				fStartLength = fLength;
				return true;
			}
			start += uint32_t(length);
		}
	}
	return StartFromTop();
}


bool
TextAccessor::NextBlock(bool& continues)
{
	if (!fStarted)
		return false;
	bool samePart = true;
	continues = false;
	while (true) {
		if (fFromPosition && fDone)
			break;
		if (!fInPart) {
			samePart = false;
			fPartIndex++;
			if (fPartIndex >= fParts.size()) {
				if (!(fFromPosition && fWrap))
					break;
				fPartIndex = 0;
			}
			if (fFromPosition && fPartIndex == fStartPartIndex)
				fAtStartPart = true;
			fInPart = true;
			fBlock = -1;
		}
		const WPPart* wp = CurrentPart();
		size_t next = fBlock < 0 ? 0 : fParagraph + 1;
		if (wp != nullptr && next < wp->paragraphs.size()) {
			fBlock++;
			fParagraph = next;
			fOffset = 0;
			fLength = wp->paragraphs[next].text.size();
			if (samePart)
				continues = true;
			if (!fFromPosition || !fAtStartPart || fBlock != fStartBlock)
				return true;
			// back at the start: its block up to the start
			fLength -= std::min(fLength, fStartLength);
			fDone = true;
			if (fLength > 0)
				return true;
			break;
		}
		samePart = false;
		fInPart = false;
	}
	fStarted = false;
	return false;
}


// #pragma mark - finding and replacing


bool
FindNext(const Document& doc, const FindReplace& find, bool fromPosition, size_t part,
	uint32_t position, FoundText& found)
{
	if (find.Pattern().empty())
		return false;
	TextAccessor accessor(doc);
	bool block = fromPosition ? accessor.StartFromPosition(part, position, find.wrap)
		: accessor.StartFromTop();
	bool continued = false, continues = false;
	size_t matched = 0;
	while (block) {
		long at;
		if (find.FindInBlock(accessor.Text(), accessor.Length(), accessor.Before(), continued,
				matched, at)) {
			found.part = accessor.Part();
			found.start = uint32_t(long(accessor.Position()) + at);
			found.end = found.start + uint32_t(find.find.size());
			return true;
		}
		block = accessor.NextBlock(continues);
		if (!continues) {
			continued = false;
			matched = 0;
		}
	}
	return false;
}


bool
ReplacePartText(Document& doc, size_t part, uint32_t start, uint32_t end,
	const std::string& text, FoundText& replaced)
{
	WPPart* wp = doc.EditableWordPart(part);
	if (wp == nullptr)
		return false;
	TextPosition from = FromPartPosition(*wp, start);
	TextPosition to = FromPartPosition(*wp, end);
	TextPosition at;
	if (!DeleteRange(*wp, from, to, at))
		return false;
	uint32_t begin = PartPosition(*wp, at);
	TextPosition after = InsertMultiline(*wp, at, text);
	replaced.part = part;
	replaced.start = begin;
	replaced.end = PartPosition(*wp, after);
	return true;
}


int
ReplaceEvery(Document& doc, const FindReplace& find, FoundText& last)
{
	if (find.Pattern().empty())
		return 0;
	int count = 0;
	TextAccessor accessor(doc);
	bool block = accessor.StartFromTop();
	bool continued = false, continues = false;
	size_t matched = 0;
	while (block) {
		long at;
		while (find.FindInBlock(accessor.Text(), accessor.Length(), accessor.Before(), continued,
				matched, at)) {
			uint32_t start = uint32_t(long(accessor.Position()) + at);
			FoundText replaced;
			if (!ReplacePartText(doc, accessor.Part(), start,
					start + uint32_t(find.find.size()), find.replace, replaced))
				return count;
			count++;
			last = replaced;
			continued = false;
			matched = 0;
			// on from the replacement (TTextAccessor::StartFromPosition)
			accessor = TextAccessor(doc);
			if (!accessor.StartFromPosition(replaced.part, replaced.end, false))
				return count;
		}
		block = accessor.NextBlock(continues);
		if (!continues) {
			continued = false;
			matched = 0;
		}
	}
	return count;
}


// #pragma mark - the dialog's text


std::string
FindToDisplay(const std::string& text)
{
	std::string out;
	for (char c : text) {
		switch (c) {
			case 0x01: out += "\\l"; break;
			case 0x02: out += "\\f"; break;
			case 0x04: out += "\\m"; break;
			case '\t': out += "\\t"; break;
			case '\n': out += "\\n"; break;
			case '\f': out += "\\p"; break;
			case '/': out += "\\/"; break;
			default: out += c; break;
		}
	}
	return out;
}


std::string
FindFromDisplay(const std::string& text, bool replaceField)
{
	std::string out;
	for (size_t i = 0; i < text.size(); i++) {
		if (text[i] != '\\' || i + 1 >= text.size()) {
			out += text[i];
			continue;
		}
		char c = text[i + 1];
		switch (c) {
			case '/': out += '/'; break;
			case 'f': out += char(0x02); break;
			case 'l': out += char(0x01); break;
			case 'm':
				if (replaceField)
					out += "\\m";
				else
					out += char(0x04);
				break;
			case 'n': out += '\n'; break;
			case 'p': out += '\f'; break;
			case 't': out += '\t'; break;
			default: out += text.substr(i, 2); break;
		}
		i++;
	}
	return out;
}


bool
FindHasSpecials(const std::string& text)
{
	for (char c : text)
		if (c == '\t' || c == 0x01 || c == '\n' || c == 0x02 || c == '\f' || c == 0x04)
			return true;
	return false;
}

} // namespace gobe
