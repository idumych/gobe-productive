/* Wave --- image filter plug-in for The Gimp image manipulation program.
 *
 * Copyright (C) 1996 Stephen Norris.
 *
 * Much of the code taken from the whirl plug-in.
 * Many hints and suggestions from Mena Quintero Federico
 * <quartic@polloux.fciencias.unam.mx>, who also wrote the whirl plug-in.
 */


/* This file has been modified from the original; converted to the ImageManip API */
/* Scott Lindsey 8/1/99 */


/* This program is free software; you can redistribute it and/or modify
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
 * You can contact the original The Gimp authors at gimp@xcf.berkeley.edu
 * You can contact me as srn@flibble.cs.su.oz.au.
 */

/* Version 1.11 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "GIMPGlue.h"
#include "gimp.h"
#include "ImageManipAddon.h"
#include "BitmapAccessor.h"
#include "Config.h"

const char addonName[] = "Waves";
const char addonInfo[] = "Apply waves to an image. Ported from the GIMP by Scott Lindsey <wombat@gobe.com>.  Original code by Stephen Norris";
const char addonCategory[] = "Distortion";
int32 addonVersion = 100;

/* Some useful macros */

//#define MIN(a, b) ((a) < (b) ? (a) : (b))
//#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define WITHIN(a, b, c) ((((a) <= (b)) && ((b) <= (c))) ? 1 : 0)

#ifndef _AIX
typedef unsigned char uchar;
#endif

static void wave(Image input, Image output, float amplitude, int32 wavelength, float phase,
		 int32 smear, int32 reflective);
static uchar bilinear(double x, double y, uchar *v);


status_t
Manipulate(
	BitmapAccessor *sourceBitmap,
	BMessage *ioExtension,
	bool checkOnly)
{
	switch(sourceBitmap->ColorSpace())
	{
		case B_RGB32:
		case B_RGBA32:
		case B_RGB24:
		case B_RGB24_BIG:
		case B_GRAYSCALE_8_BIT:
			break;
		default:
			return B_NOT_ALLOWED;
	}

	if (checkOnly)
		return B_OK;

	Prefs prefs;
	LoadPrefs(&prefs);

	gimp_init_glue(sourceBitmap, NULL, ioExtension);

	if (ioExtension)
	{
		int32 amount;
		if (!ioExtension->FindInt32("waves/type", &amount))
			prefs.type = amount;
		if (!ioExtension->FindInt32("waves/amplitude", &amount))
			prefs.amplitude = amount;
		if (!ioExtension->FindInt32("waves/phase", &amount))
			prefs.phase = amount;
		if (!ioExtension->FindInt32("waves/wavelength", &amount))
			prefs.wavelength = amount;

		bool b;
		if (!ioExtension->FindBool("waves/reflective", &b))
			prefs.reflective = b;
	}

	Image input = new GIMPImage(sourceBitmap);
	
    Image temp = gimp_temp_image();

	wave(input, temp, ((float)prefs.amplitude / 10.0), 
					prefs.wavelength, (M_PI * ((float)prefs.phase / 180.0)),
					prefs.type, prefs.reflective);
	input->Copy(temp);

    gimp_free_image(temp);
    delete input;
	
	return B_OK;
}

#if 0
/* Local functions */

static void toggle_callback (int, void *, void *);
static void scale_callback(int item_ID, void *client_data, void *call_data);
static void ok_callback(int item_ID, void *client_data, void *call_data);
static void cancel_callback(int item_ID, void *client_data, void *call_data);
static void radio_callback (int, void *, void *);


/* Local vars */

static char   *prog_name;
static int     dialog_id;

/*
 * Set up the dialog box and get the images.
 */

