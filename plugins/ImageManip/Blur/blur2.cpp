/*
 * This is a plug-in for the GIMP.
 *
 * Copyright (C) 1995 Spencer Kimball and Peter Mattis
 * Copyright (C) 1996 Torsten Martinsen
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

/*
 * This filter is like the standard 'blur', except that it uses
 * a convolution kernel of variable size.
 *
 * I am greatly indebted to Johan Klockars <d8klojo@dtek.chalmers.se>
 * for supplying the algorithm used here, which is of complexity O(1).
 * Compared with the original naive algorithm, which is O(k^2) (where
 * k is the kernel size), it gives _massive_ speed improvements.
 * The code is quite simple, too.
 */

/* This code has been modified from the original.  It has had several bug fixes relating to
 * edge conditions and selection bounds.  It has been altered to the LibImageManip API.
 * Scott Lindsey Aug 21, 1998.
 *
 */

#include "GIMPGlue.h"
#include "gimp.h"
#include <stdio.h>
#include "Config.h"

#ifndef MIN
#define MIN(a,b)	(((a) < (b)) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a,b)	(((a) > (b)) ? (a) : (b))
#endif


static void blur(Image input, int kernelSize);
static void blur_rgb(Image input, int chans, int kernelSize);
static void blur_grey(Image input, int chans, int kernelSize);

static void scale_callback(int, void *, void *);
static void ok_callback(int, void *, void *);
static void cancel_callback(int, void *, void *);

/* Image Manip stuff */
#include "ImageManipAddon.h"

const char addonName[] = "Variable Blur";
const char addonInfo[] = "Blur an image. Ported from the GIMP by Scott Lindsey <wombat@gobe.com>; original code by Torsten Martinsen.";
const char addonCategory[] = "Blur";
int32 addonVersion = 105;

status_t Manipulate(BitmapAccessor *sourceBitmap, BMessage *ioExtension, bool checkOnly)
{
	Prefs prefs;
	
	switch(sourceBitmap->ColorSpace())
	{
		case B_RGB24_BIG:
		case B_RGB24_LITTLE:
		case B_RGB32_LITTLE:
		case B_RGBA32_LITTLE:
		case B_GRAY8:
			break;
		default:
			return B_NOT_ALLOWED;
	}
	
	if (checkOnly)
		return B_OK;

	LoadPrefs(&prefs);

	gimp_init_glue(sourceBitmap, NULL, ioExtension);
	if (ioExtension)
	{
		int32 tmp;
		if (!ioExtension->FindInt32("blur/kernel_size", &tmp))
			prefs.kernelSize = tmp;
	}

	Image input = new GIMPImage(sourceBitmap);
	blur(input, prefs.kernelSize);
	
	return B_OK;
}


static void
blur(Image input, int kernelSize)
{
    switch (gimp_image_type(input)) {
    case RGB_IMAGE:
	blur_rgb(input, 3, kernelSize);
	break;
    case RGBA_IMAGE:
	blur_rgb(input, 4, kernelSize);
	break;
    case GRAY_IMAGE:
	blur_grey(input, 1, kernelSize);
	break;
    case GRAYA_IMAGE:
	blur_grey(input, 2, kernelSize);
	break;
    default:
	break;
    }
}


