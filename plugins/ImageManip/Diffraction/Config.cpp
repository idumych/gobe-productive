#include <stdio.h>
#include <algobase.h>
#include <MenuField.h>
#include <TabView.h>
#include <Bitmap.h>
#include <Window.h>

#define P_IMPEXP	// Have to do this before including libprefs.h
#include "libprefs.h"
#include "Config.h"
#include "ImageManipAddon.h"
#include "BBitmapAccessor.h"
#include "TSlider.h"

#define PLUGIN_PREFIX "diffraction"

Prefs FactoryPrefs = {
	0.8,   /* lam_r */
	1.2,   /* lam_g */
	1.0,   /* lam_b */
	2.5,   /* contour_r */
	2.5,   /* contour_g */
	2.5,   /* contour_b */
	1.0,   /* edges_r */
	1.0,   /* edges_g */
	1.0,   /* edges_b */
	0.5,   /* brightness */
	2.0,   /* scattering */
	-0.5,  /* polarization */
};

Prefs DefaultPrefs = FactoryPrefs;

status_t
GetConfigMessage(
	BMessage *ioExtension,
	BMessage *ioCapability)
{
	static bool inited;
	
	if (!inited)
	{
		inited = true;
		LoadPrefs(&DefaultPrefs);
	}
	
	// Let them know that we support these extensions
	ioCapability->AddInt32("selection_rect", B_RECT_TYPE);
	ioCapability->AddInt32("progress_invoker", B_POINTER_TYPE);
	ioCapability->AddInt32("cancel_queue", B_POINTER_TYPE);

	ioExtension->AddFloat(PLUGIN_PREFIX "/lam_r", DefaultPrefs.lam_r);
	ioExtension->AddFloat(PLUGIN_PREFIX "/lam_g", DefaultPrefs.lam_g);
	ioExtension->AddFloat(PLUGIN_PREFIX "/lam_b", DefaultPrefs.lam_b);

	ioExtension->AddFloat(PLUGIN_PREFIX "/contour_r", DefaultPrefs.contour_r);
	ioExtension->AddFloat(PLUGIN_PREFIX "/contour_g", DefaultPrefs.contour_g);
	ioExtension->AddFloat(PLUGIN_PREFIX "/contour_b", DefaultPrefs.contour_b);

	ioExtension->AddFloat(PLUGIN_PREFIX "/edges_r", DefaultPrefs.edges_r);
	ioExtension->AddFloat(PLUGIN_PREFIX "/edges_g", DefaultPrefs.edges_g);
	ioExtension->AddFloat(PLUGIN_PREFIX "/edges_b", DefaultPrefs.edges_b);

	ioExtension->AddFloat(PLUGIN_PREFIX "/brightness", DefaultPrefs.brightness);
	ioExtension->AddFloat(PLUGIN_PREFIX "/scattering", DefaultPrefs.scattering);
	ioExtension->AddFloat(PLUGIN_PREFIX "/polarization", DefaultPrefs.polarization);

	return B_OK;
}


