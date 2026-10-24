#include "Editing.h"

#include "StyleEdit.h"

#include "Styles.h"

#include <algorithm>
#include <map>
#include <set>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include "Document.h"
#include "SSEval.h"

namespace gobe {

// #pragma mark - text

namespace {

bool
IsTerminator(char c)
{
	return c == '\n' || c == '\r' || c == 0x02 || c == 0x05 || c == 0x0c;
}


// global text offset of a paragraph start
uint32_t
ParagraphStart(const WPPart& part, size_t paragraph)
{
	uint32_t start = 0;
	for (size_t i = 0; i < paragraph && i < part.paragraphs.size(); i++)
		start += uint32_t(part.paragraphs[i].text.size());
	return start;
}


void
ShiftSections(WPPart& part, uint32_t from, int64_t delta)
{
	for (WPSection& s : part.sections)
		if (s.start > from)
			s.start = uint32_t(std::max<int64_t>(from, int64_t(s.start) + delta));
}

} // namespace


size_t
EditableLength(const WPParagraph& p)
{
	size_t n = p.text.size();
	if (n > 0 && IsTerminator(p.text[n - 1]))
		n--;
	return n;
}


size_t
NextCharacter(const std::string& text, size_t offset)
{
	if (offset >= text.size())
		return text.size();
	offset++;
	while (offset < text.size() && (uint8_t(text[offset]) & 0xc0) == 0x80)
		offset++;
	return offset;
}


size_t
PreviousCharacter(const std::string& text, size_t offset)
{
	if (offset == 0)
		return 0;
	offset--;
	while (offset > 0 && (uint8_t(text[offset]) & 0xc0) == 0x80)
		offset--;
	return offset;
}


TextPosition
InsertText(WPPart& part, TextPosition at, const std::string& text)
{
	if (at.paragraph >= part.paragraphs.size() || text.empty())
		return at;
	WPParagraph& p = part.paragraphs[at.paragraph];
	at.offset = std::min(at.offset, EditableLength(p));
	p.text.insert(at.offset, text);
	uint32_t n = uint32_t(text.size());
	// text typed at a run boundary takes the style before it
	for (WPStyleRun& r : p.runs)
		if (r.position > at.offset || (r.position == at.offset && at.offset > 0))
			r.position += n;
	for (WPMiscInfo& m : p.misc)
		if (m.position >= at.offset)
			m.position += n;
	uint32_t global = ParagraphStart(part, at.paragraph) + uint32_t(at.offset);
	ShiftSections(part, global, n);
	part.edits.emplace_back(global, int64_t(n));
	at.offset += n;
	return at;
}


bool
DeleteText(WPPart& part, TextPosition from, size_t to)
{
	if (from.paragraph >= part.paragraphs.size())
		return false;
	WPParagraph& p = part.paragraphs[from.paragraph];
	to = std::min(to, EditableLength(p));
	if (from.offset >= to)
		return false;
	for (const WPMiscInfo& m : p.misc)
		if (m.position >= from.offset && m.position < to && !IsField(m))
			return false;
	// fields go with their characters
	for (auto it = p.misc.begin(); it != p.misc.end(); )
		if (it->position >= from.offset && it->position < to)
			it = p.misc.erase(it);
		else
			++it;
	uint32_t n = uint32_t(to - from.offset);
	p.text.erase(from.offset, n);
	for (WPStyleRun& r : p.runs) {
		if (r.position >= to)
			r.position -= n;
		else if (r.position > from.offset)
			r.position = uint32_t(from.offset);
	}
	// runs that became empty: keep the last of those at the same position
	for (size_t i = 0; i + 1 < p.runs.size();) {
		if (p.runs[i].position == p.runs[i + 1].position && p.runs.size() > 1)
			p.runs.erase(p.runs.begin() + i);
		else
			i++;
	}
	for (WPMiscInfo& m : p.misc)
		if (m.position >= to)
			m.position -= n;
	uint32_t global = ParagraphStart(part, from.paragraph) + uint32_t(from.offset);
	ShiftSections(part, global, -int64_t(n));
	part.edits.emplace_back(global, -int64_t(n));
	return true;
}


TextPosition
SplitParagraph(WPPart& part, TextPosition at)
{
	if (at.paragraph >= part.paragraphs.size())
		return at;
	WPParagraph& p = part.paragraphs[at.paragraph];
	at.offset = std::min(at.offset, EditableLength(p));
	WPParagraph next;
	next.value12 = p.value12;
	next.parStyle = p.parStyle;
	next.text = p.text.substr(at.offset);
	p.text = p.text.substr(0, at.offset) + "\n";
	// the run in effect at the split continues in the new paragraph
	uint32_t current = p.runs.empty() ? 0xffffffff : p.runs[0].style;
	std::vector<WPStyleRun> keep;
	for (const WPStyleRun& r : p.runs) {
		if (r.position <= at.offset) {
			// the style of the text before the split (not of the terminator)
			if (r.position < at.offset || at.offset == 0)
				current = r.style;
			if (r.position < at.offset || at.offset == 0)
				keep.push_back(r);
			else
				next.runs.push_back({0, r.style});
		} else
			next.runs.push_back({uint32_t(r.position - at.offset), r.style});
	}
	// typed text at the start of the new paragraph continues the style
	if (!p.runs.empty()) {
		if (!next.runs.empty() && next.runs[0].position == 0 && next.text.size() <= 1)
			next.runs[0].style = current;
		else if (next.runs.empty() || next.runs[0].position != 0)
			next.runs.insert(next.runs.begin(), {0, current});
	}
	if (keep.empty() && !p.runs.empty())
		keep.push_back({0, p.runs[0].style});
	p.runs = keep;
	std::vector<WPMiscInfo> stay;
	for (WPMiscInfo& m : p.misc) {
		if (m.position >= at.offset) {
			m.position -= uint32_t(at.offset);
			next.misc.push_back(std::move(m));
		} else
			stay.push_back(std::move(m));
	}
	p.misc = std::move(stay);
	// one more character (the new terminator): sections after it move by one
	uint32_t global = ParagraphStart(part, at.paragraph) + uint32_t(at.offset);
	ShiftSections(part, global, 1);
	part.edits.emplace_back(global, 1);
	part.paragraphs.insert(part.paragraphs.begin() + at.paragraph + 1, std::move(next));
	return {at.paragraph + 1, 0};
}


bool
JoinWithPrevious(WPPart& part, size_t paragraph, TextPosition& joined)
{
	if (paragraph == 0 || paragraph >= part.paragraphs.size())
		return false;
	WPParagraph& prev = part.paragraphs[paragraph - 1];
	WPParagraph& p = part.paragraphs[paragraph];
	// page, column and section breaks go like any character, as in the
	// original (TParagraph::DeleteText; sections keep their own starts)
	if (prev.text.empty() || !IsTerminator(prev.text.back()))
		return false;
	size_t base = prev.text.size() - 1;
	uint32_t globalBase = ParagraphStart(part, paragraph - 1) + uint32_t(base);
	prev.text.erase(base);
	prev.text += p.text;
	for (const WPStyleRun& r : p.runs) {
		if (r.position == 0 && !prev.runs.empty() && prev.runs.back().style == r.style)
			continue;
		prev.runs.push_back({uint32_t(r.position + base), r.style});
	}
	for (WPMiscInfo& m : p.misc) {
		m.position += uint32_t(base);
		prev.misc.push_back(std::move(m));
	}
	part.paragraphs.erase(part.paragraphs.begin() + paragraph);
	ShiftSections(part, globalBase, -1);
	part.edits.emplace_back(globalBase, -1);
	joined = {paragraph - 1, base};
	return true;
}


// Extracts plain text between two positions, handling paragraph boundaries and clamping.
std::string
RangeText(const WPPart& part, TextPosition from, TextPosition to)
{
	if (from.paragraph > to.paragraph || (from.paragraph == to.paragraph && from.offset > to.offset))
	{
		auto tmp = from; from = to; to = tmp;
	}
	const auto N = part.paragraphs.size();
	if (N == 0) return {};
	size_t f_p = std::min(from.paragraph, N - 1);
	size_t t_p = std::min(to.paragraph, N - 1);
	size_t f_o = std::min(from.offset, EditableLength(part.paragraphs[f_p]));
	size_t t_o = std::min(to.offset, EditableLength(part.paragraphs[t_p]));
	if (f_p == t_p)
		return part.paragraphs[f_p].text.substr(f_o, t_o - f_o);
	std::string res;
	for (size_t i = f_p; i <= t_p; ++i)
	{
		const auto& p = part.paragraphs[i];
		size_t start = (i == f_p) ? f_o : 0;
		size_t end = (i == t_p) ? t_o : EditableLength(p);
		res += p.text.substr(start, end - start);
		if (i < t_p) res += '\n';
	}
	return res;
}


// Deletes [from, to) across paragraphs; false (nothing changed) if it would
// remove a misc-info anchor or a break
bool
DeleteRange(WPPart& part, TextPosition from, TextPosition to, TextPosition& result)
{
	if (from.paragraph > to.paragraph
			|| (from.paragraph == to.paragraph && from.offset > to.offset))
		std::swap(from, to);
	const size_t count = part.paragraphs.size();
	if (from.paragraph >= count || to.paragraph >= count)
		return false;
	from.offset = std::min(from.offset, EditableLength(part.paragraphs[from.paragraph]));
	to.offset = std::min(to.offset, EditableLength(part.paragraphs[to.paragraph]));
	result = from;
	if (from.paragraph == to.paragraph && from.offset == to.offset)
		return true;

	// check everything first
	for (size_t i = from.paragraph; i <= to.paragraph; i++) {
		const WPParagraph& p = part.paragraphs[i];
		if (i < to.paragraph && (p.text.empty() || !IsTerminator(p.text.back())))
			return false;
		size_t a = i == from.paragraph ? from.offset : 0;
		size_t b = i == to.paragraph ? to.offset : EditableLength(p);
		for (const WPMiscInfo& m : p.misc)
			if (m.position >= a && m.position < b && !IsField(m))
				return false;
	}

	if (from.paragraph == to.paragraph)
		return DeleteText(part, from, to.offset);
	for (size_t i = to.paragraph; i > from.paragraph; i--) {
		size_t b = i == to.paragraph ? to.offset : EditableLength(part.paragraphs[i]);
		DeleteText(part, {i, 0}, b);
	}
	DeleteText(part, from, EditableLength(part.paragraphs[from.paragraph]));
	TextPosition joined;
	for (size_t i = to.paragraph; i > from.paragraph; i--)
		if (!JoinWithPrevious(part, i, joined))
			return false;
	return true;
}


// Inserts text that may contain line breaks, splitting paragraphs at them
TextPosition
InsertMultiline(WPPart& part, TextPosition at, const std::string& text)
{
	std::string piece;
	for (size_t i = 0; i <= text.size(); i++) {
		char c = i < text.size() ? text[i] : '\n';
		if (c == '\r' && i + 1 < text.size() && text[i + 1] == '\n')
			continue;
		if (c == '\n' || c == '\r') {
			at = InsertText(part, at, piece);
			piece.clear();
			if (i < text.size())
				at = SplitParagraph(part, at);
		} else
			piece += c;
	}
	return at;
}


uint32_t
LinkFor(Document& doc, size_t sourcePart, size_t reference, size_t dependentPart)
{
	for (size_t l = 0; l < doc.links.links.size(); l++) {
		const Link& k = doc.links.links[l];
		if (k.sourcePart == uint32_t(sourcePart) && k.reference == uint32_t(reference)
				&& k.dependentPart == uint32_t(dependentPart))
			return uint32_t(l);
	}
	Link k;
	k.value20 = 1;
	k.sourcePart = uint32_t(sourcePart);
	k.dependentPart = uint32_t(dependentPart);
	k.reference = uint32_t(reference);
	k.value1c = uint32_t(doc.links.links.size());
	doc.links.links.push_back(k);
	return uint32_t(doc.links.links.size() - 1);
}


bool
IsField(const WPMiscInfo& info)
{
	return info.kind == Sig("FORM") || info.kind == Sig("PAGE") || info.kind == Sig("LINE");
}


TextPosition
InsertField(WPPart& part, TextPosition at, WPMiscInfo field)
{
	if (at.paragraph >= part.paragraphs.size())
		return at;
	at.offset = std::min(at.offset, EditableLength(part.paragraphs[at.paragraph]));
	if (field.kind == Sig("LINE") && at.offset > 0)
		at = SplitParagraph(part, at);
	field.position = uint32_t(at.offset);
	TextPosition after = InsertText(part, at, "\x04");
	std::vector<WPMiscInfo>& misc = part.paragraphs[at.paragraph].misc;
	auto it = std::find_if(misc.begin(), misc.end(),
		[&](const WPMiscInfo& m) { return m.position > field.position; });
	misc.insert(it, std::move(field));
	return after;
}


TextPosition
InsertSpecialChar(WPPart& part, TextPosition at, char c)
{
	if (c != 0x02 && c != 0x05 && c != 0x0c)
		return InsertText(part, at, std::string(1, c));
	if (at.paragraph >= part.paragraphs.size())
		return at;
	TextPosition next = SplitParagraph(part, at);
	if (next.paragraph > at.paragraph)
		part.paragraphs[next.paragraph - 1].text.back() = c;
	return next;
}


namespace {

// 0: word character, 1: blank, 2: punctuation (a run by itself)
int
CharacterKind(char c)
{
	unsigned char u = (unsigned char)c;
	if (u >= 0x80 || isalnum(u) || u == '_')
		return 0;
	return u == ' ' || u == '\t' ? 1 : 2;
}

} // namespace


void
WordRange(const std::string& text, size_t offset, size_t& from, size_t& to)
{
	from = to = 0;
	if (text.empty())
		return;
	// the character at offset, or the one before at the end
	size_t start = offset >= text.size() ? PreviousCharacter(text, text.size()) : offset;
	int kind = CharacterKind(text[start]);
	from = start;
	to = NextCharacter(text, start);
	if (kind == 2)
		return;
	while (from > 0) {
		size_t previous = PreviousCharacter(text, from);
		if (CharacterKind(text[previous]) != kind)
			break;
		from = previous;
	}
	while (to < text.size() && CharacterKind(text[to]) == kind)
		to = NextCharacter(text, to);
}


// #pragma mark - find and replace


namespace {

// ASCII case-folding for a single byte; used when case-insensitive matching.
static inline char
FoldCase(char c)
{
	unsigned char u = (unsigned char)c;
	if (u >= 'A' && u <= 'Z')
		return c + ('a' - 'A');
	return c;
}

// Compare text[start..start+len) against target. If caseInsensitive is true,
// uses ASCII case-folding. Returns true on a match.
static bool
TextMatch(const std::string& text, size_t start, const std::string& target,
	bool caseInsensitive, size_t editableLength)
{
	if (start > editableLength)
		return false;
	if (start + target.size() > text.size())
		return false;
	for (size_t i = 0; i < target.size(); i++) {
		char a = caseInsensitive ? FoldCase(text[start + i]) : text[start + i];
		char b = caseInsensitive ? FoldCase(target[i]) : target[i];
		if (a != b)
			return false;
	}
	return true;
}

// Check if the character at position `pos` in paragraph `paragraph` of `part`
// is a word character (letter, digit, or underscore). Returns false if pos is
// at paragraph boundaries or on paragraph terminators/breaks.
static bool
IsWordChar(const WPPart& part, size_t paragraph, size_t pos)
{
	if (paragraph >= part.paragraphs.size())
		return false;
	const std::string& text = part.paragraphs[paragraph].text;
	if (pos >= text.size())
		return false;
	// Paragraph terminators and break characters are not word characters
	if (IsTerminator(text[pos]))
		return false;
	return CharacterKind(text[pos]) == 0;
}

} // namespace


bool
FindText(WPPart& part, FindOptions& options)
{
	if (options.text.empty())
		return false;

	size_t startPara = options.paragraph;
	size_t startOffset = options.offset;
	const bool caseInsensitive = !options.caseSensitive;

	// If we can't search the starting paragraph (offset past editable end),
	// skip it and set "not found" to the next paragraph instead.
	if (startPara < part.paragraphs.size()
			&& startOffset > EditableLength(part.paragraphs[startPara])) {
		// We can't search from the starting position — it's past the end.
		// "Not found" should point to the next paragraph (where searching would
		// resume).
		if (startPara + 1 < part.paragraphs.size()) {
			options.paragraph = startPara + 1;
			options.offset = EditableLength(part.paragraphs[startPara + 1]);
		} else {
			// Only one paragraph: we can't search it, so set to its end.
			options.paragraph = startPara;
			options.offset = EditableLength(part.paragraphs[startPara]);
		}
		if (!options.wrap)
			return false;
		// Continue to wrap-around section below.
	}

	// Search from the start position within each paragraph
	for (size_t para = startPara; para < part.paragraphs.size(); para++) {
		const WPParagraph& p = part.paragraphs[para];
		size_t from = (para == startPara) ? startOffset : 0;

		// Try to find the text in this paragraph
		size_t searchPos = from;
		size_t editableLen = EditableLength(p);
		while (searchPos <= editableLen) {
			if (TextMatch(p.text, searchPos, options.text, caseInsensitive,
					editableLen)) {
				// Check entire-word boundary if requested
				if (options.entireWord) {
					bool beforeOk = (searchPos == 0)
							|| !IsWordChar(part, para, searchPos - 1);
					bool afterOk = !IsWordChar(part, para,
						searchPos + options.text.size());
					if (beforeOk && afterOk) {
						options.paragraph = para;
						options.offset = searchPos;
						return true;
					}
				} else {
					options.paragraph = para;
					options.offset = searchPos;
					return true;
				}
			}
			searchPos++;
		}
	}

	// Wrap around: search from the beginning up to the start position.
	// Only applicable when we actually started from somewhere other than
	// the very beginning of the document.
	if (options.wrap && startPara > 0) {
		for (size_t para = 0; para < startPara; para++) {
			const WPParagraph& p = part.paragraphs[para];
			size_t to = (para == startPara - 1) ? startOffset
				: EditableLength(p);

			size_t searchPos = 0;
			size_t editableLen = EditableLength(p);
			while (searchPos <= editableLen) {
				if (TextMatch(p.text, searchPos, options.text, caseInsensitive,
						editableLen)) {
					if (options.entireWord) {
						bool beforeOk = (searchPos == 0)
								|| !IsWordChar(part, para, searchPos - 1);
						bool afterOk = !IsWordChar(part, para,
							searchPos + options.text.size());
						if (beforeOk && afterOk) {
							options.paragraph = para;
							options.offset = searchPos;
							return true;
						}
					} else {
						options.paragraph = para;
						options.offset = searchPos;
						return true;
					}
				}
				searchPos++;
			}
		}
	}

	// Not found: point to the paragraph after the last one we could search.
	if (startPara < part.paragraphs.size()) {
		if (startPara + 1 < part.paragraphs.size()) {
			options.paragraph = startPara + 1;
			options.offset = EditableLength(part.paragraphs[startPara + 1]);
		} else {
			options.paragraph = startPara;
			options.offset = EditableLength(part.paragraphs[startPara]);
		}
	} else {
		// startPara is beyond all paragraphs; set to last.
		options.paragraph = part.paragraphs.size() - 1;
		options.offset = EditableLength(
			part.paragraphs[part.paragraphs.size() - 1]);
	}
	return false;
}


uint32_t
ReplaceAll(WPPart& part, const std::string& replacement, const FindOptions& options)
{
	if (options.text.empty() || replacement.empty())
		return 0;

	const bool caseInsensitive = !options.caseSensitive;
	uint32_t count = 0;

	// Collect all match positions first (in order), then replace from the end
	// so that offsets don't shift.
	struct Match {
		size_t paragraph;
		size_t offset;
	};
	std::vector<Match> matches;

	for (size_t para = 0; para < part.paragraphs.size(); para++) {
		const WPParagraph& p = part.paragraphs[para];
		size_t searchPos = 0;
		size_t editableLen = EditableLength(p);
		while (searchPos <= editableLen) {
			if (TextMatch(p.text, searchPos, options.text, caseInsensitive, editableLen)) {
				if (options.entireWord) {
					bool beforeOk = (searchPos == 0) || !IsWordChar(part, para, searchPos - 1);
					bool afterOk = !IsWordChar(part, para, searchPos + options.text.size());
					if (!beforeOk || !afterOk) {
						searchPos++;
						continue;
					}
				}
				matches.push_back({para, searchPos});
				searchPos += options.text.size();
			} else {
				searchPos++;
			}
		}
	}

	// Replace from the end so offsets remain valid
	for (int32_t i = int32_t(matches.size()) - 1; i >= 0; i--) {
		const Match& m = matches[i];
		const WPParagraph& p = part.paragraphs[m.paragraph];
		size_t editableLen = EditableLength(p);
		if (m.offset + options.text.size() > editableLen)
			continue;

		TextPosition pos{m.paragraph, m.offset};

		if (!DeleteText(part, pos, m.offset + options.text.size()))
			continue;
		InsertText(part, pos, replacement);
		count++;
	}

	return count;
}


// #pragma mark - find/replace dialog API

// Alternative FindText: searches from `from`, forwards or backwards, with
// the new FindDialogOptions. Case-insensitive comparison uses ASCII case
// folding (non-ASCII bytes are compared literally). Matches never span
// paragraphs.
bool
FindText(const WPPart& part, TextPosition from, const std::string& query,
	const FindDialogOptions& options, TextPosition& start, TextPosition& end)
{
	if (query.empty() || part.paragraphs.empty())
		return false;

	const bool caseInsensitive = !options.caseSensitive;
	const bool backwards = options.backwards;

	// Collect all matches in the part, then pick the right one
	struct Match {
		size_t paragraph;
		size_t offset;
	};
	std::vector<Match> matches;

	for (size_t para = 0; para < part.paragraphs.size(); para++) {
		const WPParagraph& p = part.paragraphs[para];
		size_t editableLen = EditableLength(p);
		if (backwards) {
			// search backwards within this paragraph
			size_t pos = editableLen;
			while (pos > 0) {
				pos = PreviousCharacter(p.text, pos);
				if (TextMatch(p.text, pos, query, caseInsensitive, editableLen)) {
					if (options.wholeWord) {
						bool beforeOk = (pos == 0)
							|| !IsWordChar(part, para, pos - 1);
						bool afterOk = !IsWordChar(part, para,
							pos + query.size());
						if (!beforeOk || !afterOk) {
							pos = PreviousCharacter(p.text, pos);
							continue;
						}
					}
					matches.push_back({para, pos});
				}
				if (pos == 0)
					break;
			}
		} else {
			// search forwards within this paragraph
			size_t searchPos = 0;
			while (searchPos <= editableLen) {
				if (TextMatch(p.text, searchPos, query, caseInsensitive,
						editableLen)) {
					if (options.wholeWord) {
						bool beforeOk = (searchPos == 0)
							|| !IsWordChar(part, para, searchPos - 1);
						bool afterOk = !IsWordChar(part, para,
							searchPos + query.size());
						if (!beforeOk || !afterOk) {
							searchPos++;
							continue;
						}
					}
					matches.push_back({para, searchPos});
					searchPos += query.size();
				} else {
					searchPos++;
				}
			}
		}
	}

	if (matches.empty())
		return false;

	if (backwards) {
		// Find the last match strictly before `from`
		int bestIdx = -1;
		for (int i = int(matches.size()) - 1; i >= 0; i--) {
			const Match& m = matches[i];
			if (m.paragraph < from.paragraph
					|| (m.paragraph == from.paragraph && m.offset < from.offset)) {
				bestIdx = i;
				break;
			}
		}
		// If nothing before from and wrap is enabled, use the last match
		if (bestIdx < 0 && options.wrap && !matches.empty()) {
			bestIdx = int(matches.size()) - 1;
		}
		if (bestIdx < 0)
			return false;
		const Match& m = matches[bestIdx];
		start = {m.paragraph, m.offset};
		end = {m.paragraph, m.offset + query.size()};
		return true;
	} else {
		// Find the first match strictly after `from`
		int bestIdx = -1;
		for (int i = 0; i < int(matches.size()); i++) {
			const Match& m = matches[i];
			if (m.paragraph > from.paragraph
					|| (m.paragraph == from.paragraph && m.offset > from.offset)) {
				bestIdx = i;
				break;
			}
		}
		// If nothing after from and wrap is enabled, use the first match
		if (bestIdx < 0 && options.wrap && !matches.empty()) {
			bestIdx = 0;
		}
		if (bestIdx < 0)
			return false;
		const Match& m = matches[bestIdx];
		start = {m.paragraph, m.offset};
		end = {m.paragraph, m.offset + query.size()};
		return true;
	}
}


int
ReplaceAll(WPPart& part, const std::string& query, const std::string& replacement,
	const FindDialogOptions& options)
{
	if (query.empty() || replacement.empty())
		return 0;

	const bool caseInsensitive = !options.caseSensitive;
	int count = 0;

	for (size_t para = 0; para < part.paragraphs.size(); para++) {
		WPParagraph& p = part.paragraphs[para];
		size_t editableLen = EditableLength(p);
		size_t searchPos = 0;
		while (searchPos <= editableLen) {
			if (TextMatch(p.text, searchPos, query, caseInsensitive, editableLen)) {
				if (options.wholeWord) {
					bool beforeOk = (searchPos == 0) || !IsWordChar(part, para, searchPos - 1);
					bool afterOk = !IsWordChar(part, para, searchPos + query.size());
					if (!beforeOk || !afterOk) {
						searchPos++;
						continue;
					}
				}
				TextPosition pos{para, searchPos};
				if (DeleteText(part, pos, searchPos + query.size())) {
					InsertText(part, pos, replacement);
					count++;
					// update searchPos past the replacement
					searchPos += replacement.size();
				} else {
					searchPos++;
				}
				editableLen = EditableLength(p);
			} else {
				searchPos++;
			}
		}
	}

	return count;
}


// #pragma mark - styles

namespace {

bool
SameStyle(const Style& a, const Style& b)
{
	return a.id == b.id && a.value8 == b.value8 && a.valueC == b.valueC
		&& a.value10 == b.value10 && a.name == b.name && a.props == b.props
		&& a.props2 == b.props2 && a.substyles.empty() && b.substyles.empty()
		&& std::equal(a.links, a.links + 3, b.links);
}


// a PTab property's position (its first float)
float
TabPosition(const Property& property)
{
	TextStyle t;
	if (!ApplyProperty(property, t) || t.tabs.empty())
		return -1;
	return t.tabs[0].position;
}


// TProperty::Replaces: the same type; TPropParTab::Replaces: a tab at the
// same position
bool
Replaces(const Property& a, const Property& b)
{
	return a.type == b.type && (a.type != Sig("PTab") || TabPosition(a) == TabPosition(b));
}




// appends `props` to `set`, each replacing one it replaces
void
AppendProperties(const StyleMgr& styles, std::vector<uint32_t>& set,
	const std::vector<uint32_t>& props)
{
	for (uint32_t p : props) {
		if (p >= styles.properties.size())
			continue;
		set.erase(std::remove_if(set.begin(), set.end(), [&](uint32_t q) {
				return q < styles.properties.size()
					&& Replaces(styles.properties[p], styles.properties[q]);
			}), set.end());
		set.push_back(p);
	}
}


// TStyle::UpdateUnionSet: what the style inherits, its parents' inherited
// and own properties (the documents store it, and the original uses it
// as stored)
std::vector<uint32_t>
UnionSet(const StyleMgr& styles, const Style& style)
{
	std::vector<uint32_t> set;
	for (uint32_t link : style.links) {
		if (link >= styles.styles.size())
			continue;
		AppendProperties(styles, set, styles.styles[link].props2);
		AppendProperties(styles, set, styles.styles[link].props);
	}
	SortProps(styles, set);
	return set;
}


uint32_t
AddProperty(StyleMgr& styles, const Property& property)
{
	for (size_t i = 0; i < styles.properties.size(); i++)
		if (styles.properties[i].type == property.type
			&& styles.properties[i].data == property.data)
			return uint32_t(i);
	styles.properties.push_back(property);
	return uint32_t(styles.properties.size() - 1);
}


void
Retain(StyleMgr& styles, uint32_t style)
{
	if (style < styles.styles.size())
		styles.styles[style].value4++;
}


void
Release(StyleMgr& styles, uint32_t style)
{
	if (style < styles.styles.size() && styles.styles[style].value4 > 1)
		styles.styles[style].value4--;
}


// the run in effect at `offset`
size_t
RunAt(const WPParagraph& p, size_t offset)
{
	size_t run = 0;
	for (size_t i = 0; i < p.runs.size(); i++)
		if (p.runs[i].position <= offset)
			run = i;
	return run;
}

} // namespace


// TPropsTable keeps its properties sorted by type (tabs by position), and
// TPropsTable::Read takes them in the file's order: write them sorted
void
SortProps(const StyleMgr& styles, std::vector<uint32_t>& props)
{
	auto key = [&](uint32_t i) {
		if (i >= styles.properties.size())
			return std::make_pair(uint64_t(1) << 32, 0.0f);
		const Property& p = styles.properties[i];
		return std::make_pair(uint64_t(p.type), p.type == Sig("PTab") ? TabPosition(p) : 0.0f);
	};
	std::stable_sort(props.begin(), props.end(),
		[&](uint32_t a, uint32_t b) { return key(a) < key(b); });
}


std::vector<uint32_t>
StyleUnionSet(const StyleMgr& styles, const Style& style)
{
	return UnionSet(styles, style);
}


TextFragment
CopyFragment(const Document& doc, size_t part, TextPosition from, TextPosition to)
{
	TextFragment fragment;
	const WPPart* wp = doc.WordPart(part);
	if (to.paragraph < from.paragraph
			|| (to.paragraph == from.paragraph && to.offset < from.offset))
		std::swap(from, to);
	if (wp == nullptr || from.paragraph >= wp->paragraphs.size())
		return fragment;
	to.paragraph = std::min(to.paragraph, wp->paragraphs.size() - 1);
	for (size_t k = from.paragraph; k <= to.paragraph; k++) {
		const WPParagraph& p = wp->paragraphs[k];
		size_t length = EditableLength(p);
		size_t a = k == from.paragraph ? std::min(from.offset, length) : 0;
		size_t b = k == to.paragraph ? std::min(to.offset, length) : length;
		if (k > from.paragraph)
			fragment.text += '\n';
		fragment.paragraphs.push_back(ParagraphFormat(doc.styles, p.parStyle));
		// the paragraph's text without 0x04, and where each offset went
		std::vector<size_t> at(b - a + 1);
		std::string text;
		for (size_t i = a; i < b; i++) {
			at[i - a] = text.size();
			if (p.text[i] != 0x04)
				text += p.text[i];
		}
		at[b - a] = text.size();
		fragment.text += text;
		for (size_t r = 0; r < p.runs.size(); r++) {
			size_t start = std::max<size_t>(a, p.runs[r].position);
			size_t end = std::min(b, r + 1 < p.runs.size() ? size_t(p.runs[r + 1].position)
				: length);
			if (start >= end || at[start - a] >= at[end - a])
				continue;
			fragment.runs.push_back({k - from.paragraph, at[start - a], at[end - a],
				CharacterFormat(doc.styles, p.runs[r].style)});
		}
	}
	return fragment;
}


TextPosition
PasteFragment(Document& doc, size_t part, TextPosition at, const TextFragment& fragment)
{
	WPPart* wp = doc.EditableWordPart(part);
	if (wp == nullptr || at.paragraph >= wp->paragraphs.size())
		return at;
	at.offset = std::min(at.offset, EditableLength(wp->paragraphs[at.paragraph]));
	TextPosition after = InsertMultiline(*wp, at, fragment.text);
	for (size_t k = 1; k < fragment.paragraphs.size(); k++)
		for (const Property& property : fragment.paragraphs[k])
			ApplyParagraphProperty(doc, part, at.paragraph + k, at.paragraph + k, property);
	for (const TextFragment::Run& run : fragment.runs) {
		size_t paragraph = at.paragraph + run.paragraph;
		size_t base = run.paragraph == 0 ? at.offset : 0;
		for (const Property& property : run.properties)
			ApplyCharacterProperty(doc, part, {paragraph, base + run.from},
				{paragraph, base + run.to}, property);
	}
	return after;
}


namespace {

// a style's properties, own and inherited, of paragraph types or not
std::vector<Property>
StyleFormat(const StyleMgr& styles, uint32_t style, bool paragraphTypes)
{
	// ParagraphPropTypes: what TWPPartFactory::CreateProperty makes
	static const char* const kParagraph[17] = {"PLef", "PRig", "PInd", "PTab", "PAlg", "PWid",
		"LTog", "PNxt", "PgBB", "PHyp", "PChk", "PHid", "PExp", "LSpa", "SpaB", "SpaA", "PLab"};
	std::vector<Property> format;
	if (style >= styles.styles.size())
		return format;
	// its own properties, then what it inherits (the union set) and doesn't
	// set itself
	std::vector<uint32_t> all = styles.styles[style].props;
	for (uint32_t index : StyleUnionSet(styles, styles.styles[style])) {
		bool own = false;
		for (uint32_t mine : styles.styles[style].props)
			own = own || (index < styles.properties.size() && mine < styles.properties.size()
				&& Replaces(styles.properties[mine], styles.properties[index]));
		if (!own)
			all.push_back(index);
	}
	for (uint32_t index : all) {
		if (index >= styles.properties.size())
			continue;
		const Property& p = styles.properties[index];
		bool paragraph = false;
		for (const char* t : kParagraph)
			paragraph = paragraph || p.type == (uint32_t(uint8_t(t[0])) << 24
				| uint32_t(uint8_t(t[1])) << 16 | uint32_t(uint8_t(t[2])) << 8 | uint8_t(t[3]));
		if (paragraph == paragraphTypes)
			format.push_back(p);
	}
	return format;
}

} // namespace


std::vector<Property>
CharacterFormat(const StyleMgr& styles, uint32_t style)
{
	return StyleFormat(styles, style, false);
}


std::vector<Property>
ParagraphFormat(const StyleMgr& styles, uint32_t style)
{
	return StyleFormat(styles, style, true);
}


bool
PropsSorted(const StyleMgr& styles, const std::vector<uint32_t>& props)
{
	std::vector<uint32_t> sorted = props;
	SortProps(styles, sorted);
	return sorted == props;
}


uint32_t
StyleWithProperty(StyleMgr& styles, uint32_t base, const Property& property)
{
	if (base >= styles.styles.size() && base != 0xffffffff)
		return base;
	uint32_t index = AddProperty(styles, property);
	Style style;
	if (base == 0xffffffff) {
		// no style yet: a flat unnamed one, like the documents' cell styles
		style.id = styles.styles.empty() ? 0 : styles.styles[0].id;
		style.value8 = 32;
		style.valueC = 0xffff;
		style.value10 = Sig("Basi");
		style.links[0] = style.links[1] = style.links[2] = 0xffffffff;
		style.props = {index};
	} else if (styles.styles[base].name.empty()) {
		// a copy with the property replaced or added (TProperty::Replaces:
		// one of the same type; TPropParTab::Replaces: a tab at the same
		// position)
		style = styles.styles[base];
		bool replaced = false;
		for (uint32_t& p : style.props)
			if (p < styles.properties.size() && styles.properties[p].type == property.type
					&& (property.type != Sig("PTab")
						|| TabPosition(styles.properties[p]) == TabPosition(property))) {
				p = index;
				replaced = true;
			}
		if (!replaced)
			style.props.push_back(index);
	} else {
		// a named style: an unnamed child holding the property
		style = styles.styles[base];
		style.name.clear();
		style.value8 = 32;
		style.links[0] = base;
		style.props = {index};
	}
	SortProps(styles, style.props);
	style.props2 = UnionSet(styles, style);
	style.substyles.clear();
	for (size_t i = 0; i < styles.styles.size(); i++)
		if (SameStyle(styles.styles[i], style))
			return uint32_t(i);
	style.value4 = 1;
	Retain(styles, style.links[0]);
	styles.styles.push_back(style);
	return uint32_t(styles.styles.size() - 1);
}


bool
ApplyCharacterProperty(Document& doc, size_t part, TextPosition from, TextPosition to,
	const Property& property)
{
	if (to.paragraph < from.paragraph
			|| (to.paragraph == from.paragraph && to.offset < from.offset))
		std::swap(from, to);
	WPPart* wp = doc.EditableWordPart(part);
	if (wp == nullptr || from.paragraph >= wp->paragraphs.size())
		return false;
	to.paragraph = std::min(to.paragraph, wp->paragraphs.size() - 1);
	StyleMgr& styles = doc.styles;
	bool changed = false;
	for (size_t k = from.paragraph; k <= to.paragraph; k++) {
		WPParagraph& p = wp->paragraphs[k];
		if (p.runs.empty())
			continue;
		size_t length = EditableLength(p);
		size_t a = k == from.paragraph ? std::min(from.offset, length) : 0;
		size_t b = k == to.paragraph ? std::min(to.offset, length) : length;
		if (a >= b && !(a == 0 && length == 0))
			continue;
		// runs starting at a and b
		for (size_t at : {a, b}) {
			if (at >= length && at != 0)
				continue;
			size_t r = RunAt(p, at);
			if (p.runs[r].position != at) {
				p.runs.insert(p.runs.begin() + r + 1, {uint32_t(at), p.runs[r].style});
				Retain(styles, p.runs[r].style);
			}
		}
		for (WPStyleRun& run : p.runs) {
			if (run.position < a || (run.position >= b && b < length))
				continue;
			uint32_t style = StyleWithProperty(styles, run.style, property);
			if (style != run.style) {
				Retain(styles, style);
				Release(styles, run.style);
				run.style = style;
				changed = true;
			}
		}
		// neighbours alike: one run
		for (size_t i = 1; i < p.runs.size();) {
			if (p.runs[i].style == p.runs[i - 1].style) {
				Release(styles, p.runs[i].style);
				p.runs.erase(p.runs.begin() + i);
			} else
				i++;
		}
	}
	if (changed)
		doc.stylesChanged = true;
	return changed;
}


bool
ApplyCellProperty(Document& doc, size_t part, uint16_t row0, uint16_t column0,
	uint16_t row1, uint16_t column1, const Property& property)
{
	SSPart* ss = doc.EditableSpreadsheetPart(part);
	if (ss == nullptr)
		return false;
	if (row1 < row0)
		std::swap(row0, row1);
	if (column1 < column0)
		std::swap(column0, column1);
	StyleMgr& styles = doc.styles;
	bool changed = false;
	for (uint16_t column = column0; ; column++) {
		for (uint16_t row = row0; ; row++) {
			auto it = std::find_if(ss->cells.begin(), ss->cells.end(), [&](const SSCell& c) {
				return c.id.a == row && c.id.b == column;
			});
			if (it == ss->cells.end()) {
				SSCell cell;
				cell.id = {row, column};
				cell.kind = Sig("NONE");
				auto at = std::find_if(ss->cells.begin(), ss->cells.end(),
					[&](const SSCell& c) {
						return std::make_pair(c.id.b, c.id.a) > std::make_pair(column, row);
					});
				it = ss->cells.insert(at, cell);
			}
			uint32_t style = StyleWithProperty(styles, it->style, property);
			if (style != it->style) {
				Retain(styles, style);
				Release(styles, it->style);
				it->style = style;
				changed = true;
			}
			if (row == row1)
				break;
		}
		if (column == column1)
			break;
	}
	if (changed) {
		doc.stylesChanged = true;
		ss->used.to.a = std::max(ss->used.to.a, row1);
		ss->used.to.b = std::max(ss->used.to.b, column1);
	}
	return changed;
}


bool
ApplyParagraphProperty(Document& doc, size_t part, size_t first, size_t last,
	const Property& property)
{
	WPPart* wp = doc.EditableWordPart(part);
	if (wp == nullptr || first >= wp->paragraphs.size())
		return false;
	if (last < first)
		std::swap(first, last);
	last = std::min(last, wp->paragraphs.size() - 1);
	bool changed = false;
	for (size_t k = first; k <= last; k++) {
		WPParagraph& p = wp->paragraphs[k];
		uint32_t style = StyleWithProperty(doc.styles, p.parStyle, property);
		if (style != p.parStyle) {
			Retain(doc.styles, style);
			Release(doc.styles, p.parStyle);
			p.parStyle = style;
			changed = true;
		}
	}
	if (changed)
		doc.stylesChanged = true;
	return changed;
}


uint32_t
NewNamedStyle(Document& doc, uint32_t type, const std::string& name, uint32_t base,
	uint32_t fromStyle)
{
	if (name.empty())
		return kNoPart;
	// (TStyleManager::FindNamedStyle: not a cleared one's, flag 0x400)
	for (const Style& s : doc.styles.styles)
		if (s.name == name && !(s.value8 & 0x400))
			return kNoPart;
	// (its own and inherited properties: the character ones and the
	// paragraph ones)
	std::vector<Property> properties;
	if (fromStyle < doc.styles.styles.size()) {
		properties = CharacterFormat(doc.styles, fromStyle);
		for (const Property& p : ParagraphFormat(doc.styles, fromStyle))
			properties.push_back(p);
	}
	return doc.NewStyle(type, name, 0x822, base < doc.styles.styles.size() ? base : kNoPart,
		properties);
}


bool
ApplyParagraphStyle(Document& doc, size_t part, size_t first, size_t last, uint32_t named,
	bool unapply)
{
	WPPart* wp = doc.EditableWordPart(part);
	if (wp == nullptr || first >= wp->paragraphs.size() || named >= doc.styles.styles.size()
			|| doc.styles.styles[named].name.empty())
		return false;
	if (last < first)
		std::swap(first, last);
	last = std::min(last, wp->paragraphs.size() - 1);
	bool changed = false;
	for (size_t k = first; k <= last; k++) {
		WPParagraph& p = wp->paragraphs[k];
		uint32_t style = StyleWithAppliedStyle(doc.styles, p.parStyle, named, unapply);
		if (style != p.parStyle) {
			Retain(doc.styles, style);
			Release(doc.styles, p.parStyle);
			p.parStyle = style;
			changed = true;
		}
	}
	if (changed)
		doc.stylesChanged = true;
	return changed;
}


bool
RemoveParagraphTab(Document& doc, size_t part, size_t first, size_t last, float position)
{
	WPPart* wp = doc.EditableWordPart(part);
	if (wp == nullptr || first >= wp->paragraphs.size())
		return false;
	if (last < first)
		std::swap(first, last);
	last = std::min(last, wp->paragraphs.size() - 1);
	StyleMgr& styles = doc.styles;
	bool changed = false;
	for (size_t k = first; k <= last; k++) {
		WPParagraph& p = wp->paragraphs[k];
		if (p.parStyle >= styles.styles.size() || !styles.styles[p.parStyle].name.empty())
			continue;
		Style style = styles.styles[p.parStyle];
		size_t before = style.props.size();
		style.props.erase(std::remove_if(style.props.begin(), style.props.end(),
			[&](uint32_t i) {
				return i < styles.properties.size() && styles.properties[i].type == Sig("PTab")
					&& TabPosition(styles.properties[i]) == position;
			}), style.props.end());
		if (style.props.size() == before)
			continue;
		style.substyles.clear();
		uint32_t index = uint32_t(styles.styles.size());
		for (size_t i = 0; i < styles.styles.size(); i++)
			if (SameStyle(styles.styles[i], style)) {
				index = uint32_t(i);
				break;
			}
		if (index == styles.styles.size()) {
			style.value4 = 1;
			Retain(styles, style.links[0]);
			styles.styles.push_back(style);
		}
		Retain(styles, index);
		Release(styles, p.parStyle);
		p.parStyle = index;
		changed = true;
	}
	if (changed)
		doc.stylesChanged = true;
	return changed;
}


// #pragma mark - rows and columns

namespace {

// where row/column `t` goes; -1 if it's deleted
struct LineMap {
	int at, count;		// count < 0: deleted [at, at - count)

