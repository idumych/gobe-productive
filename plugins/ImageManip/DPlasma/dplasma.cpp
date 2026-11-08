/*
 * This is a plugin for the GIMP.
 *
 * Copyright (C) 1996 Andrew Kieschnick
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

/* Hack of the plasma plug-in to produce "difference clouds"
   by Andrew Kieschnick (andrewk@mail.utexas.edu)
   original code by srn@flibble.cs.su.oz.au

   Uses foreground and background as colors for "cloud"
   You generally get more of the foreground color than of the
   background color - call it a bug or a feature :)
 */

/*
 * This file has been modified from the original.  A dialog label was added.
 * <Scott Lindsey 7/21/98>
 */


/* Todo:
   Fix the progress bar so it ends at the right time - its sorta close
 */

/* Version 1.01 */

#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#include "GIMPGlue.h"
#include "gimp.h"
#include "ImageManipAddon.h"
#include "BitmapAccessor.h"
#include "Config.h"

const char addonName[] = "Difference Plasma";
const char addonInfo[] = "Hack of the plasma plug-in to produce \"difference clouds\".\nOriginal code by <srn@flibble.cs.su.oz.au>.\nUses foreground and background as colors for \"cloud\".  You generally get more of the foreground color than of the background color - call it a bug or a feature.\nPorted by Scott Lindsey\nGIMP plugin by Andrew Kieschnick";
const char addonCategory[] = "Miscellaneous";
int32 addonVersion = 100;

static void scale_callback(int, void *, void *);
static void ok_callback(int, void *, void *);
static void cancel_callback(int, void *, void *);
static void text_callback(int item_id, void *client_data, void *call_data);
int do_plasma(int x1, int y1, int x2, int y2, int depth, int scale_depth);
static void plasma(Image input, uchar *output, float turbulence);
void do_difference(Image input, Image output, uchar *temp);
void random_rgb(uchar *v);

static int	dialog_id;
uchar	fr,fg,fb;
uchar	br,bg,bb;
uchar colorvalues[256][4];

int alpha_offset;
int color_offset;

/*
 * Some globals to save passing too many paramaters that don't change.
 */

int	ix1, iy1, ix2, iy2;	/* Selected image size. */
int32	channels, row_stride;
float	turbulence;
uchar	*dest;
int	max_progress, progress;


/*
 * This structure is used for persistent data.
 */

typedef struct {
	uint32	seed;
	float		turbulence;
} Data;

/*
 * Some macros used for setting pixel and rgb triples.
 */
#define plot(x, y, v)								\
{													\
	uchar *p;										\
	p = dest + y * row_stride + x * channels;		\
	p[color_offset] = v[color_offset];				\
	p[color_offset + 1] = v[color_offset + 1];		\
	p[color_offset + 2] = v[color_offset + 2];		\
	if (alpha_offset >= 0)							\
		p[alpha_offset] = v[alpha_offset];			\
	progress++;										\
}

void random_rgb(uchar *v)
{
  v[color_offset] =		rand() % 256;
  v[color_offset + 1] = rand() % 256;
  v[color_offset + 2] = rand() % 256;
/*random alpha???*/
}

#define AVE(n, v1, v2) n[color_offset] = ((int)v1[color_offset] + (int)v2[color_offset]) / 2;\
        n[color_offset + 1] = ((int)v1[color_offset + 1] + (int)v2[color_offset + 1]) / 2;\
        n[color_offset + 2] = ((int)v1[color_offset + 2] + (int)v2[color_offset + 2]) / 2;



