/*
 * This is a plugin for the GIMP.
 *
 * Copyright (C) 1996 Stephen Norris
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
 * This plug-in produces plasma fractal images. The algorithm is losely
 * based on a description of the fractint algorithm, but completely
 * re-implemented because the fractint code was too ugly to read :)
 *
 * Please send any patches or suggestions to me: srn@flibble.cs.su.oz.au.
 */

/*
 * This file has been modified from the original.  A dialog label was added.
 * <Scott Lindsey 7/21/98>
 */

/*
 * TODO:
 *      - Alpha channels.
 *      - Greyscale images.
 *      - The progress bar sucks.
 *      - It writes some pixels more than once.
 */

/*
   Version 1.02 
 */

#include <stdlib.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#include "gimp.h"

#ifndef _AIX
typedef unsigned char uchar;

#endif

/*
 * This structure is used for persistent data.
 */

typedef struct {
	unsigned long	seed;
	float		turbulence;
	float		alpha;
} Data;

static void scale_callback (int, void *, void *);
static void ok_callback (int, void *, void *);
static void cancel_callback (int, void *, void *);
static void text_callback (int item_id, void *client_data, void *call_data);
static int do_plasma (int x1, int y1, int x2, int y2, int depth, int scale_depth);
static void plasma (Image input, Image output, Data *data, ImageType);

static int dialog_id;

/*
 * Some macros used for setting pixel and rgb triples.
 */

#define plot(x, y, v) { uchar *p; \
	p = dest + y * row_stride + x * channels; \
	*(p) = v[0];\
	*(p + 1) = v[1];\
	*(p + 2) = v[2];\
	progress++;\
}

#define random_rgb(v) v[0] = rand() % 256;\
	v[1] = rand() % 256;\
	v[2] = rand() % 256;

#define AVE(n, v1, v2) n[0] = ((int)v1[0] + (int)v2[0]) / 2;\
	n[1] = ((int)v1[1] + (int)v2[1]) / 2;\
	n[2] = ((int)v1[2] + (int)v2[2]) / 2;

int
main (int argc, char **argv)
{
	Image output, input;
	Data *data;
	int turbulence_id, text_id, col_id, alpha_id;
	char buf[100];
	ImageType image_type;

	fprintf (stderr, "Starting plug-in\n");

	if (gimp_init (argc, argv)) {
		input = gimp_get_input_image (0);
		output = gimp_get_output_image (0);

		if (input && output)
			image_type = gimp_image_type (input);
		switch (image_type) {
		    case RGB_IMAGE:
		    case RGBA_IMAGE:
			    data = gimp_get_params ();
			    if (!data) {
				    /*
				       Use arbitary defaults. 
				     */

				    data = malloc (sizeof (Data));

				    data->seed = 0;
					data->alpha = 255;
				    data->turbulence = 10.0;	/*
								   Really 1. 
								 */
			    }
			    dialog_id = gimp_new_dialog ("Plasma");

			    col_id = gimp_new_row_group (dialog_id, DEFAULT, NORMAL, "");

			    sprintf (buf, "%ld", data->seed);
			    text_id = gimp_new_text (dialog_id, col_id, buf);
			    gimp_new_label (dialog_id, text_id, "Seed:");

			    gimp_add_callback (dialog_id, text_id, text_callback, &data->seed);

			    gimp_new_label (dialog_id, col_id, "Turbulence:");

			    turbulence_id = gimp_new_scale (dialog_id, col_id, 0, 100,
						       data->turbulence, 1);
			    gimp_add_callback (dialog_id, turbulence_id, scale_callback, &data->turbulence);
			    if (gimp_image_channels(input) == 4/*image_type == RGBA_IMAGE*/) {
				    gimp_new_label (dialog_id, col_id, "Opacity:");
				    alpha_id = gimp_new_scale (dialog_id, col_id, 0, 255,
						       data->alpha, 0);
				    gimp_add_callback (dialog_id, alpha_id, scale_callback, &data->alpha);
			    }
			    gimp_add_callback (dialog_id, gimp_ok_item_id (dialog_id),
					       ok_callback, NULL);
			    gimp_add_callback (dialog_id, gimp_cancel_item_id (dialog_id),
					       cancel_callback, NULL);

			    if (gimp_show_dialog (dialog_id)) {
				    gimp_set_params (sizeof (Data), data);
				    srand (data->seed);
				    gimp_init_progress ("Plasma");
				    plasma (input, output, data, image_type);
				    gimp_update_image (output);
			    }
			    break;

		    default:
			    gimp_message ("Plasma: can operate only on RGB images");
			    break;
		}

		if (input)
			gimp_free_image (input);
		if (output)
			gimp_free_image (output);

		gimp_quit ();
	}
	return 0;
}