	int operator()(int t) const
	{
		if (t < at)
			return t;
		if (count > 0)
			return t + count;
		return t < at - count ? -1 : t + count;
	}
	// a range's ends: [from, to] -> new [from, to], false if nothing is left
	bool Range(int& from, int& to) const
	{
		int f = (*this)(from), e = (*this)(to);
		if (f < 0)
			f = at;				// first row after the deletion
		if (e < 0)
			e = at - 1;			// last row before it
		if (e < f)
			return false;
		from = f;
		to = e;
		return true;
	}
};


void
MapRange(SSRange& r, const LineMap& map, bool columns, bool& valid)
{
	int from = columns ? r.from.b : r.from.a;
	int to = columns ? r.to.b : r.to.a;
	valid = map.Range(from, to);
	if (!valid)
		return;
	(columns ? r.from.b : r.from.a) = uint16_t(from);
	(columns ? r.to.b : r.to.a) = uint16_t(to);
}

} // namespace


bool
InsertRowsColumns(Document& doc, size_t part, bool columns, uint16_t at, int count)
{
	SSPart* ss = doc.EditableSpreadsheetPart(part);
	if (ss == nullptr || count == 0)
		return false;
	LineMap map{at, count};
	auto line = [&](const CellID& id) -> int { return columns ? id.b : id.a; };
	auto setLine = [&](CellID& id, int v) { (columns ? id.b : id.a) = uint16_t(v); };

	// formulas, with their references resolved against the old positions
	// and stored against the new ones
	std::vector<SSCell> cells;
	for (SSCell& cell : ss->cells) {
		int moved = map(line(cell.id));
		if (moved < 0)
			continue;
		CellID oldAt = cell.id, newAt = cell.id;
		setLine(newAt, moved);
		const uint16_t absolute = columns ? 4 : 2;
		for (FormulaToken& token : cell.formula) {
			bool range = token.refs.size() == 2;
			int ends[2];
			bool live = true;
			for (size_t k = 0; k < token.refs.size(); k++) {
				CellRef& r = token.refs[k];
				if (r.flags & 1) {
					live = false;
					continue;
				}
				int16_t stored = int16_t(columns ? r.cell.b : r.cell.a);
				ends[k % 2] = (r.flags & absolute) ? stored : line(oldAt) + stored;
			}
			if (!live || token.refs.empty())
				continue;
			bool valid = true;
			if (range)
				valid = map.Range(ends[0], ends[1]);
			else {
				ends[0] = map(ends[0]);
				valid = ends[0] >= 0;
			}
			for (size_t k = 0; k < token.refs.size(); k++) {
				CellRef& r = token.refs[k];
				if (!valid) {
					r.flags |= 1;
					continue;
				}
				int target = ends[k % 2];
				int stored = (r.flags & absolute) ? target : target - line(newAt);
				(columns ? r.cell.b : r.cell.a) = uint16_t(int16_t(stored));
			}
		}
		cell.id = newAt;
		cells.push_back(std::move(cell));
	}
	ss->cells = std::move(cells);

	// sizes of rows/columns
	std::vector<SSHeader>& headers = columns ? ss->columns : ss->rows;
	std::vector<SSHeader> kept;
	for (SSHeader& h : headers) {
		int moved = map(int(h.index));
		if (moved < 0)
			continue;
		h.index = uint32_t(moved);
		kept.push_back(h);
	}
	headers = std::move(kept);

	// ranges kept with the part
	bool valid;
	SSRange used = ss->used;
	MapRange(used, map, columns, valid);
	if (valid)
		ss->used = used;
	// (the locked cells deleted aren't locked any more)
	std::vector<SSRange> locked;
	for (SSRange r : ss->locked) {
		MapRange(r, map, columns, valid);
		if (valid)
			locked.push_back(r);
	}
	ss->locked = std::move(locked);
	std::vector<SSFill> fills;
	for (SSFill& fill : ss->fills) {
		MapRange(fill.range, map, columns, valid);
		if (!valid)
			continue;
		std::vector<SSRange> fillCells;
		for (SSRange r : fill.cells) {
			bool ok;
			MapRange(r, map, columns, ok);
			if (ok)
				fillCells.push_back(r);
		}
		fill.cells = std::move(fillCells);
		fills.push_back(std::move(fill));
	}
	ss->fills = std::move(fills);

	// the calculation order: moved cells, deleted ones left out
	std::vector<CellID> dependents;
	for (CellID id : ss->dependents) {
		int moved = map(line(id));
		if (moved < 0)
			continue;
		setLine(id, moved);
		dependents.push_back(id);
	}
	ss->dependents = std::move(dependents);

	// named ranges and selections of this part ((row, column) pairs); their
	// number stays the same so they can be written in place
	for (Selection& sel : doc.selections.selections) {
		if (sel.kind != Sig("SPRD") || sel.part != part)
			continue;
		int c = columns ? sel.cell[1] : sel.cell[0];
		c = map(c);
		(columns ? sel.cell[1] : sel.cell[0]) = uint16_t(c < 0 ? at : c);
		for (auto& r : sel.ranges) {
			int from = columns ? r[1] : r[0], to = columns ? r[3] : r[2];
			if (!map.Range(from, to))
				from = to = std::min<int>(at, 0xfffe);	// nothing left: one row
			(columns ? r[1] : r[0]) = uint16_t(from);
			(columns ? r[3] : r[2]) = uint16_t(to);
		}
		doc.selectionsChanged = true;
	}

	UpdateDependencies(doc, part);
	return true;
}


// #pragma mark - row heights / column widths


bool
SetLineSize(SSPart& part, bool columns, uint16_t first, uint16_t last, float size)
{
	if (last < first)
		std::swap(first, last);
	std::vector<SSHeader>& headers = columns ? part.columns : part.rows;
	bool changed = false;
	for (uint16_t i = first; ; i++) {
		SSHeader* h = nullptr;
		for (SSHeader& hdr : headers)
			if (hdr.index == i) {
				h = &hdr;
				break;
			}
		if (size == 0.0f) {
			// Delete the header to revert to default
			if (h != nullptr) {
				headers.erase(std::find_if(headers.begin(), headers.end(),
					[&](const SSHeader& hdr) { return hdr.index == i; }));
				changed = true;
			}
		} else {
			if (h == nullptr) {
				SSHeader hdr;
				hdr.index = i;
				hdr.cell.kind = Sig("Head");
				hdr.cell.style = 0xffffffff;
				hdr.cell.number = size;
				headers.push_back(std::move(hdr));
				changed = true;
			} else if (h->cell.number != size) {
				h->cell.number = size;
				changed = true;
			}
		}
		if (i == last)
			break;
	}
	return changed;
}


// #pragma mark - formulas

namespace {

class Parser {
public:
	Parser(const std::string& text, CellID at,
			const std::function<bool(const std::string&)>& isName)
		: fText(text), fAt(at), fIsName(isName) {}

