/*
 * This is a plug-in for the GIMP.
 *
 * Copyright (C) 1996 Brent Burton
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
 * This file has been modified from the original.  A bug has been fixed at the
 * end of inblock (DIV/0) <Scott Lindsey 5/28/98>
 */

#include "gimp.h"
#include <stdio.h>
#include <stdlib.h>

/*
 * This plug-in creates a checkerboard pattern using
 * squares of variable size.  Also, the user can specify a regular
 * checker pattern or the ramped (1,2,3,4,...,N,...,4,3,2,1,2,3....) pattern.
 *  v. 1.1 of 6/25/96  - Brent Burton, brentb@io.com
 *
 * [The skeleton code came from a similarly straight-forward filter,
 *  the variable blurry plug-in.  (Thanks!)]
 */

static void check(Image, Image);
static int dialog_ID;

/* Variables set in dialog box */
typedef struct data {
    int size;
    int mode;
} Data;
static Data data = {10, 0};

static void toggle_callback(int, void *, void *);
static void scale_callback(int, void *, void *);
static void ok_callback(int, void *, void *);
static void cancel_callback(int, void *, void *);

static int inblock(int pos, int size);

int
main(int argc, char **argv)
{
    Image input = 0, output = 0;
    int group_ID;
    int scaler_ID;
    int mode_ID;		/* mode checkbox */

    if (gimp_init(argc, argv)) {
	input = gimp_get_input_image(0);

	if (input) {
	    if ((gimp_image_type(input) == RGB_IMAGE) ||
		(gimp_image_type(input) == GRAY_IMAGE))
	      {
		  Data *d;
		  d = (Data*)gimp_get_params();
		  if (d)	/* Retrieve old settings for... */
		    data = *d;	/* "conceptual continuity".  FZ rocks. */

		  dialog_ID = gimp_new_dialog("Checkerboard");
		  gimp_add_callback(dialog_ID, gimp_ok_item_id(dialog_ID),
				    ok_callback, 0);
		  gimp_add_callback(dialog_ID, gimp_cancel_item_id(dialog_ID),
				    cancel_callback, 0);

		  group_ID = gimp_new_row_group(dialog_ID, DEFAULT,NORMAL,"");

		  /*
		   * Check button ==>  normal or ramped checker
		   */
		  mode_ID = gimp_new_check_button(dialog_ID, group_ID,
						  "Psychobilly");
		  gimp_change_item(dialog_ID, mode_ID,
				   sizeof(data.mode), &data.mode);
		  gimp_add_callback(dialog_ID, mode_ID,
				    toggle_callback, &data.mode);

		  /*
		   * Slider ==> size of boxes, or size of largest box.
		   */
		  scaler_ID = gimp_new_scale(dialog_ID, group_ID, 1, 40,
					     data.size, 0);
		  gimp_new_label(dialog_ID, scaler_ID, "Size:");
		  gimp_add_callback(dialog_ID, scaler_ID,
				    scale_callback, &data.size);

		  /* -- */
		  if (gimp_show_dialog(dialog_ID)) {
		      gimp_set_params(sizeof(Data), &data);

		      output = gimp_get_output_image(0);
		      if (output) {
			  check(input, output);
			  gimp_update_image(output);
		      }
		  }
	    } else
	      gimp_message("check: cannot operate on indexed color images");
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
check(Image input, Image output)
{
    long width, height;
    long channels, rowstride;
    unsigned char *src;
    unsigned char *dest;
    unsigned char *dest_row;
    int x, y;
    int x1, y1, x2, y2;
	unsigned char colors[2][3];

	gimp_foreground_color(&colors[0][0], &colors[0][1], &colors[0][2]);
	gimp_background_color(&colors[1][0], &colors[1][1], &colors[1][2]);

	

    gimp_image_area(input, &x1, &y1, &x2, &y2);

    width = gimp_image_width(input);
    height = gimp_image_height(input);
    channels = gimp_image_channels(input);
    rowstride = width * channels;

    src = gimp_image_data(input);
    dest_row = gimp_image_data(output);
    dest_row += rowstride * y1 + x1 * channels;

    for (y = y1; y < y2; y++) {
	dest = dest_row;
	for (x = x1; x < x2; x++) {
	    int val, xp, yp;

	    /* ---- */
	    /* determine which region pixel is inside. */

	    if ( data.mode )  {
		/* ramped mode: 1,2,3,4,...,N,...,4,3,2,1 */
		val = (inblock(x, data.size) == inblock(y, data.size)) ?
		  0 : 1;
	    } else {
		/* Normal, regular checkerboard mode.
		 * Determine base factor (even or odd) of block
		 * this x/y position is in.
		 */
		xp = x/data.size;
		yp = y/data.size;
		/* if both even or odd, color sqr */
		val = ( (xp&1) == (yp&1) ) ? 0 : 1;
	    }

	    *dest++ = colors[val][0];
	    if (channels > 1) {
		*dest++ = colors[val][1];
		*dest++ = colors[val][2];
	    }
		if (channels == 4)
			*dest++ = 255; // Solid alpha
	    /* ---- */
	    
	}
	dest_row += rowstride;
    }
}


static int
inblock( int pos, int size)
{
    static int *in = NULL;		/* initialized first time */
    int i,j,k, len;

    len = size*size;
    /*
     * Initialize the array; since we'll be called thousands of
     * times with the same size value, precompute the array.
     */
    if (in == NULL) {
	in = (int*)malloc(sizeof(int)* len);
	if (in == NULL) {
	    return 0;
	} else {
	    int cell = 1;	/* cell value */
	    /*
	     * i is absolute index into in[]
	     * j is current number of blocks to fill in with a 1 or 0.
	     * k is just counter for the j cells.
	     */
	    i=0;
	    for (j=1; j<=size; j++ ) { /* first half */
		for (k=0; k<j; k++ ) {
		    in[i++] = cell;
		}
		cell = !cell;
	    }
	    for ( j=size-1; j>=1; j--) { /* second half */
		for (k=0; k<j; k++ ) {
		    in[i++] = cell;
		}
		cell = !cell;
	    }
	}
    } /* if n NULL */

	if (len > 1)
		pos %= len - 1;
	else
		pos = 0;

    /* place pos within 0..(len-1) grid and return the value. */
    return in[ pos ];
}


static void
toggle_callback(int item_ID, void *client_data, void *call_data)
{
    *((int *) client_data) = *((long *) call_data);
}

static void
scale_callback(int item_ID, void *client_data, void *call_data)
{
    *((int *) client_data) = (int)*((long *) call_data);
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

