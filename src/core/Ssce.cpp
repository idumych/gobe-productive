// The SSCE spelling engine, as Gobe Productive 2.0.1 links it in. Each
// function says which of the original's it is.
#include "Ssce.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <sstream>

namespace gobe {
namespace ssce {

namespace {

// Latin1_CharClass, Latin1_ToBase and Lang_PhoneticCharEquiv's table, as
// rules (the host test compares them with the original's)
struct Tables {
	uint8_t charClass[256];
	uint8_t toBase[256];
	uint8_t phonetic[256];

	Tables()
	{
		memset(this, 0, sizeof(*this));
		for (int c : {9, 10, 13, 32})
			charClass[c] = kSpace;
		for (int c = 33; c < 128; c++)
			charClass[c] = kPunct;
		for (int c = '0'; c <= '9'; c++)
			charClass[c] = kDigit;
		for (int c = 146; c <= 156; c++)
			charClass[c] = kPunct;
		for (int c = 161; c <= 191; c++)
			charClass[c] = kPunct;
		charClass[215] = charClass[247] = kPunct;
		auto vowel = [](int c) { return strchr("AEIOUaeiou", c) != nullptr && c != 0; };
		for (int c = 'A'; c <= 'Z'; c++) {
			charClass[c] = kAlpha | kUpper | (vowel(c) ? kVowel : 0);
			charClass[c + 32] = kAlpha | kLower | (vowel(c + 32) ? kVowel : 0);
			toBase[c] = toBase[c + 32] = uint8_t(c + 32);
		}
		// the accented letters: upper 192-223, lower 224-255 (not 215, 247);
		// the consonants among them (Ç Ð Ñ Ý Þ ß, ç ñ ý þ ÿ) aren't vowels
		for (int c = 192; c <= 255; c++) {
			if (c == 215 || c == 247)
				continue;
			bool upper = c <= 223;
			bool consonant = strchr("\xc7\xd0\xd1\xdd\xde\xdf\xe7\xf1\xfd\xfe\xff", c) != nullptr;
			charClass[c] = kAlpha | (upper ? kUpper : kLower) | (consonant ? 0 : kVowel);
		}
		const char* base = "aaaaaaaceeeeiiii.nooooo.ouuuuy.saaaaaaaceeeeiiii.nooooo.ouuuuy.y";
		const char* sounds = "aaaaaaasaaaaaaaa.maaaaa.aaaaaaasaaaaaaasaaaaaaaa.maaaaa.aaaaaaa.";
		for (int c = 192; c <= 255; c++) {
			toBase[c] = base[c - 192] == '.' ? 0 : uint8_t(base[c - 192]);
			phonetic[c] = sounds[c - 192] == '.' ? 0 : uint8_t(sounds[c - 192]);
		}
		const char* upperSounds = "abcdefc.ajclmmabcrsdaf.s.s";
		const char* lowerSounds = "abcdafc.ajclmmabcrsdaf.s.s";
		for (int c = 0; c < 26; c++) {
			phonetic['A' + c] = upperSounds[c] == '.' ? 0 : uint8_t(upperSounds[c]);
			phonetic['a' + c] = lowerSounds[c] == '.' ? 0 : uint8_t(lowerSounds[c]);
		}
	}
};

const Tables&
T()
{
	static Tables tables;
	return tables;
}

// Lang_PhoneticPairEquiv's pairs: two letters sounding as one
const struct {
	char first, second, sound;
} kPairs[8] = {{'c', 'e', 's'}, {'c', 'i', 's'}, {'c', 'k', 'c'}, {'d', 'g', 'j'},
	{'g', 'e', 'j'}, {'k', 'n', 'n'}, {'p', 'h', 'f'}, {'p', 's', 's'}};

uint8_t Lower(uint8_t c) { return (T().charClass[c] & kUpper) ? uint8_t(c + 0x20) : c; }
uint8_t Upper(uint8_t c) { return (T().charClass[c] & kLower) ? uint8_t(c - 0x20) : c; }
bool IsAlnum(uint8_t c) { return (T().charClass[c] & (kAlpha | kDigit)) != 0; }
bool IsQuote(uint8_t c) { return c == '\'' || c == 0x92; }


// SafeStr_Cmp, SafeStr_CmpNC
int
Compare(const std::string& a, const std::string& b)
{
	size_t n = std::min(a.size(), b.size());
	for (size_t i = 0; i < n; i++)
		if (a[i] != b[i])
			return uint8_t(a[i]) > uint8_t(b[i]) ? 1 : -1;
	return a.size() == b.size() ? 0 : a.size() > b.size() ? 1 : -1;
}


int
CompareNC(const std::string& a, const std::string& b)
{
	size_t n = std::max(a.size(), b.size());
	for (size_t i = 0; i <= n; i++) {
		uint8_t x = i < a.size() ? Lower(uint8_t(a[i])) : 0;
		uint8_t y = i < b.size() ? Lower(uint8_t(b[i])) : 0;
		if (x != y)
			return x > y ? 1 : -1;
		if (x == 0)
			return 0;
	}
	return 0;
}


// 63 characters at most, as the engine's 64-byte buffers
std::string
Clip(const std::string& s)
{
	return s.size() > 63 ? s.substr(0, 63) : s;
}


// FUN_002c9880: the replacement in the word's case (all capitals, or a
// capital first letter)
void
MatchCase(const std::string& word, std::string& replacement)
{
	if (word.empty())
		return;
	if (CompareNC(word, replacement) == 0 && !(T().charClass[uint8_t(word[0])] & kUpper))
		return;
	bool allUpper = true;
	for (char c : word) {
		uint8_t k = T().charClass[uint8_t(c)];
		if ((k & kAlpha) && !(k & kUpper)) {
			allUpper = false;
			break;
		}
	}
	if (allUpper) {
		for (char& c : replacement)
			c = char(Upper(uint8_t(c)));
	} else if ((T().charClass[uint8_t(word[0])] & kUpper) && !replacement.empty())
		replacement[0] = char(Upper(uint8_t(replacement[0])));
}


// FUN_002c8668: a lexicon's action as a result
int
ActionResult(int action)
{
	switch (action) {
		case 'A': case 'a':
			return kAutoChange;
		case 'C': case 'c':
			return kConditionalChange;
		case 'e':
			return kMisspelled;
		default:
			return 0;
	}
}


// FUN_002c9764: the word to look up (a possessive "'s" or "s'" off)
std::string
Normalize(const std::string& word, uint32_t options)
{
	std::string s = Clip(word);
	size_t n = s.size();
	if ((options & kStripPossessives) && n > 1) {
		uint8_t last = uint8_t(s[n - 1]), before = uint8_t(s[n - 2]);
		if (((last == 's' || last == 'S') && IsQuote(before))
				|| ((before == 's' || before == 'S') && IsQuote(last)))
			s.resize(n - 2);
	}
	return s;
}


// FUN_002c9314: letters after the first that change between capitals and
// small letters
bool
HasMixedCase(const std::string& word)
{
	if (word.empty())
		return false;
	size_t i = 1;
	while (i < word.size() && !(T().charClass[uint8_t(word[i])] & kAlpha))
		i++;
	if (i >= word.size())
		return false;
	uint8_t reference = uint8_t(word[i]);
	bool lower = (T().charClass[reference] & kLower) != 0;
	for (; i < word.size(); i++) {
		uint8_t c = uint8_t(word[i]);
		if (IsQuote(c))
			continue;
		if (lower && (T().charClass[c] & kUpper))
			return true;
		if (!lower && (T().charClass[c] & kLower))
			return true;
	}
	return false;
}


// FUN_002c9280: the word up to a hyphen or an apostrophe (when splitting
// at them); `at` moves past it
std::string
SplitPart(const std::string& word, size_t& at, uint32_t options)
{
	std::string part;
	while (at < word.size()) {
		uint8_t c = uint8_t(word[at]);
		if (c == '-' && (options & kSplitHyphenated)) {
			at++;
			break;
		}
		if (IsQuote(c) && (options & kSplitContracted)) {
			at++;
			break;
		}
		if (part.size() < 63)
			part += char(c);
		at++;
	}
	return part;
}


uint32_t
U32(const std::vector<uint8_t>& d, size_t at)
{
	return at + 4 <= d.size()
		? uint32_t(d[at]) << 24 | uint32_t(d[at + 1]) << 16 | uint32_t(d[at + 2]) << 8 | d[at + 3]
		: 0;
}

} // namespace


uint8_t CharClass(uint8_t c) { return T().charClass[c]; }
uint8_t ToBase(uint8_t c) { return T().toBase[c]; }
uint8_t PhoneticClass(uint8_t c) { return T().phonetic[c]; }


// WordComparator_Typographical: from 100, less for each difference (5 an
// accent, 1 the case, 40 a letter more or less, 50 two swapped or another
// letter, 100 a vowel for a consonant), relative to the word's length
int
TypographicalScore(const std::string& word, const std::string& other)
{
	int length = int(word.size());
	int base = length * 100 == 0 ? 1 : length * 100;
	int difference = std::abs(int(other.size()) - length);
	if (length / 2 < difference)
		return 0;
	const Tables& t = T();
	size_t i = 0, j = 0;		// word, other
	auto w = [&](size_t k) { return k < word.size() ? uint8_t(word[k]) : uint8_t(0); };
	auto o = [&](size_t k) { return k < other.size() ? uint8_t(other[k]) : uint8_t(0); };
	int score = base;
	while (o(j) != 0 && w(i) != 0 && score > 0) {
		uint8_t b3 = t.toBase[o(j)], b7 = t.toBase[w(i)];
		if (b3 == b7) {
			if ((t.charClass[o(j)] & kLower) != (t.charClass[w(i)] & kLower))
				score -= 1;
			if (Lower(o(j)) != Lower(w(i)))
				score -= 5;
		} else {
			uint8_t nextOther = o(j + 1), nextWord = w(i + 1);
			if (b3 == t.toBase[nextWord] && t.toBase[nextOther] == b7) {
				score -= 50;
				if (nextOther != 0)
					j++;
				if (nextWord != 0)
					i++;
			} else if (b3 == t.toBase[nextWord]) {
				if (nextWord != 0)
					i++;
				score -= 40;
			} else if (t.toBase[nextOther] == b7) {
				if (nextOther != 0)
					j++;
				score -= 40;
			} else if (((t.charClass[b3] & kVowel) == 0 || (t.charClass[b7] & kVowel) == 0)
					&& ((t.charClass[b3] & kVowel) != 0 || (t.charClass[b7] & kVowel) != 0))
				score -= 100;
			else
				score -= 50;
		}
		i++;
		j++;
	}
	if (o(j) != 0)
		score -= int(other.size() - j) * 40;
	if (w(i) != 0)
		score -= int(word.size() - i) * 40;
	if (score < 0)
		score = 0;
	return int(int16_t(score * 100 / base));
}


// Lang_PhoneticSig: a sound per letter (vowels only first), repeats once
std::string
PhoneticSignature(const std::string& word)
{
	const Tables& t = T();
	std::string out;
	for (size_t i = 0; i < word.size(); i++) {
		uint8_t c = uint8_t(word[i]);
		uint8_t next = i + 1 < word.size() ? uint8_t(word[i + 1]) : 0;
		bool pair = false;
		for (const auto& p : kPairs)
			if (c == uint8_t(p.first) && next == uint8_t(p.second)) {
				char sound = char(t.phonetic[uint8_t(p.sound)]);
				if (out.empty() || out.back() != sound)
					out += sound;
				pair = true;
				break;
			}
		if (pair)
			continue;
		char sound = char(t.phonetic[c]);
		if (sound == 'a' && i != 0)
			sound = 0;
		if (sound != 0 && (out.empty() || out.back() != sound))
			out += sound;
	}
	return out;
}


// WordComparator_EnglishPhonetic: words that sound alike, by how many of
// their letters they share
int
PhoneticScore(const std::string& word, const std::string& other)
{
	if (PhoneticSignature(word) != PhoneticSignature(other))
		return 0;
	int counts[2][256] = {};
	int total = 0;
	for (char c : word) {
		counts[0][uint8_t(c)]++;
		total++;
	}
	for (char c : other) {
		counts[1][uint8_t(c)]++;
		total++;
	}
	int same = total;
	for (int c = 0; c < 255; c++)
		if (int8_t(counts[0][c]) != int8_t(counts[1][c]))
			same--;
	return total == 0 ? 0 : int(int16_t(same * 100 / total));
}


// SuggestionSet_Add
void
SuggestionSet::Add(const std::string& word, int score)
{
	if (entries.size() == capacity && (entries.empty() || entries.back().second >= score))
		return;
	size_t at = 0;
	while (at < entries.size() && score <= entries[at].second)
		at++;
	for (const auto& e : entries)
		if (e.first == word)
			return;
	entries.insert(entries.begin() + long(at), {Clip(word), score});
	if (entries.size() > capacity)
		entries.pop_back();
}


// #pragma mark - CLex


// CLex_Open, FUN_002cbe4c (the suffix tables), FUN_002cb930 (the segments)
bool
CompressedLexicon::Open(const std::vector<uint8_t>& data, std::string& error)
{
	fData = data;
	if (U32(fData, 0) != 0x57000501) {
		error = "not a compressed lexicon";
		return false;
	}
	auto strings = [&](size_t at, uint32_t length, std::vector<std::string>& out) {
		size_t end = std::min(fData.size(), at + length);
		std::string s;
		for (size_t i = at; i < end; i++) {
			if (fData[i] == 0) {
				out.push_back(s);
				s.clear();
			} else
				s += char(fData[i]);
		}
	};
	size_t at = 4;
	while (at + 8 <= fData.size()) {
		uint32_t type = U32(fData, at), length = U32(fData, at + 4);
		at += 8;
		switch (type) {
			case 1:
				language = uint16_t(U32(fData, at));
				break;
			case 2:
				fCharSet = uint16_t(U32(fData, at));
				if (fCharSet == 2) {
					error = "Unicode lexicon not supported";
					return false;
				}
				break;
			case 3:
			{
				uint32_t simple = U32(fData, at) & 0xffff;
				strings(at + 4, simple, fSimpleSuffixes);
				size_t next = at + 4 + simple;
				uint32_t extended = U32(fData, next) & 0xffff;
				strings(next + 4, extended, fExtendedSuffixes);
				break;
			}
			case 4:
			{
				uint32_t count = U32(fData, at);
				size_t p = at + 4;
				for (uint32_t i = 0; i < count && p + 11 <= fData.size(); i++) {
					Segment s;
					memcpy(s.prefix, &fData[p], 3);
					s.offset = U32(fData, p + 3);
					s.size = U32(fData, p + 7);
					fSegments.push_back(s);
					p += 11;
				}
				break;
			}
			case 5:
				fDataStart = uint32_t(at);
				break;
		}
		at += length;
	}
	return true;
}


// CLex_Contains's bsearch (FUN_002cb864: the prefixes' bytes)
const CompressedLexicon::Segment*
CompressedLexicon::Find(const uint8_t prefix[3]) const
{
	auto it = std::lower_bound(fSegments.begin(), fSegments.end(), prefix,
		[](const Segment& s, const uint8_t* p) { return memcmp(s.prefix, p, 3) < 0; });
	if (it == fSegments.end() || memcmp(it->prefix, prefix, 3) != 0)
		return nullptr;
	return &*it;
}


// FUN_002cc3a0: each word is the count of letters it shares with the last
// after the prefix, then letters (a code below 32 ends it: 0 alone, 1 and
// an extended suffix, 2-31 a simple suffix)
std::vector<std::string>
CompressedLexicon::SegmentWords(size_t index) const
{
	std::vector<std::string> words;
	if (index >= fSegments.size())
		return words;
	const Segment& s = fSegments[index];
	size_t at = size_t(fDataStart) + s.offset, end = std::min(fData.size(), at + s.size);
	char buffer[128];
	memcpy(buffer, s.prefix, 3);
	buffer[3] = 0;
	while (at < end) {
		size_t p = 3 + fData[at++];
		bool done = false;
		while (!done && at < end) {
			uint8_t c;
			if (fCharSet == 1)
				c = fData[at++];
			else {
				c = at + 1 < end ? fData[at + 1] : 0;
				at += 2;
			}
			if (c >= 0x20) {
				if (p < sizeof(buffer) - 1)
					buffer[p++] = char(c);
			} else if (c == 0) {
				buffer[std::min(p, sizeof(buffer) - 1)] = 0;
				done = true;
			} else {
				const std::string* suffix = nullptr;
				if (c == 1) {
					uint8_t k;
					if (fCharSet == 1)
						k = at < end ? fData[at++] : 0;
					else {
						k = at + 1 < end ? fData[at + 1] : 0;
						at += 2;
					}
					if (k < fExtendedSuffixes.size())
						suffix = &fExtendedSuffixes[k];
				} else if (size_t(c - 2) < fSimpleSuffixes.size())
					suffix = &fSimpleSuffixes[c - 2];
				std::string tail = suffix != nullptr ? *suffix : std::string();
				size_t n = std::min(tail.size(), sizeof(buffer) - 1 - std::min(p, sizeof(buffer) - 1));
				memcpy(buffer + std::min(p, sizeof(buffer) - 1), tail.data(), n);
				buffer[std::min(p + n, sizeof(buffer) - 1)] = 0;
				done = true;
			}
		}
		words.push_back(buffer);
	}
	return words;
}


// FUN_002cc3a0 with an exact comparison (words sorted: stops past it) or
// FUN_002cb8f8 (any case)
bool
CompressedLexicon::ScanSegment(const Segment& segment, const std::string& word, int mode) const
{
	size_t index = size_t(&segment - &fSegments[0]);
	for (const std::string& w : SegmentWords(index)) {
		if (mode == 0) {
			int c = Compare(word, w);
			if (c == 0)
				return true;
			if (c < 0)
				return false;
		} else if (CompareNC(word, w) == 0)
			return true;
	}
	return false;
}


// CLex_Contains: by the first three letters' segment (in any case of them
// unless case sensitive); a word of one or two letters is its prefix
int
CompressedLexicon::Contains(const std::string& word, bool caseSensitive,
	std::string& replacement) const
{
	uint8_t key[3] = {0, 0, 0};
	for (size_t i = 0; i < 3 && i < word.size(); i++)
		key[i] = uint8_t(word[i]);
	if (word.size() < 3) {
		if (!caseSensitive) {
			for (int combination = 0; combination < 8; combination++) {
				for (int i = 0; i < 3; i++)
					key[i] = (combination >> i & 1) ? Upper(key[i]) : Lower(key[i]);
				if (Find(key) != nullptr)
					return 'i';
			}
			return -3;
		}
		return Find(key) != nullptr ? 'i' : -3;
	}
	if (!caseSensitive) {
		for (int combination = 0; combination < 8; combination++) {
			for (int i = 0; i < 3; i++)
				key[i] = (combination >> i & 1) ? Upper(key[i]) : Lower(key[i]);
			const Segment* s = Find(key);
			if (s != nullptr && ScanSegment(*s, word, 1))
				return 'i';
		}
		return -3;
	}
	const Segment* s = Find(key);
	return s != nullptr && ScanSegment(*s, word, 0) ? 'i' : -3;
}


// CLex_Suggest: the words of each segment whose prefix is like the word's
// beginning (scored at least 100 less the depth), scored by `compare`
void
CompressedLexicon::Suggest(const std::string& word, int depth,
	int (*compare)(const std::string&, const std::string&), SuggestionSet& set) const
{
	for (size_t k = 0; k < fSegments.size(); k++) {
		const Segment& s = fSegments[k];
		std::string prefix(reinterpret_cast<const char*>(s.prefix),
			strnlen(reinterpret_cast<const char*>(s.prefix), 3));
		if (s.prefix[2] == 0) {
			int score = compare(word, prefix);
			if (score > 0)
				set.Add(prefix, score);
			continue;
		}
		if (compare(word.substr(0, 3), prefix) < 100 - depth)
			continue;
		for (const std::string& w : SegmentWords(k)) {
			int score = compare(word, w);
			if (score > 0)
				set.Add(w, score);
		}
	}
}


// #pragma mark - TLex


// TLex_Open, FUN_002cfa08: "#LID n" first (else a plain list), then lines
// not starting with '#': a good word, or a word, a tab, an action and a
// replacement
bool
TextLexicon::Open(const std::string& path, std::string& error)
{
	std::ifstream file(path, std::ios::binary);
	if (!file) {
		error = "can't open " + path;
		return false;
	}
	std::stringstream text;
	text << file.rdbuf();
	fPath = path;
	Read(text.str());
	return true;
}


void
TextLexicon::Read(const std::string& text)
{
	fEntries.clear();
	std::vector<std::string> lines;
	std::string line;
	for (char c : text) {
		if (c == '\n') {
			if (!line.empty() && line.back() == '\r')
				line.pop_back();
			lines.push_back(line);
			line.clear();
		} else
			line += c;
	}
	if (!line.empty())
		lines.push_back(line);
	size_t first = 0;
	fPlain = true;
	if (!lines.empty() && !lines[0].empty() && lines[0][0] == '#') {
		fPlain = false;
		first = 1;
		// FUN_002cfc38: the Windows language id as SSCE's
		static const int16_t kIds[19][2] = {{1033, 0x616d}, {2057, 0x6272}, {1027, 0x7363},
			{1029, 0x637a}, {1030, 0x6461}, {1043, 0x6475}, {1035, 0x6669}, {1036, 0x6672},
			{1031, 0x6765}, {1038, 0x6875}, {1040, 0x6974}, {1044, 0x6362}, {2068, 0x6364},
			{1045, 0x706c}, {1046, 0x7062}, {2070, 0x706f}, {1049, 0x7275}, {1034, 0x7370},
			{1053, 0x7377}};
		std::istringstream header(lines[0]);
		std::string tag;
		int id = 0;
		header >> tag >> id;
		language = uint16_t(id);
		for (const auto& pair : kIds)
			if (pair[0] == id)
				language = uint16_t(pair[1]);
	} else
		language = 0x616d;
	for (size_t i = first; i < lines.size(); i++) {
		const std::string& l = lines[i];
		if (l.empty() || l[0] == '#')
			continue;
		size_t tab = l.find('\t');
		if (tab == std::string::npos)
			Insert(l, 'i', "");
		else
			Insert(l.substr(0, tab), tab + 1 < l.size() ? l[tab + 1] : 0,
				tab + 2 < l.size() ? l.substr(tab + 2) : "");
	}
}


// FUN_002cf714: before the first entry not smaller
void
TextLexicon::Insert(const std::string& word, char action, const std::string& replacement)
{
	size_t at = 0;
	while (at < fEntries.size() && Compare(word, fEntries[at].word) > 0)
		at++;
	fEntries.insert(fEntries.begin() + long(at), {word, action, Clip(replacement)});
}


// TLex_Contains: by bsearch when case sensitive, else the first entry of
// any case
int
TextLexicon::Contains(const std::string& word, bool caseSensitive,
	std::string& replacement) const
{
	if (fEntries.empty())
		return -3;
	const Entry* found = nullptr;
	if (!caseSensitive) {
		for (const Entry& e : fEntries)
			if (CompareNC(word, e.word) == 0) {
				found = &e;
				break;
			}
	} else {
		auto it = std::lower_bound(fEntries.begin(), fEntries.end(), word,
			[](const Entry& e, const std::string& w) { return Compare(e.word, w) < 0; });
		if (it != fEntries.end() && Compare(it->word, word) == 0)
			found = &*it;
	}
	if (found == nullptr)
		return -3;
	replacement = found->replacement;
	return uint8_t(found->action);
}


// TLex_Suggest: a change's replacement when the word is its word (99),
// good words scored by `compare`
void
TextLexicon::Suggest(const std::string& word, int depth,
	int (*compare)(const std::string&, const std::string&), SuggestionSet& set) const
{
	for (const Entry& e : fEntries) {
		switch (e.action) {
			case 'A': case 'C': case 'a': case 'c':
				if (Compare(word, e.word) == 0)
					set.Add(e.replacement, 99);
				break;
			case 'e':
				break;
			case 'i':
			{
				int score = compare(word, e.word);
				if (score > 0)
					set.Add(e.word, score);
				break;
			}
		}
	}
}


// FUN_002cfd1c: "#LID n" and "word<tab>action..." lines, or the plain
// words
std::string
TextLexicon::Text() const
{
	std::string text;
	if (!fPlain) {
		static const int16_t kIds[][2] = {{1033, 0x616d}, {2057, 0x6272}};
		int id = language;
		for (const auto& pair : kIds)
			if (pair[1] == language)
				id = pair[0];
		text += "#LID " + std::to_string(id) + "\n";
	}
	for (const Entry& e : fEntries) {
		if (fPlain)
			text += e.word + "\n";
		else
			text += e.word + "\t" + std::string(1, e.action) + e.replacement + "\n";
	}
	return text;
}


bool
TextLexicon::Write() const
{
	if (fPath.empty())
		return true;
	std::string temporary = fPath + ".new";
	{
		std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
		if (!file)
			return false;
		file << Text();
		if (!file)
			return false;
	}
	return std::rename(temporary.c_str(), fPath.c_str()) == 0;
}


// TLex_Add: a word already there is replaced
bool
TextLexicon::Add(const std::string& word, char action, const std::string& replacement)
{
	std::string dummy;
	if (Contains(word, true, dummy) >= 0)
		Delete(word);
	Insert(Clip(word), action, replacement);
	return Write();
}


// TLex_Delete (FUN_002cf888)
bool
TextLexicon::Delete(const std::string& word)
{
	auto it = std::find_if(fEntries.begin(), fEntries.end(),
		[&](const Entry& e) { return Compare(e.word, word) == 0; });
	if (it == fEntries.end())
		return false;
	fEntries.erase(it);
	return Write();
}


bool
TextLexicon::Clear()
{
	fEntries.clear();
	return Write();
}


// #pragma mark - Block


// BlockOpen: the cursor on the first letter or digit
Block::Block(const std::string& text, const uint32_t* options)
	:
	fText(text),
	fOptions(options),
	fPrevious(UINT32_MAX)
{
	while (fCursor < fText.size() && !IsAlnum(At(fCursor)))
		fCursor++;
}


// BlockGetWord: letters and digits, with apostrophes, periods inside
// (an abbreviation keeps its last one) and hyphens (unless they split
// words); "s'" loses its apostrophe
bool
Block::GetWord(std::string& word) const
{
	return WordAt(fCursor, word);
}


bool
Block::WordAt(uint32_t cursor, std::string& word, uint8_t* after) const
{
	word.clear();
	uint32_t i = cursor, length = uint32_t(fText.size());
	while (i < length && !IsAlnum(At(i)))
		i++;
	if (i >= length)
		return false;
	word += char(At(i++));
	bool period = false;
	auto wordChar = [&](uint8_t c) { return IsAlnum(c) || IsQuote(c); };
	while (i < length && word.size() < 63) {
		uint8_t c = At(i);
		if (c == '.') {
			if (!wordChar(At(i - 1)) || length <= i + 1 || !wordChar(At(i + 1)))
				break;
			period = true;
		} else if (c == '-' && (fOptions == nullptr || (*fOptions & kSplitHyphenated) == 0)) {
			if (!wordChar(At(i - 1)) || length < i + 1)
				break;
			if (!IsAlnum(At(i + 1)) && !IsQuote(At(i + 1)))
				break;
		} else if (!IsAlnum(c) && !IsQuote(c))
			break;
		word += char(c);
		i++;
	}
	if (period && i < length && At(i) == '.' && word.size() < 63)
		word += '.';
	size_t n = word.size();
	if (n > 1 && IsQuote(uint8_t(word[n - 1])) && (word[n - 2] == 's' || word[n - 2] == 'S'))
		word.pop_back();
	if (after != nullptr)
		*after = At(i);
	return true;
}


// BlockNextWord: past the word and what follows; punctuation between
// words keeps them from being doubled
bool
Block::NextWord()
{
	uint32_t length = uint32_t(fText.size());
	fPrevious = fCursor;
	auto skip = [&]() {
		while (fCursor < length && !IsAlnum(At(fCursor))) {
			if ((CharClass(At(fCursor)) & (kAlpha | kDigit | kPunct | kSpace))
					&& !(CharClass(At(fCursor)) & kSpace))
				fPrevious = UINT32_MAX;
			fCursor++;
		}
	};
	skip();
	if (fCursor >= length)
		return false;
	std::string word;
	GetWord(word);
	fCursor += uint32_t(word.size());
	if (fCursor >= length)
		return false;
	skip();
	if (fCursor >= length)
		return false;
	fFirst = false;
	return true;
}


// BlockIsDoubledWord: the word is the last one again (and no
// punctuation follows the last)
bool
Block::IsDoubledWord(bool caseSensitive) const
{
	if (fPrevious >= fText.size() || fPrevious == fCursor)
		return false;
	std::string word, last;
	GetWord(word);
	uint8_t after = 0;
	WordAt(fPrevious, last, &after);
	if (CharClass(after) & kPunct)
		return false;
	return caseSensitive ? Compare(last, word) == 0 : CompareNC(last, word) == 0;
}


// BlockSetCursor
void
Block::SetCursor(uint32_t cursor)
{
	fCursor = std::min(cursor, uint32_t(fText.size()));
	fPrevious = UINT32_MAX;
}


bool
Block::ReplaceWord(const std::string& replacement)
{
	std::string word;
	if (!GetWord(word))
		return false;
	uint32_t at = fCursor;
	while (at < fText.size() && !IsAlnum(At(at)))
		at++;
	fText.replace(at, word.size(), replacement);
	return true;
}


// #pragma mark - Session


int
Session::AddLex(std::unique_ptr<Lexicon> lexicon)
{
	if (fLexicons.size() >= 32)
		return -5;
	fLexicons.push_back(std::move(lexicon));
	return int(fLexicons.size() - 1);
}


Lexicon*
Session::Lex(int index) const
{
	return index >= 0 && size_t(index) < fLexicons.size() ? fLexicons[size_t(index)].get()
		: nullptr;
}


// SSCE_OpenLex, FUN_002c9518: compressed if it starts so, else text
int
Session::OpenLex(const std::string& path, std::string& error)
{
	std::ifstream file(path, std::ios::binary);
	if (!file) {
		error = "can't open " + path;
		return -4;
	}
	std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)),
		std::istreambuf_iterator<char>());
	if (U32(data, 0) == 0x57000501) {
		auto lex = std::make_unique<CompressedLexicon>();
		if (!lex->Open(data, error))
			return -26;
		return AddLex(std::move(lex));
	}
	auto lex = std::make_unique<TextLexicon>();
	if (!lex->Open(path, error))
		return -4;
	return AddLex(std::move(lex));
}


