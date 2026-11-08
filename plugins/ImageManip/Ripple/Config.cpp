#include <stdio.h>
#include <Box.h>
#include <Window.h>
#include <string.h>

#define P_IMPEXP	// Have to do this before including libprefs.h
#include "libprefs.h"
#include "Config.h"
#include "ImageManipAddon.h"

#define PLUGIN_PREFIX "ripple"

Prefs DefaultPrefs = {0, 8, 5};

const char *Labels[3] = {"Sine", "Triangle", "Random"};

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

	ioExtension->AddInt32(PLUGIN_PREFIX "/type", DefaultPrefs.type);
	ioExtension->AddInt32(PLUGIN_PREFIX "/period", DefaultPrefs.period);
	ioExtension->AddInt32(PLUGIN_PREFIX "/amplitude", DefaultPrefs.amplitude);
	ioExtension->AddBool(PLUGIN_PREFIX "/preview", DefaultPrefs.preview);
	return B_OK;
}


status_t MakeConfig(BMessage *ioExtension, BView **outView)
{
	BRect bounds(0, 0, 160, 50);
	ConfigView* config;
	*outView = config = new ConfigView(bounds, "ripple configuration", B_WILL_DRAW, B_FOLLOW_NONE);
	config->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
	
	LoadPrefs(&DefaultPrefs);

	TSlider* slider;
	config->mAmplitude = slider = new TSlider(bounds, "amplitude", "Amplitude", "%d", NULL, 1, 100);
	slider->SetHashMarks(B_HASH_MARKS_BOTTOM);
	slider->SetHashMarkCount(5);
	slider->SetLimitLabels("1", "100");
	slider->ResizeToPreferred();
	config->AddChild(slider);

	bounds.OffsetBy(0, bounds.Height());

	config->mPeriod = slider = new TSlider(bounds, "period", "Period", "%d", NULL, 1, 200);
	slider->SetHashMarks(B_HASH_MARKS_BOTTOM);
	slider->SetHashMarkCount(5);
	slider->SetLimitLabels("1", "200");
	slider->ResizeToPreferred();
	config->ResizeTo(slider->Frame().right, slider->Frame().bottom + 5);
	config->AddChild(slider);

	bounds = config->Bounds();
	bounds.top = bounds.bottom;
	bounds.bottom = bounds.top + 15;
	bounds.right -= 8;
	BBox* box = new BBox(bounds, "box");
	box->SetLabel("Wave Type");
	for (int x = 0; x < 3; x++)
	{
		box->ResizeBy(0, 20);
		bounds = box->Bounds();
		bounds.top = bounds.bottom - 20;
		bounds.InsetBy(8, 0);
		box->AddChild(config->mType[x] = new BRadioButton(bounds, Labels[x], Labels[x], new BMessage('TYPE')));
	}

	box->ResizeBy(0, 2);
	config->ResizeBy(0, 5 + box->Bounds().Height());
	config->AddChild(box);

	if (ioExtension)
	{
		bool preview;
		if (!ioExtension->FindBool(PLUGIN_PREFIX "/preview", &preview))
			DefaultPrefs.preview = preview;

		int32 amount;
		if (!ioExtension->FindInt32(PLUGIN_PREFIX "/type", &amount))
			DefaultPrefs.type = amount;
		if (!ioExtension->FindInt32(PLUGIN_PREFIX "/period", &amount))
			DefaultPrefs.period = amount;
		if (!ioExtension->FindInt32(PLUGIN_PREFIX "/amplitude", &amount))
			DefaultPrefs.amplitude = amount;

		ioExtension->FindPointer("preview_invoker", (void**) &config->mPreviewInvoker);
		config->mAmplitude->mPreviewInvoker = config->mPreviewInvoker;
		config->mPeriod->mPreviewInvoker = config->mPreviewInvoker;

		config->ResizeBy(0, 25);
		bounds = config->Bounds();
		bounds.top = bounds.bottom - 20;
		config->AddChild(config->mLivePreview = new BCheckBox(bounds, PLUGIN_PREFIX "/preview", "Preview", new BMessage('PRVW')));
		config->mLivePreview->SetValue(DefaultPrefs.preview);
	}

	config->mPeriod->SetValue(DefaultPrefs.period);
	config->mAmplitude->SetValue(DefaultPrefs.amplitude);
	config->mType[DefaultPrefs.type]->SetValue(1);
	
	return B_OK;
}

void ConfigView::AttachedToWindow()
{
	SetTargets(this);

	if (mPreviewInvoker && DefaultPrefs.preview)
		mPreviewInvoker->Invoke();
}

void ConfigView::SetTargets(BView* v)
{
	for (int x = 0; x < v->CountChildren(); x++)
	{
		SetTargets(v->ChildAt(x));

		BInvoker* invoker = dynamic_cast<BInvoker*>(v->ChildAt(x));
		if (invoker)
			invoker->SetTarget(this);
	}
}

void ConfigView::MessageReceived(BMessage *msg)
{
	switch(msg->what)
	{
		case 'TYPE':
			for (int x = 0; x < 3; x++)
			{
				if (mType[x]->Value())
					DefaultPrefs.type = x;
			}
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
			if (!strcmp("period", Name()))
				DefaultPrefs.period = value;
			else
				DefaultPrefs.amplitude = value;

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

void TSlider::DrawThumb()
{
	BSlider::DrawThumb();

	if (!mFormat.Length())
		return;

	font_height fh;
	BView* v = OffscreenView();
	BRect r = ThumbFrame();
	v->SetHighColor(0, 0, 0);
	v->SetFont(be_plain_font);
	be_plain_font->GetHeight(&fh);

	char buf[256];
	sprintf(buf, mFormat.String(), Value());
	float w = v->StringWidth(buf);
	float x = r.left + (r.Width() - v->StringWidth(buf)) / 2;
	if (x + w > Bounds().right)
		x = Bounds().right - w;
	if (x < Bounds().left)
		x = Bounds().left;
	
	v->DrawString(buf, BPoint(x, r.top - fh.descent - 2));
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
	PREFSetData(data, "type", &DefaultPrefs.type, sizeof(int32), B_INT32_TYPE);
	PREFSetData(data, "period", &DefaultPrefs.period, sizeof(int32), B_INT32_TYPE);
	PREFSetData(data, "amplitude", &DefaultPrefs.amplitude, sizeof(int32), B_INT32_TYPE);
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
		if (!PREFGetData(data, "type", &p, &size, &type))
			memcpy(&thePrefs->type, p, sizeof(int32)); // Not guaranteed to be byte aligned.
		if (!PREFGetData(data, "period", &p, &size, &type))
			memcpy(&thePrefs->period, p, sizeof(int32)); // Not guaranteed to be byte aligned.
		if (!PREFGetData(data, "amplitude", &p, &size, &type))
			memcpy(&thePrefs->amplitude, p, sizeof(int32)); // Not guaranteed to be byte aligned.
		if (!PREFGetData(data, "preview", &p, &size, &type))
			thePrefs->preview = *(bool*) p;
		PREFDisposeSet(&data);
	}
	
	PREFShutdown(prefs);
}

