// RecentDocs: the documents opened or saved last, as symbolic links in
// "Gobe Productive Recent Documents" (STR# 5, 27) in the user's settings
// folder, ten at most; File > Open Recent lists them (RecentDocsMenu).
#ifndef GOBE_TRECENT_DOCS_H
#define GOBE_TRECENT_DOCS_H

class BMenu;

namespace gobe {

namespace RecentDocs {

// RecentDocs::AddEntry: a document opened or saved
void Add(const char* path);
// RecentDocsMenu: one item per document, newest first, each sending the
// application B_REFS_RECEIVED; STR# 5, 4 (disabled) if there are none
void FillMenu(BMenu* menu);

} // namespace RecentDocs

} // namespace gobe

#endif