	bool Parse(std::vector<FormulaToken>& out, std::string& error)
	{
		try {
			Comparison(out);
			Blanks();
			if (fPos != fText.size())
				throw std::string("unexpected '") + fText.substr(fPos, 8) + "'";
			out.push_back(Token(0x1d));
			return true;
		} catch (const std::string& e) {
			error = e;
			return false;
		}
	}

private:
	static FormulaToken Token(uint16_t kind)
	{
		FormulaToken t;
		t.kind = kind;
		return t;
	}

	void Blanks()
	{
		while (fPos < fText.size() && isspace((unsigned char)fText[fPos]))
			fPos++;
	}

	bool Take(const char* s)
	{
		Blanks();
		size_t n = strlen(s);
		if (fText.compare(fPos, n, s) == 0) {
			fPos += n;
			return true;
		}
		return false;
	}

	// binary operators are stored before their operands
	void Binary(std::vector<FormulaToken>& out, uint16_t op,
		std::vector<FormulaToken>& left, std::vector<FormulaToken>& right)
	{
		std::vector<FormulaToken> r;
		r.push_back(Token(op));
		r.insert(r.end(), left.begin(), left.end());
		r.insert(r.end(), right.begin(), right.end());
		out = std::move(r);
	}

	// <= >= <> < > = (0-5)
	void Comparison(std::vector<FormulaToken>& out)
	{
		std::vector<FormulaToken> left;
		Concat(left);
		for (;;) {
			int op = -1;
			if (Take("<="))
				op = 0;
			else if (Take(">="))
				op = 1;
			else if (Take("<>"))
				op = 2;
			else if (Take("<"))
				op = 3;
			else if (Take(">"))
				op = 4;
			else if (Take("="))
				op = 5;
			if (op < 0)
				break;
			std::vector<FormulaToken> right;
			Concat(right);
			Binary(left, uint16_t(op), left, right);
		}
		out.insert(out.end(), left.begin(), left.end());
	}

