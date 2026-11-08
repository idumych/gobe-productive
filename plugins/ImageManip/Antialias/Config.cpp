#include <stdio.h>
#include <algobase.h>

#define P_IMPEXP	// Have to do this before including libprefs.h
#include "libprefs.h"
#include "Config.h"
#include "ImageManipAddon.h"

#define PLUGIN_PREFIX "antialias"

Prefs DefaultPrefs = {1.0f/3.0f, 25, 25};

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

	ioExtension->AddFloat(PLUGIN_PREFIX "/weight", DefaultPrefs.weight);
	ioExtension->AddInt8(PLUGIN_PREFIX "/s_threshold", DefaultPrefs.s_threshold);
	ioExtension->AddFloat(PLUGIN_PREFIX "/d_threshold", DefaultPrefs.d_threshold);

	return B_OK;
}


status_t MakeConfig(BMessage *ioExtension, BView **outView)
{
	BRect bounds(0, 0, 150, 40);
	ConfigView* config;
	*outView = config = new ConfigView(bounds, PLUGIN_PREFIX " configuration", B_WILL_DRAW, B_FOLLOW_NONE);
	config->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
	
	LoadPrefs(&DefaultPrefs);

	MySlider* slider;

	config->mWeight = slider = new MySlider(bounds, "weight", "Weight", "%d", NULL, 0, 100);
	slider->SetHashMarks(B_HASH_MARKS_BOTTOM);
	slider->SetHashMarkCount(5);
	slider->SetLimitLabels("0", "100");
	slider->ResizeToPreferred();
	bounds = slider->Frame();
	config->AddChild(slider);

	bounds.OffsetBy(0, bounds.Height());
	config->mSameness = slider = new MySlider(bounds, "s_threshold", "Sameness Threshold", "%d", NULL, 0, 255);
	slider->SetHashMarks(B_HASH_MARKS_BOTTOM);
	slider->SetHashMarkCount(5);
	slider->SetLimitLabels("0", "100");
	slider->ResizeToPreferred();
	config->AddChild(slider);

	bounds.OffsetBy(0, bounds.Height());
	config->mDifference = slider = new MySlider(bounds, "d_threshold", "Difference Threshold", "%d", NULL, 0, 255);
	slider->SetHashMarks(B_HASH_MARKS_BOTTOM);
	slider->SetHashMarkCount(5);
	slider->SetLimitLabels("0", "100");
	slider->ResizeToPreferred();
	config->AddChild(slider);

	config->ResizeTo(slider->Frame().right, slider->Frame().bottom);

	if (ioExtension)
	{
		BRect r;
		GetSettings(ioExtension, &DefaultPrefs, &r, &config->mPreviewInvoker);

		config->mWeight->mPreviewInvoker = config->mSameness->mPreviewInvoker = config->mDifference->mPreviewInvoker =
			config->mPreviewInvoker;

		config->ResizeBy(0, 25);
		bounds = config->Bounds();
		bounds.top = bounds.bottom - 20;
		config->AddChild(config->mLivePreview = new BCheckBox(bounds, PLUGIN_PREFIX "/preview", "Preview", new BMessage('PRVW')));
		config->mLivePreview->SetValue(DefaultPrefs.preview);
	}

	config->mWeight->SetValue(100 * DefaultPrefs.weight);
	config->mSameness->SetValue((int32) DefaultPrefs.s_threshold);
	config->mDifference->SetValue((int32) DefaultPrefs.d_threshold);
	
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

	if (!strcmp(name, "weight"))
		DefaultPrefs.weight = Value() / 100.0f;
	if (!strcmp(name, "s_threshold"))
		DefaultPrefs.s_threshold = Value();
	if (!strcmp(name, "d_threshold"))
		DefaultPrefs.d_threshold = Value();

	if (mPreviewInvoker && DefaultPrefs.preview)
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
	PREFSetData(data, "weight", &DefaultPrefs.weight, sizeof(float), B_FLOAT_TYPE);
	PREFSetData(data, "s_threshold", &DefaultPrefs.s_threshold, sizeof(uchar), B_UINT8_TYPE);
	PREFSetData(data, "d_threshold", &DefaultPrefs.d_threshold, sizeof(uchar), B_UINT8_TYPE);

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
		
		if (!PREFGetData(data, "weight", &p, &size, &type))
			memcpy(&thePrefs->weight, p, sizeof(float)); // Not guaranteed to be word aligned.
		if (!PREFGetData(data, "s_threshold", &p, &size, &type))
			thePrefs->s_threshold = *(uchar*) p;
		if (!PREFGetData(data, "d_threshold", &p, &size, &type))
			thePrefs->d_threshold = *(uchar*) p;

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

	bool abool;
	uchar auchar;
	float afloat;
	if (!ioExtension->FindBool(PLUGIN_PREFIX "/preview", &abool))
		thePrefs->preview = abool;
	if (!ioExtension->FindFloat(PLUGIN_PREFIX "/weight", &afloat))
		thePrefs->weight = afloat;
	if (!ioExtension->FindInt8(PLUGIN_PREFIX "/s_threshold", (int8*) &auchar))
		thePrefs->s_threshold = auchar;
	if (!ioExtension->FindInt8(PLUGIN_PREFIX "/d_threshold", (int8*) &auchar))
		thePrefs->d_threshold = auchar;

	BRect r;
	if (!ioExtension->FindRect("selection_rect", &r))
		*selection = r;

	if (progress)
		ioExtension->FindPointer("progress_invoker", (void**) progress);
	if (preview)
		ioExtension->FindPointer("preview_invoker", (void**) preview);
}