status_t
Manipulate(
	BitmapAccessor *sourceBitmap,
	BMessage *ioExtension,
	bool checkOnly)
{
	int r, g, b, a;
	Prefs prefs;
	alpha_offset = -1;
	color_offset = 0;
	switch(sourceBitmap->ColorSpace())
	{
		case B_RGBA32_LITTLE:
			alpha_offset = 3;
		case B_RGB32_LITTLE:
		case B_RGB24_LITTLE:
			b = 0; g = 1; r = 2; a = 3;
			break;
		case B_RGBA32_BIG:
			alpha_offset = 0;
		case B_RGB32_BIG:
			color_offset = 1;
			a = 0; r = 1; g = 2; b = 3;
			break;
		case B_RGB24_BIG:
			r = 0; g = 2; b = 3;
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
		float turbulence;
		if (!ioExtension->FindFloat("dplasma/turbulence", &turbulence))
			prefs.turbulence = turbulence;
	}

	Image input = new GIMPImage(sourceBitmap);

	gimp_foreground_color(&fr,&fg,&fb);
	gimp_background_color(&br,&bg,&bb);
	for (int i=0;i<256;i++)
	{
		float rat = (float) i/256;
		colorvalues[i][r]=(uchar)(fr+(br-fr)*rat);
		colorvalues[i][g]=(uchar)(fg+(bg-fg)*rat);
		colorvalues[i][b]=(uchar)(fb+(bb-fb)*rat);
		colorvalues[i][a]=255;
	}

	
    Image output = gimp_temp_image();
    Image temp = gimp_temp_image();

	plasma(input, gimp_image_data(temp), prefs.turbulence/10.0f);
	do_difference(input, output, gimp_image_data(temp));
	input->Copy(output);

    gimp_free_image(temp);
    gimp_free_image(output);
    delete input;
	
	return B_OK;
}


/*
 * The setup function.
 */

static void
plasma(Image input, uchar *output, float turb)
{
	int32	width, height;
	int	depth;

	gimp_image_area(input, &ix1, &iy1, &ix2, &iy2);

	width = gimp_image_width(input);
	height = gimp_image_height(input);
	channels = gimp_image_channels(input);
	row_stride = width * channels;

	dest = output;

	/* max_progress doesn't appear to be right, I guess because the plasma
	   part draws some pixels twice... */

	max_progress = (ix2 - ix1) * (iy2 - iy1) * 2;
	progress = 0;

	turbulence = turb;

	/*
	 * This first time only puts in the seed pixels - one in each
	 * corner, and one in the center of each edge, plus one in the
	 * center of the image.
	 */

	do_plasma(ix1, iy1, ix2 - 1, iy2 - 1, -1, 0);

	/*
	 * Now we recurse through the images, going further each time.
	 */
	depth = 1;
	while (!do_plasma(ix1, iy1, ix2 - 1, iy2 - 1, depth, 0)){
		depth ++;
	}
}

void
add_random(uchar *d, int amnt)
{
	int	i, tmp;

	if (amnt == 0){
		amnt = 1;
	}

	for (i = 0; i < channels; i++){
		tmp = amnt/2 - rand() % amnt;
		if (i == alpha_offset)
			continue;

		if ((int)d[i] + tmp < 0){
			d[i] = 0;
		} else if ((int)d[i] + tmp > 255){
			d[i] = 255;
		} else {
			d[i] += tmp;
		}
	}
/*d[0]=colorvalues[d[0]][0];
d[1]=colorvalues[d[0]][1];
d[2]=colorvalues[d[0]][2];*/
}

void
get_pixel(uchar *v, uchar *pixmap, int x, int y)
{
	uchar	*p;

	if (x < ix1){
		x = ix1;
	}
	if (x > ix2 - 1){
		x = ix2 - 1;
	}

	if (y < iy1){
		y = iy1;
	}
	if (y > iy2 - 1){
		y = iy2 - 1;
	}

	p = pixmap + y * row_stride + x * channels;
	v[color_offset] = p[color_offset];
	v[color_offset + 1] = p[color_offset + 1];
	v[color_offset + 2] = p[color_offset + 2];
	if (alpha_offset >= 0)
		v[alpha_offset] = p[alpha_offset];
}