status_t MakeConfig(BMessage *ioExtension, BView **outView)
{
	BRect bounds2(0, 0, 100, 100);
	BRect bounds;
	ConfigView* config;
	*outView = config = new ConfigView(bounds2, PLUGIN_PREFIX " configuration", B_WILL_DRAW, B_FOLLOW_NONE);
	config->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
	
	bounds2.OffsetBy(69, 0);
	LoadPrefs(&DefaultPrefs);

	BView* v = new BView(bounds2, "Frequencies", B_FOLLOW_ALL, 0);
	v->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));

	MySlider* slider;
	// Light frequency
	bounds.left = 8;
	bounds.top = 0;
	bounds.right = bounds.left + 318;
	bounds.bottom = bounds.top + 40;

	slider = config->lamRSlider = new MySlider(bounds, config, "lam_r", "Red", "%.1f", NULL, 0, 100, 0.2);
	slider->SetHashMarks(B_HASH_MARKS_BOTTOM);
	slider->SetHashMarkCount(5);
	slider->SetLimitLabels("0.0", "20.0");
	slider->ResizeToPreferred();
	bounds = slider->Frame();
	v->AddChild(slider);

	bounds.OffsetBy(0, bounds.Height());
	slider = config->lamGSlider = new MySlider(bounds, config, "lam_g", "Green", "%.1f", NULL, 0, 100, 0.2);
	slider->SetHashMarks(B_HASH_MARKS_BOTTOM);
	slider->SetHashMarkCount(5);
	slider->SetLimitLabels("0.0", "20.0");
	slider->ResizeToPreferred();
	bounds = slider->Frame();
	v->AddChild(slider);

	bounds.OffsetBy(0, bounds.Height());
	slider = config->lamBSlider = new MySlider(bounds, config, "lam_b", "Blue", "%.1f", NULL, 0, 100, 0.2);
	slider->SetHashMarks(B_HASH_MARKS_BOTTOM);
	slider->SetHashMarkCount(5);
	slider->SetLimitLabels("0.0", "20.0");
	slider->ResizeToPreferred();
	bounds = slider->Frame();
	v->AddChild(slider);
	
	v->ResizeTo(slider->Frame().right + 5, slider->Frame().bottom + 5);
	v->MoveTo(B_ORIGIN);
	bounds2.right = v->Bounds().Width() + 5;
	bounds2.bottom = v->Bounds().Height() + 5;
	bounds2.right = bounds2.left + v->Bounds().Width() + 5;
	bounds2.bottom = bounds2.top + v->Bounds().Height() + 5;
	BTabView* tabView = new BTabView(bounds2, "tabs", B_WIDTH_FROM_WIDEST);
	tabView->ResizeBy(0, tabView->TabHeight());
	tabView->ContainerView()->ResizeBy(0, tabView->TabHeight());
	tabView->AddTab(v);
	config->AddChild(tabView);
	
	bounds2 = v->Frame();

	// Contours
	v = new BView(bounds2, "Contours", B_FOLLOW_ALL, 0);
	v->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));

	bounds.left = 8;
	bounds.top = 0;
	bounds.right = bounds.left + 318;
	bounds.bottom = bounds.top + 40;

	slider = config->contourRSlider = new MySlider(bounds, config, "contour_r", "Red", "%.1f", NULL, 0, 100, 0.1);
	slider->SetHashMarks(B_HASH_MARKS_BOTTOM);
	slider->SetHashMarkCount(6);
	slider->SetLimitLabels("0.0", "10.0");
	slider->ResizeToPreferred();
	bounds = slider->Frame();
	v->AddChild(slider);

	bounds.OffsetBy(0, bounds.Height());
	slider = config->contourGSlider = new MySlider(bounds, config, "contour_g", "Green", "%.1f", NULL, 0, 100, 0.1);
	slider->SetHashMarks(B_HASH_MARKS_BOTTOM);
	slider->SetHashMarkCount(6);
	slider->SetLimitLabels("0.0", "10.0");
	slider->ResizeToPreferred();
	bounds = slider->Frame();
	v->AddChild(slider);

	bounds.OffsetBy(0, bounds.Height());
	slider = config->contourBSlider = new MySlider(bounds, config, "contour_b", "Blue", "%.1f", NULL, 0, 100, 0.1);
	slider->SetHashMarks(B_HASH_MARKS_BOTTOM);
	slider->SetHashMarkCount(6);
	slider->SetLimitLabels("0.0", "10.0");
	slider->ResizeToPreferred();
	bounds = slider->Frame();
	v->AddChild(slider);

	v->ResizeTo(slider->Frame().right + 5, slider->Frame().bottom + 5);
	v->MoveTo(B_ORIGIN);
	bounds2.right = v->Bounds().Width() + 5;
	bounds2.bottom = v->Bounds().Height() + 5;
	tabView->AddTab(v);
	
	bounds2 = v->Frame();

	// Sharp Edges
	v = new BView(bounds2, "Sharp Edges", B_FOLLOW_ALL, 0);
	v->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));

	bounds.left = 8;
	bounds.top = 0;
	bounds.right = bounds.left + 318;
	bounds.bottom = bounds.top + 40;

	slider = config->edgesRSlider = new MySlider(bounds, config, "edges_r", "Red", "%.2f", NULL, 0, 100, 0.01);
	slider->SetHashMarks(B_HASH_MARKS_BOTTOM);
	slider->SetHashMarkCount(5);
	slider->SetLimitLabels("0.0", "1.0");
	slider->ResizeToPreferred();
	bounds = slider->Frame();
	v->AddChild(slider);

	bounds.OffsetBy(0, bounds.Height());
	slider = config->edgesGSlider = new MySlider(bounds, config, "edges_g", "Green", "%.2f", NULL, 0, 100, 0.01);
	slider->SetHashMarks(B_HASH_MARKS_BOTTOM);
	slider->SetHashMarkCount(5);
	slider->SetLimitLabels("0.0", "1.0");
	slider->ResizeToPreferred();
	bounds = slider->Frame();
	v->AddChild(slider);

	bounds.OffsetBy(0, bounds.Height());
	slider = config->edgesBSlider = new MySlider(bounds, config, "edges_b", "Blue", "%.2f", NULL, 0, 100, 0.01);
	slider->SetHashMarks(B_HASH_MARKS_BOTTOM);
	slider->SetHashMarkCount(5);
	slider->SetLimitLabels("0.0", "1.0");
	slider->ResizeToPreferred();
	bounds = slider->Frame();
	v->AddChild(slider);

	v->ResizeTo(slider->Frame().right + 5, slider->Frame().bottom + 5);
	v->MoveTo(B_ORIGIN);
	bounds2.right = v->Bounds().Width() + 5;
	bounds2.bottom = v->Bounds().Height() + 5;
	tabView->AddTab(v);

	bounds2 = v->Frame();

	// Other
	v = new BView(bounds2, "Options", B_FOLLOW_ALL, 0);
	v->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));

	bounds.left = 8;
	bounds.top = 0;
	bounds.right = bounds.left + 318;
	bounds.bottom = bounds.top + 40;

	slider = config->brightnessSlider = new MySlider(bounds, config, "brightness", "Brightness", "%.2f", NULL, 0, 100, 0.01);
	slider->SetHashMarks(B_HASH_MARKS_BOTTOM);
	slider->SetHashMarkCount(5);
	slider->SetLimitLabels("0.0", "1.0");
	slider->ResizeToPreferred();
	bounds = slider->Frame();
	v->AddChild(slider);

	bounds.OffsetBy(0, bounds.Height());
	slider = config->scatteringSlider = new MySlider(bounds, config, "scattering", "Scattering", "%d", NULL, 0, 100);
	slider->SetHashMarks(B_HASH_MARKS_BOTTOM);
	slider->SetHashMarkCount(5);
	slider->SetLimitLabels("0", "100");
	slider->ResizeToPreferred();
	bounds = slider->Frame();
	v->AddChild(slider);

	bounds.OffsetBy(0, bounds.Height());
	slider = config->polarizationSlider = new MySlider(bounds, config, "polarization", "Polarization", "%.2f", NULL, 0, 100, 0.02, -1.0);
	slider->SetHashMarks(B_HASH_MARKS_BOTTOM);
	slider->SetHashMarkCount(5);
	slider->SetLimitLabels("-1.0", "1.0");
	slider->ResizeToPreferred();
	bounds = slider->Frame();
	v->AddChild(slider);

	v->ResizeTo(slider->Frame().right + 5, slider->Frame().bottom + 5);
	v->MoveTo(B_ORIGIN);
	bounds2.right = v->Bounds().Width() + 5;
	bounds2.bottom = v->Bounds().Height() + 5;
	tabView->AddTab(v);

	
	config->AddChild(config->preview = new BitmapView(BRect(0, 0, 63, 63), config));

	config->ResizeTo(tabView->Frame().right, tabView->Frame().bottom);

	if (ioExtension)
	{
		BRect r;
		GetSettings(ioExtension, &DefaultPrefs, &r);
	}

	InitControlSettings(config);
	
	return B_OK;
}

