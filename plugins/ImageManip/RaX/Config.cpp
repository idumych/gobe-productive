#include <stdio.h>
#include <algobase.h>
#include <MenuField.h>

#define P_IMPEXP	// Have to do this before including libprefs.h
#include "libprefs.h"
#include "Config.h"
#include "ImageManipAddon.h"
#include "TSlider.h"

#define PLUGIN_PREFIX "rax"

Prefs DefaultPrefs = {1.0, 1.0, 1.0, 1.0, 10.0, 10.0};

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

	ioExtension->AddDouble(PLUGIN_PREFIX "/a", DefaultPrefs.a);
	ioExtension->AddDouble(PLUGIN_PREFIX "/b", DefaultPrefs.b);
	ioExtension->AddDouble(PLUGIN_PREFIX "/c", DefaultPrefs.c);
	ioExtension->AddDouble(PLUGIN_PREFIX "/d", DefaultPrefs.d);
	ioExtension->AddDouble(PLUGIN_PREFIX "/e", DefaultPrefs.e);
	ioExtension->AddDouble(PLUGIN_PREFIX "/f", DefaultPrefs.f);
	ioExtension->AddBool(PLUGIN_PREFIX "/preview", DefaultPrefs.preview);

	return B_OK;
}


status_t MakeConfig(BMessage *ioExtension, BView **outView)
{
	BRect bounds(0, 0, 294, 40);
	ConfigView* config;
	*outView = config = new ConfigView(bounds, PLUGIN_PREFIX " configuration", B_WILL_DRAW, B_FOLLOW_NONE);
	config->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
	
	LoadPrefs(&DefaultPrefs);

	// PUT STUFF HERE
	TSlider* slider;
	slider = config->aSlider = new MySlider(bounds, "a", "A", "%.1f", NULL, -500, 500, 0.1);
	slider->SetHashMarks(B_HASH_MARKS_BOTTOM);
	slider->SetHashMarkCount(11);
	slider->SetLimitLabels("-50", "50");
	slider->ResizeToPreferred();
	bounds = slider->Frame();
	config->AddChild(slider);

	bounds.OffsetBy(0, bounds.Height());
	slider = config->bSlider = new MySlider(bounds, "b", "B", "%.1f", NULL, -500, 500, 0.1);
	slider->SetHashMarks(B_HASH_MARKS_BOTTOM);
	slider->SetHashMarkCount(11);
	slider->SetLimitLabels("-50", "50");
	slider->ResizeToPreferred();
	bounds = slider->Frame();
	config->AddChild(slider);

	bounds.OffsetBy(0, bounds.Height());
	slider = config->cSlider = new MySlider(bounds, "c", "C", "%.1f", NULL, -500, 500, 0.1);
	slider->SetHashMarks(B_HASH_MARKS_BOTTOM);
	slider->SetHashMarkCount(11);
	slider->SetLimitLabels("-50", "50");
	slider->ResizeToPreferred();
	bounds = slider->Frame();
	config->AddChild(slider);

	bounds.OffsetBy(0, bounds.Height());
	slider = config->dSlider = new MySlider(bounds, "d", "d", "%.1f", NULL, -500, 500, 0.1);
	slider->SetHashMarks(B_HASH_MARKS_BOTTOM);
	slider->SetHashMarkCount(11);
	slider->SetLimitLabels("-50", "50");
	slider->ResizeToPreferred();
	bounds = slider->Frame();
	config->AddChild(slider);

	bounds.OffsetBy(0, bounds.Height());
	slider = config->eSlider = new MySlider(bounds, "e", "E", "%.1f", NULL, -500, 500, 0.1);
	slider->SetHashMarks(B_HASH_MARKS_BOTTOM);
	slider->SetHashMarkCount(11);
	slider->SetLimitLabels("-50", "50");
	slider->ResizeToPreferred();
	bounds = slider->Frame();
	config->AddChild(slider);

	bounds.OffsetBy(0, bounds.Height());
	slider = config->fSlider = new MySlider(bounds, "f", "F", "%.1f", NULL, -500, 500, 0.1);
	slider->SetHashMarks(B_HASH_MARKS_BOTTOM);
	slider->SetHashMarkCount(11);
	slider->SetLimitLabels("-50", "50");
	slider->ResizeToPreferred();
	bounds = slider->Frame();
	config->AddChild(slider);

	config->ResizeTo(slider->Frame().right, slider->Frame().bottom);

	if (ioExtension)
	{
		BRect r;
		GetSettings(ioExtension, &DefaultPrefs, &r, &config->mPreviewInvoker);

		config->aSlider->mPreviewInvoker =
		config->bSlider->mPreviewInvoker =
		config->cSlider->mPreviewInvoker =
		config->dSlider->mPreviewInvoker =
		config->eSlider->mPreviewInvoker =
		config->fSlider->mPreviewInvoker =
			config->mPreviewInvoker;

		config->ResizeBy(0, 25);
		bounds = config->Bounds();
		bounds.top = bounds.bottom - 20;
		config->AddChild(config->mLivePreview = new BCheckBox(bounds, PLUGIN_PREFIX "/preview", "Preview", new BMessage('PRVW')));
		config->mLivePreview->SetValue(DefaultPrefs.preview);
	}

	config->aSlider->SetValue((int32)(DefaultPrefs.a * 10.0));
	config->bSlider->SetValue((int32)(DefaultPrefs.b * 10.0));
	config->cSlider->SetValue((int32)(DefaultPrefs.c * 10.0));
	config->dSlider->SetValue((int32)(DefaultPrefs.d * 10.0));
	config->eSlider->SetValue((int32)(DefaultPrefs.e * 10.0));
	config->fSlider->SetValue((int32)(DefaultPrefs.f * 10.0));
	
	return B_OK;
}

