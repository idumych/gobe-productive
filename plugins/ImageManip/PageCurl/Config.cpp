#include <stdio.h>
#include <Box.h>
#include <Window.h>
#include <string.h>

#define P_IMPEXP	// Have to do this before including libprefs.h
#include "libprefs.h"
#include "Config.h"
#include "ImageManipAddon.h"

#define PLUGIN_PREFIX "pagecurl"

Prefs DefaultPrefs = {true, 0.75, true, false, 1, 3, false};

const char *RadioLabels[5][4] = {
	{"Foreground Color", "Background Color"},
	{"Vertical", "Horizontal"},
	{"Foreground Color", "Background Color", "Copy image from original", "Transparent"},
	{"Upper Left", "Upper Right", "Lower Left", "Lower Right"}
};

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

	ioExtension->AddDouble(PLUGIN_PREFIX "/opacity", DefaultPrefs.opacity);
	ioExtension->AddInt32(PLUGIN_PREFIX "/background", DefaultPrefs.background);
	ioExtension->AddInt32(PLUGIN_PREFIX "/location", DefaultPrefs.location);
	ioExtension->AddBool(PLUGIN_PREFIX "/preview", DefaultPrefs.preview);
	ioExtension->AddBool(PLUGIN_PREFIX "/bgcolor", DefaultPrefs.bgcolor);
	ioExtension->AddBool(PLUGIN_PREFIX "/horizontal", DefaultPrefs.horizontal);
	ioExtension->AddBool(PLUGIN_PREFIX "/shadeunder", DefaultPrefs.shadeunder);
	return B_OK;
}


