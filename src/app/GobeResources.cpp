#include "GobeResources.h"

#include <stdlib.h>

#include <AppFileInfo.h>
#include <Application.h>
#include <Entry.h>
#include <File.h>
#include <FindDirectory.h>
#include <Message.h>
#include <Path.h>
#include <Query.h>
#include <Roster.h>
#include <Volume.h>
#include <VolumeRoster.h>

#include <vector>

namespace gobe {

const char* const kValueString = "value";

static ResourceFile sFile;
static BString sPath;


static BString
AppDirectory()
{
	app_info info;
	if (be_app == NULL || be_app->GetAppInfo(&info) != B_OK)
		return "";
	BPath path(&info.ref);
	if (path.GetParent(&path) != B_OK)
		return "";
	return path.Path();
}


// the port's settings: where the original binary was found
static BPath
SettingsPath()
{
	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) == B_OK)
		path.Append("Gobe Productive port settings");
	return path;
}


static BString
RememberedOriginal()
{
	BFile file(SettingsPath().Path(), B_READ_ONLY);
	BMessage settings;
	const char* original;
	if (file.InitCheck() != B_OK || settings.Unflatten(&file) != B_OK
			|| settings.FindString("original", &original) != B_OK)
		return "";
	return original;
}


bool
GobeResources::Use(const char* path, BString& error)
{
	if (path == NULL || path[0] == '\0')
		return false;
	if (!sFile.Load(path)) {
		error << path << ": " << sFile.Error().c_str() << "\n";
		return false;
	}
	sPath = path;
	return true;
}


void
GobeResources::Remember()
{
	BMessage settings;
	settings.AddString("original", sPath.String());
	BFile file(SettingsPath().Path(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	if (file.InitCheck() == B_OK)
		settings.Flatten(&file);
}


bool
GobeResources::Init(BString& error)
{
	// $GOBE_ORIGINAL, where it was found before, next to the app, the
	// usual places it is installed, then any file of that name on a volume
	// that knows queries
	BString dir = AppDirectory();
	std::vector<BString> candidates;
	candidates.push_back(getenv("GOBE_ORIGINAL") != NULL ? getenv("GOBE_ORIGINAL") : "");
	candidates.push_back(RememberedOriginal());
	candidates.push_back(BString(dir) << "/Gobe Productive");
	candidates.push_back(BString(dir) << "/original/Gobe Productive");
	BPath home;
	if (find_directory(B_USER_DIRECTORY, &home) == B_OK) {
		BString h = home.Path();
		candidates.push_back(BString(h)
			<< "/config/non-packaged/apps/Gobe Productive 2.0/Gobe Productive");
		candidates.push_back(BString(h) << "/Gobe Productive 2.0/Gobe Productive");
		candidates.push_back(BString(h) << "/apps/Gobe Productive 2.0/Gobe Productive");
		candidates.push_back(BString(h) << "/Desktop/Gobe Productive 2.0/Gobe Productive");
	}
	candidates.push_back("/boot/apps/Gobe Productive 2.0/Gobe Productive");

	error = "";
	for (const BString& candidate : candidates)
		if (candidate.Length() > 0 && BEntry(candidate.String()).Exists()
				&& Use(candidate.String(), error)) {
			Remember();
			return true;
		}
	BVolumeRoster volumes;
	BVolume volume;
	while (volumes.GetNextVolume(&volume) == B_OK) {
		if (!volume.KnowsQuery())
			continue;
		BQuery query;
		query.SetVolume(&volume);
		query.SetPredicate("name == \"Gobe Productive\"");
		if (query.Fetch() != B_OK)
			continue;
		BEntry entry;
		while (query.GetNextEntry(&entry) == B_OK) {
			BPath path;
			if (entry.IsFile() && entry.GetPath(&path) == B_OK && Use(path.Path(), error)) {
				Remember();
				return true;
			}
		}
	}
	error.Prepend("Gobe Productive needs your original \"Gobe Productive\" program file "
		"(from the Gobe Productive 2.0 folder) for its menus, dialogs and pictures. "
		"It wasn't found.\n\n");
	return false;
}


const ResourceFile&
GobeResources::File()
{
	return sFile;
}


const BString&
GobeResources::Path()
{
	return sPath;
}


BString
GobeResources::IndexString(int32 id, int32 index)
{
	return BString(gobe::IndexString(sFile, id, index).c_str());
}

} // namespace gobe