#pragma mark -

ConfigView::~ConfigView()
{
	status_t err;
	mAgain = false;
	wait_for_thread(mThread, &err);
}

void ConfigView::AttachedToWindow()
{
	for (int x = 0; x < CountChildren(); x++)
	{
		BInvoker* invoker = dynamic_cast<BInvoker*>(ChildAt(x));
		if (invoker)
			invoker->SetTarget(this);

		BMenuField* popup = dynamic_cast<BMenuField*>(ChildAt(x));
		if (popup)
			popup->Menu()->SetTargetForItems(this);
	}
}

void ConfigView::MessageReceived(BMessage *msg)
{
	BView::MessageReceived(msg);
}

#pragma mark -

void MySlider::ValueChanged()
{
	const char* name = Name();
	if (!strcmp("contour_r", name))
		DefaultPrefs.contour_r = Value() * mScale + mOffset;
	if (!strcmp("contour_g", name))
		DefaultPrefs.contour_g = Value() * mScale + mOffset;
	if (!strcmp("contour_b", name))
		DefaultPrefs.contour_b = Value() * mScale + mOffset;

	if (!strcmp("lam_r", name))
		DefaultPrefs.lam_r = Value() * mScale + mOffset;
	if (!strcmp("lam_g", name))
		DefaultPrefs.lam_g = Value() * mScale + mOffset;
	if (!strcmp("lam_b", name))
		DefaultPrefs.lam_b = Value() * mScale + mOffset;

	if (!strcmp("edges_r", name))
		DefaultPrefs.edges_r = Value() * mScale + mOffset;
	if (!strcmp("edges_g", name))
		DefaultPrefs.edges_g = Value() * mScale + mOffset;
	if (!strcmp("edges_b", name))
		DefaultPrefs.edges_b = Value() * mScale + mOffset;

	if (!strcmp("brightness", name))
		DefaultPrefs.brightness = Value() * mScale + mOffset;
	if (!strcmp("scattering", name))
		DefaultPrefs.scattering = Value() * mScale + mOffset;
	if (!strcmp("polarization", name))
		DefaultPrefs.polarization = Value() * mScale + mOffset;

	config->mAgain = true;
	if (config->mThread == B_ERROR)
	{
		config->mAgain = false;
		resume_thread(config->mThread = spawn_thread((thread_func) Preview, "diffraction_preview", B_NORMAL_PRIORITY, (void*) config));
	}
	else
	{
		Window()->Unlock();
		snooze(10000);
		Window()->Lock();
	}
}