static void
blur_rgb(Image input, int chans, int kernelSize)
{
    int32 width, height, srcstride, deststride;
    unsigned char *src, *src_row, *dest, *dest_row, *s, *d;
    Image temp;
    int x, y, r, g, b, i, sx1, sy1, sx2, sy2, x1, y1, x2, y2, div, alpha;


    gimp_image_area(input, &sx1, &sy1, &sx2, &sy2);
    width = gimp_image_width(input);
    height = gimp_image_height(input);
    alpha = chans-3;
   
    src = gimp_image_data(input);
    srcstride = gimp_image_stride(input);
    temp = gimp_temp_image();
    dest = gimp_image_data(temp);
    deststride = gimp_image_stride(temp);

    /* Include edges if possible */
    x1 = MAX(sx1-kernelSize/2, 0);
    x2 = MIN(sx2+kernelSize/2, width);
    y1 = MAX(sy1-kernelSize/2, 0);
    y2 = MIN(sy2+kernelSize/2, height);

    /* Vertical blur */
    src_row = src + srcstride * y1 + x1 * chans;
    dest_row = dest + deststride * (y1+kernelSize/2) + x1 * chans;
    for (x = x1; x < x2; ++x) {
	s = src_row;
	d = dest_row;
	r = g = b = 0;
	for (i = 0; i < kernelSize; ++i) {
	    r += s[0];
	    g += s[1];
	    b += s[2];
	    s += srcstride;
	}
	d[0] = r/kernelSize;
	d[1] = g/kernelSize;
	d[2] = b/kernelSize;
	d += deststride;
	s = src_row;
	for (y = y1; y < y2-kernelSize; ++y) {
	    r = r - s[0] + s[kernelSize*srcstride];
	    g = g - s[1] + s[kernelSize*srcstride+1];
	    b = b - s[2] + s[kernelSize*srcstride+2];
	    d[0] = r/kernelSize;
	    d[1] = g/kernelSize;
	    d[2] = b/kernelSize;
	    s += srcstride;
	    d += deststride;
	}
	dest_row += chans;
	src_row += chans;
	if ((x % 10) == 0)
	    gimp_do_progress(x/2, x2-x1);
    }

    /* Do remaining top pixels, if any */
    if (y1-kernelSize/2 < 0) {
	src_row = src;
	dest_row = dest;
	for (x = x1; x < x2; ++x) {
	    s = src_row + (x1 * chans);
	    d = dest_row + (x1 * chans);
	    r = g = b = 0;
	    div = kernelSize/2+1;
	    for (i = 0; i < div; ++i) {
		r += s[0];
		g += s[1];
		b += s[2];
		s += srcstride;
	    }
	    d[0] = r/div;
	    d[1] = g/div;
	    d[2] = b/div;
	    d += deststride;
	    for (y = 0; y < kernelSize/2; ++y) {
		r = r + s[0];
		g = g + s[1];
		b = b + s[2];
		++div;
		d[0] = r/div;
		d[1] = g/div;
		d[2] = b/div;
		s += srcstride;
		d += deststride;
	    }
	    dest_row += chans;
	    src_row += chans;
	}
    }
    /* Do remaining bottom pixels, if any */
    if (y2+kernelSize/2 > height) {
	src_row = src + srcstride * (height-1);
	dest_row = dest + deststride * (height-1);
	for (x = x1; x < x2; ++x) {
	    s = src_row + (x1 * chans);
	    d = dest_row + (x1 * chans);
	    r = g = b = 0;
	    div = kernelSize/2+1;
	    for (i = 0; i < div; ++i) {
		r += s[0];
		g += s[1];
		b += s[2];
		s -= srcstride;
	    }
	    d[0] = r/div;
	    d[1] = g/div;
	    d[2] = b/div;
	    d -= deststride;
	    for (y = 0; y < kernelSize/2; ++y) {
		r = r + s[0];
		g = g + s[1];
		b = b + s[2];
		++div;
		d[0] = r/div;
		d[1] = g/div;
		d[2] = b/div;
		s -= srcstride;
		d -= deststride;
	    }
	    dest_row += chans;
	    src_row += chans;
	}
    }

    src = dest;
    deststride = srcstride;
    // No output buffer.  It's the same as the input buffer, so this would work:
    // dest = gimp_image_data(input);
    // but it's friendlier to use the appropriate accessor via gimp_image_scanline()
    // this is still a trivial enough effect that doing this is possible.
    // A large number of plugins will need to simply create an output buffer, and
    // then call input->Copy(output) when done.

    /* Horizontal blur */
    src_row = src + srcstride * sy1 + x1 * chans;
    for (y = sy1; y < sy2; ++y) {
	s = src_row;
	dest_row = gimp_image_scanline(input, y, (x1+kernelSize/2), (x2-kernelSize/2));
	
	d = dest_row;
	r = g = b = 0;
	for (i = 0; i < kernelSize; ++i) {
	    r += *s++;
	    g += *s++;
	    b += *s++;
	    s += alpha;
	}
	*d++ = r/kernelSize;
	*d++ = g/kernelSize;
	*d++ = b/kernelSize;
	d += alpha;
	s = src_row;
	for (x = x1; x < x2-kernelSize; ++x) {
	    r = r - s[0] + s[kernelSize*chans];
	    ++s;
	    g = g - s[0] + s[kernelSize*chans];
	    ++s;
	    b = b - s[0] + s[kernelSize*chans];
	    ++s;
	    s += alpha;
	    *d++ = r/kernelSize;
	    *d++ = g/kernelSize;
	    *d++ = b/kernelSize;
	    d += alpha;
	
	}
	src_row += srcstride;
	if ((y % 10) == 0)
	    gimp_do_progress((y+y2-y1)/2, y2-y1);
    }

    /* Do remaining left pixels, if any */
    if (x1-kernelSize/2 < 0) {
	src_row = src + (sy1 * srcstride);
	for (y = sy1; y < sy2; ++y) {
	    s = src_row;
		dest_row = gimp_image_scanline(input, y, 0, width);
	    d = dest_row;
	    r = g = b = 0;
	    div = kernelSize/2+1;
	    for (i = 0; i < div; ++i) {
		r += *s++;
		g += *s++;
		b += *s++;
		s += alpha;
	    }
	    *d++ = r/div;
	    *d++ = g/div;
	    *d++ = b/div;
	    d += alpha;
	    for (x = 0; x < kernelSize/2; ++x) {
		r += *s++;
		g += *s++;
		b += *s++;
		s += alpha;
		++div;
		*d++ = r/div;
		*d++ = g/div;
		*d++ = b/div;
		d += alpha;
	    }
	    src_row += srcstride;
	}
    }

    /* Do remaining right pixels, if any */
    if (x2+kernelSize/2 > width) {
	src_row = src + width * chans + sy1 * srcstride;
	for (y = sy1; y < sy2; ++y) {
	    s = src_row;
		dest_row = gimp_image_scanline(input, y, 0, width) + width * chans;
	    d = dest_row;
	    r = g = b = 0;
	    div = kernelSize/2+1;
	    for (i = 0; i < div; ++i) {
		s -= alpha;
		b += *--s;
		g += *--s;
		r += *--s;
	    }
	    d -= alpha;
	    *--d = b/div;
	    *--d = g/div;
	    *--d = r/div;
	    for (x = 0; x < kernelSize/2; ++x) {
		s -= alpha;
		b += *--s;
		g += *--s;
		r += *--s;
		++div;
		d -= alpha;
		*--d = b/div;
		*--d = g/div;
		*--d = r/div;
	    }
	    src_row += srcstride;
	}
    }

    gimp_free_image(temp);
}

