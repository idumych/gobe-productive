// The About box.
#include "TDialogSplash.h"

#include <Bitmap.h>
#include <Button.h>
#include <Message.h>
#include <Screen.h>
#include <String.h>
#include <View.h>

#include "GobeResources.h"

namespace gobe {

namespace {

const uint32 kMsgOK = 'sD01';


// a BMPm resource: an archived BBitmap
BBitmap*
LogoBitmap()
{
	for (const ResourceFile::Resource* r : GobeResources::File().All('BMPm')) {
		if (r->name != "GPlogo")
			continue;
		BMessage archive;
		if (archive.Unflatten((const char*)r->data.data()) != B_OK)
			return NULL;
		BBitmap* bitmap = new BBitmap(&archive);
		if (bitmap->InitCheck() != B_OK) {
			delete bitmap;
			return NULL;
		}
		return bitmap;
	}
	return NULL;
}


// TSplashItem::TSplashItem: the last word of APPV's short version string
BString
Version()
{
	const ResourceFile::Resource* r = GobeResources::File().Find('APPV', 1);
	if (r == NULL || r->data.size() < 20 + 64)
		return "";
	BString shortInfo((const char*)r->data.data() + 20, 64);
	int32 space = shortInfo.FindLast(' ');
	if (space >= 0)
		shortInfo.Remove(0, space + 1);
	return shortInfo;
}


class TSplashItem : public BView {
public:
	TSplashItem(BRect frame, BBitmap* logo)
		:
		BView(frame, "splash", B_FOLLOW_ALL, B_WILL_DRAW),
		fLogo(logo),
		fVersion(Version()),
		fCopyright(GobeResources::IndexString(5, 44))
	{
		SetViewColor(255, 255, 255);
	}

	virtual ~TSplashItem()
	{
		delete fLogo;
	}

	// TSplashItem::Draw: the logo; the version in black, as large as fits
	// right of 160, at the bottom right; the copyright below it, 10 point
	virtual void Draw(BRect)
	{
		BRect bounds = Bounds();
		if (fLogo != NULL)
			DrawBitmap(fLogo, BPoint(5, 5));
		SetHighColor(0, 0, 0);
		BFont font;
		GetFont(&font);
		float size = 23, x;
		do {
			size -= 1;
			font.SetSize(size);
			SetFont(&font);
			x = bounds.right - 10 - StringWidth(fVersion.String()) - 7;
		} while (x < 160 && size > 6);
		float y = fLogo != NULL ? fLogo->Bounds().Height() + 26 : 30;
		DrawString(fVersion.String(), BPoint(x, y));
		font.SetSize(10);
		SetFont(&font);
		DrawString(fCopyright.String(),
			BPoint(bounds.right - 10 - StringWidth(fCopyright.String()) - 7, y + 15));
	}

private:
	BBitmap* fLogo;
	BString fVersion, fCopyright;
};

} // namespace


TDialogSplash::TDialogSplash()
	:
	BWindow(BRect(0, 0, 100, 100), "", B_MODAL_WINDOW_LOOK, B_MODAL_APP_WINDOW_FEEL,
		B_NOT_RESIZABLE | B_NOT_CLOSABLE | B_WILL_ACCEPT_FIRST_CLICK)
{
	// TDialogSplash::InitPostRecalc: the logo's size, 10 wider, 55 + 37
	// taller (with the OK button), a third of the way down the screen
	BBitmap* logo = LogoBitmap();
	BRect r = logo != NULL ? logo->Bounds() : BRect(0, 0, 300, 100);
	float width = r.Width() + 10, height = r.Height() + 55 + 37;
	ResizeTo(width - 1, height - 1);
	BRect screen = BScreen(this).Frame();
	MoveTo((screen.Width() - width) / 2, (screen.Height() - height) / 3);
	TSplashItem* item = new TSplashItem(Bounds(), logo);
	AddChild(item);
	BButton* ok = new BButton(BRect(width - 70, height - 37, width - 15, height - 15), "button",
		GobeResources::IndexString(2, 0).String(), new BMessage(kMsgOK));
	item->AddChild(ok);
	ok->MakeDefault(true);
}


void
TDialogSplash::MessageReceived(BMessage* message)
{
	if (message->what == kMsgOK) {
		Quit();
		return;
	}
	BWindow::MessageReceived(message);
}

} // namespace gobe
