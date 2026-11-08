/*
 * This is a plug-in for the GIMP.
 *
 * Copyright (C) 1996 Torsten Martinsen <bullestock@dk-online.dk>
 * Copyright (C) 1995 Spencer Kimball and Peter Mattis
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
 * $Id: ripple.cpp,v 1.3 1999/09/02 22:38:10 wombat Exp $
 *
 * @(GIMP)         = <plug-in ripple "Distorts/Ripple">
 * @(GIMP_DEP)     = <ripple.c>
 * @(GIMP_OBJ)     = <ripple.o>
 * @(GIMP_LIB)     = <m c>
 * @(GIMP_AUTHOR)  = <Torsten Martinsen>
 * @(GIMP_EMAIL)   = <bullestock@dk-online.dk>
 * @(GIMP_DESC)    = <Introduce ripples into an image.>
 * @(GIMP_VERSION) = <Revision: 1.7>
 * @(GIMP_URL)     = <http://www2.dk-online.dk/Users/Torsten_Martinsen/gimp/index.html>
 */

/* 
 * This plug-in displaces each row of the image according to a sine function,
 * a triangular function, or a random function.
 * (You can displace the columns by rotating the image
 * 90 degrees before applying the filter).
 */

/*
 * This file has been modified from the original.  It was rewritten
 * to use the BeOS libimagemanip API.  -- Scott Lindsey Aug 28, 1998
 */



#include <stdlib.h>
#include <time.h>
#include <math.h>
#include "GIMPGlue.h"
#include "gimp.h"
#include "ImageManipAddon.h"
#include "BitmapAccessor.h"
#include "Config.h"

const char addonName[] = "Ripple";
const char addonInfo[] = "Apply ripples to an image. Ported from the GIMP by Scott Lindsey <wombat@gobe.com>.  Original code by Torsten Martinsen";
const char addonCategory[] = "Distortion";
int32 addonVersion = 100;

/*
 * Change this if your system doesn't have random().
 */
#if 1
#define RANDOM	random
#define SRANDOM	srandom
#else
#define RANDOM	rand
#define SRANDOM	srand
#endif

#define NTYPES 3

typedef struct {
    int32 type[NTYPES];
    int32 period;
    int32 amplitude;
} ripple_params_t;

static void ripple(Image input, Image output, ripple_params_t params);

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
		int32 amount;
		if (!ioExtension->FindInt32("ripple/type", &amount))
			prefs.type = amount;
		if (!ioExtension->FindInt32("ripple/period", &amount))
			prefs.period = amount;
		if (!ioExtension->FindInt32("ripple/amplitude", &amount))
			prefs.amplitude = amount;
	}

	Image input = new GIMPImage(sourceBitmap);
	
    Image temp = gimp_temp_image();
    ripple_params_t params;
    params.period = prefs.period;
    params.amplitude = prefs.amplitude;
    params.type[0] = prefs.type == 0;
    params.type[1] = prefs.type == 1;
    params.type[2] = prefs.type == 2;

	ripple(input, temp, params);
	input->Copy(temp);

    gimp_free_image(temp);
    delete input;
	gimp_shutdown_glue();
	
	return B_OK;
}


static void
ripple(Image input, Image output, ripple_params_t params)
{
    int32 width, height;
    int32 channels, srcrowstride, dstrowstride;
    unsigned char *src_row, *dest_row;
    unsigned char *src, *dest;
    short row, col;
    int x1, y1, x2, y2;
    int shift, i, nc;

    gimp_image_area(input, &x1, &y1, &x2, &y2);

    width = gimp_image_width(input);
    height = gimp_image_height(input);
    channels = gimp_image_channels(input);
    nc = channels;
    
    src_row = gimp_image_data(input);
    dest_row = gimp_image_data(output);

	srcrowstride = gimp_image_stride(input);
	dstrowstride = gimp_image_stride(input);

    src_row += srcrowstride * y1 + x1 * channels;
    dest_row += dstrowstride * y1 + x1 * channels;

    if (params.type[2])
	SRANDOM(time(NULL));
    
    for (row = y1; row < y2; row++) {
	src = src_row;
	dest = dest_row;

	if (params.type[0]) {		/* sine */
	    shift = (int) (params.amplitude * sin((row-y1)*2.0*M_PI/params.period));
	} else if (params.type[1]) {	/* triangle */
	    shift = ((row-y1) % params.period)*2*params.amplitude/params.period;
	    if (shift > params.amplitude)
		shift = 2*params.amplitude - shift;
	} else {			/* random */
	    shift = (RANDOM() % (2*params.amplitude+1)) - params.amplitude;
	}

	if (shift < 0) {
	    shift = -shift;
	    src += shift * channels;
	    
	    if ((x2 - shift) > width) {
		for (col = x1; col < x2 - shift; col++) {
		    for (i = 0; i < nc; i++)
			*dest++ = *src++;
		}
		src -= channels;
		for (; col < x2; col++) {
		    for (i = 0; i < nc; i++)
			*dest++ = src[i];
		}
	    } else {
		for (col = x1; col < x2; col++) {
		    for (i = 0; i < nc; i++)
			*dest++ = *src++;
		}
	    }
	} else {
	    for (col = x1; col < x1 + shift; col++) {
		for (i = 0; i < nc; i++)
		    *dest++ = src[i];
	    }	    
	    for (; col < x2; col++) {
		for (i = 0; i < nc; i++)
		    *dest++ = *src++;
	    }		
	}
	
	src_row += srcrowstride;
	dest_row += dstrowstride;
	if ((row % 5) == 0)
	    gimp_do_progress(row, y2-y1);
    }

}
