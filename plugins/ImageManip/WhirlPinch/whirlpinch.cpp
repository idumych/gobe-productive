/* The GIMP -- an image manipulation program
 * Copyright (C) 1995 Spencer Kimball and Peter Mattis
 *
 * Whirl and Pinch plug-in --- two common distortions in one place
 * Copyright (C) 1997 Federico Mena Quintero
 * federico@nuclecu.unam.mx
 * Copyright (C) 1997 Scott Goehring
 * scott@poverty.bloomington.in.us
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
 */


/* Version 2.09:
 *
 * - Another cool patch from Scott.  The radius is now in [0.0, 2.0],
 * with 1.0 being the default as usual.  In addition to a minimal
 * speed-up (one multiplication is eliminated in the calculation
 * code), it is easier to think of nice round values instead of
 * sqrt(2) :-)
 *
 * - Modified the way out-of-range pixels are handled.  This time the
 * plug-in handles `outside' pixels better; it paints them with the
 * current background color (for images without transparency) or with
 * a completely transparent background color (for images with
 * transparency).
 */


/* Version 2.08:
 *
 * This is the first version of this plug-in.  It is called 2.08
 * because it came out of merging the old Whirl 2.08 and Pinch 2.08
 * plug-ins.  */


#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <algobase.h>
#define DEBUG 1
#include <Debug.h>

#include "ImageManipAddon.h"
#include "BitmapAccessor.h"
#include "Config.h"
#include "GIMPGlue.h"

const char addonName[] = "Whirl / Pinch";
const char addonInfo[] = "Distorts an image by whirling and pinching.\nOriginal GIMP plugin by Federico Mena Quintero and Scott Goehring.\nPorted by Scott Lindsey.";
const char addonCategory[] = "Distortion";
int32 addonVersion = 100;

typedef struct bits
{
	uchar* bits;
	int32 stride, channels;
	uchar* bg;
	int32 sel[4];
} bits;

class WhirlPinch
{
public:
	WhirlPinch(BitmapAccessor* sourceBitmap, BRect sel_rect, int32 channels, uchar* bg, BInvoker* prog, const Prefs* prefs);
	status_t Render();
	status_t Traverse(int32 which);

	int    calc_undistorted_coords(double wx, double wy,
				      double whirl, double pinch,
				      double *x, double *y);
	uchar bilinear(double x, double y, uchar *values);
	void get_pixel(int x, int y, uchar *pixel);
	
	static int32 thread_runner(void*);
	
	double cen_x, cen_y;
	double scale_x, scale_y;
	double radius, radius2;
	double whirl;

	BitmapAccessor* sourceBitmap;
	BRect sel_rect;
	int32 channels;
	uchar* bg;
	BInvoker* prog;
	const Prefs* prefs;
	
	int32 x1, x2, y1, y2;
	int32 extent_left, extent_top, extent_right, extent_bottom;
	int32 sel_width;
	int32 sel_height;
	uchar* bits;
	uchar* dst;
	int32 stride;
	
	system_info info;
	thread_id threads[B_MAX_CPU_COUNT];
	bool cancelled;
};


status_t
Manipulate(
	BitmapAccessor *sourceBitmap,
	BMessage *ioExtension,
	bool checkOnly)
{
	Prefs prefs;
	int32 channels;
	uchar bg[4];

	rgb_color rgb;
	ssize_t size;
	if (ioExtension && ioExtension->FindData("background_color", B_RGB_COLOR_TYPE, (const void**) &rgb, &size))
	{
		rgb.blue = rgb.green = rgb.red = 255;
		rgb.alpha = 0;
	}

	switch(sourceBitmap->ColorSpace())
	{
		case B_GRAY8:
			bg[0] = rgb.red;
			channels = 1;
			break;
		case B_RGB24_BIG:
			bg[0] = rgb.red;
			bg[1] = rgb.green;
			bg[2] = rgb.blue;
			channels = 3;
			break;
		case B_RGB24_LITTLE:
			bg[0] = rgb.blue;
			bg[1] = rgb.green;
			bg[2] = rgb.red;
			channels = 3;
			break;
		case B_RGB32_BIG:
			bg[0] = rgb.alpha;
			bg[1] = rgb.red;
			bg[2] = rgb.green;
			bg[3] = rgb.blue;
			channels = 4;
			break;
		case B_RGBA32_BIG:
			bg[0] = 0;
			bg[1] = rgb.red;
			bg[2] = rgb.green;
			bg[3] = rgb.blue;
			channels = 4;
			break;
		case B_RGBA32_LITTLE:
			bg[0] = rgb.blue;
			bg[1] = rgb.green;
			bg[2] = rgb.red;
			bg[3] = 0;
			channels = 4;
			break;
		case B_RGB32_LITTLE:
			bg[0] = rgb.blue;
			bg[1] = rgb.green;
			bg[2] = rgb.red;
			bg[3] = rgb.alpha;
			channels = 4;
			break;
		
		default:
			return B_NOT_ALLOWED;
	}

	if (checkOnly)
		return B_OK;

	gimp_init_glue(sourceBitmap, NULL, ioExtension);

	// If we can't find the selection rect extension, do the whole bitmap
	BRect sel_rect = sourceBitmap->Bounds();

	LoadPrefs(&prefs);
	BInvoker* prog;
	GetSettings(ioExtension, &prefs, &sel_rect, NULL, &prog);

	return WhirlPinch(sourceBitmap, sel_rect, channels, bg, prog, &prefs).Render();
}



