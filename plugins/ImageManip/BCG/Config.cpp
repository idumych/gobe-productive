#include <stdio.h>
#include <math.h>
#include <TabView.h>
#include <algobase.h>
#include <Box.h>

#define P_IMPEXP	// Have to do this before including libprefs.h
#include "libprefs.h"
#include "Config.h"
#include "ImageManipAddon.h"
#include "BitmapAccessor.h"

#define PLUGIN_PREFIX "bcg"

Prefs DefaultPrefs = {1.0, 1.0, 1.0, 1.0, 0, 0, false, false, true, true};
#define MAXVALUE	2000	// divisions on the slider (makes the fractions happy)
#define EXP_RANGE	2.0	// Ranges from 10e-2 to 10e+2
#define SCALEVAL ((MAXVALUE/2.0)/EXP_RANGE)
#define THUMB_FORMAT_STR "%0.2g"
#define MIN_LABEL		"0.01"
#define MAX_LABEL		"100"

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
	ioCapability->AddInt32("config_bitmap", B_POINTER_TYPE);

	ioExtension->AddFloat(PLUGIN_PREFIX "/red", DefaultPrefs.gamma[0]);
	ioExtension->AddFloat(PLUGIN_PREFIX "/green", DefaultPrefs.gamma[1]);
	ioExtension->AddFloat(PLUGIN_PREFIX "/blue", DefaultPrefs.gamma[2]);
	ioExtension->AddFloat(PLUGIN_PREFIX "/alpha", DefaultPrefs.gamma[3]);

	ioExtension->AddInt32(PLUGIN_PREFIX "/brightness", DefaultPrefs.brightness);
	ioExtension->AddInt32(PLUGIN_PREFIX "/contrast", DefaultPrefs.contrast);

	ioExtension->AddBool(PLUGIN_PREFIX "/preview", DefaultPrefs.preview);
	ioExtension->AddBool(PLUGIN_PREFIX "/lock", DefaultPrefs.lock);
	ioExtension->AddBool(PLUGIN_PREFIX "/b_additive", DefaultPrefs.b_additive);
	ioExtension->AddBool(PLUGIN_PREFIX "/c_stretch", DefaultPrefs.c_stretch);
	return B_OK;
}


