#include <stdio.h>
#include <algobase.h>
#include <MenuItem.h>

#define P_IMPEXP	// Have to do this before including libprefs.h
#include "libprefs.h"
#include "Config.h"
#include "ImageManipAddon.h"

#define PLUGIN_PREFIX "Wind"

Prefs DefaultPrefs = {
	10,
	LEFT,
	10,
	RENDER_WIND,
	LEADING
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

	ioExtension->AddInt32(PLUGIN_PREFIX "/threshold", DefaultPrefs.threshold);
	ioExtension->AddInt32(PLUGIN_PREFIX "/direction", DefaultPrefs.direction);
	ioExtension->AddInt32(PLUGIN_PREFIX "/strength", DefaultPrefs.strength);
	ioExtension->AddInt32(PLUGIN_PREFIX "/type", DefaultPrefs.type);
	ioExtension->AddInt32(PLUGIN_PREFIX "/edge", DefaultPrefs.edge);
	ioExtension->AddBool(PLUGIN_PREFIX "/preview", DefaultPrefs.preview);

	return B_OK;
}


status_t MakeConfig(BMessage *ioExtension, BView **outView)
{
	BRect bounds(0, 0, 234, 40);
	ConfigView* config;
	*outView = config = new ConfigView(bounds, PLUGIN_PREFIX " configuration", B_WILL_DRAW, B_FOLLOW_TOP | B_FOLLOW_LEFT);
	config->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
	
	LoadPrefs(&DefaultPrefs);

	TSlider* slider = config->mThreshold = new MySlider(bounds, "threshold", "Threshold", "%d", NULL, MIN_THRESHOLD, MAX_THRESHOLD);
	slider->SetHashMarks(B_HASH_MARKS_BOTTOM);
	slider->SetHashMarkCount(6);
	slider->SetLimitLabels("Low", "High");
	slider->ResizeToPreferred();
	bounds = slider->Frame();
	config->AddChild(slider);

	bounds.OffsetBy(0, bounds.Height());
	slider = config->mStrength = new MySlider(bounds, "strength", "Strength", "%d", NULL, MIN_STRENGTH, MAX_STRENGTH, 1.0, -1.0);
	slider->SetHashMarks(B_HASH_MARKS_BOTTOM);
	slider->SetHashMarkCount(6);
	slider->SetLimitLabels("Minimum", "Maximum");
	slider->ResizeToPreferred();
	bounds = slider->Frame();
	config->AddChild(slider);

	BMenuField* popup;
	BMenu* menu;
	BMenuItem* item;
	font_height fh;
	BFont font;
	config->GetFont(&font);
	font.GetHeight(&fh);

	bounds.OffsetBy(0, bounds.Height());
	menu = new BMenu("");
	menu->SetRadioMode(true);
	menu->SetLabelFromMarked(true);
	item = new BMenuItem("Wind", new BMessage('WIND'));
	menu->AddItem(item);
	item = new BMenuItem("Blast", new BMessage('BLST'));
	menu->AddItem(item);
	popup = config->mType = new BMenuField(bounds, "type", "Type", menu);
	popup->SetAlignment(B_ALIGN_RIGHT);
	popup->SetDivider(popup->StringWidth(popup->Label()) + 13);
	// WORTHLESS popup->ResizeToPreferred();
	popup->ResizeTo(0, fh.ascent + fh.descent + 9);
	config->AddChild(popup);

	bounds.OffsetBy(config->Bounds().Width() / 2, 0);
	menu = new BMenu("");
	menu->SetRadioMode(true);
	menu->SetLabelFromMarked(true);
	item = new BMenuItem("Left", new BMessage('LEFT'));
	menu->AddItem(item);
	item = new BMenuItem("Right", new BMessage('RGHT'));
	menu->AddItem(item);
	popup = config->mDirection = new BMenuField(bounds, "direction", "Direction", menu);
	popup->SetAlignment(B_ALIGN_RIGHT);
	popup->SetDivider(popup->StringWidth(popup->Label()) + 13);
	// WORTHLESS popup->ResizeToPreferred();
	popup->ResizeTo(0, fh.ascent + fh.descent + 9);
	config->AddChild(popup);

	bounds.OffsetBy(-bounds.left, popup->Bounds().Height() + 5);
	menu = new BMenu("");
	menu->SetRadioMode(true);
	menu->SetLabelFromMarked(true);
	item = new BMenuItem("Both", new BMessage('BOTH'));
	menu->AddItem(item);
	item = new BMenuItem("Leading", new BMessage('LEAD'));
	menu->AddItem(item);
	item = new BMenuItem("Trailing", new BMessage('TRAL'));
	menu->AddItem(item);
	popup = config->mEdge = new BMenuField(bounds, "edge", "Edge", menu);
	popup->SetAlignment(B_ALIGN_RIGHT);
	popup->SetDivider(popup->StringWidth(popup->Label()) + 13);
	// WORTHLESS popup->ResizeToPreferred();
	popup->ResizeTo(0, fh.ascent + fh.descent + 9);
	config->AddChild(popup);

	config->ResizeTo(slider->Frame().right, popup->Frame().bottom + 5);

	if (ioExtension)
	{
		BRect r;
		GetSettings(ioExtension, &DefaultPrefs, &r, &config->mPreviewInvoker);

		config->mThreshold->mPreviewInvoker = config->mPreviewInvoker;
		config->mStrength->mPreviewInvoker = config->mPreviewInvoker;

		config->ResizeBy(0, 25);
		bounds = config->Bounds();
		bounds.top = bounds.bottom - 20;
		config->AddChild(config->mLivePreview = new BCheckBox(bounds, PLUGIN_PREFIX "/preview", "Preview", new BMessage('PRVW')));
		config->mLivePreview->SetValue(DefaultPrefs.preview);
	}

	config->mType->Menu()->ItemAt(DefaultPrefs.type)->SetMarked(true);
	config->mDirection->Menu()->ItemAt(DefaultPrefs.direction)->SetMarked(true);
	config->mEdge->Menu()->ItemAt(DefaultPrefs.edge)->SetMarked(true);
	config->mThreshold->SetValue(DefaultPrefs.threshold);
	config->mStrength->SetValue(DefaultPrefs.strength);
	
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

		case 'WIND':
			DefaultPrefs.type = RENDER_WIND;
			break;
		case 'BLST':
			DefaultPrefs.type = RENDER_BLAST;
			break;
		case 'LEFT':
			DefaultPrefs.direction = LEFT;
			break;
		case 'RGHT':
			DefaultPrefs.direction = RIGHT;
			break;
		case 'BOTH':
			DefaultPrefs.edge = BOTH;
			break;
		case 'LEAD':
			DefaultPrefs.edge = LEADING;
			break;
		case 'TRAL':
			DefaultPrefs.edge = TRAILING;
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
	if (!strcmp("threshold", name))
		DefaultPrefs.threshold = Value();
	else if (!strcmp("strength", name))
		DefaultPrefs.strength = Value();

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
	PREFSetData(data, "threshold", &DefaultPrefs.threshold, sizeof(int32), B_INT32_TYPE);
	PREFSetData(data, "direction", &DefaultPrefs.direction, sizeof(int32), B_INT32_TYPE);
	PREFSetData(data, "strength", &DefaultPrefs.strength, sizeof(int32), B_INT32_TYPE);
	PREFSetData(data, "type", &DefaultPrefs.type, sizeof(int32), B_INT32_TYPE);
	PREFSetData(data, "edge", &DefaultPrefs.edge, sizeof(int32), B_INT32_TYPE);
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
		
		if (!PREFGetData(data, "threshold", &p, &size, &type))
			memcpy(&thePrefs->threshold, p, sizeof(int32)); // Not guaranteed to be word aligned.
		if (!PREFGetData(data, "direction", &p, &size, &type))
			memcpy(&thePrefs->direction, p, sizeof(int32)); // Not guaranteed to be word aligned.
		if (!PREFGetData(data, "strength", &p, &size, &type))
			memcpy(&thePrefs->strength, p, sizeof(int32)); // Not guaranteed to be word aligned.
		if (!PREFGetData(data, "type", &p, &size, &type))
			memcpy(&thePrefs->type, p, sizeof(int32)); // Not guaranteed to be word aligned.
		if (!PREFGetData(data, "edge", &p, &size, &type))
			memcpy(&thePrefs->edge, p, sizeof(int32)); // Not guaranteed to be word aligned.

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

	int32 anint;
	if (!ioExtension->FindInt32(PLUGIN_PREFIX "/threshold", &anint))
		thePrefs->threshold = anint;
	if (!ioExtension->FindInt32(PLUGIN_PREFIX "/direction", &anint))
		thePrefs->direction = anint;
	if (!ioExtension->FindInt32(PLUGIN_PREFIX "/strength", &anint))
		thePrefs->strength = anint;
	if (!ioExtension->FindInt32(PLUGIN_PREFIX "/type", &anint))
		thePrefs->type = anint;
	if (!ioExtension->FindInt32(PLUGIN_PREFIX "/edge", &anint))
		thePrefs->edge = anint;

	BRect r;
	if (!ioExtension->FindRect("selection_rect", &r))
		*selection = r;

	if (progress)
		ioExtension->FindPointer("progress_invoker", (void**) progress);
	if (preview)
		ioExtension->FindPointer("preview_invoker", (void**) preview);
}

