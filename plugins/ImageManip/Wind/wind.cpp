/*
 * wind - a plug-in for the GIMP
 *
 * Copyright (C) Nigel Wetten
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Contact info: nigel@cs.nwu.edu
 *
 * Version: 1.0.0
 *
 * THIS FILE HAS BEEN MODIFIED FROM THE ORIGINAL
 * - Scott Lindsey 7/23/99
 */

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <malloc.h>

#include "ImageManipAddon.h"
#include "BitmapAccessor.h"
#include "Config.h"
#include "GIMPGlue.h"

#define PLUG_IN_NAME "wind"

#define COMPARE_WIDTH 3


static status_t render_wind(BitmapAccessor* sourceBitmap, BRect sel_rect, BInvoker* prog, int32 threshold, int32 strength,
						direction_t direction, edge_t edge);
static status_t render_blast(BitmapAccessor* sourceBitmap, BRect sel_rect, BInvoker* prog, int32 threshold, int32 strength,
						 direction_t direction, edge_t edge);
static int32 render_blast_row(uchar *buffer, int32 bytes, int32 lpi, int32 threshold,
					   int32 strength, edge_t edge);
static void render_wind_row(uchar *sb, int32 bytes, int32 lpi, int32 threshold,
					  int32 strength, edge_t edge);
static void get_derivative(uchar *pixel_R1, uchar *pixel_R2,
						   edge_t edge, int32 *derivative_R,
						   int32 *derivative_G, int32 *derivative_B);
static int32 threshold_exceeded(uchar *pixel_R1, uchar *pixel_R2,
							   edge_t edge, int32 threshold);
static void reverse_buffer(uchar *buffer, int32 length, int32 bytes);


const char addonName[] = "Wind";
const char addonInfo[] = "Renders a wind effect. Original GIMP plugin by Nigel Wetten; Ported by Scott Lindsey";
const char addonCategory[] = "Effects";
int32 addonVersion = 100;


status_t
Manipulate(
		BitmapAccessor *sourceBitmap,
		BMessage *ioExtension,
		bool checkOnly)
{
		Prefs prefs;

		switch(sourceBitmap->ColorSpace())
		{
				case B_RGBA32_LITTLE:
				case B_RGB32_LITTLE:
						break;
				
				default:
						return B_NOT_ALLOWED;
		}

		if (checkOnly)
				return B_OK;

		gimp_init_glue(sourceBitmap, NULL, ioExtension);

		// If we can't find the selection rect extension, do the whole bitmap
		BRect sel_rect = sourceBitmap->Bounds();

		LoadPrefs(&prefs);
		BInvoker* prog;
		GetSettings(ioExtension, &prefs, &sel_rect, NULL, &prog);

		if (prefs.type == RENDER_WIND)
				return render_wind(sourceBitmap, sel_rect, prog, prefs.threshold, prefs.strength, (direction_t) prefs.direction, (edge_t) prefs.edge);
		else
				return render_blast(sourceBitmap, sel_rect, prog, prefs.threshold, prefs.strength, (direction_t) prefs.direction, (edge_t) prefs.edge);
}


static status_t
render_blast(BitmapAccessor* sourceBitmap, BRect sel, BInvoker* prog, int32 threshold, int32 strength,
			 direction_t direction, edge_t edge)
{
  GIMPImage srcPR(sourceBitmap);
  GIMPImage destPR(sourceBitmap, true);
  int32 width = (int32) sourceBitmap->Bounds().Width() + 1;
  int32 height = (int32) sourceBitmap->Bounds().Height() + 1;
  int32 bytes = (int32) srcPR.bpp;
  int32 x1, x2, y1, y2;
  uchar *buffer;
  int32 row;
  int32 row_stride = width * bytes;
  int32 marker = 0;
  int32 lpi = row_stride - bytes;

		x1 = (int) sel.left;
		x2 = (int) sel.right;
		y1 = (int) sel.top;
		y2 = (int) sel.bottom;
  
  buffer = (uchar*) malloc(row_stride);
  
  for (row = y1; row <= y2; row++)
	{
	  
	  srcPR.GetRow(buffer, x1, row, width);
	  if (direction == RIGHT)
		{
		  reverse_buffer(buffer, row_stride, bytes);
		}
	  marker = render_blast_row(buffer, bytes, lpi, threshold, strength, edge);
	  if (direction == RIGHT)
		{
		  reverse_buffer(buffer, row_stride, bytes);
		}
	  destPR.SetRow(buffer, x1, row, width);
	  gimp_progress_update(prog, (double) row / (double) (y2 - y1 + 1));
	  if (marker)
		{
		  int32 j, limit;
		  
		  limit = 1 + rand() % 2;
		  for (j = 0; (j < limit) && (row <= y2); j++)
			{
			  row++;
			  if (row <= y2)
				{
				  srcPR.GetRow(buffer, x1, row, width);
				  destPR.SetRow(buffer, x1, row, width);
				}
			}
		  marker = 0;
		}
	}  /* for */
  free(buffer);
  
  srcPR.Copy(&destPR);

  return B_OK;
}  /* render_blast */

