// Config.h for Wind: the plug-in's settings (Prefs) and its configuration
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
#include "TSlider.h"

// GIMP's wind enums (their order as the menus list them)
typedef enum { LEFT, RIGHT } direction_t;
typedef enum { RENDER_WIND, RENDER_BLAST } rendermode_t;
typedef enum { BOTH, LEADING, TRAILING } edge_t;

// the sliders' ranges (the original's MakeConfig: 0 to 50, 1 to 51)
#define MIN_THRESHOLD 0
#define MAX_THRESHOLD 50
#define MIN_STRENGTH 1
#define MAX_STRENGTH 51

struct Prefs {
	int32 threshold;
	int32 direction;
	int32 strength;
	int32 type;
	int32 edge;
	bool preview;
};
extern Prefs DefaultPrefs;
void LoadPrefs(Prefs* thePrefs);
void SavePrefs();
void GetSettings(BMessage* ioExtension, Prefs* thePrefs, BRect* selection, BInvoker** preview = NULL, BInvoker** progress = NULL);

class ConfigView;
class MySlider : public TSlider {
public:
	MySlider(BRect frame, const char* name, const char* label, const char* format,
			BMessage* message, int32 minValue, int32 maxValue, float scale = 1.0,
			float offset = 0.0)
		: TSlider(frame, name, label, format, message, minValue, maxValue, scale, offset) {}
	virtual void ValueChanged();
};

class ConfigView : public BView {
public:
	ConfigView(BRect frame, const char* name, uint32 flags, uint32 resizingMode)
		: BView(frame, name, resizingMode, flags) {}
	virtual void AttachedToWindow();
	virtual void MessageReceived(BMessage *msg);
	BMenuField* mDirection = NULL;
	BMenuField* mEdge = NULL;
	BCheckBox* mLivePreview = NULL;
	MySlider* mStrength = NULL;
	MySlider* mThreshold = NULL;
	BMenuField* mType = NULL;
	BInvoker* mPreviewInvoker = NULL;
};

#endif
