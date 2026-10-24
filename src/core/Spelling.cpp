// Spell checking.
#include "Spelling.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>

#include "Document.h"
#include "WordCount.h"

namespace gobe {

using namespace ssce;

namespace {

uint32_t
U32(const std::vector<uint8_t>& data, size_t at)
{
	return uint32_t(data[at]) | uint32_t(data[at + 1]) << 8 | uint32_t(data[at + 2]) << 16
		| uint32_t(data[at + 3]) << 24;
}


void
SetOption(Session& session, uint32_t option, bool on)
{
	if (on)
		session.options |= option;
	else
		session.options &= ~option;
}


std::vector<size_t>
TextParts(const Document& doc)
{
	return ShownTextParts(doc);
}

} // namespace


bool
SpellPrefs::Read(const std::vector<uint8_t>& data)
{
	if (data.size() < 28)
		return false;
	bool* flags[6] = {&ignoreMixedCase, &ignoreMixedDigits, &ignoreAllCaps, &ignoreCapitalized,
		&suggestTypo, &suggestPhonetic};
	for (int i = 0; i < 6; i++)
		*flags[i] = U32(data, size_t(i) * 4) != 0;
	depth = int32_t(U32(data, 24));
	size_t at = 28;
	for (std::string* s : {&mainDict, &userDict}) {
		size_t end = at;
		while (end < data.size() && data[end] != 0)
			end++;
		s->assign(data.begin() + long(std::min(at, data.size())), data.begin() + long(end));
		at = end + 1;
	}
	return true;
}


std::string
Latin1FromUtf8(const std::string& text)
{
	std::string out;
	for (size_t i = 0; i < text.size();) {
		uint8_t c = uint8_t(text[i]);
		size_t n = c < 0x80 ? 1 : c < 0xe0 ? 2 : c < 0xf0 ? 3 : 4;
		uint32_t code = c;
		if (n == 2 && i + 1 < text.size())
			code = (c & 0x1f) << 6 | (uint8_t(text[i + 1]) & 0x3f);
		out += n == 1 || (n == 2 && code < 0x100) ? char(code) : char(0x1a);
		i += n;
	}
	return out;
}


std::string
Utf8FromLatin1(const std::string& text)
{
	std::string out;
	for (char ch : text) {
		uint8_t c = uint8_t(ch);
		if (c < 0x80)
			out += ch;
		else {
			out += char(0xc0 | c >> 6);
			out += char(0x80 | (c & 0x3f));
		}
	}
	return out;
}


// #pragma mark - Speller


// TSpellSession::StartSession: a session; the temporary lexicon
// (SSCE_CreateLex, language 'xx'); the user dictionary; the language
// folder's lexicons, its name and 1 to 5 (isValidMainFolder: at least the
// first); the options (SetUserOptions, then by language)
int
Speller::Start(const std::string& dictionaries, const SpellPrefs& prefs,
	const std::vector<std::string>& languages)
{
	if (fSession != nullptr)
		return 0;
	if (dictionaries.empty())
		return 1;
	auto session = std::make_unique<ssce::Session>();
	auto temp = std::make_unique<TextLexicon>();
	temp->language = 0x7878;
	fTemp = session->AddLex(std::move(temp));
	std::string error;
	fUser = session->OpenLex(dictionaries + "/" + prefs.userDict, error);
	if (fUser < 0)
		return 4;
	std::string folder = dictionaries + "/" + prefs.mainDict + "/" + prefs.mainDict;
	int opened = 0;
	for (int i = 1; i <= 5; i++) {
		std::string path = folder + std::to_string(i);
		if (FILE* f = fopen(path.c_str(), "rb"))
			fclose(f);
		else
			break;
		if (session->OpenLex(path, error) < 0)
			return 3;
		opened++;
	}
	if (opened == 0)
		return 3;

	fSession = std::move(session);
	SetUserOptions(prefs);
	auto is = [&](size_t i) { return i < languages.size() && languages[i] == prefs.mainDict; };
	SetOption(*fSession, kSplitContracted, is(0) || is(1));
	bool compounds = is(2) || is(3);
	SetOption(*fSession, kSplitWords, compounds);
	if (!compounds)
		SetOption(*fSession, kSuggestSplitWords, true);
	return 0;
}


void
Speller::SetUserOptions(const SpellPrefs& prefs)
{
	if (fSession == nullptr)
		return;
	ssce::Session& session = *fSession;
	SetOption(session, kIgnoreMixedCase, prefs.ignoreMixedCase);
	SetOption(session, kIgnoreMixedDigits, prefs.ignoreMixedDigits);
	SetOption(session, kIgnoreAllCaps, prefs.ignoreAllCaps);
	SetOption(session, kSuggestTypographical, prefs.suggestTypo);
	SetOption(session, kSuggestPhonetic, prefs.suggestPhonetic);
	SetOption(session, kIgnoreCappedWord, prefs.ignoreCapitalized);
	fDepth = prefs.depth == 0 ? 20 : prefs.depth == 2 ? 100 : 50;
}


void
Speller::Close()
{
	fSession.reset();
	fTemp = fUser = -1;
}


int
Speller::CheckWord(const std::string& word)
{
	if (fSession == nullptr)
		return kNoSession;
	if (word.empty())
		return kQuestionable;
	// the word's buffer holds 64 characters
	fWord = Latin1FromUtf8(word).substr(0, 64);
	fReplacement.clear();
	int result = fSession->CheckWord(fWord, &fReplacement);
	if (result == 0)
		return kGood;
	if (result & ssce::kUncapped)
		return kUncapped;
	if (result & ssce::kMixedCase)
		return kMixedCase;
	if (result & ssce::kMixedDigits)
		return kMixedDigits;
	if (result & kAutoChange) {
		// one chosen for it before (SSCE_FindLexWord in the temporary
		// lexicon: 'A')
		std::string replacement;
		if (fSession->Lex(fTemp)->Contains(fWord, true, replacement) == 'A') {
			fAuto = Utf8FromLatin1(fReplacement);
			return kAutoReplace;
		}
	}
	return kQuestionable;
}


std::vector<std::string>
Speller::Alternatives()
{
	std::vector<std::string> labels;
	fFound.clear();
	if (fSession == nullptr)
		return labels;
	std::vector<int> scores;
	bool replacement = !fReplacement.empty() && fReplacement != fWord;
	if (replacement) {
		fFound.push_back(fReplacement);
		scores.push_back(100);
	}
	for (const auto& s : fSession->Suggest(fWord, fDepth, 7)) {
		fFound.push_back(s.first);
		scores.push_back(s.second);
	}
	for (size_t i = 0; i < fFound.size() && !fFound[i].empty() && scores[i] > 74
			&& scores[0] - scores[i] < 31; i++) {
		if (replacement && i >= 1 && fFound[i] == fReplacement)
			continue;
		labels.push_back(Utf8FromLatin1(fFound[i]));
	}
	return labels;
}


bool
Speller::AddReplacement(int index)
{
	if (fSession == nullptr || index < 0 || index > 6 || size_t(index) >= fFound.size())
		return false;
	auto* temp = dynamic_cast<TextLexicon*>(fSession->Lex(fTemp));
	return temp != nullptr && temp->Add(fWord, 'A', fFound[size_t(index)]);
}


bool
Speller::Skip()
{
	auto* temp = fSession != nullptr ? dynamic_cast<TextLexicon*>(fSession->Lex(fTemp)) : nullptr;
	return temp != nullptr && !fWord.empty() && temp->Add(fWord);
}


bool
Speller::Learn()
{
	auto* user = fSession != nullptr ? dynamic_cast<TextLexicon*>(fSession->Lex(fUser)) : nullptr;
	return user != nullptr && !fWord.empty() && user->Add(fWord);
}


void
Speller::ClearSkipped()
{
	if (auto* temp = fSession != nullptr ? dynamic_cast<TextLexicon*>(fSession->Lex(fTemp))
			: nullptr)
		temp->Clear();
}


void
DictionaryChoices(const std::string& dictionaries, std::vector<std::string>& languages,
	std::vector<std::string>& users)
{
	namespace fs = std::filesystem;
	languages.clear();
	users.clear();
	std::error_code error;
	std::vector<fs::path> entries;
	for (const auto& entry : fs::directory_iterator(dictionaries, error))
		entries.push_back(entry.path());
	// (BFS lists a folder by name)
	std::sort(entries.begin(), entries.end());
	for (const fs::path& path : entries) {
		std::string name = path.filename().string();
		// (the attributes a copy of the original keeps beside its files
		// aren't lexicons)
		if (name.size() > 11 && name.compare(name.size() - 11, 11, ".attrs.json") == 0)
			continue;
		Session session;
		std::string message;
		if (fs::is_directory(path, error)) {
			if (session.OpenLex((path / (name + "1")).string(), message) >= 0)
				languages.push_back(name);
		} else if (fs::is_regular_file(path, error)) {
			int lex = session.OpenLex(path.string(), message);
			if (lex >= 0 && dynamic_cast<TextLexicon*>(session.Lex(lex)) != nullptr)
				users.push_back(name);
		}
	}
}


bool
CreateUserDictionary(const std::string& dictionaries, const std::string& name)
{
	if (name.empty())
		return false;
	std::string path = dictionaries + "/" + name;
	if (FILE* f = fopen(path.c_str(), "rb")) {
		fclose(f);
		return false;
	}
	// (the original passes SSCE_CreateLex no language: whatever its stack
	// holds; the session's is written)
	TextLexicon lexicon;
	std::ofstream file(path, std::ios::binary);
	file << lexicon.Text();
	return bool(file);
}


// #pragma mark - the walk


bool
SpellStartFromTop(const Document& doc, SpellPosition& at)
{
	std::vector<size_t> parts = TextParts(doc);
	for (size_t part : parts) {
		const WPPart* wp = doc.WordPart(part);
		if (wp != nullptr && !wp->paragraphs.empty()) {
			at = {part, 0, 0};
			return true;
		}
	}
	return false;
}


bool
SpellStartFromPosition(const Document& doc, SpellPosition& at)
{
	std::vector<size_t> parts = TextParts(doc);
	const WPPart* wp = doc.WordPart(at.part);
	if (std::find(parts.begin(), parts.end(), at.part) == parts.end() || wp == nullptr
			|| at.paragraph >= wp->paragraphs.size())
		return false;
	const std::string& text = wp->paragraphs[at.paragraph].text;
	at.offset = std::min(at.offset, text.size());
	while (at.offset > 0 && at.offset < text.size() && uint8_t(text[at.offset]) >= 0x31)
		at.offset--;
	return true;
}


int
FindMisspelling(const Document& doc, Speller& speller, SpellPosition& at, size_t& length)
{
	std::vector<size_t> parts = TextParts(doc);
	auto it = std::find(parts.begin(), parts.end(), at.part);
	for (; it != parts.end(); ++it) {
		const WPPart* wp = doc.WordPart(*it);
		if (*it != at.part)
			at = {*it, 0, 0};
		for (; wp != nullptr && at.paragraph < wp->paragraphs.size();
				at.paragraph++, at.offset = 0) {
			const std::string& text = wp->paragraphs[at.paragraph].text;
			size_t pos = at.offset;
			bool digits;
			while (NextWordInBlock(text, pos, length, digits)) {
				if (!digits) {
					int result = speller.CheckWord(text.substr(pos, length));
					if (result != Speller::kGood) {
						at.offset = pos;
						return result;
					}
				}
				pos += length;
			}
		}
	}
	return 0;
}

} // namespace gobe
