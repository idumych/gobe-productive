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
 */

/* pixelize filter written for the GIMP
 *  -Tracy Scott
 *
 * This filter acts as a low pass filter on the color components of
 * the provided region
 */

/*
 * This file has been modified from the original.  It was rewritten
 * to use the BeOS libimagemanip API.  -- Scott Lindsey Aug 23, 1998
*/


#include "GIMPGlue.h"
#include "gimp.h"
#include "ImageManipAddon.h"
#include "BitmapAccessor.h"
#include "Config.h"

static void pixelize (Image, Image, int32);

const char addonName[] = "Pixelize";
const char addonInfo[] = "This filter acts as a low pass filter on the color components of the provided region. Ported from the GIMP by Scott Lindsey <wombat@gobe.com>.  Original code by Tracy Scott";
const char addonCategory[] = "Artistic";
int32 addonVersion = 101;

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

	gimp_init_glue(sourceBitmap, NULL, ioExtension);

	if (ioExtension)
	{
		int32 width;
		if (!ioExtension->FindInt32("pixelize/width", &width))
			prefs.width = width;
	}

	Image input = new GIMPImage(sourceBitmap);
	
    Image temp = gimp_temp_image();
	pixelize(input, temp, prefs.width);
	input->Copy(temp);

    gimp_free_image(temp);
    delete input;
	
	return B_OK;
}


static void
pixelize (Image input, Image output, int32 pixelizewidth)
{
  int32 width, height;
  int32 channels, srcrowstride, dstrowstride;
  unsigned char *src_row, *dest_row;
  unsigned char *src, *dest;
  short row, col;
  int x1, y1, x2, y2, i, j, c;
  unsigned int average[4], count;
  
  /* Get the input area. This is the bounding box of the selection in 
   *  the image (or the entire image if there is no selection). Only
   *  operating on the input area is simply an optimization. It doesn't
   *  need to be done for correct operation. (It simply makes it go
   *  faster, since fewer pixels need to be operated on).
   */
  gimp_image_area (input, &x1, &y1, &x2, &y2);

  src_row = gimp_image_data (input);
  dest_row = gimp_image_data (output);

  /* Get the size of the input image. (This will/must be the same
   *  as the size of the output image.
   */
  width = gimp_image_width (input);
  height = gimp_image_height (input);
  channels = gimp_image_channels (input);
  srcrowstride = gimp_image_stride(input);
  dstrowstride = gimp_image_stride(output);

  /* Advance the source and destination pointers
   */
  src_row += srcrowstride * y1;
  dest_row += dstrowstride * y1;

  for (row = y1; row < y2; row+=pixelizewidth-(row%pixelizewidth))
    {
      src     = src_row;
      dest    = dest_row;
      /*  Calculate across the scanline  */
      for (col = x1; col < x2; col+=((pixelizewidth-(col%pixelizewidth))))
	{
		for (c = 0; c < channels; c++)
			average[c] = 0;
	  count = 0;
	  /* Compute the average values of the pixelize square area */
	  for (j = 0; (j < pixelizewidth - (row % pixelizewidth)) && ((j + row) < y2); j++) {
	      src = src_row + j * srcrowstride + col * channels;
	      for (i = 0; (i < pixelizewidth - (col % pixelizewidth)) && ((i + col) < x2); i++) {
			for (c = 0; c < channels; c++)
				average[c] += *src++;
		  count++;
	      }
	  }

	  if (count)
	    {
	    	for (c = 0; c < channels; c++)
				average[c] = average[c] / count;
	    }
	  
	  /* Store the new average values into the pixelized square */
	  for (j = 0; (j < pixelizewidth - (row % pixelizewidth)) && ((j + row) < y2); j++) {
              dest = dest_row + j * dstrowstride + col * channels;
	      for (i = 0; (i < pixelizewidth - (col % pixelizewidth)) && ((i + col) < x2); i++) {
			for (c = 0; c < channels; c++)
				*dest++ = average[c];
	      }
	  }
	}	  
      src_row += srcrowstride * (pixelizewidth - (row%pixelizewidth));
      dest_row += dstrowstride * (pixelizewidth - (row%pixelizewidth));
    }
}