	void Concat(std::vector<FormulaToken>& out)
	{
		std::vector<FormulaToken> left;
		Sum(left);
		while (Take("&")) {
			std::vector<FormulaToken> right;
			Sum(right);
			Binary(left, 6, left, right);
		}
		out.insert(out.end(), left.begin(), left.end());
	}

	void Sum(std::vector<FormulaToken>& out)
	{
		std::vector<FormulaToken> left;
		Product(left);
		for (;;) {
			uint16_t op;
			if (Take("+"))
				op = 7;
			else if (Take("-"))
				op = 8;
			else
				break;
			std::vector<FormulaToken> right;
			Product(right);
			Binary(left, op, left, right);
		}
		out.insert(out.end(), left.begin(), left.end());
	}

	void Product(std::vector<FormulaToken>& out)
	{
		std::vector<FormulaToken> left;
		Power(left);
		for (;;) {
			uint16_t op;
			if (Take("*"))
				op = 9;
			else if (Take("/"))
				op = 10;
			else
				break;
			std::vector<FormulaToken> right;
			Power(right);
			Binary(left, op, left, right);
		}
		out.insert(out.end(), left.begin(), left.end());
	}

	void Power(std::vector<FormulaToken>& out)
	{
		std::vector<FormulaToken> left;
		Unary(left);
		while (Take("^")) {
			std::vector<FormulaToken> right;
			Unary(right);
			Binary(left, 11, left, right);
		}
		out.insert(out.end(), left.begin(), left.end());
	}

