/*
 * This is a plug-in for the GIMP.
 *
 * Copyright (C) 1995 Spencer Kimball and Peter Mattis
 * Copyright (C) 1996 Torsten Martinsen <bullestock@dk-online.dk>
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
 * $Id: engrave.c,v 1.1 1999/02/09 00:08:59 wombat Exp $
 *
 * @(GIMP)         = <plug-in engrave "Effects/Engrave">
 * @(GIMP_DEP)     = <engrave.c>
 * @(GIMP_OBJ)     = <engrave.o>
 * @(GIMP_LIB)     = <c>
 * @(GIMP_AUTHOR)  = <Torsten Martinsen>
 * @(GIMP_EMAIL)   = <bullestock@dk-online.dk>
 * @(GIMP_DESC)    = <Engrave an image.>
 * @(GIMP_VERSION) = <Revision: 1.8>
 * @(GIMP_URL)     = <http://www2.dk-online.dk/Users/Torsten_Martinsen/gimp/index.html>
 */

/*
* This file has been modified from the original. Relationships between labels and fields
* in the dialog have been changed for aesthetic reasons.  <Scott Lindsey 7/21/98>
*/

/*
 * This plug-in creates a black-and-white 'engraved' version of an image.
 * Much of the code is stolen from the Pixelize plug-in.
 */

#include "gimp.h"

typedef struct {
    long height;
    long limit;
} engrave_params_t;

static void scale_callback(int, void *, void *);
static void ok_callback(int, void *, void *);
static void cancel_callback(int, void *, void *);
static void toggle_callback(int item_ID, void *client_data, void *call_data);
static void engrave(Image, Image);

static engrave_params_t params = { 4, 1 };
static int dialog_ID;

#define R 0
#define G 1
#define B 2

#define INTENSITY(r,g,b) (r * 0.30 + g * 0.59 + b * 0.11)

int
main(int argc, char **argv)
{
    Image input, output;
    void *data;
    int scale_ID, group_ID, temp_ID, limit_ID;

    if (gimp_init(argc, argv)) {
	data = gimp_get_params();
	if (data)
	    params = *((engrave_params_t *) data);

	input = output = 0;

	input = gimp_get_input_image(0);

	if (input)
	    switch (gimp_image_type(input)) {
	    case RGB_IMAGE:
	    case GRAY_IMAGE:
	    case RGBA_IMAGE:
	    case GRAYA_IMAGE:
	    case INDEXED_IMAGE:
		dialog_ID = gimp_new_dialog("Engrave");
		group_ID = gimp_new_row_group(dialog_ID, DEFAULT, NORMAL, "");

		limit_ID = gimp_new_check_button(dialog_ID, group_ID,
						 "Limit line width");
		gimp_change_item(dialog_ID, limit_ID,
				 sizeof(params.limit), &params.limit);
		gimp_add_callback(dialog_ID, limit_ID,
				  toggle_callback, &params.limit);
		
		scale_ID = gimp_new_scale(dialog_ID, group_ID, 2, 16,
					  params.height, 0);
		gimp_new_label(dialog_ID, scale_ID, "Mask size");
		gimp_add_callback(dialog_ID,
				  scale_ID, scale_callback, &params.height);
		gimp_add_callback(dialog_ID,
				  gimp_ok_item_id(dialog_ID), ok_callback, 0);
		gimp_add_callback(dialog_ID, gimp_cancel_item_id(dialog_ID),
				  cancel_callback, 0);

		if (gimp_show_dialog(dialog_ID)) {
		    gimp_set_params(sizeof(engrave_params_t), &params);

		    output = gimp_get_output_image(0);
		    if (output) {
			gimp_init_progress("Engrave");
			engrave(input, output);
			gimp_update_image(output);
		    }
		}
		break;
	    default:
		gimp_message("engrave: cannot operate on unknown image types");
		break;
	    }
	/* Free both images.
	 */
	if (input)
	    gimp_free_image(input);
	if (output)
	    gimp_free_image(output);

	/* Quit
	 */
	gimp_quit();
    }
    return 0;
}

