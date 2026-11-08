/* Page Curl 0.5 --- image filter plug-in for The Gimp
 * Copyright (C) 1996 Federico Mena Quintero
 *
 * You can contact me at quartic@polloux.fciencias.unam.mx
 * You can contact the original The Gimp authors at gimp@xcf.berkeley.edu
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
 */

/* TODO:
 *
 * As of version 0.5 alpha, the only thing that is not yet implemented
 * is the "Warp curl" option.  Everything else seems to be working
 * just fine.  Please email me if you find any bugs.  I know that the
 * calculation code is horrible, but you don't want to tweak it anyway ;)
 */


#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "GIMPGlue.h"
#include "gimp.h"
#include "ImageManipAddon.h"
#include "BitmapAccessor.h"
#include "Config.h"

const char addonName[] = "Page Curl";
const char addonInfo[] = "Page curl effect, like curling the corner of a sheet of paper. Ported from the GIMP by Scott Lindsey <wombat@gobe.com>.  Original code by Federico Mena Quintero";
const char addonCategory[] = "Artistic";
int32 addonVersion = 100;


/***** Macros *****/

#define WITHIN(a, b, c) ((((a) <= (b)) && ((b) <= (c))) ? 1 : 0)


/***** Types *****/

typedef struct {
	double x, y;
} vector_t;



class PageCurl
{
public:
	PageCurl(Prefs*, Image, Image, int, color_space);
	
	void page_curl();

private:
	void paint_background();
	void shade_under();
	void shade_curl();

	void v_set(vector_t *v, double x, double y);
	void v_add(vector_t *v, vector_t a, vector_t b);
	void v_sub(vector_t *v, vector_t a, vector_t b);
	double v_mag(vector_t v);
	double v_dot(vector_t a, vector_t b);

	int left_of_diagl(double x, double y);
	int right_of_diagr(double x, double y);
	int below_diagb(double x, double y);
	int right_of_diagm(double x, double y);
	int inside_circle(double x, double y);

	void put_pixel(uchar *image, int x, int y, uchar *p);
	void get_pixel(uchar *image, int x, int y, uchar *p);
	void set_pixel_value(uchar* pixel, rgb_color value);
	void do_progress();

	bool do_curl_foreground;
	bool do_curl_background;
	
	double do_curl_opacity;
	
	bool do_shade_under;
	
	bool do_background_foreground;
	bool do_background_background;
	bool do_background_original;
	bool do_background_transparent;
	
	bool do_upper_left;
	bool do_upper_right;
	bool do_lower_left;
	bool do_lower_right;
	
	bool do_vertical;
	bool do_horizontal;
	
	rgb_color fore_color;
	rgb_color back_color;
	rgb_color transp_color;
	
	color_space depth;
	
	int    alpha_offset;

	/* Image parameters */

	Image  src_image, dest_image;
	uchar *src_data, *dest_data;
	
	int32   width, height;
	int32   channels;
	int32   src_stride, dst_stride;
	
	int    sel_x1, sel_y1, sel_x2, sel_y2;
	int    true_sel_width, true_sel_height;
	int    sel_width, sel_height;
	
	int    progress, max_progress;
	
	/* Center and radius of circle */
	
	vector_t center;
	double   radius;
	
	/* Useful points to keep around */
	
	vector_t left_tangent;
	vector_t right_tangent;
	
	
	/* Slopes --- these are *not* in the usual geometric sense! */
	
	double diagl_slope;
	double diagr_slope;
	double diagb_slope;
	double diagm_slope;
};




/***** Functions *****/

