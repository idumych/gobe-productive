/*
 * lensflare 0.01 -- image filter plug-in for The GIMP
 * Copyright (C) 1996 Marc Bless
 *
 * E-mail: bless@ai-lab.fh-furtwangen.de
 * WWW:    www.ai-lab.fh-furtwangen.de/~bless
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
 
How to generate a lens flare:
-----------------------------

 1. Given a set of relative center-positions of several flares in
    a structure like

		flare_set = {
		  { relative_vector, type, rgb_set, relative_size },
			{ ... }
			... }

		This is a model of the following figure:

		-----------------------------------------------
		|               \ | / |                       |
		|                \|/  |                       |
		|               --0-- |                       |
		|                /|\  |                       |
		|               / | \ |                       |
		|                    1|                       |
		|---------------------C-----------------------|
		|                     |\                      |
		|                     |(2)                    |
		|                     |  \___                 |
		|                     |  (\  )                |
		|                     | (  n  )               |
		|                     |  (__\)                |
		-----------------------------------------------

		C : origin of the image (center)
		0 : center of the 'sun' with bright lines come out of it
		1 : first flare
		2 : second flare
		...
		n : n'th flare

		All flares have a relative position (a vector) to the sun and
		the origin (C).

*/

/*
 * History:
 * 
 * v 0.00 - 1996 July 24 : another try to hack it.
 *   0.01 - 1996 Sept 15 : first release. ugly but it works.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "gimp.h"

#define DIALOG_TITLE "Lens Flare (v 0.00)"

#define sqr(x)	((x) * (x))
#define WITHIN(a, b, c) ((((a) <= (b)) && ((b) <= (c))) ? 1 : 0)

typedef unsigned char uchar;

static int 	 dlg_init (void);
static void  lensflare (Image, Image);
static void  cb_scale (int, void *, void *);
static void  cb_ok (int, void *, void *);
static void  cb_cancel (int, void *, void *);
static void  cb_toggle (int, void *, void *);
static void  cb_radio (int, void *, void *);
static void  saveimage (void);
static void  freshen (void);
static void  calc_all (void);

static char *prog_name;

static int   dialogID;

static double xm = 0;
static double ym = 0;

static long width;
static long height;
static long channels, rowstride;
static long xsun       = 100;
static long ysun       = 105;
static long brightness = 100;

static long  aapply    = 0;
static long  angle     = 0;
static long  spikes    = 0;

static Image input, output;

static unsigned char *saved;

/*
 * The main function.
 */

int main (int argc, char **argv)
{
  prog_name = argv[0];

  if (!gimp_init (argc, argv))
		return 1;

  input = 0;
  output = 0;
      
  if (!(input = gimp_get_input_image (0)))
		return 1;

	if (!(output = gimp_get_output_image (0)))
	{
		gimp_free_image (input);
		gimp_quit ();
		return 1;
	}

	if ((gimp_image_type (input) != RGB_IMAGE) &&
			(gimp_image_type (input) != GRAY_IMAGE))
	{
  	gimp_message ("lensflare: can operate on rgb and gray image types only");
		gimp_free_image (input);
		gimp_free_image (output);
		gimp_quit ();
		return 1;
	}

	saveimage ();		/* save our input image before anything happens */

	dialogID = dlg_init ();
						
	if (!gimp_show_dialog (dialogID))
	{
		if (aapply)
		{
			freshen ();
			gimp_update_image (output);
		}
	}
	else
	{
		if (!aapply)
			calc_all ();
	}

	free (saved);

	gimp_free_image (input);
	gimp_free_image (output);

  gimp_quit ();

  return 0;
}

/* 
 * The dialog init function.
 */

