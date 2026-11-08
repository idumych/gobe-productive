/*
  illusion.cpp, by Scott Lindsey <wombat@gobe.com>, Aug 1998

  This is based on code by

  Copyright (C) 1997  Hirotsuna Mizuno
                      s1041150@u-aizu.ac.jp
*/

/* This file has been modified from the original; converted to the ImageManip API */
/* Scott Lindsey 8/1/99 */


/*
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
*/

#include <malloc.h>
#include <string.h>
#include "ImageManipAddon.h"
#include "BitmapAccessor.h"
#include "Config.h"

const char addonName[] = "Illusion";
const char addonInfo[] = "Surround an image with multiple copies of itself.  By Scott Lindsey <wombat@gobe.com>, based on work by Hirotsuna Mizuno.";
const char addonCategory[] = "Effects";
int32 addonVersion = 100;

#ifndef PI
#define PI 3.141592653589793238462643383279
#endif
#ifndef PI_2
#define PI_2 (PI*2)
#endif


status_t
Manipulate(
	BitmapAccessor *sourceBitmap,
	BMessage *ioExtension,
	bool checkOnly)
{
	Prefs prefs;

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

	LoadPrefs(&prefs);

	int bpp = (int) sourceBitmap->BytesPerPixel();
	int32 division;
	BRect bounds = sourceBitmap->Bounds(), selection;
	selection = bounds;
	
	if (ioExtension)
	{
		BRect r;
		if (!ioExtension->FindRect("selection_rect", &r))
			selection = r;
		
		if (!ioExtension->FindInt32("illusion/division", &division))
			prefs.division = division;
	}
	division = prefs.division;

	int32 rowBytes;
	int select_y1, select_y2;
	int select_x1, select_x2;
	int image_height = bounds.IntegerHeight() + 1;
	int image_width = bounds.IntegerWidth() + 1;
	int select_height = selection.IntegerHeight() + 1;
	int select_width = selection.IntegerWidth() + 1;
	double center_y, center_x;

	select_y1 = (int) selection.top;
	select_y2 = (int) selection.bottom;
	select_x1 = (int) selection.left;
	select_x2 = (int) selection.right;

	center_x = select_x1 + (double) select_width / 2;
  	center_y = select_y1 + (double) select_height / 2;

	uchar** pixels = (uchar **) malloc(image_height * sizeof(uchar *));
	uchar** destpixels = (uchar **) malloc(image_height * sizeof(uchar *));

	for (int y = 0; y < image_height; y++)
	{
		bounds.top = bounds.bottom = y;
		ssize_t size = image_width * bpp;
		pixels[y] = (uchar *) malloc(size);
		destpixels[y] = (uchar *) malloc(size);
		memcpy(pixels[y], sourceBitmap->AccessBits(bounds, &rowBytes), size);
		sourceBitmap->BitsNotChanged();
	}


	double scale = sqrt(select_width*select_width + select_height*select_height) / 2;
	int offset = (int)(scale / 2);

	for (int y = select_y1; y <= select_y2; y++)
	{
		double cy = ((double) y - center_y) / scale;
		for (int x = select_x1; x <= select_x2; x++)
		{
			double cx = ((double) x - center_x) / scale;
			double angle = floor(atan2(cy,cx) * division / PI_2) * PI_2 / division + (PI / division);
			double radius = sqrt((double)(cx*cx+cy*cy));
			int xx = (int)(x - offset * cos(angle));
			int yy = (int)(y - offset * sin(angle));

			if (xx < 0)
				xx = 0;
			else if (image_width <= xx) 
				xx = image_width - 1;

			if (yy < 0)
				yy = 0;
			else if (image_height <= yy)
				yy = image_height - 1;

			for (int b = 0; b < bpp; b++)
			{
				destpixels[y][x*bpp+b] = (uchar)((1-radius)*pixels[y][x*bpp+b] +
					radius*pixels[yy][xx*bpp+b]);
			}
		}
		selection.top = selection.bottom = y;
		memcpy(sourceBitmap->AccessBits(selection, &rowBytes), destpixels[y] + select_x1 * bpp, (select_x2 - select_x1 + 1) * bpp);
	}


	for (int y = 0; y < image_height; y++)
		free(pixels[y]);
	free(pixels);
	for (int y = 0; y < image_height; y++)
		free(destpixels[y]);
	free(destpixels);

	return B_OK;
}