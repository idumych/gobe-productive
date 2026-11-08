// Colorify: the image made grey, then tinted with a colour. Gobe shipped this
// add-on without its source; rewritten for the Gobe Productive port from the
// shipped one's behaviour (by Scott Lindsey).
#include <CheckBox.h>
#include <Invoker.h>
#include <Message.h>
#include <View.h>
#include <Window.h>

#include <math.h>
#include <string.h>

#include "BitmapAccessor.h"
#include "ImageManipAddon.h"
#include "TSlider.h"
#include "libprefs.h"

#define PLUGIN_PREFIX "colorify"
#define SIGNATURE "x-vnd.wombat.plugins"

const char addonName[] = "Colorify";
const char addonInfo[] = "Change the color of an image.  This transforms the image to greyscale and then tints it with the selected color.\nWritten by Scott Lindsey <wombat@gobe.com>.";
const char addonCategory[] = "Color";
int32 addonVersion = 100;

static rgb_color sColor = {0, 255, 255, 255};
static bool sPreview = false;

static void LoadPrefs();


class ConfigView : public BView {
public:
	ConfigView(BRect frame)
		: BView(frame, PLUGIN_PREFIX " configuration", B_FOLLOW_NONE, B_WILL_DRAW) {}
	virtual void AttachedToWindow();
	virtual void MessageReceived(BMessage* message);

	BCheckBox* fLivePreview = NULL;
	BInvoker* fPreviewInvoker = NULL;
};


// a channel's slider: its value into the colour as it changes
class ChannelSlider : public TSlider {
public:
	ChannelSlider(BRect frame, const char* name, const char* label, uint8* channel)
		: TSlider(frame, name, label, "%d", NULL, 0, 255), fChannel(channel) {}
	virtual void ValueChanged()
	{
		*fChannel = uint8(TSlider::Value());
		if (sPreview)
			TSlider::ValueChanged();
	}
private:
	uint8* fChannel;
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


// colorify: each pixel's grey (0.3 red, 0.59 green, 0.11 blue) times the
// colour, over 256
static void
Colorify(BitmapAccessor* bitmap, const uint8 tint[4], BRect r)
{
	bool big = bitmap->ColorSpace() == B_RGBA32_BIG || bitmap->ColorSpace() == B_RGB32_BIG;
	int32 offset = big ? 1 : 0;
	float weights[4];
	if (big) {
		weights[0] = 0;
		weights[1] = 0.3f;
		weights[2] = 0.59f;
		weights[3] = 0.11f;
	} else {
		weights[0] = 0.11f;
		weights[1] = 0.59f;
		weights[2] = 0.3f;
		weights[3] = 0;
	}
	int32 rowBytes;
	uint8* bits = (uint8*)bitmap->AccessBits(r, &rowBytes);
	int32 width = int32(rint(r.Width())), height = int32(rint(r.Height()));
	for (int32 y = 0; y <= height; y++) {
		uint8* p = bits + y * rowBytes;
		for (int32 x = 0; x <= width; x++, p += 4) {
			int32 grey = int32(rintf(p[3] * weights[3] + p[2] * weights[2] + p[1] * weights[1]
				+ p[0] * weights[0]));
			for (int32 c = 0; c < 3; c++)
				p[offset + c] = uint8((tint[offset + c] * grey) >> 8);
		}
	}
}


status_t
Manipulate(BitmapAccessor* sourceBitmap, BMessage* ioExtension, bool checkOnly)
{
	color_space space = sourceBitmap->ColorSpace();
	if (space != B_RGBA32 && space != B_RGB32 && space != B_RGBA32_BIG && space != B_RGB32_BIG)
		return B_NOT_ALLOWED;
	if (checkOnly)
		return B_OK;
	LoadPrefs();
	rgb_color color = sColor;
	BRect r = sourceBitmap->Bounds();
	if (ioExtension != NULL) {
		const void* data;
		ssize_t size;
		if (ioExtension->FindData(PLUGIN_PREFIX "/color", B_RGB_COLOR_TYPE, &data, &size) == B_OK)
			color = *(const rgb_color*)data;
		BRect selection;
		if (ioExtension->FindRect("selection_rect", &selection) == B_OK) {
			if (!r.Contains(selection))
				return B_BAD_VALUE;
			r = selection;
		}
	}
	// (the colour in the pixels' byte order)
	uint8 tint[4];
	if (space == B_RGBA32_BIG || space == B_RGB32_BIG) {
		tint[0] = color.alpha;
		tint[1] = color.red;
		tint[2] = color.green;
		tint[3] = color.blue;
	} else {
		tint[0] = color.blue;
		tint[1] = color.green;
		tint[2] = color.red;
		tint[3] = color.alpha;
	}
	Colorify(sourceBitmap, tint, r);
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
	ioExtension->AddData(PLUGIN_PREFIX "/color", B_RGB_COLOR_TYPE, &sColor, sizeof(sColor));
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
	const char* names[3] = {"red", "green", "blue"};
	const char* labels[3] = {"Red", "Green", "Blue"};
	uint8* channels[3] = {&sColor.red, &sColor.green, &sColor.blue};
	TSlider* sliders[3];
	for (int32 i = 0; i < 3; i++) {
		sliders[i] = new ChannelSlider(bounds, names[i], labels[i], channels[i]);
		sliders[i]->ResizeToPreferred();
		config->AddChild(sliders[i]);
		bounds = sliders[i]->Frame();
		bounds.OffsetBy(0, bounds.Height());
	}
	config->ResizeTo(sliders[2]->Frame().right, sliders[2]->Frame().bottom);
	if (ioExtension != NULL) {
		bool preview;
		if (ioExtension->FindBool(PLUGIN_PREFIX "/preview", &preview) == B_OK)
			sPreview = preview;
		const void* data;
		ssize_t size;
		if (ioExtension->FindData(PLUGIN_PREFIX "/color", B_RGB_COLOR_TYPE, &data, &size) == B_OK)
			sColor = *(const rgb_color*)data;
		ioExtension->FindPointer("preview_invoker", (void**)&config->fPreviewInvoker);
		for (int32 i = 0; i < 3; i++)
			sliders[i]->mPreviewInvoker = config->fPreviewInvoker;
		config->ResizeBy(0, 25);
		BRect r = config->Bounds();
		r.top = r.bottom - 20;
		config->fLivePreview = new BCheckBox(r, PLUGIN_PREFIX "/preview", "Preview",
			new BMessage('PRVW'));
		config->AddChild(config->fLivePreview);
		config->fLivePreview->SetValue(sPreview);
	}
	uint8 values[3] = {sColor.red, sColor.green, sColor.blue};
	for (int32 i = 0; i < 3; i++)
		sliders[i]->SetValue(int32(values[i]));
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
		if (!PREFGetData(data, "color", &p, &size, &type))
			memcpy(&sColor, p, sizeof(sColor));
		if (!PREFGetData(data, "preview", &p, &size, &type))
			sPreview = *(const bool*)p;
		PREFDisposeSet(&data);
	}
	PREFShutdown(prefs);
}
