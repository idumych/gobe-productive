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

/* 
 * This filter displaces an images pixels by a random amount.
 *  The idea for this filter is based on ppmspread, a program
 *  that is part of the netpbm package.
 */

/*
 * This file has been modified from the original.  It was rewritten
 * to use the BeOS libimagemanip API.  -- Scott Lindsey Aug 22, 1998
 */


#include <stdlib.h>
#include <time.h>
#include "GIMPGlue.h"
#include "gimp.h"
#include "ImageManipAddon.h"
#include "BitmapAccessor.h"
#include "Config.h"

const char addonName[] = "Spread";
const char addonInfo[] = "Displace the pixels of an image by a random amount. Ported from the GIMP by Scott Lindsey <wombat@gobe.com>.";
const char addonCategory[] = "Distortion";
int32 addonVersion = 100;

static void spread (Image, Image, int32);

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
		case B_GRAYSCALE_8_BIT:
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
		int32 amount;

		if (!ioExtension->FindInt32("spread/amount", &amount))
			prefs.amount = amount;
	}

	Image input = new GIMPImage(sourceBitmap);
	
    Image temp = gimp_temp_image();
	spread(input, temp, prefs.amount);
	input->Copy(temp);

    gimp_free_image(temp);
    delete input;
	
	return B_OK;
}


#if 0
static char *prog_name;

/* amount of spread */
int32 amount = 5;
int dialog_ID;

int
main (argc, argv)
     int argc;
     char **argv;
{
  Image input, output;
  void *data;
  int scale_ID;

  /* Save the program name so we can use it later in reporting errors
   */
  prog_name = argv[0];

  /* Call 'gimp_init' to initialize this filter.
   * 'gimp_init' makes sure that the filter was properly called and
   *  it opens pipes for reading and writing.
   */
  if (gimp_init (argc, argv))
    {
      input = output = 0;

      /* This is a regular filter. What that means is that it operates
       *  on the input image. Output is put into the ouput image. The
       *  filter should not worry, or even care where these images come
       *  from. The only guarantee is that they are the same size and
       *  depth.
       */
      input = gimp_get_input_image (0);

      /* If input image is available, then do some work. (Spread). 
       *  Then update the output image.
       */
      if (input)
	switch (gimp_image_type (input))
	  {
	  case RGB_IMAGE:
	  case GRAY_IMAGE:
	    data = gimp_get_params ();
	    if (data)
	      amount = *((int32*) data);
	    
	    dialog_ID = gimp_new_dialog ("Spread");
	    scale_ID = gimp_new_scale (dialog_ID, DEFAULT, 1, 200, amount, 0);
	    gimp_new_label (dialog_ID, scale_ID, "Amount:");
	    gimp_add_callback (dialog_ID, scale_ID, scale_callback, &amount);
	    gimp_add_callback (dialog_ID, gimp_ok_item_id (dialog_ID), ok_callback, 0);
	    gimp_add_callback (dialog_ID, gimp_cancel_item_id (dialog_ID), cancel_callback, 0);
	    
	    if (gimp_show_dialog (dialog_ID))
	      {
		gimp_set_params (sizeof (amount), &amount);
		
		output = gimp_get_output_image (0);
		if (output)
		  {
		    spread (input, output);
		    gimp_update_image (output);
		  }
	      }
	    break;
	  case INDEXED_IMAGE:
	    gimp_message ("spread: cannot operate on indexed color images");
	    break;
	  default:
	    gimp_message ("spread: cannot operate on unknown image types");
	    break;
	  }

      /* Free both images.
       */
      if (input)
	gimp_free_image (input);
      if (output)
	gimp_free_image (output);

      /* Quit
       */
      gimp_quit ();
    }

  return 0;
}

static void
scale_callback (item_ID, client_data, call_data)
     int item_ID;
     void *client_data;
     void *call_data;
{
  *((int32*) client_data) = *((int32*) call_data);
}

static void
ok_callback (item_ID, client_data, call_data)
     int item_ID;
     void *client_data;
     void *call_data;
{
  gimp_close_dialog (dialog_ID, 1);
}

static void
cancel_callback (item_ID, client_data, call_data)
     int item_ID;
     void *client_data;
     void *call_data;
{
  gimp_close_dialog (dialog_ID, 0);
}
#endif



static void spread(Image input, Image output, int32 amount)
{
  int32 width, height;
  int32 channels, srcrowstride, dstrowstride;
  unsigned char *src_image, *dest_image;
  unsigned char *src_row, *dest_row;
  unsigned char *src, *dest;
  unsigned char *pixel;
  short row, col;
  int x1, y1, x2, y2;
  int mod_value, sub_value;
  int xdist, ydist;
  int xnew, ynew;
  int i;
  
  /* Get the input area. This is the bounding box of the selection in 
   *  the image (or the entire image if there is no selection). Only
   *  operating on the input area is simply an optimization. It doesn't
   *  need to be done for correct operation. (It simply makes it go
   *  faster, since fewer pixels need to be operated on).
   */
  gimp_image_area (input, &x1, &y1, &x2, &y2);

  /* Get the size of the input image. (This will/must be the same
   *  as the size of the output image.
   */
  width = gimp_image_width (input);
  height = gimp_image_height (input);
  channels = gimp_image_channels (input);

  src_row = src_image = gimp_image_data (input);
  dest_row = dest_image = gimp_image_data (output);

  srcrowstride = gimp_image_stride(input);
  dstrowstride = gimp_image_stride(output);
  
  /* Advance the source and destination pointers
   */
  src_row += srcrowstride * y1 + (x1 * channels);
  dest_row += dstrowstride * y1 + (x1 * channels);

  /* Clear out the output image.
   */
  for (row = y1; row < y2; row++)
    {
      dest = dest_row;
      
      for (col = x1; col < x2; col++)
	for (i = 0; i < channels; i++)
	  *dest++ = 0;

      dest_row += dstrowstride;
    }

  dest_row = dest_image;
  dest_row += dstrowstride * y1 + (x1 * channels);

  srand (time (NULL));
  mod_value = amount + 1;
  sub_value = mod_value / 2;

  for (row = y1; row < y2; row++)
    {
      src     = src_row;
      dest    = dest_row;
      
      /*  Calculate across the scanline  */
      for (col = x1; col < x2; col++)
	{
	  xdist = (rand () & mod_value) - sub_value;
	  ydist = (rand () & mod_value) - sub_value;

	  xnew = col + xdist;
	  ynew = row + ydist;

	  /* Only displace the pixel if it's within the bounds of
	   *  the image.
	   */
	  if ((xnew >= 0) && (xnew < width) && (ynew >= 0) && (ynew < height))
	    {
	      /* Displacing a pixel is accomplished by swapping it with
	       *  another pixel in its vicintity.
	       */

	      pixel = dest_image + ynew * dstrowstride + (xnew * channels);
	      for (i = 0; i < channels; i++)
		pixel[i] = src[i];

	      pixel = src_image + ynew * srcrowstride + (xnew * channels);
	      for (i = 0; i < channels; i++)
		dest[i] = pixel[i];
	    }
	  else
	    {
	      for (i = 0; i < channels; i++)
		dest[i] = src[i];
	    }

	  src += channels;
	  dest += channels;
	}

      src_row += srcrowstride;
      dest_row += dstrowstride;
    }
}