/*****/

static status_t
render_wind(BitmapAccessor* sourceBitmap, BRect sel, BInvoker* prog, int32 threshold, int32 strength,
			direction_t direction, edge_t edge)
{
  GIMPImage srcPR(sourceBitmap);
  GIMPImage destPR(sourceBitmap, true);
  int32 width = (int32) sourceBitmap->Bounds().Width() + 1;
  int32 height = (int32) sourceBitmap->Bounds().Height() + 1;
  int32 bytes = (int32) srcPR.bpp;
  int32 comp_stride = bytes * COMPARE_WIDTH;
  int32 row;
  uchar *sb;
  int32 x1, y1, x2, y2;
  int32 row_stride = width * bytes;
  int32 lpi = row_stride - comp_stride;

		x1 = (int) sel.left;
		x2 = (int) sel.right;
		y1 = (int) sel.top;
		y2 = (int) sel.bottom;



  sb = (uchar*) malloc(row_stride);
  
		for (row = y1; row <= y2; row++)
		{
				srcPR.GetRow(sb, x1, row, width);
				if (direction == RIGHT)
				{
						reverse_buffer(sb, row_stride, bytes);
				}
				render_wind_row(sb, bytes, lpi, threshold, strength, edge);
				if (direction == RIGHT)
				{
						reverse_buffer(sb, row_stride, bytes);
				}
				destPR.SetRow(sb, x1, row, width);
				gimp_progress_update(prog, (double) row / (double) (y2 - y1 + 1));
		}
  free(sb);

  srcPR.Copy(&destPR);

  return B_OK;
}  /* render_wind */

/*****/

static int32
render_blast_row(uchar *buffer, int32 bytes, int32 lpi, int32 threshold,
				 int32 strength, edge_t edge)
{
  int32 Ri, Gi, Bi;
  int32 sbi, lbi;
  int32 bleed_length;
  int32 i, j;
  int32 weight, random_factor;
  int32 skip = 0;

  for (j = 0; j < lpi; j += bytes)
	{
	  Ri = j; Gi = j + 1; Bi = j + 2;
		  
	  if (threshold_exceeded(buffer+Ri, buffer+Ri+bytes, edge, threshold))
		{
		  /* we have found an edge, do bleeding */
		  sbi = Ri;
			  
		  weight = rand() % 10;
		  if (weight > 5)
			{
			  random_factor = 2;
			}
		  else if (weight > 3)
			{
			  random_factor = 3;
			}
		  else
			{
			  random_factor = 4;
			}
		  bleed_length = 0;
		  switch (rand() % random_factor)
			{
			case 3:
			  bleed_length += strength;
			  /* fall through to add up multiples of strength */
			case 2:
			  bleed_length += strength;
			  /* fall through */
			case 1:
			  bleed_length += strength;
			  /* fall through */
			case 0:
			  bleed_length += strength;
			  /* fall through */
			}

		  lbi = sbi + bytes * bleed_length;
		  if (lbi > lpi)
			{
			  lbi = lpi;
			}

		  for (i = sbi; i < lbi; i += bytes)
			{
			  buffer[i] = buffer[Ri];
			  buffer[i+1] = buffer[Gi];
			  buffer[i+2] = buffer[Bi];
			}
		  j = lbi - bytes;
		  if ((rand() % 10) > 7)
			{
			  skip = 1;
			}
		}  /* if */
	}  /* for j=0 */
 return skip;
}  /* render_blast_row */

/*****/