status_t MakeConfig(BMessage *ioExtension, BView **outView)
{
	BRect bounds(0, 0, 330, 50);
	ConfigView* config;
	*outView = config = new ConfigView(bounds, "gamma configuration", B_WILL_DRAW, B_FOLLOW_NONE);
	config->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
	color_space depth = B_RGBA32;
	MySlider* slider;
	
	BitmapAccessor* accessor;
	if (ioExtension && !ioExtension->FindPointer("config_bitmap", (void**) &accessor))
		depth = accessor->ColorSpace();
	
	LoadPrefs(&DefaultPrefs);


	// Brightness / Contrast

	BView* v = new BView(bounds, "Brightness / Contrast", B_FOLLOW_ALL, 0);
	BView* v2;
	v->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));

	config->mBrightness = slider = new MySlider(bounds, "brightness", "Brightness", "%d", NULL, -255, 255);
	slider->mScaled = false;
	slider->SetHashMarks(B_HASH_MARKS_BOTTOM);
	slider->SetHashMarkCount(5);
	slider->SetLimitLabels("-255", "255");
	slider->ResizeToPreferred();
	bounds = slider->Frame();
	v->AddChild(slider);

	
	bounds.OffsetBy(0, bounds.Height() + 5);
	BRect bounds2 = bounds;
	bounds2.InsetBy(5, 0);
	v2 = new BView(bounds2, "brightness_radios", B_FOLLOW_ALL, 0);
	v2->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
	v->AddChild(v2);

	
	bounds2 = v2->Bounds();
	bounds2.bottom /= 2;
	
	BRadioButton* radio[2];
	radio[1] = new BRadioButton(bounds2, "additive", "Additive", new BMessage('ADDI'));
	radio[1]->ResizeToPreferred();
	v2->AddChild(radio[1]);
	bounds2 = radio[1]->Frame();
	
	bounds2.OffsetBy(0, bounds2.Height());
	radio[0] = new BRadioButton(bounds2, "multiplicative", "Multiplicative", new BMessage('MULT'));
	radio[0]->ResizeToPreferred();
	v2->AddChild(radio[0]);
	v2->ResizeTo(radio[0]->Frame().right, radio[0]->Frame().bottom);
	
	radio[DefaultPrefs.b_additive]->SetValue(1);


	bounds.OffsetBy(0, bounds.Height() + 15);
	bounds.bottom = bounds.top + 1;
	bounds2 = bounds;
	bounds2.InsetBy(20, 0);
	v2 = new BBox(bounds2);
	v->AddChild(v2);
	


	bounds.OffsetBy(0, bounds.Height() + 15);
	bounds.bottom = bounds.top + 50;

	config->mContrast = slider = new MySlider(bounds, "contrast", "Contrast", "%d", NULL, -127, 127);
	slider->mScaled = false;
	slider->SetHashMarks(B_HASH_MARKS_BOTTOM);
	slider->SetHashMarkCount(5);
	slider->SetLimitLabels("-127", "127");
	slider->ResizeToPreferred();
	bounds = slider->Frame();
	v->AddChild(slider);


	bounds.OffsetBy(0, bounds.Height() + 5);
	bounds2 = bounds;
	bounds2.InsetBy(5, 0);
	v2 = new BView(bounds2, "contrast_radios", B_FOLLOW_ALL, 0);
	v2->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
	v->AddChild(v2);

	
	bounds2 = v2->Bounds();
	bounds2.bottom /= 2;
	
	radio[0] = new BRadioButton(bounds2, "bias", "Bias", new BMessage('BIAS'));
	radio[0]->ResizeToPreferred();
	v2->AddChild(radio[0]);
	bounds2 = radio[0]->Frame();

	bounds2.OffsetBy(0, bounds2.Height());
	radio[1] = new BRadioButton(bounds2, "stretch", "Stretch", new BMessage('STRC'));
	radio[1]->ResizeToPreferred();
	v2->AddChild(radio[1]);
	v2->ResizeTo(radio[1]->Frame().right, radio[1]->Frame().bottom);

	radio[DefaultPrefs.c_stretch]->SetValue(1);

	v->ResizeTo(slider->Frame().right + 5.0, v2->Frame().bottom + 5.0);

	BRect tabRect = v->Bounds();
	tabRect.right += 5.0;
	tabRect.bottom += 5.0;
	v->MoveTo(5.0, 5.0);

	BTabView* tabView = new BTabView(tabRect, "tabs", B_WIDTH_FROM_WIDEST);
	tabView->ResizeBy(0, tabView->TabHeight());
	tabView->ContainerView()->ResizeBy(0, tabView->TabHeight());
	tabView->AddTab(v);
	config->AddChild(tabView);


	// Gamma

	bounds.Set(0, 0, 330, 50);
	v = new BView(bounds, "Gamma", B_FOLLOW_ALL, 0);
	v->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));

	if (depth == B_GRAY8)
		config->mRed = slider = new MySlider(bounds, "red", "Greyscale", THUMB_FORMAT_STR, NULL, 0, MAXVALUE);
	else
		config->mRed = slider = new MySlider(bounds, "red", "Red", THUMB_FORMAT_STR, NULL, 0, MAXVALUE);
	slider->SetHashMarks(B_HASH_MARKS_BOTTOM);
	slider->SetHashMarkCount(4);
	slider->SetLimitLabels(MIN_LABEL, MAX_LABEL);
	slider->ResizeToPreferred();
	v->AddChild(slider);

	if (depth != B_GRAY8)
	{
		bounds.OffsetBy(0, bounds.Height() + 5);
		config->mGreen = slider = new MySlider(bounds, "green", "Green", THUMB_FORMAT_STR, NULL, 0, MAXVALUE);
		slider->SetHashMarks(B_HASH_MARKS_BOTTOM);
		slider->SetHashMarkCount(4);
		slider->SetLimitLabels(MIN_LABEL, MAX_LABEL);
		slider->ResizeToPreferred();
		v->AddChild(slider);
	
		bounds.OffsetBy(0, bounds.Height() + 5);
		config->mBlue = slider = new MySlider(bounds, "blue", "Blue", THUMB_FORMAT_STR, NULL, 0, MAXVALUE);
		slider->SetHashMarks(B_HASH_MARKS_BOTTOM);
		slider->SetHashMarkCount(4);
		slider->SetLimitLabels(MIN_LABEL, MAX_LABEL);
		slider->ResizeToPreferred();
		v->AddChild(slider);
	
		if (depth == B_RGBA32 || depth == B_RGBA32_BIG)
		{
			bounds.OffsetBy(0, bounds.Height() + 5);
			config->mAlpha = slider = new MySlider(bounds, "alpha", "Alpha", THUMB_FORMAT_STR, NULL, 0, MAXVALUE);
			slider->SetHashMarks(B_HASH_MARKS_BOTTOM);
			slider->SetHashMarkCount(4);
			slider->SetLimitLabels(MIN_LABEL, MAX_LABEL);
			slider->ResizeToPreferred();
			slider->mLockable = false; // Hack
			v->AddChild(slider);
		}
		else
			config->mAlpha = NULL;
	}
	else
		config->mAlpha = config->mGreen = config->mBlue = NULL;


	v->ResizeTo(slider->Frame().right + 5.0, slider->Frame().bottom + 5.0);

	if (depth != B_GRAY8)
	{
		v->ResizeBy(0, 25);
		bounds = v->Bounds();
		bounds.top = bounds.bottom - 20;
		v->AddChild(config->mLock = new BCheckBox(bounds, PLUGIN_PREFIX "/lock", "Lock RGB", new BMessage('LOCK')));
		config->mLock->SetValue(DefaultPrefs.lock);
	}

	v->MoveTo(5.0, 5.0);

	BPoint delta(max(v->Frame().right, tabView->ContainerView()->Bounds().right), max(v->Frame().bottom, tabView->ContainerView()->Bounds().bottom));

	delta -= tabView->ContainerView()->Bounds().RightBottom();

	tabView->ResizeBy(delta.x, delta.y);
	tabView->ContainerView()->ResizeBy(delta.x, delta.y);

	tabView->AddTab(v);
	
	config->ResizeTo(tabView->Frame().right, tabView->Frame().bottom);


	if (ioExtension)
	{
		bool preview;
		if (!ioExtension->FindBool(PLUGIN_PREFIX "/preview", &preview))
			DefaultPrefs.preview = preview;

		float amount;
		if (!ioExtension->FindFloat(PLUGIN_PREFIX "/red", &amount))
			DefaultPrefs.gamma[0] = amount;
		if (!ioExtension->FindFloat(PLUGIN_PREFIX "/green", &amount))
			DefaultPrefs.gamma[1] = amount;
		if (!ioExtension->FindFloat(PLUGIN_PREFIX "/blue", &amount))
			DefaultPrefs.gamma[2] = amount;
		if (!ioExtension->FindFloat(PLUGIN_PREFIX "/alpha", &amount))
			DefaultPrefs.gamma[3] = amount;

		ioExtension->FindPointer("preview_invoker", (void**) &config->mPreviewInvoker);

		config->ResizeBy(0, 25);
		bounds = config->Bounds();
		bounds.top = bounds.bottom - 20;
		config->AddChild(config->mLivePreview = new BCheckBox(bounds, PLUGIN_PREFIX "/preview", "Preview", new BMessage('PRVW')));
		config->mLivePreview->SetValue(DefaultPrefs.preview);
	}

	config->mBrightness->mPreviewInvoker = config->mContrast->mPreviewInvoker = config->mRed->mPreviewInvoker = config->mPreviewInvoker;
	config->mRed->mConfig = config;
	if (config->mGreen)
	{
		config->mGreen->mPreviewInvoker = config->mPreviewInvoker;
		config->mGreen->mConfig = config;
	}
	if (config->mBlue)
	{
		config->mBlue->mPreviewInvoker = config->mPreviewInvoker;
		config->mBlue->mConfig = config;
	}
	if (config->mAlpha)
		config->mAlpha->mPreviewInvoker = config->mPreviewInvoker;

	config->mRed->SetValue(DefaultPrefs.gamma[0]);
	if (config->mGreen)
		config->mGreen->SetValue(DefaultPrefs.gamma[1]);
	if (config->mBlue)
		config->mBlue->SetValue(DefaultPrefs.gamma[2]);
	if (config->mAlpha)
		config->mAlpha->SetValue(DefaultPrefs.gamma[3]);

	config->mBrightness->SetValue(DefaultPrefs.brightness);
	config->mContrast->SetValue(DefaultPrefs.contrast);
	
	return B_OK;
}

