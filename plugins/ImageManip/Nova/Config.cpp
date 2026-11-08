#include <stdio.h>
#include <Window.h>
#include <string.h>

#define P_IMPEXP	// Have to do this before including libprefs.h
#include "libprefs.h"
#include "Config.h"
#include "ImageManipAddon.h"

Prefs DefaultPrefs = {90, 100, 255, 20, 100, 0, 0, 0, false};
#define MIN_LABEL "0"
#define MAX_LABEL "255"
#define MAXVALUE 255
#define THUMB_FORMAT_STR "%d"

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

	ioExtension->AddInt32("nova/red", DefaultPrefs.red);
	ioExtension->AddInt32("nova/green", DefaultPrefs.green);
	ioExtension->AddInt32("nova/blue", DefaultPrefs.blue);

	ioExtension->AddInt32("nova/xcenter", DefaultPrefs.xcenter);
	ioExtension->AddInt32("nova/ycenter", DefaultPrefs.ycenter);

	ioExtension->AddInt32("nova/random_hue", DefaultPrefs.hue);
	ioExtension->AddInt32("nova/spokes", DefaultPrefs.spokes);
	ioExtension->AddInt32("nova/radius", DefaultPrefs.radius);

	ioExtension->AddBool("nova/preview", DefaultPrefs.preview);
	return B_OK;
}


