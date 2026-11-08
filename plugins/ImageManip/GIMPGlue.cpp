// GIMPGlue.cpp

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <GraphicsDefs.h>
#include <Message.h>
#include <MessageQueue.h>
#include <Invoker.h>
#include "GIMPGlue.h"
#include "BitmapAccessor.h"
#include "BBitmapAccessor.h"

static color_space MapColorSpace(ImageType type);

// BOGUS re-entrancy problems with globals
BitmapAccessor* SrcBitmap, *DestBitmap;
BRect SelBounds(-1, -1, 0, 0);
BInvoker* ProgressInvoker;
BMessageQueue* CancelQueue;

rgb_color fore = {0, 0, 0, 255}, back = {255, 255, 255, 255};

bool WasCancelled;



// This file contains glue to assist converting old GIMP filters
// to LibImageManip add-ons.  It is not a complete replacement for
// libgimp, and makes many assumptions about the porting process.
// It mixes in various of the GIMP API versions

void gimp_init_glue(BitmapAccessor* src, BitmapAccessor* dst, BMessage* ioExtension)
{
	WasCancelled = false;
	ProgressInvoker = NULL;
	SelBounds = src->Bounds();
	if (ioExtension)
	{
		BRect r;
		if (!ioExtension->FindRect("selection_rect", &r))
			SelBounds = r;
		ioExtension->FindPointer("progress_invoker", (void**) &ProgressInvoker);
		ioExtension->FindPointer("cancel_queue", (void**) &CancelQueue);
		printf("CancelQueue: %p\n", CancelQueue);
		
		const rgb_color* rgb;
		ssize_t size;
		
		if (!ioExtension->FindData("foreground_color", B_RGB_COLOR_TYPE, (const void**) &rgb, &size))
			fore = *rgb;
		if (!ioExtension->FindData("background_color", B_RGB_COLOR_TYPE, (const void**) &rgb, &size))
			back = *rgb;
	}
	SrcBitmap = src;
	DestBitmap = dst;
}

void gimp_shutdown_glue()
{
	ProgressInvoker = 0;
	CancelQueue = 0;
	SrcBitmap = DestBitmap = 0;
}

void gimp_free_image (Image im)
{
	delete im;
}

//void gimp_init_progress(char *title)
//{
//	if (ProgressInvoker)
//	{
//		BMessage msg('INIT');
//		msg.AddString("title", title);
//		ProgressInvoker->Invoke();
//	}
//}

void gimp_progress_init(const char* str)
{
	if (ProgressInvoker)
	{
		BMessage msg(*ProgressInvoker->Message());
		msg.AddString("label", str);
		ProgressInvoker->Invoke(&msg);
	}
}


bool gimp_do_progress(int complete, int total)
{
	if (WasCancelled)
		return false;

	if (CancelQueue && NULL != CancelQueue->FindMessage(B_CANCEL, 0))
	{
		CancelQueue->RemoveMessage(CancelQueue->FindMessage(B_CANCEL, 0));
		printf("Plugin cancelled\n");
		WasCancelled = true;
		return false;
	}

	if (ProgressInvoker)
	{
		BMessage msg(*ProgressInvoker->Message());
		msg.AddFloat("percentage", (float) complete / total);
		ProgressInvoker->Invoke(&msg);
	}
	
	return true;
}

bool gimp_progress_update(BInvoker* invoker, float fraction)
{
	if (WasCancelled)
		return false;

	if (CancelQueue && NULL != CancelQueue->FindMessage(B_CANCEL, 0))
	{
		printf("Plugin cancelled\n");
		WasCancelled = true;
		return false;
	}

	if (!invoker)
		return true;
	
	BMessage msg(*invoker->Message());
	msg.AddFloat("percentage", fraction);
	invoker->Invoke(&msg);
	
	return true;
}


Image gimp_temp_image()
{
	return new GIMPImage(SrcBitmap, true);
}

Image gimp_new_image (char *, int32 width, int32 height, ImageType type)
{
	BRect bounds(0, 0, width - 1, height - 1);

	if (!DestBitmap->CreateBitmap(bounds, MapColorSpace(type)))
		return NULL;

	return new GIMPImage(DestBitmap, false);
}

uchar* gimp_image_data(Image im)
{
	
	return static_cast<uchar*>(im->mBitmap->AccessBits(im->mBitmap->Bounds(), &im->mRowBytes));
}

