#include <stdio.h>
#include <algobase.h>
#include <MenuField.h>

#define P_IMPEXP	// Have to do this before including libprefs.h
#include "libprefs.h"
#include "Config.h"
#include "ImageManipAddon.h"
#include "TSlider.h"

#define PLUGIN_PREFIX "whirlpinch"

Prefs DefaultPrefs = {90.0, 0.0, 1.0, false};

static bool inited;

status_t
GetConfigMessage(
	BMessage *ioExtension,
	BMessage *ioCapability)
{
	if (!inited)
		LoadPrefs(&DefaultPrefs);
	
	// Let them know that we support these extensions
	ioCapability->AddInt32("selection_rect", B_RECT_TYPE);
	ioCapability->AddInt32("progress_invoker", B_POINTER_TYPE);
	ioCapability->AddInt32("preview_invoker", B_POINTER_TYPE);
	ioCapability->AddInt32("cancel_queue", B_POINTER_TYPE);

	ioExtension->AddDouble(PLUGIN_PREFIX "/whirl", DefaultPrefs.whirl);
	ioExtension->AddDouble(PLUGIN_PREFIX "/pinch", DefaultPrefs.pinch);
	ioExtension->AddDouble(PLUGIN_PREFIX "/radius", DefaultPrefs.radius);

	ioExtension->AddBool(PLUGIN_PREFIX "/preview", DefaultPrefs.preview);

	return B_OK;
}


status_t MakeConfig(BMessage *ioExtension, BView **outView)
{
	BRect bounds(0, 0, 200, 40);
	ConfigView* config;
	*outView = config = new ConfigView(bounds, PLUGIN_PREFIX " configuration", B_WILL_DRAW, B_FOLLOW_NONE);
	config->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
	
	LoadPrefs(&DefaultPrefs);

	TSlider* slider = config->mWhirl = new MySlider(bounds, "whirl", "Whirl angle", "%d°", NULL, -720, 720);
	slider->SetHashMarks(B_HASH_MARKS_BOTTOM);
	slider->SetHashMarkCount(9);
	slider->SetLimitLabels("-720", "720");
	slider->ResizeToPreferred();
	bounds = slider->Frame();
	config->AddChild(slider);

	bounds.OffsetBy(0, bounds.Height());
	slider = config->mPinch = new MySlider(bounds, "pinch", "Pinch amount", "%0.2g", NULL, -200, 200, 0.01);
	slider->SetHashMarks(B_HASH_MARKS_BOTTOM);
	slider->SetHashMarkCount(5);
	slider->SetLimitLabels("-2.0", "2.0");
	slider->ResizeToPreferred();
	bounds = slider->Frame();
	config->AddChild(slider);

	bounds.OffsetBy(0, bounds.Height());
	slider = config->mRadius = new MySlider(bounds, "radius", "Radius", "%0.2g", NULL, 0, 200, 0.01);
	slider->SetHashMarks(B_HASH_MARKS_BOTTOM);
	slider->SetHashMarkCount(5);
	slider->SetLimitLabels("0", "2");
	slider->ResizeToPreferred();
	bounds = slider->Frame();
	config->AddChild(slider);


	config->ResizeTo(slider->Frame().right, slider->Frame().bottom);

	if (ioExtension)
	{
		BRect r;
		GetSettings(ioExtension, &DefaultPrefs, &r, &config->mPreviewInvoker);

		config->mWhirl->mPreviewInvoker = config->mPinch->mPreviewInvoker = config->mRadius->mPreviewInvoker = config->mPreviewInvoker;

		config->ResizeBy(0, 25);
		bounds = config->Bounds();
		bounds.top = bounds.bottom - 20;
		config->AddChild(config->mLivePreview = new BCheckBox(bounds, PLUGIN_PREFIX "/preview", "Preview", new BMessage('PRVW')));
		config->mLivePreview->SetValue(DefaultPrefs.preview);
	}

	config->mWhirl->SetValue(DefaultPrefs.whirl);
	config->mPinch->SetValue(DefaultPrefs.pinch);
	config->mRadius->SetValue(DefaultPrefs.radius);
	
	return B_OK;
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

	if (mPreviewInvoker && DefaultPrefs.preview)
		mPreviewInvoker->Invoke();
}

void ConfigView::MessageReceived(BMessage *msg)
{
	switch(msg->what)
	{
		case 'PRVW':
			DefaultPrefs.preview = mLivePreview->Value();
			break;
		
		default:
			BView::MessageReceived(msg);
			return;
	}

	if (mPreviewInvoker)
	{
		BMessage msg(*mPreviewInvoker->Message());
		msg.AddBool("preview", DefaultPrefs.preview);
		mPreviewInvoker->Invoke(&msg);
	}
}

void MySlider::ValueChanged()
{
	const char* name = Name();
	double value = mOffset + Value() * mScale;

	if (!strcmp("whirl", name))
		DefaultPrefs.whirl = value;
	if (!strcmp("pinch", name))
		DefaultPrefs.pinch = value;
	if (!strcmp("radius", name))
		DefaultPrefs.radius = value;

	if (DefaultPrefs.preview)
		TSlider::ValueChanged();
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
	PREFSetData(data, "whirl", &DefaultPrefs.whirl, sizeof(double), B_DOUBLE_TYPE);
	PREFSetData(data, "pinch", &DefaultPrefs.pinch, sizeof(double), B_DOUBLE_TYPE);
	PREFSetData(data, "radius", &DefaultPrefs.radius, sizeof(double), B_DOUBLE_TYPE);
	PREFSetData(data, "preview", &DefaultPrefs.preview, sizeof(bool), B_BOOL_TYPE);
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

	inited = true;
	*thePrefs = DefaultPrefs;
	
	if (!PREFLoadSet(prefs, PLUGIN_PREFIX, true, &data))
	{
		const void* p;
		ssize_t size;
		uint32 type;
		
		if (!PREFGetData(data, "whirl", &p, &size, &type))
			memcpy(&thePrefs->whirl, p, sizeof(double)); // Not guaranteed to be word aligned.
		if (!PREFGetData(data, "pinch", &p, &size, &type))
			memcpy(&thePrefs->pinch, p, sizeof(double)); // Not guaranteed to be word aligned.
		if (!PREFGetData(data, "radius", &p, &size, &type))
			memcpy(&thePrefs->radius, p, sizeof(double)); // Not guaranteed to be word aligned.

		if (!PREFGetData(data, "preview", &p, &size, &type))
			thePrefs->preview = *(bool*) p;
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
	bool abool;
	if (!ioExtension->FindBool(PLUGIN_PREFIX "/preview", &abool))
		thePrefs->preview = abool;

	double d;
	if (!ioExtension->FindDouble(PLUGIN_PREFIX "/whirl", &d))
		thePrefs->whirl = d;
	if (!ioExtension->FindDouble(PLUGIN_PREFIX "/pinch", &d))
		thePrefs->pinch = d;
	if (!ioExtension->FindDouble(PLUGIN_PREFIX "/radius", &d))
		thePrefs->radius = d;

	BRect r;
	if (!ioExtension->FindRect("selection_rect", &r))
		*selection = r;

	if (progress)
		ioExtension->FindPointer("progress_invoker", (void**) progress);
	if (preview)
		ioExtension->FindPointer("preview_invoker", (void**) preview);
}