void ConfigView::AttachedToWindow()
{
	for (int x = 0; x < CountChildren(); x++)
	{
		BInvoker* invoker = dynamic_cast<BInvoker*>(ChildAt(x));
		if (invoker)
			invoker->SetTarget(this);

		BMenuField* popup = dynamic_cast<BMenuField*>(ChildAt(x));
		if (popup)
			popup->Menu()->SetTargetForItems(this);
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
	if (!strcmp("a", name))
		DefaultPrefs.a = Value() / 10.0;
	if (!strcmp("b", name))
		DefaultPrefs.b = Value() / 10.0;
	if (!strcmp("c", name))
		DefaultPrefs.c = Value() / 10.0;
	if (!strcmp("d", name))
		DefaultPrefs.d = Value() / 10.0;
	if (!strcmp("e", name))
		DefaultPrefs.e = Value() / 10.0;
	if (!strcmp("f", name))
		DefaultPrefs.f = Value() / 10.0;

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
	PREFSetData(data, "a", &DefaultPrefs.a, sizeof(double), B_DOUBLE_TYPE);
	PREFSetData(data, "b", &DefaultPrefs.b, sizeof(double), B_DOUBLE_TYPE);
	PREFSetData(data, "c", &DefaultPrefs.c, sizeof(double), B_DOUBLE_TYPE);
	PREFSetData(data, "d", &DefaultPrefs.d, sizeof(double), B_DOUBLE_TYPE);
	PREFSetData(data, "e", &DefaultPrefs.e, sizeof(double), B_DOUBLE_TYPE);
	PREFSetData(data, "f", &DefaultPrefs.f, sizeof(double), B_DOUBLE_TYPE);
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
		
		if (!PREFGetData(data, "a", &p, &size, &type))
			memcpy(&thePrefs->a, p, sizeof(double)); // Not guaranteed to be word aligned.
		if (!PREFGetData(data, "b", &p, &size, &type))
			memcpy(&thePrefs->b, p, sizeof(double)); // Not guaranteed to be word aligned.
		if (!PREFGetData(data, "c", &p, &size, &type))
			memcpy(&thePrefs->c, p, sizeof(double)); // Not guaranteed to be word aligned.
		if (!PREFGetData(data, "d", &p, &size, &type))
			memcpy(&thePrefs->d, p, sizeof(double)); // Not guaranteed to be word aligned.
		if (!PREFGetData(data, "e", &p, &size, &type))
			memcpy(&thePrefs->e, p, sizeof(double)); // Not guaranteed to be word aligned.
		if (!PREFGetData(data, "f", &p, &size, &type))
			memcpy(&thePrefs->f, p, sizeof(double)); // Not guaranteed to be word aligned.

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

	// Have to use temp variable, because FindXXX helpfully zeroes out your
	// parameter if the data isn't found.
	bool abool;
	if (!ioExtension->FindBool(PLUGIN_PREFIX "/preview", &abool))
		thePrefs->preview = abool;

	double adouble;
	if (!ioExtension->FindDouble(PLUGIN_PREFIX "/a", &adouble))
		thePrefs->a = adouble;
	if (!ioExtension->FindDouble(PLUGIN_PREFIX "/b", &adouble))
		thePrefs->b = adouble;
	if (!ioExtension->FindDouble(PLUGIN_PREFIX "/c", &adouble))
		thePrefs->c = adouble;
	if (!ioExtension->FindDouble(PLUGIN_PREFIX "/d", &adouble))
		thePrefs->d = adouble;
	if (!ioExtension->FindDouble(PLUGIN_PREFIX "/e", &adouble))
		thePrefs->e = adouble;
	if (!ioExtension->FindDouble(PLUGIN_PREFIX "/f", &adouble))
		thePrefs->f = adouble;

	BRect r;
	if (!ioExtension->FindRect("selection_rect", &r))
		*selection = r;

	if (progress)
		ioExtension->FindPointer("progress_invoker", (void**) progress);
	if (preview)
		ioExtension->FindPointer("preview_invoker", (void**) preview);
}

