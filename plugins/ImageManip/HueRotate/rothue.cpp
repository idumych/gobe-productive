
/* Rotate Hue 1.0 -- image filter plug-in for the GIMP image manipulation program
 * Copyright (C) 1996 Ian Tester
 *
 * You can contact me at 94024831@postoffice.csu.edu.au
 * You can contact the original The GIMP authors at gimp@xcf.berkeley.edu
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

/* This plug-in "rotates" the hue of an RGB image.
*/

/*
 * This file has been modified from the original.  It was rewritten
 * to use the BeOS libimagemanip API.  -- Scott Lindsey Aug 30, 1998
 */

#include <stdlib.h>
#include <stdio.h>
#include "GIMPGlue.h"
#include "gimp.h"
#include "ImageManipAddon.h"
#include "BitmapAccessor.h"
#include "Config.h"

const char addonName[] = "Rotate Hue";
const char addonInfo[] = "Rotate the color space of an image. Ported from the GIMP by Scott Lindsey <wombat@gobe.com>.  Original code by Ian Tester";
const char addonCategory[] = "Color";
int32 addonVersion = 100;

/* Declare a local function.
 */

static void rothue_image (Image, Image, int32);

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
		int32 angle;

		if (!ioExtension->FindInt32("rothue/angle", &angle))
			prefs.angle = angle;
	}

	Image input = new GIMPImage(sourceBitmap);
	
	Image temp = gimp_temp_image();
	rothue_image(input, temp, prefs.angle);
	input->Copy(temp);

	gimp_free_image(temp);
	delete input;
	
	return B_OK;
}


void rothue_image (Image input, Image output, int32 ramount)
{
  int32 width, height;
  int32 channels, srcrowstride, dstrowstride;
  unsigned char *src,*dest,*src_row,*dest_row;
  short row, col;
  int x1,y1,x2,y2;
  int rh,gs,bv;
  int rr,rg,rb; /* factors for red */
  int gr,gg,gb; /* factors for green */
  int br,bg,bb; /* factors for blue */
  int range,temp;

  gimp_image_area(input,&x1,&y1,&x2,&y2);

  /* Get the size of the input image. (This will/must be the same
   *  as the size of the output image.
   */
  width = gimp_image_width (input);
  height = gimp_image_height (input);
  channels = gimp_image_channels (input);

  src = gimp_image_data (input);
  dest = gimp_image_data (output);
  
  srcrowstride = gimp_image_stride(input);
  dstrowstride = gimp_image_stride(output);

  src += y1 * srcrowstride + x1 * channels;
  dest += y1 * dstrowstride + x1 * channels;

  range = -ramount;
  if (range < 0)
    range += 360;

  rr = rg = rb = gr = gg = gb = br = bg = bb = 0;

  /* need 127.5 for accuracy, but can't do that with integers.
     just use 255 and 510 instead of 127.5 and 255, then divide by 2
  */

  switch (range / 60)
  {
    case 0:
      temp = (range * 255) / 60;

      rr = 510 - temp;
      rg = temp;

      gg = 510 - temp;
      gb = temp;

      br = temp;
      bb = 510 - temp;
      break;
    case 1:
      temp = ((range - 60) * 255) / 60;

      rr = 255 - temp;
      rg = 255 + temp;

      gg = 255 - temp;
      gb = 255 + temp;

      br = 255 + temp;
      bb = 255 - temp;
      break;
    case 2:
      temp = ((range - 120) * 255) / 60;

      rg = 510 - temp;
      rb = temp;

      gr = temp;
      gb = 510 - temp;

      br = 510 - temp;
      bg = temp;
      break;
    case 3:
      temp = ((range - 180) * 255) / 60;

      rg = 255 - temp;
      rb = 255 + temp;

      gr = 255 + temp;
      gb = 255 - temp;

      br = 255 - temp;
      bg = 255 + temp;
      break;
    case 4:
      temp = ((range - 240) * 255) / 60;

      rr = temp;
      rb = 510 - temp;

      gr = 510 - temp;
      gg = temp;

      bg = 510 - temp;
      bb = temp;
      break;
    case 5:
      temp = ((range - 300) * 255) / 60;

      rr = 255 + temp;
      rb = 255 - temp;

      gr = 255 - temp;
      gg = 255 + temp;

      bg = 255 - temp;
      bb = 255 + temp;
      break;
  }

  rr /= 2;
  rg /= 2;
  rb /= 2;

  gr /= 2;
  gg /= 2;
  gb /= 2;

  br /= 2;
  bg /= 2;
  bb /= 2;

  src_row = src;
  dest_row = dest;

  for (row = y1; row < y2; row++)
    {
      src = src_row;
      dest = dest_row;	

      for (col = x1; col < x2; col++)
	{
          rh = src[0];
          gs = src[1];
          bv = src[2];

          dest[0] = ((rh * rr) + (gs * rg) + (bv * rb)) / 255;
          dest[1] = ((rh * gr) + (gs * gg) + (bv * gb)) / 255;
          dest[2] = ((rh * br) + (gs * bg) + (bv * bb)) / 255;
          
          src += channels;
          dest += channels;
	}
	src_row += srcrowstride;
	dest_row += dstrowstride;

/*
      if ((row % 5) == 0)
	gimp_do_progress (row, y2 - y1);
*/
    }
  
}
