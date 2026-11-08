
/* Median 1.0 -- image filter plug-in for the GIMP image manipulation program
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

/* This plug-in goes along each pixel, taking the median of the surrounding 
   8-neighbor pixels and the "current" pixel.

   if anyone has a faster method to find the median, please email me,
   my collection of computer books is not big by any means, and I had to
   figure it for myself.
*/

#include "gimp.h"

#define cmp(a,b) ((a) < (b) ? -1 : ((a) > (b) ? 1 : 0))

/* Declare a local function.
 */
static void median (Image, Image);

static char *prog_name;

int
main (int argc, char **argv)
{
  Image input, output;

  /* Save the program name so we can use it later in reporting errors
   */
  prog_name = argv[0];

  /* Call 'gimp_init' to initialize this filter.
   * 'gimp_init' makes sure that the filter was properly called and
   *  it opens pipes for reading and writing.
   */
  if (gimp_init (argc, argv))
    {
      /* This is a regular filter. What that means is that it operates
       *  on the input image. Output is put into the ouput image. The
       *  filter should not worry, or even care where these images come
       *  from. The only guarantee is that they are the same size and
       *  depth.
       */
      input = gimp_get_input_image (0);
      output = gimp_get_output_image (0);

      /* If both an input and output image were available, then do some
       *  work. (Blur). Then update the output image.
       */
      if (input && output)
	{
	  if ((gimp_image_type (input) == RGB_IMAGE) ||
	      (gimp_image_type (input) == GRAY_IMAGE))
	    {
	      gimp_init_progress ("Median");
	      median (input, output);
	      gimp_update_image (output);
	    }
	  else
	    gimp_message ("median: cannot operate on indexed color images");
	}
      
      /* Free both images.
       */
      gimp_free_image (input);
      gimp_free_image (output);

      /* Quit
       */
      gimp_quit ();
    }

  return 0;
}

static long median4(long a, long b, long c, long d)
{
 long val[4],cval[4];
 long cpos,pos;
 long cneg,neg;
 long cnt,c2;

 val[0] = a;
 val[1] = b;
 val[2] = c;
 val[3] = d;

 cpos = 10;
 pos = 0;

 cneg = -10;
 neg = 0;

 for (cnt = 0; cnt < 4; cnt++)
 {
  cval[cnt] = 0;

  for (c2 = 0; c2 < 4; c2++)
   cval[cnt] += cmp(val[cnt],val[c2]);

  if (cval[cnt] == 0)
   return val[cnt];

  if ((cval[cnt] > 0) && (cval[cnt] < cpos))
   {
    cpos = cval[cnt];
    pos = val[cnt];
   }

  if ((cval[cnt] < 0) && (cval[cnt] > cneg))
   {
    cneg = cval[cnt];
    neg = val[cnt];
   }
 }

 return ((pos + neg) / 2);
}


static long median6(long a, long b, long c, long d, long e, long f)
{
 long val[6],cval[6];
 long cpos,pos;
 long cneg,neg;
 long cnt,c2;

 val[0] = a;
 val[1] = b;
 val[2] = c;
 val[3] = d;
 val[4] = e;
 val[5] = f;

 cpos = 10;
 pos = 0;

 cneg = -10;
 neg = 0;

 for (cnt = 0; cnt < 6; cnt++)
 {
  cval[cnt] = 0;

  for (c2 = 0; c2 < 6; c2++)
   cval[cnt] += cmp(val[cnt],val[c2]);

  if (cval[cnt] == 0)
   return val[cnt];

  if ((cval[cnt] > 0) && (cval[cnt] < cpos))
   {
    cpos = cval[cnt];
    pos = val[cnt];
   }

  if ((cval[cnt] < 0) && (cval[cnt] > cneg))
   {
    cneg = cval[cnt];
    neg = val[cnt];
   }
 }

 return ((pos + neg) / 2);
}


