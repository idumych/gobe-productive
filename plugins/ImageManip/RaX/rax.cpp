/**********************************************************************
 *  RaX Structurizer Transformation Plug-In (Version 1.00)
 *  Daniel Cotting (cotting@multimania.com)
 **********************************************************************
 *  Official homepages: http://www.multimania.com/cotting
 *                      http://cotting.citeweb.net
 **********************************************************************    
 */

/* The GIMP -- an image manipulation program
 * Copyright (C) 1995 Spencer Kimball and Peter Mattis
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
 *
 * MODIFIED FROM THE ORIGINAL - 7/24/99 Scott Lindsey
 */

#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#include "ImageManipAddon.h"
#include "BitmapAccessor.h"
#include "Config.h"
#include "GIMPGlue.h"

const char addonName[] = "RaX Structurizer";
const char addonInfo[] = "Sinusoidal distortion.  GIMP plugin by Daniel Cotting; ported by Scott Lindsey.";
const char addonCategory[] = "Miscellaneous";
int32 addonVersion = 100;

static const uchar* Bitmap;
static int32 Stride;
static int32 Bpp;
static int32 Right, Bottom;

static void      RaX_render_row  (const uchar *src_row,
        			     uchar *dest_row,
        			     int32 row,
        			     int32 row_width,
        			     int32 bytes,
						float  height,
				     double, double, double,
				     double, double, double);
static void      RaX_get_pixel(int x, int y, uchar *pixel);


status_t RaX(BitmapAccessor* sourceBitmap, BRect sel_rect, BInvoker* prog, const Prefs* prefs);

status_t
Manipulate(
	BitmapAccessor *sourceBitmap,
	BMessage *ioExtension,
	bool checkOnly)
{
	Prefs prefs;

	switch(sourceBitmap->ColorSpace())
	{
		// PUT STUFF HERE
		case B_RGBA32_LITTLE:
		case B_RGB32_LITTLE:
		case B_RGB24_LITTLE:
		case B_RGB24_BIG:
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

	RaX(sourceBitmap, sel_rect, prog, &prefs);
	
	return B_OK;
}

static void
RaX_get_pixel(int x, int y, uchar *pixel)
{
	// BOGUS
	if (x < 0 || y < 0 || x > Right || y > Bottom)
	{
		pixel[0] = pixel[1] = pixel[2] = pixel[3] = 0;
		return;
	}
	
	const uchar* p = Bitmap + (y * Stride) + (x * Bpp);
	for (int i = 0; i < Bpp; i++)
		pixel[i] = p[i];
	
} /* RaX_get_pixel */



static void
RaX_render_row (const uchar *src_row,
        	  uchar *dest_row,
        	  int32 row,
        	  int32 row_width,
        	  int32 bytes,
			  float height,
		  double a, double b, double c,
   	          double d, double e, double f		  
		  )
{
  int32 col, bytenum;
  uchar *pixel;
  pixel=(uchar*) malloc(10);

  height /= 2.0;
  
  
  for (col = 0; col < row_width ; col++)
    {
      short int v1, v2, v3;
      RaX_get_pixel( (int) (cos(((float)col-(float)row_width/2.0)/e)*(float)((float)col-(float)row_width/2.0)/a+
                     sin(((float)row-height)/f)*(float)((float)row-height)/b+ (float)row_width/2.0),
		     (int) (sin(((float)col-(float)row_width/2.0)/e)*(float)((float)col-(float)row_width/2.0)/c+
		     cos(((float)row-height)/f)*(float)((float)row-height)/d+height),
		     pixel);
      v1=pixel[0];
      v2=pixel[1];
      v3=pixel[2];
      
      dest_row[col*bytes] = (int)v1;
      dest_row[col*bytes +1] = (int)v2;
      dest_row[col*bytes +2] = (int)v3;

      if (bytes>3)
        for (bytenum = 3; bytenum<bytes; bytenum++)
          {
            dest_row[col*bytes+bytenum] = src_row[col*bytes+bytenum];
          }
    }
}





status_t RaX(BitmapAccessor* sourceBitmap, BRect sel_rect, BInvoker* prog, const Prefs* prefs)
{
	GIMPImage srcPR(sourceBitmap);
	GIMPImage destPR(sourceBitmap, true);
  int32 width = (int32) sourceBitmap->Bounds().Width() + 1;
  int32 height = (int32) sourceBitmap->Bounds().Height() + 1;
  int32 bytes = (int32) srcPR.bpp;
  uchar *src_row;
  uchar *dest_row;
  int32 row;
  int32 x1, y1, x2, y2;
  double a,b,c;
  double d,e,f;
	status_t result = B_OK;

	gimp_progress_init("Transforming...");

	// BOGUS
	Bitmap = (const uchar*) sourceBitmap->AccessBits(sourceBitmap->Bounds(), &Stride);
	Bpp = (int32) sourceBitmap->BytesPerPixel();
	Right = (int32) sourceBitmap->Bounds().right;
	Bottom = (int32) sourceBitmap->Bounds().bottom;

	x1 = (int32) sel_rect.left;
	x2 = (int32) sel_rect.right;
	y1 = (int32) sel_rect.top;
	y2 = (int32) sel_rect.bottom;

  /*  allocate row buffers  */
  src_row = (uchar *) malloc ((x2 - x1 + 1) * bytes);
  dest_row = (uchar *) malloc ((x2 - x1 + 1) * bytes);


  a = prefs->a;
  b = prefs->b;
  c = prefs->c;
  d = prefs->d;
  e = prefs->e;
  f = prefs->f;

  for (row = y1; row <= y2; row++)

    {
      srcPR.GetRow(src_row, x1, row, (x2 - x1 + 1));

      RaX_render_row (src_row,
        		dest_row,
        		row,
        		(x2 - x1 + 1),
        		bytes,
				(float) height,
        		a, b, c,
			d, e, f);

      /*  store the dest  */
      destPR.SetRow(dest_row, x1, row, (x2 - x1 + 1));

      if ((row % 10) == 0 && !gimp_progress_update (prog, (double) row / (double) (y2 - y1 + 1)))
		{
			result = B_CANCELED;
			break;
		}
    }

  free (src_row);
  free (dest_row);

	srcPR.Copy(&destPR);
	return result;
}

