// Change To Alpha: whiteness (or blackness) made transparency. Gobe shipped
// this add-on without its source; rewritten for the Gobe Productive port
// from the shipped one's behaviour (Change To Alpha 1.0, by Scott Lindsey).
#include <CheckBox.h>
#include <Invoker.h>
#include <Message.h>
#include <RadioButton.h>
#include <View.h>
#include <Window.h>

#include <algorithm>
#include <math.h>
#include <string.h>

#include "BitmapAccessor.h"
#include "ImageManipAddon.h"
#include "libprefs.h"

#define PLUGIN_PREFIX "to_alpha"
#define SIGNATURE "x-vnd.wombat.plugins"

const char addonName[] = "Change To Alpha";
const char addonInfo[] = "Change whiteness or blackness to alpha.  \xC2\xA9" "1999 by Scott Lindsey <wombat@gobe.com>.";
const char addonCategory[] = "Transparency";
int32 addonVersion = 100;

// 0 white to alpha, 1 black to alpha
static int32 sAlphaOption = 0;
static bool sPreview = false;

static void LoadPrefs();


class ConfigView : public BView {
public:
	ConfigView(BRect frame)
		: BView(frame, PLUGIN_PREFIX " configuration", B_FOLLOW_NONE, B_WILL_DRAW) {}
	virtual void AttachedToWindow();
	virtual void MessageReceived(BMessage* message);

	BRadioButton* fWhite = NULL;
	BRadioButton* fBlack = NULL;
	BCheckBox* fLivePreview = NULL;
	BInvoker* fPreviewInvoker = NULL;
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
	switch (message->what) {
		case 'ALPH':
			sAlphaOption = fBlack->Value() ? 1 : 0;
			break;
		case 'PRVW':
			sPreview = fLivePreview->Value();
			break;
		default:
			BView::MessageReceived(message);
			return;
	}
	if (fPreviewInvoker != NULL) {
		BMessage preview(*fPreviewInvoker->Message());
		preview.AddBool("preview", sPreview);
		fPreviewInvoker->Invoke(&preview);
	}
}


// black_alpha: the brightest channel becomes the alpha, the others scaled up
// under it (the brightest to 255)
static void
BlackAlpha(float& high, float& a, float& b, float& alpha)
{
	alpha = high;
	if (alpha >= 1.0) {
		a = a * 255.0 / alpha;
		b = b * 255.0 / alpha;
		high = 255.0;
	}
}


// white_alpha: 255 less the darkest channel becomes the alpha, the others
// scaled down from white (the darkest to 0)
static void
WhiteAlpha(float& low, float& a, float& b, float& alpha)
{
	alpha = 255.0 - low;
	if (alpha >= 1.0) {
		a = 255.0 - (255.0 - a) * 255.0 / (255.0 - low);
		b = 255.0 - (255.0 - b) * 255.0 / (255.0 - low);
		low = 0.0;
	}
}


static void
ToAlpha(BitmapAccessor* bitmap, BRect r, bool black)
{
	for (float y = r.top; y <= r.bottom; y++) {
		int32 rowBytes;
		uint8* p = (uint8*)bitmap->AccessBits(BRect(r.left, y, r.right, y), &rowBytes);
		for (float x = r.left; x <= r.right; x++, p += 4) {
			float b = p[0], g = p[1], red = p[2], alpha = p[3];
			if (black) {
				if (b <= g) {
					if (g <= red)
						BlackAlpha(red, g, b, alpha);
					else
						BlackAlpha(g, b, red, alpha);
				} else if (b <= red)
					BlackAlpha(red, g, b, alpha);
				else
					BlackAlpha(b, g, red, alpha);
			} else {
				if (b < g) {
					if (b < red)
						WhiteAlpha(b, g, red, alpha);
					else
						WhiteAlpha(red, g, b, alpha);
				} else if (g < red)
					WhiteAlpha(g, b, red, alpha);
				else
					WhiteAlpha(red, g, b, alpha);
			}
			p[0] = uint8(rint(b));
			p[1] = uint8(rint(g));
			p[2] = uint8(rint(red));
			p[3] = uint8(rint(alpha));
		}
	}
}


status_t
Manipulate(BitmapAccessor* sourceBitmap, BMessage* ioExtension, bool checkOnly)
{
	if (sourceBitmap->ColorSpace() != B_RGBA32)
		return B_NOT_ALLOWED;
	if (checkOnly)
		return B_OK;
	BRect selection;
	if (ioExtension == NULL || ioExtension->FindRect("selection_rect", &selection) != B_OK)
		selection = sourceBitmap->Bounds();
	int32 option = 0;
	if (ioExtension != NULL)
		ioExtension->FindInt32(PLUGIN_PREFIX "/alpha_option", &option);
	ToAlpha(sourceBitmap, selection, option != 0);
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
	ioExtension->AddInt32(PLUGIN_PREFIX "/alpha_option", sAlphaOption);
	ioExtension->AddBool(PLUGIN_PREFIX "/preview", sPreview);
	return B_OK;
}


status_t
MakeConfig(BMessage* ioExtension, BView** outView)
{
	BRect bounds(0, 0, 180, 30);
	ConfigView* config = new ConfigView(bounds);
	*outView = config;
	config->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
	LoadPrefs();
	config->fWhite = new BRadioButton(bounds, "white", "White to Alpha", new BMessage('ALPH'));
	config->fWhite->ResizeToPreferred();
	bounds.OffsetTo(config->fWhite->Frame().LeftBottom());
	config->fBlack = new BRadioButton(bounds, "black", "Black to Alpha", new BMessage('ALPH'));
	config->fBlack->ResizeToPreferred();
	config->ResizeTo(std::max(config->fWhite->Frame().right, config->fBlack->Frame().right),
		config->fBlack->Frame().bottom);
	config->AddChild(config->fWhite);
	config->AddChild(config->fBlack);
	if (ioExtension != NULL) {
		bool preview;
		if (ioExtension->FindBool(PLUGIN_PREFIX "/preview", &preview) == B_OK)
			sPreview = preview;
		int32 option;
		if (ioExtension->FindInt32(PLUGIN_PREFIX "/alpha_option", &option) == B_OK)
			sAlphaOption = option;
		ioExtension->FindPointer("preview_invoker", (void**)&config->fPreviewInvoker);
		config->ResizeBy(0, 25);
		BRect r = config->Bounds();
		r.top = r.bottom - 20;
		config->fLivePreview = new BCheckBox(r, PLUGIN_PREFIX "/preview", "Preview",
			new BMessage('PRVW'));
		config->AddChild(config->fLivePreview);
		config->fLivePreview->SetValue(sPreview);
	}
	config->fWhite->SetValue(sAlphaOption == 0);
	config->fBlack->SetValue(sAlphaOption == 1);
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
		if (!PREFGetData(data, "alpha-option", &p, &size, &type))
			memcpy(&sAlphaOption, p, sizeof(int32));
		if (!PREFGetData(data, "preview", &p, &size, &type))
			sPreview = *(const bool*)p;
		PREFDisposeSet(&data);
	}
	PREFShutdown(prefs);
}
