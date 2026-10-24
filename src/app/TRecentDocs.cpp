// RecentDocs.
#include "TRecentDocs.h"

#include <algorithm>
#include <string>
#include <vector>

#include <Application.h>
#include <Directory.h>
#include <Entry.h>
#include <FindDirectory.h>
#include <Menu.h>
#include <MenuItem.h>
#include <Path.h>
#include <SymLink.h>

#include <string.h>

#include "GobeResources.h"

namespace gobe {

namespace RecentDocs {

static const int32 kLimit = 10;	// RecentDocs::RecentDocs: +0x2c


static bool
Folder(BDirectory& folder)
{
	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path, true) != B_OK)
		return false;
	BDirectory settings(path.Path());
	BString name = GobeResources::IndexString(5, 27);
	if (settings.CreateDirectory(name.String(), &folder) == B_OK)
		return true;
	path.Append(name.String());
	return folder.SetTo(path.Path()) == B_OK;
}


struct Link {
	BEntry link;
	BPath target;
	time_t when;
};


// the links whose documents still exist, newest first (the others go)
static std::vector<Link>
Links(BDirectory& folder)
{
	std::vector<Link> links;
	folder.Rewind();
	BEntry entry;
	while (folder.GetNextEntry(&entry, false) == B_OK) {
		entry_ref ref;
		if (entry.GetRef(&ref) != B_OK)
			continue;
		BEntry resolved(&ref, true);
		BPath path;
		if (!resolved.Exists() || resolved.GetPath(&path) != B_OK) {
			entry.Remove();
			continue;
		}
		time_t when = 0;
		entry.GetModificationTime(&when);
		links.push_back({entry, path, when});
	}
	std::sort(links.begin(), links.end(),
		[](const Link& a, const Link& b) { return a.when > b.when; });
	return links;
}


void
Add(const char* path)
{
	BDirectory folder;
	BPath document(path);
	if (path == NULL || path[0] == '\0' || !Folder(folder) || document.InitCheck() != B_OK)
		return;
	std::vector<Link> links = Links(folder);
	// a document already listed comes first again
	for (Link& l : links)
		if (strcmp(l.target.Path(), document.Path()) == 0)
			l.link.Remove();
	// named as the document, numbered if that name is taken
	BString name = document.Leaf();
	BSymLink link;
	for (int32 n = 2; folder.CreateSymLink(name.String(), document.Path(), &link) == B_FILE_EXISTS
			&& n < 100; n++) {
		name = document.Leaf();
		name << " " << n;
	}
	links = Links(folder);
	for (size_t i = kLimit; i < links.size(); i++)
		links[i].link.Remove();
}


void
FillMenu(BMenu* menu)
{
	if (menu == NULL)
		return;
	while (BMenuItem* item = menu->RemoveItem((int32)0))
		delete item;
	BDirectory folder;
	std::vector<Link> links;
	if (Folder(folder))
		links = Links(folder);
	if (links.empty()) {
		BMenuItem* none = new BMenuItem(GobeResources::IndexString(5, 4).String(), NULL);
		none->SetEnabled(false);
		menu->AddItem(none);
		return;
	}
	for (size_t i = 0; i < links.size() && i < size_t(kLimit); i++) {
		entry_ref ref;
		if (get_ref_for_path(links[i].target.Path(), &ref) != B_OK)
			continue;
		BMessage* open = new BMessage(B_REFS_RECEIVED);
		open->AddRef("refs", &ref);
		BMenuItem* item = new BMenuItem(links[i].target.Leaf(), open);
		item->SetTarget(be_app);
		menu->AddItem(item);
	}
}

} // namespace RecentDocs

} // namespace gobe
