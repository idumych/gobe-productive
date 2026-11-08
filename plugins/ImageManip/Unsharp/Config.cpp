#include <stdio.h>
#include <algobase.h>

#define P_IMPEXP	// Have to do this before including libprefs.h
#include "libprefs.h"
#include "Config.h"
#include "ImageManipAddon.h"
#include "TSlider.h"

#define PLUGIN_PREFIX "unsharp"

Prefs DefaultPrefs = {5.0, 0.5, 1, false, true};

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
	ioCapability->AddInt32("preview_invoker", B_POINTER_TYPE);
	ioCapability->AddInt32("cancel_queue", B_POINTER_TYPE);

	ioExtension->AddDouble(PLUGIN_PREFIX "/radius", DefaultPrefs.radius);
	ioExtension->AddDouble(PLUGIN_PREFIX "/amount", DefaultPrefs.amount);
	ioExtension->AddInt32(PLUGIN_PREFIX "/threshold", DefaultPrefs.threshold);
	ioExtension->AddBool(PLUGIN_PREFIX "/preview", DefaultPrefs.preview);

	system_info info;
	get_system_info(&info);
	if (info.cpu_count == 1)
		DefaultPrefs.multithread = false;
	ioExtension->AddBool(PLUGIN_PREFIX "/multithread", DefaultPrefs.multithread);
	

	return B_OK;
}


status_t MakeConfig(BMessage *ioExtension, BView **outView)
{
	BRect bounds(0, 0, 205, 40);
	ConfigView* config;
	*outView = config = new ConfigView(bounds, PLUGIN_PREFIX " configuration", B_WILL_DRAW, B_FOLLOW_NONE);
	config->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
	
	LoadPrefs(&DefaultPrefs);

	MySlider* slider;
	config->mAmount = slider = new MySlider(bounds, PLUGIN_PREFIX "/amount", "Amount", "%d%%", NULL, 1, 500);
	slider->SetLimitLabels("1", "500");
	slider->SetHashMarks(B_HASH_MARKS_BOTTOM);
	slider->SetHashMarkCount(2);
	slider->ResizeToPreferred();
	bounds = slider->Frame();
	config->AddChild(slider);

	bounds.OffsetBy(0, bounds.Height());
	config->mRadius = slider = new MySlider(bounds, PLUGIN_PREFIX "/radius", "Radius", "%0.1f pixels", NULL, 10, 2500, 0.1);
	slider->SetLimitLabels("1", "250");
	slider->SetHashMarks(B_HASH_MARKS_BOTTOM);
	slider->SetHashMarkCount(2);
	slider->ResizeToPreferred();
	bounds = slider->Frame();
	config->AddChild(slider);

	bounds.OffsetBy(0, bounds.Height());
	config->mThreshold = slider = new MySlider(bounds, PLUGIN_PREFIX "/threshold", "Threshold", "%d levels", NULL, 0, 255);
	slider->SetLimitLabels("0", "255");
	slider->SetHashMarks(B_HASH_MARKS_BOTTOM);
	slider->SetHashMarkCount(2);
	slider->ResizeToPreferred();
	bounds = slider->Frame();
	config->AddChild(slider);

	config->ResizeTo(slider->Frame().right, slider->Frame().bottom);

	if (ioExtension)
	{
		BRect r;
		GetSettings(ioExtension, &DefaultPrefs, &r, &config->mPreviewInvoker);

		config->mRadius->mPreviewInvoker = config->mAmount->mPreviewInvoker = config->mThreshold->mPreviewInvoker = config->mPreviewInvoker;

		config->ResizeBy(0, 25);
		bounds = config->Bounds();
		bounds.top = bounds.bottom - 20;
		config->AddChild(config->mLivePreview = new BCheckBox(bounds, PLUGIN_PREFIX "/preview", "Preview", new BMessage('PRVW')));
		config->mLivePreview->SetValue(DefaultPrefs.preview);
		
		system_info info;
		get_system_info(&info);
		if (info.cpu_count > 1)
		{
			config->ResizeBy(0, 25);
			bounds = config->Bounds();
			bounds.top = bounds.bottom - 20;
			config->AddChild(config->mMultiThread = new BCheckBox(bounds, PLUGIN_PREFIX "/multithread", "Multithreaded", new BMessage('MULT')));
			config->mMultiThread->SetValue(DefaultPrefs.multithread);
		}
	}

	config->mRadius->SetValue((int32) DefaultPrefs.radius * 10);
	config->mAmount->SetValue((int32) (DefaultPrefs.amount * 100));
	config->mThreshold->SetValue(DefaultPrefs.threshold);
	
	return B_OK;
}

