#include <stdio.h>
#include <algobase.h>

#define P_IMPEXP	// Have to do this before including libprefs.h
#include "libprefs.h"
#include "Config.h"
#include "ImageManipAddon.h"

#define PLUGIN_PREFIX "randomize"

Prefs DefaultPrefs = {50.0, 1, 0, false};


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
	ioCapability->AddInt32("config_bitmap", B_POINTER_TYPE);
	ioCapability->AddInt32("cancel_queue", B_POINTER_TYPE);

	ioExtension->AddFloat(PLUGIN_PREFIX "/randomness", DefaultPrefs.randomness);
	ioExtension->AddInt32(PLUGIN_PREFIX "/repeats", DefaultPrefs.repeats);
	ioExtension->AddInt32(PLUGIN_PREFIX "/type", DefaultPrefs.type);
	ioExtension->AddBool(PLUGIN_PREFIX "/preview", DefaultPrefs.preview);
	return B_OK;
}


status_t MakeConfig(BMessage *ioExtension, BView **outView)
{
	BRect bounds(0, 0, 206, 50);
	float right = 0;
	ConfigView* config;
	*outView = config = new ConfigView(bounds, "randomize configuration", B_WILL_DRAW, B_FOLLOW_NONE);
	config->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
	
	LoadPrefs(&DefaultPrefs);

	MySlider* slider;
	BRadioButton* radio, *radio1;

	config->mRandomness = slider = new MySlider(bounds, "randomness", "Randomness", "%d%%", NULL, 1, 100);
	slider->SetHashMarks(B_HASH_MARKS_BOTTOM);
	slider->SetHashMarkCount(3);
	slider->SetLimitLabels("1", "100");
	slider->ResizeToPreferred();
	config->AddChild(slider);
	right = max(right, slider->Frame().right);

	bounds.OffsetBy(0, slider->Bounds().Height());
	config->mRepeats = slider = new MySlider(bounds, "repeats", "Repeats", "%d", NULL, 1, 100);
	slider->SetHashMarks(B_HASH_MARKS_BOTTOM);
	slider->SetHashMarkCount(3);
	slider->SetLimitLabels("1", "100");
	slider->ResizeToPreferred();
	config->AddChild(slider);
	right = max(right, slider->Frame().right);

	bounds.OffsetBy(0, slider->Bounds().Height());
	config->AddChild(config->mType[0] = radio1 = radio = new BRadioButton(bounds, "hurl", "Hurl", new BMessage('OPER')));
	radio->ResizeToPreferred();
	right = max(right, radio->Frame().right);
	bounds.OffsetBy(0, radio->Bounds().Height());
	config->AddChild(config->mType[1] = radio = new BRadioButton(bounds, "pick", "Pick", new BMessage('OPER')));
	radio->ResizeToPreferred();
	right = max(right, radio->Frame().right);
	bounds.OffsetBy(0, radio->Bounds().Height());
	config->AddChild(config->mType[2] = radio = new BRadioButton(bounds, "slur", "Slur", new BMessage('OPER')));
	radio->ResizeToPreferred();
	right = max(right, radio->Frame().right);


	config->ResizeTo(right, radio->Frame().bottom);

	if (ioExtension)
	{
		BRect r;
		GetSettings(ioExtension, &DefaultPrefs, &r, &config->mPreviewInvoker);

		config->mRandomness->mPreviewInvoker = config->mPreviewInvoker;
		config->mRepeats->mPreviewInvoker = config->mPreviewInvoker;

		bounds.OffsetTo(config->Bounds().Width() / 2, radio1->Frame().top);
		config->AddChild(config->mLivePreview = new BCheckBox(bounds, "preview", "Preview", new BMessage('PRVW')));
		config->mLivePreview->SetValue((int32) DefaultPrefs.preview);
	}
	config->mRandomness->SetValue(DefaultPrefs.randomness);
	config->mRepeats->SetValue((int32) DefaultPrefs.repeats);
	config->mType[DefaultPrefs.type]->SetValue(1);
	
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
		case 'OPER':
			for (int x = 0; x < 3; x++)
				if (mType[x]->Value())
					DefaultPrefs.type = x;
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


void MySlider::ValueChanged()
{
	const char* name = Name();
	if (!strcmp("randomness", name))
		DefaultPrefs.randomness = Value();
	else if (!strcmp("repeats", name))
		DefaultPrefs.repeats = Value();

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

	PREFSetData(data, "randomness", &DefaultPrefs.randomness, sizeof(float), B_FLOAT_TYPE);
	PREFSetData(data, "repeats", &DefaultPrefs.repeats, sizeof(int32), B_INT32_TYPE);
	PREFSetData(data, "type", &DefaultPrefs.type, sizeof(int32), B_INT32_TYPE);


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
		// Not guaranteed to be byte aligned.
		if (!PREFGetData(data, "randomness", &p, &size, &type))
			memcpy(&thePrefs->randomness, p, sizeof(float));
		if (!PREFGetData(data, "repeats", &p, &size, &type))
			memcpy(&thePrefs->repeats, p, sizeof(int32));
		if (!PREFGetData(data, "type", &p, &size, &type))
			memcpy(&thePrefs->type, p, sizeof(int32));

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
	if (!ioExtension->FindBool(PLUGIN_PREFIX "/preview", &abool))
		thePrefs->preview = abool;

	float amount;
	if (!ioExtension->FindFloat(PLUGIN_PREFIX "/randomness", &amount))
		thePrefs->randomness = amount;
	int32 i32;
	if (!ioExtension->FindInt32(PLUGIN_PREFIX "/repeats", &i32))
		thePrefs->repeats = i32;
	if (!ioExtension->FindInt32(PLUGIN_PREFIX "/type", &i32))
		thePrefs->type = i32;

	BRect r;
	if (!ioExtension->FindRect("selection_rect", &r))
		*selection = r;

	if (progress)
		ioExtension->FindPointer("progress_invoker", (void**) progress);
	if (preview)
		ioExtension->FindPointer("preview_invoker", (void**) preview);
}