/*
 * Some glabals to save passing too many paramaters that don't change.
 */

int ix1, iy1, ix2, iy2;		/* Selected image size. */
long channels, row_stride;
float turbulence;
uchar *dest;
int max_progress, progress;

/*
 * The setup function.
 */

static void
plasma (Image input, Image output, Data *data, ImageType image_type)
{
	long	width, height;
	int	depth;
	float	turb;

	turb = data->turbulence;

	gimp_image_area (input, &ix1, &iy1, &ix2, &iy2);

	width = gimp_image_width (input);
	height = gimp_image_height (input);
	channels = gimp_image_channels (input);
	row_stride = width * channels;

	dest = gimp_image_data (output);

	max_progress = (ix2 - ix1) * (iy2 - iy1);
	progress = 0;

	turbulence = turb;

	/*
	 * This first time only puts in the seed pixels - one in each
	 * corner, and one in the center of each edge, plus one in the
	 * center of the image.
	 */

	do_plasma (ix1, iy1, ix2 - 1, iy2 - 1, -1, 0);

	/*
	 * Now we recurse through the images, going further each time.
	 */
	depth = 1;
	while (!do_plasma (ix1, iy1, ix2 - 1, iy2 - 1, depth, 0)) {
		depth++;
	}
	if (channels == 4/*image_type == RGBA_IMAGE*/) {
		int	x, y;
		uchar	*p;
		uchar	a = (uchar) data->alpha;
		
		for (x = 0; x < width; x++) {
			for (y = 0; y < height; y++) {
				p = dest + y * row_stride + x * channels;

				*(p + 3) = a;
			}
		}
	}
	return;
}

void
add_random (uchar * d, int amnt)
{
	int i, tmp;

	for (i = 0; i < 3; i++) {
		if (amnt == 0) {
			amnt = 1;
		}
		tmp = amnt / 2 - rand () % amnt;

		if ((int) d[i] + tmp < 0) {
			d[i] = 0;
		} else if ((int) d[i] + tmp > 255) {
			d[i] = 255;
		} else {
			d[i] += tmp;
		}
	}
}

void
get_pixel (uchar * v, uchar * pixmap, int x, int y)
{
	uchar *p;

	if (x < ix1) {
		x = ix1;
	}
	if (x > ix2 - 1) {
		x = ix2 - 1;
	}
	if (y < iy1) {
		y = iy1;
	}
	if (y > iy2 - 1) {
		y = iy2 - 1;
	}
	p = pixmap + y * row_stride + x * channels;
	v[0] = *(p);
	v[1] = *(p + 1);
	v[2] = *(p + 2);
}