// FUN_002c877c
int
Session::LexContains(uint32_t opts, const Lexicon& lex, const std::string& word,
	std::string* replacement) const
{
	std::string found;
	int r = lex.Contains(word, (opts & kCaseSensitive) != 0, found);
	if (r == 'A' || r == 'C') {
		MatchCase(word, found);
	}
	if (r > 0 && replacement != nullptr && (r == 'A' || r == 'C' || r == 'a' || r == 'c'))
		*replacement = found;
	return r;
}


// FUN_002c8858: in a lexicon as it is; else, case sensitive, with a small
// first letter, or (all capitals) in any case
int
Session::Lookup(uint32_t opts, const std::string& word, std::string* replacement) const
{
	auto each = [&](uint32_t o, const std::string& w, int& result) {
		for (const auto& lex : fLexicons) {
			std::string found;
			int r = LexContains(o, *lex, w, &found);
			if (r > 0) {
				if (r == 'A' || r == 'C' || r == 'a' || r == 'c') {
					MatchCase(word, found);
					if (replacement != nullptr)
						*replacement = found;
				}
				result = ActionResult(r);
				return true;
			}
		}
		return false;
	};
	int result;
	if (each(opts, word, result))
		return result;
	if ((opts & kCaseSensitive) && !word.empty() && (CharClass(uint8_t(word[0])) & kUpper)) {
		std::string lower = Clip(word);
		lower[0] = char(Lower(uint8_t(lower[0])));
		if (each(opts, lower, result))
			return result;
	}
	if (opts & kCaseSensitive) {
		bool allUpper = true;
		for (char c : word) {
			uint8_t k = CharClass(uint8_t(c));
			if ((k & kAlpha) && !(k & kUpper)) {
				allUpper = false;
				break;
			}
		}
		if (allUpper && each(opts & ~uint32_t(kCaseSensitive), word, result))
			return result;
	}
	return kMisspelled;
}


