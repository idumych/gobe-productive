#include <stdio.h>

#define P_IMPEXP	// Have to do this before including libprefs.h
#include <string.h>
#include <Window.h>
#include "libprefs.h"
#include "Config.h"
#include "ImageManipAddon.h"

#define PLUGIN_PREFIX "carpet"

Prefs DefaultPrefs = {20, 10, 1, 2, false};

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

	ioExtension->AddInt32(PLUGIN_PREFIX "/depth", DefaultPrefs.depth);
	ioExtension->AddInt32(PLUGIN_PREFIX "/noise", DefaultPrefs.noise);
	ioExtension->AddInt32(PLUGIN_PREFIX "/max_shift", DefaultPrefs.max_shift);
	ioExtension->AddInt32(PLUGIN_PREFIX "/repeats", DefaultPrefs.repeats);
	ioExtension->AddBool(PLUGIN_PREFIX "/preview", DefaultPrefs.preview);
	return B_OK;
}

//	((bvals.depth < 0) || (bvals.depth > 50) ||
//	 (bvals.noise < 0) || (bvals.noise > 50) ||
//	 (bvals.max_shift < 0) || (bvals.max_shift > 5) ||
//	 (bvals.repeats < 0) || (bvals.repeats > 5)))

status_t MakeConfig(BMessage *ioExtension, BView **outView)
{
	BRect bounds(0, 0, 200, 50);
	ConfigView* config;
	*outView = config = new ConfigView(bounds, "carpet configuration", B_WILL_DRAW, B_FOLLOW_NONE);
	config->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
	
	LoadPrefs(&DefaultPrefs);

	TSlider* slider;
	config->mDepth = slider = new TSlider(bounds, "depth", "Depth", NULL, 0, 50);
	slider->SetHashMarks(B_HASH_MARKS_BOTTOM);
	slider->SetHashMarkCount(6);
	slider->SetLimitLabels("0", "50");
	slider->ResizeToPreferred();
	config->AddChild(slider);

	bounds.OffsetBy(0, bounds.Height());
	config->mNoise = slider = new TSlider(bounds, "noise", "Noise", NULL, 0, 50);
	slider->SetHashMarks(B_HASH_MARKS_BOTTOM);
	slider->SetHashMarkCount(6);
	slider->SetLimitLabels("0", "50");
	slider->ResizeToPreferred();
	config->AddChild(slider);

	bounds.OffsetBy(0, bounds.Height());
	config->mShift = slider = new TSlider(bounds, "max_shift", "Maximum Shift", NULL, 0, 5);
	slider->SetHashMarks(B_HASH_MARKS_BOTTOM);
	slider->SetHashMarkCount(6);
	slider->SetLimitLabels("0", "5");
	slider->ResizeToPreferred();
	config->AddChild(slider);

	bounds.OffsetBy(0, bounds.Height());
	config->mRepeats = slider = new TSlider(bounds, "repeats", "Repeats", NULL, 0, 5);
	slider->SetHashMarks(B_HASH_MARKS_BOTTOM);
	slider->SetHashMarkCount(6);
	slider->SetLimitLabels("0", "5");
	slider->ResizeToPreferred();
	config->AddChild(slider);


	config->ResizeTo(slider->Frame().right, slider->Frame().bottom);

	if (ioExtension)
	{
		ioExtension->FindPointer("preview_invoker", (void**) &config->mPreviewInvoker);
		config->mDepth->mPreviewInvoker = config->mNoise->mPreviewInvoker = 
		config->mShift->mPreviewInvoker = config->mRepeats->mPreviewInvoker = config->mPreviewInvoker;

		config->ResizeBy(0, 25);
		bounds = config->Bounds();
		bounds.top = bounds.bottom - 20;
		config->AddChild(config->mLivePreview = new BCheckBox(bounds, PLUGIN_PREFIX "/preview", "Preview", new BMessage('PRVW')));
		config->mLivePreview->SetValue(DefaultPrefs.preview);
	}
	config->mDepth->SetValue(DefaultPrefs.depth);
	config->mNoise->SetValue(DefaultPrefs.noise);
	config->mShift->SetValue(DefaultPrefs.max_shift);
	config->mRepeats->SetValue(DefaultPrefs.repeats);
	
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
			if (mPreviewInvoker)
			{
				BMessage msg(*mPreviewInvoker->Message());
				msg.AddBool("preview", DefaultPrefs.preview);
				mPreviewInvoker->Invoke(&msg);
			}
			break;
	}
}


