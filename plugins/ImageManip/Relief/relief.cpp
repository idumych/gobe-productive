/* relief filter for the GIMP
 *  -Peter Mattis
 *
 * The code for this filter is heavily based upon the code for
 *  "ppmrelief" in the "netpbm" package. That code is freely modifyable
 *  as int32 as the following notice appears.
 */

/* ppmrelief.c - generate a relief map of a portable pixmap
**
** Copyright (C) 1990 by Wilson H. Bent, Jr.
**
** Permission to use, copy, modify, and distribute this software and its
** documentation for any purpose and without fee is hereby granted, provided
** that the above copyright notice appear in all copies and that both that
** copyright notice and this permission notice appear in supporting
** documentation.  This software is provided "as is" without express or
** implied warranty.
*/

#include <stdio.h>
#include "GIMPGlue.h"
#include "gimp.h"
#include "ImageManipAddon.h"
#include "BitmapAccessor.h"

const char addonName[] = "Relief";
const char addonInfo[] = "Generate a relief map of an image. Ported from the GIMP by Scott Lindsey <wombat@gobe.com>.  Original code by Wilson H. Bent, Jr";
const char addonCategory[] = "Miscellaneous";
int32 addonVersion = 100;

static void relief (Image, Image, int alpha_offset);

status_t
Manipulate(
	BitmapAccessor *sourceBitmap,
	BMessage *ioExtension,
	bool checkOnly)
{
	int alpha;
	switch(sourceBitmap->ColorSpace())
	{
		case B_RGBA32_LITTLE:
		case B_RGB32_LITTLE:
			alpha = 3;
			break;
		case B_RGBA32_BIG:
		case B_RGB32_BIG:
			alpha = 0;
			break;
		case B_RGB24_LITTLE:
		case B_RGB24_BIG:
		case B_GRAYSCALE_8_BIT:
			alpha = -1;
			break;
		default:
			return B_NOT_ALLOWED;
	}

	if (checkOnly)
		return B_OK;

	gimp_init_glue(sourceBitmap, NULL, ioExtension);

	Image input = new GIMPImage(sourceBitmap);
	
    Image temp = gimp_temp_image();
	relief(input, temp, alpha);
	input->Copy(temp);

    gimp_free_image(temp);
    delete input;
	
	return B_OK;
}

void relief (Image input, Image output, int alpha_offset)
{
  unsigned char *dest, *p;
  unsigned char *prev_row, *cur_row;
  int32 width, height;
  int32 channels, srcrowstride, dstrowstride;
  int row, col;
  int mv2, val;
  int x1, y1, x2, y2;
  int left, right;
  int top, bottom;
  int cur_progress;
  int max_progress;

  gimp_image_area (input, &x1, &y1, &x2, &y2);

  width = gimp_image_width (input);
  height = gimp_image_height (input);
  channels = gimp_image_channels (input);

  left = (x1 == 0);
  right = (x2 == width);
  top = (y1 == 0);
  bottom = (y2 == height);

  if (x1 == 0)
    x1++;
  if (y1 == 0)
    y1++;
  if (x2 == width)
    x2--;
  if (y2 == height)
    y2--;

  x1 *= channels;
  x2 *= channels;

  p = dest = gimp_image_data (output);
  prev_row = gimp_image_data (input);

  srcrowstride = gimp_image_stride(input);
  dstrowstride = gimp_image_stride(output);

  prev_row += (y1 - 1) * srcrowstride;
  cur_row = prev_row + srcrowstride;


  mv2 = 128;

  if (top)
    {
      for (col = 0; col < dstrowstride; col++)
		if ((col & 3) != alpha_offset)
			*dest++ = 0;
		else
			dest++;
    }

  cur_progress = y1;
  max_progress = y2;

  /* Now the rest of the image - read in the 3rd row of inputbuf,
  ** and convolve with the first row into the output buffer.
  */
  for (row = y1; row < y2; ++row)
    {
		dest = p + row * dstrowstride;
      if (left)
	{
	  for (col = 0; col < channels; col++)
		{
			if ((col & 3) != alpha_offset)
		    	*dest++ = 0;
			else
				dest++;
		}
	}
      else
	{
	  dest += x1;
	}

      for (col = x1; col < x2; col++)
	{
	  if ((col & 3) != alpha_offset)
	  {
		  val = prev_row[col - channels] - cur_row[col + channels] + mv2;
		  if (val < 0) val = 0;
		  if (val > 255) val = 255;
		  *dest++ = val;
	  }
	  else
		dest++;
	}
     
      if (right)
	{
	  for (col = col; col < dstrowstride; col++)
		{
			if ((col & 3) != alpha_offset)
		    	*dest++ = 0;
			else
				dest++;
		}
	}

      prev_row = cur_row;
      cur_row += srcrowstride;

      cur_progress++;
      if ((cur_progress % 5) == 0)
	gimp_do_progress (cur_progress, max_progress);
    }

  /* And write the last row, zeros again. */
  if (bottom)
    for (col = 0; col < dstrowstride; col++)
		{
			if ((col & 3) != alpha_offset)
		    	*dest++ = 0;
			else
				dest++;
		}
}
