/*
 * 'Solarize' plugin for the GIMP.
 *
 * Copyright (C) 1995 Spencer Kimball and Peter Mattis
 * Copyright (C) 1996 Torsten Martinsen
 */

/* This file has been modified from the original; converted to the ImageManip API */
/* The "lunarize" effect was added, as per Ian Tester's Lunarize plugin */
/* Scott Lindsey 8/1/99 */


/* This program is free software; you can redistribute it and/or modify
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
 * $Id: solarize.cpp,v 1.2 1999/08/02 18:55:21 wombat Exp $
 *
 * @(GIMP)         = <plug-in solarize "Map/Solarize">
 * @(GIMP_DEP)     = <solarize.c>
 * @(GIMP_OBJ)     = <solarize.o>
 * @(GIMP_LIB)     = <c>
 * @(GIMP_AUTHOR)  = <Torsten Martinsen>
 * @(GIMP_EMAIL)   = <bullestock@dk-online.dk>
 * @(GIMP_DESC)    = <Solarize an image.>
 * @(GIMP_VERSION) = <Revision: 1.8>
 * @(GIMP_URL)     = <http://www2.dk-online.dk/Users/Torsten_Martinsen/gimp/index.html>
 */

/*
 * This filter produces the 'solarization' effect seen when exposing a 
 * photographic film to light during the development process.
 * This is done by inverting all pixels above the set threshold level.
 *
 * If the option 'Maximize Contrast' is enabled, Solarize tries to avoid
 * reducing the contrast of the image. This usually gives better results.
 *
 * This filter also (sort of) works with indexed images.
 */

#include <stdlib.h>
#include "GIMPGlue.h"
#include "gimp.h"
#include "ImageManipAddon.h"
#include "BitmapAccessor.h"
#include "Config.h"

const char addonName[] = "Solarize";
const char addonInfo[] = "This filter produces the 'solarization' effect seen when exposing a photographic film to light during the development process. Ported from the GIMP by Scott Lindsey <wombat@gobe.com>.  Original code by Torsten Martinsen";
const char addonCategory[] = "Color";
int32 addonVersion = 102;

static void solarize(Image, Image, int32, bool, bool);

status_t
Manipulate(
	BitmapAccessor *sourceBitmap,
	BMessage *ioExtension,
	bool checkOnly)
{
	switch(sourceBitmap->ColorSpace())
	{
		case B_RGB32:
		case B_RGBA32:
		case B_RGB24:
		case B_RGB24_BIG:
		case B_GRAYSCALE_8_BIT:
			break;
		default:
			return B_NOT_ALLOWED;
	}

	if (checkOnly)
		return B_OK;

	Prefs prefs;
	LoadPrefs(&prefs);
	gimp_init_glue(sourceBitmap, NULL, ioExtension);

	if (ioExtension)
	{
		int32 threshold;
		bool stretch, lunar;

		if (!ioExtension->FindInt32("solarize/threshold", &threshold))
			prefs.threshold = threshold;
		if (!ioExtension->FindBool("solarize/stretch", &stretch))
			prefs.stretch = stretch;
		if (!ioExtension->FindBool("solarize/lunar", &lunar))
			prefs.lunar = lunar;
	}
	Image input = new GIMPImage(sourceBitmap);
	
    Image temp = gimp_temp_image();
	solarize(input, temp, prefs.threshold, prefs.stretch, prefs.lunar);
	input->Copy(temp);

    gimp_free_image(temp);
    delete input;
	
	return B_OK;
}



static void
solarize(Image input, Image output, int32 threshold, bool stretch, bool lunar)
{
    int32 width, height;
    int32 channels, srcrowstride, dstrowstride;
    unsigned char *src_row, *dest_row;
    unsigned char *src, *dest;
    int x1, y1, x2, y2, row, col, v, xx1, xx2;
    

    gimp_image_area(input, &x1, &y1, &x2, &y2);
    width = gimp_image_width(input);
    height = gimp_image_height(input);
    channels = gimp_image_channels(input);

    src_row = gimp_image_data(input);
    dest_row = gimp_image_data(output);

    srcrowstride = gimp_image_stride(input);
    dstrowstride = gimp_image_stride(output);

    if (gimp_image_type(input) != INDEXED_IMAGE) {
	xx1 = x1*channels;
	xx2 = x2*channels;
	
	src_row += srcrowstride * y1 + x1*channels;
	dest_row += dstrowstride * y1 + x1*channels;

	for (row = y1; row < y2; row++) {
	    src = src_row;
	    dest = dest_row;

	    for (col = xx1; col < xx2; col++) {
	    		unsigned char u;
			if (stretch) {
			    v = *src++;
			    if (v > threshold)
				v = (255-v)*255/(255-threshold);
			    else
				v = v*255/threshold;
			    u = v;
			} else
			    u = *src > threshold ? 255 - *src++ : *src++;
			
			*dest++ = lunar ? u ^ 0xFF : u;
		}
	    
	    src_row += srcrowstride;
	    dest_row += dstrowstride;
	}
    }

    /* Copy alpha channel */
    if ((channels == 1) || (channels == 3))
	return;
    
    src_row = gimp_image_data(input) + srcrowstride * y1 + (x1+1) * channels - 1;
    dest_row = gimp_image_data(output) + dstrowstride * y1 + (x1+1) * channels - 1;
    for (row = y1; row < y2; ++row) {
	src = src_row;
	dest = dest_row;
	for (col = x1; col < x2; ++col) {
	    *dest = *src;
	    dest += channels;
	    src += channels;
	}
	dest_row += dstrowstride;
	src_row += srcrowstride;
    }
}