uchar* gimp_image_scanline(Image im, int y, int x1, int x2)
{
	int32 rowBytes;
	return static_cast<uchar*>(im->mBitmap->AccessBits(BRect(x1, y, x2, y), &rowBytes));
}


int32 gimp_image_width (Image im)
{
	return (int32) im->mBitmap->Bounds().Width() + 1;
}

int32 gimp_image_height (Image im)
{
	return (int32) im->mBitmap->Bounds().Height() + 1;
}

int32 gimp_image_stride (Image im)
{
	return (int32) im->mRowBytes;
}


int32 gimp_image_channels (Image im)
{
	switch(im->mBitmap->ColorSpace())
	{
		case B_RGB32:
		case B_RGBA32:
			return 4;
		case B_GRAY8:
			return 1;
	}

	return 0;
}

void gimp_image_area (Image /*im*/, int *x1, int *y1, int *x2, int *y2)
{
	BRect r(SelBounds);
	*x1 = (int) r.left;
	*y1 = (int) r.top;
	*x2 = (int) r.right + 1;
	*y2 = (int) r.bottom + 1;
}

ImageType gimp_image_type(Image im)
{
	switch(im->mBitmap->ColorSpace())
	{
		case B_RGB32:
		case B_RGBA32:
			return RGBA_IMAGE;
		case B_GRAY8:
			return GRAY_IMAGE;
	}

	return UNKNOWN_IMAGE;
}

void gimp_foreground_color (unsigned char *red, unsigned char *green, unsigned char *blue)
{
	*red = fore.red;
	*green = fore.green;
	*blue = fore.blue;
}


void gimp_background_color (unsigned char *red, unsigned char *green, unsigned char *blue)
{
	*red = back.red;
	*green = back.green;
	*blue = back.blue;
}

#pragma mark -

GIMPImage::GIMPImage(BitmapAccessor* bitmap, bool output)
{
	mReadOnly = !output;
	if (mReadOnly)
		mBitmap = bitmap;
	else
	{
		mBitmap = new BBitmapAccessor;
		mBitmap->CreateBitmap(bitmap->Bounds(), bitmap->ColorSpace());
#if 0
		int32 rowbytes;
		ulong* p = (ulong*) mBitmap->AccessBits(bitmap->Bounds(), &rowbytes);
		for (int y = 0; y <= bitmap->Bounds().bottom; y++)
		{
			ulong* q = p;
			for (int x = 0; x <= bitmap->Bounds().right; x++)
			{
				*q = 0x0000FFFF;
				q++;
			}
			p += rowbytes / 4;
		}
#endif
	}
	mRowBytes = -1;
	bpp = (int32) bitmap->BytesPerPixel();
}

GIMPImage::~GIMPImage()
{
	if (!mReadOnly)
		delete mBitmap;
}

void GIMPImage::Copy(GIMPImage* im, BRect bounds)
{
	// Copy the contents of im to mBitmap, presumably scanline by scanline
	// Use this if you need an output buffer and have to copy the result back
	// onto the input (for a manipulator).

	BRect r(bounds);

	for (float y = bounds.top; y <= bounds.bottom; y++)
	{
		
		r.top = r.bottom = y;

		int32 rowBytes;
		void* src = im->mBitmap->AccessBits(r, &rowBytes);
		void* dst = mBitmap->AccessBits(r, &rowBytes);
		float pixelWidth = mBitmap->BytesPerPixel();
		memcpy(dst, src, (int) (pixelWidth * (r.Width() + 1)));
		im->mBitmap->BitsNotChanged();
	}
}

void GIMPImage::CopyRow(const uchar* bits, int x1, int row, int count)
{
	int x2 = x1 + count - 1;
	float pixelWidth = mBitmap->BytesPerPixel();
	BRect r(x1, row, x2, row);

	int32 rowBytes;
	uchar* dst = (uchar*) mBitmap->AccessBits(r, &rowBytes);
	
	count = (int) (count * pixelWidth);
	for (int x = 0; x < count; x++)
		*dst++ = *bits++;
}

void GIMPImage::GetRow(uchar* bits, int x1, int row, int count) const
{
	if (row < 0)
		row = 0;

	int x2 = x1 + count - 1;
	float pixelWidth = mBitmap->BytesPerPixel();
	BRect r(x1, row, x2, row);

	int32 rowBytes;
	const uchar* src = (const uchar*) mBitmap->AccessBits(r, &rowBytes);
	
	count = (int) (count * pixelWidth);
	for (int x = 0; x < count; x++)
		*bits++ = *src++;

	mBitmap->BitsNotChanged();
}


