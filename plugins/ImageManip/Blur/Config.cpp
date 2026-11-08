#include <stdio.h>

#define P_IMPEXP	// Have to do this before including libprefs.h
#include "libprefs.h"

#include <Window.h>
#include <string.h>
#include "Config.h"
#include "ImageManipAddon.h"

#define PLUGIN_PREFIX "variable-blur"

const int32 MINSIZE = 1, MAXSIZE = 13;

Prefs DefaultPrefs = {9, false};

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

	ioExtension->AddInt32(PLUGIN_PREFIX "/kernel_size", DefaultPrefs.kernelSize);
	ioExtension->AddBool(PLUGIN_PREFIX "/preview", DefaultPrefs.preview);

	return B_OK;
}


// Very roughly based on the settings dialog used by blur2.c

status_t MakeConfig(BMessage *ioExtension, BView **outView)
{
	BRect bounds(0, 0, 180, 40);
	ConfigView* config;
	*outView = config = new ConfigView(bounds, "blur configuration", B_WILL_DRAW, B_FOLLOW_NONE);
	config->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
	
	LoadPrefs(&DefaultPrefs);

	TSlider* slider;
	config->mKernelSize = slider = new TSlider(bounds, PLUGIN_PREFIX "/kernel_size", "Radius", NULL, MINSIZE, MAXSIZE);
	slider->SetHashMarks(B_HASH_MARKS_BOTTOM);
	slider->SetHashMarkCount(5);
	slider->SetLimitLabels("1", "13");
	slider->ResizeToPreferred();


	config->ResizeTo(slider->Frame().right, slider->Frame().bottom);
	config->AddChild(slider);
	slider->MakeFocus(true);

	if (ioExtension)
	{
		bool preview;
		if (!ioExtension->FindBool(PLUGIN_PREFIX "/preview", &preview))
			DefaultPrefs.preview = preview;

		int32 kernelSize;
		if (!ioExtension->FindInt32(PLUGIN_PREFIX "/kernel_size", &kernelSize))
			DefaultPrefs.kernelSize = kernelSize;

		ioExtension->FindPointer("preview_invoker", (void**) &config->mPreviewInvoker);
		slider->mPreviewInvoker = config->mPreviewInvoker;

		config->ResizeBy(0, 25);
		bounds = config->Bounds();
		bounds.top = bounds.bottom - 20;
		config->AddChild(config->mLivePreview = new BCheckBox(bounds, PLUGIN_PREFIX "/preview", "Preview", new BMessage('PRVW')));
		config->mLivePreview->SetValue(DefaultPrefs.preview);
	}
	slider->SetValue((DefaultPrefs.kernelSize - 1) / 2);
	
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
	bool doit = value != Value() && Target();
	BSlider::SetValue(value);

	if (doit && value == Value())
	{
		DefaultPrefs.kernelSize = 2 * Value() + 1;
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

#define SIGNATURE "x-vnd.wombat.plugins"

// This is only called by main to save out default settings
void SavePrefs()
{
	PREFHandle prefs;
	PREFData data;
	if (PREFInit(SIGNATURE, &prefs))
		return;

	PREFLoadSet(prefs, PLUGIN_PREFIX, true, &data);
	PREFSetData(data, "kernel_size", &DefaultPrefs.kernelSize, sizeof(int32), B_INT32_TYPE);
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
		if (!PREFGetData(data, "kernel_size", &p, &size, &type))
			memcpy(&thePrefs->kernelSize, p, sizeof(int32)); // Not guaranteed to be byte aligned.
		if (!PREFGetData(data, "preview", &p, &size, &type))
			thePrefs->preview = *(bool*) p;
		PREFDisposeSet(&data);
	}
	
	PREFShutdown(prefs);
}

