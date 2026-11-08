// Set Transparency: every selected pixel given one alpha level. Gobe shipped
// this add-on without its source; rewritten for the Gobe Productive port
// from the shipped one's behaviour (by Scott Lindsey).
#include <CheckBox.h>
#include <Invoker.h>
#include <Message.h>
#include <View.h>
#include <Window.h>

#include <string.h>

#include "BitmapAccessor.h"
#include "ImageManipAddon.h"
#include "TSlider.h"
#include "libprefs.h"

#define PLUGIN_PREFIX "transparency"
#define SIGNATURE "x-vnd.wombat.plugins"

const char addonName[] = "Set Transparency";
const char addonInfo[] = "Adjust the transparency of an image. Written by Scott Lindsey <wombat@gobe.com>.";
const char addonCategory[] = "Transparency";
int32 addonVersion = 100;

static int32 sAlphaLevel = 128;
static bool sPreview = false;

static void LoadPrefs();


class ConfigView : public BView {
public:
	ConfigView(BRect frame)
		: BView(frame, PLUGIN_PREFIX " configuration", B_FOLLOW_NONE, B_WILL_DRAW) {}
	virtual void AttachedToWindow();
	virtual void MessageReceived(BMessage* message);

	TSlider* fLevel = NULL;
	BCheckBox* fLivePreview = NULL;
	BInvoker* fPreviewInvoker = NULL;
};


// the level as the slider takes it (TSlider::ValueChanged)
class LevelSlider : public TSlider {
public:
	LevelSlider(BRect frame)
		: TSlider(frame, PLUGIN_PREFIX "/alpha_level", "Alpha Level", "%d", NULL, 0, 255) {}
	virtual void ValueChanged()
	{
		sAlphaLevel = TSlider::Value();
		if (sPreview)
			TSlider::ValueChanged();
	}
};


void
ConfigView::AttachedToWindow()
{
	for (int32 i = 0; i < CountChildren(); i++)
		if (BInvoker* invoker = dynamic_cast<BInvoker*>(ChildAt(i)))
			invoker->SetTarget(this);
	if (fPreviewInvoker != NULL && sPreview)
		fPreviewInvoker->Invoke();
}


void
ConfigView::MessageReceived(BMessage* message)
{
	if (message->what != 'PRVW') {
		BView::MessageReceived(message);
		return;
	}
	sPreview = fLivePreview->Value();
	if (fPreviewInvoker != NULL) {
		BMessage preview(*fPreviewInvoker->Message());
		preview.AddBool("preview", sPreview);
		fPreviewInvoker->Invoke(&preview);
	}
}


status_t
Manipulate(BitmapAccessor* sourceBitmap, BMessage* ioExtension, bool checkOnly)
{
	if (!sourceBitmap->IsValid())
		return B_BAD_VALUE;
	color_space space = sourceBitmap->ColorSpace();
	if (space != B_RGBA32 && space != B_RGBA32_BIG)
		return B_NOT_ALLOWED;
	if (checkOnly)
		return B_OK;
	LoadPrefs();
	int32 level = sAlphaLevel;
	BRect r = sourceBitmap->Bounds();
	if (ioExtension != NULL) {
		ioExtension->FindInt32(PLUGIN_PREFIX "/alpha_level", &level);
		BRect selection;
		if (ioExtension->FindRect("selection_rect", &selection) == B_OK) {
			if (!r.Contains(selection))
				return B_BAD_VALUE;
			r = selection;
		}
	}
	// transparentize: the alpha byte of each pixel
	int32 offset = space == B_RGBA32_BIG ? 0 : 3;
	int32 rowBytes;
	uint8* bits = (uint8*)sourceBitmap->AccessBits(r, &rowBytes);
	int32 width = int32(r.Width()), height = int32(r.Height());
	for (int32 y = 0; y <= height; y++) {
		uint8* p = bits + y * rowBytes;
		for (int32 x = 0; x <= width; x++, p += 4)
			p[offset] = uint8(level);
	}
	return B_OK;
}


status_t
GetConfigMessage(BMessage* ioExtension, BMessage* ioCapability)
{
	static bool inited;
	if (!inited) {
		inited = true;
		LoadPrefs();
	}
	ioCapability->AddInt32("selection_rect", B_RECT_TYPE);
	ioCapability->AddInt32("preview_invoker", B_POINTER_TYPE);
	ioExtension->AddInt32(PLUGIN_PREFIX "/alpha_level", sAlphaLevel);
	ioExtension->AddBool(PLUGIN_PREFIX "/preview", sPreview);
	return B_OK;
}


status_t
MakeConfig(BMessage* ioExtension, BView** outView)
{
	BRect bounds(0, 0, 180, 40);
	ConfigView* config = new ConfigView(bounds);
	*outView = config;
	config->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
	LoadPrefs();
	config->fLevel = new LevelSlider(bounds);
	config->fLevel->ResizeToPreferred();
	config->ResizeTo(config->fLevel->Frame().right, config->fLevel->Frame().bottom);
	config->AddChild(config->fLevel);
	if (ioExtension != NULL) {
		bool preview;
		if (ioExtension->FindBool(PLUGIN_PREFIX "/preview", &preview) == B_OK)
			sPreview = preview;
		int32 level;
		if (ioExtension->FindInt32(PLUGIN_PREFIX "/alpha_level", &level) == B_OK)
			sAlphaLevel = level;
		ioExtension->FindPointer("preview_invoker", (void**)&config->fPreviewInvoker);
		config->fLevel->mPreviewInvoker = config->fPreviewInvoker;
		config->ResizeBy(0, 25);
		BRect r = config->Bounds();
		r.top = r.bottom - 20;
		config->fLivePreview = new BCheckBox(r, PLUGIN_PREFIX "/preview", "Preview",
			new BMessage('PRVW'));
		config->AddChild(config->fLivePreview);
		config->fLivePreview->SetValue(sPreview);
	}
	config->fLevel->SetValue(sAlphaLevel);
	return B_OK;
}


static void
LoadPrefs()
{
	PREFHandle prefs;
	PREFData data;
	if (PREFInit(SIGNATURE, &prefs))
		return;
	if (!PREFLoadSet(prefs, PLUGIN_PREFIX, true, &data)) {
		const void* p;
		ssize_t size;
		uint32 type;
		if (!PREFGetData(data, "alpha-level", &p, &size, &type))
			memcpy(&sAlphaLevel, p, sizeof(int32));
		if (!PREFGetData(data, "preview", &p, &size, &type))
			sPreview = *(const bool*)p;
		PREFDisposeSet(&data);
	}
	PREFShutdown(prefs);
}
