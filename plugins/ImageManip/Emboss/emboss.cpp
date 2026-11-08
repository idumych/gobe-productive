// Emboss: the image lit as if its surface had a texture's bumps (a bump image
// tiled from an offset, lit from an azimuth and elevation, its relief
// scaled by a depth). Gobe shipped this add-on without its source, though
// it is built on the GPL GIMP glue; rewritten for the Gobe Productive port
// from the shipped one's behaviour.
#include <Bitmap.h>
#include <CheckBox.h>
#include <Clipboard.h>
#include <Directory.h>
#include <Entry.h>
#include <FilePanel.h>
#include <FindDirectory.h>
#include <Invoker.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <Message.h>
#include <MessageQueue.h>
#include <Path.h>
#include <PopUpMenu.h>
#include <TranslationUtils.h>
#include <View.h>
#include <Window.h>

#include <math.h>
#include <string.h>

#include "BitmapAccessor.h"
#include "ImageManipAddon.h"
#include "TSlider.h"
#include "libprefs.h"

#define PLUGIN_PREFIX "emboss"
#define SIGNATURE "x-vnd.wombat.plugins"

const char addonName[] = "Emboss";
const char addonInfo[] = "Applies an embossing effect to an image, using a specified texture image.";
const char addonCategory[] = "Effects";
int32 addonVersion = 100;

struct Prefs {
	entry_ref mapFile;
	float azimuth = 135;
	float elevation = 45;
	int32 depth = 3;
	int32 xOrigin = 0;
	int32 yOrigin = 0;
	bool clipboard = false;
	bool preview = false;
};
static Prefs sPrefs;

static void LoadPrefs(Prefs* prefs);


// GetPrefsDir: the settings folder's bumpmaps
static bool
BumpmapsDirectory(BDirectory& directory)
{
	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path, true) != B_OK)
		return false;
	path.Append("bumpmaps");
	return directory.SetTo(path.Path()) == B_OK;
}


class ConfigView : public BView {
public:
	ConfigView(BRect frame)
		: BView(frame, PLUGIN_PREFIX " configuration", B_FOLLOW_NONE, B_WILL_DRAW) {}
	virtual ~ConfigView() { delete fPanel; }
	virtual void AttachedToWindow();
	virtual void MessageReceived(BMessage* message);
	void UpdatePopup();
	void Preview();

	BMenuField* fImage = NULL;
	BCheckBox* fClipboard = NULL;
	BCheckBox* fLivePreview = NULL;
	BInvoker* fPreviewInvoker = NULL;
	BFilePanel* fPanel = NULL;
};


class EmbossSlider : public TSlider {
public:
	EmbossSlider(BRect frame, const char* name, const char* label, const char* format,
			int32 minValue, int32 maxValue)
		: TSlider(frame, name, label, format, NULL, minValue, maxValue) {}
	virtual void ValueChanged()
	{
		const char* name = Name();
		if (strcmp(name, "azimuth") == 0)
			sPrefs.azimuth = TSlider::Value();
		else if (strcmp(name, "elevation") == 0)
			sPrefs.elevation = TSlider::Value();
		else if (strcmp(name, "depth") == 0)
			sPrefs.depth = TSlider::Value();
		else if (strcmp(name, "x_origin") == 0)
			sPrefs.xOrigin = TSlider::Value();
		else if (strcmp(name, "y_origin") == 0)
			sPrefs.yOrigin = TSlider::Value();
		if (sPrefs.preview)
			TSlider::ValueChanged();
	}
};


// UpdatePopup: the bump images in the settings folder, then Other…, the one
// in use marked
void
ConfigView::UpdatePopup()
{
	BMenu* menu = fImage->Menu();
	while (menu->CountItems() > 1)
		delete menu->RemoveItem(int32(0));
	BDirectory directory;
	int32 at = 0;
	if (BumpmapsDirectory(directory)) {
		entry_ref ref;
		while (directory.GetNextRef(&ref) == B_OK) {
			BMessage* use = new BMessage(B_REFS_RECEIVED);
			use->AddRef("refs", &ref);
			BMenuItem* item = new BMenuItem(ref.name, use);
			menu->AddItem(item, at++);
			if (ref == sPrefs.mapFile)
				item->SetMarked(true);
		}
	}
	menu->SetTargetForItems(this);
}


void
ConfigView::Preview()
{
	if (fPreviewInvoker != NULL) {
		BMessage preview(*fPreviewInvoker->Message());
		preview.AddBool("preview", sPrefs.preview);
		fPreviewInvoker->Invoke(&preview);
	}
}