int
Session::CheckWord(const std::string& word, std::string* replacement) const
{
	return CheckWord(options, word, replacement);
}


// FUN_002c8c00
int
Session::CheckWord(uint32_t opts, const std::string& word, std::string* replacement) const
{
	int result = 0;
	bool mixed = HasMixedCase(word);
	std::string normal = Normalize(word, opts);
	if (normal.empty())
		return 0;
	if (replacement != nullptr)
		*replacement = Clip(word);
	uint8_t first = uint8_t(word[0]);
	if ((opts & kIgnoreCappedWord) && (CharClass(first) & kUpper))
		return result;
	if ((opts & kIgnoreMixedCase) && mixed)
		return result;
	if (opts & kIgnoreMixedDigits)
		for (char c : word)
			if (CharClass(uint8_t(c)) & kDigit)
				return result;
	if (opts & kIgnoreAllCaps) {
		bool all = true;
		for (char c : word)
			if (!(CharClass(uint8_t(c)) & kUpper))
				all = false;
		if (all)
			return result;
	}
	if (opts & kReportSpelling) {
		bool good = false;
		int r = Lookup(opts, normal, replacement);
		if (r < 0)
			return r;
		if ((r & kMisspelled) == 0)
			good = true;
		else {
			if (opts & kSplitWords) {
				// in words of the lexicons one after another, the longest
				// first (any case after the first)
				bool split = true;
				size_t at = 0;
				while (at < normal.size() && split) {
					bool missing = true;
					size_t length = normal.size() - at;
					while (missing && length != 0) {
						uint32_t o = opts;
						if (at != 0)
							o &= ~uint32_t(kCaseSensitive);
						r = Lookup(o, normal.substr(at, length), replacement);
						missing = (r & kMisspelled) != 0;
						if (missing)
							length--;
					}
					if (!missing)
						at += length;
					else
						split = false;
				}
				if (split)
					good = true;
			}
			if (!good && (opts & (kSplitHyphenated | kSplitContracted))) {
				bool all = true;
				size_t at = 0;
				while (at < normal.size() && all) {
					std::string part = SplitPart(normal, at, opts);
					r = Lookup(opts, part, replacement);
					if (r < 0)
						return r;
					if (r & kMisspelled)
						all = false;
				}
				if (all)
					good = true;
			}
		}
		if (!good)
			result |= kMisspelled;
		else
			result |= r;
	}
	if (opts & kReportUncapped) {
		if (!(CharClass(first) & kUpper))
			result |= kUncapped;
		if (replacement != nullptr && !replacement->empty())
			(*replacement)[0] = char(Upper(uint8_t((*replacement)[0])));
	}
	if ((opts & kReportMixedCase) && mixed) {
		result |= kMixedCase;
		if (replacement != nullptr) {
			for (char& c : *replacement)
				c = char(Lower(uint8_t(c)));
			if ((CharClass(first) & kUpper) && !replacement->empty())
				(*replacement)[0] = char(Upper(uint8_t((*replacement)[0])));
		}
	}
	if ((result & kMisspelled) && (opts & kReportMixedDigits))
		for (char c : word)
			if (CharClass(uint8_t(c)) & kDigit) {
				result |= kMixedDigits;
				break;
			}
	// FUN_002c9a30: the possessive back on the replacement
	if ((opts & kStripPossessives) && replacement != nullptr && word.size() > 1) {
		std::string tail = word.substr(word.size() - 2);
		if (CompareNC(tail, "'s") == 0 || CompareNC(tail, "s'") == 0)
			*replacement = Clip(*replacement + tail);
	}
	return result;
}


