// Config.h for Qbist: the plug-in's settings (Prefs) and its configuration
// view. The original's src.zip left this header out; rebuilt for the Gobe
// Productive port from how the plug-in's sources use it.
#ifndef CONFIG_H
#define CONFIG_H

#include <Bitmap.h>
#include <Box.h>
#include <Button.h>
#include <CheckBox.h>
#include <ColorControl.h>
#include <Invoker.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <Message.h>
#include <PopUpMenu.h>
#include <RadioButton.h>
#include <Slider.h>
#include <String.h>
#include <StringView.h>
#include <TabView.h>
#include <TextControl.h>
#include <View.h>
#include <Window.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "BitmapAccessor.h"
#include "ImageManipAddon.h"
#include <Entry.h>
#include <FilePanel.h>
#include <FindDirectory.h>
// (Haiku has no common settings folder: the user's)
#ifndef B_COMMON_SETTINGS_DIRECTORY
#define B_COMMON_SETTINGS_DIRECTORY B_USER_SETTINGS_DIRECTORY
#endif
#include <OS.h>
#define B_MAX_CPU_COUNT_QBIST 64

// GIMP's qbist (ExpInfo): 36 transforms of 6 registers, and the alpha
// option (the original's info[9] is 9 * 580 bytes)
#define MAX_TRANSFORMS 36
#define NUM_TRANSFORMS 9
#define NUM_REGISTERS 6

struct Prefs {
	int transformSequence[MAX_TRANSFORMS];
	int source[MAX_TRANSFORMS];
	int control[MAX_TRANSFORMS];
	int dest[MAX_TRANSFORMS];
	bool usealpha;
	bool inited;
};
extern Prefs info[9];
extern bool info_refreshed;
void LoadPrefs(Prefs* thePrefs);
void SavePrefs();
void GetSettings(BMessage* ioExtension, Prefs* thePrefs, BRect* selection, BInvoker** preview = NULL,
	BInvoker** progress = NULL);
void create_info(Prefs* info);
void modify_info(Prefs* o_info, Prefs* n_info);
class BDirectory;
BDirectory* GetPrefsDir();

class ConfigView;

class BitmapView : public BView, public BInvoker {
public:
	BitmapView(BRect r, BMessage* msg, ConfigView* cf);
	virtual void Draw(BRect);
	virtual void MouseDown(BPoint) { Invoke(); }
	void SetBitmap(BBitmap* bits);
	BBitmap* bitmap;
	ConfigView* config;
};

class ConfigView : public BView {
public:
	ConfigView(BRect frame, const char* name, uint32 flags, uint32 resizingMode)
		: BView(frame, name, resizingMode, flags) {}
	virtual ~ConfigView();
	virtual void AttachedToWindow();
	void AttachedToWindow(BView* v);
	virtual void MessageReceived(BMessage* msg);
	void RefreshPreviews(bool recalc = false);
	void Preview(int32 which);
	static int32 PreviewThread(void* p);
	bool load_data(entry_ref* ref);
	void save_data(const char* name);
	BitmapView* thumbnail[9] = {};
	BCheckBox* alpha = NULL;
	BFilePanel* panel = NULL;
	thread_id thread[B_MAX_CPU_COUNT_QBIST] = {};
	system_info sysinfo;
};

#endif