int
main(int argc, char **argv)
{
	Image	input, output;
	Data	*data;
	int	row_id;
	int	amplitude_id, wavelength_id, phase_id;
	int	black_id, smear_id, reflective_id;

	/* Save program name */

	prog_name = argv[0];

	/* Initialize filter and continue if success */

	if (!gimp_init(argc, argv))
		return 0;

	/* Get default input and output images */

	input  = gimp_get_input_image(0);
	output = gimp_get_output_image(0);

	/* If both images were available, continue */

	if (input && output) {
		if ((gimp_image_type(input) == RGB_IMAGE) ||
		    (gimp_image_type(input) == GRAY_IMAGE)) {
			data = gimp_get_params();

			if (!data){
				data = malloc(sizeof(Data));

				/*
				 * Arbitrary default values.
				 */

				data->amplitude = 50;	/* 5. */
				data->wavelength = 20;
				data->phase = 0;
				data->black = 0;
				data->smear = 1;
				data->reflective = 0;
			}

			dialog_id = gimp_new_dialog("Wave");

			row_id = gimp_new_row_group(dialog_id, DEFAULT, NORMAL, "");
			amplitude_id = gimp_new_scale(dialog_id, row_id, 1, 800,
							data->amplitude, 1);
			gimp_new_label(dialog_id, amplitude_id, "Amplitude:");
			gimp_add_callback(dialog_id, amplitude_id, scale_callback,
					  &data->amplitude);

			wavelength_id = gimp_new_scale(dialog_id, row_id, 1, 200,
							data->wavelength, 0);
			gimp_new_label(dialog_id, wavelength_id, "Wavelength:");
			gimp_add_callback(dialog_id, wavelength_id, scale_callback,
					  &data->wavelength);

			phase_id = gimp_new_scale(dialog_id, row_id, 0, 360,
							data->phase, 0);
			gimp_new_label(dialog_id, phase_id, "Phase:");
			gimp_add_callback(dialog_id, phase_id, scale_callback,
					  &data->phase);

			gimp_add_callback(dialog_id, gimp_ok_item_id(dialog_id),
					  ok_callback, NULL);
			gimp_add_callback(dialog_id, gimp_cancel_item_id(dialog_id),
					  cancel_callback, NULL);
			/* What happens at the edge of the image. */

			row_id = gimp_new_row_group (dialog_id, row_id, RADIO, "");

			/* Fill with black. */

			black_id = gimp_new_radio_button (dialog_id, row_id,
								 "Black");
			gimp_change_item (dialog_id, black_id, sizeof (data->black),
								 &data->black);
			gimp_add_callback (dialog_id, black_id, radio_callback,
								 &data->black);

			/* Smear the edges out. */

			smear_id = gimp_new_radio_button (dialog_id, row_id,
								 "Smeared");
			gimp_change_item (dialog_id, smear_id, sizeof (data->smear),
								 &data->smear);
			gimp_add_callback (dialog_id, smear_id, radio_callback,
								 &data->smear);

			/* Reflective waves or just distorting. */

			row_id = gimp_new_row_group (dialog_id, row_id, NORMAL, "");

			reflective_id = gimp_new_check_button(dialog_id, row_id, "Reflective?");
			gimp_change_item (dialog_id, reflective_id, sizeof (data->reflective),
								 &data->reflective);
			gimp_add_callback(dialog_id, reflective_id, toggle_callback,
					  &data->reflective);

			if (gimp_show_dialog(dialog_id)) {
				gimp_set_params(sizeof(Data), data);

				gimp_init_progress("Wave");
				wave(input, output, ((float)data->amplitude / 10.0), 
					data->wavelength, (M_PI * ((float)data->phase / 180.0)),
					data->smear, data->reflective);

				gimp_update_image(output);
			} /* if */
		} else
			gimp_message("Wave: can only operate on 24-bit or grayscale images");

		gimp_free_image(input);
		gimp_free_image(output);

		gimp_quit();
	} /* if */

	return 0;
} /* main */
    
/*
 * Callbacks.
 */

static void
scale_callback(int item_id, void *client_data, void *call_data)
{
	*((int32 *) client_data) = *((int32 *) call_data);
} /* text_callback */

static void
ok_callback(int item_id, void *client_data, void *call_data)
{
	gimp_close_dialog(dialog_id, 1);
} /* ok_callback */

static void
cancel_callback(int item_id, void *client_data, void *call_data)
{
	gimp_close_dialog(dialog_id, 0);
} /* cancel_callback */

static void
radio_callback (int item_ID, void *client_data, void *call_data)
{
	*((int32*) client_data) = *((int32*) call_data);
}

static void
toggle_callback (int item_ID, void *client_data, void *call_data)
{
	*((int32*) client_data) = *((int32*) call_data);
}
#endif

/*
 * Wave the image.
 */

