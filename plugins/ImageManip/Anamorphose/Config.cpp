#include <stdio.h>
#include <algobase.h>
#include <MenuField.h>
#include <MenuItem.h>

#define P_IMPEXP	// Have to do this before including libprefs.h
#include "libprefs.h"
#include "Config.h"
#include "ImageManipAddon.h"
#include "TSlider.h"

#define PLUGIN_PREFIX "anamorphose"

Prefs DefaultPrefs = {
	100.0, 60.0,
	true, false, true, true, false
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

	ioExtension->AddDouble(PLUGIN_PREFIX "/cone_radius", DefaultPrefs.cone_radius);
	ioExtension->AddDouble(PLUGIN_PREFIX "/base_angle", DefaultPrefs.base_angle);
	ioExtension->AddInt32(PLUGIN_PREFIX "/surrounding", (int) DefaultPrefs.use_bkgr + (int) DefaultPrefs.set_transparent * 2);
	ioExtension->AddBool(PLUGIN_PREFIX "/use_antialias", DefaultPrefs.use_antialias);
	ioExtension->AddBool(PLUGIN_PREFIX "/flip", DefaultPrefs.flip);
	ioExtension->AddBool(PLUGIN_PREFIX "/dedouble", DefaultPrefs.dedouble);
	ioExtension->AddBool(PLUGIN_PREFIX "/preview", DefaultPrefs.preview);

	return B_OK;
}