#define SIGNATURE "x-vnd.wombat.plugins"

// This is only called by main to save out default settings
void SavePrefs()
{
	PREFHandle prefs;
	PREFData data;
	if (PREFInit(SIGNATURE, &prefs))
		return;

	PREFLoadSet(prefs, PLUGIN_PREFIX, true, &data);

	PREFSetData(data, "lam_r", &DefaultPrefs.lam_r, sizeof(float), B_FLOAT_TYPE);
	PREFSetData(data, "lam_g", &DefaultPrefs.lam_g, sizeof(float), B_FLOAT_TYPE);
	PREFSetData(data, "lam_b", &DefaultPrefs.lam_b, sizeof(float), B_FLOAT_TYPE);

	PREFSetData(data, "contour_r", &DefaultPrefs.contour_r, sizeof(float), B_FLOAT_TYPE);
	PREFSetData(data, "contour_g", &DefaultPrefs.contour_g, sizeof(float), B_FLOAT_TYPE);
	PREFSetData(data, "contour_b", &DefaultPrefs.contour_b, sizeof(float), B_FLOAT_TYPE);

	PREFSetData(data, "edges_r", &DefaultPrefs.edges_r, sizeof(float), B_FLOAT_TYPE);
	PREFSetData(data, "edges_g", &DefaultPrefs.edges_g, sizeof(float), B_FLOAT_TYPE);
	PREFSetData(data, "edges_b", &DefaultPrefs.edges_b, sizeof(float), B_FLOAT_TYPE);

	PREFSetData(data, "brightness", &DefaultPrefs.brightness, sizeof(float), B_FLOAT_TYPE);
	PREFSetData(data, "scattering", &DefaultPrefs.scattering, sizeof(float), B_FLOAT_TYPE);
	PREFSetData(data, "polarization", &DefaultPrefs.polarization, sizeof(float), B_FLOAT_TYPE);

	PREFSaveSet(data);
	PREFDisposeSet(&data);
	
	PREFShutdown(prefs);
}