void TSlider::SetValue(int32 value)
{
	// Don't invoke the preview unless the value actually changes.
	bool hasTarget = !!Target();
	bool changed = value != Value();

	BSlider::SetValue(value);
	
	if (changed && value == Value())
	{
		if (hasTarget)
		{
			if (!strcmp(Name(), "depth"))
				DefaultPrefs.depth = value;
			else if (!strcmp(Name(), "noise"))
				DefaultPrefs.noise = value;
			else if (!strcmp(Name(), "max_shift"))
				DefaultPrefs.max_shift = value;
			else
				DefaultPrefs.repeats = value;
			
			if (mPreviewInvoker && DefaultPrefs.preview)
			{
				BLooper* looper;
				BHandler* handler;
				handler = mPreviewInvoker->Target(&looper);
				BMessage msg(*mPreviewInvoker->Message());
	
				// Gross hack to work around BSlider bug (Modification message is queued up but not delivered while tracking BSlider)
	
				if (looper && looper == Window())
				{
					looper->DispatchMessage(&msg, handler);
				}
				else
					mPreviewInvoker->Invoke(&msg);
			}
		}
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
	PREFSetData(data, "depth", &DefaultPrefs.depth, sizeof(int32), B_INT32_TYPE);
	PREFSetData(data, "noise", &DefaultPrefs.noise, sizeof(int32), B_INT32_TYPE);
	PREFSetData(data, "max_shift", &DefaultPrefs.max_shift, sizeof(int32), B_INT32_TYPE);
	PREFSetData(data, "repeats", &DefaultPrefs.repeats, sizeof(int32), B_INT32_TYPE);
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
		if (!PREFGetData(data, "depth", &p, &size, &type))
			memcpy(&thePrefs->depth, p, sizeof(int32)); // Not guaranteed to be byte aligned.
		if (!PREFGetData(data, "noise", &p, &size, &type))
			memcpy(&thePrefs->noise, p, sizeof(int32)); // Not guaranteed to be byte aligned.
		if (!PREFGetData(data, "max_shift", &p, &size, &type))
			memcpy(&thePrefs->max_shift, p, sizeof(int32)); // Not guaranteed to be byte aligned.
		if (!PREFGetData(data, "repeats", &p, &size, &type))
			memcpy(&thePrefs->repeats, p, sizeof(int32)); // Not guaranteed to be byte aligned.
		if (!PREFGetData(data, "preview", &p, &size, &type))
			thePrefs->preview = *(bool*) p;
		PREFDisposeSet(&data);
	}
	
	PREFShutdown(prefs);
}


void PrefsFromMessage(Prefs* thePrefs, BMessage* msg, BInvoker** progress)
{
	bool b;
	if (!msg->FindBool(PLUGIN_PREFIX "/preview", &b))
		thePrefs->preview = b;

	int32 amount;
	if (!msg->FindInt32(PLUGIN_PREFIX "/depth", &amount))
		thePrefs->depth = amount;
	if (!msg->FindInt32(PLUGIN_PREFIX "/noise", &amount))
		thePrefs->noise = amount;
	if (!msg->FindInt32(PLUGIN_PREFIX "/max_shift", &amount))
		thePrefs->max_shift = amount;
	if (!msg->FindInt32(PLUGIN_PREFIX "/repeats", &amount))
		thePrefs->repeats = amount;
	if (progress)
		msg->FindPointer("progress_invoker", (void**) progress);

}
