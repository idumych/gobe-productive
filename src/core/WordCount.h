// Writing Tools > Word Count… (TWordCountDialog): characters, words,
// paragraphs and pages.
#ifndef GOBE_WORD_COUNT_H
#define GOBE_WORD_COUNT_H

#include <cstdint>
#include <string>

namespace gobe {

struct Document;

struct WordCount {
	int64_t characters = 0, words = 0, paragraphs = 0, pages = 0;
};

// TWordCountDialog::countBlock: a block of UTF-8 text's characters and
// words (GetNextWordInBlock: letters and digits, joined by apostrophes and
// by hyphens and periods between them; CJK characters count as words too,
// HasCJKChars)
void CountBlock(const std::string& text, WordCount& count);
// TWordCountDialog::countDoc: every paragraph of every text part shown
// (TTextAccessor; each a block and a paragraph) and the pages
WordCount CountDocument(const Document& doc);

} // namespace gobe

#endif