void
ConfigView::AttachedToWindow()
{
	for (int32 i = 0; i < CountChildren(); i++)
		if (BInvoker* invoker = dynamic_cast<BInvoker*>(ChildAt(i)))
			invoker->SetTarget(this);
	fImage->Menu()->SetTargetForItems(this);
	if (fPreviewInvoker != NULL && sPrefs.preview)
		fPreviewInvoker->Invoke();
}


void
ConfigView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case 'OTHR':
			if (fPanel == NULL) {
				BMessenger target(this);
				fPanel = new BFilePanel(B_OPEN_PANEL, &target, NULL, B_FILE_NODE, false,
					new BMessage(B_REFS_RECEIVED));
			}
			fPanel->Show();
			return;
		case 'CLIP':
			sPrefs.clipboard = fClipboard->Value();
			break;
		case B_REFS_RECEIVED: {
			entry_ref ref;
			if (message->FindRef("refs", &ref) != B_OK)
				return;
			sPrefs.mapFile = ref;
			UpdatePopup();
			break;
		}
		case 'PRVW':
			sPrefs.preview = fLivePreview->Value();
			break;
		default:
			BView::MessageReceived(message);
			return;
	}
	Preview();
}


// Embosser: the bump image's first byte at a point, the point tiled from
// the origin; the normal from the eight around it (a Sobel); lit
class Embosser {
public:
	~Embosser() { delete fBump; }
	status_t Emboss(BitmapAccessor* image, BRect r, BInvoker* progress, BMessageQueue* cancel,
		const Prefs& prefs);

private:
	uint8 BumpAt(int32 x, int32 y) const
		{ return fBits[x * fBytes + y * fRowBytes]; }
	void BumpCoord(int32 x, int32 y, int32& bx, int32& by) const;
	void CalcNormal(int32 x, int32 y, int32& nx, int32& ny) const;

	BBitmap* fBump = NULL;
	const uint8* fBits = NULL;
	int32 fRowBytes = 0, fBytes = 1, fHeight = 0, fWidth = 0;
	const Prefs* fPrefs = NULL;
};


void
Embosser::BumpCoord(int32 x, int32 y, int32& bx, int32& by) const
{
	bx = x - fPrefs->xOrigin;
	by = y - fPrefs->yOrigin;
	while (by < 0)
		by += fHeight;
	while (bx < 0)
		bx += fWidth;
	bx %= fWidth;
	by %= fHeight;
}


void
Embosser::CalcNormal(int32 x, int32 y, int32& nx, int32& ny) const
{
	int32 bx, by;
	BumpCoord(x - 1, y - 1, bx, by);
	int32 v = BumpAt(bx, by);
	nx = v;
	ny = -v;
	BumpCoord(x, y - 1, bx, by);
	ny -= BumpAt(bx, by);
	BumpCoord(x + 1, y - 1, bx, by);
	v = BumpAt(bx, by);
	nx -= v;
	ny -= v;
	BumpCoord(x - 1, y, bx, by);
	nx += BumpAt(bx, by);
	BumpCoord(x + 1, y, bx, by);
	nx -= BumpAt(bx, by);
	BumpCoord(x - 1, y + 1, bx, by);
	v = BumpAt(bx, by);
	nx += v;
	ny -= v;
	BumpCoord(x, y + 1, bx, by);
	ny += BumpAt(bx, by);
	BumpCoord(x + 1, y + 1, bx, by);
	v = BumpAt(bx, by);
	nx -= v;
	ny += v;
}