void ConfigView::AttachedToWindow()
{
	for (int x = 0; x < CountChildren(); x++)
	{
		BInvoker* invoker = dynamic_cast<BInvoker*>(ChildAt(x));
		if (invoker)
			invoker->SetTarget(this);
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
		case 'MULT':
			DefaultPrefs.multithread = mMultiThread->Value();
			return; // Don't cause a preview to occur!
		
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



#define SIGNATURE "x-vnd.wombat.plugins"

// This is only called by main to save out default settings
void SavePrefs()
{
	PREFHandle prefs;
	PREFData data;
	if (PREFInit(SIGNATURE, &prefs))
		return;

	PREFLoadSet(prefs, PLUGIN_PREFIX, true, &data);
	PREFSetData(data, "radius", &DefaultPrefs.radius, sizeof(double), B_DOUBLE_TYPE);
	PREFSetData(data, "amount", &DefaultPrefs.amount, sizeof(double), B_DOUBLE_TYPE);
	PREFSetData(data, "threshold", &DefaultPrefs.threshold, sizeof(int32), B_INT32_TYPE);
	PREFSetData(data, "preview", &DefaultPrefs.preview, sizeof(bool), B_BOOL_TYPE);
	PREFSetData(data, "multithread", &DefaultPrefs.multithread, sizeof(bool), B_BOOL_TYPE);
	PREFSaveSet(data);
	PREFDisposeSet(&data);
	
	PREFShutdown(prefs);
}

void MySlider::ValueChanged()
{
	const char* name = Name();
	if (!strcmp(PLUGIN_PREFIX "/threshold", name))
		DefaultPrefs.threshold = Value();
	else if (!strcmp(PLUGIN_PREFIX "/amount", name))
		DefaultPrefs.amount = Value() / 100.0;
	else if (!strcmp(PLUGIN_PREFIX "/radius", name))
		DefaultPrefs.radius = Value() / 10.0;

	if (DefaultPrefs.preview)
		TSlider::ValueChanged();
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
		
		if (!PREFGetData(data, "radius", &p, &size, &type))
			memcpy(&thePrefs->radius, p, sizeof(double)); // Not guaranteed to be word aligned.
		if (!PREFGetData(data, "amount", &p, &size, &type))
			memcpy(&thePrefs->amount, p, sizeof(double)); // Not guaranteed to be word aligned.
		if (!PREFGetData(data, "threshold", &p, &size, &type))
			memcpy(&thePrefs->threshold, p, sizeof(int32)); // Not guaranteed to be word aligned.

		if (!PREFGetData(data, "preview", &p, &size, &type))
			thePrefs->preview = *(bool*) p;
		if (!PREFGetData(data, "multithread", &p, &size, &type))
			thePrefs->multithread = *(bool*) p;
		PREFDisposeSet(&data);
	}
	
	PREFShutdown(prefs);
}

void GetSettings(BMessage* ioExtension, Prefs* thePrefs, BRect* selection, BInvoker** preview, BInvoker** progress, BMessageQueue** cancel)
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
	if (!ioExtension->FindBool(PLUGIN_PREFIX "/multithread", &abool))
		thePrefs->multithread = abool;

	double adouble;
	int32 anint;
	if (!ioExtension->FindDouble(PLUGIN_PREFIX "/radius", &adouble))
		thePrefs->radius = adouble;
	if (!ioExtension->FindDouble(PLUGIN_PREFIX "/amount", &adouble))
		thePrefs->amount = adouble;
	if (!ioExtension->FindInt32(PLUGIN_PREFIX "/threshold", &anint))
		thePrefs->threshold = anint;

	BRect r;
	if (!ioExtension->FindRect("selection_rect", &r))
		*selection = r;

	if (progress)
		ioExtension->FindPointer("progress_invoker", (void**) progress);
	if (preview)
		ioExtension->FindPointer("preview_invoker", (void**) preview);
	if (cancel)
		ioExtension->FindPointer("cancel_queue", (void**) cancel);
}

