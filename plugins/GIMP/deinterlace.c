/* De-interlace 1.01 --- image filter plug-in for The Gimp image
 * manipulation program
 *
 * Copyright (C) 1996 Federico Mena Quintero
 *
 * You can contact me at quartic@polloux.fciencias.unam.mx
 * You can contact the original The Gimp authors at gimp@xcf.berkeley.edu
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

/* This plug-in is used for the following...  When an image is taken
 * from a video capture (f.i. a freeze-frame), sometimes only either
 * the odd or the even fields get captured, resulting in an image
 * which, line by line, is data-black-data-black-etc.  This plug-in
 * does an interpolation between the odd or even lines in an image to
 * correct this.
 */


#include <stdio.h>
#include <stdlib.h>

#include "gimp.h"


#ifndef _AIX
typedef unsigned char uchar;
#endif


/***** Local functions *****/

static void radio_callback(int dialog_id, void *client_data, void *call_data);
static void ok_callback(int item_id, void *client_data, void *call_data);
static void cancel_callback(int item_id, void *client_data, void *call_data);

static void deinterlace(Image source_img, Image dest_img, long keep);


/***** Local vars *****/

static char *prog_name;
static int   dialog_id;


/***** Functions *****/

/*****/

int
main(int argc, char **argv)
{
	Image  source, dest;
	int    group_id;
	int    even_id, odd_id;
	long   even, odd;

	/* Save program name */

	prog_name = argv[0];

	/* Initialize plug-in and continue if success */

	if (!gimp_init(argc, argv))
		return 0;

	source = gimp_get_input_image(0);
	dest   = gimp_get_output_image(0);

	if (source && dest) {
		if ((gimp_image_type(source) == RGB_IMAGE) ||
		    (gimp_image_type(source) == GRAY_IMAGE)) {
			even = 1;
			odd  = 0;

			dialog_id = gimp_new_dialog("De-interlace");

			group_id = gimp_new_row_group(dialog_id, DEFAULT,
						      RADIO, "");

			even_id = gimp_new_radio_button(dialog_id, group_id,
							"Keep even fields");
			gimp_change_item(dialog_id, even_id, sizeof(even), &even);

			odd_id = gimp_new_radio_button(dialog_id, group_id,
						       "Keep odd fields");
			gimp_change_item(dialog_id, odd_id, sizeof(odd), &odd);

			gimp_add_callback(dialog_id, even_id, radio_callback, &even);
			gimp_add_callback(dialog_id, odd_id, radio_callback, &odd);

			gimp_add_callback(dialog_id, gimp_ok_item_id(dialog_id),
					  ok_callback, NULL);

			gimp_add_callback(dialog_id, gimp_cancel_item_id(dialog_id),
					  cancel_callback, NULL);

			if (gimp_show_dialog(dialog_id)) {
				gimp_init_progress("De-interlace");

				deinterlace(source, dest, even ? 0 : 1);

				gimp_update_image(dest);
			} /* if */
		} else
			gimp_message("De-interlace: can only operate on 24-bit or grayscale images");

		gimp_free_image(source);
		gimp_free_image(dest);
	} /* if */

	gimp_quit();
	return 0;
} /* main */


/*****/

static void
radio_callback(int dialog_id, void *client_data, void *call_data)
{
	*((long *) client_data) = *((long *) call_data);
} /* radio_callback */


/*****/

static void
ok_callback(int item_id, void *client_data, void *call_data)
{
	gimp_close_dialog(dialog_id, 1);
} /* ok_callback */


/*****/

static void
cancel_callback(int item_id, void *client_data, void *call_data)
{
	gimp_close_dialog(dialog_id, 0);
} /* cancel_callback */


/*****/

static void
deinterlace(Image source_img, Image dest_img, long keep)
{
	long   width, height;
	long   channels, rowsiz;
	long   sel_width;
	uchar *src_line, *dest_line;
	uchar *src, *dest;
	int    progress, max_progress;
	int    x1, y1, x2, y2;
	int    x, y;

	/* Get selection area */

	gimp_image_area(source_img, &x1, &y1, &x2, &y2);

	width    = gimp_image_width(source_img);
	height   = gimp_image_height(source_img);
	channels = gimp_image_channels(source_img);
	rowsiz   = width * channels;

	progress     = 0;
	max_progress = y2 - y1;

	sel_width = channels * (x2 - x1);

	/* Get image data */

	src_line  = (uchar *) gimp_image_data(source_img) +
		    channels * (width * y1 + x1);
	
	dest_line = (uchar *) gimp_image_data(dest_img) +
		    channels * (width * y1 + x1);

	for (y = y1; y < y2; y++) {
		src  = src_line;
		dest = dest_line;

		/* If we are on a line corresponding to the 'keep', or
                   if we are on an image border line (i.e. one which
                   has *not* two surrounding lines), just copy it to
                   the destination image.  Else, interpolate among the
                   two surrounding lines. */

		if ((y % 2 == keep) || (y - 1 < 0) || (y + 1 >= height))
			for (x = sel_width; x; x--)  
				*dest++ = *src++;
		else
			for (x = sel_width; x; x--) {
				*dest++ = (*(src - rowsiz)
					   + *(src + rowsiz)) >> 1;
				
				src++;
			} /* for */
		
		src_line  += rowsiz;
		dest_line += rowsiz;

		/* Update progress */

		progress++;
		if (progress % 64 == 0)
			gimp_do_progress(progress, max_progress);
	} /* for */
} /* deinterlace */
