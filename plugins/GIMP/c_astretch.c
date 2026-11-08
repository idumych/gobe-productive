/* Contrast Autostretch 1.04 --- image filter plug-in for The Gimp image
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

/* This simple plug-in does an automatic contrast stretch.  For each
   channel in the image, it finds the minimum and maximum values... it
   uses those values to stretch the individual histograms to the full
   contrast range.  For some images it may do just what you want; for
   others it may be total crap :) */


#include <stdio.h>
#include <stdlib.h>

#include "gimp.h"


#ifndef _AIX
typedef unsigned char uchar;
#endif


/***** Local functions *****/

static void do_contrast_stretch(Image source_img, Image dest_img);


/***** Local vars *****/

static char   *prog_name;


/***** Functions *****/

/*****/

int
main(int argc, char **argv)
{
	Image  source, dest;

	/* Save program name */

	prog_name = argv[0];

	/* Initialize filter and continue if success */

	if (!gimp_init(argc, argv))
		return 0;

	source = gimp_get_input_image(0);
	dest   = gimp_get_output_image(0);
	
	if (source && dest) {
		if ((gimp_image_type(source) == RGB_IMAGE) || gimp_image_type(source) == RGBA_IMAGE ||
		    (gimp_image_type(source) == GRAY_IMAGE)) {
			gimp_init_progress("Contrast Autostretch");
			
			do_contrast_stretch(source, dest);
			
			gimp_update_image(dest);
		} else
			gimp_message("Contrast Autostretch: can only operate on 24-bit or grayscale images");
			
		gimp_free_image(source);
		gimp_free_image(dest);
	} /* if */
	
	gimp_quit();
	
	return 0;
} /* main */
    

/*****/

static void
do_contrast_stretch(Image source_img, Image dest_img)
{
	long   src_width, src_height;
	long   channels, rowsiz;
	uchar *img_src, *img_dest;
	uchar *src, *dest;
	uchar *src_line, *dest_line;
	uchar  min[3], max[3];  /* Minimums and maximums */
	uchar  range;
	uchar  lut[256][3];
	int    progress, max_progress;

	int    x1, y1, x2, y2;

	long   x, y;
	long   k;

	/* Get selection area */

	gimp_image_area(source_img, &x1, &y1, &x2, &y2);

	src_width  = gimp_image_width(source_img);
	src_height = gimp_image_height(source_img);
	channels   = gimp_image_channels(source_img);
	rowsiz     = src_width * channels;

	/* Get image data */

	img_src  = (uchar *) gimp_image_data(source_img)
		   + channels * (src_width * y1 + x1);
	img_dest = (uchar *) gimp_image_data(dest_img)
		   + channels * (src_width * y1 + x1);

	/* Initialize progress */

	progress = 0;
	max_progress = (y2 - y1) * 2;

	/* Get minimum and maximum values for each channel */

	min[0] = min[1] = min[2] = 255;
	max[0] = max[1] = max[2] = 0;

	src_line  = img_src;
	
	for (y = y1; y < y2; y++) {
		src  = src_line;

		for (x = x1; x < x2; x++)
			for (k = 0; k < channels; k++) {
				if (k < 3)
				{
					if (*src < min[k])
						min[k] = *src;
	
					if (*src > max[k])
						max[k] = *src;
				}
				src++;
			} /* for */

		src_line  += rowsiz;

		/* Update progress */

		progress++;
		if (progress % 10 == 0)
			gimp_do_progress(progress, max_progress);
	} /* for */
				
	/* Calculate LUTs with stretched contrast */

	for (k = 0; k < 3; k++) {
		range = max[k] - min[k];

		if (range != 0)
			for (x = min[k]; x <= max[k]; x++)
				lut[x][k] = 255 * (x - min[k]) / range;
		else
			lut[min[k]][k] = min[k];
	} /* for */

	/* Now substitute pixel vales */
	
	src_line  = img_src;
	dest_line = img_dest;
	
	for (y = y1; y < y2; y++) {
		src  = src_line;
		dest = dest_line;

		for (x = x1; x < x2; x++)
			for (k = 0; k < channels; k++)
			{
				if (k >= 3)
				{
					*dest++ = 255;
					*src++;
				}
				else
					*dest++ = lut[*src++][k];
			}

		src_line  += rowsiz;
		dest_line += rowsiz;

		/* Update progress */

		progress++;
		if (progress % 64 == 0)
			gimp_do_progress(progress, max_progress);
	} /* for */
} /* do_contrast_stretch */