static int dlg_init (void)
{
  int mainID,   labelID, paramID,   paramgrpID, aapplyID,  
      dlgID,     previewgrpID, previewID, brightID,
			xsunID, ysunID, spikeID;
  int x1, y1, x2, y2;

  width  = gimp_image_width  (input);
  height = gimp_image_height (input);

  gimp_image_area (input, &x1, &y1, &x2, &y2);
	xsun       = x1 + (x2 - x1) / 3;
	ysun       = y1 + (y2 - y1) / 3;
	brightness = 100;

	dlgID = gimp_new_dialog (DIALOG_TITLE);
  mainID   = gimp_new_row_group (dlgID, DEFAULT, NORMAL, "");
	paramID  = gimp_new_frame (dlgID, mainID, "Parameters");
		paramgrpID = gimp_new_row_group (dlgID, paramID, NORMAL, "");
			labelID  = gimp_new_label (dlgID, paramgrpID, "x-Position");
			xsunID   = gimp_new_scale (dlgID, paramgrpID, -width, 2*width, xsun, 0);
			labelID  = gimp_new_label (dlgID, paramgrpID, "y-Position");
			ysunID   = gimp_new_scale (dlgID, paramgrpID, -height, 2*height, ysun, 0);
			labelID  = gimp_new_label (dlgID, paramgrpID, "Brightness");
			brightID = gimp_new_scale (dlgID, paramgrpID, 0, 300, 100, 0);
			spikeID = gimp_new_check_button (dlgID, paramgrpID, "Sun star flare");
	previewID = gimp_new_frame (dlgID, mainID, "Preview");
		previewgrpID = gimp_new_row_group (dlgID, previewID, NORMAL, "");
			aapplyID = gimp_new_check_button (dlgID, previewgrpID, "Auto Apply");
			gimp_change_item (dlgID, aapplyID, sizeof (aapply), &aapply);
			
	gimp_add_callback (dlgID, xsunID, cb_scale, &xsun);
	gimp_add_callback (dlgID, ysunID, cb_scale, &ysun);
	gimp_add_callback (dlgID, brightID, cb_scale, &brightness);
	gimp_add_callback (dlgID, aapplyID, cb_toggle, &aapply);
	gimp_add_callback (dlgID, spikeID, cb_toggle, &spikes);
	gimp_add_callback (dlgID, gimp_ok_item_id (dlgID), cb_ok, 0);
	gimp_add_callback (dlgID, gimp_cancel_item_id (dlgID), cb_cancel, 0);

	return dlgID;
}

/* 
 * The radio button callback function.
 */

static void cb_radio (int itemID, void *client_data, void *call_data)
{
	if (aapply && (*((long*) client_data) != *((long*) call_data)))
	{
		*((long*) client_data) = *((long*) call_data);
		calc_all ();
	}
	else
		*((long*) client_data) = *((long*) call_data);
}

/* 
 * The check button callback function.
 */

static void cb_toggle (int itemID, void *client_data, void *call_data)
{
	*((long*) client_data) = *((long*) call_data);
	if (aapply)
		calc_all ();
	else
	{
		freshen ();
		gimp_update_image (output);
	}
}

/*
 * The scale callback function.
 */

static void cb_scale (int itemID, void *client_data, void *call_data)
{
	if (aapply && (*((long*) client_data) != *((long*) call_data)))
	{
		*((long*) client_data) = *((long*) call_data);
		calc_all ();
	}
	else
		*((long*) client_data) = *((long*) call_data);
}

/*
 * The saveimage function.
 */

static void saveimage (void)
{
	saved = (unsigned char *) malloc (gimp_image_width (input) *
	                          gimp_image_height (input) *
														gimp_image_channels (input));
	memcpy (saved, gimp_image_data (input),
	               gimp_image_width (input) *
								 gimp_image_height (input) *
								 gimp_image_channels (input));
}

/*
 * The freshen function.
 */

static void freshen (void)
{
	memcpy (gimp_image_data (output), saved,
		      gimp_image_width (input) *
					gimp_image_height (input) *
					gimp_image_channels (input));
}

/*
 * The ok button callback function.
 */

static void cb_ok (int itemID, void *client_data, void *call_data)
{
	gimp_close_dialog (dialogID, 1);
}

/*
 * The cancel button callback function.
 */

static void cb_cancel (int itemID, void *client_data, void *call_data)
{
	gimp_close_dialog (dialogID, 0);
}

static unsigned char flare_radial (int x, int y, unsigned char src, double rel, int r, unsigned char col1, unsigned char col2, int bright)
{
	int ret;
	int xpos = xm + (xsun - xm) * rel;
	int ypos = ym + (ysun - ym) * rel;
	int dist = (int) sqrt (sqr (x - xpos) + sqr (y - ypos));

	if (dist > r)		/* point outside radius? */
		return src;

	if (dist == 0)
		return col1;
	
	ret = (((col1 * (r - dist)) + (col2 * dist)) / r);
	ret = ret * bright / 100 + src;
	return (ret > 255 ? 255 : ret);
/*	return (col - (col * (double)((double)dist / (double)r)));
*/
}

