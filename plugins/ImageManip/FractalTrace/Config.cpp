#include <stdio.h>
#include <algobase.h>
#include <MenuField.h>
#include <MenuItem.h>

#define P_IMPEXP	// Have to do this before including libprefs.h
#include "libprefs.h"
#include "Config.h"
#include "ImageManipAddon.h"
#include "TSlider.h"

#define PLUGIN_PREFIX "fractal_trace"

Prefs DefaultPrefs = {
	-1.5, +0.5, -1.3, +1.3,
	3,
	OUTSIDE_TYPE_WRAP
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
	ioCapability->AddInt32("progress_invoker", B_POINTER_TYPE);
	ioCapability->AddInt32("preview_invoker", B_POINTER_TYPE);
	ioCapability->AddInt32("cancel_queue", B_POINTER_TYPE);

	ioExtension->AddDouble(PLUGIN_PREFIX "/x1", DefaultPrefs.x1);
	ioExtension->AddDouble(PLUGIN_PREFIX "/y1", DefaultPrefs.y1);
	ioExtension->AddDouble(PLUGIN_PREFIX "/x2", DefaultPrefs.x2);
	ioExtension->AddDouble(PLUGIN_PREFIX "/y2", DefaultPrefs.y2);
	ioExtension->AddInt32(PLUGIN_PREFIX "/depth", DefaultPrefs.depth);
	ioExtension->AddInt32(PLUGIN_PREFIX "/outside_type", DefaultPrefs.outside_type);
	
	ioExtension->AddBool(PLUGIN_PREFIX "/preview", DefaultPrefs.preview);

	return B_OK;
}


status_t MakeConfig(BMessage *ioExtension, BView **outView)
{
	BRect bounds(0, 0, 200, 40);
	ConfigView* config;
	*outView = config = new ConfigView(bounds, PLUGIN_PREFIX " configuration", B_WILL_DRAW, B_FOLLOW_NONE);
	config->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
	
	LoadPrefs(&DefaultPrefs);

	TSlider* slider;
	slider = config->mX1 = new MySlider(bounds, "x1", "X1", "%0.2f", NULL, -300, 0, 0.01);
	slider->SetHashMarks(B_HASH_MARKS_BOTTOM);
	slider->SetHashMarkCount(3);
	slider->SetLimitLabels("-3.0", "0.0");
	slider->ResizeToPreferred();
	bounds = slider->Frame();
	config->AddChild(slider);

	bounds.OffsetBy(0, bounds.Height());
	slider = config->mX2 = new MySlider(bounds, "x2", "X2", "%0.2f", NULL, 0, 200, 0.01);
	slider->SetHashMarks(B_HASH_MARKS_BOTTOM);
	slider->SetHashMarkCount(3);
	slider->SetLimitLabels("0.0", "2.0");
	slider->ResizeToPreferred();
	bounds = slider->Frame();
	config->AddChild(slider);

	bounds.OffsetBy(0, bounds.Height());
	slider = config->mY1 = new MySlider(bounds, "y1", "Y1", "%0.2f", NULL, -200, 0, 0.01);
	slider->SetHashMarks(B_HASH_MARKS_BOTTOM);
	slider->SetHashMarkCount(3);
	slider->SetLimitLabels("-2.0", "0.0");
	slider->ResizeToPreferred();
	bounds = slider->Frame();
	config->AddChild(slider);

	bounds.OffsetBy(0, bounds.Height());
	slider = config->mY2 = new MySlider(bounds, "y2", "Y2", "%0.2f", NULL, 0, 200, 0.01);
	slider->SetHashMarks(B_HASH_MARKS_BOTTOM);
	slider->SetHashMarkCount(3);
	slider->SetLimitLabels("0.0", "2.0");
	slider->ResizeToPreferred();
	bounds = slider->Frame();
	config->AddChild(slider);

	bounds.OffsetBy(0, bounds.Height());
	slider = config->mDepth = new MySlider(bounds, "depth", "Depth (iterations)", "%d", NULL, 1, 100);
	slider->SetHashMarks(B_HASH_MARKS_BOTTOM);
	slider->SetHashMarkCount(5);
	slider->SetLimitLabels("1", "100");
	slider->ResizeToPreferred();
	bounds = slider->Frame();
	config->AddChild(slider);

	bounds.OffsetBy(0, bounds.Height());
	BMenu* menu = new BMenu("");
	menu->SetRadioMode(true);
	menu->SetLabelFromMarked(true);
	BMenuItem* item = new BMenuItem("Wrap", new BMessage('WRAP'));
	menu->AddItem(item);
	item = new BMenuItem("Transparent", new BMessage('TRNS'));
	menu->AddItem(item);
	item = new BMenuItem("Foreground Color", new BMessage('FORE'));
	menu->AddItem(item);
	item = new BMenuItem("Background Color", new BMessage('BACK'));
	menu->AddItem(item);
	menu->ItemAt(DefaultPrefs.outside_type)->SetMarked(true);
	BMenuField* popup = new BMenuField(bounds, "outside_type", "Outside Type", menu);
	popup->SetAlignment(B_ALIGN_RIGHT);
	popup->SetDivider(popup->StringWidth(popup->Label()) + 13);
	// WORTHLESS popup->ResizeToPreferred();
	//popup->ResizeTo(0, fh.ascent + fh.descent + 9);
	config->AddChild(popup);
	

	config->ResizeTo(popup->Frame().right, popup->Frame().bottom);

	if (ioExtension)
	{
		BRect r;
		GetSettings(ioExtension, &DefaultPrefs, &r, &config->mPreviewInvoker);

		config->mX1->mPreviewInvoker =
			config->mX2->mPreviewInvoker =
			config->mY1->mPreviewInvoker =
			config->mY2->mPreviewInvoker =
			config->mDepth->mPreviewInvoker =
			config->mPreviewInvoker;

		config->ResizeBy(0, 25);
		bounds = config->Bounds();
		bounds.top = bounds.bottom - 20;
		config->AddChild(config->mLivePreview = new BCheckBox(bounds, PLUGIN_PREFIX "/preview", "Preview", new BMessage('PRVW')));
		config->mLivePreview->SetValue(DefaultPrefs.preview);
	}

	config->mX1->SetValue(DefaultPrefs.x1);
	config->mX2->SetValue(DefaultPrefs.x2);
	config->mY1->SetValue(DefaultPrefs.y1);
	config->mY2->SetValue(DefaultPrefs.y2);
	config->mDepth->SetValue(DefaultPrefs.depth);
	
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
		
		case 'WRAP':
			DefaultPrefs.outside_type = OUTSIDE_TYPE_WRAP;
			break;
		case 'TRNS':
			DefaultPrefs.outside_type = OUTSIDE_TYPE_TRANSPARENT;
			break;
		case 'FORE':
			DefaultPrefs.outside_type = OUTSIDE_TYPE_FORE;
			break;
		case 'BACK':
			DefaultPrefs.outside_type = OUTSIDE_TYPE_BACK;
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
	if (!strcmp("x1", name))
		DefaultPrefs.x1 = Value();
	if (!strcmp("x2", name))
		DefaultPrefs.x2 = Value();
	if (!strcmp("y1", name))
		DefaultPrefs.y1 = Value();
	if (!strcmp("y2", name))
		DefaultPrefs.y2 = Value();
	if (!strcmp("depth", name))
		DefaultPrefs.depth = TSlider::Value();

	if (DefaultPrefs.preview)
		TSlider::ValueChanged();
}