static int
do_plasma (int x1, int y1, int x2, int y2, int depth, int scale_depth)
{
	uchar tl[4], ml[4], bl[4], mt[4], mm[4], mb[4], tr[4], mr[4], br[4];
	uchar tmp[4];
	int ran;
	int xm, ym;
	static int count = 0;

	/*
	   Initial pass through - no averaging. 
	 */

	if (depth == -1) {
		random_rgb (tl);
		plot (x1, y1, tl);
		random_rgb (tr);
		plot (x2, y1, tr);
		random_rgb (bl);
		plot (x1, y2, bl);
		random_rgb (br);
		plot (x2, y2, br);
		random_rgb (mm);
		plot ((x1 + x2) / 2, (y1 + y2) / 2, mm);
		random_rgb (ml);
		plot (x1, (y1 + y2) / 2, ml);
		random_rgb (mr);
		plot (x2, (y1 + y2) / 2, mr);
		random_rgb (mt);
		plot ((x1 + x2) / 2, y1, mt);
		random_rgb (ml);
		plot ((x1 + x2) / 2, y2, ml);

		return 0;
	}
	/*
	 * Some later pass, at the bottom of this pass,
	 * with averaging at this depth.
	 */
	if (depth == 0) {
		float rnd;
		int xave, yave;

		get_pixel (tl, dest, x1, y1);
		get_pixel (bl, dest, x1, y2);
		get_pixel (tr, dest, x2, y1);
		get_pixel (br, dest, x2, y2);

		rnd = (256.0 / (2.0 * (float) scale_depth)) * turbulence;
		ran = rnd;

		xave = (x1 + x2) / 2;
		yave = (y1 + y2) / 2;

		if (xave == x1 && xave == x2 && yave == y1 && yave == y2) {
			return 0;
		}
		if (xave != x1 || xave != x2) {
			/*
			   Left. 
			 */
			AVE (ml, tl, bl);
			add_random (ml, ran);
			plot (x1, yave, ml);

			if (x1 != x2) {
				/*
				   Right. 
				 */
				AVE (mr, tr, br);
				add_random (mr, ran);
				plot (x2, yave, mr);
			}
		}
		if (yave != y1 || yave != y2) {
			if (x1 != xave || yave != y2) {
				/*
				   Bottom. 
				 */
				AVE (mb, bl, br);
				add_random (mb, ran);
				plot (xave, y2, mb);
			}
			if (y1 != y2) {
				/*
				   Top. 
				 */
				AVE (mt, tl, tr);
				add_random (mt, ran);
				plot (xave, y1, mt);
			}
		}
		if (y1 != y2 || x1 != x2) {
			/*
			   Middle pixel. 
			 */
			AVE (mm, tl, br);
			AVE (tmp, bl, tr);
			AVE (mm, mm, tmp);

			add_random (mm, ran);
			plot (xave, yave, mm);
		}
		count++;

		if (!(count % 2000)) {
			gimp_do_progress (progress >> 2, max_progress);
		}
		if ((x2 - x1) < 3 && (y2 - y1) < 3) {
			return 1;
		}
		return 0;
	}
	xm = (x1 + x2) >> 1;
	ym = (y1 + y2) >> 1;

	/*
	   Top left. 
	 */
	do_plasma (x1, y1, xm, ym, depth - 1, scale_depth + 1);
	/*
	   Bottom left. 
	 */
	do_plasma (x1, ym, xm, y2, depth - 1, scale_depth + 1);
	/*
	   Top right. 
	 */
	do_plasma (xm, y1, x2, ym, depth - 1, scale_depth + 1);
	/*
	   Bottom right. 
	 */
	return do_plasma (xm, ym, x2, y2, depth - 1, scale_depth + 1);
}

static void
scale_callback (int item_ID, void *client_data, void *call_data)
{
	*((float *) client_data) = *((long *) call_data);
}

static void
ok_callback (int item_ID, void *client_data, void *call_data)
{
	gimp_close_dialog (dialog_id, 1);
}

static void
cancel_callback (int item_ID, void *client_data, void *call_data)
{
	gimp_close_dialog (dialog_id, 0);
}

static void
text_callback (int item_id, void *client_data, void *call_data)
{
	*((long *) client_data) = atoi ((char *) call_data);
}				/*

				   text_callback 
				 */
