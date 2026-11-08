
/* Despeckle 1.0 -- image filter plug-in for the GIMP image manipulation program
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

/* This plug-in uses the classic "De-speckle" algorithm, which I believe was
   first used by JPL and NASA to remove drop-outs from data sent back from the
   early planetary space probes.

   It simply goes along each pixel, taking the average of the surrounding 
   8-neighbor pixels. if the difference between the "current" pixel and that 
   average is greater that a certain tolerence, replace it with the average.

   (hasn't anybody heard of ANSI C?!!)
*/

/* This file has been modified from the original.  The abs() macro has been
 * removed in favor of that in stdlib.h.  <Scott Lindsey 5/28/98>
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "gimp.h"

#define Accuracy 10

/* Declare a local function.
 */
static void despeckle (Image, Image);

static void scale_callback (int, void *, void *);
static void toggle_callback (int, void *, void *);
static void ok_callback (int, void *, void *);
static void cancel_callback(int, void *, void *);
static void saveimage(void);
static void freshen(void);

static char *prog_name;

static long aapply;
static int aapply_ID;
static int dialog_ID;

static Image input, output;
static unsigned char *saved;

static long Tolerence = 8 * Accuracy;

int main (int argc, char **argv)
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

      /* If both an input and output image were available, then do some
       *  work. (Despeckle). Then update the output image.
       */
      if (input)
        switch (gimp_image_type (input))
          {
           case RGB_IMAGE:
           case GRAY_IMAGE:
             output = gimp_get_output_image(0);
             if (output)
               {
                saveimage();

                dialog_ID= gimp_new_dialog ("Despeckle");
                group_ID = gimp_new_row_group (dialog_ID,DEFAULT,NORMAL,"");

                aapply_ID = gimp_new_check_button(dialog_ID,group_ID,"Auto Apply");
                aapply = 1;
                gimp_change_item(dialog_ID,aapply_ID,sizeof(aapply),&aapply);

                scale_ID = gimp_new_scale (dialog_ID,group_ID, 0, 2560, Tolerence, 1);

                gimp_new_label(dialog_ID,scale_ID,"Tolerence:");
                gimp_add_callback(dialog_ID,scale_ID,scale_callback, &Tolerence);

                gimp_add_callback(dialog_ID,aapply_ID,toggle_callback,&aapply);
		gimp_add_callback(dialog_ID,gimp_ok_item_id (dialog_ID) , ok_callback, 0);
		gimp_add_callback(dialog_ID,gimp_cancel_item_id (dialog_ID) , cancel_callback, 0);

                despeckle(input,output);
                gimp_update_image(output);

                if (!gimp_show_dialog(dialog_ID))
                  {
                    if (aapply)
                      {
                        freshen();
                        gimp_update_image(output);
                      }
                   }
                else
                   if (!aapply)
                     {
                      despeckle(input,output);
                      gimp_update_image(output);
                     }

                free(saved);
               }
             break;
           case INDEXED_IMAGE:
	    gimp_message ("despeckle: cannot operate on indexed color images");
            break;
           default:
	    gimp_message ("despeckle: cannot operate on unkown image types");
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

static void saveimage(void)
{
  saved = (unsigned char *) malloc(gimp_image_width(input) * gimp_image_height(input) * gimp_image_channels(input));

  memcpy(saved, gimp_image_data(input), gimp_image_width(input)*gimp_image_height(input)*gimp_image_channels(input)); 
}

static void freshen(void)
{
  memcpy(gimp_image_data(output), saved, gimp_image_width(input) * gimp_image_height(input) * gimp_image_channels(input));
}

static void scale_callback (int item_ID, void *client_data, void *call_data)
{
 if (aapply && (*((long *) client_data) != *((long *) call_data)))
   {
     *((long *) client_data) = *((long *) call_data);

     despeckle(input,output);
     gimp_update_image(output);
   }

 *((long *) client_data) = *((long *) call_data);
}

static void ok_callback (int item_ID, void *client_date, void *call_data)
{
  gimp_close_dialog(dialog_ID,1);
}

static void cancel_callback (int item_ID, void *client_date, void *call_data)
{
  gimp_close_dialog(dialog_ID,0);
}

static void toggle_callback (int item_ID, void *client_data, void *call_data)
{
  *((long *) client_data) = *((long *) call_data);

  if (aapply)
    {
      despeckle(input,output);
      gimp_update_image(output);
    }
  else
    {
      freshen();
      gimp_update_image(output);
    }
}



static void despeckle (Image input, Image output)
{
  long width, height;
  long channels, rowstride;
  long val,avg;
  unsigned char *dest;
  unsigned char *prev_row;
  unsigned char *cur_row;
  unsigned char *next_row;
  short row, col;
  int x1, y1, x2, y2;
  int left, right;
  int top, bottom;
  long diff;

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
  
  if (left)
    x1++;
  if (right)
    x2--;
  if (top)
    y1++;
  if (bottom)
    y2--;

  x1 *= channels;
  x2 *= channels;

  prev_row = saved;
  prev_row += (y1 - 1) * rowstride;
  cur_row = prev_row + rowstride;
  next_row = cur_row + rowstride;

  dest = gimp_image_data (output);

  if (top)
    {
      if (left)
	for (col = 0; col < channels; col++)
	  {
            val = (long) cur_row[col];
	    avg = ((long) cur_row[col + channels] +
		   (long) next_row[col] + (long) next_row[col + channels]) / 3;
            diff = abs(avg - val) * Accuracy;

            if (diff > Tolerence)
	     *dest++ = avg;
	    else
	     *dest++ = val;

	  }
      
      for (col = x1; col < x2; col++)
	{
          val = (long) cur_row[col];
	  avg = ((long) cur_row[col - channels] +
		 (long) cur_row[col + channels] + (long) next_row[col - channels] +
		 (long) next_row[col] + (long) next_row[col + channels]) / 5;
          diff = abs(avg - val) * Accuracy;

          if (diff > Tolerence)
	   *dest++ = avg;
	  else
	   *dest++ = val;
	}

      if (right)
	for (col = col; col < rowstride; col++)
	  {
            val = (long) cur_row[col];
	    avg = ((long) cur_row[col - channels] +
		   (long) next_row[col] + (long) next_row[col - channels]) / 3;
            diff = abs(avg - val) * Accuracy;

            if (diff > Tolerence)
	     *dest++ = avg;
	    else
	     *dest++ = val;
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
              val = (long) cur_row[col];
	      avg = ((long) prev_row[col] + (long) prev_row[col + channels] +
		     (long) cur_row[col + channels] +
		     (long) next_row[col] + (long) next_row[col + channels]) / 5;
              diff = abs(avg - val) * Accuracy;

              if (diff > Tolerence)
	       *dest++ = avg;
	      else
	       *dest++ = val;
	    }
	}
      else
	{
	  dest += x1;
	}
      
      for (col = x1; col < x2; col++)
	{
          val = (long) cur_row[col];
	  avg = ((long) prev_row[col - channels] + (long) prev_row[col] + 
		 (long) prev_row[col + channels] + (long) cur_row[col - channels] +
		 (long) cur_row[col + channels] +
		 (long) next_row[col - channels] + (long) next_row[col] +
		 (long) next_row[col + channels]) / 8;
          diff = abs(avg - val) * Accuracy;

          if (diff > Tolerence)
	   *dest++ = avg;
	  else
	   *dest++ = val;
	}

      if (right)
	{
	  for (col = col; col < rowstride; col++)
	    {
              val = (long) cur_row[col];
	      avg = ((long) prev_row[col] + (long) prev_row[col - channels] +
		     (long) cur_row[col - channels] +
		     (long) next_row[col] + (long) next_row[col - channels]) / 5;
              diff = abs(avg - val) * Accuracy;

              if (diff > Tolerence)
	       *dest++ = avg;
	      else
	       *dest++ = val;
	    }
	}
      else
	{
	  dest += rowstride - x2;
	}

      prev_row = cur_row;
      cur_row = next_row;
      next_row += rowstride;
      
      if ((row % 5) == 0)
	gimp_do_progress (row, y2 - y1);
    }
  
  if (bottom)
    {
      if (left)
	for (col = 0; col < channels; col++)
	  {
            val = (long) cur_row[col];
	    avg = ((long) prev_row[col] + (long) prev_row[col + channels] +
		   (long) cur_row[col + channels]) / 3;
            diff = abs(avg - val) * Accuracy;

            if (diff > Tolerence)
	     *dest++ = avg;
	    else
	     *dest++ = val;
	  }
      
      for (col = x1; col < x2; col++)
	{
          val = (long) cur_row[col];
	  avg = ((long) prev_row[col - channels] + (long) prev_row[col] +
		 (long) prev_row[col + channels] + (long) cur_row[col - channels] +
		 (long) cur_row[col + channels]) / 5;
          diff = abs(avg - val) * Accuracy;

          if (diff > Tolerence)
	   *dest++ = avg;
	  else
	   *dest++ = val;
	}

      if (right)
	for (col = col; col < rowstride; col++)
	  {
            val = (long) cur_row[col];
	    avg = ((long) prev_row[col] + (long) prev_row[col - channels] +
		   (long) cur_row[col - channels]) / 3;
            diff = abs(avg - val) * Accuracy;

            if (diff > Tolerence)
	     *dest++ = avg;
	    else
	     *dest++ = val;
	  }
    }

}