static void
scale_callback(int item_ID, void *client_data, void *call_data)
{
    *((long *) client_data) = *((long *) call_data);
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

static void
engrave(Image input, Image output)
{
    long width, height;
    long channels, rowstride;
    unsigned char *src_row, *dest_row;
    unsigned char *src, *dest, *cmap;
    short row, col;
    int x1, y1, x2, y2, i, j, inten, v, type, white, alpha;
    unsigned int red_average, green_average, blue_average, count;
    static unsigned char mycmap[] = { 0, 0, 0, 255, 255, 255 };
    
    gimp_image_area(input, &x1, &y1, &x2, &y2);
    type = gimp_image_type(input);
    if (type == INDEXED_IMAGE) {
	cmap = gimp_image_cmap (input);
	white = 1;
	gimp_set_image_colors(output, mycmap, 2);
    } else
	white = 255;

    width = gimp_image_width(input);
    height = gimp_image_height(input);
    channels = gimp_image_channels(input);
    rowstride = width * channels;
    alpha = (channels == 2) || (channels == 4);
    
    src_row = gimp_image_data(input);
    dest_row = gimp_image_data(output);

    src_row += rowstride * y1;
    dest_row += rowstride * y1;

    for (row = y1; row < y2; row += params.height - (row % params.height)) {
	src = src_row;
	dest = dest_row;
	for (col = x1; col < x2; ++col) {
	    red_average = 0;
	    green_average = 0;
	    blue_average = 0;
	    count = 0;
	    /* Compute the average values of the engrave square area */
	    if (type == INDEXED_IMAGE)
		for (j = 0; (j < params.height - (row % params.height)) && ((j + row) < y2); j++) {
		    src = src_row + j * rowstride + col * channels;
		    i = *src++ * 3;
		    red_average += cmap[i++];
		    green_average += cmap[i++];
		    blue_average += cmap[i];
		    count++;
		}
	    else
		for (j = 0;
		     (j < params.height - (row % params.height)) &&
			 ((j + row) < y2); j++) {
		    src = src_row + j * rowstride + col * channels;
		    red_average += *src++;
		    if (channels > 2) {
			green_average += *src++;
			blue_average += *src++;
		    }
		    src += alpha;
		    count++;
		}

	    if (count) {
		red_average = red_average / count;
		green_average = green_average / count;
		blue_average = blue_average / count;
	    }

	    if (channels < 3)
		inten = red_average/254.0*params.height;
	    else
		inten = INTENSITY(red_average,
				  green_average,
				  blue_average)/254.0*params.height;

	    for (j = 0;
		 (j < params.height - (row % params.height)) && ((j + row) < y2);
		 j++) {
		dest = dest_row + j * rowstride + col * channels;
		v = inten > j ? white : 0;
		if (params.limit) {
		    if (j == 0)
			v = white;
		    else if (j == params.height-1)
			v = 0;
		}
		*dest++ = v;
		if (channels > 2) {
		    *dest++ = v;
		    *dest++ = v;
		}
		dest += alpha;
	    }
	}
	src_row += rowstride * (params.height - (row % params.height));
	dest_row += rowstride * (params.height - (row % params.height));
	if ((row % 5) == 0)
	    gimp_do_progress(row, y2-y1);
    }

    if (!alpha)
	return;

    src_row = gimp_image_data(input) + rowstride * y1 + (x1+1) * channels - 1;
    dest_row = gimp_image_data(output) + rowstride * y1 + (x1+1) * channels - 1;
    for (row = y1; row < y2; ++row) {
	src = src_row;
	dest = dest_row;
	for (col = x1; col < x2; ++col) {
	    *dest = *src;
	    dest += channels;
	    src += channels;
	}
	dest_row += rowstride;
	src_row += rowstride;
    }

}

static void
toggle_callback(int item_ID, void *client_data, void *call_data)
{
  *((long *) client_data) = *((long *) call_data);
}