static void
render_wind_row(uchar *sb, int32 bytes, int32 lpi, int32 threshold,
				int32 strength, edge_t edge)
{
  int32 i, j;
  int32 bleed_length;
  int32 blend_amt_R, blend_amt_G, blend_amt_B;
  int32 blend_colour_R, blend_colour_G, blend_colour_B;
  int32 target_colour_R, target_colour_G, target_colour_B;
  double bleed_length_max;
  int32 bleed_variation;
  int32 n;
  int32 sbi;  /* starting bleed index */
  int32 lbi;	 /* last bleed index */
  double denominator;
  int32 comp_stride = bytes * COMPARE_WIDTH;
  
  for (j = 0; j < lpi; j += bytes)
	{
	  int32 Ri = j;
	  int32 Gi = j + 1;
	  int32 Bi = j + 2;

	  if (threshold_exceeded(sb+Ri, sb+Ri+comp_stride, edge, threshold))
		{
		  /* we have found an edge, do bleeding */
		  sbi = Ri + comp_stride;
		  blend_colour_R = sb[Ri];
		  blend_colour_G = sb[Gi];
		  blend_colour_B = sb[Bi];
		  target_colour_R = sb[sbi];
		  target_colour_G = sb[sbi+1];
		  target_colour_B = sb[sbi+2];
		  bleed_length_max = strength;

		  if (rand() % 3) /* introduce weighted randomness */
			{
			  bleed_length_max = strength;
			}
		  else
			{
			  bleed_length_max = 4 * strength;
			}

		  bleed_variation = 1
			+ (int32) (bleed_length_max * rand() / (RAND_MAX + 1.0));

		  lbi = sbi + bleed_variation * bytes;
		  if (lbi > lpi)
			{
			  lbi = lpi; /* stop overunning the buffer */
			}

		  bleed_length = bleed_variation;

		  blend_amt_R = target_colour_R - blend_colour_R;
		  blend_amt_G = target_colour_G - blend_colour_G;
		  blend_amt_B = target_colour_B - blend_colour_B;
		  denominator = bleed_length * bleed_length + bleed_length;
		  denominator = 2.0 / denominator;
		  n = bleed_length;
		  for (i = sbi; i < lbi; i += bytes)
			{

			  /* check against original colour */
			  if (!threshold_exceeded(sb+Ri, sb+i, edge, threshold)
				  && (rand() % 2))
				{
				  break;
				}

			  blend_colour_R = (int32) (blend_colour_R + blend_amt_R * n * denominator);
			  blend_colour_G = (int32) (blend_colour_G + blend_amt_G * n * denominator);
			  blend_colour_B = (int32) (blend_colour_B + blend_amt_B * n * denominator);

			  if (blend_colour_R > 255) blend_colour_R = 255;
			  else if (blend_colour_R < 0) blend_colour_R = 0;
			  if (blend_colour_G > 255) blend_colour_G = 255;
			  else if (blend_colour_G < 0) blend_colour_G = 0;
			  if (blend_colour_B > 255) blend_colour_B = 255;
			  else if (blend_colour_B < 0) blend_colour_B = 0;

			  sb[i] = (blend_colour_R * 2 + sb[i]) / 3;
			  sb[i+1] = (blend_colour_G * 2 + sb[i+1]) / 3;
			  sb[i+2] = (blend_colour_B * 2 + sb[i+2]) / 3;

			  if (threshold_exceeded(sb+i, sb+i+comp_stride, BOTH,
									 threshold))
				{
				  target_colour_R = sb[i+comp_stride];
				  target_colour_G = sb[i+comp_stride+1];
				  target_colour_B = sb[i+comp_stride+2];
				  blend_amt_R = target_colour_R - blend_colour_R;
				  blend_amt_G = target_colour_G - blend_colour_G;
				  blend_amt_B = target_colour_B - blend_colour_B;
				  denominator = n * n + n;
				  denominator = 2.0 / denominator;
				}
			  n--;
			}
		}  /* if */
	}  /* for j=0 */
  return;
}  /* render_wind_row */

/*****/

static int32
threshold_exceeded(uchar *pixel_R1, uchar *pixel_R2, edge_t edge,
				   int32 threshold)
{
  int32 derivative_R, derivative_G, derivative_B;
  int32 return_value;

  get_derivative(pixel_R1, pixel_R2, edge,
				 &derivative_R, &derivative_G, &derivative_B);

  if(((derivative_R + derivative_G + derivative_B) / 3) > threshold)
	{
	  return_value = 1;
	}
  else
	{
	  return_value = 0;
	}
  return return_value;
}  /* threshold_exceeded */

/*****/

static void
get_derivative(uchar *pixel_R1, uchar *pixel_R2, edge_t edge,
			   int32 *derivative_R, int32 *derivative_G, int32 *derivative_B)
{
  uchar *pixel_G1 = pixel_R1 + 1;
  uchar *pixel_B1 = pixel_R1 + 2;
  uchar *pixel_G2 = pixel_R2 + 1;
  uchar *pixel_B2 = pixel_R2 + 2;

  *derivative_R = *pixel_R2 - *pixel_R1;
  *derivative_G = *pixel_G2 - *pixel_G1;
  *derivative_B = *pixel_B2 - *pixel_B1;
  
  if (edge == BOTH)
	{
	  *derivative_R = abs(*derivative_R);
	  *derivative_G = abs(*derivative_G);
	  *derivative_B = abs(*derivative_B);
	}
  else if (edge == LEADING)
	{
	  *derivative_R = -(*derivative_R);
	  *derivative_G = -(*derivative_G);
	  *derivative_B = -(*derivative_B);
	}
  else if (edge == TRAILING)
	{
	  /* no change needed */
	}
  return;
}  /* get_derivative */

/*****/

static void
reverse_buffer(uchar *buffer, int32 length, int32 bytes)
{
  int32 i, si;
  int32 temp;
  int32 midpoint;
  
  midpoint = length / 2;
  for (i = 0; i < midpoint; i += bytes)
	{
	  si = length - bytes - i;
	  
	  temp = buffer[i];
	  buffer[i] = buffer[si];
	  buffer[si] = (uchar) temp;

	  temp = buffer[i+1];
	  buffer[i+1] = buffer[si+1];
	  buffer[si+1] = (uchar) temp;

	  temp = buffer[i+2];
	  buffer[i+2] = buffer[si+2];
	  buffer[si+2] = (uchar) temp;
	}

  return;
}  /* reverse_buffer */

