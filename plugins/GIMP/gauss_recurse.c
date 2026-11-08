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
* This file has been modified from the original. Relationships between labels and fields
* in the dialog have been changed for aesthetic reasons.  <Scott Lindsey 7/21/98>
*/

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "gimp.h"

typedef struct {
  double radius;
  long horizontal;
  long vertical;
} BlurValues;

#define MAX(a,b) ((a) > (b) ? (a) : (b))

/* Declare a local function.
 */
static void text_callback (int, void *, void *);
static void toggle_callback (int, void *, void *);
static void ok_callback (int, void *, void *);
static void cancel_callback (int, void *, void *);
static void gaussian_blur (Image, Image, int, int, double);
static void find_constants (double *, double *, double *, double *,
			    double *, double *, double);
static void transfer_pixels (double *, double *, unsigned char *, int, int, int);

static char *prog_name;
static int dialog_ID;
static BlurValues vals;

int
main (argc, argv)
     int argc;
     char **argv;
{
  Image input, output;
  int group_ID;
  int text_ID;
  int horz_ID;
  int vert_ID;
  int frame_ID;
  int temp_ID;
  char buf[16];
  void *data;
  double std_dev;
  double radius;

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
	      data = gimp_get_params ();
	      if (data)
		vals = *((BlurValues*) data);
	      else
		{
		  vals.radius = 5.0;
		  vals.horizontal = 1;
		  vals.vertical = 1;
		}

	      dialog_ID = gimp_new_dialog ("Gaussian Blur");
	      gimp_new_label (dialog_ID, DEFAULT, "Options");
	      group_ID = gimp_new_row_group (dialog_ID, DEFAULT, NORMAL, "");
	      
	      sprintf (buf, "%0.2f", vals.radius);
	      text_ID = gimp_new_text (dialog_ID, group_ID, buf);
	      gimp_new_label (dialog_ID, text_ID, "Pixel Radius:");

	      frame_ID = gimp_new_frame (dialog_ID, group_ID, "Direction");
	      temp_ID = gimp_new_row_group (dialog_ID, frame_ID, NORMAL, "");
	      horz_ID = gimp_new_check_button (dialog_ID, temp_ID, "Horizontal");
	      vert_ID = gimp_new_check_button (dialog_ID, temp_ID, "Vertical");
	      gimp_change_item (dialog_ID, horz_ID, sizeof (vals.horizontal), &vals.horizontal);
	      gimp_change_item (dialog_ID, vert_ID, sizeof (vals.vertical), &vals.vertical);
	      
	      gimp_add_callback (dialog_ID, text_ID, text_callback, &vals.radius);
	      gimp_add_callback (dialog_ID, horz_ID, toggle_callback, &vals.horizontal);
	      gimp_add_callback (dialog_ID, vert_ID, toggle_callback, &vals.vertical);
	      gimp_add_callback (dialog_ID, gimp_ok_item_id (dialog_ID), ok_callback, 0);
	      gimp_add_callback (dialog_ID, gimp_cancel_item_id (dialog_ID), cancel_callback, 0);

	      if (gimp_show_dialog (dialog_ID))
		{
		  gimp_set_params (sizeof (BlurValues), &vals);
		  gimp_init_progress ("Gaussian Blur");
		  
		  radius = fabs (vals.radius) + 1.0;
		  if (radius < 2.0)
		    {
		      gimp_message ("Sub-pixel radii yield spurious results.");
		    }

		  std_dev = sqrt (-(radius * radius) / (2 * log (1.0 / 255.0)));

		  gaussian_blur (input, output, vals.horizontal, vals.vertical, std_dev);
		    
		  gimp_update_image (output);
		}
	    }
	  else
	    gimp_message ("blur: cannot operate on indexed color images");
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

static void
text_callback (item_ID, client_data, call_data)
     int item_ID;
     void *client_data;
     void *call_data;
{
  *((double*) client_data) = atof (call_data);
}

