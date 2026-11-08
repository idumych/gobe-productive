/* edge filter for the GIMP
 *  -Peter Mattis
 *
 * This filter performs edge detection on the input image.
 *  The code for this filter is based on "pgmedge", a program
 *  that is part of the netpbm package.
 */

/* pgmedge.c - edge-detect a portable graymap
**
** Copyright (C) 1989 by Jef Poskanzer.
**
** Permission to use, copy, modify, and distribute this software and its
** documentation for any purpose and without fee is hereby granted, provided
** that the above copyright notice appear in all copies and that both that
** copyright notice and this permission notice appear in supporting
** documentation.  This software is provided "as is" without express or
** implied warranty.
*/

#include <math.h>
#include "GIMPGlue.h"
#include "gimp.h"
#include "ImageManipAddon.h"
#include "BitmapAccessor.h"
#include "Config.h"

const char addonName[] = "Edge";
const char addonInfo[] = "Edge detection. Ported from the GIMP by Scott Lindsey <wombat@gobe.com>.  Original code by Jef Poskanzer";
const char addonCategory[] = "Miscellaneous";
int32 addonVersion = 100;

/* Declare local functions.
 */
static void edge (Image, Image, int32, int);
static int32 long_sqrt (int32);

status_t
Manipulate(
	BitmapAccessor *sourceBitmap,
	BMessage *ioExtension,
	bool checkOnly)
{
	int alpha;
	Prefs prefs;
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

	LoadPrefs(&prefs);

	gimp_init_glue(sourceBitmap, NULL, ioExtension);

	if (ioExtension)
	{
		int32 amount;
		if (!ioExtension->FindInt32("edge/amount", &amount))
			prefs.amount = amount;
	}

	Image input = new GIMPImage(sourceBitmap);
	
    Image temp = gimp_temp_image();
	edge(input, temp, prefs.amount, alpha);
	input->Copy(temp);

    gimp_free_image(temp);
    delete input;
	
	return B_OK;
}