status_t MakeConfig(BMessage *ioExtension, BView **outView)
{
	BRect bounds(0, 0, 160, 50);
	ConfigView* config;
	*outView = config = new ConfigView(bounds, "pagecurl configuration", B_WILL_DRAW, B_FOLLOW_NONE);
	config->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
	
	LoadPrefs(&DefaultPrefs);

	bounds.bottom = bounds.top + 15;

	BBox* box = new BBox(bounds, "box");
	box->SetLabel("Curl Settings");
	
	bounds = box->Bounds();
	bounds.InsetBy(8, 0);
	bounds.top = bounds.bottom + 5;
	bounds.bottom = bounds.top + 20;

	BBox* box2 = new BBox(bounds, "box2");
	box2->SetLabel("Shading Color");
	for (int x = 0; x < 2; x++)
	{
		box2->ResizeBy(0, 20);
		bounds = box2->Bounds();
		bounds.top = bounds.bottom - 20;
		bounds.InsetBy(8, 0);
		box2->AddChild(config->mShadingColor[x] = new BRadioButton(bounds, RadioLabels[0][x], RadioLabels[0][x], new BMessage('SHDC')));
	}
	box2->ResizeBy(0, 2);

	box->ResizeBy(0, box2->Bounds().Height());
	box->AddChild(box2);
	
	
	bounds = box->Bounds();
	bounds.InsetBy(8, 0);
	bounds.top = bounds.bottom + 15;
	bounds.bottom = bounds.top + 50;

	TSlider* slider;
	config->mOpacity = slider = new TSlider(bounds, "opacity", "Opacity", "%.1f%%", NULL, 0, 1000);
	slider->SetHashMarks(B_HASH_MARKS_BOTTOM);
	slider->SetHashMarkCount(5);
	slider->SetLimitLabels("0", "100");
	slider->ResizeToPreferred();
	box->ResizeBy(0, slider->Bounds().Height() + 15);
	box->AddChild(slider);
	
	box->ResizeBy(0, 35);
	bounds = box->Bounds();
	bounds.InsetBy(8, 0);
	bounds.bottom -= 5;
	bounds.top = bounds.bottom - 20;
	box->AddChild(config->mShading = new BCheckBox(bounds, PLUGIN_PREFIX "/shade", "Shade Under Curl", new BMessage('SHAD')));

	bounds = box->Bounds();
	config->ResizeTo(bounds.Width(), bounds.Height() + 10);
	config->AddChild(box);
	
	bounds.top = bounds.bottom + 10;
	bounds.bottom = bounds.top + 20;
	box = new BBox(bounds, "box3");
	box->SetLabel("Orientation");

	for (int x = 0; x < 2; x++)
	{
		box->ResizeBy(0, 20);
		bounds = box->Bounds();
		bounds.top = bounds.bottom - 20;
		bounds.InsetBy(8, 0);
		box->AddChild(config->mOrientation[x] = new BRadioButton(bounds, RadioLabels[1][x], RadioLabels[1][x], new BMessage('ORNT')));
	}
	box->ResizeBy(0, 2);
	
	config->ResizeBy(0, box->Bounds().Height() + 10);
	config->AddChild(box);

	bounds = box->Bounds();
	bounds.OffsetBy(bounds.Width() + 10, 0);
	
	bounds.bottom = bounds.top + 20;
	
	box = new BBox(bounds, "box4");
	box->SetLabel("Curl Background");
	for (int x = 0; x < 4; x++)
	{
		box->ResizeBy(0, 20);
		bounds = box->Bounds();
		bounds.top = bounds.bottom - 20;
		bounds.InsetBy(8, 0);
		box->AddChild(config->mBgColor[x] = new BRadioButton(bounds, RadioLabels[2][x], RadioLabels[2][x], new BMessage('BACK')));
	}
	box->ResizeBy(0, 2);
	
	config->ResizeBy(bounds.Width() + 25, 0);
	config->AddChild(box);
	
	bounds = box->Frame();
	bounds.top = bounds.bottom + 10;
	bounds.bottom = bounds.top + 20;
	box = new BBox(bounds, "box5");
	box->SetLabel("Location");
	for (int x = 0; x < 4; x++)
	{
		box->ResizeBy(0, 20);
		bounds = box->Bounds();
		bounds.top = bounds.bottom - 20;
		bounds.InsetBy(8, 0);
		box->AddChild(config->mLocation[x] = new BRadioButton(bounds, RadioLabels[3][x], RadioLabels[3][x], new BMessage('LOCA')));
	}
	box->ResizeBy(0, 2);
	config->AddChild(box);
	

	if (ioExtension)
	{
		PrefsFromMessage(&DefaultPrefs, ioExtension);

		ioExtension->FindPointer("preview_invoker", (void**) &config->mPreviewInvoker);
		config->mOpacity->mPreviewInvoker = config->mPreviewInvoker;

		bounds = box->Frame();
		bounds.top = bounds.bottom + 10;
		bounds.bottom = bounds.top + 20;
		config->AddChild(config->mLivePreview = new BCheckBox(bounds, PLUGIN_PREFIX "/preview", "Preview", new BMessage('PRVW')));
		config->mLivePreview->SetValue(DefaultPrefs.preview);
	}

	config->mOpacity->SetValue(DefaultPrefs.opacity * 1000);
	config->mOrientation[DefaultPrefs.horizontal]->SetValue(1);
	config->mBgColor[DefaultPrefs.background]->SetValue(1);
	config->mShadingColor[DefaultPrefs.bgcolor]->SetValue(1);
	config->mLocation[DefaultPrefs.location]->SetValue(1);
	config->mShading->SetValue(DefaultPrefs.shadeunder);
	
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
		case 'PRVW':
			DefaultPrefs.preview = mLivePreview->Value();
			break;
		case 'SHDC':
			for (int x = 0; x < 2; x++)
			{
				if (mShadingColor[x]->Value())
					DefaultPrefs.bgcolor = x;
			}
			break;
		case 'SHAD':
			DefaultPrefs.shadeunder = mShading->Value();
			break;
		case 'ORNT':
			for (int x = 0; x < 2; x++)
			{
				if (mOrientation[x]->Value())
					DefaultPrefs.horizontal = x;
			}
			break;
		case 'BACK':
			for (int x = 0; x < 4; x++)
			{
				if (mBgColor[x]->Value())
					DefaultPrefs.background = x;
			}
			break;
		case 'LOCA':
			for (int x = 0; x < 4; x++)
			{
				if (mLocation[x]->Value())
					DefaultPrefs.location = x;
			}
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
			DefaultPrefs.opacity = value / 1000.0;

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
	sprintf(buf, mFormat.String(), Value() / 10.0);
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
	PREFSetData(data, "preview", &DefaultPrefs.preview, sizeof(bool), B_BOOL_TYPE);
	PREFSetData(data, "bgcolor", &DefaultPrefs.bgcolor, sizeof(bool), B_BOOL_TYPE);
	PREFSetData(data, "horizontal", &DefaultPrefs.horizontal, sizeof(bool), B_BOOL_TYPE);
	PREFSetData(data, "shadeunder", &DefaultPrefs.shadeunder, sizeof(bool), B_BOOL_TYPE);

	PREFSetData(data, "opacity", &DefaultPrefs.opacity, sizeof(double), B_DOUBLE_TYPE);
	PREFSetData(data, "background", &DefaultPrefs.background, sizeof(int32), B_INT32_TYPE);
	PREFSetData(data, "location", &DefaultPrefs.location, sizeof(int32), B_INT32_TYPE);
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
		if (!PREFGetData(data, "preview", &p, &size, &type))
			thePrefs->preview = *(bool*) p;
		if (!PREFGetData(data, "bgcolor", &p, &size, &type))
			thePrefs->bgcolor = *(bool*) p;
		if (!PREFGetData(data, "horizontal", &p, &size, &type))
			thePrefs->horizontal = *(bool*) p;
		if (!PREFGetData(data, "shadeunder", &p, &size, &type))
			thePrefs->shadeunder = *(bool*) p;

		if (!PREFGetData(data, "opacity", &p, &size, &type))
			memcpy(&thePrefs->opacity, p, sizeof(double)); // Not guaranteed to be byte aligned.
		if (!PREFGetData(data, "background", &p, &size, &type))
			memcpy(&thePrefs->background, p, sizeof(int32)); // Not guaranteed to be byte aligned.
		if (!PREFGetData(data, "location", &p, &size, &type))
			memcpy(&thePrefs->location, p, sizeof(int32)); // Not guaranteed to be byte aligned.
		PREFDisposeSet(&data);
	}
	
	PREFShutdown(prefs);
}

void PrefsFromMessage(Prefs* thePrefs, BMessage* msg)
{
	bool b;
	
	if (!msg)
		return;
	
	if (!msg->FindBool(PLUGIN_PREFIX "/preview", &b))
		thePrefs->preview = b;

	int32 amount;
	double d;
	if (!msg->FindDouble(PLUGIN_PREFIX "/opacity", &d))
		thePrefs->opacity = d;
	if (!msg->FindInt32(PLUGIN_PREFIX "/background", &amount))
		thePrefs->background = amount;
	if (!msg->FindInt32(PLUGIN_PREFIX "/location", &amount))
		thePrefs->location = amount;
	if (!msg->FindBool(PLUGIN_PREFIX "/bgcolor", &b))
		thePrefs->bgcolor = b;
	if (!msg->FindBool(PLUGIN_PREFIX "/horizontal", &b))
		thePrefs->horizontal = b;
	if (!msg->FindBool(PLUGIN_PREFIX "/shadeunder", &b))
		thePrefs->shadeunder = b;

}