	void Unary(std::vector<FormulaToken>& out)
	{
		if (Take("-")) {
			out.push_back(Token(0xd));
			Unary(out);
			return;
		}
		if (Take("+")) {
			Unary(out);
			return;
		}
		std::vector<FormulaToken> operand;
		Primary(operand);
		while (Take("%")) {
			std::vector<FormulaToken> r = {Token(0xc)};
			r.insert(r.end(), operand.begin(), operand.end());
			operand = std::move(r);
		}
		out.insert(out.end(), operand.begin(), operand.end());
	}

	// a cell reference at fPos: [$]letters[$]digits
	bool CellRefAt(size_t& pos, CellRef& ref)
	{
		size_t p = pos;
		bool absColumn = false, absRow = false;
		if (p < fText.size() && fText[p] == '$') {
			absColumn = true;
			p++;
		}
		size_t letters = p;
		while (p < fText.size() && isalpha((unsigned char)fText[p]))
			p++;
		size_t nLetters = p - letters;
		if (nLetters < 1 || nLetters > 2)
			return false;
		if (p < fText.size() && fText[p] == '$') {
			absRow = true;
			p++;
		}
		size_t digits = p;
		while (p < fText.size() && isdigit((unsigned char)fText[p]))
			p++;
		if (p == digits || (p < fText.size() && (isalnum((unsigned char)fText[p])
				|| fText[p] == '_' || fText[p] == '(')))
			return false;
		int column = 0;
		for (size_t i = letters; i < letters + nLetters; i++)
			column = column * 26 + (toupper((unsigned char)fText[i]) - 'A' + 1);
		column--;
		int row = atoi(fText.c_str() + digits) - 1;
		if (row < 0 || row > 0x3fff || column > 0x2be)
			return false;
		ref.flags = (absRow ? 2 : 0) | (absColumn ? 4 : 0);
		ref.cell.a = uint16_t(absRow ? row : row - fAt.a);
		ref.cell.b = uint16_t(absColumn ? column : column - fAt.b);
		pos = p;
		return true;
	}

