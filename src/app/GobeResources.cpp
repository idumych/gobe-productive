#include "GobeResources.h"

#include <stdio.h>
#include <stdlib.h>
#ifdef APP_COSMOE
#include <spawn.h>
#include <sys/wait.h>
extern char** environ;
#endif

#include <Alert.h>
#include <AppFileInfo.h>
#include <Cursor.h>
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

#include <map>
#include <string>
#include <vector>

#include "OriginalImport.h"

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


// where an imported original is unpacked: the settings' "Gobe Productive
// port" folder
static BPath
ImportPath()
{
	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) == B_OK)
		path.Append("Gobe Productive port/Original");
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
	// the program's own signature (BEOS:APP_SIG), not another BeOS program's
	// (the installer has resources too)
	const ResourceFile::Resource* signature = sFile.Find(FourCC("MIMS"), 1);
	if (signature == NULL || std::string(signature->data.begin(), signature->data.end())
			.compare(0, 33, "application/x-vnd.gobe.productive") != 0
			|| (signature->data.size() > 33 && signature->data[33] != 0)) {
		error << path << ": not the Gobe Productive program\n";
		sFile = ResourceFile();
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
	// unpacked before from the CD or the installer
	candidates.push_back(BString(ImportPath().Path())
		<< "/Gobe Productive 2.0b2/Gobe Productive 2.0/Gobe Productive");

	error = "";
	for (const BString& candidate : candidates)
		if (candidate.Length() > 0 && BEntry(candidate.String()).Exists()
				&& Use(candidate.String(), error)) {
			Remember();
			return true;
		}
	// $GOBE_ORIGINAL naming the CD image or the installer
	if (getenv("GOBE_ORIGINAL") != NULL && BEntry(getenv("GOBE_ORIGINAL")).Exists()
			&& Import(getenv("GOBE_ORIGINAL"), error))
		return true;
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
	error.Prepend("Gobe Productive needs the original Gobe Productive 2.0 for its menus, "
		"dialogs and pictures, and it wasn't found.\n\nChoose its CD image (ISO), its "
		"installer (\"Install Gobe Productive\") or a folder copied from the CD or an old "
		"BeOS installation, and the port unpacks what it needs.\n\n");
	return false;
}


bool
GobeResources::Import(const char* source, BString& error)
{
	BPath destination = ImportPath();
	if (destination.InitCheck() != B_OK) {
		error = "No settings folder to unpack the original into.";
		return false;
	}
	std::string program, why;
	if (!ImportOriginal(source, destination.Path(), program, why)) {
		error = why.c_str();
		return false;
	}
	if (!Use(program.c_str(), error))
		return false;
	Remember();
	return true;
}


const ResourceFile&
GobeResources::File()
{
	return sFile;
}


const ResourceFile&
GobeResources::Part(const char* name)
{
	static std::map<std::string, ResourceFile> sParts;
	auto found = sParts.find(name);
	if (found != sParts.end())
		return found->second;
	ResourceFile& file = sParts[name];
	BPath path(sPath.String());
	if (path.GetParent(&path) == B_OK && path.Append("Parts") == B_OK
		&& path.Append(name) == B_OK && !file.Load(path.Path()))
		fprintf(stderr, "Parts/%s: %s\n", name, file.Error().c_str());
	return file;
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


const BCursor*
GobeResources::Cursor(int32 id, const ResourceFile* file)
{
	if (file == NULL)
		file = &sFile;
	static std::map<std::pair<const ResourceFile*, int32>, BCursor*> sCursors;
	auto found = sCursors.find({file, id});
	if (found != sCursors.end())
		return found->second;
	BCursor*& cursor = sCursors[{file, id}];
	cursor = NULL;
	const ResourceFile::Resource* hot = file->Find('CURS', id);
	const ResourceFile::Resource* picture = file->Find('MICN', id);
	if (picture == NULL || picture->data.size() != 256)
		return NULL;
	// (TCursor: size 16, depth 1, the hot spot's y then x, the picture's
	// bits, the mask's)
	uint8 data[68] = {16, 1, 0, 0};
	if (hot != NULL && hot->data.size() >= 8) {
		data[3] = hot->data[0];		// (TRsrHelper::GetUInt32: little endian)
		data[2] = hot->data[4];
	}
	const uint8* pixel = picture->data.data();
	for (int row = 0; row < 32; row++)
		for (int bit = 0; bit < 8; bit++, pixel++) {
			if (*pixel == 0 || (*pixel != '?' && *pixel != B_TRANSPARENT_MAGIC_CMAP8))
				data[4 + row] |= uint8(1 << (7 - bit));
			if (*pixel == 0 || *pixel == '?')
				data[36 + row] |= uint8(1 << (7 - bit));
		}
	cursor = new BCursor(data);
	return cursor;
}



void
StringAlert(const char* text)
{
	(new BAlert("Alert", text, GobeResources::IndexString(2, 0).String(), NULL, NULL,
		B_WIDTH_AS_USUAL, B_INFO_ALERT))->Go();
}


bool
ErrorAlert(status_t status, const char* text)
{
	if (status == B_OK)
		return false;
	int32 message = -1;
	switch (uint32(status)) {
		case 0x8000000c:
		case 0x80010000:
			return true;
		case 0x80004800: message = 0x1e; break;	// B_NO_TRANSLATOR
		case 0x80004801: message = 0x1f; break;	// B_ILLEGAL_DATA
		case 0xa0b00000: message = 0x18; break;
		case 0xa0b00001: message = 0x19; break;
		case 0xa0b00002: message = 0x1a; break;
		case 0xa0b00003: message = 0x1b; break;
		case 0xa0b00004: message = 0x1c; break;
		case 0xa0b00005: message = 0x1d; break;
		case 0xdada0000: message = 0x20; break;
		case 0xdada0001: message = 0x21; break;
		case 0xdada0002: message = 0x22; break;
	}
	BString shown;
	if (message >= 0)
		shown = GobeResources::IndexString(6, message);
	else if (text == NULL)
		shown.SetToFormat(GobeResources::IndexString(2, 9).String(), strerror(status));
	else
		shown.SetToFormat(GobeResources::IndexString(2, 10).String(), text, strerror(status));
	StringAlert(shown.String());
	return true;
}


void
LaunchURL(const char* mime, const char* url)
{
#ifdef APP_COSMOE
	// (be_roster->Launch doesn't reach the Linux desktop)
	char* argv[] = {const_cast<char*>("xdg-open"), const_cast<char*>(url), NULL};
	pid_t pid;
	if (posix_spawnp(&pid, "xdg-open", NULL, NULL, argv, environ) == 0)
		waitpid(pid, NULL, 0);
#else
	const char* argv[] = {url, NULL};
	be_roster->Launch(mime, 1, const_cast<char**>(argv));
#endif
}

} // namespace gobe