int
do_plasma(int x1, int y1, int x2, int y2, int depth, int scale_depth)
{
	uchar		tl[4], ml[4], bl[4], mt[4], mm[4], mb[4], tr[4], mr[4], br[4];
	uchar		tmp[4];
	int		ran;
	int		xm, ym;
	static int	count = 0;

	/* Initial pass through - no averaging. */

	if (depth == -1){
		random_rgb(tl);
		plot(x1, y1, tl);
		random_rgb(tr);
		plot(x2, y1, tr);
		random_rgb(bl);
		plot(x1, y2, bl);
		random_rgb(br);
		plot(x2, y2, br);
		random_rgb(mm);
		plot((x1 + x2) / 2, (y1 + y2) / 2, mm);
		random_rgb(ml);
		plot(x1, (y1 + y2) / 2, ml);
		random_rgb(mr);
		plot(x2, (y1 + y2) / 2, mr);
		random_rgb(mt);
		plot((x1 + x2) / 2, y1, mt);
		random_rgb(ml);
		plot((x1 + x2) / 2, y2, ml);

		return 0;
	}

	/*
	 * Some later pass, at the bottom of this pass,
	 * with averaging at this depth.
	 */
	if (depth == 0){
		int	xave, yave;

		get_pixel(tl, dest, x1, y1);	
		get_pixel(bl, dest, x1, y2);
		get_pixel(tr, dest, x2, y1);	
		get_pixel(br, dest, x2, y2);

		ran = (int) ((256.0 / (2.0 * (float)scale_depth)) * turbulence);

		xave = (x1 + x2) / 2;
		yave = (y1 + y2) / 2;

		if (xave == x1 && xave == x2 && yave == y1 && yave == y2){
			return 0;
		}

		if (xave != x1 || xave != x2){
			/* Left. */
			AVE(ml, tl, bl);
			add_random(ml, ran);
			plot(x1, yave, ml);

			if (x1 != x2){
				/* Right. */
				AVE(mr, tr, br);
				add_random(mr, ran);
				plot(x2, yave, mr);
			}
		}

		if (yave != y1 || yave != y2){
			if (x1 != xave || yave != y2){
				/* Bottom. */
				AVE(mb, bl, br);
				add_random(mb, ran);
				plot(xave, y2, mb);
			}

			if (y1 != y2){
				/* Top. */
				AVE(mt, tl, tr);
				add_random(mt, ran);
				plot(xave, y1, mt);
			}
		}

		if (y1 != y2 || x1 != x2){
			/* Middle pixel. */
			AVE(mm, tl, br);
			AVE(tmp, bl, tr);
			AVE(mm, mm, tmp);

			add_random(mm, ran);
			plot(xave, yave, mm);
		}

		count ++;

		if (!(count % 2000)){
			gimp_do_progress(progress >> 2, max_progress);
		}

		if ((x2 - x1) < 3 && (y2 - y1) < 3){
			return 1;
		}
		return 0;
	}

	xm = (x1 + x2) >> 1;
	ym = (y1 + y2) >> 1;

	/* Top left. */
	do_plasma(x1, y1, xm, ym, depth - 1, scale_depth + 1);
	/* Bottom left. */
	do_plasma(x1, ym, xm ,y2, depth - 1, scale_depth + 1);
	/* Top right. */
	do_plasma(xm, y1, x2 , ym, depth - 1, scale_depth + 1);
	/* Bottom right. */
	return do_plasma(xm, ym, x2, y2, depth - 1, scale_depth + 1);
}

void do_difference(Image input, Image output, uchar *temp)
{
	uchar src1[4],src2[4];
	uchar *dest, *srcA, *srcB;
	int ix,iy;
	int width,height;
	int count=0;
	
	width=gimp_image_width(output);
	height=gimp_image_height(output);
	srcA = gimp_image_data(input);
        srcB = temp;
	dest = gimp_image_data(output);
	for (ix=0;ix<width;ix++)
	  for (iy=0;iy<height;iy++)
	    {
	      get_pixel(src1,srcA,ix,iy);
	      get_pixel(src2,srcB,ix,iy);
	      
	      src2[color_offset]=colorvalues[(src2[color_offset]+src2[color_offset + 1]+src2[color_offset + 2])/3][color_offset];
	      src2[color_offset + 1]=colorvalues[(src2[color_offset]+src2[color_offset + 1]+src2[color_offset + 2])/3][color_offset + 1];
	      src2[color_offset + 2]=colorvalues[(src2[color_offset]+src2[color_offset + 1]+src2[color_offset + 2])/3][color_offset + 2];

	      src2[color_offset] = abs(src2[color_offset]-src1[color_offset]);
	      src2[color_offset + 1] = abs(src2[color_offset + 1]-src1[color_offset + 1]);
	      src2[color_offset + 2] = abs(src2[color_offset + 2]-src1[color_offset + 2]);
		  if (alpha_offset >= 0)
				src2[alpha_offset] = src1[alpha_offset];
	      plot(ix,iy,src2);		
	      
	      count ++;
	      
	      if (!(count % 2000)){
		gimp_do_progress(progress >> 2, max_progress);
	      }
	    }
}
   
   

