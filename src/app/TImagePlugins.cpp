#include "TImagePlugins.h"

#include <Alert.h>
#include <Application.h>
#include <FindDirectory.h>
#include <Menu.h>
#include <MenuItem.h>
#include <Path.h>
#include <Roster.h>

#include <algorithm>
#include <mutex>

#include "GobeResources.h"
#include "ImageManip.h"
#include "TImageGIMP.h"

namespace gobe {

namespace {

const ResourceFile&
ImageResources()
{
	return GobeResources::Part("ImageProcessing");
}


// where the add-ons are: next to the application as packaged (add-ons/ in
// its folder, or beside its bin/ in the AppImage) or as built here
// (plugins/build), the system's (the Haiku package's), then the user's
// add-ons (the original's ~/config/add-ons/ImageManip)
BString
AddonPath(const char* kind)
{
	BString path;
	app_info info;
	if (be_app != NULL && be_app->GetAppInfo(&info) == B_OK) {
		BPath app(&info.ref);
		if (app.GetParent(&app) == B_OK) {
			path << app.Path() << "/add-ons/" << kind << ":" << app.Path() << "/../add-ons/"
				<< kind << ":" << app.Path() << "/plugins/build/" << kind;
		}
	}
	// (the Haiku package's: the system's add-ons)
	BPath system;
	if (find_directory(B_SYSTEM_ADDONS_DIRECTORY, &system) == B_OK) {
		system.Append(kind);
		if (path.Length() > 0)
			path << ":";
		path << system.Path();
	}
	BPath user;
	if (find_directory(B_USER_ADDONS_DIRECTORY, &user) == B_OK) {
		user.Append(kind);
		if (path.Length() > 0)
			path << ":";
		path << user.Path();
	}
	return path;
}


std::vector<ImagePlugin> sPlugins;
std::once_flag sBuilt;


// TPartIMFactory::InsertPlugin: before the first whose name sorts after
void
InsertPlugin(const ImagePlugin& plugin)
{
	auto at = std::find_if(sPlugins.begin(), sPlugins.end(),
		[&](const ImagePlugin& p) { return strcmp(plugin.name.String(), p.name.String()) < 0; });
	sPlugins.insert(at, plugin);
}


// the manipulators or converters libimagemanip found
void
AddAddons(bool converters)
{
	image_addon_id* list = NULL;
	int32 count = 0;
	if ((converters ? Image_GetConverters(NULL, NULL, &list, &count)
			: Image_GetManipulators(NULL, NULL, &list, &count)) != B_OK)
		return;
	for (int32 i = 0; i < count; i++) {
		const char* name = NULL;
		const char* info = NULL;
		const char* category = NULL;
		int32 version = 0;
		Image_GetAddonInfo(list[i], &name, &info, &category, &version);
		ImagePlugin plugin;
		plugin.kind = converters ? 2 : 1;
		// (no category: STR# 2's "Conversion" or "Manipulation")
		plugin.category = category != NULL && category[0] != '\0' ? BString(category)
			: BString(IndexString(ImageResources(), 2, converters ? 11 : 12).c_str());
		plugin.name = name != NULL ? name : "";
		plugin.addon = list[i];
		InsertPlugin(plugin);
	}
	delete[] list;
}


void
BuildPluginTable()
{
	// (the GIMP filters first, as TPartIMFactory looks for them)
	std::vector<ImagePlugin> gimp;
	AddGIMPPlugins(gimp);
	for (const ImagePlugin& plugin : gimp)
		InsertPlugin(plugin);
	Image_Init(AddonPath("ImageManip").String());
	AddAddons(false);
	AddAddons(true);
}


// ManipPlugin::VersionString: " 1.2", or " 1.2.3"
BString
VersionString(int32 version)
{
	int32 c = version % 10, b = ((version - c) % 100) / 10, a = (version - c - b) / 100;
	BString text;
	if (c == 0)
		text.SetToFormat(" %d.%d", int(a), int(b));
	else
		text.SetToFormat(" %d.%d.%d", int(a), int(b), int(c));
	return text;
}

} // namespace


const std::vector<ImagePlugin>&
ImagePlugins()
{
	std::call_once(sBuilt, BuildPluginTable);
	return sPlugins;
}


ImagePlugin*
EditableImagePlugin(int32 index)
{
	ImagePlugins();
	return index >= 0 && size_t(index) < sPlugins.size() ? &sPlugins[size_t(index)] : NULL;
}


void
AddPluginItems(BMenu* plugins)
{
	const std::vector<ImagePlugin>& list = ImagePlugins();
	int32 own = plugins->CountItems();
	for (size_t i = 0; i < list.size(); i++) {
		const char* category = list[i].category.String();
		BMenuItem* item = plugins->FindItem(category);
		BMenu* submenu = item != NULL ? item->Submenu() : NULL;
		if (submenu == NULL) {
			int32 at = own;
			while (at < plugins->CountItems()
					&& strcmp(category, plugins->ItemAt(at)->Label()) >= 0)
				at++;
			submenu = new BMenu(category);
			plugins->AddItem(submenu, at);
		}
		BMessage* run = new BMessage('sI03');
		run->AddInt32("index", int32(i));
		submenu->AddItem(new BMenuItem(list[i].name.String(), run));
	}
}


void
PluginAbout(int32 index)
{
	const ImagePlugin* plugin = EditableImagePlugin(index);
	if (plugin == NULL || plugin->kind == 0)
		return;
	const char* name = NULL;
	const char* info = NULL;
	const char* category = NULL;
	int32 version = 0;
	if (Image_GetAddonInfo(plugin->addon, &name, &info, &category, &version) != B_OK)
		return;
	BString text = name != NULL ? name : "";
	text << VersionString(version) << "\n\n" << (info != NULL ? info : "");
	(new BAlert("", text.String(), "OK"))->Go(NULL);
}


// #pragma mark - TDialogManipConfig


TDialogManipConfig::TDialogManipConfig(const char* name, BView* config, BMessenger owner)
	:
	TDialog(2, 2, 'BITS', NULL, &ImageResources()),
	fName(name),
	fConfig(config),
	fOwner(owner)
{
}


// Init: titled with the plug-in's name, its view in the config holder,
// which takes its size (ConfigRecalc)
void
TDialogManipConfig::Init()
{
	SetTitle(fName.String());
	BView* holder = Item(3);
	if (holder != NULL && fConfig != NULL) {
		BRect bounds = fConfig->Bounds();
		BSize size(bounds.Width(), bounds.Height());
		holder->SetExplicitMinSize(size);
		holder->SetExplicitPreferredSize(size);
		holder->SetExplicitMaxSize(size);
		fConfig->MoveTo(0, 0);
		holder->AddChild(fConfig);
	}
}


void
TDialogManipConfig::MessageReceived(BMessage* message)
{
	// the settings view's preview invoker
	if (message->what == 'UPDT') {
		BMessage preview(kMsgPluginPreview);
		bool on;
		if (message->FindBool("preview", &on) == B_OK)
			preview.AddBool("preview", on);
		fOwner.SendMessage(&preview);
		return;
	}
	TDialog::MessageReceived(message);
}


bool
TDialogManipConfig::ItemHit(int32 index, BMessage* message)
{
	if (index == 2) {
		fOwner.SendMessage(kMsgPluginAbout);
		return true;
	}
	if (index == 0 || index == 1) {
		fAnswered = true;
		fOwner.SendMessage(index == 0 ? kMsgPluginOK : kMsgPluginCancel);
	}
	return TDialog::ItemHit(index, message);
}


bool
TDialogManipConfig::QuitRequested()
{
	if (!fAnswered) {
		fAnswered = true;
		fOwner.SendMessage(kMsgPluginCancel);
	}
	return true;
}

} // namespace gobe
