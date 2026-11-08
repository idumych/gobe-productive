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
 * $Id: oilify.c,v 1.1 1999/02/09 00:09:06 wombat Exp $
 *
 * @(GIMP)         = <plug-in oilify "Effects/Oil Paint">
 * @(GIMP_DEP)     = <oilify.c>
 * @(GIMP_OBJ)     = <oilify.o>
 * @(GIMP_LIB)     = <c>
 * @(GIMP_AUTHOR)  = <Torsten Martinsen>
 * @(GIMP_EMAIL)   = <bullestock@dk-online.dk>
 * @(GIMP_DESC)    = <Do the 'oil paint' effect.>
 * @(GIMP_VERSION) = <Revision: 1.5>
 * @(GIMP_URL)     = <http://www2.dk-online.dk/Users/Torsten_Martinsen/gimp/index.html>
 */

#include "gimp.h"
#include <stdio.h>

/*
 * This filter does the well-known 'oil paint' effect.
 */

static void oilify(Image, Image);

static char *prog_name;
static int masksize = 3;
static int dialog_ID;

static void scale_callback(int, void *, void *);
static void ok_callback(int, void *, void *);
static void cancel_callback(int, void *, void *);

#define HISTSIZE	256

int
main(int argc, char **argv)
{
    Image input = 0, output = 0;
    void *data;
    char msg[25];
    int group_ID, scale_ID;

    prog_name = argv[0];

    if (gimp_init(argc, argv)) {
	input = gimp_get_input_image(0);

	if (input)
	    switch (gimp_image_type(input)) {
	    case RGB_IMAGE:
	    case GRAY_IMAGE:
	    case RGBA_IMAGE:
	    case GRAYA_IMAGE:
	    case INDEXED_IMAGE:
		data = gimp_get_params();
		if (data)
		    masksize = ((long *) data)[0];

		dialog_ID = gimp_new_dialog("Oil Paint");
		group_ID = gimp_new_row_group(dialog_ID, DEFAULT, NORMAL, "");
		gimp_new_label(dialog_ID, group_ID, "Mask size");
		/*
		 * Unfortunately, there is currently no way to specify
		 * a scale increment other than 1.
		 */
		scale_ID = gimp_new_scale(dialog_ID, group_ID, 3, 25,
					  masksize, 0);
		gimp_add_callback(dialog_ID, scale_ID,
				  scale_callback, &masksize);
		gimp_add_callback(dialog_ID, gimp_ok_item_id(dialog_ID),
				  ok_callback, 0);
		gimp_add_callback(dialog_ID, gimp_cancel_item_id(dialog_ID),
				  cancel_callback, 0);

		if (gimp_show_dialog(dialog_ID)) {
		    gimp_set_params(sizeof(int), &masksize);

		    output = gimp_get_output_image(0);
		    if (output) {
			sprintf(msg, "Oil Paint (mask size %d)", masksize);
			gimp_init_progress(msg);
			oilify(input, output);
			gimp_update_image(output);
		    }
		}
		break;
	    default:
		gimp_message("Oil Paint: cannot operate on unknown image types");
		break;
	    }
	if (input)
	    gimp_free_image(input);
	if (output)
	    gimp_free_image(output);

	gimp_quit();
    }
    return 0;
}

/*
 * For each RGB channel, replace the pixel at (x,y) with the
 * value that occurs most often in the n x n chunk centered
 * at (x,y).
 */
static void
oilify(Image input, Image output)
{
    long width, height;
    long channels, rowstride;
    unsigned char *src, *src_row, *dest, *dest_row, *p;
    int x, y, c, i, px, xx, yy, xv, yv, n, rgb, alpha;
    int x1, y1, x2, y2;
    int rVal, gVal, bVal;
    int rCnt, gCnt, bCnt;
    int rHist[HISTSIZE], gHist[HISTSIZE], bHist[HISTSIZE];


    gimp_image_area(input, &x1, &y1, &x2, &y2);

    width = gimp_image_width(input);
    height = gimp_image_height(input);
    channels = gimp_image_channels(input);
    rowstride = width * channels;
    rgb = (channels >= 3);
    alpha = (channels == 2) || (channels == 4);

    src = gimp_image_data(input);
    dest_row = gimp_image_data(output);
    dest_row += rowstride * y1 + x1 * channels;

    n = masksize / 2;

    for (y = y1; y < y2; y++) {
	dest = dest_row;
	for (x = x1; x < x2; x++) {
	    rCnt = gCnt = bCnt = 0;
	    rVal = gVal = bVal = 0;
	    for (i = 0; i < HISTSIZE; i++)
		rHist[i] = gHist[i] = bHist[i] = 0;
	    for (yy = -n; yy <= n; yy++) {
		for (xx = -n; xx <= n; xx++) {
		    xv = x + xx;
		    yv = y + yy;
		    if (xv < x1 || yv < y1)
			continue;
		    if (xv >= x2 || yv >= y2)
			continue;
		    p = src + yv * rowstride + xv * channels;
		    if ((c = ++rHist[px = p[0]]) > rCnt) {
			rVal = px;
			rCnt = c;
		    }
		    if (rgb) {
			if ((c = ++gHist[px = p[1]]) > gCnt) {
			    gVal = px;
			    gCnt = c;
			}
			if ((c = ++bHist[px = p[2]]) > bCnt) {
			    bVal = px;
			    bCnt = c;
			}
		    }
		}
	    }
	    *dest++ = rVal;
	    if (rgb) {
		*dest++ = gVal;
		*dest++ = bVal;
	    }
	    if (alpha)
		++dest;
	}
	dest_row += rowstride;
	if ((y % 5) == 0)
	    gimp_do_progress(y, y2 - y1);
    }

    /* Copy alpha channel */
    if (!alpha)
	return;
    
    src_row = gimp_image_data(input) + rowstride * y1 + (x1+1) * channels - 1;
    dest_row = gimp_image_data(output) + rowstride * y1 + (x1+1) * channels - 1;
    for (y = y1; y < y2; ++y) {
	src = src_row;
	dest = dest_row;
	for (x = x1; x < x2; ++x) {
	    *dest = *src;
	    dest += channels;
	    src += channels;
	}
	dest_row += rowstride;
	src_row += rowstride;
    }
}

static void
scale_callback(int item_ID, void *client_data, void *call_data)
{
    int n;

    n = *((long *) call_data);
    *((int *) client_data) = n | 1;
}

static void
ok_callback(int item_ID, void *client_data, void *call_data)
{
    gimp_close_dialog(dialog_ID, 1);
}

static void
cancel_callback(int item_ID, void *client_data, void *call_data)
{
    gimp_close_dialog(dialog_ID, 0);
}