void LoadPrefs(Prefs* thePrefs)
{
	PREFHandle prefs;
	PREFData data;

	if (PREFInit(SIGNATURE, &prefs))
		return;

	*thePrefs = DefaultPrefs;
	
	if (!PREFLoadSet(prefs, PLUGIN_PREFIX, true, &data))
	{
		const void* p;
		ssize_t size;
		uint32 type;
		
		// Not guaranteed to be word aligned.

		if (!PREFGetData(data, "lam_r", &p, &size, &type))
			memcpy(&thePrefs->lam_r, p, sizeof(float));
		if (!PREFGetData(data, "lam_g", &p, &size, &type))
			memcpy(&thePrefs->lam_g, p, sizeof(float));
		if (!PREFGetData(data, "lam_b", &p, &size, &type))
			memcpy(&thePrefs->lam_b, p, sizeof(float));
	
		if (!PREFGetData(data, "contour_r", &p, &size, &type))
			memcpy(&thePrefs->contour_r, p, sizeof(float));
		if (!PREFGetData(data, "contour_g", &p, &size, &type))
			memcpy(&thePrefs->contour_g, p, sizeof(float));
		if (!PREFGetData(data, "contour_b", &p, &size, &type))
			memcpy(&thePrefs->contour_b, p, sizeof(float));
	
		if (!PREFGetData(data, "edges_r", &p, &size, &type))
			memcpy(&thePrefs->edges_r, p, sizeof(float));
		if (!PREFGetData(data, "edges_g", &p, &size, &type))
			memcpy(&thePrefs->edges_g, p, sizeof(float));
		if (!PREFGetData(data, "edges_b", &p, &size, &type))
			memcpy(&thePrefs->edges_b, p, sizeof(float));
	
		if (!PREFGetData(data, "brightness", &p, &size, &type))
			memcpy(&thePrefs->brightness, p, sizeof(float));
		if (!PREFGetData(data, "scattering", &p, &size, &type))
			memcpy(&thePrefs->scattering, p, sizeof(float));
		if (!PREFGetData(data, "polarization", &p, &size, &type))
			memcpy(&thePrefs->polarization, p, sizeof(float));


		PREFDisposeSet(&data);
	}
	
	PREFShutdown(prefs);
}

