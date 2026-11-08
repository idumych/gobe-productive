/*******************************************************************************

	fractaltrace.c  -- This is a plug-in for the GIMP 1.0

	Copyright (C) 1997  Hirotsuna Mizuno
					            s1041150@u-aizu.ac.jp

	This program is free software; you can redistribute it and/or modify it
	under the terms of the GNU General Public License as published by the Free
	Software Foundation; either version 2 of the License, or (at your option)
	any later version.

	This program is distributed in the hope that it will be useful, but WITHOUT
	ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
	FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
	more details.

	You should have received a copy of the GNU General Public License along with
	this program; if not, write to the Free Software Foundation, Inc.,
	675 Mass Ave, Cambridge, MA 02139, USA.

*******************************************************************************/


#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <algobase.h>

#include "ImageManipAddon.h"
#include "BitmapAccessor.h"
#include "Config.h"
#include "GIMPGlue.h"

const char addonName[] = "Fractal Trace";
const char addonInfo[] = "Transforms an image with the Mandelbrot set.\nOriginal GIMP plugin by Hirotsuna Mizuno.\nPorted by Scott Lindsey.";
const char addonCategory[] = "Distortion";
int32 addonVersion = 100;


class fractal_trace
{
public:
	fractal_trace(BitmapAccessor *sourceBitmap, BRect r, BInvoker* prog, Prefs* prefs);
	~fractal_trace()	{free(dest);}
	void filter();

private:
	typedef struct {
		int32         width;
		int32         height;
		int32         bpp;
		int32         alpha;
	} image_t;

	void pixels_get( int32 x, int32 y, rgb_color *pixel );
	void pixels_get_bilinear( double x, double y, rgb_color *pixel );
	void pixels_set( int32 x, int32 y, rgb_color *pixel );
	void pixels_store( void );
	void mandelbrot( double x, double y, double *u, double *v );

	typedef struct {
		int32     x1;
		int32     x2;
		int32     y1;
		int32     y2;
		int32     width;
		int32     height;
	} selection_t;
	
	
	selection_t selection;
	image_t     image;
		
	uchar    *src;
	uchar    *dest;
	
	BitmapAccessor* mSrc;
	BInvoker* mProg;
	Prefs* mPrefs;
	int32 srcStride;
	int32 destStride;
	color_space depth;
	rgb_color fore;
	rgb_color back;
};


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

	fractal_trace(sourceBitmap, sel_rect, prog, &prefs).filter();
	
	return B_OK;
}


fractal_trace::fractal_trace(BitmapAccessor *sourceBitmap, BRect r, BInvoker* prog, Prefs* prefs)
{
	mSrc = sourceBitmap;
	selection.x1 = (int32) r.left;
	selection.x2 = (int32) r.right + 1;
	selection.y1 = (int32) r.top;
	selection.y2 = (int32) r.bottom + 1;
	selection.height = selection.y2 - selection.y1;
	selection.width = selection.x2 - selection.x1;
	mProg = prog;
	mPrefs = prefs;
	
	destStride = selection.width * 4;
	dest = (uchar*) malloc(selection.height * destStride);
	
	r = sourceBitmap->Bounds();
	image.height = (int32) r.bottom;
	image.width = (int32) r.right;
	src = (uchar*) mSrc->AccessBits(r, &srcStride);
	
	depth = mSrc->ColorSpace();

	back.alpha = fore.alpha = 255;
	gimp_foreground_color(&fore.red, &fore.green, &fore.blue);
	gimp_background_color(&back.red, &back.green, &back.blue);
}


void fractal_trace::pixels_get( int32 x, int32 y, rgb_color *pixel )
{
	x = max(min(x, image.width - 1), 0);
	y = max(min(y, image.height - 1), 0);
	
	uchar* p = src + y * srcStride + x * 4;

	switch(depth)
	{
		case B_RGB32_LITTLE:
			pixel->red = p[2];
			pixel->green = p[1];
			pixel->blue = p[0];
			pixel->alpha = 255;
			break;
		case B_RGBA32_LITTLE:
			pixel->red = p[2];
			pixel->green = p[1];
			pixel->blue = p[0];
			pixel->alpha = p[3];
			break;
	}
}