WhirlPinch::WhirlPinch(BitmapAccessor* src, BRect sel, int32 chan, uchar* background, BInvoker* progress, const Prefs* preferences)
{
	sourceBitmap = src;
	sel_rect = sel;
	channels = chan;
	bg = background;
	prog = progress;
	prefs = preferences;
	cancelled = false;
	get_system_info(&info);
}

int32 size;
status_t WhirlPinch::Render()
{
	status_t err = B_OK;
	double xhsiz, yhsiz;

	sel_width = sel_rect.IntegerWidth();
	sel_height = sel_rect.IntegerHeight();

	x1 = (int32) sel_rect.left;
	y1 = (int32) sel_rect.top;
	x2 = (int32) sel_rect.right;
	y2 = (int32) sel_rect.bottom;
	
	BRect r = sourceBitmap->Bounds();
	extent_left = (int32) r.left;
	extent_top = (int32) r.top;
	extent_right = (int32) r.right;
	extent_bottom = (int32) r.bottom;
	bits = (uchar*) sourceBitmap->AccessBits(r, &stride);


	sel_width  = x2 - x1;
	sel_height = y2 - y1;

	cen_x = (double) (x1 + x2) / 2.0;
	cen_y = (double) (y1 + y2) / 2.0;

	xhsiz = (double) (sel_width) / 2.0;
	yhsiz = (double) (sel_height) / 2.0;

	if (xhsiz < yhsiz) {
		scale_x = yhsiz / xhsiz;
		scale_y = 1.0;
	} else if (xhsiz > yhsiz) {
		scale_x = 1.0;
		scale_y = xhsiz / yhsiz;
	} else {
		scale_x = 1.0;
		scale_y = 1.0;
	} /* else */

	radius = max(xhsiz, yhsiz);


	/*size_t*/ size = stride * (y2 - y1 + 1) - (x1 - 1) * channels;
	dst = (uchar*) malloc(size);

	gimp_progress_init("Whirling and pinching...");
	
	whirl   = prefs->whirl * M_PI / 180;
	radius2 = radius * radius * prefs->radius;

	for (int i = 1; i < info.cpu_count; i++)
	{
		resume_thread(threads[i] = spawn_thread(thread_runner, "whirl_pinch_render", B_NORMAL_PRIORITY, this));
	}

	err = Traverse(0);
	

	for (int i = 1; i < info.cpu_count; i++)
	{
		status_t status;
		wait_for_thread(threads[i], &status);
	}
	
	gimp_progress_update(prog, 1.0);

	if (B_OK == err)
		memcpy(bits + y1 * stride + x1 * channels, dst, size);
	free(dst);
	
	return err;
}

