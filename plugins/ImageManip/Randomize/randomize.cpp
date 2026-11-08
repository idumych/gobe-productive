/****************************************************************************
 * This is a plugin for the GIMP v 1.0 or later.  Documentation is
 * available at http://www.rru.com/~meo/gimp/ .
 *
 * Copyright (C) 1997-8 Miles O'Neal  <meo@rru.com>  http://www.rru.com/~meo/
 */

/* This file has been modified from the original; converted to the ImageManip API */
/* Scott Lindsey 8/1/99 */


/*
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
 ****************************************************************************/

/****************************************************************************
 * Randomize:
 *
 * randomize version 2.1 (1 May 1998, MEO)
 *
 * Please send any patches or suggestions to the author: meo@rru.com .
 * 
 * This plug-in adds a user-defined amount of randomization to an
 * image.  Variations include:
 * 
 *  - hurling (spewing random colors)
 *  - picking a nearby pixel at random
 *  - slurring (a crude form of melting)
 * 
 * In any case, for each pixel in the selection or image,
 * whether to change the pixel is decided by picking a
 * random number, weighted by the user's "randomization" percentage.
 * If the random number is in range, the pixel is modified.  Picking
 * one selects the new pixel value at random from the current and
 * adjacent pixels.  Hurling assigns a random value to the pixel.
 * Slurring sort of melts downwards; if a pixel is to be slurred,
 * there is an 80% chance the pixel above be used; otherwise, one
 * of the pixels adjacent to the one above is used (even odds as
 * to which it will be).
 * 
 * Picking, hurling and slurring work with any image type.
 * 
 * This plug-in's effectiveness varies a lot with the type
 * and clarity of the image being "randomized".
 * 
 * Hurling more than 75% or so onto an existing image will
 * make the image nearly unrecognizable.  By 90% hurl, most
 * images are indistinguishable from random noise.
 * 
 * The repeat count is especially useful with slurring.
 * 
 * TODO List
 * 
 *  - add a real melt function
 ****************************************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#include "ImageManipAddon.h"
#include "BitmapAccessor.h"
#include "Config.h"
#include "GIMPGlue.h"

const char addonName[] = "Randomize";
const char addonInfo[] = "Adds a random factor to an image.\nPorted by Scott Lindsey; original GIMP plugin by Miles O'Neal.";
const char addonCategory[] = "Noise";
int32 addonVersion = 100;

static void randomize(BitmapAccessor* sourceBitmap, BRect sel_rect, BInvoker* prog, const Prefs* prefs);


status_t
Manipulate(
	BitmapAccessor *sourceBitmap,
	BMessage *ioExtension,
	bool checkOnly)
{
	Prefs prefs;

	switch(sourceBitmap->ColorSpace())
	{
		case B_RGBA32_LITTLE:
			break;
		
		default:
			return B_NOT_ALLOWED;
	}

	if (checkOnly)
		return B_OK;

	gimp_init_glue(sourceBitmap, NULL, ioExtension);

	// If we can't find the selection rect extension, do the whole bitmap
	BRect sel_rect = sourceBitmap->Bounds();

	LoadPrefs(&prefs);
	BInvoker* prog;
	GetSettings(ioExtension, &prefs, &sel_rect, NULL, &prog);

	randomize(sourceBitmap, sel_rect, prog, &prefs);
	return B_OK;
}


/*********************************
 *
 *  PLUGIN-SPECIFIC CONSTANTS
 *
 ********************************/


/*
 *  progress meter update frequency
 */
#define PROG_UPDATE_TIME ((row % 10) == 0)

#define RNDM_HURL 0
#define RNDM_PICK 1
#define RNDM_SLUR 2

#define SEED_TIME 10
#define SEED_USER 11

#define ENTRY_WIDTH 75
#define SCALE_WIDTH 100

/*********************************
 *
 *  randomize_prepare_row()
 *
 *  Get a row of pixels.  If the requested row
 *  is off the edge, clone the edge row.
 *
 ********************************/

static void randomize_prepare_row(GIMPImage* image, uchar *data, int x, int y, int w)
{
    int b;

    if (y < 0) {
		y = 0;
    } else if (y > image->Height()) {
		y = image->Height();
    }

	image->GetRow(data, x, y, w);

/*
 *  Fill in edge pixels
 */
    for (b = 0; b < image->bpp; b++) {
        data[-image->bpp + b] = data[b];
        data[w * image->bpp + b] = data[(w - 1) * image->bpp + b];
    }
}

/*********************************
 *
 *  randomize()
 *
 *  Actually mess with the image.
 *
 ********************************/

