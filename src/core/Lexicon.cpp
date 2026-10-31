// The thesaurus and dictionary files (TThesaurus, TDictionary, DictIndex).
#include "Lexicon.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <iterator>

namespace gobe {

namespace {

// a line's tokens: split at ':', ',' and the line's end (the original's
// strtok_r delimiters ":,\n")
std::vector<std::string>
Tokens(const std::string& line)
{
	std::vector<std::string> out;
	std::string token;
	for (char c : line) {
		if (c == ':' || c == ',' || c == '\n' || c == '\r') {
			if (!token.empty())
				out.push_back(token);
			token.clear();
		} else
			token += c;
	}
	if (!token.empty())
		out.push_back(token);
	return out;
}


// TThesaurus::AddPart: "-noun-" -> "noun"
std::string
PartName(const std::string& token)
{
	std::string name;
	for (char c : token)
		if (c != '-' && c != ',' && c != ' ')
			name += c;
	return name;
}


ThesaurusPart&
PartOf(std::vector<ThesaurusPart>& parts, const std::string& name)
{
	for (ThesaurusPart& p : parts)
		if (p.name == name)
			return p;
	parts.push_back({name, {}});
	return parts.back();
}


std::string
Lower(std::string s)
{
	for (char& c : s)
		c = char(tolower((unsigned char)c));
	return s;
}

} // namespace


std::vector<ThesaurusPart>
ThesaurusLookup(const std::string& path, const std::string& word)
{
	std::vector<ThesaurusPart> parts;
	std::ifstream in(path, std::ios::binary);
	if (!in || word.empty())
		return parts;
	std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	// the binary search: the line starting at or after the middle
	size_t low = 0, high = data.size();
	std::string found;
	while (low < high) {
		size_t middle = low + (high - low) / 2;
		size_t start = middle == 0 ? 0 : data.find('\n', middle - 1);
		start = start == std::string::npos ? data.size() : (middle == 0 ? 0 : start + 1);
		if (start >= high) {
			high = middle;
			continue;
		}
		size_t end = data.find('\n', start);
		std::string line = data.substr(start, end == std::string::npos ? std::string::npos
			: end - start);
		size_t colon = line.find(':');
		if (colon == std::string::npos)
			break;
		int order = strcmp(line.substr(0, colon).c_str(), word.c_str());
		if (order == 0) {
			found = line.substr(colon + 1);
			break;
		}
		if (order < 0)
			low = end == std::string::npos ? data.size() : end + 1;
		else
			high = start;
	}
	if (!found.empty()) {
		ThesaurusPart* part = nullptr;
		for (const std::string& t : Tokens(found)) {
			if (t[0] == '-')
				part = &PartOf(parts, PartName(t));
			else {
				if (part == nullptr)
					part = &PartOf(parts, "");
				part->words.push_back(t);
			}
		}
		parts.erase(std::remove_if(parts.begin(), parts.end(),
			[](const ThesaurusPart& p) { return p.words.empty(); }), parts.end());
		if (!parts.empty())
			return parts;
	}
	// TThesaurus::Manufacture: every line that lists the word, its
	// headword under the part the word is in
	for (size_t at = 0; at < data.size();) {
		size_t end = data.find('\n', at);
		if (end == std::string::npos)
			end = data.size();
		std::string line = data.substr(at, end - at);
		at = end + 1;
		std::vector<std::string> tokens = Tokens(line);
		std::string part;
		for (size_t i = 1; i < tokens.size(); i++) {
			if (tokens[i][0] == '-')
				part = PartName(tokens[i]);
			else if (tokens[i] == word) {
				ThesaurusPart& p = PartOf(parts, part);
				if (std::find(p.words.begin(), p.words.end(), tokens[0]) == p.words.end())
					p.words.push_back(tokens[0]);
				break;
			}
		}
	}
	return parts;
}


bool
Dictionary::Open(const std::string& path)
{
	std::ifstream in(path, std::ios::binary);
	if (!in)
		return false;
	fData.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	fEntries.clear();
	for (size_t at = 0; (at = fData.find('@', at)) != std::string::npos; at++)
		if (at == 0 || fData[at - 1] == '\n')
			fEntries.push_back(at);
	return true;
}


std::string
Dictionary::Headword(size_t entry) const
{
	if (entry >= fEntries.size())
		return "";
	size_t at = fEntries[entry] + 1;
	size_t end = fData.find('\n', at);
	std::string word;
	for (size_t i = at; i < (end == std::string::npos ? fData.size() : end); i++)
		if (fData[i] != '.')
			word += fData[i];
	return word;
}


std::string
Dictionary::Text(size_t entry) const
{
	if (entry >= fEntries.size())
		return "";
	size_t at = fData.find('\n', fEntries[entry]);
	if (at == std::string::npos)
		return "";
	size_t end = entry + 1 < fEntries.size() ? fEntries[entry + 1] : fData.size();
	std::string text = fData.substr(at + 1, end - at - 1);
	while (!text.empty() && (text.back() == '\n' || text.back() == ' '))
		text.pop_back();
	return text;
}


size_t
Dictionary::Find(const std::string& word) const
{
	if (word.empty())
		return fEntries.size();
	std::string want = Lower(word);
	// the entries are sorted ignoring case: the first not before the word
	size_t low = 0, high = fEntries.size();
	while (low < high) {
		size_t middle = (low + high) / 2;
		if (Lower(Headword(middle)) < want)
			low = middle + 1;
		else
			high = middle;
	}
	if (low < fEntries.size() && Lower(Headword(low)).compare(0, want.size(), want) == 0)
		return low;
	return fEntries.size();
}


std::string
DictionaryRuns(const std::string& text, std::vector<std::pair<size_t, size_t>>& runs)
{
	runs.clear();
	std::string out;
	size_t start = std::string::npos;
	for (char c : text) {
		if (c == '\\') {
			if (start == std::string::npos)
				start = out.size();
			else {
				runs.push_back({start, out.size()});
				start = std::string::npos;
			}
			continue;
		}
		out += c;
	}
	return out;
}

} // namespace gobe
