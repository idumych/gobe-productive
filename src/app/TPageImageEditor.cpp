// TPageView's external image editor (Parts/ImageProcessing: TPartIM's
// 'sI08', EditorLooper, the BBP messages): with "Use External Editor" on in
// the preferences, Image > Edit Externally… (and the image's context menu)
// hands the image to the editor whose signature they name, launching it if
// it isn't running, in a 'BPob' message: "BBitmap" (the image archived),
// "target" (a looper of the image's that hears from the editor) and "name";
// the editor answers 'BPob' with its own "target", sends the edited bits
// back in 'BPsb' ("BBitmap"), put down over the image as one step, and
// 'BPbc' when it's done with them.
#include "TPageView.h"

#include <Bitmap.h>
#include <Looper.h>
#include <MenuItem.h>
#include <PopUpMenu.h>
#include <Roster.h>
#include <Window.h>

#include "GobeResources.h"
#include "TPreferences.h"

namespace gobe {

namespace {

// EditorLooper: the editor's messages, passed on to the image's view
class EditorLooper : public BLooper {
public:
	EditorLooper(BMessenger view)
		:
		BLooper("BBP looper", 10, 100),
		fView(view)
	{
	}

	virtual void MessageReceived(BMessage* message)
	{
		fView.SendMessage(message);
	}

private:
	BMessenger fView;
};


const ResourceFile&
ImageResources()
{
	return GobeResources::Part("ImageProcessing");
}

} // namespace


// TFrameIM::SetUpContextMenu: Edit Externally… (STR# 2, 6) while the
// preferences use an editor
bool
TPageView::ImageContextMenu(BPoint where)
{
	int main = fDocument.MainPart();
	size_t part;
	int x, y;
	if (main < 0 || fDocument.ImagePart(size_t(main)) == NULL || !ImageAt(where, part, x, y)
			|| part != size_t(main))
		return false;
	if (!AppPreferences().imageExternalEditor || Window() == NULL)
		return true;
	BPopUpMenu* menu = new BPopUpMenu("context", false, false);
	menu->AddItem(new BMenuItem(IndexString(ImageResources(), 2, 6).c_str(),
		new BMessage('sI08')));
	BPoint screen = ConvertToScreen(BPoint(where.x * fZoom, where.y * fZoom));
	BMenuItem* chosen = menu->Go(screen, false, false, BRect(screen.x - 5, screen.y - 5,
		screen.x + 5, screen.y + 5), false);
	if (chosen != NULL)
		EditImageExternally();
	delete menu;
	return true;
}


// 'sI08': the image to the editor, in 'BPob'
void
TPageView::EditImageExternally()
{
	int main = fDocument.MainPart();
	const IMPart* im = main >= 0 ? fDocument.ImagePart(size_t(main)) : NULL;
	if (im == NULL || im->image.Empty())
		return;
	if (fImageEditor == NULL) {
		fImageEditor = new EditorLooper(BMessenger(this));
		fImageEditor->Run();
	}
	BBitmap bitmap(BRect(0, 0, im->image.Width() - 1, im->image.Height() - 1),
		color_space(im->image.colorSpace));
	if (bitmap.InitCheck() != B_OK)
		return;
	bitmap.ImportBits(im->image.bits.data(), im->image.bits.size(), im->image.bytesPerRow, 0,
		color_space(im->image.colorSpace));
	BMessage archive;
	bitmap.Archive(&archive);
	BMessage open('BPob');
	open.AddMessenger("target", BMessenger(fImageEditor));
	open.AddMessage("BBitmap", &archive);
	open.AddString("name", Window() != NULL ? Window()->Title() : "");
	const std::string& signature = AppPreferences().imageEditor;
	BMessenger editor(signature.c_str());
	if (editor.IsValid())
		editor.SendMessage(&open);
	else
		ErrorAlert(be_roster->Launch(signature.c_str(), &open));
}


// the editor's messages
bool
TPageView::ImageEditorMessage(BMessage* message)
{
	switch (message->what) {
		case 'BPob':
			// (its own messenger, kept while the looper is there)
			if (fImageEditor == NULL
					|| message->FindMessenger("target", &fImageEditorTarget) != B_OK)
				fImageEditorTarget = BMessenger();
			return true;
		case 'BPbc':
			// (done with the image)
			fImageEditorTarget = BMessenger();
			EndImageEditor();
			return true;
		case 'BPsb': {
			// TSetBitsCommand: the bits over the image, from its corner
			BMessage archive;
			int main = fDocument.MainPart();
			if (main < 0 || fDocument.ImagePart(size_t(main)) == NULL
					|| message->FindMessage("BBitmap", &archive) != B_OK)
				return true;
			BBitmap bitmap(&archive);
			if (!bitmap.IsValid())
				return true;
			BBitmap converted(bitmap.Bounds(), B_RGBA32);
			if (converted.ImportBits(&bitmap) != B_OK)
				return true;
			size_t part = size_t(main);
			fUndo.BeginDocument(fDocument, "", CurrentViewState());
			IMPart* im = fDocument.EditableImagePart(part);
			ArchiveBitmap& target = im->floating.bits.empty() ? im->image : im->floating;
			int width = std::min(target.Width(), int(converted.Bounds().Width()) + 1);
			int height = std::min(target.Height(), int(converted.Bounds().Height()) + 1);
			const uint8* from = (const uint8*)converted.Bits();
			for (int y = 0; y < height; y++)
				memcpy(&target.bits[size_t(y) * target.bytesPerRow],
					from + size_t(y) * converted.BytesPerRow(), size_t(width) * 4);
			ImageChanged(part);
			return true;
		}
	}
	return false;
}


void
TPageView::EndImageEditor()
{
	if (fImageEditor != NULL && fImageEditor->Lock())
		fImageEditor->Quit();
	fImageEditor = NULL;
}

} // namespace gobe