static void
blur_grey(Image input, int /*chans*/, int kernelSize)
{

    int32 width, height, rowstride;
    unsigned char *src, *src_row, *dest, *dest_row, *s, *d;
    Image temp;
    int x, y, r, i, sx1, sy1, sx2, sy2, x1, y1, x2, y2, div;


    gimp_image_area(input, &sx1, &sy1, &sx2, &sy2);

    width = gimp_image_width(input);
    height = gimp_image_height(input);
    rowstride = width;
    
    src = gimp_image_data(input);
    temp = gimp_temp_image();
    dest = gimp_image_data(temp);
    
    /* Include edges if possible */
    x1 = MAX(sx1-kernelSize/2, 0);
    x2 = MIN(sx2+kernelSize/2, width);
    y1 = MAX(sy1-kernelSize/2, 0);
    y2 = MIN(sy2+kernelSize/2, height);
    
    /* Vertical blur */
    src_row = src + rowstride * y1 + x1;
    dest_row = dest + rowstride * (y1+kernelSize/2) + x1;
    for (x = x1; x < x2; ++x) {
	s = src_row;
	d = dest_row;
	r = 0;
	for (i = 0; i < kernelSize; ++i) {
	    r += s[0];
	    s += rowstride;
	}
	d[0] = r/kernelSize;
	d += rowstride;
	s = src_row;
	for (y = y1; y < y2-kernelSize; ++y) {
	    r = r - s[0] + s[kernelSize*rowstride];
	    d[0] = r/kernelSize;
	    s += rowstride;
	    d += rowstride;
	}
	dest_row += 1;
	src_row += 1;
	if ((x % 10) == 0)
	    gimp_do_progress(x/2, x2-x1);
    }

    /* Do remaining top pixels, if any */
    if (y1-kernelSize/2 < 0) {
	src_row = src;
	dest_row = dest;
	for (x = x1; x < x2; ++x) {
	    s = src_row;
	    d = dest_row;
	    r = 0;
	    div = kernelSize/2+1;
	    for (i = 0; i < div; ++i) {
		r += s[0];
		s += rowstride;
	    }
	    d[0] = r/div;
	    d += rowstride;
	    for (y = 0; y < kernelSize/2; ++y) {
		r = r + s[0];
		++div;
		d[0] = r/div;
		s += rowstride;
		d += rowstride;
	    }
	    dest_row += 1;;
	    src_row += 1;;
	}
    }

    /* Do remaining bottom pixels, if any */
    if (y2+kernelSize/2 > height) {
	src_row = src + rowstride * (height-1);
	dest_row = dest + rowstride * (height-1);
	for (x = x1; x < x2; ++x) {
	    s = src_row;
	    d = dest_row;
	    r = 0;
	    div = kernelSize/2+1;
	    for (i = 0; i < div; ++i) {
		r += s[0];
		s -= rowstride;
	    }
	    d[0] = r/div;
	    d -= rowstride;
	    for (y = 0; y < kernelSize/2; ++y) {
		r = r + s[0];
		++div;
		d[0] = r/div;
		s -= rowstride;
		d -= rowstride;
	    }
	    dest_row += 1;
	    src_row += 1;
	}
    }

    src = dest;
    dest = gimp_image_data(input);	// Blur back onto the original

    /* Horizontal blur */
    src_row = src + rowstride * y1 + x1;
    dest_row = dest + rowstride * y1 + (x1+kernelSize/2);
    for (y = y1; y < sy2; ++y) {
	s = src_row;
	d = dest_row;
	r = 0;
	for (i = 0; i < kernelSize; ++i) {
	    r += *s;
	    s += 1;
	}
	*d = r/kernelSize;
	d += 1;
	s = src_row;
	for (x = x1; x < x2-kernelSize; ++x) {
	    r = r - s[0] + s[kernelSize];
	    *d = r/kernelSize;
	    s += 1;
	    d += 1;
	}
	dest_row += rowstride;
	src_row += rowstride;
	if ((y % 10) == 0)
	    gimp_do_progress((y+y2-y1)/2, y2-y1);
    }
    
    /* Do remaining left pixels, if any */
    if (x1-kernelSize/2 < 0) {
	src_row = src;
	dest_row = dest;
	for (y = y1; y < sy2; ++y) {
	    s = src_row;
	    d = dest_row;
	    r = 0;
	    div = kernelSize/2+1;
	    for (i = 0; i < div; ++i) {
		r += *s;
		s += 1;
	    }
	    *d = r/div;
	    d += 1;
	    for (x = 0; x < kernelSize/2; ++x) {
		r += *s;
		++div;
		*d = r/div;
		s += 1;
		d += 1;
	    }
	    dest_row += rowstride;
	    src_row += rowstride;
	}
    }

    /* Do remaining right pixels, if any */
    if (x2+kernelSize/2 > width) {
	src_row = src + width;
	dest_row = dest + width;
	for (y = y1; y < sy2; ++y) {
	    s = src_row;
	    d = dest_row;
	    r = 0;
	    div = kernelSize/2+1;
	    for (i = 0; i < div; ++i) {
		s -= 1;
		r += *s;
	    }
	    d -= 1;
	    *d = r/div;
	    for (x = 0; x < kernelSize/2; ++x) {
		s -= 1;
		d -= 1;
		r += *s;
		++div;
		*d = r/div;
	    }
	    dest_row += rowstride;
	    src_row += rowstride;
	}
    }
    gimp_free_image(temp);
}