// SSCE_CheckBlock: words of digits are good; the first word only is
// checked for a capital
int
Session::CheckBlock(Block& block, std::string& word, std::string* replacement) const
{
	uint32_t opts = options;
	do {
		if (!block.GetWord(word))
			return kEndOfBlock;
		bool digits = true;
		for (char c : word)
			if (!(CharClass(uint8_t(c)) & kDigit)) {
				digits = false;
				break;
			}
		if ((opts & kReportUncapped) && !block.IsFirstWord())
			opts &= ~uint32_t(kReportUncapped);
		int r = 0;
		if (!digits) {
			r = CheckWord(opts, word, replacement);
			if ((opts & kReportDoubledWord) && block.IsDoubledWord((opts & kCaseSensitive) != 0))
				r |= kDoubledWord;
		}
		if (r != 0)
			return r;
	} while (block.NextWord());
	return kEndOfBlock;
}


// SSCE_Suggest, FUN_002c9580, FUN_002c9400: from the lexicons (the last
// opened first), typographically and phonetically like the word; split in
// two good words; in the word's case, a repeat after case matching left
// out
std::vector<std::pair<std::string, int>>
Session::Suggest(const std::string& word, int depth, size_t count) const
{
	std::vector<std::pair<std::string, int>> out;
	std::string normal = Normalize(word, options);
	if (normal.empty() || count == 0)
		return out;
	SuggestionSet set(count);
	for (size_t i = fLexicons.size(); i-- > 0;) {
		if (options & kSuggestTypographical)
			fLexicons[i]->Suggest(normal, depth, TypographicalScore, set);
		if (options & kSuggestPhonetic)
			fLexicons[i]->Suggest(normal, depth, PhoneticScore, set);
	}
	if (options & kSuggestSplitWords) {
		for (size_t at = 1; at < normal.size(); at++) {
			std::string first = normal.substr(0, at), rest = normal.substr(at);
			if ((CheckWord(kReportSpelling, first, nullptr) & kMisspelled) == 0
					&& (CheckWord(kReportSpelling, rest, nullptr) & kMisspelled) == 0) {
				std::string both = first + " " + rest;
				set.Add(both, TypographicalScore(normal, both));
			}
		}
	}
	for (size_t i = 0; i < set.entries.size(); i++) {
		std::string s = set.entries[i].first;
		MatchCase(word, s);
		if (!out.empty() && out.back().first == s)
			continue;
		out.push_back({s, set.entries[i].second});
	}
	return out;
}

} // namespace ssce
} // namespace gobe
