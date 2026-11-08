/*
 * This is a plug-in for the GIMP.
 *
 * Copyright (C) 1996 Torsten Martinsen <bullestock@dk-online.dk>
 * Bresenham algorithm stuff hacked from HP2xx written by Heinz W. Werntges
 * Changes for version 1.11/1.12 Copyright (C) 1996 Federico Mena Quintero
 * quartic@polloux.fciencias.unam.mx
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * $Id: mblur.c,v 1.3 1999/09/13 21:20:58 wombat Exp $
 *
 * @(GIMP)		   = <plug-in mblur "Blur/Motion Blur">
 * @(GIMP_DEP)	   = <mblur.c>
 * @(GIMP_OBJ)	   = <mblur.o>
 * @(GIMP_LIB)	   = <m c>
 * @(GIMP_AUTHOR)  = <Torsten Martinsen>
 * @(GIMP_EMAIL)   = <bullestock@dk-online.dk>
 * @(GIMP_DESC)	   = <Do motion blur on a picture.>
 * @(GIMP_VERSION) = <Revision: 1.22>
 * @(GIMP_URL)	   = <http://www2.dk-online.dk/Users/Torsten_Martinsen/gimp/index.html>
 */

#include "gimp.h"
#include <stdlib.h>
#include <math.h>
#define DEBUG 1
#include <Debug.h>

/*
 * This filter simulates the effect seen when photographing a
 * moving object at a slow shutter speed.
 * Done by adding multiple displaced copies.
 */

/* Changes in version 1.11, by Quartic
 *
 * Corrected an ugly bug with the dialog box callbacks and
 * parameters... now we use a struct with all the parameters instead
 * of stuffing them in an array (the array wasn't working properly,
 * anyway) :)
 *
 * Added a frame around the radio buttons in the dialog box --- it
 * looks much better now IMO.  Changed some labels.
 * TODO:
 *
 * Add bilinear interpolation for Zoom Blur so that blurs will look *much*
 * better.
 */

/* define this if you want */
#define BILINEAR

#ifndef MIN
#define MIN(a,b)		(((a) < (b)) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a,b)		(((a) > (b)) ? (a) : (b))
#endif

#define NTYPES 3

typedef struct {
	long type[NTYPES];
	long length;
	long angle;
} mblur_params_t;

static void mblur(Image, Image);
static void mblur_linear(Image, Image);
static void mblur_radial(Image, Image);
static void mblur_radial_rgba(Image input, Image output, int chan);
static void mblur_radial_grey(Image input, Image output, int chan);
static void mblur_zoom(Image, Image);
static void do_hide(mblur_params_t * params);
static int bilinear(double x, double y, unsigned char *v1, unsigned char *v2,
					unsigned char *v3, unsigned char *v4);

static mblur_params_t params = { {1, 0, 0}, 5, 45 };
static int dialog_ID;
static int scalea_ID, scaleal_ID, scaled_ID, scaledl_ID;

static void scale_callback(int, void *, void *);
static void ok_callback(int, void *, void *);
static void cancel_callback(int, void *, void *);
static void radio_callback(int item_ID, void *client_data, void *call_data);

typedef void (*BlurFunc) (Image, Image);