status_t
Embosser::Emboss(BitmapAccessor* image, BRect r, BInvoker* progress, BMessageQueue* cancel,
	const Prefs& prefs)
{
	fPrefs = &prefs;
	// the bump image: the file, or the clipboard's picture
	if (!prefs.clipboard) {
		BEntry entry(&prefs.mapFile, false);
		if (entry.InitCheck() != B_OK)
			return entry.InitCheck();
		if (!entry.Exists())
			return B_ENTRY_NOT_FOUND;
		if (entry.IsDirectory())
			return B_IS_A_DIRECTORY;
		BPath path(&entry);
		if (path.InitCheck() != B_OK)
			return path.InitCheck();
		fBump = BTranslationUtils::GetBitmapFile(path.Path());
		if (fBump == NULL)
			return B_NO_TRANSLATOR;
	} else {
		if (be_clipboard->Lock()) {
			BMessage archive;
			BMessage* data = be_clipboard->Data();
			static const char* kNames[] = {"image/x-vnd.Be-bitmap", "image/x-vnd.be-bitmap",
				"bitmap", "icon/large", "icon/mini", "image/bitmap"};
			for (const char* name : kNames)
				if (data != NULL && data->FindMessage(name, &archive) == B_OK) {
					fBump = new BBitmap(&archive);
					break;
				}
			be_clipboard->Unlock();
		}
		if (fBump == NULL || fBump->InitCheck() != B_OK || !fBump->IsValid())
			return B_ERROR;
	}
	fRowBytes = fBump->BytesPerRow();
	fBits = (const uint8*)fBump->Bits();
	fHeight = fBump->Bounds().IntegerHeight() + 1;
	fWidth = fBump->Bounds().IntegerWidth() + 1;
	switch (fBump->ColorSpace()) {
		case B_GRAY8:
		case B_CMAP8:
			fBytes = 1;
			break;
		case B_RGB24:
		case B_RGB24_BIG:
			fBytes = 3;
			break;
		case B_RGB32:
		case B_RGBA32:
		case B_RGB32_BIG:
		case B_RGBA32_BIG:
			fBytes = 4;
			break;
		default:
			return B_NOT_ALLOWED;
	}
	color_space space = image->ColorSpace();
	int32 pixel = space == B_GRAY8 || space == B_CMAP8 ? 1
		: space == B_RGB24 || space == B_RGB24_BIG ? 3 : 4;
	double azimuth = prefs.azimuth * 3.1415927 / 180.0;
	double elevation = prefs.elevation * 3.1415927 / 180.0;
	int32 lx = int32(rint(cos(azimuth) * cos(elevation) * 255.9));
	int32 ly = int32(rint(sin(azimuth) * cos(elevation) * 255.9));
	int32 lz = int32(rint(sin(elevation) * 255.9));
	int32 nz = 1530 / std::max(int32(1), prefs.depth);
	int32 top = int32(rint(r.top)), bottom = int32(rint(r.bottom));
	int32 left = int32(rint(r.left)), right = int32(rint(r.right));
	for (int32 y = top; y <= bottom; y++) {
		if (cancel != NULL && !cancel->IsEmpty())
			return B_CANCELED;
		int32 rowBytes;
		uint8* p = (uint8*)image->AccessBits(BRect(left, y, right, y), &rowBytes);
		for (int32 x = left; x <= right; x++, p += pixel) {
			int32 nx, ny;
			CalcNormal(x, y, nx, ny);
			uint8 shade;
			if (nx == 0 && ny == 0)
				shade = uint8(lz);
			else {
				int32 dot = nx * lx + ny * ly + nz * lz;
				shade = dot < 0 ? 0
					: uint8(rint(double(dot) / sqrt(double(nx * nx + ny * ny + nz * nz))));
			}
			switch (space) {
				case B_GRAY8:
					p[0] = uint8(p[0] * shade >> 8);
					break;
				case B_RGB32_BIG:
				case B_RGBA32_BIG:
					p[1] = uint8(p[1] * shade >> 8);
					p[2] = uint8(p[2] * shade >> 8);
					p[3] = uint8(p[3] * shade >> 8);
					break;
				case B_RGB24:
				case B_RGB24_BIG:
				case B_RGB32:
				case B_RGBA32:
					p[0] = uint8(p[0] * shade >> 8);
					p[1] = uint8(p[1] * shade >> 8);
					p[2] = uint8(p[2] * shade >> 8);
					break;
				default:
					break;
			}
		}
		if (progress != NULL) {
			BMessage update(*progress->Message());
			update.AddFloat("percentage", float(y - top + 1) / float(bottom - top + 1));
			progress->Invoke(&update);
		}
	}
	return B_OK;
}