void fractal_trace::pixels_get_bilinear( double x, double y, rgb_color *pixel )
{
	rgb_color A, B, C, D;
	double a, b, c, d;
	int32    x1, y1, x2, y2;
	double dx, dy;

	x1 = (int32)floor( x );
	x2 = x1 + 1;
	y1 = (int32)floor( y );
	y2 = y1 + 1;

	dx = x - (double)x1;
	dy = y - (double)y1;
	a  = ( 1.0 - dx ) * ( 1.0 - dy );
	b  = dx * ( 1.0 - dy );
	c  = ( 1.0 - dx ) * dy;
	d  = dx * dy;
	
	pixels_get( x1, y1, &A );
	pixels_get( x2, y1, &B );
	pixels_get( x1, y2, &C );
	pixels_get( x2, y2, &D );

	pixel->red = (uchar)( a * (double)A.red + b * (double)B.red +
					 c * (double)C.red + d * (double)D.red );
	pixel->green = (uchar)( a * (double)A.green + b * (double)B.green +
					 c * (double)C.green + d * (double)D.green );
	pixel->blue = (uchar)( a * (double)A.blue + b * (double)B.blue +
					 c * (double)C.blue + d * (double)D.blue );
	pixel->alpha = (uchar)( a * (double)A.alpha + b * (double)B.alpha +
					 c * (double)C.alpha + d * (double)D.alpha );
}

void fractal_trace::pixels_set( int32 x, int32 y, rgb_color *pixel )
{
	x -= selection.x1;
	y -= selection.y1;
	
	uchar* p = dest + y * destStride + x * 4;
	
	switch(depth)
	{
		case B_RGB32_LITTLE:
			p[2] = pixel->red;
			p[1] = pixel->green;
			p[0] = pixel->blue;
			p[3] = 255;
			break;
		case B_RGBA32_LITTLE:
			p[2] = pixel->red;
			p[1] = pixel->green;
			p[0] = pixel->blue;
			p[3] = pixel->alpha;
			break;
	}
}

void fractal_trace::pixels_store( void )
{
	int32 y;

	uchar* d = src + selection.y1 * srcStride + selection.x1 * 4;
	uchar* s = dest;
	
	for( y = selection.y1; y < selection.y2; y++ )
	{
		memcpy(d, s, destStride);
		d += srcStride;
		s += destStride;
	}
}

/******************************************************************************/

void fractal_trace::mandelbrot( double x, double y, double *u, double *v )
{
	int32    iter = 0;
	double xx   = x;
	double yy   = y;
	double x2   = xx * xx;
	double y2   = yy * yy;
	double tmp;
	while( iter < mPrefs->depth )
	{
		tmp = x2 - y2 + x;
		yy  = 2 * xx * yy + y;
		xx  = tmp;
		x2  = xx * xx;
		y2  = yy * yy;
		iter++;
	}
	*u = xx;
	*v = yy;
}


/******************************************************************************/

void fractal_trace::filter()
{
	int32    x, y;
	rgb_color pixel;
	double scale_x, scale_y;
	double cx, cy;
	double px, py;

	gimp_progress_init("Rendering...");

	scale_x = ( mPrefs->x2 - mPrefs->x1 ) / selection.width;
	scale_y = ( mPrefs->y2 - mPrefs->y1 ) / selection.height;

	for( y = selection.y1; y < selection.y2; y++ )
	{
		cy = mPrefs->y1 + ( y - selection.y1 ) * scale_y;
		for( x = selection.x1; x < selection.x2; x++ )
		{
			cx = mPrefs->x1 + ( x - selection.x1 ) * scale_x;
			mandelbrot( cx, cy, &px, &py );
			px = ( px - mPrefs->x1 ) / scale_x + selection.x1;
			py = ( py - mPrefs->y1 ) / scale_y + selection.y1;
			if( 0 <= px && px < image.width && 0 <= py && py < image.height )
			{
				pixels_get_bilinear( px, py, &pixel );
			}
			else
			{
				switch( mPrefs->outside_type )
				{
					case OUTSIDE_TYPE_WRAP:
						px = fmod( px, image.width );
						py = fmod( py, image.height );
						if( px < 0.0 ) px += image.width;
						if( py < 0.0 ) py += image.height;
						pixels_get_bilinear( px, py, &pixel );
						break;
					case OUTSIDE_TYPE_TRANSPARENT:
						pixel.red = pixel.green = pixel.blue = 0;
						pixel.alpha = 0;
						break;
					case OUTSIDE_TYPE_FORE:
						pixel = fore;
						break;
					case OUTSIDE_TYPE_BACK:
						pixel = back;
						break;
				}
			}
			pixels_set( x, y, &pixel );
		}
		gimp_progress_update(mProg, (double)(y-selection.y1)/selection.height );
	}

	pixels_store();

}

