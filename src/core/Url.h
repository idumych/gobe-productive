// ParseURL: the URL a text's selection (or the place clicked) is in, for a
// text part's context menu's Open URL.
#ifndef GOBE_URL_H
#define GOBE_URL_H

#include <string>

namespace gobe {

// ParseURL(text, length, start, end, url): with no selection (start ==
// end) the run of characters around it up to a space or bracket (an
// opening bracket or quote kept with its closing one); spaces and tabs off
// its ends, line breaks and the spaces around them out; a bracket or quote
// pair around it, else a period after it, off; "URL:" or "URI:" off. A URL
// has a ':' with a character on each side; false if there's none.
bool ParseURL(const std::string& text, size_t start, size_t end, std::string& url);

} // namespace gobe

#endif
