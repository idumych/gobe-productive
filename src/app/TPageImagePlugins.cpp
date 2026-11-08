// TPageView's image plug-ins (Parts/ImageProcessing: TPartIM::HandleMessage
// 'sI03', ManipPlugin's PluginThread, PreviewThread and Manipulate,
// TSetBitsCommand): a plug-in chosen in the Plugins menu runs on the
// current bits (the floating ones if there are any) within the selection,
// all of it if nothing was selected; its settings view comes up in the
// Plugin dialog, previewing on the image as it changes; the final run shows
// its progress in the Status window and is undone as one step named after
// it.
#include "TPageView.h"

#include <Alert.h>
#include <Application.h>
#include <Bitmap.h>
#include <Invoker.h>
#include <MessageQueue.h>
#include <OS.h>
#include <Window.h>

#include "BBitmapAccessor.h"
#include "GobeResources.h"
#include "ImageManip.h"
#include "ImageSelect.h"
#include "TImagePlugins.h"
#include "TRibbon.h"
#include "TStatusDialog.h"

namespace gobe {

// a plug-in being run on an image (ManipPlugin's state)
struct TPageView::PluginRun {
	int32 index = -1;
	size_t part = 0;
	BMessage settings;				// the io extension (its settings)
	BInvoker previewInvoker;		// "preview_invoker": 'UPDT' to the dialog
	BMessageQueue cancel;			// "cancel_queue"
	ArchiveBitmap original;			// the bits before any preview
	bool floating = false;			// working on the floating bits
	BBitmap* configBits = NULL;		// "config_bitmap"
	BBitmapAccessor* configAccessor = NULL;
	TDialogManipConfig* dialog = NULL;
	TStatusDialog* status = NULL;
	std::shared_ptr<Document> before;	// for the undo step
	UndoStack::ViewState beforeView;
	thread_id worker = -1;
	int32 serial = 0;				// the preview asked for last
	bool previewPending = false;	// asked for while one was running
	bool previewShown = false;
	bool final = false;
};

namespace {

// ManipPlugin::Manipulate's work, on its own thread: the add-on run on a
// copy of the bits, the result posted back
struct PluginJob {
	BMessenger view;
	intptr_t addon = 0;
	bool converter = false;
	BMessage settings;
	ArchiveBitmap bits;
	BMessageQueue* cancel = NULL;
	BMessenger progress;			// the view, for "progress_invoker"
	bool withProgress = false;
	int32 serial = 0;
	uint32 reply = 0;
};


BBitmap*
ToBitmap(const ArchiveBitmap& bits)
{
	BBitmap* bitmap = new BBitmap(BRect(0, 0, bits.Width() - 1, bits.Height() - 1), B_RGBA32);
	if (bitmap->InitCheck() != B_OK || bitmap->ImportBits(bits.bits.data(), bits.bits.size(),
			bits.bytesPerRow, 0, color_space(bits.colorSpace)) != B_OK) {
		delete bitmap;
		return NULL;
	}
	return bitmap;
}


ArchiveBitmap
FromBitmap(BBitmap* bitmap, uint32 colorSpace)
{
	ArchiveBitmap out;
	BRect b = bitmap->Bounds();
	int w = int(b.Width()) + 1, h = int(b.Height()) + 1;
	out.colorSpace = colorSpace == 0x0008 ? 0x0008 : 0x2008;
	out.bytesPerRow = uint32(w) * 4;
	out.bounds[2] = float(w - 1);
	out.bounds[3] = float(h - 1);
	out.bits.resize(size_t(out.bytesPerRow) * size_t(h));
	BBitmap converted(BRect(0, 0, w - 1, h - 1), B_RGBA32);
	if (converted.ImportBits(bitmap) != B_OK)
		return ArchiveBitmap();
	for (int y = 0; y < h; y++)
		memcpy(&out.bits[size_t(y) * out.bytesPerRow],
			(const uint8*)converted.Bits() + size_t(y) * size_t(converted.BytesPerRow()),
			out.bytesPerRow);
	return out;
}


int32
RunJob(void* data)
{
	std::unique_ptr<PluginJob> job((PluginJob*)data);
	BBitmap* bitmap = ToBitmap(job->bits);
	status_t status = B_NO_MEMORY;
	ArchiveBitmap* result = NULL;
	if (bitmap != NULL) {
		BBitmapAccessor source(bitmap);
		source.SetDispose(false);
		BInvoker progress(new BMessage('STAT'), job->progress);
		if (job->withProgress)
			job->settings.AddPointer("progress_invoker", &progress);
		job->settings.AddPointer("cancel_queue", job->cancel);
		if (job->converter) {
			BBitmapAccessor dest(NULL);
			status = Image_Convert(job->addon, &source, &dest, &job->settings, false);
			if (status == B_OK && dest.Bitmap() != NULL) {
				dest.SetDispose(false);
				BBitmap* made = dest.Bitmap();
				result = new ArchiveBitmap(FromBitmap(made, job->bits.colorSpace));
				delete made;
			}
		} else {
			status = Image_Manipulate(job->addon, &source, &job->settings, false);
			if (status == B_OK)
				result = new ArchiveBitmap(FromBitmap(bitmap, job->bits.colorSpace));
		}
		delete bitmap;
	}
	BMessage reply(job->reply);
	reply.AddInt32("status", status);
	reply.AddInt32("serial", job->serial);
	if (result != NULL)
		reply.AddPointer("bits", result);
	if (job->view.SendMessage(&reply) != B_OK)
		delete result;
	return 0;
}

} // namespace


// the bits a plug-in works on, and where the selection is in them
// (selection_rect: the selection's rectangle, at 0, 0 for floating bits)
static BRect
PluginRect(const IMPart& im, bool floating)
{
	IMRect m = im.selection.Mask();
	if (!m.Valid())
		return BRect(0, 0, im.image.Width() - 1, im.image.Height() - 1);
	BRect r(m.left, m.top, m.right, m.bottom);
	if (floating)
		r.OffsetTo(0, 0);
	return r;
}


// the settings with the selection's rectangle and the colors
static void
AddRunSettings(BMessage& settings, const IMPart& im, bool floating)
{
	settings.RemoveName("selection_rect");
	settings.AddRect("selection_rect", PluginRect(im, floating));
	rgb_color fore = {im.colors[0][0], im.colors[0][1], im.colors[0][2], im.colors[0][3]};
	rgb_color back = {im.colors[1][0], im.colors[1][1], im.colors[1][2], im.colors[1][3]};
	settings.RemoveName("foreground_color");
	settings.RemoveName("background_color");
	settings.AddData("foreground_color", B_RGB_COLOR_TYPE, &fore, sizeof(fore));
	settings.AddData("background_color", B_RGB_COLOR_TYPE, &back, sizeof(back));
}


// TSetBitsCommand::Perform: the plug-in's bits put down where the selection
// is (all of them without one)
static void
PutBits(IMPart& im, bool floating, const ArchiveBitmap& original, const ArchiveBitmap& result)
{
	ArchiveBitmap& target = floating ? im.floating : im.image;
	target = original;
	IMRect m = im.selection.Mask();
	int left = floating && m.Valid() ? m.left : 0, top = floating && m.Valid() ? m.top : 0;
	bool all = im.selection.Empty();
	for (int y = 0; y < std::min(target.Height(), result.Height()); y++)
		for (int x = 0; x < std::min(target.Width(), result.Width()); x++)
			if (all || im.selection.Contains(x + left, y + top))
				memcpy(&target.bits[size_t(y) * target.bytesPerRow + size_t(x) * 4],
					&result.bits[size_t(y) * result.bytesPerRow + size_t(x) * 4], 4);
}


// 'sI03': the plug-in at "index" (Option: its About instead)
void
TPageView::RunImagePlugin(int32 index)
{
	size_t part;
	IMPart* im = MainImage(&part);
	ImagePlugin* plugin = EditableImagePlugin(index);
	if (im == NULL || plugin == NULL || fPluginRun)
		return;
	if ((modifiers() & B_OPTION_KEY) != 0) {
		PluginAbout(index);
		return;
	}
	// (nothing selected: all of it, a step of its own)
	if (plugin->kind != 2 && im->selection.Empty()) {
		BMessage all(B_SELECT_ALL);
		ImageSelectionCommand(&all);
	}
	im = fDocument.EditableImagePart(part);
	fPluginRun.reset(new PluginRun);
	PluginRun& run = *fPluginRun;
	run.index = index;
	run.part = part;
	run.floating = !im->floating.bits.empty();
	run.original = run.floating ? im->floating : im->image;
	run.before.reset(new Document(fDocument));
	run.beforeView = CurrentViewState();
	// ManipPlugin::GetConfig: the settings it had last, else its own
	if (!plugin->config) {
		plugin->config.reset(new BMessage(B_SIMPLE_DATA));
		BMessage capabilities;
		Image_GetConfigurationMessage(plugin->addon, plugin->config.get(), &capabilities);
	}
	run.settings = *plugin->config;
	AddRunSettings(run.settings, *im, run.floating);
	// (checkOnly: whether it takes these bits)
	BBitmap* check = new BBitmap(BRect(0, 0, run.original.Width() - 1, run.original.Height() - 1),
		B_RGBA32);
	BBitmapAccessor checkAccessor(check);
	status_t status = plugin->kind == 2
		? Image_Convert(plugin->addon, &checkAccessor, &checkAccessor, &run.settings, true)
		: Image_Manipulate(plugin->addon, &checkAccessor, &run.settings, true);
	if (status != B_OK) {
		fPluginRun.reset();
		BString text;
		text << strerror(status);
		(new BAlert("", text.String(), "OK"))->Go(NULL);
		return;
	}
	// its settings view (Image_MakeConfigurationView), in the Plugin dialog
	run.previewInvoker.SetMessage(new BMessage('UPDT'));
	run.configBits = ToBitmap(run.original);
	run.configAccessor = new BBitmapAccessor(run.configBits);
	run.configAccessor->SetDispose(false);
	BMessage ext(run.settings);
	ext.AddPointer("preview_invoker", &run.previewInvoker);
	ext.AddPointer("config_bitmap", run.configAccessor);
	ext.AddPointer("cancel_queue", &run.cancel);
	BView* config = NULL;
	Image_MakeConfigurationView(plugin->addon, &ext, &config);
	if (config == NULL) {
		FinishPluginSettings(true);
		return;
	}
	run.dialog = new TDialogManipConfig(plugin->name.String(), config, BMessenger(this));
	run.previewInvoker.SetTarget(run.dialog);
	run.dialog->RunDialog();
}


// the settings the add-on has now (GetConfigurationMessage), with the
// selection and colors
void
TPageView::CurrentPluginSettings()
{
	PluginRun& run = *fPluginRun;
	const ImagePlugin* plugin = EditableImagePlugin(run.index);
	BMessage settings(B_SIMPLE_DATA), capabilities;
	if (plugin != NULL
			&& Image_GetConfigurationMessage(plugin->addon, &settings, &capabilities) == B_OK)
		run.settings = settings;
	if (const IMPart* im = fDocument.ImagePart(run.part))
		AddRunSettings(run.settings, *im, run.floating);
}


void
TPageView::StartPluginJob(bool final)
{
	PluginRun& run = *fPluginRun;
	const ImagePlugin* plugin = EditableImagePlugin(run.index);
	if (plugin == NULL)
		return;
	// (anything a cancelled run left)
	while (BMessage* left = run.cancel.NextMessage())
		delete left;
	PluginJob* job = new PluginJob;
	job->view = BMessenger(this);
	job->addon = plugin->addon;
	job->converter = plugin->kind == 2;
	job->settings = run.settings;
	job->bits = run.original;
	job->cancel = &run.cancel;
	job->progress = BMessenger(this);
	job->withProgress = final;
	job->serial = ++run.serial;
	job->reply = final ? kMsgPluginDone : kMsgPluginPreviewDone;
	run.worker = spawn_thread(RunJob, plugin->name.String(), B_NORMAL_PRIORITY, job);
	if (run.worker < B_OK) {
		delete job;
		return;
	}
	resume_thread(run.worker);
}


// the dialog's OK (or a plug-in without settings): the settings kept for
// next time, the final run with its progress
void
TPageView::FinishPluginSettings(bool ok)
{
	PluginRun& run = *fPluginRun;
	if (run.worker >= B_OK) {
		run.cancel.AddMessage(new BMessage('_CNC'));
		status_t result;
		wait_for_thread(run.worker, &result);
		run.worker = -1;
	}
	// (the preview taken back)
	if (IMPart* im = fDocument.EditableImagePart(run.part)) {
		(run.floating ? im->floating : im->image) = run.original;
		ImageChanged(run.part, false);
	}
	if (!ok) {
		EndImagePlugin();
		return;
	}
	CurrentPluginSettings();
	if (ImagePlugin* plugin = EditableImagePlugin(run.index)) {
		plugin->config.reset(new BMessage(run.settings));
		run.status = new TStatusDialog(plugin->name.String(), NULL, NULL);
		run.status->RunDialog();
	}
	run.final = true;
	StartPluginJob(true);
}


void
TPageView::EndImagePlugin()
{
	if (!fPluginRun)
		return;
	if (fPluginRun->status != NULL)
		fPluginRun->status->Finish();
	delete fPluginRun->configAccessor;
	delete fPluginRun->configBits;
	fPluginRun.reset();
}


// the messages of a running plug-in
bool
TPageView::ImagePluginMessage(BMessage* message)
{
	switch (message->what) {
		case 'sI03': {
			RunImagePlugin(message->GetInt32("index", -1));
			return true;
		}
		case kMsgPluginAbout:
			if (fPluginRun)
				PluginAbout(fPluginRun->index);
			return true;
		case kMsgPluginPreview: {
			// PreviewThread: the preview taken back, or run again
			if (!fPluginRun || fPluginRun->final)
				return true;
			PluginRun& run = *fPluginRun;
			bool on = true;
			message->FindBool("preview", &on);
			CurrentPluginSettings();
			if (!on) {
				if (IMPart* im = fDocument.EditableImagePart(run.part)) {
					(run.floating ? im->floating : im->image) = run.original;
					ImageChanged(run.part, false);
				}
				run.previewShown = false;
				return true;
			}
			if (run.worker >= B_OK) {
				run.cancel.AddMessage(new BMessage('_CNC'));
				run.previewPending = true;
				return true;
			}
			StartPluginJob(false);
			return true;
		}
		case kMsgPluginPreviewDone: {
			ArchiveBitmap* bits = NULL;
			message->FindPointer("bits", (void**)&bits);
			std::unique_ptr<ArchiveBitmap> owned(bits);
			if (!fPluginRun)
				return true;
			PluginRun& run = *fPluginRun;
			run.worker = -1;
			if (run.final)
				return true;
			if (run.previewPending) {
				run.previewPending = false;
				StartPluginJob(false);
				return true;
			}
			IMPart* im = fDocument.EditableImagePart(run.part);
			if (bits != NULL && im != NULL && message->GetInt32("serial", 0) == run.serial) {
				PutBits(*im, run.floating, run.original, *bits);
				run.previewShown = true;
				ImageChanged(run.part, false);
			}
			return true;
		}
		case kMsgPluginOK:
		case kMsgPluginCancel:
			if (fPluginRun && !fPluginRun->final)
				FinishPluginSettings(message->what == kMsgPluginOK);
			return true;
		case 'STAT': {
			// the progress invoker: the bar, Cancel into the cancel queue
			if (!fPluginRun || fPluginRun->status == NULL)
				return true;
			float fraction;
			if (message->FindFloat("percentage", &fraction) == B_OK
					&& !fPluginRun->status->Update(fraction))
				fPluginRun->cancel.AddMessage(new BMessage('_CNC'));
			return true;
		}
		case kMsgPluginDone: {
			ArchiveBitmap* bits = NULL;
			message->FindPointer("bits", (void**)&bits);
			std::unique_ptr<ArchiveBitmap> owned(bits);
			if (!fPluginRun)
				return true;
			PluginRun& run = *fPluginRun;
			run.worker = -1;
			const ImagePlugin* plugin = EditableImagePlugin(run.index);
			status_t status = message->GetInt32("status", B_ERROR);
			if (status == B_OK && bits != NULL && plugin != NULL) {
				if (plugin->kind == 2) {
					// (a converter's bits: a new image document)
					BMessage made('s001');
					made.AddInt32("kind", 'BITS');
					made.AddPointer("image", owned.release());
					be_app->PostMessage(&made);
				} else if (IMPart* im = fDocument.EditableImagePart(run.part)) {
					// TIMPluginCommand: one step, named after the plug-in
					fUndo.BeginDocument(*run.before, plugin->name.String(), run.beforeView);
					im = fDocument.EditableImagePart(run.part);
					PutBits(*im, run.floating, run.original, *bits);
					fLastPlugin[run.part] = run.index;
					ImageChanged(run.part);
				}
			}
			EndImagePlugin();
			return true;
		}
	}
	return false;
}

int32
TPageView::LastImagePlugin() const
{
	int main = fDocument.MainPart();
	auto found = main >= 0 ? fLastPlugin.find(size_t(main)) : fLastPlugin.end();
	return found != fLastPlugin.end() ? found->second : -1;
}

} // namespace gobe
