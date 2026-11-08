// The image part's plug-ins (Parts/ImageProcessing: TPartIMFactory's plugin
// table, PluginMenuEntry, ManipPlugin, TDialogManipConfig): the ImageManip
// add-ons libimagemanip finds (manipulators and converters) and the GIMP
// filters, in the Plugins menu by category; a plug-in run on the image's
// selection, with its settings view (the Plugin dialog), a live preview, its
// progress in the Status window, and its result put down where the
// selection is.
#ifndef GOBE_TIMAGEPLUGINS_H
#define GOBE_TIMAGEPLUGINS_H

#include <Message.h>
#include <Messenger.h>
#include <String.h>

#include <memory>
#include <string>
#include <vector>

#include "TDialog.h"

class BMenu;
class BMessageQueue;

namespace gobe {

// PluginMenuEntry: its kind (0 a GIMP filter, 1 an ImageManip manipulator,
// 2 a converter), category, name, the GIMP filter's file or the add-on's id,
// and its settings (ManipPlugin::GetConfig, kept between runs)
struct ImagePlugin {
	int32 kind = 1;
	BString category;
	BString name;
	BString path;
	intptr_t addon = 0;
	std::shared_ptr<BMessage> config;
};

// TPartIMFactory::BuildPluginTable (once): sorted by name (InsertPlugin)
const std::vector<ImagePlugin>& ImagePlugins();
ImagePlugin* EditableImagePlugin(int32 index);

// TPartIM::InstallMenus: after the Plugins menu's own items, a submenu per
// category (in alphabetical order), each plug-in an 'sI03' item with its
// "index"
void AddPluginItems(BMenu* plugins);

// ManipPlugin::About: the add-on's name, its version, its info
void PluginAbout(int32 index);

// the messages between a running plug-in, its dialog and the page view
enum {
	kMsgPluginPreview = 'IMpv',		// "preview" (bool): show it with the settings now
	kMsgPluginPreviewDone = 'IMpr',	// "bits" (ArchiveBitmap*), "serial"
	kMsgPluginOK = 'IMpo',
	kMsgPluginCancel = 'IMpc',
	kMsgPluginDone = 'IMpd',		// "bits" (ArchiveBitmap*) or none, "status"
	kMsgPluginAbout = 'IMpa'
};

// TDialogManipConfig (DIAL 2, Item 2): titled with the plug-in's name, its
// settings view in the config holder (item 3), OK, Cancel, About…; the
// view's preview invoker ('UPDT') asks `owner` for a preview
class TDialogManipConfig : public TDialog {
public:
	TDialogManipConfig(const char* name, BView* config, BMessenger owner);
	virtual void MessageReceived(BMessage* message);
	virtual bool QuitRequested();

protected:
	virtual void Init();
	virtual bool ItemHit(int32 index, BMessage* message);

private:
	BString fName;
	BView* fConfig;
	BMessenger fOwner;
	bool fAnswered = false;
};

} // namespace gobe

#endif
