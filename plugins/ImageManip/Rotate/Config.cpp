#include <stdio.h>

#define P_IMPEXP	// Have to do this before including libprefs.h
#include "libprefs.h"
#include "Config.h"
#include "ImageManipAddon.h"

#define PLUGIN_PREFIX "rotate"


Prefs DefaultPrefs = {90.0, false};

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
//	ioCapability->AddInt32("selection_rect", B_RECT_TYPE);
//	ioCapability->AddInt32("progress_invoker", B_POINTER_TYPE);
//	ioCapability->AddInt32("preview_invoker", B_POINTER_TYPE);

	ioExtension->AddDouble(PLUGIN_PREFIX "/angle", DefaultPrefs.angle);
	ioExtension->AddBool(PLUGIN_PREFIX "/sharpen", DefaultPrefs.sharpen);
	return B_OK;
}


status_t MakeConfig(BMessage *ioExtension, BView **outView)
{
	BRect bounds(0, 0, 360, 40);
	ConfigView* config;
	*outView = config = new ConfigView(bounds, "rotate configuration", B_WILL_DRAW, B_FOLLOW_NONE);
	config->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
	
	LoadPrefs(&DefaultPrefs);

	if (ioExtension)
	{
		double angle;
		if (!ioExtension->FindDouble(PLUGIN_PREFIX "/angle", &angle))
			DefaultPrefs.angle = angle;
		bool sharpen;
		if (!ioExtension->FindBool(PLUGIN_PREFIX "/sharpen", &sharpen))
			DefaultPrefs.sharpen = sharpen;

	}

	TSlider* slider;
	config->mAngle = slider = new TSlider(bounds, "angle", "Angle", "%d°", NULL, -360 , 360);
	slider->SetHashMarks(B_HASH_MARKS_BOTTOM);
	slider->SetHashMarkCount(9);
	slider->SetLimitLabels("-360°", "360°");
	slider->ResizeToPreferred();
	slider->SetValue(DefaultPrefs.angle);

	config->ResizeTo(slider->Frame().right, slider->Frame().bottom);
	config->AddChild(slider);

	config->ResizeBy(0, 25);
	bounds = config->Bounds();
	bounds.top = bounds.bottom - 20;
	config->AddChild(config->mSharpen = new BCheckBox(bounds, PLUGIN_PREFIX "/sharpen", "Sharpen", new BMessage('SHRP')));
	config->mSharpen->SetValue(DefaultPrefs.sharpen);

	
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
}

void ConfigView::MessageReceived(BMessage *msg)
{
	switch(msg->what)
	{
		case 'SHRP':
			DefaultPrefs.sharpen = mSharpen->Value();
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
		DefaultPrefs.angle = value;
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
	PREFSetData(data, "angle", &DefaultPrefs.angle, sizeof(double), B_DOUBLE_TYPE);
	PREFSetData(data, "sharpen", &DefaultPrefs.sharpen, sizeof(bool), B_BOOL_TYPE);
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
			memcpy(&thePrefs->angle, p, sizeof(double)); // Not guaranteed to be byte aligned.
		if (!PREFGetData(data, "sharpen", &p, &size, &type))
			thePrefs->sharpen = *(bool*) p;
		PREFDisposeSet(&data);
	}
	
	PREFShutdown(prefs);
}