status_t MakeConfig(BMessage *ioExtension, BView **outView)
{
	BRect bounds(0, 0, 206, 50);
	ConfigView* config;
	*outView = config = new ConfigView(bounds, "nova configuration", B_WILL_DRAW, B_FOLLOW_NONE);
	config->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
	
	LoadPrefs(&DefaultPrefs);

	TSlider* slider;

	config->mX = slider = new TSlider(bounds, "x", "X", THUMB_FORMAT_STR, NULL, -500, 500);
	slider->SetHashMarks(B_HASH_MARKS_BOTTOM);
	slider->SetHashMarkCount(3);
	slider->SetLimitLabels("-500", "500");
	slider->ResizeToPreferred();
	config->AddChild(slider);

	bounds.OffsetBy(0, bounds.Height());
	config->mY = slider = new TSlider(bounds, "y", "Y", THUMB_FORMAT_STR, NULL, -500, 500);
	slider->SetHashMarks(B_HASH_MARKS_BOTTOM);
	slider->SetHashMarkCount(3);
	slider->SetLimitLabels("-500", "500");
	slider->ResizeToPreferred();
	config->AddChild(slider);

	bounds.OffsetBy(0, bounds.Height());
	config->mRadius = slider = new TSlider(bounds, "radius", "Radius", THUMB_FORMAT_STR, NULL, 1, 100);
	slider->SetHashMarks(B_HASH_MARKS_BOTTOM);
	slider->SetHashMarkCount(3);
	slider->SetLimitLabels("1", "100");
	slider->ResizeToPreferred();
	config->AddChild(slider);

	bounds.OffsetBy(0, bounds.Height());
	config->mSpokes = slider = new TSlider(bounds, "spokes", "Spokes", THUMB_FORMAT_STR, NULL, 1, 1024);
	slider->SetHashMarks(B_HASH_MARKS_BOTTOM);
	slider->SetHashMarkCount(3);
	slider->SetLimitLabels("1", "1024");
	slider->ResizeToPreferred();
	config->AddChild(slider);

	bounds.OffsetTo(bounds.Width() + 10, 0);
	config->mRed = slider = new TSlider(bounds, "red", "Red", THUMB_FORMAT_STR, NULL, 0, MAXVALUE);
	slider->SetHashMarks(B_HASH_MARKS_BOTTOM);
	slider->SetHashMarkCount(3);
	slider->SetLimitLabels(MIN_LABEL, MAX_LABEL);
	slider->ResizeToPreferred();
	config->AddChild(slider);

	bounds.OffsetBy(0, bounds.Height());
	config->mGreen = slider = new TSlider(bounds, "green", "Green", THUMB_FORMAT_STR, NULL, 0, MAXVALUE);
	slider->SetHashMarks(B_HASH_MARKS_BOTTOM);
	slider->SetHashMarkCount(3);
	slider->SetLimitLabels(MIN_LABEL, MAX_LABEL);
	slider->ResizeToPreferred();
	config->AddChild(slider);

	bounds.OffsetBy(0, bounds.Height());
	config->mBlue = slider = new TSlider(bounds, "blue", "Blue", THUMB_FORMAT_STR, NULL, 0, MAXVALUE);
	slider->SetHashMarks(B_HASH_MARKS_BOTTOM);
	slider->SetHashMarkCount(3);
	slider->SetLimitLabels(MIN_LABEL, MAX_LABEL);
	slider->ResizeToPreferred();
	config->AddChild(slider);

	bounds.OffsetBy(0, bounds.Height());
	config->mHue = slider = new TSlider(bounds, "hue", "Random Hue", THUMB_FORMAT_STR, NULL, 0, 360);
	slider->SetHashMarks(B_HASH_MARKS_BOTTOM);
	slider->SetHashMarkCount(3);
	slider->SetLimitLabels("0", "360");
	slider->ResizeToPreferred();
	config->AddChild(slider);

	config->ResizeTo(slider->Frame().right, slider->Frame().bottom);

	if (ioExtension)
	{
		bool preview;
		if (!ioExtension->FindBool("nova/preview", &preview))
			DefaultPrefs.preview = preview;

		int32 amount;
		if (!ioExtension->FindInt32("nova/red", &amount))
			DefaultPrefs.red = amount;
		if (!ioExtension->FindInt32("nova/green", &amount))
			DefaultPrefs.green = amount;
		if (!ioExtension->FindInt32("nova/blue", &amount))
			DefaultPrefs.blue = amount;

		if (!ioExtension->FindInt32("nova/xcenter", &amount))
			DefaultPrefs.xcenter = amount;
		if (!ioExtension->FindInt32("nova/ycenter", &amount))
			DefaultPrefs.ycenter = amount;

		if (!ioExtension->FindInt32("nova/random_hue", &amount))
			DefaultPrefs.hue = amount;
		if (!ioExtension->FindInt32("nova/spokes", &amount))
			DefaultPrefs.spokes = amount;
		if (!ioExtension->FindInt32("nova/radius", &amount))
			DefaultPrefs.radius = amount;


		ioExtension->FindPointer("preview_invoker", (void**) &config->mPreviewInvoker);
		config->mX->mPreviewInvoker = config->mPreviewInvoker;
		config->mY->mPreviewInvoker = config->mPreviewInvoker;
		config->mRed->mPreviewInvoker = config->mPreviewInvoker;
		config->mGreen->mPreviewInvoker = config->mPreviewInvoker;
		config->mBlue->mPreviewInvoker = config->mPreviewInvoker;
		config->mRadius->mPreviewInvoker = config->mPreviewInvoker;
		config->mSpokes->mPreviewInvoker = config->mPreviewInvoker;
		config->mHue->mPreviewInvoker = config->mPreviewInvoker;

		config->ResizeBy(0, 25);
		bounds = config->Bounds();
		bounds.top = bounds.bottom - 20;
		config->AddChild(config->mLivePreview = new BCheckBox(bounds, "nova/preview", "Preview", new BMessage('PRVW')));
		config->mLivePreview->SetValue(DefaultPrefs.preview);
	}
	config->mX->SetValue(DefaultPrefs.xcenter);
	config->mY->SetValue(DefaultPrefs.ycenter);
	config->mRed->SetValue(DefaultPrefs.red);
	config->mGreen->SetValue(DefaultPrefs.green);
	config->mBlue->SetValue(DefaultPrefs.blue);
	config->mRadius->SetValue(DefaultPrefs.radius);
	config->mSpokes->SetValue(DefaultPrefs.spokes);
	config->mHue->SetValue(DefaultPrefs.hue);
	
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
			const char* name = Name();
			if (!strcmp("red", name))
				DefaultPrefs.red = value;
			else if (!strcmp("green", name))
				DefaultPrefs.green = value;
			else if (!strcmp("blue", name))
				DefaultPrefs.blue = value;
			else if (!strcmp("radius", name))
				DefaultPrefs.blue = value;
			else if (!strcmp("spokes", name))
				DefaultPrefs.spokes = value;
			else if (!strcmp("hue", name))
				DefaultPrefs.hue = value;
			else if (!strcmp("x", name))
				DefaultPrefs.xcenter = value;
			else if (!strcmp("y", name))
				DefaultPrefs.ycenter = value;

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

	PREFLoadSet(prefs, "nova", true, &data);

	PREFSetData(data, "red", &DefaultPrefs.red, sizeof(uchar), B_UINT8_TYPE);
	PREFSetData(data, "green", &DefaultPrefs.green, sizeof(uchar), B_UINT8_TYPE);
	PREFSetData(data, "blue", &DefaultPrefs.blue, sizeof(uchar), B_UINT8_TYPE);

	PREFSetData(data, "xcenter", &DefaultPrefs.xcenter, sizeof(int32), B_INT32_TYPE);
	PREFSetData(data, "ycenter", &DefaultPrefs.ycenter, sizeof(int32), B_INT32_TYPE);

	PREFSetData(data, "random_hue", &DefaultPrefs.hue, sizeof(int32), B_INT32_TYPE);
	PREFSetData(data, "spokes", &DefaultPrefs.spokes, sizeof(int32), B_INT32_TYPE);
	PREFSetData(data, "radius", &DefaultPrefs.radius, sizeof(int32), B_INT32_TYPE);


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
	
	if (!PREFLoadSet(prefs, "nova", true, &data))
	{
		const void* p;
		ssize_t size;
		uint32 type;
		// Not guaranteed to be byte aligned.
		if (!PREFGetData(data, "xcenter", &p, &size, &type))
			memcpy(&thePrefs->xcenter, p, sizeof(int32));
		if (!PREFGetData(data, "ycenter", &p, &size, &type))
			memcpy(&thePrefs->ycenter, p, sizeof(int32));

		if (!PREFGetData(data, "random_hue", &p, &size, &type))
			memcpy(&thePrefs->hue, p, sizeof(int32));
		if (!PREFGetData(data, "spokes", &p, &size, &type))
			memcpy(&thePrefs->spokes, p, sizeof(int32));
		if (!PREFGetData(data, "radius", &p, &size, &type))
			memcpy(&thePrefs->radius, p, sizeof(int32));


		if (!PREFGetData(data, "red", &p, &size, &type))
			thePrefs->red = *(uchar*) p;
		if (!PREFGetData(data, "green", &p, &size, &type))
			thePrefs->green = *(uchar*) p;
		if (!PREFGetData(data, "blue", &p, &size, &type))
			thePrefs->blue = *(uchar*) p;


		if (!PREFGetData(data, "preview", &p, &size, &type))
			thePrefs->preview = *(bool*) p;
		PREFDisposeSet(&data);
	}
	
	PREFShutdown(prefs);
}