static void
wave(Image input, Image output, float amplitude, int32 wavelength, float phase, int32 smear, int32 reflective)
{
	int32   width, height;
	int32   channels, rowsiz;
	uchar *src, *p;
	uchar *destrow;
	uchar *dest;
	int    x1, y1, x2, y2;
	int    x, y;
	int    progress, max_progress;
	int     x1_in, y1_in, x2_in, y2_in;

	double cen_x, cen_y;       /* Center of wave */
	double xhsiz, yhsiz;       /* Half size of selection */
	double radius, radius2;    /* Radius and radius^2 */
	double amnt, d;
	double needx, needy;
	double dx, dy;
	double xscale, yscale;

	int xi, yi;

	uchar  values[4];
	uchar  val;

	int k;
    
	/* Get selection area */

	gimp_image_area(input, &x1, &y1, &x2, &y2);

	width    = gimp_image_width(input);
	height   = gimp_image_height(input);
	channels = gimp_image_channels(input);
	rowsiz   = width * channels;

	progress     = 0;
	max_progress = y2 - y1;

	src     = gimp_image_data(input);
	destrow = gimp_image_data(output);

	/* Wave the image.  For each pixel in the destination image
 	 * which is inside the selection, we compute the corresponding
  	 * waved location in the source image.  We use bilinear
  	 * interpolation to avoid ugly jaggies.
	 *
	 * Let's assume that we are operating on a circular area.
	 * Every point within <radius> distance of the wave center is
	 * waveed to its destination position.
	 *
	 * Basically, introduce a wave into the image. I made up the
	 * forumla initially, but it looks good. Quartic added the 
	 * wavelength etc. to it while I was asleep :) Just goes to show
	 * we should work with people in different time zones more.
	 *
	 */

	/* Center of selection */

	cen_x = (double) (x2 - 1 + x1) / 2.0;
	cen_y = (double) (y2 - 1 + y1) / 2.0;

	/* Compute wave radii (semiaxes) */

	xhsiz = (double) (x2 - x1) / 2.0;
	yhsiz = (double) (y2 - y1) / 2.0;

	/* These are the necessary scaling factors to turn the wave
	   ellipse into a large circle */

	if (xhsiz < yhsiz) {
		xscale = yhsiz / xhsiz;
		yscale = 1.0;
	} else if (xhsiz > yhsiz) {
		xscale = 1.0;
		yscale = xhsiz / yhsiz;
	} else {
		xscale = 1.0;
		yscale = 1.0;
	} /* else */

	radius  = MAX(xhsiz, yhsiz);
	radius2 = radius * radius;

	/* Wave the image! */

	destrow += y1 * rowsiz + x1 * channels;
    
	wavelength = wavelength << 1;

	for (y = y1; y < y2; y++) {
		dest = destrow;

		for (x = x1; x < x2; x++) {
			/* Distance from current point to wave center, scaled */

			dx = (x - cen_x) * xscale;
			dy = (y - cen_y) * yscale;

			/* Distance^2 to center of *circle* (our scaled ellipse) */

			d = sqrt(dx * dx + dy * dy);
	    
			/* Use the formula described above. */

			/* Calculate waved point and scale again to ellipsify */

			/*
			 * Reflective waves are strange - the effect is much
			 * more like a mirror which is in the shape of
			 * the wave than anything else.
			 */

			if (reflective){
				amnt = amplitude * fabs(sin(((d / wavelength)
					* (2.0 * M_PI) + phase)));

				needx = (amnt * dx) / xscale + cen_x;
				needy = (amnt * dy) / yscale + cen_y;
			} else {
				amnt = amplitude * sin(((d / wavelength)
					* (2.0 * M_PI) + phase));

				needx = (amnt + dx) / xscale + cen_x;
				needy = (amnt + dy) / yscale + cen_y;
			}

			/* Calculations complete; now copy the proper pixel */

			xi = (int) needx;
			yi = (int) needy;

			if (smear){
				if (xi > width - 2){
					xi = width - 2;
				} else if (xi < 0){
					xi = 0;
				}
				if (yi > height - 2){
					yi = height - 2;
				} else if (yi < 0){
					yi = 0;
				}
			}

			p = src + rowsiz * yi + xi * channels;
		
			x1_in = WITHIN(0, xi, width - 1);
			y1_in = WITHIN(0, yi, height - 1);
			x2_in = WITHIN(0, xi + 1, width - 1);
			y2_in = WITHIN(0, yi + 1, height - 1);

			for (k = 0; k < channels; k++) {
					if (x1_in && y1_in)
						values[0] = *(p + k);
					else
						values[0] = 0;

					if (x2_in && y1_in)
						values[1] = *(p + channels + k);
					else
						values[1] = 0;

					if (x1_in && y2_in)
						values[2] = *(p + rowsiz + k);
					else 
						values[2] = 0;

					if (x2_in) {
					       if (y2_in)
						       values[3] = *(p + channels + k + rowsiz);
					       else
						       values[3] = 0;
					} else
					       values[3] = 0;

					val = bilinear(needx, needy, values);

					*dest++ = val;
				} /* for */
			} /* for */

		destrow += rowsiz;

		progress++;
		if (progress % 5 == 0)
			gimp_do_progress(progress, max_progress);
	} /* for */
} /* wave */

static uchar
bilinear(double x, double y, uchar *v)
{
	double m0, m1;

	x = fmod(x, 1.0);
	y = fmod(y, 1.0);

	if (x < 0)
		x += 1.0;
	if (y < 0)
		y += 1.0;

	m0 = (1.0 - x) * v[0] + x * v[1];
	m1 = (1.0 - x) * v[2] + x * v[3];

	return (uchar) ((1.0 - y) * m0 + y * m1);
} /* bilinear */
