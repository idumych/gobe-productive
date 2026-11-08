// Config.h for Nova: the plug-in's settings (Prefs) and its configuration
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

struct Prefs {
	uchar red;
	uchar green;
	uchar blue;
	int32 xcenter;
	int32 ycenter;
	int32 hue;
	int32 spokes;
	int32 radius;
	bool preview;
};
extern Prefs DefaultPrefs;
void LoadPrefs(Prefs* thePrefs);
void SavePrefs();

class ConfigView;
class TSlider : public BSlider {
public:
	TSlider(BRect frame, const char* name, const char* label, const char* format,
			BMessage* message, int32 minValue, int32 maxValue)
		: BSlider(frame, name, label, message, minValue, maxValue),
		mPreviewInvoker(NULL), mFormat(format) {}
	virtual void DrawThumb();
	virtual void SetValue(int32 value);
	BInvoker* mPreviewInvoker;
	BString mFormat;
	BString number;
};

class ConfigView : public BView {
public:
	ConfigView(BRect frame, const char* name, uint32 flags, uint32 resizingMode)
		: BView(frame, name, resizingMode, flags) {}
	virtual void AttachedToWindow();
	virtual void MessageReceived(BMessage *msg);
	TSlider* mBlue = NULL;
	TSlider* mGreen = NULL;
	TSlider* mHue = NULL;
	BCheckBox* mLivePreview = NULL;
	TSlider* mRadius = NULL;
	TSlider* mRed = NULL;
	TSlider* mSpokes = NULL;
	TSlider* mX = NULL;
	TSlider* mY = NULL;
	BInvoker* mPreviewInvoker = NULL;
};

#endif
