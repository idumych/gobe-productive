/* antialias.c  
 * Anti-Aliasing plug-in for the GIMP v1.0.
 * by Kevin Turner <kevint@poboxes.com>
 * http://www.poboxes.com/kevint/gimp/
 */

/* I adopted multiply_ and seperate_alpha from gauss_iir.c.  This
 * program was inspired by pnmalias.c ((C) 1992 by Alberto Accomazzi,
 * Smithsonian Astrophysical Observatory).  */

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
 */

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <algobase.h>

#include "ImageManipAddon.h"
#include "BitmapAccessor.h"
#include "Config.h"
#include "GIMPGlue.h"

const char addonName[] = "Antialias";
const char addonInfo[] = "Smooths stair-step edges.  Original GIMP plugin by Kevin Turner";
const char addonCategory[] = "Blur";
int32 addonVersion = 100;

void antialias(BitmapAccessor* sourceBitmap, BRect sel_rect, BInvoker* prog, const Prefs* prefs);
static void multiply_alpha (uchar *buf, int width, int bytes);
static void separate_alpha (uchar *buf, int width, int bytes);

/* samecolor returns TRUE if all components of the two colors are
   within s_threshhold of each other. */

inline static bool samecolor(uchar *color1, 
				 uchar *color2, 
				 int bytes,
				 uchar s_threshold);

/* diffcolor returns TRUE if some component of both color1 and color2
   differs from color by more than d_threshold. */

inline static bool diffcolor(uchar *color, 
				 uchar *color1, 
				 uchar *color2, 
				 int bytes,
				 uchar d_threshold);