// GetSettings: the settings in the io extension over the ones kept
static void
GetSettings(BMessage* ioExtension, Prefs* prefs, BRect* selection, BInvoker** preview,
	BInvoker** progress, BMessageQueue** cancel)
{
	if (ioExtension == NULL)
		return;
	entry_ref ref;
	if (ioExtension->FindRef(PLUGIN_PREFIX "/map_file", &ref) == B_OK)
		prefs->mapFile = ref;
	float f;
	if (ioExtension->FindFloat(PLUGIN_PREFIX "/azimuth", &f) == B_OK)
		prefs->azimuth = f;
	if (ioExtension->FindFloat(PLUGIN_PREFIX "/elevation", &f) == B_OK)
		prefs->elevation = f;
	int32 i;
	if (ioExtension->FindInt32(PLUGIN_PREFIX "/depth", &i) == B_OK)
		prefs->depth = i;
	if (ioExtension->FindInt32(PLUGIN_PREFIX "/x_origin", &i) == B_OK)
		prefs->xOrigin = i;
	if (ioExtension->FindInt32(PLUGIN_PREFIX "/y_origin", &i) == B_OK)
		prefs->yOrigin = i;
	bool b;
	if (ioExtension->FindBool(PLUGIN_PREFIX "/clipboard", &b) == B_OK)
		prefs->clipboard = b;
	if (ioExtension->FindBool(PLUGIN_PREFIX "/preview", &b) == B_OK)
		prefs->preview = b;
	BRect r;
	if (selection != NULL && ioExtension->FindRect("selection_rect", &r) == B_OK)
		*selection = r;
	if (preview != NULL)
		ioExtension->FindPointer("preview_invoker", (void**)preview);
	if (progress != NULL)
		ioExtension->FindPointer("progress_invoker", (void**)progress);
	if (cancel != NULL)
		ioExtension->FindPointer("cancel_queue", (void**)cancel);
}


status_t
Manipulate(BitmapAccessor* sourceBitmap, BMessage* ioExtension, bool checkOnly)
{
	color_space space = sourceBitmap->ColorSpace();
	switch (space) {
		case B_GRAY8: case B_RGB24: case B_RGB24_BIG: case B_RGB32: case B_RGBA32:
		case B_RGB32_BIG: case B_RGBA32_BIG:
			break;
		default:
			return B_NOT_ALLOWED;
	}
	if (checkOnly)
		return B_OK;
	Prefs prefs;
	LoadPrefs(&prefs);
	BRect selection = sourceBitmap->Bounds();
	BInvoker* progress = NULL;
	BMessageQueue* cancel = NULL;
	GetSettings(ioExtension, &prefs, &selection, NULL, &progress, &cancel);
	Embosser embosser;
	return embosser.Emboss(sourceBitmap, selection, progress, cancel, prefs);
}


status_t
GetConfigMessage(BMessage* ioExtension, BMessage* ioCapability)
{
	static bool inited;
	if (!inited) {
		inited = true;
		LoadPrefs(&sPrefs);
	}
	ioCapability->AddInt32("selection_rect", B_RECT_TYPE);
	ioCapability->AddInt32("progress_invoker", B_POINTER_TYPE);
	ioCapability->AddInt32("preview_invoker", B_POINTER_TYPE);
	ioCapability->AddInt32("cancel_queue", B_POINTER_TYPE);
	ioExtension->AddRef(PLUGIN_PREFIX "/map_file", &sPrefs.mapFile);
	ioExtension->AddFloat(PLUGIN_PREFIX "/azimuth", sPrefs.azimuth);
	ioExtension->AddFloat(PLUGIN_PREFIX "/elevation", sPrefs.elevation);
	ioExtension->AddInt32(PLUGIN_PREFIX "/depth", sPrefs.depth);
	ioExtension->AddInt32(PLUGIN_PREFIX "/x_origin", sPrefs.xOrigin);
	ioExtension->AddInt32(PLUGIN_PREFIX "/y_origin", sPrefs.yOrigin);
	ioExtension->AddBool(PLUGIN_PREFIX "/clipboard", sPrefs.clipboard);
	ioExtension->AddBool(PLUGIN_PREFIX "/preview", sPrefs.preview);
	return B_OK;
}