status_t
Manipulate(
	BitmapAccessor *sourceBitmap,
	BMessage *ioExtension,
	bool checkOnly)
{
	int alpha_offset = -1;
	switch(sourceBitmap->ColorSpace())
	{
		case B_RGBA32_BIG:
			alpha_offset = 0;
			break;
		case B_RGBA32_LITTLE:
			alpha_offset = 3;
			break;
		case B_RGB32_LITTLE:
		case B_RGB32_BIG:
		case B_RGB24_LITTLE:
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

	PrefsFromMessage(&prefs, ioExtension);

	gimp_init_glue(sourceBitmap, NULL, ioExtension);

	Image input = new GIMPImage(sourceBitmap);
    Image temp = gimp_temp_image();

	PageCurl it(&prefs, input, temp, alpha_offset, sourceBitmap->ColorSpace());
	
	it.page_curl();

	input->Copy(temp);
    gimp_free_image(temp);
    delete input;
	
	return B_OK;
}


/*****/

void PageCurl::do_progress()
{
	progress++;
	if (progress % 5 == 0)
		gimp_do_progress(progress, max_progress);
} /* do_progress */

		
/*****/

void PageCurl::v_set(vector_t *v, double x, double y)
{
	v->x = x;
	v->y = y;
} /* v_set */


/*****/

void PageCurl::v_add(vector_t *v, vector_t a, vector_t b)
{
	v->x = a.x + b.x;
	v->y = a.y + b.y;
} /* v_add */


/*****/

void PageCurl::v_sub(vector_t *v, vector_t a, vector_t b)
{
	v->x = a.x - b.x;
	v->y = a.y - b.y;
} /* v_sub */


/*****/

double PageCurl::v_mag(vector_t v)
{
	return sqrt(v.x * v.x + v.y * v.y);
} /* v_mag */


/*****/

double PageCurl::v_dot(vector_t a, vector_t b)
{
	return a.x * b.x + a.y * b.y;
} /* v_dot */


/*****/

PageCurl::PageCurl(Prefs* prefs, Image input, Image output, int alpha_off, color_space d)
{
	double   k;
	double   alpha, beta;
	double   angle;
	vector_t v1, v2;

	src_image = input;
	dest_image = output;

	do_curl_foreground = !prefs->bgcolor;
	do_curl_background = prefs->bgcolor;
	
	do_curl_opacity = prefs->opacity;
	
	do_shade_under = prefs->shadeunder;
	
	do_background_foreground	= prefs->background == 0;
	do_background_background	= prefs->background == 1;
	do_background_original		= prefs->background == 2;
	do_background_transparent	= prefs->background == 3;
	
	do_upper_left	= prefs->location == 0;
	do_upper_right	= prefs->location == 1;
	do_lower_left	= prefs->location == 2;
	do_lower_right	= prefs->location == 3;
	
	do_vertical = !prefs->horizontal;
	do_horizontal = prefs->horizontal;
	
	/* Image parameters */

	gimp_image_area(src_image, &sel_x1, &sel_y1, &sel_x2, &sel_y2);

	true_sel_width  = sel_x2 - sel_x1;
	true_sel_height = sel_y2 - sel_y1;

	if (do_vertical) {
		sel_width  = true_sel_width;
		sel_height = true_sel_height;
	} else {
		sel_width  = true_sel_height;
		sel_height = true_sel_width;
	} /* else */

	src_data  = gimp_image_data(src_image);
	dest_data = gimp_image_data(dest_image);

	width  	 = gimp_image_width(src_image);
	height 	 = gimp_image_height(src_image);
	channels = gimp_image_channels(src_image);

	src_stride = gimp_image_stride(src_image);
	dst_stride = gimp_image_stride(dest_image);

	progress     = 0;
	max_progress = sel_height * 3;
	
	/* Circle parameters */

	alpha = atan((double) sel_height / sel_width);
	beta  = alpha / 2.0;

	k = sel_width / ((M_PI + alpha) * sin(beta) + cos(beta));

	v_set(&center, k * cos(beta), k * sin(beta));

	radius = center.y;

	/* left_tangent  */

	v_set(&left_tangent, radius * -sin(alpha), radius * cos(alpha));
	v_add(&left_tangent, left_tangent, center);

	/* right_tangent */
	
	v_sub(&v1, left_tangent, center);
	v_set(&v2, sel_width - center.x, sel_height - center.y);

	angle = -2.0 * acos(v_dot(v1, v2) / (v_mag(v1) * v_mag(v2)));

	v_set(&right_tangent,
	      v1.x * cos(angle) + v1.y * -sin(angle),
	      v1.x * sin(angle) + v1.y * cos(angle));

	v_add(&right_tangent, right_tangent, center);

	/* Slopes */

	diagl_slope = (double) sel_width / sel_height;
	diagr_slope = (sel_width - right_tangent.x) / (sel_height - right_tangent.y);
	diagb_slope = (right_tangent.y - left_tangent.y) / (right_tangent.x - left_tangent.x);
	diagm_slope = (sel_width - center.x) / sel_height;

	/* Colors */

	gimp_foreground_color(&fore_color.red, &fore_color.green, &fore_color.blue);
	gimp_background_color(&back_color.red, &back_color.green, &back_color.blue);
	transp_color = back_color;
	transp_color.alpha = 0;
	fore_color.alpha = back_color.alpha = 255;
	
	alpha_offset = alpha_off;
	depth = d;

} /* init_calculation */


/*****/

int PageCurl::left_of_diagl(double x, double y)
{
	return (x < (sel_width + (y - sel_height) * diagl_slope));
} /* left_of_diagl */


/*****/

int PageCurl::right_of_diagr(double x, double y)
{
	return (x > (sel_width + (y - sel_height) * diagr_slope));
} /* right_of_diagr */


/*****/

int PageCurl::below_diagb(double x, double y)
{
	return (y < (right_tangent.y + (x - right_tangent.x) * diagb_slope));
} /* below_diagb */


/*****/

int PageCurl::right_of_diagm(double x, double y)
{
	return (x > (sel_width + (y - sel_height) * diagm_slope));
} /* right_of_diagm */


/*****/

int PageCurl::inside_circle(double x, double y)
{
	double dx, dy;

	dx = x - center.x;
	dy = y - center.y;
		
	return (sqrt(dx * dx + dy * dy) <= radius);
} /* inside_circle */


/*****/

void PageCurl::put_pixel(uchar *image, int x, int y, uchar *p)
{
	int    temp;
	int    k;
	uchar *destp;

	/* Flip horizontally if necessary */

	if (do_upper_left || do_lower_left)
		x = (sel_width - 1) - x;

	/* Flip vertically if necessary */

	if (do_upper_left || do_upper_right)
		y = (sel_height - 1) - y;

	/* Rotate if necessary */

	if (do_horizontal) {
		y    = (sel_height - 1) - y;
		temp = x;
		x    = y;
		y    = (true_sel_height - 1) - temp;

		if (do_upper_right || do_lower_left) {
			x = (true_sel_width - 1) - x;
			y = (true_sel_height - 1) - y;
		} /* if */
	} /* if */

	/* Transform to image coordinates */

	x = sel_x1 + x;
	y = (sel_y2 - 1) - y;

	if (WITHIN(0, x, width - 1) && WITHIN(0, y, height - 1)) {
		destp = image + dst_stride * y + channels * x;
		
		for (k = channels; k; k--)
			*destp++ = *p++;
	} /* if */
} /* put_pixel */


/*****/

void PageCurl::get_pixel(uchar *image, int x, int y, uchar *p)
{
	int    temp;
	int    k;
	uchar *srcp;

	/* Flip horizontally if necessary */

	if (do_upper_left || do_lower_left)
		x = (sel_width - 1) - x;

	/* Flip vertically if necessary */

	if (do_upper_left || do_upper_right)
		y = (sel_height - 1) - y;

	/* Rotate if necessary */

	if (do_horizontal) {
		y    = (sel_height - 1) - y;
		temp = x;
		x    = y;
		y    = (true_sel_height - 1) - temp;

		if (do_upper_right || do_lower_left) {
			x = (true_sel_width - 1) - x;
			y = (true_sel_height - 1) - y;
		} /* if */
	} /* if */

	/* Transform to image coordinates */

	x = sel_x1 + x;
	y = (sel_y2 - 1) - y;

	if (WITHIN(0, x, width - 1) && WITHIN(0, y, height - 1)) {
		srcp = image + src_stride * y + channels * x;
		
		for (k = channels; k; k--)
			*p++ = *srcp++;
	} else
		p[0] = p[1] = p[2] = 0;
} /* get_pixel */


void PageCurl::set_pixel_value(uchar* p, rgb_color value)
{
	int i = 0;
	switch(depth)
	{
		case B_RGBA32_BIG:
			p[0] = value.alpha;
		case B_RGB32_BIG:
			i++;
		case B_RGB24_BIG:
			p[i] = value.red;
			p[i + 1] = value.green;
			p[i + 2] = value.blue;
			break;
		case B_RGBA32_LITTLE:
			p[3] = value.alpha;
		case B_RGB32_LITTLE:
		case B_RGB24_LITTLE:
			p[0] = value.blue;
			p[1] = value.green;
			p[2] = value.red;
			break;
		case B_GRAYSCALE_8_BIT:
			p[0] = value.red;
			break;
	}
}

/*****/

void PageCurl::paint_background()
{
	int    x, y;
	uchar  p[3];
	int    copy;

	for (y = sel_height - 1; y >= 0; y--) {
		for (x = 0; x < sel_width; x++) {
			copy = do_background_original
			       || !(right_of_diagr(x, y)
				    || (right_of_diagm(x, y) && below_diagb(x, y)
					&& !inside_circle(x, y)));

			if (copy) {
				get_pixel(src_data, x, y, p);
				put_pixel(dest_data, x, y, p);
			} else {
				// BOGUS - byte ordering!
				if (do_background_foreground) {
					set_pixel_value(p, fore_color);
				} else if (do_background_background) {
					set_pixel_value(p, back_color);
				} else if (do_background_transparent) {
					set_pixel_value(p, transp_color);
				} else
					get_pixel(src_data, x, y, p);
				
				put_pixel(dest_data, x, y, p);
			} /* else */
		} /* for */
		
		do_progress();
	} /* for */
} /* paint_background */


/*****/

void PageCurl::shade_under()
{
	int      x, y;
	double   alpha;
	vector_t v, dl, dr;
	double   dl_mag, dr_mag;
	double   angle, factor;
	uchar    p[3];
	uchar   *pp;
	int      k;

	if (!do_shade_under) {
		progress += sel_height;
		gimp_do_progress(progress, max_progress);
		return;
	} /* if */

	v_set(&dl, -sel_width, -sel_height);
	dl_mag = v_mag(dl);

	v_set(&dr, -(sel_width - right_tangent.x), -(sel_height - right_tangent.y));
	dr_mag = v_mag(dr);

	alpha = acos(v_dot(dl, dr) / (dl_mag * dr_mag));

	for (y = sel_height - 1; y >= 0; y--) {
		for (x = 0; x < sel_width; x++)
			if (!left_of_diagl(x, y) && !right_of_diagr(x, y) && 
			    !(right_of_diagm(x, y) && below_diagb(x, y) && !inside_circle(x, y))) {
				v.x = -(sel_width - x);
				v.y = -(sel_height - y);
				
				angle  = acos(v_dot(v, dl) / (v_mag(v) * dl_mag));
				factor = 1.0 - angle / alpha;
				
				get_pixel(src_data, x, y, p);
				
				pp = p;
				
				for (k = channels; k; k--)
				{
					if ((channels - k) == alpha_offset)
						pp++;
					else
						*pp++ = (uchar) (*pp * factor);
				}
				
				put_pixel(dest_data, x, y, p);
			} /* if */
		
		do_progress();
	} /* for */
} /* shade_under */


/*****/

void PageCurl::shade_curl()
{
	int      x, y;
	uchar    p[3], c[3];
	vector_t v, dl, dr;
	double   dl_mag, dr_mag;
	double   alpha;
	double   angle;
	double   intensity;

	v_set(&dl, -sel_width, -sel_height);
	dl_mag = v_mag(dl);

	v_set(&dr, -(sel_width - right_tangent.x), -(sel_height - right_tangent.y));
	dr_mag = v_mag(dr);

	alpha = acos(v_dot(dl, dr) / (dl_mag * dr_mag));

	if (do_curl_foreground) {
		set_pixel_value(c, fore_color);
	} else {
		set_pixel_value(c, back_color);
	} /* else */

	for (y = sel_height - 1; y >= 0; y--) {
		for (x = 0; x < sel_width; x++)
			if (!left_of_diagl(x, y) && !right_of_diagr(x, y)
			    && !inside_circle(x, y) && !below_diagb(x, y)) {
				v.x = -(sel_width - x);
				v.y = -(sel_height - y);

				angle = acos(v_dot(v, dl) / (v_mag(v) * dl_mag));

				intensity = pow(sin(M_PI * angle / alpha), 1.5);
				
				get_pixel(dest_data, x, y, p);

				p[0] = (uchar) (do_curl_opacity * (intensity * c[0]) + (1.0 - do_curl_opacity) * p[0]);
				p[1] = (uchar) (do_curl_opacity * (intensity * c[1]) + (1.0 - do_curl_opacity) * p[1]);
				p[2] = (uchar) (do_curl_opacity * (intensity * c[2]) + (1.0 - do_curl_opacity) * p[2]);
				
				put_pixel(dest_data, x, y, p);
			} /* if */
		
		do_progress();
	} /* for */
} /* shade_curl */


/*****/

void PageCurl::page_curl()
{
//	gimp_init_progress("Page Curl");

	paint_background();
	shade_under();
	shade_curl();
} /* page_curl */