status_t
Manipulate(
	BitmapAccessor *sourceBitmap,
	BMessage *ioExtension,
	bool checkOnly)
{
	Prefs prefs;

	switch(sourceBitmap->ColorSpace())
	{
		case B_RGB24_BIG:
		case B_RGB32_LITTLE:
		case B_RGB24_LITTLE:
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

	antialias(sourceBitmap, sel_rect, prog, &prefs);
	
	return B_OK;
}

void antialias(BitmapAccessor* sourceBitmap, BRect sel_rect, BInvoker* prog, const Prefs* prefs)
{
	int x1, y1, x2, y2;
	int sx1, sy1, sx2, sy2;
	int32 srowstride, drowstride;
	const bool has_alpha = false;

	gimp_progress_init("Creating diffraction pattern...");

	GIMPImage src(sourceBitmap);
	GIMPImage dest(sourceBitmap, true);
	x1 = (int) sel_rect.left;
	x2 = (int) sel_rect.right;
	y1 = (int) sel_rect.top;
	y2 = (int) sel_rect.bottom;
	
	const int bpp=src.bpp;
	
	uchar *src_buf;
	uchar *dest_ptr, *src_ptr;
	uchar *dest_row, *src_row;
	int i, j, row;
	
	/* normalize mask elements */
	const double fmask4 = prefs->weight; /* The weight the source pixel has. */
	const double fmask = ( 1.0 - prefs->weight ) / 8.0; /* The weight its neigbors
								have (both orthagonal and
								diagonal). */
	/* double fmask[9]; */	
	/* fmask[0] = fmask[1] = fmask[2] = fmask[3] =
	   fmask[5] = fmask[6] = fmask[7] = fmask[8] = ( 1.0 - aavals.weight ) / 8.0; */
	

	/* The source needs to be one larger than the destination. */
	sx1=max(0, x1-1);
	sy1=max(0, y1-1);
	sx2=min(x2+1, src.Width());
	sy2=min(y2+1, src.Height());
	
	x1 = sx1 + 1;
	y1 = sy1 + 1;
	x2 = sx2 - 1;
	y2 = sy2 - 1;

#ifdef AALIAS_DEBUG
	g_print(" x1: %d\t y1: %d\t x2: %d\t y2: %d\n"
		"sx1: %d\tsy1: %d\tsx2: %d\tsy2: %d\n",
		x1,y1,x2,y2,sx1,sy1,sx2,sy2);
#endif

	src_buf= (uchar*) src.mBitmap->AccessBits(BRect(sx1, sy1, sx2, sy2), &srowstride);
	
	/* Grab a rectangle for our source region.  It's the
	   destination tile plus a pixel of padding on each
	   side, which should be guaranteed by earlier
	   precautions. */

	/* Convert our source to premultiplied alpha. */
	if (has_alpha)
		multiply_alpha(src_buf, (src.Width() + 3)*(src.Height() + 3), bpp);
	
	/* Do that funky antialiasing stuff, the results are
	   going in dest_rgn.data */
	
	dest_row=(uchar*) dest.mBitmap->AccessBits(BRect(x1, y1, x2, y2), &drowstride);
	/* dest_end=dest_rgn.data + dest_rgn.w * dest_rgn.h * bpp; */
	src_row =src_buf + srowstride + bpp; /* src_rgn.data */
	for(row=y1; row<=y2; row++, 
		    dest_row+=drowstride,
		    src_row +=srowstride) {
		dest_ptr=dest_row;
		src_ptr =src_row;
		
		for(i=x1; i <= x2; i++) {
			
/* Now we decide if this pixel needs to be antialiased.  Explanation time:

            N 
          W . E 
            S

  If N == W (samecolor), then there is a stairstep at point.  But
  check to make sure that W != S and W != E (diffcolor), because that
  would make notch or hole instead, which we do not want to antialias.

  Note that in checking for the notch, we could have done N != S and
  N != E instead, since we have determined that N and W are 'equal'.
  The decision is arbatrary.  Because they may not be exactly equal,
  for accuracy we would check both, or an average of the two, but
  this is an inexact effect, so we prefer speed and simplicity over
  such complications. 
  
  (Repeat for NE, SE, SW steps.)

*/

/* Some shorthand to make things readable. */
#define NORTH (src_ptr - srowstride)
#define SOUTH (src_ptr + srowstride)
#define EAST  (src_ptr + bpp)
#define WEST  (src_ptr - bpp)

			if ((samecolor(WEST,NORTH,bpp,prefs->s_threshold) && 
			     diffcolor(WEST,SOUTH,EAST,bpp,prefs->d_threshold)) ||
			    (samecolor(WEST,SOUTH,bpp,prefs->s_threshold) &&
			     diffcolor(WEST,NORTH,EAST,bpp,prefs->d_threshold)) ||
			    (samecolor(EAST,NORTH,bpp,prefs->s_threshold) && 
			     diffcolor(EAST,SOUTH,WEST,bpp,prefs->d_threshold)) ||
			    (samecolor(EAST,SOUTH,bpp,prefs->s_threshold) &&
			     diffcolor(EAST,NORTH,WEST,bpp,prefs->d_threshold))) {

				for (j=bpp; j>0; j--) {
					*(dest_ptr++) = (uchar) ((*src_ptr) * fmask4 +
						(*(src_ptr-srowstride-bpp) + 
						 *(src_ptr-srowstride)   +
						 *(src_ptr-srowstride+bpp) +
						 *(src_ptr-bpp) +
						 *(src_ptr+bpp) +
						 *(src_ptr+srowstride-bpp) +
						 *(src_ptr+srowstride)   +
						 *(src_ptr+srowstride+bpp)) * fmask
						+ 0.5);
					src_ptr++;
				} /* next byte */
			} else { /* elseif not aliasing */
				memcpy(dest_ptr, src_ptr, bpp);
				dest_ptr+=bpp; 
				src_ptr+=bpp;
			}
			
		} /* next pixel */
		
		/* In dest_rgn, Convert back from
		   premultiplied alpha to seperated alpha. */
		
		if (has_alpha) separate_alpha(dest_row, dest.Width() + 1, bpp);
		
		if ((y2 - y1 - row) % 5 == 4)
			gimp_progress_update(prog, (y2 - y1 - row) / (float) (y2 - y1));
	} /* end while (next row) */
	
	gimp_progress_update(prog, 100.0f);
	
	src.Copy(&dest, BRect(x1, y1, x2, y2));	

}


inline static bool
samecolor(uchar *color1, uchar *color2, int bytes, uchar s_threshold)
{
	int i, maxdiff=0, diff;
	
	for(i=0; i<bytes; i++) {
		diff = abs(color1[i] - color2[i]);
		if (diff > maxdiff) 
			maxdiff = diff;
	}
	return (maxdiff < s_threshold);

	/* If we really cared, we'd calculate color distance with 
	   something like this:

	   0.3 * dr^2 + 0.6 * dg^2 + 0.1 * db^2

	   Or, with alpha:
	   sqrt (da^2 + (a0 + a1) * (0.15 * dr^2 + 0.3 * dg^2 + 0.05 * db^2)) 

	   But we don't care that much.  The way it is is fairly fast
	   and is indepentant of bpp, color space, image-mode,
	   whatever.

	*/
}

inline static bool
diffcolor(uchar *color, uchar *color1, uchar *color2, int bytes, uchar d_threshold)
{
	int i, maxdiff=0, diff;
	
	for(i=0; i<bytes; i++) {
		diff = abs(color[i] - color1[i]);
		if (diff > maxdiff) 
			maxdiff = diff;
	}
	if (maxdiff < d_threshold)
		return FALSE; /* Small difference, diffcolor is FALSE */
	
	maxdiff=0;
	for(i=0; i<bytes; i++) {
		diff = abs(color[i] - color2[i]);
		if (diff > maxdiff) 
			maxdiff = diff;
	}
	return (maxdiff > d_threshold);
}

/* Convert from separated to premultiplied alpha, on a single scan line. */
static void
multiply_alpha (uchar *buf, int width, int bytes)
{
	int i, j;
	double alpha;
	
	for (i = 0; i < width * bytes; i += bytes)
	{
		alpha = buf[i + bytes - 1] * (1.0 / 255.0);
		for (j = 0; j < bytes - 1; j++)
			buf[i + j] = (uchar) (buf[i + j] * alpha);
	}
}

/* Convert from premultiplied to separated alpha, on a single scan line. */
static void
separate_alpha (uchar *buf, int width, int bytes)
{
	int i, j;
	uchar alpha;
	double recip_alpha;
	int new_val;
	
	for (i = 0; i < width * bytes; i += bytes) {
		alpha = buf[i + bytes - 1];
		if (alpha != 0 && alpha != 255) {
			recip_alpha = 255.0 / alpha;
			for (j = 0; j < bytes - 1; j++) {
				new_val = (int) (buf[i + j] * recip_alpha);
				buf[i + j] = MIN (255, new_val);
			}
		}
	}
}