static unsigned char flare_ring (int x, int y, unsigned char src, double rel, int r, int size, unsigned char col, int bright)
{
	int ret;
	int xpos = xm + (xsun - xm) * rel;
	int ypos = ym + (ysun - ym) * rel;
	int dist = (int) sqrt (sqr (x - xpos) + sqr (y - ypos));

	if ((dist > r) || (dist < r - size))
		return src;
	
	ret =  col * bright / 100 + src;
	return (ret > 255 ? 255 : ret);
	/* ((col * bright + src * (100 - bright)) / 200); */
}

static unsigned char flare_ring2 (int x, int y, unsigned char src, double rel, int r, int size, unsigned char col, int bright)
{
	int ret;
	int xpos = xm + (xsun - xm) * rel;
	int ypos = ym + (ysun - ym) * rel;
	int dist = (int) sqrt (sqr (x - xpos) + sqr (y - ypos));

	if ((dist > r + size) || (dist < r - size))
		return src;
	
	ret =  (col * bright / 100) * (1 - abs (dist - r) / (double)size) + src ;
	return (ret > 255 ? 255 : ret);
	/* ((col * bright + src * (100 - bright)) / 200); */
}

static unsigned char flare_ball (int x, int y, unsigned char src, double rel, int r, unsigned char col, int bright)
{
	int ret;
	int xpos = xm + (xsun - xm) * rel;
	int ypos = ym + (ysun - ym) * rel;
	int dist = (int) sqrt (sqr (x - xpos) + sqr (y - ypos));

	if (dist > r)
		return src;

	ret =  col * bright / 100 + src;
	return (ret > 255 ? 255 : ret);
}

static void add_pixel (unsigned char *src, unsigned char *dest, double xr, double yr, double ri, double gi, double bi, int x, int y)
{
	double dx = xr - x;
	double dy = yr - y;
	double rs = dx * dx + dy * dy;
	double vr = ri * exp (-rs / 2);
	double vg = gi * exp (-rs / 2);
	double vb = bi * exp (-rs / 2);

	if ((x >= 0) && (y >= 0) && (x < width) && (y < height))
	{
		vr += *(src + rowstride * y + channels * x    ) / 255.0;
		vg += *(src + rowstride * y + channels * x + 1) / 255.0;
		vb += *(src + rowstride * y + channels * x + 2) / 255.0;
		if (vr > 1.0) vr = 1.0;
		if (vg > 1.0) vg = 1.0;
		if (vb > 1.0) vb = 1.0;
		*(dest + rowstride * y + channels * x    ) = (unsigned char) (vr * 255.0);
		*(dest + rowstride * y + channels * x + 1) = (unsigned char) (vg * 255.0);
		*(dest + rowstride * y + channels * x + 2) = (unsigned char) (vb * 255.0);
	}
}

static void four_point (unsigned char *src, unsigned char *dest, double x, double y, unsigned char R, unsigned char G, unsigned char B)
{
	int xx = (int) x;
	int yy = (int) y;

	add_pixel (src, dest, x, y, R / 255.0, G / 255.0, B / 255.0, xx, yy);
	xx++;
	add_pixel (src, dest, x, y, R / 255.0, G / 255.0, B / 255.0, xx, yy);
	yy++;
	add_pixel (src, dest, x, y, R / 255.0, G / 255.0, B / 255.0, xx, yy);
	xx--;
	add_pixel (src, dest, x, y, R / 255.0, G / 255.0, B / 255.0, xx, yy);
}

static void flare_star (unsigned char *src, unsigned char *dest, int x, int y, unsigned char R, unsigned char G, unsigned char B, int size, double angle, int spikes, int bright)
{
	double alpha = angle;
	double dx, dy;
	int i, s;
	double xx, yy;

	for (i = 0; i < spikes; i++)
	{
		xx = x;
		yy = y;
		dx = 0.8 * cos (alpha * M_PI / 180);
		dy = 0.8 * sin (alpha * M_PI / 180);
		while ((s = sqrt (sqr (xx - x) + sqr (yy - y))) < size)
		{
			four_point (src, dest, xx, yy, R * (1.0 - (s / (double)size)), G * (1.0 - (s / (double)size)), B * (1.0 - (s / (double)size)));
			xx += dx;
			yy += dy;
		}
		alpha += angle;
	}
}