	void Primary(std::vector<FormulaToken>& out)
	{
		Blanks();
		if (fPos >= fText.size())
			throw std::string("the formula ends early");
		char c = fText[fPos];
		if (c == '(') {
			fPos++;
			out.push_back(Token(0xe));
			Comparison(out);
			if (!Take(")"))
				throw std::string("missing ')'");
			out.push_back(Token(0xf));
			return;
		}
		if (c == '"') {
			size_t end = fText.find('"', fPos + 1);
			if (end == std::string::npos)
				throw std::string("missing '\"'");
			FormulaToken t = Token(0x11);
			t.strings.push_back(fText.substr(fPos + 1, end - fPos - 1));
			fPos = end + 1;
			out.push_back(t);
			return;
		}
		if (isdigit((unsigned char)c) || c == '.') {
			char* end;
			double v = strtod(fText.c_str() + fPos, &end);
			std::string literal(fText.c_str() + fPos, size_t(end - (fText.c_str() + fPos)));
			fPos = end - fText.c_str();
			bool integer = literal.find_first_of(".eE") == std::string::npos;
			FormulaToken t = Token(integer && fabs(v) < 32768 ? 0x13
				: integer && fabs(v) < 2147483648.0 ? 0x14 : 0x15);
			t.number = v;
			out.push_back(t);
			return;
		}
		// a cell or range reference
		size_t p = fPos;
		CellRef from;
		if (CellRefAt(p, from)) {
			size_t q = p;
			while (q < fText.size() && isspace((unsigned char)fText[q]))
				q++;
			CellRef to;
			if (q < fText.size() && fText[q] == ':') {
				q++;
				while (q < fText.size() && isspace((unsigned char)fText[q]))
					q++;
				if (CellRefAt(q, to)) {
					FormulaToken t = Token(0x1a);
					t.refs = {from, to};
					fPos = q;
					out.push_back(t);
					return;
				}
			}
			FormulaToken t = Token(0x19);
			t.refs = {from};
			fPos = p;
			out.push_back(t);
			return;
		}
		// names: the longest known name here (names may contain spaces)
		for (size_t end = fText.size(); end > fPos; end--) {
			std::string name = fText.substr(fPos, end - fPos);
			while (!name.empty() && isspace((unsigned char)name.back()))
				name.pop_back();
			if (!name.empty() && fIsName && fIsName(name)) {
				FormulaToken t = Token(0x1b);
				t.value = 0;
				t.strings = {"", name};
				fPos += name.size();
				out.push_back(t);
				return;
			}
		}
		// a function call or TRUE/FALSE
		size_t start = fPos;
		while (fPos < fText.size() && (isalnum((unsigned char)fText[fPos]) || fText[fPos] == '_'))
			fPos++;
		std::string word = fText.substr(start, fPos - start);
		if (word.empty())
			throw std::string("unexpected '") + c + "'";
		std::string upper = word;
		for (char& ch : upper)
			ch = toupper((unsigned char)ch);
		if (!Take("(")) {
			if (upper == "TRUE" || upper == "FALSE") {
				FormulaToken t = Token(0x12);
				t.value = upper == "TRUE";
				out.push_back(t);
				return;
			}
			throw std::string("unknown name '") + word + "'";
		}
		FormulaToken call = Token(0x1c);
		call.strings.push_back(word);
		std::vector<FormulaToken> args;
		int argc = 0;
		if (!Take(")")) {
			do {
				Comparison(args);
				argc++;
			} while (Take(","));
			if (!Take(")"))
				throw std::string("missing ')' after the arguments of ") + word;
		}
		call.value = uint32_t(argc);
		out.push_back(call);
		out.insert(out.end(), args.begin(), args.end());
	}

