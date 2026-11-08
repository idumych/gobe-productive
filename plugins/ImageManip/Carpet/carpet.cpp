/*
  carpet.cpp

  based on carpet.c

 *  by Heinz Sollich <heinz@ventoux.in-berlin.de>
 *  06/1998
 *
 *  makes a carpet-like structure
 *
 *  some code adapted from canvas.c, noisify.c and others
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
 * THIS FILE HAS BEEN MODIFIED FROM THE ORIGINAL - Scott Lindsey 8/1/99
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "ImageManipAddon.h"
#include "BitmapAccessor.h"
#include "Config.h"
#include "GIMPGlue.h"

const char addonName[] = "Carpet";
const char addonInfo[] = "Sort of like applying the image to a carpet's surface.  Ported by Scott Lindsey <wombat@gobe.com>, from a GIMP plugin by Heinz Sollich.";
const char addonCategory[] = "Effects";
int32 addonVersion = 100;

void get_col(BitmapAccessor* bitmap, uchar* p, int32 y, int32 x1, int32 x2);
void set_col(BitmapAccessor* bitmap, const uchar* p, int32 y, int32 x1, int32 x2);

#ifndef PI
#define PI 3.141592653589793238462643383279
#endif
#ifndef PI_2
#define PI_2 (PI*2)
#endif


/*
 * Return a Gaussian (aka normal) random variable. (from noisify.c)
 */