status_t WhirlPinch::Traverse(int32 which_thread)
{
	status_t err = B_OK;
	double cx, cy;
	int32     ix, iy;
	uchar pixel[4][4];
	uchar values[4];
	int32             progress, max_progress;
	progress = 0;

	int32 rows = (y2 - y1 + 1) / info.cpu_count / 2;
	int32 start = y1 + which_thread * rows;
	int32 end = y1 + (which_thread + 1) * rows;
	if (which_thread + 1 == info.cpu_count)
		end++;	// Take care of possible odd row in the middle

	max_progress = rows;

	for (int32 row = start; row <= end; row++)
	{
		uchar* top_row = dst + stride * (row - y1);
		uchar* bot_row = dst + stride * (y2 - row) + (x2 - x1) * channels;

		for (int32 col = x1; col <= x2; col++)
		{
			if (calc_undistorted_coords(col, row, whirl, prefs->pinch, &cx, &cy))
			{
				/* We are inside the distortion area */

				/* Top */

				if (cx >= 0.0)
					ix = (int) cx;
				else
					ix = -((int) -cx + 1);

				if (cy >= 0.0)
					iy = (int) cy;
				else
					iy = -((int) -cy + 1);

				get_pixel(ix,     iy,     pixel[0]);
				get_pixel(ix + 1, iy,     pixel[1]);
				get_pixel(ix,     iy + 1, pixel[2]);
				get_pixel(ix + 1, iy + 1, pixel[3]);

				for (int i = 0; i < channels; i++)
				{
					values[0] = pixel[0][i];
					values[1] = pixel[1][i];
					values[2] = pixel[2][i];
					values[3] = pixel[3][i];

					*top_row++ = bilinear(cx, cy, values);
				} /* for */


				/* Bottom */

				cx = cen_x + (cen_x - cx);
				cy = cen_y + (cen_y - cy);

				if (cx >= 0.0)
					ix = (int) cx;
				else
					ix = -((int) -cx + 1);

				if (cy >= 0.0)
					iy = (int) cy;
				else
					iy = -((int) -cy + 1);

				get_pixel(ix,     iy,     pixel[0]);
				get_pixel(ix + 1, iy,     pixel[1]);
				get_pixel(ix,     iy + 1, pixel[2]);
				get_pixel(ix + 1, iy + 1, pixel[3]);

				for (int i = 0; i < channels; i++) {
					values[0] = pixel[0][i];
					values[1] = pixel[1][i];
					values[2] = pixel[2][i];
					values[3] = pixel[3][i];

					*bot_row++ = bilinear(cx, cy, values);
					ASSERT(bot_row < dst + size);
				} /* for */

				bot_row -= 2 * channels; /* We move backwards! */

			} else {
				/* We are outside the distortion area; just copy the source pixels */

				/* Top */

				get_pixel(col, row, pixel[0]);

				for (int i = 0; i < channels; i++)
					*top_row++ = pixel[0][i];

				/* Bottom */

				get_pixel(x2 - (col - x1),
							y2 - (row - y1),
							pixel[0]);

				for (int i = 0; i < channels; i++)
				{
					*bot_row++ = pixel[0][i];
					ASSERT(bot_row < dst + size);
				}

				bot_row -= 2 * channels; /* We move backwards! */
			} /* else */
		}

		progress ++;
		if (cancelled || (!which_thread && prog && !(row % 5) && !gimp_progress_update(prog, 0.99 * (float) progress / max_progress)))
		{
			cancelled = true;
			err = B_CANCELED;
			break;
		}
	}
	return err;
}

/*****/

int
WhirlPinch::calc_undistorted_coords(double wx, double wy,
			double whirl, double pinch,
			double *x, double *y)
{
	double dx, dy;
	double d, factor;
	double dist;
	double ang, sina, cosa;
	int    inside;

	/* Distances to center, scaled */

	dx = (wx - cen_x) * scale_x;
	dy = (wy - cen_y) * scale_y;

	/* Distance^2 to center of *circle* (scaled ellipse) */

	d = dx * dx + dy * dy;

	/* If we are inside circle, then distort.  Else, just return the same position */

	inside = (d < radius2);

	if (inside) {
		dist = sqrt(d / prefs->radius) / radius;

		/* Pinch */

		factor = pow(sin(M_PI_2 * dist), -pinch);

		dx *= factor;
		dy *= factor;

		/* Whirl */

		factor = 1.0 - dist;

		ang = whirl * factor * factor;

		sina = sin(ang);
		cosa = cos(ang);

		*x = (cosa * dx - sina * dy) / scale_x + cen_x;
		*y = (sina * dx + cosa * dy) / scale_y + cen_y;
	} else {
		*x = wx;
		*y = wy;
	} /* else */

	return inside;
} /* calc_undistorted_coords */


/*****/

uchar
WhirlPinch::bilinear(double x, double y, uchar *values)
{
	double m0, m1;

	x = fmod(x, 1.0);
	y = fmod(y, 1.0);

	if (x < 0.0)
		x += 1.0;

	if (y < 0.0)
		y += 1.0;

	m0 = (double) values[0] + x * ((double) values[1] - values[0]);
	m1 = (double) values[2] + x * ((double) values[3] - values[2]);

	return (uchar) (m0 + y * (m1 - m0));
} /* bilinear */


void WhirlPinch::get_pixel(int x, int y, uchar *pixel)
{
	if (x < extent_left || x > extent_right || y < extent_top || y > extent_bottom)
	{
		for (int i = 0; i < channels; i++)
			pixel[i] = bg[i];
		return;
	}
	
	uchar* p = bits + y * stride + x * channels;
	
	for (int i = 0; i < channels; i++)
		pixel[i] = p[i];
}

int32 WhirlPinch::thread_runner(void* p)
{
	WhirlPinch* it = static_cast<WhirlPinch*>(p);
	
	// Skip first (0th)
	int i;
	thread_id id = find_thread(NULL);
	for (i = 1; i < B_MAX_CPU_COUNT; i++)
	{
		if (id == it->threads[i])
			break;
	}
	
	return it->Traverse(i);
}