void GIMPImage::SetCol(const uchar* bits, int col, int y1, int count)
{
	int y2 = y1 + count - 1;
	int pixelWidth = (int) mBitmap->BytesPerPixel();
	BRect r(col, y1, col, y2);

	int32 rowBytes;
	const uchar* dst = (uchar*) mBitmap->AccessBits(r, &rowBytes);
	
	if (pixelWidth == sizeof(ulong))
	{
		for (int y = 0; y < count; y++)
		{
			*(ulong*)dst = *(ulong*) bits;
			dst += rowBytes;
			bits += sizeof(ulong);
		}
	}
	else
	{
		for (int y = 0; y <= count; y++)
		{
			for (int x = 0; x < pixelWidth; x++)
				*(ulong*)dst[x] = *(ulong*) bits++;
			dst += rowBytes;
		}
	}
}


void GIMPImage::GetCol(uchar* bits, int col, int y1, int count) const
{
	if (col < 0)
		col = 0;

	int y2 = y1 + count - 1;
	int pixelWidth = (int) mBitmap->BytesPerPixel();
	BRect r(col, y1, col, y2);

	int32 rowBytes;
	const uchar* src = (const uchar*) mBitmap->AccessBits(r, &rowBytes);
	
	if (pixelWidth == sizeof(ulong))
	{
		for (int y = 0; y < count; y++)
		{
			*(ulong*) bits = *(ulong*)src;
			src += rowBytes;
			bits += sizeof(ulong);
		}
	}
	else
	{
		for (int y = 0; y <= count; y++)
		{
			for (int x = 0; x < pixelWidth; x++)
				*(ulong*) bits++ = *(ulong*)src[x];
			src += rowBytes;
		}
	}

	mBitmap->BitsNotChanged();
}


void GIMPImage::GetRect(uchar* bits, int x, int y, int w, int h) const
{
	int32 sstride, dstride;
	BRect b(mBitmap->Bounds());
	BRect r2(x, y, x + w, y + h);
	BRect r;
	r = r2 & b;
	const uchar* p = (const uchar*) mBitmap->AccessBits(r, &sstride);
	float bpp = mBitmap->BytesPerPixel();
	dstride = (int) (w * bpp);

	int rb = (int) (bpp * (r.Width() + 1.0f));
	for (int y = (int) r.top; y <= r.bottom; y++)
	{
		const uchar* srcRow = p + (y - (int) r.top) * sstride;
		uchar* dstRow = bits + (int) ((y - (int) r2.top) * dstride + (int) (r.left - r2.left) * bpp);
		
		for (int x = 0; x < rb; x++)
			*dstRow++ = *srcRow++;
	}
	
	mBitmap->BitsNotChanged();
}

void GIMPImage::SetRect(const uchar* bits, int x, int y, int w, int h) const
{
	int32 sstride, dstride;
	BRect b(mBitmap->Bounds());
	BRect r2(x, y, x + w, y + h);
	BRect r;
	r = r2 & b;
	uchar* p = (uchar*) mBitmap->AccessBits(r, &sstride);
	float bpp = mBitmap->BytesPerPixel();
	dstride = (int) (w * bpp);

	int rb = (int) (bpp * (r.Width() + 1.0f));
	for (int y = (int) r.top; y <= r.bottom; y++)
	{
		const uchar* srcRow = bits + (int) ((y - (int) r2.top) * dstride + (int) (r.left - r2.left) * bpp);
		uchar* dstRow = p + (y - (int) r.top) * sstride;
		
		for (int x = 0; x < rb; x++)
			*dstRow++ = *srcRow++;
	}
}


#pragma mark -

color_space MapColorSpace(ImageType type)
{
	switch(type)
	{
		case DEFAULT_IMAGE:
			return SrcBitmap->ColorSpace();
		case RGB_IMAGE:	// Not quite correct.  Plugins assume that this is 24-bit color, not 32.  They must be corrected.
			return B_RGB32;
		case GRAY_IMAGE:
			return B_GRAY8;
		case INDEXED_IMAGE:
			return B_CMAP8;
		case RGBA_IMAGE:
			return B_RGBA32;
		default:
			return B_NO_COLOR_SPACE;
	}
}