static void edge (Image input, Image output, int32 amount, int alpha_offset)
{
  unsigned char *dest, *p;
  unsigned char *prev_row, *cur_row, *next_row;
  int32 width, height;
  int32 channels, srcrowstride, dstrowstride;
  int row, col;
  int x1, y1, x2, y2;
  int left, right;
  int top, bottom;
  int32 sum1, sum2;
  int32 sum, scale;
  int maxval;
  int cur_progress;
  int max_progress;

  gimp_image_area (input, &x1, &y1, &x2, &y2);

  width = gimp_image_width (input);
  height = gimp_image_height (input);
  channels = gimp_image_channels (input);

  dest = p = gimp_image_data (output);
  prev_row = gimp_image_data (input);
  
  srcrowstride = gimp_image_stride(input);
  dstrowstride = gimp_image_stride(output);

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

  prev_row += (y1 - 1) * srcrowstride;
  cur_row = prev_row + srcrowstride;
  next_row = cur_row + srcrowstride;


  maxval = 255;
  scale = (10 << 16) / amount;

  if (top)
    {
      for (col = 0; col < dstrowstride; col++)
		if ((col & 3) != alpha_offset)
			*dest++ = 0;
		else
			dest++;
    }

  cur_progress = 0;
  max_progress = y2 - y1;

  /* Now the rest of the image - read in the 3rd row of inputbuf,
  ** and convolve with the first row into the output buffer.
  */
  for (row = y1; row < y2; ++row)
    {
		dest = p + row * dstrowstride;
      if (left)
        {
          for (col = 0; col < channels; col++)
			if ((col & 3) != alpha_offset)
		    	*dest++ = 0;
			else
				dest++;
        }

      for (col = x1; col < x2; col++)
        {
	  if ((col & 3) != alpha_offset)
	  {
		  sum1 = ((int32) prev_row[col + channels] - (int32) prev_row[col - channels] +
			  2 * ((int32) cur_row[col + channels] - (int32) cur_row[col - channels]) +
			  (int32) next_row[col + channels] - (int32) cur_row[col - channels]);
		  sum2 = (((int32) next_row[col - channels] + 
			   2 * (int32) next_row[col] +
			   (int32) next_row[col + channels]) -
			  ((int32) prev_row[col - channels] +
			   2 * (int32) prev_row[col] +
			   (int32) prev_row[col + channels]));
		  sum = long_sqrt (sum1 * sum1 + sum2 * sum2);
		  sum = (sum * scale) >> 16;    /* arbitrary scaling factor */
		  if (sum > maxval) sum = maxval;
		  *dest++ = sum;
		}
		else
			dest++;
        }
     
      if (right)
        {
          for (col = col; col < dstrowstride; col++)
			if ((col & 3) != alpha_offset)
		    	*dest++ = 0;
			else
				dest++;
        }

      prev_row = cur_row;
      cur_row = next_row;
      next_row += srcrowstride;

      gimp_do_progress (++cur_progress, max_progress);
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

static int32 long_sqrt (int32 n)
{
#define lsqrt_max4pow (1UL << 30)
  /* lsqrt_max4pow is the (machine-specific) largest power of 4 that can
   * be represented in an uint32.
   *
   * Compute the integer square root of the integer argument n
   * Method is to divide n by x computing the quotient x and remainder r
   * Notice that the divisor x is changing as the quotient x changes
   * 
   * Instead of shifting the dividend/remainder left, we shift the
   * quotient/divisor right. The binary point starts at the extreme
   * left, and shifts two bits at a time to the extreme right.
   * 
   * The residue contains n-x^2. (Within these comments, the ^ operator
   * signifies exponentiation rather than exclusive or. Also, the /
   * operator returns fractions, rather than truncating, so 1/4 means
   * one fourth, not zero.)
   * 
   * Since (x + 1/2)^2 == x^2 + x + 1/4,
   * n - (x + 1/2)^2 == (n - x^2) - (x + 1/4)
   * Thus, we can increase x by 1/2 if we decrease (n-x^2) by (x+1/4)
   */

  uint32 residue;        /* n - x^2  */
  uint32 root;           /* x + 1/4  */
  uint32 half;           /* 1/2      */

  residue = n;                  /* n - (x = 0)^2, with suitable alignment */

  /*
   * if the correct answer fits in two bits, pull it out of a magic hat
   */
  if (residue <= 12)
    return (0x03FFEA94 >> (residue *= 2)) & 3;

  root = lsqrt_max4pow;         /* x + 1/4, shifted all the way left */
  /* half = root + root; 1/2, shifted likewise */

  /* 
   * Unwind iterations corresponding to leading zero bits 
   */
  while (root > residue)
    root >>= 2;

  /*
   * Unwind the iteration corresponding to the first one bit
   * Operations have been rearranged and combined for efficiency
   * Initialization of half is folded into this iteration
   */
  residue -= root;              /* Decrease (n-x^2) by (0+1/4)             */
  half = root >> 2;             /* 1/4, with binary point shifted right 2  */
  root += half;                 /* x=1. (root is now (x=1)+1/4.)           */
  half += half;                 /* 1/2, properly aligned                   */

  /*
   * Normal loop (there is at least one iteration remaining)
   */
  do
    {
      if (root <= residue)      /* Whenever we can,                          */
        {
          residue -= root;      /* decrease (n-x^2) by (x+1/4)               */
          root += half;         /* increase x by 1/2                         */
        }
      half >>= 2;               /* Shift binary point 2 places right          */
      root -= half;             /* x{ +1/2 } +1/4 - 1/8 == x { +1/2 } 1/8     */
      root >>= 1;               /* 2x{ +1 } +1/4, shifted right 2 places      */
    }
  while (half);                 /* When 1/2 == 0, bin. point is at far right  */

  /* 
   * round up if (x+1/2)^2 < n
   */
  if (root < residue)
    ++root;

  /* 
   * Guaranteed to be correctly rounded (or truncated)
   */
  return root;
}
