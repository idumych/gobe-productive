#include <stdio.h>
#include <Window.h>
#include <string.h>

#define P_IMPEXP	// Have to do this before including libprefs.h
#include "libprefs.h"
#include "Config.h"
#include "ImageManipAddon.h"

#define PLUGIN_PREFIX "rothue"

Prefs DefaultPrefs = {90};

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

	ioExtension->AddInt32(PLUGIN_PREFIX "/angle", DefaultPrefs.angle);
	ioExtension->AddBool(PLUGIN_PREFIX "/preview", DefaultPrefs.preview);
	return B_OK;
}


status_t MakeConfig(BMessage *ioExtension, BView **outView)
{
	BRect bounds(0, 0, 160, 40);
	ConfigView* config;
	*outView = config = new ConfigView(bounds, "rothue configuration", B_WILL_DRAW, B_FOLLOW_NONE);
	config->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
	
	LoadPrefs(&DefaultPrefs);

	TSlider* slider;
	config->mAmount = slider = new TSlider(bounds, "angle", "Angle", "%d°", NULL, 0, 360);
	slider->SetHashMarks(B_HASH_MARKS_BOTTOM);
	slider->SetHashMarkCount(5);
	slider->SetLimitLabels("0", "360");
	slider->ResizeToPreferred();


	config->ResizeTo(slider->Frame().right, slider->Frame().bottom);
	config->AddChild(slider);

	if (ioExtension)
	{
		bool preview;
		if (!ioExtension->FindBool(PLUGIN_PREFIX "/preview", &preview))
			DefaultPrefs.preview = preview;

		int32 amount;
		if (!ioExtension->FindInt32(PLUGIN_PREFIX "/amount", &amount))
			DefaultPrefs.angle = amount;

		ioExtension->FindPointer("preview_invoker", (void**) &config->mPreviewInvoker);
		slider->mPreviewInvoker = config->mPreviewInvoker;

		config->ResizeBy(0, 25);
		bounds = config->Bounds();
		bounds.top = bounds.bottom - 20;
		config->AddChild(config->mLivePreview = new BCheckBox(bounds, PLUGIN_PREFIX "/preview", "Preview", new BMessage('PRVW')));
		config->mLivePreview->SetValue(DefaultPrefs.preview);
	}
	slider->SetValue(DefaultPrefs.angle);
	
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
			DefaultPrefs.angle = value;
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
	PREFSetData(data, "angle", &DefaultPrefs.angle, sizeof(int32), B_INT32_TYPE);
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
		if (!PREFGetData(data, "angle", &p, &size, &type))
			memcpy(&thePrefs->angle, p, sizeof(int32)); // Not guaranteed to be byte aligned.
		if (!PREFGetData(data, "preview", &p, &size, &type))
			thePrefs->preview = *(bool*) p;
		PREFDisposeSet(&data);
	}
	
	PREFShutdown(prefs);
}

