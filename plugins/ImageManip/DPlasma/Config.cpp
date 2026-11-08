#include <stdlib.h>

#define P_IMPEXP	// Have to do this before including libprefs.h

#include <string.h>
#include <Window.h>
#include "libprefs.h"
#include "Config.h"
#include "ImageManipAddon.h"

#define PLUGIN_PREFIX "dplasma"

Prefs DefaultPrefs = {1.0, -1, false};

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
//	ioCapability->AddInt32("progress_invoker", B_POINTER_TYPE);
	ioCapability->AddInt32("preview_invoker", B_POINTER_TYPE);

	ioExtension->AddFloat(PLUGIN_PREFIX "/turbulence", DefaultPrefs.turbulence);
	ioExtension->AddInt32(PLUGIN_PREFIX "/seed", DefaultPrefs.seed == -1 ? real_time_clock() : DefaultPrefs.seed);
	ioExtension->AddBool(PLUGIN_PREFIX "/preview", DefaultPrefs.preview);
	return B_OK;
}


status_t MakeConfig(BMessage *ioExtension, BView **outView)
{
	BRect bounds(0, 0, 140, 50);
	ConfigView* config;
	*outView = config = new ConfigView(bounds, "dplasma configuration", B_WILL_DRAW, B_FOLLOW_NONE);
	config->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
	
	LoadPrefs(&DefaultPrefs);

	TSlider* slider;
	config->mTurbulence = slider = new TSlider(bounds, "turbulence", "Turbulence", NULL, 0, 100);
	slider->SetHashMarks(B_HASH_MARKS_BOTTOM);
	slider->SetHashMarkCount(5);
	slider->SetLimitLabels("0.0", "1.0");
	slider->ResizeToPreferred();
	config->AddChild(slider);


	bounds.OffsetBy(0, bounds.Height());
	config->mSeed = slider = new TSlider(bounds, "seed", "Random Seed", NULL, 0, 100);
	slider->SetHashMarks(B_HASH_MARKS_BOTTOM);
	slider->SetHashMarkCount(5);
	slider->SetLimitLabels("0", "100");
	slider->ResizeToPreferred();


	config->ResizeTo(slider->Frame().right, slider->Frame().bottom);
	config->AddChild(slider);
	slider->MakeFocus(true);

	if (ioExtension)
	{
		float turbulence;
		if (!ioExtension->FindFloat(PLUGIN_PREFIX "/turbulence", &turbulence))
			DefaultPrefs.turbulence = turbulence;
		bool preview;
		if (!ioExtension->FindBool(PLUGIN_PREFIX "/preview", &preview))
			DefaultPrefs.preview = preview;

		ioExtension->FindPointer("preview_invoker", (void**) &config->mPreviewInvoker);
		config->mTurbulence->mPreviewInvoker = config->mPreviewInvoker;
		config->mSeed->mPreviewInvoker = config->mPreviewInvoker;

		config->ResizeBy(0, 25);
		bounds = config->Bounds();
		bounds.top = bounds.bottom - 20;
		config->AddChild(config->mLivePreview = new BCheckBox(bounds, PLUGIN_PREFIX "/preview", "Preview", new BMessage('PRVW')));
		config->mLivePreview->SetValue(DefaultPrefs.preview);
	}
	config->mTurbulence->SetValue(DefaultPrefs.turbulence * 100);
	config->mSeed->SetValue(rand() % 101);
	
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
			if (!strcmp(Name(), "turbulence"))
				DefaultPrefs.turbulence = value / 100.0;
			else
				DefaultPrefs.seed = value;

			if (mPreviewInvoker && DefaultPrefs.preview)
			{
				BLooper* looper;
				BHandler* handler;
				handler = mPreviewInvoker->Target(&looper);
				BMessage msg(*mPreviewInvoker->Message());
	
				// Gross hack to work around BSlider bug (Modification message is queued up but not delivered while tracking BSlider)
	
				if (looper && looper == Window())
					looper->DispatchMessage(&msg, handler);
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
	PREFSetData(data, "turbulence", &DefaultPrefs.turbulence, sizeof(int32), B_FLOAT_TYPE);
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

	*thePrefs = DefaultPrefs;
	
	if (!PREFLoadSet(prefs, PLUGIN_PREFIX, true, &data))
	{
		const void* p;
		ssize_t size;
		uint32 type;
		if (!PREFGetData(data, "turbulence", &p, &size, &type))
			memcpy(&thePrefs->turbulence, p, sizeof(float)); // Not guaranteed to be byte aligned.

		if (!PREFGetData(data, "preview", &p, &size, &type))
			thePrefs->preview = *(bool*) p;
		PREFDisposeSet(&data);
	}
	
	PREFShutdown(prefs);
}