static long median9(long a, long b, long c, long d, long e, long f, long g, long h, long i)
{
 long val[9],cval[9];
 long cpos,pos;
 long cneg,neg;
 long cnt,c2;

 val[0] = a;
 val[1] = b;
 val[2] = c;
 val[3] = d;
 val[4] = e;
 val[5] = f;
 val[6] = g;
 val[7] = h;
 val[8] = i;

 cpos = 10;
 pos = 0;

 cneg = -10;
 neg = 0;

 for (cnt = 0; cnt < 9; cnt++)
 {
  cval[cnt] = 0;

  for (c2 = 0; c2 < 9; c2++)
   cval[cnt] += cmp(val[cnt],val[c2]);

  if (cval[cnt] == 0)
   return val[cnt];

  if ((cval[cnt] > 0) && (cval[cnt] < cpos))
   {
    cpos = cval[cnt];
    pos = val[cnt];
   }

  if ((cval[cnt] < 0) && (cval[cnt] > cneg))
   {
    cneg = cval[cnt];
    neg = val[cnt];
   }
 }

 return ((pos + neg) / 2);

}


static void median (Image input, Image output)
{
  long width, height;
  long channels, rowstride;
  long a,b,c,d,e,f,g,h,i;
  unsigned char *dest;
  unsigned char *prev_row;
  unsigned char *cur_row;
  unsigned char *next_row;
  short row, col;
  int x1, y1, x2, y2;
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

  prev_row = gimp_image_data (input);
  prev_row += (y1 - 1) * rowstride;
  cur_row = prev_row + rowstride;
  next_row = cur_row + rowstride;

  dest = gimp_image_data (output);

  if (top)
    {
      if (left)
	for (col = 0; col < channels; col++)
	  {
	    a = (long) cur_row[col];
	    b = (long) cur_row[col + channels];
	    c = (long) next_row[col];
            d = (long) next_row[col + channels];
	    *dest++ = median4(a,b,c,d);
	  }

      for (col = x1; col < x2; col++)
	{
	  a = (long) cur_row[col - channels];
          b = (long) cur_row[col];
	  c = (long) cur_row[col + channels];
	  d = (long) next_row[col - channels];
	  e = (long) next_row[col];
	  f = (long) next_row[col + channels];
	  *dest++ = median6(a,b,c,d,e,f);
	}

      if (right)
	for (col = col; col < rowstride; col++)
	  {
	    a = (long) cur_row[col];
	    b = (long) cur_row[col - channels];
	    c = (long) next_row[col];
	    d = (long) next_row[col - channels];
	    *dest++ = median4(a,b,c,d);
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
	      a = (long) prev_row[col];
	      b = (long) prev_row[col + channels];
	      c = (long) cur_row[col];
	      d = (long) cur_row[col + channels];
	      e = (long) next_row[col];
	      f = (long) next_row[col + channels];
	      *dest++ = median6(a,b,c,d,e,f);
	    }
	}
      else
	{
	  dest += x1;
	}
      
      for (col = x1; col < x2; col++)
	{
	  a = (long) prev_row[col - channels];
	  b = (long) prev_row[col];
	  c = (long) prev_row[col + channels];
	  d = (long) cur_row[col - channels];
	  e = (long) cur_row[col];
	  f = (long) cur_row[col + channels];
	  g = (long) next_row[col - channels];
	  h = (long) next_row[col];
	  i = (long) next_row[col + channels];
	  *dest++ = median9(a,b,c,d,e,f,g,h,i);
	}

      if (right)
	{
	  for (col = col; col < rowstride; col++)
	    {
	      a = (long) prev_row[col];
	      b = (long) prev_row[col - channels];
	      c = (long) cur_row[col];
	      d = (long) cur_row[col - channels];
	      e = (long) next_row[col];
	      f = (long) next_row[col - channels];
	      *dest++ = median6(a,b,c,d,e,f);
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
	    a = (long) prev_row[col];
	    b = (long) prev_row[col + channels];
	    c = (long) cur_row[col];
	    d = (long) cur_row[col + channels];
	    *dest++ = median4(a,b,c,d);
	  }
      
      for (col = x1; col < x2; col++)
	{
	  a = (long) prev_row[col - channels];
	  b = (long) prev_row[col];
	  c = (long) prev_row[col + channels];
	  d = (long) cur_row[col - channels];
	  e = (long) cur_row[col];
	  f = (long) cur_row[col + channels];
	  *dest++ = median6(a,b,c,d,e,f);
	}

      if (right)
	for (col = col; col < rowstride; col++)
	  {
	    a = (long) prev_row[col];
	    b = (long) prev_row[col - channels];
	    c = (long) cur_row[col];
	    d = (long) cur_row[col - channels];
	    *dest++ = median4(a,b,c,d);
	  }
    }
}