void randomize(BitmapAccessor* sourceBitmap, BRect sel_rect, BInvoker* prog, const Prefs* prefs)
{
	bool cancelled = false;
	GIMPImage srcPR(sourceBitmap);
	GIMPImage destPR(sourceBitmap, true);
	GIMPImage destPR2(sourceBitmap, true);

    GIMPImage *sp, *dp, *tp;
    int bytes;
    uchar *dest, *d;
    uchar *prev_row, *pr;
    uchar *cur_row, *cr;
    uchar *next_row, *nr;
    uchar *tmp;
    int row, col, c2;
    int x1, y1, x2, y2;
    int cnt;
    int has_alpha, ind;
    int rndm_pct = (int) prefs->randomness;
    int need_rand = (rndm_pct < 100);    /* speeds things up when using 100% */
                                          /* but hardly affects anything else */

	if (prefs->type == RNDM_HURL)
		printf("HURL\n");
	if (prefs->type == RNDM_PICK)
		printf("PICK\n");
	if (prefs->type == RNDM_SLUR)
		printf("SLUR\n");

/*
 *  Get the input area. This is the bounding box of the selection in
 *  the image (or the entire image if there is no selection). Only
 *  operating on the input area is simply an optimization. It doesn't
 *  need to be done for correct operation. (It simply makes it go
 *  faster, since fewer pixels need to be operated on).
 */
	x1 = (int) sel_rect.left;
	x2 = (int) sel_rect.right;
	y1 = (int) sel_rect.top;
	y2 = (int) sel_rect.bottom;

/*
 *  Get the size of the input image. (This will/must be the same
 *  as the size of the output image.  Also get alpha info.
 */
    bytes = srcPR.bpp;
    has_alpha = true;
/*
 *  allocate row buffers
 */
    prev_row = (uchar *) malloc((x2 - x1 + 3) * bytes);
    cur_row = (uchar *) malloc((x2 - x1 + 3) * bytes);
    next_row = (uchar *) malloc((x2 - x1 + 3) * bytes);
    dest = (uchar *) malloc((x2 - x1 + 1) * bytes);

/*
 *  initialize the pixel regions
 */
    sp = &srcPR;
    dp = &destPR;
    tp = NULL;

    pr = prev_row + bytes;
    cr = cur_row + bytes;
    nr = next_row + bytes;

    for (cnt = 1; cnt <= prefs->repeats && !cancelled; cnt++) {
/*
 *  prepare the first row and previous row
 */
        randomize_prepare_row(sp, pr, x1, y1 - 1, (x2 - x1 + 1));
        randomize_prepare_row(sp, cr, x1, y1, (x2 - x1 + 1));
/*
 *  loop through the rows, applying the selected convolution
 */
        for (row = y1; row <= y2; row++) {
            /*  prepare the next row  */
            randomize_prepare_row(sp, nr, x1, row + 1, (x2 - x1 + 1));

            d = dest;
            ind = c2 = 0;
            for (col = 0; col < (x2 - x1 + 1) * bytes; col += bytes, c2 = col) {
                if ((! need_rand) || (((rand() % 100)) <= rndm_pct)) {
					int r = rand();
    /*
     *  ind has several uses here.  It is a counter, of course,
     *  but it is also the correct offset for previous alpha,
     *  and an indicator that we have hit the alpha channel.
     */
                    for (ind = 0; ind < bytes; ind++, c2++) {
						// Preserve the alpha for now.
						if (ind == 3)
						{
							*d++ = cr[3];
							continue;
						}
                        switch (prefs->type) {
    /*
     *  HURL
     *      Just assign a random value.
     */
                          case RNDM_HURL:
                            *d++ = rand() % 256;
                          break;
    /*
     *  PICK
     *      pick at random from a neighboring pixel.
     */
                          case RNDM_PICK:
                            switch (r % 9) {
                                case 0:
                                    *d++ = (int) pr[c2 - bytes];
                                    break;
                                case 1:
                                    *d++ = (int) pr[c2];
                                    break;
                                case 2:
                                    *d++ = (int) pr[c2 + bytes];
                                    break;
                                case 3:
                                    *d++ = (int) cr[c2 - bytes];
                                    break;
                                case 4:
                                    *d++ = (int) cr[c2];
                                    break;
                                case 5:
                                    *d++ = (int) cr[c2 + bytes];
                                    break;
                                case 6:
                                    *d++ = (int) nr[c2 - bytes];
                                    break;
                                case 7:
                                    *d++ = (int) nr[c2];
                                    break;
                                case 8:
                                    *d++ = (int) nr[c2 + bytes];
                                    break;
                            }
                          break;
    /*
     *  SLUR
     *      80% chance it's from directly above,
     *      10% from above left,
     *      10% from above right.
     */
                          case RNDM_SLUR:
                            switch (r % 10) {
                                case 0:
                                    *d++ = (int) pr[c2 - bytes];
                                    break;
                                case 9:
                                    *d++ = (int) pr[c2 + bytes];
                                    break;
                                default:
                                    *d++ = (int) pr[c2];
                                    break;
                            }
                          break;
							default:
								*d++ = 128;
                        }
                    }
/*
 *  Otherwise, this pixel was not selected for randomization,
 *  so use the current value.
 */
                } else {
                    for (ind = 0; ind < bytes; ind++, c2++) {
                        *d++ = (int) cr[c2];
                    }
                }
            }
/*
 *  Save the modified row, shuffle the row pointers, and every
 *  so often, update the progress meter.
 */
			dp->CopyRow(dest, x1, row, (x2 - x1 + 1));

            tmp = pr;
            pr = cr;
            cr = nr;
            nr = tmp;

            if (PROG_UPDATE_TIME)
			{
				float fraction = (float) row / (float) (y2 - y1);
				fraction /= prefs->repeats;
				fraction += (cnt - 1) / (float) prefs->repeats;
                cancelled = !gimp_progress_update(prog, fraction);
				if (cancelled)
					break;
			}
        }
/*
 *  if we have more cycles to perform, swap the src and dest Pixel Regions
 */
        if (cnt < prefs->repeats) {
            if (tp != NULL) {
                tp = dp;
                dp = sp;
                sp = tp;
            } else {
                tp = &srcPR;
                sp = &destPR;
                dp = &destPR2;
            }
        }
    }

	if (!cancelled)
	{
		srcPR.Copy(&destPR);
	
	    gimp_progress_update(prog, 100.0f);
	}
/*
 *  clean up after ourselves.
 */
    free(prev_row);
    free(cur_row);
    free(next_row);
    free(dest);
}
