/* enhance filter for the Gimp
 *  -Peter Mattis
 *
 * This filter enhances or sharpens the input image
 *  The code for this filter is based on "pgmenhance", a program
 *  that is part of the netpbm package.
 */

/* pgmenhance.c - edge-enhance a portable graymap
**
** Copyright (C) 1989, 1991 by Jef Poskanzer.
**
** Permission to use, copy, modify, and distribute this software and its
** documentation for any purpose and without fee is hereby granted, provided
** that the above copyright notice appear in all copies and that both that
** copyright notice and this permission notice appear in supporting
** documentation.  This software is provided "as is" without express or
** implied warranty.
*/

/*
 * This file has been modified from the original.  A dialog label was added.
 * <Scott Lindsey 7/21/98>
 */


#include "gimp.h"

/* Declare a local function.
 */
static void scale_callback (int, void *, void *);
static void ok_callback (int, void *, void *);
static void cancel_callback (int, void *, void *);
static void enhance (Image, Image);

static char *prog_name;
static long amount = 9;
static int dialog_ID;

int
main (argc, argv)
     int argc;
     char *argv[];
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

      /* If input image is available and the input is color, then do some
       *  work. (Enhance). Then update the output image.
       */
      if (input)
	switch (gimp_image_type (input))
	  {
	  case RGB_IMAGE:
	  case GRAY_IMAGE:
	    data = gimp_get_params ();
	    if (data)
	      amount = *((long*) data);
	    
	    dialog_ID = gimp_new_dialog ("Enhance");
	    scale_ID = gimp_new_scale (dialog_ID, DEFAULT, 1, 9, amount, 0);
		gimp_new_label(dialog_ID, scale_ID, "Amount:");
	    gimp_add_callback (dialog_ID, scale_ID, scale_callback, &amount);
	    gimp_add_callback (dialog_ID, gimp_ok_item_id (dialog_ID), ok_callback, 0);
	    gimp_add_callback (dialog_ID, gimp_cancel_item_id (dialog_ID), cancel_callback, 0);
	    
	    if (gimp_show_dialog (dialog_ID))
	      {
		gimp_set_params (sizeof (amount), &amount);
		
		output = gimp_get_output_image (0);
		if (output)
		  {
		    enhance (input, output);
		    gimp_update_image (output);
		  }
	      }
	    break;
	  case INDEXED_IMAGE:
	    gimp_message ("enhance: cannot operate on indexed color images");
	    break;
	  default:
	    gimp_message ("enhance: cannot operate on unknown image types");
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
enhance (input, output)
     Image input, output;
{
  long width, height;
  long channels, rowstride;
  unsigned char *prev_row;
  unsigned char *cur_row;
  unsigned char *next_row;
  unsigned char *dest;
  long sum, newval;
  short row, col;
  int x1, y1, x2, y2;
  float phi, omphi;
  int left, right;
  int top, bottom;
  
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

  /* To do the blurring quickly, we'll keep track of the current row,
   *  the previous row, and the next row. These pointers allow us to
   *  deal with edge cases without too much difficulty.
   */

  /* Multiply the x bounds by the number of channels.
   */
  x1 *= channels;
  x2 *= channels;

  phi = amount / 10.0;
  omphi = 1.0 - phi;
  
  prev_row = gimp_image_data (input);
  prev_row += (y1 - 1) * rowstride;
  cur_row = prev_row + rowstride;
  next_row = cur_row + rowstride;

  dest = gimp_image_data (output);

  if (top)
    {
      if (left)
	{
	  for (col = 0; col < channels; col++)
	    {
	      sum = ((long) cur_row[col] +
		     (long) cur_row[col] +
		     (long) cur_row[col + channels] +
		     (long) cur_row[col] +
		     (long) cur_row[col] +
		     (long) cur_row[col + channels] +
		     (long) next_row[col] +
		     (long) next_row[col] +
		     (long) next_row[col + channels]);
	      
	      newval = ((cur_row[col] - phi * sum / 9) / omphi + 0.5);
	      if (newval < 0)
		*dest++ = 0;
	      else if (newval > 255)
		*dest++ = 255;
	      else
		*dest++ = newval;
	    }
	}
      else
	{
	  dest += x1;
	}

      for (col = x1; col < x2; col++)
        {
	  sum = ((long) cur_row[col - channels] +
		 (long) cur_row[col] +
		 (long) cur_row[col + channels] +
		 (long) cur_row[col - channels] +
		 (long) cur_row[col] +
		 (long) cur_row[col + channels] +
		 (long) next_row[col - channels] +
		 (long) next_row[col] +
		 (long) next_row[col + channels]);

	  newval = ((cur_row[col] - phi * sum / 9) / omphi + 0.5);
	  if (newval < 0)
	    *dest++ = 0;
	  else if (newval > 255)
	    *dest++ = 255;
	  else
	    *dest++ = newval;
        }

      if (right)
	{
	  for (col = col; col < rowstride; col++)
	    {
	      sum = ((long) cur_row[col - channels] +
		     (long) cur_row[col] +
		     (long) cur_row[col] +
		     (long) cur_row[col - channels] +
		     (long) cur_row[col] +
		     (long) cur_row[col] +
		     (long) next_row[col - channels] +
		     (long) next_row[col] +
		     (long) next_row[col]);
	      
	      newval = ((cur_row[col] - phi * sum / 9) / omphi + 0.5);
	      if (newval < 0)
		*dest++ = 0;
	      else if (newval > 255)
		*dest++ = 255;
	      else
		*dest++ = newval;
	    }
	}
      else
	{
	  dest += rowstride - x2;
	}      
    }
  else
    {
      dest += y1 * rowstride;
    }

  for (row = y1; row < y2; row++)
    {
      if (left)
	{
	  for (col = 0; col < channels; col++)
	    {
	      sum = ((long) prev_row[col] +
		     (long) prev_row[col] +
		     (long) prev_row[col + channels] +
		     (long) cur_row[col] +
		     (long) cur_row[col] +
		     (long) cur_row[col + channels] +
		     (long) next_row[col] +
		     (long) next_row[col] +
		     (long) next_row[col + channels]);
	      
	      newval = ((cur_row[col] - phi * sum / 9) / omphi + 0.5);
	      if (newval < 0)
		*dest++ = 0;
	      else if (newval > 255)
		*dest++ = 255;
	      else
		*dest++ = newval;
	    }
	}
      else
	{
	  dest += x1;
	}

      for (col = x1; col < x2; col++)
        {
	  sum = ((long) prev_row[col - channels] +
		 (long) prev_row[col] +
		 (long) prev_row[col + channels] +
		 (long) cur_row[col - channels] +
		 (long) cur_row[col] +
		 (long) cur_row[col + channels] +
		 (long) next_row[col - channels] +
		 (long) next_row[col] +
		 (long) next_row[col + channels]);

	  newval = ((cur_row[col] - phi * sum / 9) / omphi + 0.5);
	  if (newval < 0)
	    *dest++ = 0;
	  else if (newval > 255)
	    *dest++ = 255;
	  else
	    *dest++ = newval;
        }

      if (right)
	{
	  for (col = col; col < rowstride; col++)
	    {
	      sum = ((long) prev_row[col - channels] +
		     (long) prev_row[col] +
		     (long) prev_row[col] +
		     (long) cur_row[col - channels] +
		     (long) cur_row[col] +
		     (long) cur_row[col] +
		     (long) next_row[col - channels] +
		     (long) next_row[col] +
		     (long) next_row[col]);
	      
	      newval = ((cur_row[col] - phi * sum / 9) / omphi + 0.5);
	      if (newval < 0)
		*dest++ = 0;
	      else if (newval > 255)
		*dest++ = 255;
	      else
		*dest++ = newval;
	    }
	}
      else
	{
	  dest += rowstride - x2;
	}

      prev_row = cur_row;
      cur_row = next_row;
      next_row += rowstride;
    }

  if (bottom)
    {
      if (left)
	{
	  for (col = 0; col < channels; col++)
	    {
	      sum = ((long) prev_row[col] +
		     (long) prev_row[col] +
		     (long) prev_row[col + channels] +
		     (long) cur_row[col] +
		     (long) cur_row[col] +
		     (long) cur_row[col + channels] +
		     (long) cur_row[col] +
		     (long) cur_row[col] +
		     (long) cur_row[col + channels]);
	      
	      newval = ((cur_row[col] - phi * sum / 9) / omphi + 0.5);
	      if (newval < 0)
		*dest++ = 0;
	      else if (newval > 255)
		*dest++ = 255;
	      else
		*dest++ = newval;
	    }
	}
      else
	{
	  dest += x1;
	}

      for (col = x1; col < x2; col++)
        {
	  sum = ((long) prev_row[col - channels] +
		 (long) prev_row[col] +
		 (long) prev_row[col + channels] +
		 (long) cur_row[col - channels] +
		 (long) cur_row[col] +
		 (long) cur_row[col + channels] +
		 (long) cur_row[col - channels] +
		 (long) cur_row[col] +
		 (long) cur_row[col + channels]);

	  newval = ((cur_row[col] - phi * sum / 9) / omphi + 0.5);
	  if (newval < 0)
	    *dest++ = 0;
	  else if (newval > 255)
	    *dest++ = 255;
	  else
	    *dest++ = newval;
        }

      if (right)
	{
	  for (col = col; col < rowstride; col++)
	    {
	      sum = ((long) prev_row[col - channels] +
		     (long) prev_row[col] +
		     (long) prev_row[col] +
		     (long) cur_row[col - channels] +
		     (long) cur_row[col] +
		     (long) cur_row[col] +
		     (long) cur_row[col - channels] +
		     (long) cur_row[col] +
		     (long) cur_row[col]);
	      
	      newval = ((cur_row[col] - phi * sum / 9) / omphi + 0.5);
	      if (newval < 0)
		*dest++ = 0;
	      else if (newval > 255)
		*dest++ = 255;
	      else
		*dest++ = newval;
	    }
	}
      else
	{
	  dest += rowstride - x2;
	}      
    }
}