// Recursively fix up all the controls
void ConfigView::AttachedToWindow(BView* v)
{
	BInvoker* invoker = dynamic_cast<BInvoker*>(v);
	if (invoker)
	{
		invoker->SetTarget(this);
		return;
	}

	for (int x = 0; x < v->CountChildren(); x++)
	{
		BView* child = v->ChildAt(x);
		invoker = dynamic_cast<BInvoker*>(child);
		if (invoker)
			invoker->SetTarget(this);
		else
		{
			// Special hack for BTabView, since its tabs aren't in the view hierarchy
			BTabView* tab = dynamic_cast<BTabView*>(child);
			if (tab)
			{
				for (int y = 0; y < tab->CountTabs(); y++)
					AttachedToWindow(tab->TabAt(y)->View());
			}
			else
				AttachedToWindow(child);
		}
	}

}

void ConfigView::AttachedToWindow()
{
	AttachedToWindow(this);

	if (mPreviewInvoker && DefaultPrefs.preview)
		mPreviewInvoker->Invoke();

	mRed->mLocked = mGreen->mLocked = mBlue->mLocked = DefaultPrefs.lock;
}

void ConfigView::MessageReceived(BMessage *msg)
{
	switch(msg->what)
	{
		case 'LOCK':
			mRed->mLocked = mGreen->mLocked = mBlue->mLocked = DefaultPrefs.lock = mLock->Value();
			if (DefaultPrefs.lock)
			{
				// Hack to force immediate RGB lock
				int32 value = mRed->TSlider::Value();
				bool preview = DefaultPrefs.preview;
				DefaultPrefs.lock = DefaultPrefs.preview = false;
				// Make sure the value changes.
				mGreen->TSlider::SetValue(value);
				mBlue->TSlider::SetValue(value);
				DefaultPrefs.preview = preview;
				DefaultPrefs.lock = true;
			}
			break;

		case 'PRVW':
			DefaultPrefs.preview = mLivePreview->Value();
			break;
		
		case 'ADDI':
			DefaultPrefs.b_additive = true;
			break;
		case 'MULT':
			DefaultPrefs.b_additive = false;
			break;
		
		case 'STRC':
			DefaultPrefs.c_stretch = true;
			break;
		case 'BIAS':
			DefaultPrefs.c_stretch = false;
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

#if 0
static int32 ToSlider(float value)
{
	return ((int32)(SCALEVAL * log10(value) + 0.5)) + MAXVALUE / 2;
}

static float FromSlider(int32 value)
{
	return pow(10, (value - MAXVALUE / 2) / SCALEVAL);
}


void TSlider::SetValue(int32 value)
{
	// Don't invoke the preview unless the value actually changes.
	bool hasTarget = !!Target();
	bool changed = value != Value();
	int which = 0;
	float v;

	if (mLockable && DefaultPrefs.lock && !mLocked)
	{
		ConfigView* v = static_cast<ConfigView*>(Parent());
		if (v)
		{
			v->mRed->mLocked = true;
			v->mRed->SetValue(value);
			v->mRed->mLocked = false;
			
			if (v->mGreen)
			{
				v->mGreen->mLocked = v->mBlue->mLocked = true;
				v->mGreen->SetValue(value);
				v->mBlue->SetValue(value);
				v->mGreen->mLocked = v->mBlue->mLocked = false;
			}
		}
		goto invoke;
	}
	else
		BSlider::SetValue(value);
	
	if (changed && value == Value())
	{
		which = 0;
		if (!strcmp("red", Name()))
			which = 0;
		else if (!strcmp("grey", Name()))
			which = 0;
		else if (!strcmp("green", Name()))
			which = 1;
		else if (!strcmp("blue", Name()))
			which = 2;
		else if (!strcmp("alpha", Name()))
			which = 3;

		if (hasTarget)
		{
			v = FromSlider(value);

			switch(which)
			{
				case 0:
					DefaultPrefs.red = v;
					break;
				case 1:
					DefaultPrefs.green = v;
					break;
				case 2:
					DefaultPrefs.blue = v;
					break;
				case 3:
					DefaultPrefs.alpha = v;
					break;
			}

invoke:
			if (mPreviewInvoker && DefaultPrefs.preview && (!mLockable || !mLocked))
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
#endif

void MySlider::SetValue(float f)
{
	TSlider::SetValue(((int32)(SCALEVAL * log10(f) + 0.5)) + MAXVALUE / 2);
}

void MySlider::DrawHashMarks()
{
	if (!mScaled)
	{
		TSlider::DrawHashMarks();
		return;
	}
	
	BView* v = OffscreenView();
	BRect bar = BarFrame();
	BRect hash = HashMarksFrame();
	BRect r;
	BPoint p1;
	BPoint p2;
	
	
	for (int i = 0; i < HashMarkCount(); i++)
	{
		r = hash;
		r.left = bar.left + 1.0;
		r.right = bar.right - 1.0;
		float w = r.Width() / HashMarkCount();
		
		r.right = r.left + (i + 1) * w;
		r.left += i * w;
		
	
		for (int x = 1; x <= 10; x++)
		{
			float p;
			p = r.left + r.Width() * log10((double) x);

			p1.Set(p, r.top);
			p2.Set(p, r.bottom);

			if (x != 1)
				p2.y = r.top + 0.5 * r.Height();


			v->SetHighColor(148, 148, 148);
			v->StrokeLine(p1, p2);

			p1.x++;
			p2.x++;
			v->SetHighColor(255, 255, 255);
			v->StrokeLine(p1, p2);
		}
	}

	p1.Set(bar.right, hash.top);
	p2.Set(bar.right, hash.bottom);

	v->SetHighColor(255, 255, 255);
	v->StrokeLine(p1, p2);

	p1.x--;
	p2.x--;
	v->SetHighColor(148, 148, 148);
	v->StrokeLine(p1, p2);

}


void MySlider::DrawThumb()
{
	if (!mScaled)
	{
		TSlider::DrawThumb();
		return;
	}

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
	if (number.Length())
		strcpy(buf, number.String());
	else
	{
		// OK this is bogus, but there's no simple way to get the desired behavior.
		const char* fmt;
		if (Value() > 50.0 || Value() < 1.0)
			fmt = "%0.3g";
		else
			fmt = "%0.1f";
		sprintf(buf, fmt, (float) Value());
	}
	float w = v->StringWidth(buf);
	float x = 1 + rint(r.left + (r.Width() - v->StringWidth(buf)) / 2);
	if (x + w > Bounds().right)
		x = Bounds().right - w;
	if (x < Bounds().left)
		x = Bounds().left;
	
	v->DrawString(buf, BPoint(x, r.top - fh.descent - 1));

}

float MySlider::Value()
{
	int32 value = TSlider::Value();
	return pow(10, (value - MAXVALUE / 2) / SCALEVAL);
}

void MySlider::ValueChanged()
{
	const char* name = Name();
	int32 which = 0;
	
	if (!strcmp(name, "red"))
		which = 1;
	else if (!strcmp(name, "green"))
		which = 2;
	else if (!strcmp(name, "blue"))
		which = 3;
	else if (!strcmp(name, "alpha"))
		which = 4;
	else if (!strcmp(name, "brightness"))
		which = 5;
	else if (!strcmp(name, "contrast"))
		which = 6;

	if (mLocked)
	{
		bool preview = DefaultPrefs.preview;
		DefaultPrefs.preview = false;
		
		mConfig->mRed->mLocked = false;
		mConfig->mGreen->mLocked = false;
		mConfig->mBlue->mLocked = false;

		if (this != mConfig->mRed)
			mConfig->mRed->TSlider::SetValue(TSlider::Value());
		if (this != mConfig->mGreen)
			mConfig->mGreen->TSlider::SetValue(TSlider::Value());
		if (this != mConfig->mBlue)
			mConfig->mBlue->TSlider::SetValue(TSlider::Value());

		mConfig->mRed->mLocked = true;
		mConfig->mGreen->mLocked = true;
		mConfig->mBlue->mLocked = true;
		
		DefaultPrefs.preview = preview;
	}

	switch(which)
	{
		case 1:
			DefaultPrefs.gamma[0] = Value();
			break;
		case 2:
			DefaultPrefs.gamma[1] = Value();
			break;
		case 3:
			DefaultPrefs.gamma[2] = Value();
			break;
		case 4:
			DefaultPrefs.gamma[3] = Value();
			break;
		case 5:
			DefaultPrefs.brightness = TSlider::Value();
			break;
		case 6:
			DefaultPrefs.contrast = TSlider::Value();
			break;
	}

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
	PREFSetData(data, "red", &DefaultPrefs.gamma[0], sizeof(float), B_FLOAT_TYPE);
	PREFSetData(data, "green", &DefaultPrefs.gamma[1], sizeof(float), B_FLOAT_TYPE);
	PREFSetData(data, "blue", &DefaultPrefs.gamma[2], sizeof(float), B_FLOAT_TYPE);
	PREFSetData(data, "alpha", &DefaultPrefs.gamma[3], sizeof(float), B_FLOAT_TYPE);

	PREFSetData(data, "brightness", &DefaultPrefs.brightness, sizeof(int32), B_INT32_TYPE);
	PREFSetData(data, "contrast", &DefaultPrefs.contrast, sizeof(int32), B_INT32_TYPE);

	PREFSetData(data, "preview", &DefaultPrefs.preview, sizeof(bool), B_BOOL_TYPE);
	PREFSetData(data, "lock", &DefaultPrefs.lock, sizeof(bool), B_BOOL_TYPE);
	PREFSetData(data, "b_additive", &DefaultPrefs.b_additive, sizeof(bool), B_BOOL_TYPE);
	PREFSetData(data, "c_stretch", &DefaultPrefs.c_stretch, sizeof(bool), B_BOOL_TYPE);
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
		if (!PREFGetData(data, "red", &p, &size, &type))
			memcpy(&thePrefs->gamma[0], p, sizeof(float));
		if (!PREFGetData(data, "green", &p, &size, &type))
			memcpy(&thePrefs->gamma[1], p, sizeof(float));
		if (!PREFGetData(data, "blue", &p, &size, &type))
			memcpy(&thePrefs->gamma[2], p, sizeof(float));
		if (!PREFGetData(data, "alpha", &p, &size, &type))
			memcpy(&thePrefs->gamma[3], p, sizeof(float));

		if (!PREFGetData(data, "brightness", &p, &size, &type))
			memcpy(&thePrefs->brightness, p, sizeof(int32));
		if (!PREFGetData(data, "contrast", &p, &size, &type))
			memcpy(&thePrefs->contrast, p, sizeof(int32));

		if (!PREFGetData(data, "preview", &p, &size, &type))
			thePrefs->preview = *(bool*) p;
		if (!PREFGetData(data, "lock", &p, &size, &type))
			thePrefs->lock = *(bool*) p;
		if (!PREFGetData(data, "b_additive", &p, &size, &type))
			thePrefs->b_additive = *(bool*) p;
		if (!PREFGetData(data, "c_stretch", &p, &size, &type))
			thePrefs->c_stretch = *(bool*) p;
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

	// Have to use temp variable, because FindXXX helpfully zeroes out your
	// parameter if the data isn't found.
	bool abool;
	if (!ioExtension->FindBool(PLUGIN_PREFIX "/preview", &abool))
		thePrefs->preview = abool;
	if (!ioExtension->FindBool(PLUGIN_PREFIX "/b_additive", &abool))
		thePrefs->b_additive = abool;
	if (!ioExtension->FindBool(PLUGIN_PREFIX "/c_stretch", &abool))
		thePrefs->c_stretch = abool;

	float f;
	if (!ioExtension->FindFloat(PLUGIN_PREFIX "/red", &f))
		thePrefs->gamma[0] = f;
	if (!ioExtension->FindFloat(PLUGIN_PREFIX "/green", &f))
		thePrefs->gamma[1] = f;
	if (!ioExtension->FindFloat(PLUGIN_PREFIX "/blue", &f))
		thePrefs->gamma[2] = f;
	if (!ioExtension->FindFloat(PLUGIN_PREFIX "/alpha", &f))
		thePrefs->gamma[3] = f;

	int32 anint;
	if (!ioExtension->FindInt32(PLUGIN_PREFIX "/brightness", &anint))
		thePrefs->brightness = anint;
	if (!ioExtension->FindInt32(PLUGIN_PREFIX "/contrast", &anint))
		thePrefs->contrast = anint;

	BRect r;
	if (!ioExtension->FindRect("selection_rect", &r))
		*selection = r;

	if (progress)
		ioExtension->FindPointer("progress_invoker", (void**) progress);
	if (preview)
		ioExtension->FindPointer("preview_invoker", (void**) preview);
}
