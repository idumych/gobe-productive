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
 * This filter displaces each row of images pixels by a random amount. (left or right)
 *  The idea for this filter is based on ppmshift, a program
 *  that is part of the netpbm package.
 */

/*
 * This file has been modified from the original.  A dialog label was added.
 * <Scott Lindsey 7/21/98>
 */


#include <stdlib.h>
#include <time.h>
#include "gimp.h"

/* Declare a local function.
 */
static void scale_callback (int, void *, void *);
static void ok_callback (int, void *, void *);
static void cancel_callback (int, void *, void *);
static void shift (Image, Image);

static char *prog_name;

/*  amount of shift  */
long amount = 5;
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
	      amount = *((long*) data);
	    
	    dialog_ID = gimp_new_dialog ("Shift");
	    scale_ID = gimp_new_scale (dialog_ID, DEFAULT, 1, gimp_image_width (input), amount, 0);
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
		    shift (input, output);
		    gimp_update_image (output);
		  }
	      }
	    break;
	  case INDEXED_IMAGE:
	    gimp_message ("shift: cannot operate on indexed color images");
	    break;
	  default:
	    gimp_message ("shift: cannot operate on unknown image types");
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
  *((long*) client_data) = *((long*) call_data);
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

static void
shift (input, output)
     Image input, output;
{
  long width, height;
  long channels, rowstride;
  unsigned char *src_row, *dest_row;
  unsigned char *src, *dest;
  short row, col;
  int x1, y1, x2, y2;
  int mod_value, sub_value;
  int shift, i;
  
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
  rowstride = width * channels;

  src_row = gimp_image_data (input);
  dest_row = gimp_image_data (output);
  
  /* Advance the source and destination pointers
   */
  src_row += rowstride * y1 + (x1 * channels);
  dest_row += rowstride * y1 + (x1 * channels);

  srand (time (NULL));
  mod_value = amount + 1;
  sub_value = amount / 2;

  for (row = y1; row < y2; row++)
    {
      src     = src_row;
      dest    = dest_row;
      
      shift = (rand () % mod_value) - sub_value;

      if (shift < 0)
	{
	  shift = -shift;
	  src += shift * channels;

	  if ((x2 - shift) > width)
	    {
	      for (col = x1; col < x2 - shift; col++)
		for (i = 0; i < channels; i++)
		  *dest++ = *src++;

	      src -= channels;
	      for ( ; col < x2; col++)
		for (i = 0; i < channels; i++)
		  *dest++ = src[i];
	    }
	  else
	    {
	      for (col = x1; col < x2; col++)
		for (i = 0; i < channels; i++)
		  *dest++ = *src++;
	    }
	}
      else
	{
	  for (col = x1; col < x1 + shift; col++)
	    for (i = 0; i < channels; i++)
	      *dest++ = src[i];
	  
	  for ( ; col < x2; col++)
	    for (i = 0; i < channels; i++)
	      *dest++ = *src++;
	}
      
      src_row += rowstride;
      dest_row += rowstride;
    }
}