static void
toggle_callback (item_ID, client_data, call_data)
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
gaussian_blur (input, output, horz, vert, std_dev)
     Image input, output;
     int horz, vert;
     double std_dev;
{
  long width, height;
  long channels, rowstride;
  unsigned char *dest;
  unsigned char *src, *sp_p, *sp_m;
  double n_p[5], n_m[5];
  double d_p[5], d_m[5];
  double bd_p[5], bd_m[5];
  int x1, y1, x2, y2;
  int i, j;
  int row, col, chan;
  int terms;
  int progress, max_progress;
  double *val_p, *val_m, *vp, *vm;
  int initial_p[4];
  int initial_m[4];

  gimp_image_area (input, &x1, &y1, &x2, &y2);
  
  width = (x2 - x1);
  height = (y2 - y1);
  channels = gimp_image_channels (input);
  rowstride = gimp_image_width (input) * channels;

  val_p = (double *) malloc (MAX (width, height) * channels * sizeof (double));
  val_m = (double *) malloc (MAX (width, height) * channels * sizeof (double));

  if (!val_p || !val_m)
    {
      fprintf (stderr, "%s: unable to allocate memory\n", prog_name);
      gimp_quit ();
    }

  /*  derive the constants for calculating the gaussian from the std dev  */
  find_constants (n_p, n_m, d_p, d_m, bd_p, bd_m, std_dev);

  progress = 0;
  max_progress = (horz) ? height : 0;
  max_progress += (vert) ? width : 0;
  
  src = (unsigned char *) gimp_image_data (input) + (x1 * channels) + y1 * rowstride;

  if (vert)
    {
      dest = (unsigned char *) gimp_image_data (output) + (x1 * channels) + y1 * rowstride;

      /*  First the vertical pass  */
      for (col = 0; col < width; col++)
	{
	  for (i = 0; i < height * channels; i++)
	    {
	      val_p[i] = val_m[i] = 0;
	    }
	  
	  sp_p = src;
	  sp_m = src + (height - 1) * rowstride;
	  vp = val_p;
	  vm = val_m + (height - 1) * channels;
	  
	  /*  Set up the first vals  */
	  for (i = 0; i < channels; i++)
	    {
	      initial_p[i] = sp_p[i];
	      initial_m[i] = sp_m[i];
	    }
	  
	  for (row = 0; row < height; row++)
	    {
	      terms = (row < 4) ? row : 4;
	      
	      for (chan = 0; chan < channels; chan++)
		{
		  for (i = 0; i <= terms; i++)
		    {
		      vp[chan] += n_p[i] * sp_p[(-i * rowstride) + chan] -
			d_p[i] * vp[(-i * channels) + chan];
		      vm[chan] += n_m[i] * sp_m[(i * rowstride) + chan] -
			d_m[i] * vm[(i * channels) + chan];
		    }
		  for (j = i; j <= 4; j++)
		    {
		      vp[chan] += (n_p[j] - bd_p[j]) * initial_p[chan];
		      vm[chan] += (n_m[j] - bd_m[j]) * initial_m[chan];
		    }
		}

	      sp_p += rowstride;
	      sp_m -= rowstride;
	      vp += channels;
	      vm -= channels;
	    }

	  transfer_pixels (val_p, val_m, dest, channels, rowstride, height);
	  
	  src += channels;
	  dest += channels;
	  
	  progress++;
	  if ((progress % 5) == 0)
	    gimp_do_progress (progress, max_progress);
	}

      /*  prepare for the horizontal pass  */
      src = (unsigned char *) gimp_image_data (output) + (x1 * channels) + y1 * rowstride;
    }

  if (horz)
    {
      dest = (unsigned char *) gimp_image_data (output) + (x1 * channels) + y1 * rowstride;
      
      /*  Now the horizontal pass  */
      for (row = 0; row < height; row++)
	{
	  for (i = 0; i < width * channels; i++)
	    {
	      val_p[i] = val_m[i] = 0;
	    }
	  
	  sp_p = src;
	  sp_m = src + (width - 1) * channels;
	  vp = val_p;
	  vm = val_m + (width - 1) * channels;
	  
	  /*  Set up the first vals  */
	  for (i = 0; i < channels; i++)
	    {
	      initial_p[i] = sp_p[i];
	      initial_m[i] = sp_m[i];
	    }
	  
	  for (col = 0; col < width; col++)
	    {
	      terms = (col < 4) ? col : 4;
	      
	      for (chan = 0; chan < channels; chan++)
		{
		  for (i = 0; i <= terms; i++)
		    {
		      vp[chan] += n_p[i] * sp_p[(-i * channels) + chan] -
			d_p[i] * vp[(-i * channels) + chan];
		      vm[chan] += n_m[i] * sp_m[(i * channels) + chan] -
			d_m[i] * vm[(i * channels) + chan];
		    }
		  for (j = i; j <= 4; j++)
		    {
		      vp[chan] += (n_p[j] - bd_p[j]) * initial_p[chan];
		      vm[chan] += (n_m[j] - bd_m[j]) * initial_m[chan];
		    }
		}
	      
	      sp_p += channels;
	      sp_m -= channels;
	      vp += channels;
	      vm -= channels;
	    }
	  
	  transfer_pixels (val_p, val_m, dest, channels, channels, width);
	  
	  src += rowstride;
	  dest += rowstride;
	  
	  progress++;
	  if ((progress % 5) == 0)
	    gimp_do_progress (progress, max_progress);
	}
    }

  free (val_p);
  free (val_m);
}


