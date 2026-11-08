#include "Config.h"
#include "ImageManipAddon.h"
#include <Window.h>
#include <string.h>

#define PLUGIN_PREFIX "solarize"

static Prefs DefaultPrefs = {50, false, false, false};

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

	ioExtension->AddInt32(PLUGIN_PREFIX "/threshold", DefaultPrefs.threshold);
	ioExtension->AddBool(PLUGIN_PREFIX "/stretch", DefaultPrefs.stretch);
	ioExtension->AddBool(PLUGIN_PREFIX "/lunar", DefaultPrefs.lunar);
	ioExtension->AddBool(PLUGIN_PREFIX "/preview", DefaultPrefs.preview);
	return B_OK;
}


status_t MakeConfig(BMessage *ioExtension, BView **outView)
{
	BRect bounds(0, 0, 150, 40);
	ConfigView* config;
	*outView = config = new ConfigView(bounds, "solarize configuration", B_WILL_DRAW, B_FOLLOW_NONE);
	config->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
	
	LoadPrefs(&DefaultPrefs);

	TSlider* slider;
	config->mThreshold = slider = new TSlider(bounds, "threshold", "Threshold", NULL, 1, 255);
	slider->SetHashMarks(B_HASH_MARKS_BOTTOM);
	slider->SetHashMarkCount(5);
	slider->SetLimitLabels("1", "255");
	slider->ResizeToPreferred();

	config->ResizeTo(slider->Frame().right, slider->Frame().bottom);
	config->AddChild(slider);

	config->ResizeBy(0, 25);
	bounds = config->Bounds();
	bounds.top = bounds.bottom - 20;
	config->AddChild(config->mStretch = new BCheckBox(bounds, PLUGIN_PREFIX "/stretch", "Maximize Contrast", new BMessage('STRC')));
	config->mStretch->SetValue(DefaultPrefs.stretch);

	config->ResizeBy(0, 25);
	bounds = config->Bounds();
	bounds.top = bounds.bottom - 20;
	config->AddChild(config->mLunar = new BCheckBox(bounds, PLUGIN_PREFIX "/lunar", "Lunarize", new BMessage('MOON')));
	config->mLunar->SetValue(DefaultPrefs.lunar);


	if (ioExtension)
	{
		bool preview;
		if (!ioExtension->FindBool(PLUGIN_PREFIX "/preview", &preview))
			DefaultPrefs.preview = preview;

		int32 threshold;
		if (!ioExtension->FindInt32(PLUGIN_PREFIX "/threshold", &threshold))
			DefaultPrefs.threshold = threshold;

		if (!ioExtension->FindPointer("preview_invoker", (void**) &config->mPreviewInvoker))
		{
			slider->mPreviewInvoker = config->mPreviewInvoker;
	
			config->ResizeBy(0, 25);
			bounds = config->Bounds();
			bounds.top = bounds.bottom - 20;
			config->AddChild(config->mLivePreview = new BCheckBox(bounds, PLUGIN_PREFIX "/preview", "Preview", new BMessage('PRVW')));
			config->mLivePreview->SetValue(DefaultPrefs.preview);
		}
	}
	slider->SetValue(DefaultPrefs.threshold);
	
	return B_OK;
}

ConfigView::~ConfigView()
{
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
		case 'STRC':
			DefaultPrefs.stretch = mStretch->Value();
			break;
		case 'MOON':
			DefaultPrefs.lunar = mLunar->Value();
			break;
		case 'PRVW':
			DefaultPrefs.preview = mLivePreview->Value();
			break;
		default:
			return;
	}

	if (mPreviewInvoker)
	{
		BMessage msg(*mPreviewInvoker->Message());
		msg.AddBool("preview", DefaultPrefs.preview);
		mPreviewInvoker->Invoke(&msg);
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
			DefaultPrefs.threshold = value;
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


#pragma mark -

#define SIGNATURE "x-vnd.wombat.plugins"

// This is only called by main to save out default settings
void SavePrefs()
{
	PREFHandle prefs;
	PREFData data;
	if (PREFInit(SIGNATURE, &prefs))
		return;

	PREFLoadSet(prefs, PLUGIN_PREFIX, true, &data);
	PREFSetData(data, "threshold", &DefaultPrefs.threshold, sizeof(int32), B_INT32_TYPE);
	PREFSetData(data, "stretch", &DefaultPrefs.stretch, sizeof(bool), B_BOOL_TYPE);
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
		if (!PREFGetData(data, "threshold", &p, &size, &type))
			memcpy(&thePrefs->threshold, p, sizeof(int32)); // Not guaranteed to be byte aligned.
		if (!PREFGetData(data, "stretch", &p, &size, &type))
			thePrefs->stretch = *(bool*) p;
		if (!PREFGetData(data, "preview", &p, &size, &type))
			thePrefs->preview = *(bool*) p;
		PREFDisposeSet(&data);
	}
	
	PREFShutdown(prefs);
}