void GetSettings(BMessage* ioExtension, Prefs* thePrefs, BRect* selection, BInvoker** preview, BInvoker** progress)
{
	if (preview)
		*preview = NULL;
	if (progress)
		*progress = NULL;
	
	if (!ioExtension)
		return;

	// Have to use temp variable, because FindXXX helpfully zeroes out your
	// parameter if the data isn't found.
	float afloat;

	if (!ioExtension->FindFloat(PLUGIN_PREFIX "/lam_r", &afloat))
		thePrefs->lam_r = afloat;
	if (!ioExtension->FindFloat(PLUGIN_PREFIX "/lam_g", &afloat))
		thePrefs->lam_g = afloat;
	if (!ioExtension->FindFloat(PLUGIN_PREFIX "/lam_b", &afloat))
		thePrefs->lam_b = afloat;

	if (!ioExtension->FindFloat(PLUGIN_PREFIX "/contour_r", &afloat))
		thePrefs->contour_r = afloat;
	if (!ioExtension->FindFloat(PLUGIN_PREFIX "/contour_g", &afloat))
		thePrefs->contour_g = afloat;
	if (!ioExtension->FindFloat(PLUGIN_PREFIX "/contour_b", &afloat))
		thePrefs->contour_b = afloat;

	if (!ioExtension->FindFloat(PLUGIN_PREFIX "/edges_r", &afloat))
		thePrefs->edges_r = afloat;
	if (!ioExtension->FindFloat(PLUGIN_PREFIX "/edges_g", &afloat))
		thePrefs->edges_g = afloat;
	if (!ioExtension->FindFloat(PLUGIN_PREFIX "/edges_b", &afloat))
		thePrefs->edges_b = afloat;

	if (!ioExtension->FindFloat(PLUGIN_PREFIX "/brightness", &afloat))
		thePrefs->brightness = afloat;
	if (!ioExtension->FindFloat(PLUGIN_PREFIX "/scattering", &afloat))
		thePrefs->scattering = afloat;
	if (!ioExtension->FindFloat(PLUGIN_PREFIX "/polarization", &afloat))
		thePrefs->polarization = afloat;

	BRect r;
	if (!ioExtension->FindRect("selection_rect", &r))
		*selection = r;

	if (progress)
		ioExtension->FindPointer("progress_invoker", (void**) progress);
}


#pragma mark -

BitmapView::BitmapView(BRect r, ConfigView* cf)
	: BView(r, "bitmap", B_FOLLOW_TOP | B_FOLLOW_LEFT, B_WILL_DRAW)
{
	config = cf;
	SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
	bitmap = NULL;
}

void BitmapView::Draw(BRect)
{
	if (!bitmap)
	{
		bitmap = new BBitmap(Bounds(), B_BITMAP_IS_AREA | B_BITMAP_CLEAR_TO_WHITE, B_RGB32_LITTLE);
		Preview(config);
		SetViewColor(B_TRANSPARENT_COLOR);
	}
	else
		DrawBitmap(bitmap, B_ORIGIN);
}

int32 Preview(ConfigView* config)
{
	BBitmapAccessor accessor(config->preview->bitmap);
	accessor.SetDispose(false);
	bool again = true;
	
	
	while (again)
	{
		again = false;
		{
			BMessage ioExt, ioCap;
			GetConfigMessage(&ioExt, &ioCap);
			Manipulate(&accessor, &ioExt, false);
		}
		
		if (config->Window())
		{
			config->Window()->Lock();
			config->preview->Draw(config->preview->Bounds());
			again = config->mAgain;
			config->mAgain = false;
			config->Window()->Unlock();
		}
	}	

	config->mThread = B_ERROR;
	return B_OK;
}

void InitControlSettings(ConfigView* config)
{
	config->lamRSlider->SetValue(DefaultPrefs.lam_r);
	config->lamGSlider->SetValue(DefaultPrefs.lam_g);
	config->lamBSlider->SetValue(DefaultPrefs.lam_b);
	
	config->contourRSlider->SetValue(DefaultPrefs.contour_r);
	config->contourGSlider->SetValue(DefaultPrefs.contour_g);
	config->contourBSlider->SetValue(DefaultPrefs.contour_b);

	config->edgesRSlider->SetValue(DefaultPrefs.edges_r);
	config->edgesGSlider->SetValue(DefaultPrefs.edges_g);
	config->edgesBSlider->SetValue(DefaultPrefs.edges_b);

	config->brightnessSlider->SetValue(DefaultPrefs.brightness);
	config->scatteringSlider->SetValue(DefaultPrefs.scattering);
	config->polarizationSlider->SetValue(DefaultPrefs.polarization);
}
