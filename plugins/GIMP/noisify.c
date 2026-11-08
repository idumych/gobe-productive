/*
 * This is a plugin for the GIMP.
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
 * $Id: noisify.c,v 1.2 1999/05/26 23:37:28 wombat Exp $
 *
 * @(GIMP)         = <plug-in noisify "Noise/Add Noise">
 * @(GIMP_DEP)     = <noisify.c>
 * @(GIMP_OBJ)     = <noisify.o>
 * @(GIMP_LIB)     = <c>
 * @(GIMP_AUTHOR)  = <Torsten Martinsen>
 * @(GIMP_EMAIL)   = <bullestock@dk-online.dk>
 * @(GIMP_DESC)    = <Add noise to an image.>
 * @(GIMP_VERSION) = <Revision: 1.7>
 * @(GIMP_URL)     = <http://www2.dk-online.dk/Users/Torsten_Martinsen/gimp/index.html>
 */

/*
 * This filter adds random noise to an image.
 * The amount of noise can be set individually for each RGB channel.
 * You can choose between normally distributed (Gaussian) or
 * uniformly distributed noise.
 * This filter does not operate on indexed images.
 */

#include <stdlib.h>
#include <time.h>
#include "gimp.h"

/*
 * Change this if your system doesn't have random().
 */
/* WARNING: Don't use random(), since RAND_MAX is defined for rand() */
#define RANDOM	rand
#define SRANDOM	srand

static void noisify(Image, Image);
static void scale_callback(int, void *, void *);
static void ok_callback(int, void *, void *);
static void cancel_callback(int, void *, void *);
static void toggle_callback(int item_ID, void *client_data, void *call_data);
static void toggle_callback_lock(int item_ID, void *client_data,
				 void *call_data);
static int gauss(int);
static int uniform(int);

typedef int (*RandFunc) (int);

typedef struct {
    int	mode;		/* 0: uniform, 1: normal */
    int	lock;
    int	grey;
    int amount[3];
} Data;

static int dialog_ID;
static Data * data;
static int type;
static int scale_ID[3];