status_t
MakeConfig(BMessage* ioExtension, BView** outView)
{
	BRect bounds(0, 0, 200, 40);
	ConfigView* config = new ConfigView(bounds);
	*outView = config;
	config->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
	LoadPrefs(&sPrefs);
	static const struct {
		const char* name;
		const char* label;
		const char* format;
		int32 low, high;
		const char* lowLabel;
		const char* highLabel;
	} kSliders[] = {
		{"azimuth", "Azimuth", "%d\xC2\xB0", 0, 360, "0\xC2\xB0", "360\xC2\xB0"},
		{"elevation", "Elevation", "%d\xC2\xB0", 0, 90, "0\xC2\xB0", "90\xC2\xB0"},
		{"depth", "Depth", "%d", 1, 100, "1", "100"},
		{"x_origin", "X Offset", "%d", -100, 100, "-100", "100"},
		{"y_origin", "Y Offset", "%d", -100, 100, "-100", "100"}
	};
	TSlider* sliders[5];
	for (int32 i = 0; i < 5; i++) {
		sliders[i] = new EmbossSlider(bounds, kSliders[i].name, kSliders[i].label,
			kSliders[i].format, kSliders[i].low, kSliders[i].high);
		sliders[i]->SetHashMarks(B_HASH_MARKS_BOTTOM);
		sliders[i]->SetHashMarkCount(5);
		sliders[i]->SetLimitLabels(kSliders[i].lowLabel, kSliders[i].highLabel);
		sliders[i]->ResizeToPreferred();
		config->AddChild(sliders[i]);
		bounds = sliders[i]->Frame();
		bounds.OffsetBy(0, bounds.Height());
	}
	bounds.OffsetBy(0, 5);
	BPopUpMenu* menu = new BPopUpMenu("");
	menu->SetLabelFromMarked(true);
	menu->AddItem(new BMenuItem("Other\xE2\x80\xA6", new BMessage('OTHR')));
	config->fImage = new BMenuField(bounds, "outside_type", "Bump Image", menu);
	config->fImage->ResizeToPreferred();
	config->fImage->SetDivider(config->fImage->StringWidth("Bump Image") + 9);
	config->UpdatePopup();
	config->AddChild(config->fImage);
	bounds = config->fImage->Frame();
	bounds.OffsetBy(0, bounds.Height() + 5);
	config->fClipboard = new BCheckBox(bounds, PLUGIN_PREFIX "/clipboard",
		"Get Bump Image from Clipboard", new BMessage('CLIP'));
	config->fClipboard->ResizeToPreferred();
	config->AddChild(config->fClipboard);
	config->ResizeTo(std::max(sliders[4]->Frame().right, config->fClipboard->Frame().right),
		config->fClipboard->Frame().bottom);
	if (ioExtension != NULL) {
		BRect selection;
		GetSettings(ioExtension, &sPrefs, &selection, &config->fPreviewInvoker, NULL, NULL);
		for (int32 i = 0; i < 5; i++)
			sliders[i]->mPreviewInvoker = config->fPreviewInvoker;
		config->ResizeBy(0, 25);
		BRect r = config->Bounds();
		r.top = r.bottom - 20;
		config->fLivePreview = new BCheckBox(r, PLUGIN_PREFIX "/preview", "Preview",
			new BMessage('PRVW'));
		config->AddChild(config->fLivePreview);
		config->fLivePreview->SetValue(sPrefs.preview);
	}
	sliders[0]->SetValue(int32(sPrefs.azimuth));
	sliders[1]->SetValue(int32(sPrefs.elevation));
	sliders[2]->SetValue(sPrefs.depth);
	sliders[3]->SetValue(sPrefs.xOrigin);
	sliders[4]->SetValue(sPrefs.yOrigin);
	config->fClipboard->SetValue(sPrefs.clipboard);
	return B_OK;
}


static void
LoadPrefs(Prefs* prefs)
{
	PREFHandle handle;
	PREFData data;
	if (PREFInit(SIGNATURE, &handle))
		return;
	*prefs = sPrefs;
	if (!PREFLoadSet(handle, PLUGIN_PREFIX, true, &data)) {
		const void* p;
		ssize_t size;
		uint32 type;
		if (!PREFGetData(data, "map_file", &p, &size, &type)) {
			BEntry entry((const char*)p);
			entry.GetRef(&prefs->mapFile);
		}
		if (!PREFGetData(data, "azimuth", &p, &size, &type))
			memcpy(&prefs->azimuth, p, sizeof(float));
		if (!PREFGetData(data, "elevation", &p, &size, &type))
			memcpy(&prefs->elevation, p, sizeof(float));
		if (!PREFGetData(data, "depth", &p, &size, &type))
			memcpy(&prefs->depth, p, sizeof(int32));
		if (!PREFGetData(data, "x_origin", &p, &size, &type))
			memcpy(&prefs->xOrigin, p, sizeof(int32));
		if (!PREFGetData(data, "y_origin", &p, &size, &type))
			memcpy(&prefs->yOrigin, p, sizeof(int32));
		if (!PREFGetData(data, "clipboard", &p, &size, &type))
			prefs->clipboard = *(const bool*)p;
		if (!PREFGetData(data, "preview", &p, &size, &type))
			prefs->preview = *(const bool*)p;
		PREFDisposeSet(&data);
	}
	PREFShutdown(handle);
}