void
find_constants (n_p, n_m, d_p, d_m, bd_p, bd_m, std_dev)
     double n_p[];
     double n_m[];
     double d_p[];
     double d_m[];
     double bd_p[];
     double bd_m[];
     double std_dev;
{
  int i;
  double constants [8];
  double div;

  /*  The constants used in the implemenation of a casual sequence
   *  using a 4th order approximation of the gaussian operator
   */

  div = sqrt(2 * M_PI) * std_dev;
  constants [0] = -1.783 / std_dev;
  constants [1] = -1.723 / std_dev;
  constants [2] = 0.6318 / std_dev;
  constants [3] = 1.997  / std_dev;
  constants [4] = 1.6803 / div;
  constants [5] = 3.735 / div;
  constants [6] = -0.6803 / div;
  constants [7] = -0.2598 / div;

  n_p [0] = constants[4] + constants[6];
  n_p [1] = exp (constants[1]) *
    (constants[7] * sin (constants[3]) -
     (constants[6] + 2 * constants[4]) * cos (constants[3])) +
       exp (constants[0]) *
	 (constants[5] * sin (constants[2]) - 
	  (2 * constants[6] + constants[4]) * cos (constants[2]));
  n_p [2] = 2 * exp (constants[0] + constants[1]) *
    ((constants[4] + constants[6]) * cos (constants[3]) * cos (constants[2]) -
     constants[5] * cos (constants[3]) * sin (constants[2]) -
     constants[7] * cos (constants[2]) * sin (constants[3])) +
       constants[6] * exp (2 * constants[0]) +
	 constants[4] * exp (2 * constants[1]);
  n_p [3] = exp (constants[1] + 2 * constants[0]) *
    (constants[7] * sin (constants[3]) - constants[6] * cos (constants[3])) +
      exp (constants[0] + 2 * constants[1]) *
	(constants[5] * sin (constants[2]) - constants[4] * cos (constants[2]));
  n_p [4] = 0.0;

  d_p [0] = 0.0;
  d_p [1] = -2 * exp (constants[1]) * cos (constants[3]) -
    2 * exp (constants[0]) * cos (constants[2]);
  d_p [2] = 4 * cos (constants[3]) * cos (constants[2]) * exp (constants[0] + constants[1]) +
    exp (2 * constants[1]) + exp (2 * constants[0]);
  d_p [3] = -2 * cos (constants[2]) * exp (constants[0] + 2 * constants[1]) -
    2 * cos (constants[3]) * exp (constants[1] + 2 * constants[0]);
  d_p [4] = exp (2 * constants[0] + 2 * constants[1]);

  for (i = 0; i <= 4; i++)
    d_m [i] = d_p [i];

  n_m[0] = 0.0;
  for (i = 1; i <= 4; i++)
    n_m [i] = n_p[i] - d_p[i] * n_p[0];

  {
    double sum_n_p, sum_n_m, sum_d;
    double a, b;

    sum_n_p = 0.0;
    sum_n_m = 0.0;
    sum_d = 0.0;
    for (i = 0; i <= 4; i++)
      {
	sum_n_p += n_p[i];
	sum_n_m += n_m[i];
	sum_d += d_p[i];
      }

    a = sum_n_p / (1 + sum_d);
    b = sum_n_m / (1 + sum_d);

    for (i = 0; i <= 4; i++)
      {
	bd_p[i] = d_p[i] * a;
	bd_m[i] = d_m[i] * b;
      }
  }
}
     

static void
transfer_pixels (src1, src2, dest, channels, rowstride, width)
     double * src1, * src2;
     unsigned char * dest;
     int channels, rowstride, width;
{
  int c;
  double sum;

  while (width --)
    {
      for (c = 0; c < channels; c++)
	{
	  sum = src1[c] + src2[c];
	  if (sum > 255) sum = 255;
	  else if (sum < 0) sum = 0;
	    
	  dest[c] = (unsigned char) sum;
	}
      src1 += channels;
      src2 += channels;
      dest += rowstride;
    }
}