int
main(int argc, char **argv)
{
    Image input, output;
    int group_ID, mode_ID, temp_ID, lock_ID, grey_ID;
    

    if (gimp_init(argc, argv)) {
	input = 0;
	output = 0;

	input = gimp_get_input_image(0);
	type = gimp_image_type(input);
	if (input)
	    switch (type) {
	    case RGB_IMAGE:
	    case GRAY_IMAGE:
	    case RGBA_IMAGE:
	    case GRAYA_IMAGE:
		data = gimp_get_params();
		if (!data) {
		    data = malloc(sizeof(Data));
		    data->mode = 1;
		    data->lock = 0;
		    data->grey = 1;
		    data->amount[0] = 20;
		    data->amount[1] = 20;
		    data->amount[2] = 20;
		}

		dialog_ID = gimp_new_dialog("Add Noise");
		gimp_new_label (dialog_ID, DEFAULT, "Options");
		group_ID = gimp_new_row_group(dialog_ID, DEFAULT, NORMAL, "");
		mode_ID = gimp_new_check_button(dialog_ID, group_ID, "Gaussian");
		gimp_change_item(dialog_ID, mode_ID,
				 sizeof(data->mode), &data->mode);
		gimp_add_callback(dialog_ID, mode_ID,
				  toggle_callback, &data->mode);
		if ((type == GRAY_IMAGE) || (type == GRAYA_IMAGE)) {
		    scale_ID[0] = gimp_new_scale(dialog_ID, group_ID,
						 1, 100, data->amount[0], 0);
		    temp_ID = gimp_new_column_group (dialog_ID,
						     group_ID, NORMAL, "");
		    gimp_new_label (dialog_ID, temp_ID, "Level");
		    gimp_add_callback(dialog_ID, scale_ID[0],
				      scale_callback, &data->amount[0]);
		} else {
		    lock_ID = gimp_new_check_button(dialog_ID, group_ID,
						    "Lock RGB");
		    gimp_change_item(dialog_ID, lock_ID,
				     sizeof(data->lock), &data->lock);
		    gimp_add_callback(dialog_ID, lock_ID,
				      toggle_callback_lock, &data->lock);
		    grey_ID = gimp_new_check_button(dialog_ID, group_ID, "Grey");
		    gimp_change_item(dialog_ID, grey_ID,
				     sizeof(data->grey), &data->grey);
		    gimp_add_callback(dialog_ID, grey_ID,
				      toggle_callback, &data->grey);

		    temp_ID = gimp_new_row_group (dialog_ID,
						  group_ID, NORMAL, "");
		    gimp_new_label (dialog_ID, temp_ID, "Red");
		    scale_ID[0] = gimp_new_scale(dialog_ID, temp_ID,
						 1, 100, data->amount[0], 0);
		    
		    temp_ID = gimp_new_row_group (dialog_ID,
						  group_ID, NORMAL, "");
		    gimp_new_label (dialog_ID, temp_ID, "Green");
		    scale_ID[1] = gimp_new_scale(dialog_ID, temp_ID,
						 1, 100, data->amount[1], 0);
		    
		    temp_ID = gimp_new_row_group (dialog_ID,
						  group_ID, NORMAL, "");
		    gimp_new_label (dialog_ID, temp_ID, "Blue");
		    scale_ID[2] = gimp_new_scale(dialog_ID, temp_ID,
						 1, 100, data->amount[2], 0);
		    
		    gimp_add_callback(dialog_ID, scale_ID[0],
				      scale_callback, &data->amount[0]);
		    gimp_add_callback(dialog_ID, scale_ID[1],
				      scale_callback, &data->amount[1]);
		    gimp_add_callback(dialog_ID, scale_ID[2],
				      scale_callback, &data->amount[2]);
		}
		gimp_add_callback(dialog_ID, gimp_ok_item_id(dialog_ID),
				  ok_callback, 0);
		gimp_add_callback(dialog_ID, gimp_cancel_item_id(dialog_ID),
				  cancel_callback, 0);

		if (gimp_show_dialog(dialog_ID)) {
		    gimp_set_params(sizeof(Data), data);

		    output = gimp_get_output_image(0);
		    if (output) {
			gimp_init_progress("Add Noise");
			noisify(input, output);
			gimp_update_image(output);
		    }
		}
		break;
	    case INDEXED_IMAGE:
		gimp_message("Add Noise: cannot operate on indexed color images");
		break;
	    default:
		gimp_message("Add Noise: cannot operate on unknown image types");
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

static void
noisify(Image input, Image output)
{
    long width, height;
    long channels, rowstride;
    unsigned char *src_row, *dest_row;
    unsigned char *src, *dest;
    short row, col;
    int x1, y1, x2, y2, p;
    int ra, ga, ba, rv, gv, bv, rgb, alpha;
    RandFunc randfunc;
    
    gimp_image_area(input, &x1, &y1, &x2, &y2);

    width = gimp_image_width(input);
    height = gimp_image_height(input);
    channels = gimp_image_channels(input);
    rowstride = width * channels;
    rgb = (channels >= 3);
    alpha = (channels == 2) || (channels == 4);
    
    src_row = gimp_image_data(input);
    dest_row = gimp_image_data(output);

    x1 *= channels;    x2 *= channels;

    src_row += rowstride * y1 + x1;
    dest_row += rowstride * y1 + x1;

    randfunc = data->mode ? gauss : uniform;
    ra = data->amount[0];
    ga = data->amount[1];
    ba = data->amount[2];
    
    SRANDOM(time(NULL));
    
    for (row = y1; row < y2; row++) {
	src = src_row;
	dest = dest_row;

	for (col = x1; col < x2; col += channels) {
	    p = *src++ + (rv = randfunc(ra));
	    if (p < 0)
		p = 0;
	    else if (p > 255)
		p = 255;
	    *dest++ = p;
	    if (rgb) {
		if (data->grey)
		    gv = bv = rv;
		else {
		    gv = randfunc(ga);
		    bv = randfunc(ba);
		}
		p = *src++ + gv;
		if (p < 0)
		    p = 0;
		else if (p > 255)
		    p = 255;
		*dest++ = p;
		p = *src++ + bv;
		if (p < 0)
		    p = 0;
		else if (p > 255)
		    p = 255;
		*dest++ = p;
	    }
	    if (alpha)
		*dest++ = *src++;
	}
	src_row += rowstride;
	dest_row += rowstride;
	if ((row % 5) == 0)
	    gimp_do_progress(row, y2 - y1);
    }
}

static void
scale_callback(int item_ID, void *client_data, void *call_data)
{
    int i;
    
    if ((type == RGB_IMAGE) && data->lock)
	for (i = 0; i < 3; ++i) {
	    data->amount[i] = *((long *) call_data);
	    if (&data->amount[i] != client_data)
		gimp_change_item(dialog_ID, scale_ID[i],
				 sizeof(data->amount[i]), &data->amount[i]);
	}
    else
	*((int *) client_data) = *((long *) call_data);
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
toggle_callback(int item_ID, void *client_data, void *call_data)
{
    *((int *) client_data) = *((long *) call_data);
}

static void
toggle_callback_lock(int item_ID, void *client_data, void *call_data)
{
    int i;
    *((int *) client_data) = *((long *) call_data);
#if 0
    if ((type == RGB_IMAGE) && data->lock)
	for (i = 1; i < 3; ++i) {
	    data->amount[i] = data->amount[0];
	    gimp_change_item(dialog_ID, scale_ID[i],
			     sizeof(data->amount[0]), &data->amount[0]);
	}
#endif
}

/*
 * Return a Gaussian (aka normal) random variable.
 *
 * Adapted from ppmforge.c, which is part of PBMPLUS.
 * The algorithm comes from:
 * 'The Science Of Fractal Images'. Peitgen, H.-O., and Saupe, D. eds.
 * Springer Verlag, New York, 1988.
 */
static int
gauss(int scale)
{
    double sum;

    sum = (RANDOM() & 0x7FFF) + (RANDOM() & 0x7FFF) +
	(RANDOM() & 0x7FFF) + (RANDOM() & 0x7FFF);

    return (int) (scale * (sum * 5.28596089837e-5 - 3.46410161514));
}

/*
 * Return a uniform random variable in the range from -scale to scale.
 */
static int
uniform(int scale)
{
	return RANDOM() / (RAND_MAX/(2*scale)) - scale;
}
