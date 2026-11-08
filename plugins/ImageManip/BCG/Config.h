// Config.h for BCG: the plug-in's settings (Prefs) and its configuration
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

struct Prefs {
	float gamma[4];
	int32 brightness;
	int32 contrast;
	bool preview;
	bool lock;
	bool b_additive;
	bool c_stretch;
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
	virtual float Value();
	virtual void DrawHashMarks();
	virtual void DrawThumb();
	virtual void SetValue(float f);
	virtual void ValueChanged();
	ConfigView* mConfig = NULL;
	bool mScaled = true;
	bool mLockable = true;
	bool mLocked = false;
};

class ConfigView : public BView {
public:
	ConfigView(BRect frame, const char* name, uint32 flags, uint32 resizingMode)
		: BView(frame, name, resizingMode, flags) {}
	virtual void AttachedToWindow();
	virtual void AttachedToWindow(BView* v);
	virtual void MessageReceived(BMessage *msg);
	MySlider* mAlpha = NULL;
	MySlider* mBlue = NULL;
	MySlider* mBrightness = NULL;
	MySlider* mContrast = NULL;
	MySlider* mGreen = NULL;
	BCheckBox* mLivePreview = NULL;
	BCheckBox* mLock = NULL;
	MySlider* mRed = NULL;
	BInvoker* mPreviewInvoker = NULL;
};

#endif