double MySlider::Value()
{
	double d = TSlider::Value() * mScale + mOffset;
	
	return d;
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

	PREFSetData(data, "x1", &DefaultPrefs.x1, sizeof(double), B_DOUBLE_TYPE);
	PREFSetData(data, "x2", &DefaultPrefs.x2, sizeof(double), B_DOUBLE_TYPE);
	PREFSetData(data, "y1", &DefaultPrefs.y1, sizeof(double), B_DOUBLE_TYPE);
	PREFSetData(data, "y2", &DefaultPrefs.y2, sizeof(double), B_DOUBLE_TYPE);

	PREFSetData(data, "depth", &DefaultPrefs.depth, sizeof(int32), B_INT32_TYPE);
	PREFSetData(data, "outside_type", &DefaultPrefs.outside_type, sizeof(int32), B_INT32_TYPE);

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
		
		// Not guaranteed to be word aligned.
		if (!PREFGetData(data, "x1", &p, &size, &type))
			memcpy(&thePrefs->x1, p, sizeof(double));
		if (!PREFGetData(data, "x2", &p, &size, &type))
			memcpy(&thePrefs->x2, p, sizeof(double));
		if (!PREFGetData(data, "y1", &p, &size, &type))
			memcpy(&thePrefs->y1, p, sizeof(double));
		if (!PREFGetData(data, "y2", &p, &size, &type))
			memcpy(&thePrefs->y2, p, sizeof(double));

		if (!PREFGetData(data, "depth", &p, &size, &type))
			memcpy(&thePrefs->depth, p, sizeof(int32));
		if (!PREFGetData(data, "outside_type", &p, &size, &type))
			memcpy(&thePrefs->outside_type, p, sizeof(int32));


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


	double d;
	if (!ioExtension->FindDouble(PLUGIN_PREFIX "/x1", &d))
		thePrefs->x1 = d;
	if (!ioExtension->FindDouble(PLUGIN_PREFIX "/x2", &d))
		thePrefs->x2 = d;
	if (!ioExtension->FindDouble(PLUGIN_PREFIX "/y1", &d))
		thePrefs->y1 = d;
	if (!ioExtension->FindDouble(PLUGIN_PREFIX "/y2", &d))
		thePrefs->y2 = d;

	int32 i;
	if (!ioExtension->FindInt32(PLUGIN_PREFIX "/depth", &i))
		thePrefs->depth = i;
	if (!ioExtension->FindInt32(PLUGIN_PREFIX "/outside_type", &i))
		thePrefs->outside_type = i;

	BRect r;
	if (!ioExtension->FindRect("selection_rect", &r))
		*selection = r;

	if (progress)
		ioExtension->FindPointer("progress_invoker", (void**) progress);
	if (preview)
		ioExtension->FindPointer("preview_invoker", (void**) preview);
}

