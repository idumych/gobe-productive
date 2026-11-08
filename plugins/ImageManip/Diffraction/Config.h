// Config.h for Diffraction: the plug-in's settings (Prefs) and its configuration
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
	float lam_r, lam_g, lam_b;
	float contour_r, contour_g, contour_b;
	float edges_r, edges_g, edges_b;
	float brightness;
	float scattering;
	float polarization;
};
extern Prefs DefaultPrefs;
extern Prefs FactoryPrefs;
void LoadPrefs(Prefs* thePrefs);
void SavePrefs();
void GetSettings(BMessage* ioExtension, Prefs* thePrefs, BRect* selection, BInvoker** preview = NULL,
	BInvoker** progress = NULL);

class ConfigView;
int32 Preview(ConfigView* config);
void InitControlSettings(ConfigView* config);

class MySlider : public TSlider {
public:
	MySlider(BRect frame, ConfigView* cf, const char* name, const char* label,
			const char* format, BMessage* message, int32 minValue, int32 maxValue,
			float scale = 1.0, float offset = 0.0)
		: TSlider(frame, name, label, format, message, minValue, maxValue, scale, offset),
		config(cf) {}
	virtual void ValueChanged();
	ConfigView* config;
};

class BitmapView : public BView {
public:
	BitmapView(BRect r, ConfigView* cf);
	virtual void Draw(BRect);
	BBitmap* bitmap;
	ConfigView* config;
};

class ConfigView : public BView {
public:
	ConfigView(BRect frame, const char* name, uint32 flags, uint32 resizingMode)
		: BView(frame, name, resizingMode, flags) {}
	virtual ~ConfigView();
	virtual void AttachedToWindow();
	virtual void MessageReceived(BMessage* msg);
	MySlider* lamRSlider = NULL;
	MySlider* lamGSlider = NULL;
	MySlider* lamBSlider = NULL;
	MySlider* contourRSlider = NULL;
	MySlider* contourGSlider = NULL;
	MySlider* contourBSlider = NULL;
	MySlider* edgesRSlider = NULL;
	MySlider* edgesGSlider = NULL;
	MySlider* edgesBSlider = NULL;
	MySlider* brightnessSlider = NULL;
	MySlider* scatteringSlider = NULL;
	MySlider* polarizationSlider = NULL;
	BitmapView* preview = NULL;
	volatile bool mAgain = false;
	thread_id mThread = B_ERROR;
};

#endif
