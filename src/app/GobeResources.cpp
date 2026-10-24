#include "GobeResources.h"

#include <stdlib.h>

#include <AppFileInfo.h>
#include <Application.h>
#include <Path.h>
#include <Roster.h>

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


bool
GobeResources::Init(BString& error)
{
	BString dir = AppDirectory();
	const char* candidates[] = {
		getenv("GOBE_ORIGINAL"),
		NULL,
		NULL,
	};
	BString nextTo = dir;
	nextTo << "/Gobe Productive";
	BString inOriginal = dir;
	inOriginal << "/original/Gobe Productive";
	candidates[1] = nextTo.String();
	candidates[2] = inOriginal.String();

	error = "";
	for (const char* candidate : candidates) {
		if (candidate == NULL || candidate[0] == '\0')
			continue;
		if (sFile.Load(candidate)) {
			sPath = candidate;
			return true;
		}
		error << candidate << ": " << sFile.Error().c_str() << "\n";
	}
	error.Prepend("Could not load the original Gobe Productive binary.\n"
		"Set GOBE_ORIGINAL or put \"Gobe Productive\" next to the app.\n\n");
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