	const std::string& fText;
	CellID fAt;
	const std::function<bool(const std::string&)>& fIsName;
	size_t fPos = 0;
};

} // namespace


bool
ParseFormula(const std::string& text, CellID at,
	const std::function<bool(const std::string&)>& isName,
	std::vector<FormulaToken>& tokens, std::string& error)
{
	tokens.clear();
	Parser parser(text, at, isName);
	return parser.Parse(tokens, error);
}


// #pragma mark - cells

namespace {

bool
ParseDate(const std::string& s, int64_t& seconds)
{
	int m, d, y;
	char tail;
	if (sscanf(s.c_str(), "%d/%d/%d%c", &m, &d, &y, &tail) != 3)
		return false;
	if (m < 1 || m > 12 || d < 1 || d > 31)
		return false;
	if (y < 100)
		y += y < 70 ? 2000 : 1900;
	// days from civil
	int yy = y - (m <= 2);
	int era = (yy >= 0 ? yy : yy - 399) / 400;
	int yoe = yy - era * 400;
	int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
	int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	seconds = (int64_t(era) * 146097 + doe - 719468) * 86400;
	return true;
}

} // namespace


bool
SetCellInput(SSPart& part, uint16_t row, uint16_t column, const std::string& input,
	const std::function<bool(const std::string&)>& isName, std::string& error)
{
	// cells are kept column by column, as TSparseArray writes them
	auto it = std::find_if(part.cells.begin(), part.cells.end(),
		[&](const SSCell& c) { return c.id.a == row && c.id.b == column; });
	SSCell cell;
	if (it != part.cells.end())
		cell = *it;
	cell.id = {row, column};
	std::string text = input;	// kept as typed (text cells may end in spaces)

	SSCell value = cell;
	value.formula.clear();
	value.text.clear();
	value.text2.clear();
	value.number = 0;
	value.value = 0;
	double number;
	char* end;
	int64_t seconds;
	if (text.empty()) {
		value.kind = Sig("NONE");
	} else if (text[0] == '=') {
		value.kind = Sig("Fmla");
		if (!ParseFormula(text.substr(1), cell.id, isName, value.formula, error))
			return false;
	} else if ((number = strtod(text.c_str(), &end)), *end == 0 && end != text.c_str()) {
		if (number == floor(number) && fabs(number) < 2147483648.0
				&& text.find_first_of(".eE") == std::string::npos) {
			value.kind = Sig("Lng#");
			value.value = uint32_t(int32_t(number));
		} else
			value.kind = Sig("Dbl#");
		value.number = number;
	} else if (text == "TRUE" || text == "true" || text == "FALSE" || text == "false") {
		value.kind = Sig("Bool");
		value.number = text[0] == 'T' || text[0] == 't';
	} else if (ParseDate(text, seconds)) {
		value.kind = Sig("Time");
		value.value = uint32_t(seconds);
		value.number = double(seconds);
	} else {
		value.kind = Sig("Strn");
		value.text = text;
	}

	if (it != part.cells.end()) {
		if (value.kind == Sig("NONE") && value.style == 0xffffffff)
			part.cells.erase(it);
		else
			*it = value;
		return true;
	}
	if (value.kind == Sig("NONE"))
		return true;
	auto at = std::find_if(part.cells.begin(), part.cells.end(), [&](const SSCell& c) {
		return std::make_pair(c.id.b, c.id.a) > std::make_pair(column, row);
	});
	part.cells.insert(at, value);
	// the used range grows to include the cell
	part.used.to.a = std::max(part.used.to.a, row);
	part.used.to.b = std::max(part.used.to.b, column);
	return true;
}


bool
UpdateDependencies(Document& doc, size_t partIndex)
{
	SSPart* part = nullptr;
	for (auto& [index, ss] : doc.spreadsheetParts)
		if (index == partIndex)
			part = &ss;
	if (part == nullptr)
		return false;

	// the formula cells and the same-part cells each one refers to
	std::map<uint32_t, const SSCell*> formulas;
	auto key = [](int row, int column) { return uint32_t(row) << 16 | uint16_t(column); };
	for (const SSCell& c : part->cells)
		if (c.kind == Sig("Fmla"))
			formulas[key(c.id.a, c.id.b)] = &c;
	auto resolve = [](const CellRef& r, CellID at, int& row, int& column) {
		if (r.flags & 1)
			return false;
		row = (r.flags & 2) ? int16_t(r.cell.a) : at.a + int16_t(r.cell.a);
		column = (r.flags & 4) ? int16_t(r.cell.b) : at.b + int16_t(r.cell.b);
		return true;
	};
	auto precedents = [&](const SSCell& c) {
		std::vector<uint32_t> out;
		for (const FormulaToken& t : c.formula) {
			int r0, c0, r1, c1;
			if (t.kind == 0x19 && resolve(t.refs[0], c.id, r0, c0)) {
				if (formulas.count(key(r0, c0)))
					out.push_back(key(r0, c0));
			} else if (t.kind == 0x1a && resolve(t.refs[0], c.id, r0, c0)
				&& resolve(t.refs[1], c.id, r1, c1)) {
				for (auto& [k, f] : formulas) {
					int row = k >> 16, column = int16_t(k & 0xffff);
					if (row >= std::min(r0, r1) && row <= std::max(r0, r1)
						&& column >= std::min(c0, c1) && column <= std::max(c0, c1))
						out.push_back(k);
				}
			}
		}
		return out;
	};

	// TSSDepManager::FixFormulaCells: a depth-first walk in list order
	std::vector<uint32_t> order;
	std::set<uint32_t> seen;
	for (const CellID& id : part->dependents)
		if (formulas.count(key(id.a, id.b)) && seen.insert(key(id.a, id.b)).second)
			order.push_back(key(id.a, id.b));
	for (auto& [k, f] : formulas)
		if (seen.insert(k).second)
			order.push_back(k);
	std::vector<CellID> sorted;
	std::set<uint32_t> done, visiting;
	std::function<void(uint32_t)> visit = [&](uint32_t k) {
		if (done.count(k) || visiting.count(k))
			return;	// circular references keep their place
		visiting.insert(k);
		for (uint32_t p : precedents(*formulas[k]))
			if (p != k)
				visit(p);
		visiting.erase(k);
		done.insert(k);
		sorted.push_back({uint16_t(k >> 16), uint16_t(k & 0xffff)});
	};
	for (uint32_t k : order)
		visit(k);
	part->dependents = std::move(sorted);

	// links: the names the formulas use, by link
	std::map<uint32_t, std::vector<CellID>> users;	// link -> cells (in cell order)
	bool linksChanged = false;
	for (const SSCell& c : part->cells) {
		if (c.kind != Sig("Fmla"))
			continue;
		std::set<uint32_t> linked;
		for (const FormulaToken& t : c.formula) {
			if (t.kind != 0x1b || t.strings.size() < 2)
				continue;
			// the reference of that name and the link from it to this part
			size_t sourcePart, sourceRef;
			if (!doc.FindName(t.strings[1], int(partIndex), sourcePart, sourceRef))
				continue;
			size_t links = doc.links.links.size();
			int link = int(LinkFor(doc, sourcePart, sourceRef, partIndex));
			linksChanged |= doc.links.links.size() != links;
			if (linked.insert(uint32_t(link)).second)
				users[uint32_t(link)].push_back(c.id);
		}
	}
	std::vector<SSPart::LinkDep> deps;
	std::set<uint32_t> present;
	for (const SSPart::LinkDep& old : part->linkDepInfo) {
		auto it = users.find(old.link);
		if (it == users.end())
			continue;	// no formula uses it any more
		SSPart::LinkDep d = old;
		// keep the original order of the cells still using it, new ones after
		std::vector<SSRange> cells;
		std::set<uint32_t> keep;
		for (const CellID& id : it->second)
			keep.insert(key(id.a, id.b));
		std::set<uint32_t> listed;
		for (const SSRange& r : old.cells)
			for (int row = r.from.a; row <= r.to.a; row++)
				for (int column = r.from.b; column <= r.to.b; column++)
					if (keep.count(key(row, column)) && listed.insert(key(row, column)).second)
						cells.push_back({{uint16_t(row), uint16_t(column)},
							{uint16_t(row), uint16_t(column)}});
		for (const CellID& id : it->second)
			if (listed.insert(key(id.a, id.b)).second)
				cells.push_back({id, id});
		// a list naming the same cells stays exactly as it was (the original
		// keeps ranges and repeats)
		std::set<uint32_t> oldSet;
		for (const SSRange& r : old.cells)
			for (int row = r.from.a; row <= r.to.a; row++)
				for (int column = r.from.b; column <= r.to.b; column++)
					oldSet.insert(key(row, column));
		if (oldSet != keep) {
			d.cells = std::move(cells);
			d.value = uint32_t(listed.size());
		}
		deps.push_back(std::move(d));
		present.insert(old.link);
	}
	for (auto& [link, cells] : users) {
		if (present.count(link))
			continue;
		SSPart::LinkDep d;
		d.link = link;
		for (const CellID& id : cells)
			d.cells.push_back({id, id});
		d.value = uint32_t(cells.size());
		deps.push_back(std::move(d));
	}
	part->linkDepInfo = std::move(deps);
	part->linkDeps = uint32_t(part->linkDepInfo.size());
	return linksChanged;
}


std::string
CellInput(const SSPart& part, uint16_t row, uint16_t column)
{
	const SSCell* c = part.Cell(row, column);
	if (c == nullptr)
		return "";
	char buf[64];
	switch (c->kind) {
		case Sig("Fmla"):
			return "=" + FormulaText(c->formula, c->id);
		case Sig("Strn"):
			return c->text;
		case Sig("Lng#"):
			return std::to_string(int32_t(c->value));
		case Sig("Flt#"):
		case Sig("Dbl#"):
			snprintf(buf, sizeof buf, "%.15g", c->number);
			return buf;
		case Sig("Bool"):
			return c->number != 0 ? "TRUE" : "FALSE";
		case Sig("Time"): {
			time_t t = time_t(c->value);
			struct tm tm;
			gmtime_r(&t, &tm);
			snprintf(buf, sizeof buf, "%d/%d/%d", tm.tm_mon + 1, tm.tm_mday,
				tm.tm_year + 1900);
			return buf;
		}
		case Sig("FmEr"):
			return c->text;
		default:
			return "";
	}
}


std::string
RangeInput(const SSPart& part, uint16_t row0, uint16_t column0,
	uint16_t row1, uint16_t column1)
{
	std::string s;
	for (uint16_t r = row0; r <= row1; r++) {
		if (r > row0)
			s += "\n";
		for (uint16_t c = column0; c <= column1; c++) {
			if (c > column0)
				s += "\t";
			s += CellInput(part, r, c);
		}
	}
	return s;
}


bool
ClearRange(SSPart& part, uint16_t row0, uint16_t column0,
	uint16_t row1, uint16_t column1)
{
	bool changed = false;
	for (uint16_t r = row0; r <= row1; r++) {
		for (uint16_t c = column0; c <= column1; c++) {
			std::string input = CellInput(part, r, c);
			if (!input.empty()) {
				std::string error;
				SetCellInput(part, r, c, "", nullptr, error);
				changed = true;
			}
		}
	}
	return changed;
}


bool
FillRange(SSPart& part, uint16_t row0, uint16_t column0, uint16_t row1,
	uint16_t column1, bool right)
{
	return FillCells(part, row0, column0, row1, column1,
		right ? FillKind::right : FillKind::down);
}


// TSSFillCommand::FillRangeWithCell for one cell: the source's data cell
// cloned into (row, column), or the cell emptied if there is none
static bool
FillOneCell(SSPart& part, uint16_t sourceRow, uint16_t sourceColumn, uint16_t row,
	uint16_t column)
{
	const SSCell* source = part.Cell(sourceRow, sourceColumn);
	auto at = std::find_if(part.cells.begin(), part.cells.end(),
		[&](const SSCell& c) { return c.id.a == row && c.id.b == column; });
	if (source == nullptr) {
		if (at == part.cells.end())
			return false;
		part.cells.erase(at);
		return true;
	}
	SSCell cell = *source;
	cell.id = {row, column};
	if (at != part.cells.end()) {
		*at = cell;
		return true;
	}
	// (cells sorted by column, then row)
	auto before = std::find_if(part.cells.begin(), part.cells.end(),
		[&](const SSCell& c) {
			return std::make_pair(c.id.b, c.id.a) > std::make_pair(column, row);
		});
	part.cells.insert(before, cell);
	return true;
}


bool
FillCells(SSPart& part, uint16_t row0, uint16_t column0, uint16_t row1,
	uint16_t column1, FillKind kind)
{
	// TRange::Pin to the sheet
	row0 = std::max(row0, part.used.from.a);
	column0 = std::max(column0, part.used.from.b);
	row1 = std::min(row1, part.used.to.a);
	column1 = std::min(column1, part.used.to.b);
	if (row0 > row1 || column0 > column1)
		return false;
	bool vertical = kind == FillKind::down || kind == FillKind::up;
	if (vertical ? row0 == row1 : column0 == column1)
		return false;

	bool changed = false;
	for (uint16_t row = row0; row <= row1; row++) {
		for (uint16_t column = column0; column <= column1; column++) {
			uint16_t sourceRow = row, sourceColumn = column;
			switch (kind) {
				case FillKind::down: sourceRow = row0; break;
				case FillKind::up: sourceRow = row1; break;
				case FillKind::right: sourceColumn = column0; break;
				case FillKind::left: sourceColumn = column1; break;
			}
			if (sourceRow == row && sourceColumn == column)
				continue;
			if (FillOneCell(part, sourceRow, sourceColumn, row, column))
				changed = true;
		}
	}
	return changed;
}


// TRange::Intersects
static bool
RangesIntersect(const SSRange& x, const SSRange& y)
{
	return x.from.a <= y.to.a && y.from.a <= x.to.a && x.from.b <= y.to.b
		&& y.from.b <= x.to.b;
}


bool
LockCells(SSPart& part, uint16_t row0, uint16_t column0, uint16_t row1, uint16_t column1,
	bool lock)
{
	// TRange::Normalize
	SSRange block{{std::min(row0, row1), std::min(column0, column1)},
		{std::max(row0, row1), std::max(column0, column1)}};
	if (lock) {
		part.locked.push_back(block);
		return true;
	}
	// TCellsList::RemoveRange: the ranges it touches out, those it doesn't
	// contain stacked to come back in pieces (last first)
	std::vector<SSRange> stack;
	bool changed = false;
	for (size_t i = 0; i < part.locked.size(); i++) {
		const SSRange& r = part.locked[i];
		if (!RangesIntersect(block, r))
			continue;
		bool contained = block.from.a <= r.from.a && block.from.b <= r.from.b
			&& r.to.a <= block.to.a && r.to.b <= block.to.b;
		if (!contained)
			stack.push_back(r);
		part.locked.erase(part.locked.begin() + i);
		i--;
		changed = true;
	}
	while (!stack.empty()) {
		SSRange r = stack.back();
		stack.pop_back();
		if (r.from.a < block.from.a) {		// above
			part.locked.push_back({r.from, {uint16_t(block.from.a - 1), r.to.b}});
			r.from.a = block.from.a;
		}
		if (r.from.b < block.from.b) {		// left
			part.locked.push_back({r.from, {r.to.a, uint16_t(block.from.b - 1)}});
			r.from.b = block.from.b;
		}
		if (block.to.a < r.to.a) {			// below
			part.locked.push_back({{uint16_t(block.to.a + 1), r.from.b}, r.to});
			r.to.a = block.to.a;
		}
		if (block.to.b < r.to.b)			// right
			part.locked.push_back({{r.from.a, uint16_t(block.to.b + 1)}, r.to});
	}
	return changed;
}


bool
CellsLocked(const SSPart& part, uint16_t row0, uint16_t column0, uint16_t row1,
	uint16_t column1)
{
	SSRange block{{std::min(row0, row1), std::min(column0, column1)},
		{std::max(row0, row1), std::max(column0, column1)}};
	for (const SSRange& r : part.locked)
		if (RangesIntersect(r, block))
			return true;
	return false;
}


bool
FillTargets(uint16_t& row0, uint16_t& column0, uint16_t& row1, uint16_t& column1,
	FillKind kind)
{
	switch (kind) {
		case FillKind::down: row0++; break;
		case FillKind::right: column0++; break;
		case FillKind::up:
			if (row1 == 0)
				return false;
			row1--;
			break;
		case FillKind::left:
			if (column1 == 0)
				return false;
			column1--;
			break;
	}
	return row0 <= row1 && column0 <= column1;
}


bool
PasteInput(SSPart& part, uint16_t row, uint16_t column, const std::string& text,
	const std::function<bool(const std::string&)>& isName, std::string& error,
	uint16_t& rows, uint16_t& columns)
{
	// split into rows (the \n of \r\n ends the line)
	std::vector<std::string> lines;
	std::string cur;
	for (size_t i = 0; i <= text.size(); i++) {
		char c = i < text.size() ? text[i] : '\n';
		if (c == '\r' && i + 1 < text.size() && text[i + 1] == '\n')
			continue;
		if (c == '\n' || c == '\r') {
			lines.push_back(cur);
			cur.clear();
		} else
			cur += c;
	}
	// ignore trailing line break (trailing empty line)
	if (!lines.empty() && lines.back().empty())
		lines.pop_back();

	rows = uint16_t(lines.size());
	// determine columns from the row with the most tabs
	uint16_t maxCols = 0;
	for (const auto& line : lines) {
		uint16_t cols = 1;
		for (char c : line)
			if (c == '\t')
				cols++;
		if (cols > maxCols)
			maxCols = cols;
	}
	columns = maxCols;

	// paste cell by cell, stop at first parse failure
	for (uint16_t r = 0; r < rows; r++) {
		const std::string& line = lines[r];
		size_t start = 0;
		for (uint16_t c = 0; c < columns; c++) {
			// short rows: the cells after their last one are cleared
			std::string cellText;
			if (start <= line.size()) {
				size_t end = std::min(line.find('\t', start), line.size());
				cellText = line.substr(start, end - start);
				start = end + 1;
			}
			if (!SetCellInput(part, row + r, column + c, cellText, isName, error))
				return false;
		}
	}
	return true;
}

uint32_t
PartPosition(const WPPart& part, TextPosition at)
{
	uint32_t position = 0;
	for (size_t i = 0; i < at.paragraph && i < part.paragraphs.size(); i++)
		position += uint32_t(part.paragraphs[i].text.size());
	return position + uint32_t(at.offset);
}


TextPosition
FromPartPosition(const WPPart& part, uint32_t position)
{
	for (size_t i = 0; i < part.paragraphs.size(); i++) {
		size_t size = part.paragraphs[i].text.size();
		if (position < size || i + 1 == part.paragraphs.size())
			return {i, std::min<size_t>(position, size)};
		position -= uint32_t(size);
	}
	return {0, 0};
}

namespace {

bool
IsBlank(char c)
{
	return c == 0x01 || c == '\n' || c == 0x02 || c == '\f' || c == 0x05 || c == '\t' || c == ' ';
}


// TParagraph::PrevCharPos
size_t
PrevCharPos(const std::string& text, size_t at)
{
	while (at > 0) {
		at--;
		if ((uint8_t(text[at]) & 0xc0) != 0x80)
			break;
	}
	return at;
}

} // namespace


int
CharKind(const std::string& text, size_t at)
{
	if (at >= text.size())
		return 1;
	uint8_t c = uint8_t(text[at]);
	if (IsBlank(char(c)))
		return 1;
	// FlattenUTF8: the bytes big-endian
	size_t n = ((0xe5000000u >> ((c >> 3) & 0x1e)) & 3) + 1;
	uint32_t flat = 0;
	for (size_t i = 0; i < n && at + i < text.size(); i++)
		flat |= uint32_t(uint8_t(text[at + i])) << (24 - 8 * i);
	if ((c < 0x80 && ispunct(c)) || flat - 0xe2808000u <= 0x12f00
			|| flat - 0xe3808000u <= 0x3f00)
		return 2;
	if (c >= 0x80) {
		if (flat - 0xe3888000u <= 0x33f00)
			return 3;
		if (flat - 0xefbc8000u <= 0x32f00)
			return 4;
		if (flat - 0xe3818000u <= 0x11f00)
			return 5;
		if (flat - 0xe382a000u <= 0x11f00)
			return 6;
		if (flat - 0xe4b88000u <= 0x5073f00 || flat - 0xefa48000u <= 0x73f00)
			return 7;
	}
	return 0;
}


void
WordBounds(const std::string& text, size_t offset, bool before, size_t& from, size_t& to)
{
	if (offset != 0 && offset < text.size() && text[offset] == 0x05)
		offset = PrevCharPos(text, offset);
	int kind = CharKind(text, offset);
	size_t at = offset;
	bool scan = true;
	if (before && kind == 1) {
		if (offset == 0) {
			from = 0;
			scan = false;
		} else {
			at = PrevCharPos(text, offset);
			kind = CharKind(text, at);
		}
	}
	if (scan) {
		from = at;
		while (from > 0) {
			size_t previous = PrevCharPos(text, from);
			if (CharKind(text, previous) != kind)
				break;
			from = previous;
		}
	}
	to = offset;
	while (to < text.size() && CharKind(text, to) == kind)
		to += ((0xe5000000u >> ((uint8_t(text[to]) >> 3) & 0x1e)) & 3) + 1;
	to = std::min(to, text.size());
}


void
SpaceBounds(const std::string& text, size_t offset, size_t& from, size_t& to)
{
	from = std::min(offset, text.size());
	while (from > 0 && IsBlank(text[from - 1]))
		from--;
	to = std::min(offset, text.size());
	while (to < text.size() && IsBlank(text[to]))
		to++;
}

} // namespace gobe
