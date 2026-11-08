
/* BCG 2.4 -- image filter plug-in for The Gimp image manipulation program
 * Copyright (C) 1996 Adam D. Moss  -  aspirin@tigerden.com
 * Copyright (C) 1996 Ian Tester - 94024831@athene.mit.csu.edu.au
 * Copyright (C) 1996 Federico Mena Quintero - quartic@polloux.fciencias.unam.mx
 *
 * THIS FILE HAS BEEN MODIFIED FROM THE ORIGINAL - 8/5/99 Scott Lindsey
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
 */

/* 
 * This filter allows the image gamma, brightness, and contrast to
 * be adjusted.  The gamma of each channel may be set independantly.
 *
 *      I have added the feature of an "RGB lock" when used on RGB images.
 *      When set, any change to either of the R, G, or B gamma values
 *      causes the other two gamma values to change also.
 *      - Ian Tester
 *
 * NOTE: This is written for The GIMP version 0.54 - it might not
 *   compile or run on other versions.
 *
 */

/* Some days after this plug-in's first version was finished, I wrote
 * another brightness and contrast plug-in, which used a (traditional)
 * additive brightness and stretch contrast algorithm.  This is different
 * with what this plug-in originally had; a very neat multiplicative
 * brightness and biased contrast algorithm.  My version of the plug-in
 * was never released to the public.
 * 
 * So a few weeks later (i.e. now) I decided to integrate them together.
 * The dialog box is a bit ugly, IMO, but the plug-in now provides a lot
 * of functionality.  I don't know what Adam will say, as I have made
 * *my* algorithms the default... :) This is because it's what most
 * people are accustomed to.
 * 
 * Adam's algorithms frequently give more pleasing results when used
 * against photographs, however, since they tend not to discard image
 * information --- values do not get clamped.  Choose the algorithms
 * which serve your purposes best.
 *
 * The code could use some cleaning up.  It may have bugs, although I
 * have not detected any.
 *
 * - Federico Mena Quintero (quartic@polloux.fciencias.unam.mx)
 */

/* Changes in version 2.1, by Quartic
 *
 * Arghh... fixed stupid bugs with the Auto Apply function, which I
 * introduced in version 2.0.  Sorry, guys :)
 */

/* Changes in version 2.3, by Adam
 *
 * Rearranged the dialog box a little, Fixed overflow and underflow in
 * Quartic's additive/subtractive brightness adjustment... and made my
 * algorithms the default again, teehee.  =)
 * Added 'Reset All' button.
 * Fixed a lot of auto-apply bugs.
 */

/* Changes in version 2.4, by Quartic
 *
 * Hehehe... Adam sent his own 2.2 to the mailing list, so we had two
 * versions... his 2.2 and my 2.3 :) So I integrated them.  The dialog
 * is Adam's redesigned one (much better than the original!).  I
 * cleaned up the algorithms, and hopefully they are now Bug-Free (tm).
 *
 * I cleaned up all the source code, since it was getting kinda messy.
 * Adam did some minor bug-fixes to the auto-apply code.  Let's hope
 * we all got it right this time :)
 */
 

#include <math.h>
#include <stdio.h>
#include "GIMPGlue.h"
#include "gimp.h"
#include "ImageManipAddon.h"
#include "BitmapAccessor.h"
#include "Config.h"

const char addonName[] = "Brightness/Contrast/Gamma";
const char addonInfo[] = "Allows the image gamma, brightness, and contrast to be adjusted.  The gamma of each channel may be set independantly.\nPorted from the GIMP by Scott Lindsey.\nOriginal code by Federico Mena Quintero, Adam D. Moss, and Ian Tester.";
const char addonCategory[] = "Color";
int32 addonVersion = 100;

/* Some useful macros */

#define CLAMP(a, x, b) (((x) <= (a)) ? (a) : (((x) <= (b)) ? (x) : (b)))


/***** Prototypes *****/

static int do_gamma(int tmp, float howmuch);
static int brightness(int tmp, int howmuch, bool b_additive);
static int contrast(int tmp, int howmuch, bool c_stretch);

static void bcg(BitmapAccessor* sourceBitmap, BRect sel_rect, Prefs* prefs);


status_t
Manipulate(
	BitmapAccessor *sourceBitmap,
	BMessage *ioExtension,
	bool checkOnly)
{
	color_space depth = sourceBitmap->ColorSpace();

	switch(depth)
	{
		case B_RGB32_LITTLE:
		case B_RGBA32_LITTLE:
		case B_RGB24_LITTLE:
		case B_RGB32_BIG:
		case B_RGBA32_BIG:
		case B_RGB24_BIG:
		case B_GRAYSCALE_8_BIT:
			break;
		default:
			return B_NOT_ALLOWED;
	}

	if (checkOnly)
		return B_OK;

	gimp_init_glue(sourceBitmap, NULL, ioExtension);

	// If we can't find the selection rect extension, do the whole bitmap
	BRect sel_rect = sourceBitmap->Bounds();

	Prefs prefs;
	LoadPrefs(&prefs);
	GetSettings(ioExtension, &prefs, &sel_rect, NULL, NULL);

	bcg(sourceBitmap, sel_rect, &prefs);

	return B_OK;
}


/*****/