int
main(int argc, char **argv)
{
	Image input = 0, output = 0;
	void *data;
	int temp_ID, frame_ID;
	int group_ID;
	int i, ID;
	static char * rb_label[NTYPES] = {
		"Linear", "Radial", "Zoom"
	};


	if (gimp_init(argc, argv)) {
		input = gimp_get_input_image(0);

		if (input)
			switch (gimp_image_type(input)) {
			case RGB_IMAGE:
			case GRAY_IMAGE:
			case RGBA_IMAGE:
			case GRAYA_IMAGE:
				data = gimp_get_params();
				if (data)
					params = *((mblur_params_t *) data);

				dialog_ID = gimp_new_dialog("Motion Blur");
				gimp_new_label (dialog_ID, DEFAULT, "Options");
				group_ID = gimp_new_row_group (dialog_ID, DEFAULT, NORMAL, "");

				temp_ID = gimp_new_row_group (dialog_ID, group_ID, NORMAL, "");
				scaledl_ID = gimp_new_label (dialog_ID, temp_ID, "Length:");
				scaled_ID = gimp_new_scale(dialog_ID, temp_ID, 1, 50,
										   params.length, 0);

				frame_ID = gimp_new_frame(dialog_ID, group_ID, "Blur type");
				temp_ID = gimp_new_row_group(dialog_ID, frame_ID, RADIO, "");

				for (i = 0; i < NTYPES; ++i) {
					ID = gimp_new_radio_button (dialog_ID, temp_ID,
												rb_label[i]);
					gimp_add_callback (dialog_ID, ID,
									   radio_callback, &params.type[i]);
					gimp_change_item (dialog_ID, ID, sizeof (long),
									  &params.type[i]);
				}

				temp_ID = gimp_new_row_group (dialog_ID, group_ID, NORMAL, "");
				scaleal_ID = gimp_new_label(dialog_ID, temp_ID, "Angle:");
				scalea_ID = gimp_new_scale(dialog_ID, temp_ID, 0, 360,
										   params.angle, 0);

				do_hide(&params);
				
				gimp_add_callback(dialog_ID, scaled_ID,
								  scale_callback, &params.length);
				gimp_add_callback(dialog_ID, scalea_ID,
								  scale_callback, &params.angle);
				gimp_add_callback(dialog_ID, gimp_ok_item_id(dialog_ID),
								  ok_callback, 0);
				gimp_add_callback(dialog_ID, gimp_cancel_item_id(dialog_ID),
								  cancel_callback, 0);

				if (gimp_show_dialog(dialog_ID)) {
					gimp_set_params(sizeof(mblur_params_t), &params);

					output = gimp_get_output_image(0);
					if (output) {
						gimp_init_progress("Motion Blur");
						mblur(input, output);
						gimp_update_image(output);
					}
				}
				break;
			case INDEXED_IMAGE:
				gimp_message("mblur: cannot operate on indexed color images");
				break;
			default:
				gimp_message("mblur: cannot operate on unknown type images");
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
 * Dispatch function - calls the real blur function.
 */
static void
mblur(Image input, Image output)
{
	int k;
		
	static BlurFunc mblur_funcs[NTYPES] = {
		mblur_linear, mblur_radial, mblur_zoom
	};

	for (k = 0; k < NTYPES; k++)
		if (params.type[k]) {
			mblur_funcs[k](input, output);
			break;
		}
}

/*
 * Do a 'linear blur' - the camera is translated during exposure.
 * Each pixel at (x,y) is replaced by the average of the pixels located on a
 * straight line with start at (x,y), pointing in the specified direction
 * and having the specified length specified angle.
 */
static void
mblur_linear(Image input, Image output)
{
	long width, height;
	long channels, rowstride;
	unsigned char *src_base;
	unsigned char *dest;
	int x, y, i, xx, yy, n, sum;
	int dx, dy, px, py, swapdir, err, e, s1, s2;

	width = gimp_image_width(input);
	height = gimp_image_height(input);
	channels = gimp_image_channels(input);
	rowstride = width * channels;

	src_base = gimp_image_data(input);

	n = params.length;
	px = n*cos(params.angle/180.0*M_PI);
	py = n*sin(params.angle/180.0*M_PI);

	/*
	 * Initialization for Bresenham algorithm:
	 * dx = abs(x2-x1), s1 = sign(x2-x1)
	 * dy = abs(y2-y1), s2 = sign(y2-y1)
	 */
	if ((dx = px) != 0) {
	  if (dx < 0) {
		  dx = -dx;
		  s1 = -1;
	  }
	  else
		  s1 = 1;
	} else
		s1 = 0;
	
	if ((dy = py) != 0) {
		if (dy < 0) {
			dy = -dy;
			s2 = -1;
		}
		else
			s2 = 1;
	} else
		s2 = 0;

	if (dy > dx) {
		swapdir = dx;
		dx = dy;
		dy = swapdir;
		swapdir = 1;
	}
	else
		swapdir = 0;

	dy *= 2;
	err = dy - dx;		/* Initial error term	*/
	dx *= 2;

	s1 *= channels;
	dest = gimp_image_data(output);
	for (y = 0; y < height; ++y) {
		for (x = 0; x < rowstride; ++x) {
			xx = x; yy = y; e = err;
			for (i = 0, sum = 0; i < n; ++i) {
				while (e >= 0) {
					if (swapdir)
						xx += s1;
					else
						yy += s2;
					e -= dx;
				}
				if (swapdir)
					yy += s2;
				else
					xx += s1;
				e += dy;
				if ((yy < 0) || (yy >= height) ||
					(xx < 0) || (xx >= rowstride))
					break;
				sum += src_base[yy*rowstride + xx];
			}
			if (i == 0)
				*dest++ = src_base[y*rowstride + x];
			else
				*dest++ = sum/i;
		}
		if ((y % 5) == 0)
			gimp_do_progress(y, height);
	}
}

/*
 * Do a 'radial blur' - the camera is rotated during exposure.
 * Each pixel at (x,y) is replaced by the average of the pixels located on an
 * arc going through (x,y), center at (width/2,height/2) and extending half
 * the specified angle on each side of (x,y).
 * You probably did not understand that. Don't worry.
 * To speed up computing the rotated coordinates, a table of sine and cosine
 * values is built before entering the loop. This is 8-10 times faster.
 * Also, separate functions are used for 3 and 1 bit planes, respectively.
 * This doubles the speed.
 * The number of samples to use is determined automatically.
 */
static void
mblur_radial(Image input, Image output)
{
	int c = gimp_image_channels(input);
	
	if (c == 4)
		mblur_radial_rgba(input, output, c);
	else if (c == 1)
		mblur_radial_grey(input, output, c);
}

static void
mblur_radial_rgba(Image input, Image output, int chan)
{
	long width, height;
	long rowstride;
	unsigned char *src_base, *src;
	unsigned char *dest, *dest_row;
	int x, y, i, j, n, sum, sumG, sumB, sumA, cx, cy, xr, yr;
	int x1, y1, x2, y2, count, R, r, w, h, step;
	float angle, theta, * ct, * st, offset, xx, yy;

	gimp_image_area(input, &x1, &y1, &x2, &y2);
	width = gimp_image_width(input);
	height = gimp_image_height(input);
	rowstride = width * chan;
	
	src_base = gimp_image_data(input);
	angle = ((float) params.angle)/180.0*M_PI;
	cx = (x1+x2)/2;
	cy = (y1+y2)/2;
	w = MAX(width-cx, cx);
	h = MAX(height-cy, cy);
	R = sqrt(w*w + h*h);
	n = 4*angle*sqrt(R)+2;
	theta = angle/((float) (n-1));

	if (((ct = malloc(n*sizeof(float))) == NULL) ||
		((st = malloc(n*sizeof(float))) == NULL))
		return;
	offset = theta*(n-1)/2;
	for (i = 0; i < n; ++i) {
		ct[i] = cos(theta*i-offset);
		st[i] = sin(theta*i-offset);
	}

	dest = gimp_image_data(output);
	for (y = 0; y < height; ++y) {
		for (x = 0; x < width; ++x) {
			xr = x-cx;
			yr = y-cy;
			r = sqrt(xr*xr + yr*yr);
			if (r == 0)
				step = 1;
			else if ((step = R/r) == 0)
				step = 1;
			else if (step > n-1)
				step = n-1;
			sum = sumG = sumB = sumA = 0;
			for (i = 0, count = 0; i < n; i += step) {
				xx = cx + xr*ct[i] - yr*st[i];
				yy = cy + xr*st[i] + yr*ct[i];
				if ((yy < 0) || (yy >= height) ||
					(xx < 0) || (xx >= width))
					continue;
#ifdef BILINEAR
				src = &src_base[((int) yy)*rowstride + ((int) xx)*chan];
				sum += bilinear(xx, yy, src, src+chan,
								src+rowstride, src+rowstride+chan);
				++src;
				sumG += bilinear(xx, yy, src, src+chan,
								 src+rowstride, src+rowstride+chan);
				++src;
				sumB += bilinear(xx, yy, src, src+chan,
								 src+rowstride, src+rowstride+chan);
				++src;
				sumA += bilinear(xx, yy, src, src+chan,
								 src+rowstride, src+rowstride+chan);
#else
				sum += src_base[((int) yy)*rowstride + ((int) xx)*chan];
				sumG += src_base[((int) yy)*rowstride + ((int) xx)*chan + 1];
				sumB += src_base[((int) yy)*rowstride + ((int) xx)*chan + 2];
				sumA += src_base[((int) yy)*rowstride + ((int) xx)*chan + 3];
#endif
				++count;
			}
			if (count == 0)
			{
				for (j = 0; j < chan; ++j)
					*dest++ = src_base[y*rowstride + x*chan + j];
			}
			else
			{
				*dest++ = sum/count;
				*dest++ = sumG/count;
				*dest++ = sumB/count;
				*dest++ = sumA/count;
			}
		}
		if ((y % 5) == 0)
			gimp_do_progress(y, height);
	}
}

static void
mblur_radial_grey(Image input, Image output, int chan)
{
	long width, height;
	long rowstride;
	unsigned char *src_base, *src;
	unsigned char *dest, *dest_row;
	int x, y, i, j, n, sum, sumG, sumB, cx, cy, xr, yr;
	int x1, y1, x2, y2, count, R, r, w, h, step;
	float angle, theta, * ct, * st, offset, xx, yy;

	gimp_image_area(input, &x1, &y1, &x2, &y2);
	width = gimp_image_width(input);
	height = gimp_image_height(input);
	rowstride = width*chan;

	src_base = gimp_image_data(input);

	angle = ((float) params.angle)/180.0*M_PI;
	cx = (x1+x2)/2;
	cy = (y1+y2)/2;
	w = MAX(width-cx, cx);
	h = MAX(height-cy, cy);
	R = sqrt(w*w + h*h);
	n = 4*angle*sqrt(R)+2;
	theta = angle/((float) (n-1));

	if (((ct = malloc(n*sizeof(float))) == NULL) ||
		((st = malloc(n*sizeof(float))) == NULL))
		return;
	offset = theta*(n-1)/2;
	for (i = 0; i < n; ++i) {
		ct[i] = cos(theta*i-offset);
		st[i] = sin(theta*i-offset);
	}

	dest = gimp_image_data(output);
	for (y = 0; y < height; ++y) {
		for (x = 0; x < width; ++x) {
			xr = x-cx;
			yr = y-cy;
			r = sqrt(xr*xr + yr*yr);
			if (r == 0)
				step = 1;
			else if ((step = R/r) == 0)
				step = 1;
			else if (step > n-1)
				step = n-1;
			sum = sumG = sumB = 0;
			for (i = 0, count = 0; i < n; i += step) {
				xx = cx + xr*ct[i] - yr*st[i];
				yy = cy + xr*st[i] + yr*ct[i];
				if ((yy < 0) || (yy >= height) ||
					(xx < 0) || (xx >= width))
					continue;
#ifdef BILINEAR
				src = &src_base[((int) yy)*rowstride + ((int) xx) * chan];
				sum += bilinear(xx, yy, src, src+chan,
								src+rowstride, src+rowstride);
#else
				sum += src_base[((int) yy)*rowstride + ((int) xx) * chan];
#endif
				++count;
			}
			if (count == 0)
				*dest = src_base[y*rowstride + x];
			else
				*dest = sum/count;
			dest += chan;
			}
		if ((y % 5) == 0)
			gimp_do_progress(y, height);
	}

	/* Copy alpha channel */
	if (chan == 1)
		return;
	
	src_base = gimp_image_data(input) + rowstride * y1 + x1 * chan + 1;
	dest_row = gimp_image_data(output) + rowstride * y1 + x1 * chan + 1;
	for (y = y1; y < y2; ++y) {
		src = src_base;
		dest = dest_row;
		for (x = x1; x < x2; ++x) {
			*dest = *src;
			dest += 2;
			src += 2;
		}
		dest_row += rowstride;
		src_base += rowstride;
	}
}


/*
 * Do a 'zoom blur' - the object is zoomed during exposure.
 * Each pixel at (x,y) is replaced by the average of the pixels located on a
 * straight line with start at (x,y), pointing towards (width/2,height/2) and
 * length dependent on the distance from the center. (The angle is ignored).
 */
static void
mblur_zoom(Image input, Image output)
{
	long width, height;
	long channels, rowstride;
	unsigned char *src_base, *src;
	unsigned char *dest, *dest_row;
	int x, y, i, j, xx, yy, n, sum, sumG, sumB, sumA, cx, cy;
	int x1, y1, x2, y2;
	float f;
	
	gimp_image_area(input, &x1, &y1, &x2, &y2);
	width = gimp_image_width(input);
	height = gimp_image_height(input);
	channels = gimp_image_channels(input);
	rowstride = width * channels;

	src_base = gimp_image_data(input);

	n = params.length;

	dest = gimp_image_data(output);
	cx = (x1+x2)/2;
	cy = (y1+y2)/2;
	f = 0.02;
	for (y = 0; y < height; ++y) {
		for (x = 0; x < width; ++x) {
			sum = sumG = sumB = sumA = 0;
			for (i = 0; i < n; ++i) {
				xx = cx + (x-cx)*(1.0 + f*i);
				yy = cy + (y-cy)*(1.0 + f*i);
				if ((yy < 0) || (yy >= height) ||
					(xx < 0) || (xx >= width))
					break;
				xx *= channels;
				sum += src_base[yy*rowstride + xx];
				if (channels > 1) {
					sumG += src_base[yy*rowstride + xx + 1];
					sumB += src_base[yy*rowstride + xx + 2];
					sumA += src_base[yy*rowstride + xx + 3];
				}
			}
			if (i == 0)
				for (j = 0; j < channels; ++j)
					*dest++ = src_base[y*rowstride + x + j];
			else {
				*dest++ = sum/i;
				switch (channels) {
				case 1:			/* GRAY */
					break;
				case 2:			/* GRAYA */
					++dest;
					break;
				case 3:			/* RGB */
					*dest++ = sumG/i;
					*dest++ = sumB/i;
					break;
				case 4:			/* RGBA */
					*dest++ = sumG/i;
					*dest++ = sumB/i;
					*dest++ = sumA/i;
					break;
				}
			}
		}
		if ((y % 5) == 0)
			gimp_do_progress(y, height);
	}
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
radio_callback(int item_ID, void *client_data, void *call_data)
{
	*((long *) client_data) = *((long *) call_data);
	do_hide(&params);
}

#ifdef BILINEAR
static int
bilinear(double x, double y, unsigned char *v1, unsigned char *v2,
		 unsigned char *v3, unsigned char *v4)
{
	double m0, m1;
	
	x = fmod(x, 1.0);
	y = fmod(y, 1.0);

	m0 = (1.0 - x) * *v1 + x * *v2;
	m1 = (1.0 - x) * *v3 + x * *v4;
	return (1.0 - y) * m0 + y * m1;
}
#endif

static void
do_hide(mblur_params_t * params)
{
#if 0
	if (params->type[1]) {
		gimp_hide_item(dialog_ID, scaled_ID);
		gimp_hide_item(dialog_ID, scaledl_ID);
	}
	else {
		gimp_show_item(dialog_ID, scaled_ID);
		gimp_show_item(dialog_ID, scaledl_ID);
	}
	if (params->type[2]) {
		gimp_hide_item(dialog_ID, scalea_ID);
		gimp_hide_item(dialog_ID, scaleal_ID);
	} else {
		gimp_show_item(dialog_ID, scalea_ID);
		gimp_show_item(dialog_ID, scaleal_ID);
	}
#endif
}
