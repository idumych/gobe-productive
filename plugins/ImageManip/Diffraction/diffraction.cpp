/* The GIMP -- an image manipulation program
 * Copyright (C) 1995 Spencer Kimball and Peter Mattis
 *
 * Diffraction plug-in --- Generate diffraction patterns
 * Copyright (C) 1997 Federico Mena Quintero and David Bleecker
 * federico@nuclecu.unam.mx
 * bleecker@math.hawaii.edu
 */ 

/* This file has been modified from the original; converted to the ImageManip API */
/* Scott Lindsey 8/1/99 */

/*
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


#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#include "ImageManipAddon.h"
#include "BitmapAccessor.h"
#include "Config.h"
#include "GIMPGlue.h"

/***** Magic numbers *****/

#define ITERATIONS   100      /* 100 */
#define WEIRD_FACTOR 0.04     /* 0.04 */


/***** Prototypes *****/

static status_t diffraction(BitmapAccessor* sourceBitmap, BRect sel_rect, int32 rowStart, int32 rowEnd, BInvoker* prog, const Prefs* prefs);

static void   diff_diffract(float x, float y, float *r, float *g, float *b, float poldiff, float polsum, const Prefs* prefs);
static float diff_point(float x, float y, float edges, float contours, float lam, float poldiff, float polsum, const Prefs* prefs);
static float diff_intensity(float x, float y, float lam, float poldiff, float polsum, float scattering);

static float cos_lut[ITERATIONS + 1];
static float param_lut1[ITERATIONS + 1];
static float param_lut2[ITERATIONS + 1];

class InitObj
{
public:
	InitObj();
};

class ThreadRunner
{
public:
	ThreadRunner(BitmapAccessor* bitmap, BRect sel, int32 start, int32 end, const Prefs* prefs);
	~ThreadRunner();

	static int32	Thread(void*);
	void		  	Go();

	const Prefs*	mPrefs;
	BRect			mSel;
	BitmapAccessor* mBitmap;
	thread_id		mThreadID;
	int32			mRowStart, mRowEnd;
};


static InitObj Init;
const char addonName[] = "Diffraction";
const char addonInfo[] = "Generate diffraction patterns.  Original GIMP plugin by Federico Mena Quintero & David Bleecker.  Ported by Scott Lindsey.";
const char addonCategory[] = "Render";
int32 addonVersion = 100;