status_t MakeConfig(BMessage *ioExtension, BView **outView)
{
	BRect bounds(0, 0, 250, 40);
	ConfigView* config;
	*outView = config = new ConfigView(bounds, PLUGIN_PREFIX " configuration", B_WILL_DRAW, B_FOLLOW_NONE);
	config->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
	
	LoadPrefs(&DefaultPrefs);

	int32 maxwidth = 500;
	BRect r;
	if (!ioExtension->FindRect("selection_rect", &r))
		maxwidth = (int32) min(r.Width(), r.Height()) + 1;

	TSlider* slider;
	slider = config->mConeRadius = new MySlider(bounds, "cone_radius", "Cone Radius", "%.1f", NULL, 1, 10 * maxwidth, 0.1);
	slider->SetHashMarks(B_HASH_MARKS_BOTTOM);
	slider->SetHashMarkCount(2);
	char str[32];
	sprintf(str, "%d.0", maxwidth);
	slider->SetLimitLabels("0.1", str);
	slider->ResizeToPreferred();
	bounds = slider->Frame();
	config->AddChild(slider);

	bounds.OffsetBy(0, bounds.Height());
	slider = config->mBaseAngle = new MySlider(bounds, "base_angle", "Base Angle", " %.1f°", NULL, 0, 900, 0.1);
	slider->SetHashMarks(B_HASH_MARKS_BOTTOM);
	slider->SetHashMarkCount(5);
	slider->SetLimitLabels("0", "90");
	slider->ResizeToPreferred();
	bounds = slider->Frame();
	config->AddChild(slider);

	BCheckBox* checkbox;

	
	bounds.OffsetBy(0, bounds.Height() + 5);
	checkbox = config->mFlip = new BCheckBox(bounds, PLUGIN_PREFIX "/flip", "Flip image vertically", new BMessage('FLIP'));
	checkbox->ResizeToPreferred();
	bounds = checkbox->Frame();
	config->AddChild(checkbox);

	bounds.OffsetBy(config->Bounds().Width() / 2, 0);
	checkbox = config->mUseAntialias = new BCheckBox(bounds, PLUGIN_PREFIX "/use_antialias", "Use antialias", new BMessage('ANTI'));
	checkbox->ResizeToPreferred();
	bounds = checkbox->Frame();
	config->AddChild(checkbox);

	bounds.OffsetBy(-bounds.left, bounds.Height() + 5);
	checkbox = config->mDeDouble = new BCheckBox(bounds, PLUGIN_PREFIX "/dedouble", "Double reflection", new BMessage('DOUB'));
	checkbox->ResizeToPreferred();
	bounds = checkbox->Frame();
	config->AddChild(checkbox);

	BMenuField* popup;
	BMenu* menu;
	BMenuItem* item;
	font_height fh;
	BFont font;
	config->GetFont(&font);
	font.GetHeight(&fh);

	bounds.OffsetBy(0, bounds.Height() + 5);
	bounds.right = config->Bounds().right;
	bounds.bottom = bounds.top + fh.ascent + fh.descent + 9;
	menu = new BMenu("");
	menu->SetRadioMode(true);
	menu->SetLabelFromMarked(true);
	item = new BMenuItem("Keep original", new BMessage('KEEP'));
	menu->AddItem(item);
	item = new BMenuItem("Set to background color", new BMessage('BKGR'));
	menu->AddItem(item);
	item = new BMenuItem("Make transparent", new BMessage('TRNS'));
	menu->AddItem(item);
	popup = config->mSurroundings = new BMenuField(bounds, "surroudings", "Surroundings", menu);
	popup->SetDivider(popup->StringWidth(popup->Label()) + 8);
	config->AddChild(popup);
	

	config->ResizeTo(slider->Frame().right, bounds.bottom);

	if (ioExtension)
	{
		BRect r;
		GetSettings(ioExtension, &DefaultPrefs, &r, &config->mPreviewInvoker);

		config->mConeRadius->mPreviewInvoker = config->mBaseAngle->mPreviewInvoker = config->mPreviewInvoker;

		bounds = config->mDeDouble->Frame();
		bounds.OffsetTo(config->Bounds().Width() / 2, bounds.top);
		config->AddChild(config->mLivePreview = new BCheckBox(bounds, PLUGIN_PREFIX "/preview", "Preview", new BMessage('PRVW')));
		config->mLivePreview->SetValue(DefaultPrefs.preview);
	}

	config->mSurroundings->Menu()->ItemAt((int) DefaultPrefs.use_bkgr + (int) DefaultPrefs.set_transparent * 2)->SetMarked(true);
	
	config->mUseAntialias->SetValue(DefaultPrefs.use_antialias);
	config->mFlip->SetValue(DefaultPrefs.flip);
	config->mDeDouble->SetValue(DefaultPrefs.dedouble);
	
	config->mConeRadius->SetValue((int32)(DefaultPrefs.cone_radius * 10));
	config->mBaseAngle->SetValue((int32)(DefaultPrefs.base_angle * 10));
	
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

		case 'FLIP':
			DefaultPrefs.flip = mFlip->Value();
			break;
		case 'ANTI':
			DefaultPrefs.use_antialias = mUseAntialias->Value();
			break;
		case 'DOUB':
			DefaultPrefs.dedouble = mDeDouble->Value();
			break;
		
		case 'KEEP':
		case 'BKGR':
		case 'TRNS':
			DefaultPrefs.keep_surr = DefaultPrefs.use_bkgr = DefaultPrefs.set_transparent = false;
			switch(msg->what)
			{
				case 'KEEP':
					DefaultPrefs.keep_surr = true;
					break;
				case 'BKGR':
					DefaultPrefs.use_bkgr = true;
					break;
				case 'TRNS':
					DefaultPrefs.set_transparent = true;
					break;
				default:
					break;
			}
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
	if (!strcmp("cone_radius", name))
		DefaultPrefs.cone_radius = Value() / 10.0;
	if (!strcmp("base_angle", name))
		DefaultPrefs.base_angle = Value() / 10.0;

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
	PREFSetData(data, "cone_radius", &DefaultPrefs.cone_radius, sizeof(double), B_DOUBLE_TYPE);
	PREFSetData(data, "base_angle", &DefaultPrefs.base_angle, sizeof(double), B_DOUBLE_TYPE);
	int32 surrounding = (int) DefaultPrefs.use_bkgr + (int) DefaultPrefs.set_transparent * 2;
	PREFSetData(data, "surrounding", &surrounding, sizeof(int32), B_INT32_TYPE);
	PREFSetData(data, "use_antialias", &DefaultPrefs.use_antialias, sizeof(bool), B_BOOL_TYPE);
	PREFSetData(data, "flip", &DefaultPrefs.flip, sizeof(bool), B_BOOL_TYPE);
	PREFSetData(data, "dedouble", &DefaultPrefs.dedouble, sizeof(bool), B_BOOL_TYPE);
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
		int32 surrounding;
		
		if (!PREFGetData(data, "cone_radius", &p, &size, &type))
			memcpy(&thePrefs->cone_radius, p, sizeof(double)); // Not guaranteed to be word aligned.
		if (!PREFGetData(data, "base_angle", &p, &size, &type))
			memcpy(&thePrefs->base_angle, p, sizeof(double)); // Not guaranteed to be word aligned.

		if (!PREFGetData(data, "surrounding", &p, &size, &type))
		{
			memcpy(&surrounding, p, sizeof(double)); // Not guaranteed to be word aligned.
			DefaultPrefs.keep_surr = DefaultPrefs.use_bkgr = DefaultPrefs.set_transparent = false;
			switch(surrounding)
			{
				case 0:
					DefaultPrefs.keep_surr = true;
					break;
				case 1:
					DefaultPrefs.use_bkgr = true;
					break;
				case 2:
					DefaultPrefs.set_transparent = true;
					break;
				default:
					break;
			}
		}

		if (!PREFGetData(data, "use_antialias", &p, &size, &type))
			thePrefs->use_antialias = *(bool*) p;
		if (!PREFGetData(data, "flip", &p, &size, &type))
			thePrefs->flip = *(bool*) p;
		if (!PREFGetData(data, "dedouble", &p, &size, &type))
			thePrefs->dedouble = *(bool*) p;

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
	double adouble;
	int32 anint;
	if (!ioExtension->FindBool(PLUGIN_PREFIX "/preview", &abool))
		thePrefs->preview = abool;
	if (!ioExtension->FindBool(PLUGIN_PREFIX "/use_antialias", &abool))
		thePrefs->use_antialias = abool;
	if (!ioExtension->FindBool(PLUGIN_PREFIX "/flip", &abool))
		thePrefs->flip = abool;
	if (!ioExtension->FindBool(PLUGIN_PREFIX "/dedouble", &abool))
		thePrefs->dedouble = abool;

	if (!ioExtension->FindInt32(PLUGIN_PREFIX "/surrounding", &anint))
	{
		thePrefs->keep_surr = thePrefs->use_bkgr = thePrefs->set_transparent = false;
		switch(anint)
		{
			case 0:
				thePrefs->keep_surr = true;
				break;
			case 1:
				thePrefs->use_bkgr = true;
				break;
			case 2:
				thePrefs->set_transparent = true;
				break;
			default:
				break;
		}
	}

	if (!ioExtension->FindDouble(PLUGIN_PREFIX "/cone_radius", &adouble))
		thePrefs->cone_radius = adouble;
	if (!ioExtension->FindDouble(PLUGIN_PREFIX "/base_angle", &adouble))
		thePrefs->base_angle = adouble;

	BRect r;
	if (!ioExtension->FindRect("selection_rect", &r))
		*selection = r;

	if (progress)
		ioExtension->FindPointer("progress_invoker", (void**) progress);
	if (preview)
		ioExtension->FindPointer("preview_invoker", (void**) preview);
}

