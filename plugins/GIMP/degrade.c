/* Degrade -- image filter plug-in for The Gimp image manipulation program
 * Copyright (C) 1996 Adam D. Moss  -  aspirin@tigerden.com
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
 * This filter allows fine reduction of the colour resolution of an image
 *
 * NOTE: This is written for The GIMP version 0.54 - it might not
 *   compile or run on other versions.
 */

/*
 * This file has been modified from the original.  A dialog label was added.
 * <Scott Lindsey 7/21/98>
 */


#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "gimp.h"

/* Declare a local function.
 */
static void degrade (Image, Image, long);

static void scale_callback (int, void *, void *);
static void toggle_callback (int, void *, void *);
static void ok_callback (int, void *, void *);
static void cancel_callback (int, void *, void *);
static void saveimage(void);
static void freshen(void);

static char *prog_name;

static long aapply;
static int aapply_ID;
static int dialog_ID;

static long amount = 0;

static Image input, output;
static unsigned char *saved;

int
main (argc, argv)
     int argc;
     char **argv;
{
  int group_ID;
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
      input = 0;
      output = 0;
      
      /* This is a regular filter. What that means is that it operates
       *  on the input image. Output is put into the ouput image. The
       *  filter should not worry, or even care where these images come
       *  from. The only guarantee is that they are the same size and
       *  depth.
       */
      input = gimp_get_input_image (0);

      if (1)
	{
	  /* If input image is available, then do some work. (Degrade).
	   *  Then update the output image.
	   */
	  if (input)
	    switch (gimp_image_type (input))
	      {
	      case RGB_IMAGE:
	      case GRAY_IMAGE:
		output = gimp_get_output_image (0);
		if (output)
		  {
		    saveimage();

		    dialog_ID = gimp_new_dialog ("Degrade Color Depth");
		    group_ID = gimp_new_row_group (dialog_ID, DEFAULT, NORMAL, "");

		    aapply_ID = gimp_new_check_button (dialog_ID, group_ID, "Auto Apply");
		    aapply=1;
		    gimp_change_item (dialog_ID, aapply_ID, sizeof (aapply), &aapply);
		    scale_ID = gimp_new_scale (dialog_ID, group_ID, 2, 256, 256, 0);
			gimp_new_label(dialog_ID, scale_ID, "Amount:");  
		    gimp_add_callback (dialog_ID, scale_ID, scale_callback, &amount);
		    gimp_add_callback (dialog_ID, aapply_ID, toggle_callback, &aapply);
		    gimp_add_callback (dialog_ID, gimp_ok_item_id (dialog_ID), ok_callback, 0);
		    gimp_add_callback (dialog_ID, gimp_cancel_item_id (dialog_ID), cancel_callback, 0);
		    
		    if (!gimp_show_dialog (dialog_ID))
		      {
			if ((amount!=0)&&(aapply))
			  {
			    freshen();
			    gimp_update_image (output);
			  }
		      }
		    else
		      {
			if ((amount!=0)&&(!aapply))
			  {
			    degrade(input,output,amount);
			    gimp_update_image (output);			
			  }
		      }

		    free(saved);
		  }
		break;
	      case INDEXED_IMAGE:
		gimp_message ("degrade: cannot operate on indexed color images");
		break;
	      default:
		gimp_message ("degrade: cannot operate on unknown image types");
		break;
	      }
	  
	  /* Free both images.
	   */
	  if (input)
	    gimp_free_image (input);
	  if (output)
	    gimp_free_image (output);
	}

	  /* Quit
	   */
      gimp_quit ();
    }

  return 0;
}

/* save a copy of the original image to work on.... */
static void saveimage(void)
{
  saved = (unsigned char *) malloc(gimp_image_width(input) * gimp_image_height(input) * gimp_image_channels(input));
  memcpy(saved, gimp_image_data(input), gimp_image_width(input)*gimp_image_height(input)*gimp_image_channels(input));
}

/* copy the original image to the working buffer... */
static void freshen(void)
{
  memcpy(gimp_image_data(output), saved, gimp_image_width(input)*gimp_image_height(input)*gimp_image_channels(input));
}

static void
scale_callback (item_ID, client_data, call_data)
     int item_ID;
     void *client_data;
     void *call_data;
{
  *((long*) client_data) = *((long*) call_data);
  
  if (aapply)
    {
	degrade (input, output, amount);
	gimp_update_image (output);
    }
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
toggle_callback (item_ID, client_data, call_data)
     int item_ID;
     void *client_data;
     void *call_data;
{
  *((long*) client_data) = *((long*) call_data);

  if (aapply)
    {
      degrade (input, output, amount);
      gimp_update_image (output);
    }
  else
    {
      freshen();
      gimp_update_image (output);
    }
}

static void
degrade (linput, loutput, howmuch)
     Image linput, loutput;
     long howmuch;
{
  long width, height;
  long channels, rowstride;
  unsigned char *src_row, *dest_row;
  unsigned char *src, *dest;
  short row, col;
  int x1, y1, x2, y2;
  int tmp;
  unsigned char table[256];

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

  src_row = saved;
  dest_row = gimp_image_data (output);

  x1 *= channels;
  x2 *= channels;

  /* Advance the source and destination pointers
   */
  src_row += rowstride * y1 + x1;
  dest_row += rowstride * y1 + x1;

  /* old versions of the lookup generation... all of these have
   terrible rounding errors or simply don't work... */

  /*for (tmp=0;tmp<256;tmp++) table[(unsigned char)tmp] = (unsigned char) ((tmp/amount)*amount);*/
/*  for (tmp=0;tmp<256;tmp++) table[(unsigned char)tmp] = (unsigned char) (((tmp*2)/(amount*2))*(amount/2));*/
/*  for (tmp=0;tmp<256;tmp++) table[(unsigned char)tmp] =
    (unsigned char) rint((double)tmp/((double)amount)) *amount;*/
/*  for (tmp=1;tmp<256;tmp++) table[(unsigned char)tmp] =
    ((unsigned char) (double)rint((double)tmp/(double)amount))*(unsigned char)amount;*/
/*  for (tmp=0;tmp<256;tmp++) table[(unsigned char)tmp] =
    (unsigned char)((double)rint((double)tmp/(double)amount)*(double)amount);*/
/*for (tmp=0;tmp<256;tmp++) table[(unsigned char)tmp] =
    ((tmp/amount)*255)/(255/amount);*/
/*  for (tmp=0;tmp<256;tmp++) table[(unsigned char)tmp] =
    (( (tmp) *(amount-1)/255)*255) / (amount-1);*/

  /* the good one! */
  
  for (tmp=0;tmp<256;tmp++) table[(unsigned char)tmp] =
    (( (tmp) *(howmuch)/256)*255) / (howmuch-1);

  if (howmuch!=256)
    for (row = y1; row < y2; row++)
      {
	src     = src_row;
	dest    = dest_row;
	
	/*  Calculate across the scanline  */
	for (col = x1; col < x2; col++)
	  {
	    *dest++ = (unsigned char)(table[(unsigned char)(*src++)]);
	  }
	
	src_row += rowstride;
	dest_row += rowstride;
      }
  else
    {
      freshen();
    }
}