static int
do_gamma(int tmp, float howmuch)
{
	/* This piece is basically from Spencer & Peter's gamma.c, but
	 * without the overflow bug.  ;) :P =)
	 */

	double one_over_gamma, ind, v;

	one_over_gamma = 1.0 / howmuch;

	ind = (double) tmp / 255.0;
	v = 255.0 * pow(ind, one_over_gamma);

	return (int) (v + 0.5);
} /* do_gamma */


/*****/

static int
brightness(int tmp, int32 howmuch, bool b_additive)
{
	if (b_additive)
		return (tmp + howmuch);
	else {
		if (howmuch < 0)
			return (tmp * 256) / (65536 / (256 + howmuch));
		else if (howmuch > 0)
			return tmp + ((255 - tmp) * 256 / (65536 / howmuch));
		else
			return tmp;
	} /* else */
} /* brightness */


/*****/

static int
contrast(int tmp, int32 howmuch, bool c_stretch)
{
	double ii;
	int    i0, i1, o0, o1;
	int    result;

	if (c_stretch) {
		if (howmuch >= 0) {
			i0 = howmuch;
			i1 = 255 - howmuch;
			o0 = 0;
			o1 = 255;
		} else {
			i0 = 0;
			i1 = 255;
			o0 = -howmuch;
			o1 = 255 + howmuch;
		} /* else */

		tmp = (o1 - o0) * (tmp - i0) / (i1 - i0) + o0;

		return tmp;
	} else {
		howmuch = -howmuch;

		tmp = CLAMP(0, tmp, 255);

		if (howmuch > 0) {
			if (tmp > 127)
				result = 255 - tmp;
			else
				result = tmp;
			
			ii = 127.0 * pow((double) result / 127.5, (double) (127 - howmuch) / 127.0);
			
			result = (int) ii;

			if (tmp > 127)
				result = 255 - result;
		} else if (howmuch < 0) {
			if (tmp > 127)
				result = 255 - tmp;
			else
				result = tmp;
			
			if (howmuch == -127)
				ii = (result < 127) ? 0.0 : 127.0;
			else
				ii = 127.0 * pow((double) result / 127.5, 127.0 / (double) (howmuch + 127));

			result = (int) ii;

			if (tmp > 127)
				result = 255 - result;
		} else
			result = tmp;

		return result;
	} /* else */
} /* contrast */


/*****/

static void
bcg(BitmapAccessor* sourceBitmap, BRect sel, Prefs* prefs)
{
	int32 width, height;
	int32 channels, rowstride;
	unsigned char *src_row, *dest_row;
	unsigned char *src, *dest;
	int row, col;
	int x1, y1, x2, y2;
	int tmp;
	int table[256][4];
	int skip = -1;

	color_space depth = sourceBitmap->ColorSpace();

	int j;
	for (int i = 0; i < 4; i++)
	{
		j = i;
		switch(depth)
		{
			case B_RGB32_LITTLE:
				channels = 4;
				skip = 3;
				if (i == 3)
					continue;
				j = 2 - j;
				break;
			case B_RGBA32_LITTLE:
				channels = 4;
				if (i == 3)
					goto doalpha;
				j = 2 - j;
				break;
			case B_RGB24_LITTLE:
				channels = 3;
				if (i == 3)
					continue;
				j = 2 - j;
				break;
			case B_RGB32_BIG:
				channels = 4;
				skip = 0;
				if (i == 0)
					continue;
				j = j - 1;
				break;
			case B_RGBA32_BIG:
				channels = 4;
				if (i == 0)
				{
					j = 3;
					goto doalpha;
				}
				j = j - 1;
				break;
			case B_RGB24_BIG:
				channels = 3;
				if (i == 0)
					continue;
				// j is correct
				break;
			case B_GRAYSCALE_8_BIT:
				channels = 1;
				if (i > 0)
					continue;
				// j is correct
				break;
		}
		for (tmp = 0; tmp < 256; tmp++) {
			int32 v = contrast(brightness(do_gamma(tmp, prefs->gamma[j]), prefs->brightness, prefs->b_additive), prefs->contrast, prefs->c_stretch);
			table[tmp][i] = CLAMP(0, v, 255);
		} /* for */
		
		continue;
doalpha:
		// Alpha channel doesn't do brightness/contrast
		for (tmp = 0; tmp < 256; tmp++) {
			int32 v = do_gamma(tmp, prefs->gamma[j]);
			table[tmp][i] = CLAMP(0, v, 255);
		} /* for */
	}

	x1 = (int) sel.left;
	x2 = (int) sel.right;
	y1 = (int) sel.top;
	y2 = (int) sel.bottom;

	int cols = channels * (x2 - x1 + 1);
	
	for (row = y1; row <= y2; row++)
	{
		int32 stride;
		uchar* p = (uchar*) sourceBitmap->AccessBits(BRect(x1, row, x2, row), &stride);

		/*  Calculate across the scanline  */
		for (col = x1; col <= x2; col++)
		{
			int ch;

			for (ch = 0; ch < channels; ch++)
			{
				if (ch == skip)
					p++;
				else
				*p++ = (unsigned char) (table[*p][ch]);
			} /* for */
		} /* for */
	} /* for */ 
} /* bcg */

