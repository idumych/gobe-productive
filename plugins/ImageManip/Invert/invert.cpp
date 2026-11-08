// Invert: red, green and blue inverted, the alpha kept. Gobe shipped this
// add-on without its source; rewritten for the Gobe Productive port from the
// shipped one's behaviour (Invert 1.0.2, by Scott Lindsey).
#include <Message.h>

#include "BitmapAccessor.h"
#include "ImageManipAddon.h"

const char addonName[] = "Invert";
const char addonInfo[] = "Invert an image, using RGB inversion.  \xC2\xA9" "1998 by Scott Lindsey <wombat@gobe.com>.";
const char addonCategory[] = "Color";
int32 addonVersion = 102;


status_t
Manipulate(BitmapAccessor* sourceBitmap, BMessage* ioExtension, bool checkOnly)
{
	color_space space = sourceBitmap->ColorSpace();
	int32 bytes;
	uint32 mask32 = 0;
	switch (space) {
		case B_RGB32:
		case B_RGBA32:
			bytes = 4;
			mask32 = 0x00ffffff;		// B, G, R (not A)
			break;
		case B_RGB32_BIG:
		case B_RGBA32_BIG:
			bytes = 4;
			mask32 = 0xffffff00;
			break;
		case B_GRAY8:
		case B_CMAP8:
			bytes = 1;
			break;
		default:
			return B_NOT_ALLOWED;
	}
	if (checkOnly)
		return B_OK;
	BRect r;
	if (ioExtension == NULL || ioExtension->FindRect("selection_rect", &r) != B_OK)
		r = sourceBitmap->Bounds();
	for (float y = r.top; y <= r.bottom; y++) {
		int32 rowBytes;
		uint8* p = (uint8*)sourceBitmap->AccessBits(BRect(r.left, y, r.right, y), &rowBytes);
		for (float x = r.left; x <= r.right; x++, p += bytes) {
			if (bytes == 4)
				*(uint32*)p ^= mask32;
			else
				*p = uint8(~*p);
		}
	}
	return B_OK;
}


status_t
GetConfigMessage(BMessage*, BMessage* ioCapability)
{
	ioCapability->AddInt32("selection_rect", B_RECT_TYPE);
	return B_OK;
}