/*
 * The lensflare filter function.
 */

static void lensflare (Image linput, Image loutput)
{
  long rad_base;
  unsigned char *src_row, *dest_row;
  unsigned char *src, *dest;
	unsigned char  val;
  short row, col;
  int x1, y1, x2, y2;
	int xdiff, ydiff;
	double angl;
  int progress, max_progress;
	long bright;

  gimp_image_area (linput, &x1, &y1, &x2, &y2);

/*  width     = gimp_image_width (linput);
  height    = gimp_image_height (linput);
*/  channels  = gimp_image_channels (linput);
  rowstride = width * channels;

	xdiff = x2 - x1;
	ydiff = y2 - y1;
	rad_base  = xdiff > ydiff ? xdiff : ydiff;

	bright = brightness > 100 ? 100 : brightness;

  progress     = 0;
  max_progress = ydiff;
	gimp_init_progress ("Lens Flare");

	src_row  = saved                     + rowstride * y1 + x1 * channels;
  dest_row = gimp_image_data (loutput) + rowstride * y1 + x1 * channels;

  xm = xdiff / 2.0;
  ym = ydiff / 2.0;

	angl = (double)angle / 180.0 * M_PI;

  for (row = y1; row < y2; row++)
  {
    src     = src_row;
    dest    = dest_row;

    for (col = x1; col < x2; col++)
    {
			int extra;
 			val     = *src++;
/*  1 */ val  = flare_ring2 (col, row, val,  1.000, rad_base / 12, 3, 138, bright);
/*  1 */ val  = flare_radial (col, row, val,  1.000, rad_base / 8, 255, 0, bright);
/*  1 */ val  = flare_radial (col, row, val,  1.000, brightness * rad_base / 200, 255 * brightness / 600, 0, brightness);
/*  2 */ val	= flare_ring2 (col, row, val,  1.300, rad_base / 30,  3,  90, bright); 
/*  3    val  = flare_ball  (col, row, val,  0.510, rad_base / 45,       0, bright);
    4    val  = flare_ball  (col, row, val,  0.410, rad_base / 35,       0, bright);
    5    val  = flare_ball  (col, row, val,  0.450, rad_base / 17,       0, bright); */
/*  6 */ val	= flare_ball  (col, row, val,  0.195, rad_base / 55,      69, bright); 
/*  7 */ val  = flare_radial (col, row, val,  0.000, 2, 94,  0, bright);
/*  8 */ val  = flare_radial (col, row, val, -0.240, rad_base / 80, 126,  0, bright);
/*  9 */ val  = flare_radial (col, row, val, -0.450, rad_base / 15,  0, 64, bright);
/* 10 */ val  = flare_ball  (col, row, val, -0.415, rad_base / 28,      64, bright);
/* 11 */ val  = flare_ball  (col, row, val, -0.470, rad_base / 70,      64, bright);
/* 12 */ val  = flare_ball  (col, row, val, -0.650, rad_base / 40,      64, bright);
/* 13    val  = flare_ball  (col, row, val, -0.665, rad_base / 100,      0, bright); */
/* 14 */ val	= flare_ring2 (col, row, val, -1.000, rad_base / 10,  3, 192, bright / 2); 
/* 14 */ val  = flare_radial (col, row, val, -1.000, rad_base / 10, 0,  42, bright);
/* 15 */ val  = flare_ring2 (col, row, val, -1.320, rad_base / 5.3, 3, 42, bright);
/* 15 */ val  = flare_ring2 (col, row, val, -1.320, rad_base / 5.1, 5, 42, bright);
			*dest++ = val;

			val     = *src++;
/*  1 */ val  = flare_ring2 (col, row, val,  1.000, rad_base / 12, 3, 63, bright);
/*  1 */ val  = flare_radial (col, row, val,  1.000, rad_base / 24, 255, 0, bright);
/*  1 */ val  = flare_radial (col, row, val,  1.000, brightness * rad_base / 200, 255 * brightness / 600, 0, brightness);
/*  2 */ val	= flare_ring2 (col, row, val,  1.300, rad_base / 30, 3, 62, bright); 
/*  3    val  = flare_ball  (col, row, val,  0.510, rad_base / 45,     0, bright);
    4    val  = flare_ball  (col, row, val,  0.410, rad_base / 35,     0, bright);
    5    val  = flare_ball  (col, row, val,  0.450, rad_base / 17,     0, bright); */
/*  6 */ val	= flare_ball  (col, row, val,  0.195, rad_base / 55,    47, bright); 
/*  7 */ val  = flare_radial (col, row, val,  0.000, 2, 185,  0, bright);
/*  8 */ val  = flare_radial (col, row, val, -0.240, rad_base / 80, 195,  0, bright);
/*  9    val  = flare_radial (col, row, val, -0.450, rad_base / 15,  0, 48, bright); */
/* 10 */ val  = flare_ball  (col, row, val, -0.415, rad_base / 28,    48, bright);
/* 11 */ val  = flare_ball  (col, row, val, -0.470, rad_base / 70,    48, bright);
/* 12 */ val  = flare_ball  (col, row, val, -0.650, rad_base / 40,    72, bright);
/* 13    val  = flare_ball  (col, row, val, -0.665, rad_base / 100,    0, bright); */
/* 14 */ val	= flare_ring2 (col, row, val, -1.000, rad_base / 10,  3, 192, bright / 2); 
/* 14 */ val  = flare_radial (col, row, val, -1.000, rad_base / 10, 0,  42, bright);
/* 15 */ val  = flare_ring2 (col, row, val, -1.320, rad_base / 5.3, 3, 32, bright);
/* 15 */ val  = flare_ring2 (col, row, val, -1.320, rad_base / 5.1, 5, 32, bright);
			*dest++ = val;

			val     = *src++;
/*  1    val  = flare_ring2 (col, row, val,  1.000, rad_base / 12, 3,   0, bright); */
/*  1 */ val  = flare_radial (col, row, val,  1.000, rad_base / 24, 255, 0, bright);
/*  1 */ val  = flare_radial (col, row, val,  1.000, brightness * rad_base / 200, 255 * brightness / 600, 0, brightness);
/*  2 */ val	= flare_ring2 (col, row, val,  1.300, rad_base / 30, 3,  58, bright); 
/*  3 */ val  = flare_ball  (col, row, val,  0.510, rad_base / 45,     64, bright);
/*  4 */ val  = flare_ball  (col, row, val,  0.410, rad_base / 35,     64, bright);
/*  4 */ val  = flare_ball  (col, row, val,  0.450, rad_base / 17,     32, bright);
/*  6 */ val	= flare_ball  (col, row, val,  0.195, rad_base / 55,     36, bright); 
/*  7 */ val  = flare_radial (col, row, val,  0.000, 2, 154,  0, bright);
/*  8 */ val  = flare_radial (col, row, val, -0.240, rad_base / 80, 174,  0, bright);
/*  9    val  = flare_radial (col, row, val, -0.450, rad_base / 15,  0, 0, bright);
   10    val  = flare_ball  (col, row, val, -0.415, rad_base / 28,      0, bright);
   11    val  = flare_ball  (col, row, val, -0.470, rad_base / 70,      0, bright);
   12    val  = flare_ball  (col, row, val, -0.650, rad_base / 40,      0, bright); */
/* 13 */ val  = flare_ball  (col, row, val, -0.665, rad_base / 100,  192, bright);
/* 14    val	= flare_ring2 (col, row, val, -1.000, rad_base / 10,  3,   0, bright); 
   14    val  = flare_radial (col, row, val, -1.000, rad_base / 10, 0,  0, bright); */
/* 15 */ val  = flare_ring2 (col, row, val, -1.320, rad_base / 5.3, 3, 64, bright);
/* 15 */ val  = flare_ring2 (col, row, val, -1.320, rad_base / 5.1, 5, 18, bright);
			*dest++ = val;

			/* Account for alpha */			
			if (channels == 4)
				*dest++ = *src++;
		}

    src_row += rowstride;
    dest_row += rowstride;

    if (++progress % 5 == 0)
    	gimp_do_progress (progress, max_progress);
  }

	if (spikes)
/*  1 */ flare_star (saved, gimp_image_data (loutput), xsun, ysun,   5,   5,   5, 3*brightness, 31, 27, brightness);

	gimp_do_progress (1, 1);
}

static void calc_all (void)
{
	lensflare (input, output);
	gimp_update_image (output);
}

