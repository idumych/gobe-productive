// Config.h for FractalTrace: the plug-in's settings (Prefs) and its configuration
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

// (the original's values, from the add-on's MessageReceived)
enum {
	OUTSIDE_TYPE_WRAP = 0,
	OUTSIDE_TYPE_TRANSPARENT = 1,
	OUTSIDE_TYPE_FORE = 2,
	OUTSIDE_TYPE_BACK = 3
};

struct Prefs {
	double x1;
	double x2;
	double y1;
	double y2;
	int32 depth;
	int32 outside_type;
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
	virtual double Value();
	virtual void ValueChanged();
};

class ConfigView : public BView {
public:
	ConfigView(BRect frame, const char* name, uint32 flags, uint32 resizingMode)
		: BView(frame, name, resizingMode, flags) {}
	virtual void AttachedToWindow();
	virtual void MessageReceived(BMessage *msg);
	MySlider* mDepth = NULL;
	BCheckBox* mLivePreview = NULL;
	MySlider* mX1 = NULL;
	MySlider* mX2 = NULL;
	MySlider* mY1 = NULL;
	MySlider* mY2 = NULL;
	BInvoker* mPreviewInvoker = NULL;
};

#endif