status_t
Manipulate(
	BitmapAccessor *sourceBitmap,
	BMessage *ioExtension,
	bool checkOnly)
{
	Prefs prefs;

	switch(sourceBitmap->ColorSpace())
	{
		case B_RGBA32_LITTLE:
		case B_RGB32_LITTLE:
		case B_RGB24_LITTLE:
		case B_RGB24_BIG:
		case B_RGB32_BIG:
		case B_RGBA32_BIG:
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

	system_info info;
	get_system_info(&info);
	ThreadRunner** threads = info.cpu_count > 1 ? new ThreadRunner* [info.cpu_count - 1]: NULL;

	BRect r(sel_rect);
	r.bottom = rint(r.top + (r.bottom - r.top) / info.cpu_count);

	for (int x = 0; x < info.cpu_count - 1; x++)
	{
		threads[x] = new ThreadRunner(sourceBitmap, sel_rect, (int32) r.top, (int32) r.bottom, &prefs);
		r.OffsetBy(0, r.Height() + 1);
	}

	// Main renderer
	r.bottom = sel_rect.bottom;
	status_t result = diffraction(sourceBitmap, sel_rect, (int32) r.top, (int32) r.bottom, prog, &prefs);
	
	for (int x = 0; x < info.cpu_count - 1; x++)
		delete threads[x];
	delete threads;
	
	return result;
}

/*****/

static status_t
diffraction(BitmapAccessor* sourceBitmap, BRect sel, int32 rowStart, int32 rowEnd, BInvoker* prog, const Prefs* prefs)
{
	status_t  result = B_OK;
	int32      x1, y1, x2, y2;
	int32      width, height;
	int32      row, col, stride;
	uchar     *dest_row;
	uchar     *dest;
	float    left, right, bottom, top;
	float    dhoriz, dvert;
	float    px, py;
	float    r, g, b, a;
	float		polpi2, cospolpi2, sinpolpi2;
	float	poldiff, polsum;
	color_space colors = sourceBitmap->ColorSpace();

	gimp_progress_init("Creating diffraction pattern...");

	/* Get the mask bounds and image size */

	x1 = (int) sel.left;
	x2 = (int) sel.right;
	y1 = (int) sel.top;
	y2 = (int) sel.bottom;

	width  = x2 - x1 + 1;
	height = y2 - y1 + 1;

	/* Create diffraction pattern */

	left   = -5.0;
	right  =  5.0;
	bottom = -5.0;
	top    =  5.0;

	dhoriz = (right - left) / (width - 1);
	dvert  = (bottom - top) / (height - 1);

	BRect rect(x1, y1, x2, y2);
	dest_row = (uchar*) sourceBitmap->AccessBits(rect, &stride);
	dest_row += (rowStart - y1) * stride;
	
	py = top + (rowStart - y1) * dvert;

	polpi2 = prefs->polarization * (M_PI / 2.0);

	cospolpi2 = cos(polpi2);
	sinpolpi2 = sin(polpi2);
	poldiff = cospolpi2 - sinpolpi2;
	polsum = cospolpi2 + sinpolpi2;


	for (row = rowStart; row <= rowEnd; row++)
	{
		dest = dest_row;
		
		px = left;
		
		for (col = 0; col < width; col++)
		{
			diff_diffract(px, py, &r, &g, &b, poldiff, polsum, prefs);

			switch(colors)
			{
				case B_RGBA32_LITTLE:
					*dest++ = (uchar) (255.0 * b);
					*dest++ = (uchar) (255.0 * g);
					*dest++ = (uchar) (255.0 * r);
					*dest++ = 255;
					break;
				case B_RGB32_LITTLE:
					*dest++ = (uchar) (255.0 * b);
					*dest++ = (uchar) (255.0 * g);
					*dest++ = (uchar) (255.0 * r);
					dest++;
					break;
				case B_RGB24_LITTLE:
					*dest++ = (uchar) (255.0 * b);
					*dest++ = (uchar) (255.0 * g);
					*dest++ = (uchar) (255.0 * r);
					break;
				case B_RGBA32_BIG:
					*dest++ = 255;
				case B_RGB24_BIG:
					*dest++ = (uchar) (255.0 * r);
					*dest++ = (uchar) (255.0 * g);
					*dest++ = (uchar) (255.0 * b);
					break;
				case B_RGB32_BIG:
					dest++;
					*dest++ = (uchar) (255.0 * r);
					*dest++ = (uchar) (255.0 * g);
					*dest++ = (uchar) (255.0 * b);
					break;
			}
			px += dhoriz;

		} /* for */
		py       += dvert;
		dest_row += stride;
		if (row % 3 == 0 && !gimp_progress_update(prog, (float) (row - rowStart) / (rowEnd - rowStart + 1)))
		{
			result = B_CANCELED;
			break;
		}
	} /* for */


	return result;
} /* diffraction */


/*****/

static void
diff_diffract(float x, float y, float *r, float *g, float *b, float poldiff, float polsum, const Prefs* prefs)
{
	*r = diff_point(x, y, prefs->edges_r, prefs->contour_r, prefs->lam_r, poldiff, polsum, prefs);
	*g = diff_point(x, y, prefs->edges_g, prefs->contour_g, prefs->lam_g, poldiff, polsum, prefs);
	*b = diff_point(x, y, prefs->edges_b, prefs->contour_b, prefs->lam_b, poldiff, polsum, prefs);
} /* diff_diffract */


/*****/

static float
diff_point(float x, float y, float edges, float contours, float lam, float poldiff, float polsum, const Prefs* prefs)
{
	return fabs(edges * sin(contours * atan(prefs->brightness * diff_intensity(x, y, lam, poldiff, polsum, prefs->scattering))));
} /* diff_point */


/*****/

static float
diff_intensity(float x, float y, float lam, float poldiff, float polsum, float scattering)
{
	int    i;
	float cxy, sxy;
	float param;

	cxy = 0.0;
	sxy = 0.0;

	lam *= 4.0;

	for (i = 0; i < ITERATIONS; i++) {
		param = lam *
			(cos_lut[i] * x +
			 param_lut1[i] * y -
			 param_lut2[i]);

		cxy += cos(param);
		sxy += sin(param);
	} /* for */

	cxy *= WEIRD_FACTOR;
	sxy *= WEIRD_FACTOR;

	return scattering * (polsum * cxy * cxy +
				   poldiff * sxy * sxy);
} /* diff_intensity */


InitObj::InitObj()
{
	int    i;
	float a;
	float sina;

	a = -M_PI;

	for (i = 0; i <= ITERATIONS; i++) {
		sina = sin(a);
		
		cos_lut[i]    = cos(a);

		param_lut1[i] = 0.75 * sina;
		param_lut2[i] = 0.5 * (4.0 * cos_lut[i] * cos_lut[i] + sina * sina);

		a += (2.0 * M_PI / ITERATIONS);
	} /* for */
}

#pragma mark -

ThreadRunner::ThreadRunner(BitmapAccessor* bitmap, BRect sel, int32 start, int32 end, const Prefs* prefs)
{
	mPrefs = prefs;
	mSel = sel;
	mBitmap = bitmap;
	mRowStart = start;
	mRowEnd = end;
	
	mThreadID = spawn_thread(Thread, "diffraction_render", B_NORMAL_PRIORITY, this);
	resume_thread(mThreadID);
}

ThreadRunner::~ThreadRunner()
{
	status_t err;
	wait_for_thread(mThreadID, &err);
}


int32 ThreadRunner::Thread(void*p)
{
	static_cast<ThreadRunner*>(p)->Go();
	
	return B_OK;
}

void ThreadRunner::Go()
{
	diffraction(mBitmap, mSel, mRowStart, mRowEnd, NULL, mPrefs);
}