static inline double gauss ()
{
  int i;
  double sum = 0.0;

  for (i = 0; i < 4; i++)
    sum += rand () & 0x7FFF;

  return sum * 5.28596089837e-5 - 3.46410161514;
}

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

	BInvoker* prog;
	PrefsFromMessage(&prefs, ioExtension, &prog);

	gimp_init_glue(sourceBitmap, NULL, ioExtension);

	int bpp = (int) sourceBitmap->BytesPerPixel();
	bool has_alpha = bpp == 2 || bpp == 4;
	int bytes = bpp - has_alpha;
	BRect bounds = sourceBitmap->Bounds(), selection;
	selection = bounds;
	
	if (ioExtension)
	{
		BRect r;
		if (!ioExtension->FindRect("selection_rect", &r))
			selection = r;
	}

	int n =         prefs.depth;             /* depth of the structure */
	int m =         prefs.noise;             /* noise */
	int max_shift = prefs.max_shift;         /* max. shift */
	float fmax =      2.0 * max_shift + 1.0;
	int reps =      prefs.repeats;           /* number of shifts */
	int shift;


	int32 rowBytes;
	int y1, y2;
	int x1, x2;
	int image_height = bounds.IntegerHeight() + 1;
	int image_width = bounds.IntegerWidth() + 1;
	int select_height = selection.IntegerHeight() + 1;
	int select_width = selection.IntegerWidth() + 1;

    int v0[4] = { 2,  1,  0,  1 },
        v1[4] = { 1,  0, -1,  0 },
        v2[4] = { 0, -1, -2, -1 },
        v3[4] = { 1,  0, -1,  0 };
	

	y1 = (int) selection.top;
	y2 = (int) selection.bottom;
	x1 = (int) selection.left;
	x2 = (int) selection.right;

	int row, col, noise, pvalue, i;
	uchar* d, *p, *p0, *p1, *p2, *p3;
	status_t err = B_OK;

	gimp_progress_init("Generating carpet");

	/* make structure */
	uchar* cr = (uchar *) malloc((image_width+4) * bpp);
	uchar* dest = (uchar *) malloc(image_width * bpp);
	for (row=y1; row<=y2; row+=4) /* mod 0 */
	{
		bounds.top = bounds.bottom = row;
		memcpy(cr, sourceBitmap->AccessBits(bounds, &rowBytes), (bounds.IntegerWidth() + 1) * bpp);
		sourceBitmap->BitsNotChanged();
		d = dest + x1 * bpp;
		for (col=x1; col<=x2; col++)
		{
			noise = (int)(m * gauss());
			for (i=0; i<bytes; i++) {
				pvalue = cr[col * bpp + i] + n * v0[col%4] + noise;
				if (pvalue < 0)
					*d = 0;
				else if (pvalue > 255)
					*d = 255;
				else
					*d = pvalue;
				d++;
			}
			if ( has_alpha ) {
				*d = cr[col * bpp + bytes];
				d++;
			}
		}
		selection.top = selection.bottom = row;
		memcpy(sourceBitmap->AccessBits(selection, &rowBytes), dest + x1 * bpp, (x2 - x1 + 1) * bpp);
	}

	gimp_progress_update(prog, 0.25 * 0.33);

	for (row=y1+2; row<=y2; row+=4)  /* mod 2 */
	{
		bounds.top = bounds.bottom = row;
		memcpy(cr, sourceBitmap->AccessBits(bounds, &rowBytes), (bounds.IntegerWidth() + 1) * bpp);
		sourceBitmap->BitsNotChanged();
		d = dest + x1 * bpp;
		for (col=x1; col<=x2; col++) {
			noise = (int) (m * gauss());
			for (i=0; i<bytes; i++) {
				pvalue = cr[col * bpp + i] + n * v2[col%4] + noise;
				if (pvalue < 0)
					*d = 0;
				else if (pvalue > 255)
					*d = 255;
				else
					*d = pvalue;
				d++;
			}
			if ( has_alpha ) {
				*d = cr[col * bpp + bytes];
				d++;
			}
		}
		selection.top = selection.bottom = row;
		memcpy(sourceBitmap->AccessBits(selection, &rowBytes), dest + x1 * bpp, (x2 - x1 + 1) * bpp);
	}

	gimp_progress_update(prog, 0.25 * 0.66);

	for (row=y1+3; row<=y2; row+=4) /* mod 3 */
	{
		bounds.top = bounds.bottom = row;
		memcpy(cr, sourceBitmap->AccessBits(bounds, &rowBytes), (bounds.IntegerWidth() + 1) * bpp);
		sourceBitmap->BitsNotChanged();
		d = dest + x1 * bpp;
		for (col=x1; col<=x2; col++) {
			noise = (int) (m * gauss());
			for (i=0; i<bytes; i++) {
				pvalue = cr[col * bpp + i] + n * v3[col%4] + noise;
				if (pvalue < 0)
					*d = 0;
				else if (pvalue > 255)
					*d = 255;
				else
					*d = pvalue;
				d++;
			}
			if ( has_alpha ) {
				*d = cr[col * bpp + bytes];
				d++;
			}
		}
		selection.top = selection.bottom = row;
		memcpy(sourceBitmap->AccessBits(selection, &rowBytes), dest + x1 * bpp, (x2 - x1 + 1) * bpp);

	}

	free(cr);
	free(dest);

	if (!gimp_progress_update(prog, 0.25))
		return B_CANCELED;


	int totalreps = reps;

	gimp_progress_init("Applying repetitions");


	// BOGUS - this doesn't use the selection correctly.

	while (reps-- > 0 && B_OK == err)
	{
		float percent = 0.25 + 0.75 * (totalreps - reps + 1) / (float) totalreps;
		float frac = 0.75 / (float) totalreps;

		/* horizontal shift */
		p = (uchar *) malloc((image_width + 2 * max_shift) * bpp);
		p0 = p + max_shift * bpp;
		p2 = p0 + image_width * bpp;
		p1 = p2 - max_shift * bpp;
		dest = (uchar *) malloc((x2-x1+1) * bpp);
		for (row=y1; row<=y2; row++)
		{
			bounds.top = bounds.bottom = row;
			memcpy(p0, sourceBitmap->AccessBits(bounds, &rowBytes), (bounds.IntegerWidth() + 1) * bpp);
			memcpy(p, p1, max_shift * bpp);
			memcpy(p2, p0, max_shift * bpp);
			shift = -max_shift + (int) (fmax * rand() / (RAND_MAX + 1.0));
			p3 = p0 + (x1 + shift) * bpp;
			d = dest;
			for (col=x1; col<=x2; col++)
			{
				for (i=0; i<bpp; i++)
				{
					*d = *p3;
					d++; p3++;
				}
			}
			selection.top = selection.bottom = row;
			memcpy(sourceBitmap->AccessBits(selection, &rowBytes), dest, (x2 - x1 + 1) * bpp);
			if ((row % 10) == 0 && !gimp_progress_update(prog, percent + 0.15 * frac * (double) (row - y1) / (double) (y2 - y1)))
			{
				err = B_CANCELED;
				break;
			}
		}
		free(p);
		free(dest);
		
		percent += 0.15 * frac;

		if (B_OK != err)
			break;
		
		/* vertical shift */
		p = (uchar *) malloc((image_height + 2 * max_shift) * bpp);
		p0 = p + max_shift * bpp;
		p2 = p0 + image_height * bpp;
		p1 = p2 - max_shift * bpp;
		dest = (uchar *) malloc((y2-y1) * bpp);
		for (col=x1; col<=x2; col++)
		{
			get_col(sourceBitmap, p0, col, 0, image_height - 1);
			memcpy(p, p1, max_shift * bpp);
			memcpy(p2, p0, max_shift * bpp);
			shift = -max_shift + (int) (fmax * rand() / (RAND_MAX + 1.0));
			p3 = p0 + (y1 + shift) * bpp;
			d = dest;
			for (row=y1; row<=y2; row++)
			{
				for (i=0; i<bpp; i++)
				{
					*d = *p3;
					d++; p3++;
				}
			}
			set_col(sourceBitmap, dest, col, y1, y2);
			if ((col % 10) == 0 && !gimp_progress_update(prog, percent + 0.85 * frac * (double) (col - x1) / (double) (x2 - x1)))
			{
				err = B_CANCELED;
				break;
			}
		}
		free(p);
		free(dest);
	} /* while (reps > 0 */


	return err;
}

void get_col(BitmapAccessor* bitmap, uchar* dest, int32 x, int32 y1, int32 y2)
{
	BRect bounds(x, y1, x, y2);
	int32 rowBytes, bpp = (int32) bitmap->BytesPerPixel();
	const uchar* srcrow = (const uchar*) bitmap->AccessBits(bounds, &rowBytes);
	const uchar* src;
	
	for (int32 y = y1; y <= y2; y++)
	{
		src = srcrow;
		for (int b = 0; b < bpp; b++)
		{
			*dest++ = *src++;
		}
		srcrow += rowBytes;
	}
	
	bitmap->BitsNotChanged();
}

void set_col(BitmapAccessor* bitmap, const uchar* src, int32 x, int32 y1, int32 y2)
{
	BRect bounds(x, y1, x, y2);
	int32 rowBytes, bpp = (int32) bitmap->BytesPerPixel();
	uchar* destrow = (uchar*) bitmap->AccessBits(bounds, &rowBytes);
	uchar* dest;
	
	for (int32 y = y1; y <= y2; y++)
	{
		dest = destrow;
		for (int b = 0; b < bpp; b++)
		{
			*dest++ = *src++;
		}
		destrow += rowBytes;
	}
}

