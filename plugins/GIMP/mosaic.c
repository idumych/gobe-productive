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

/*  Mosaic is a filter which transforms an image into
 *  what appears to be a mosaic, composed of small primitives,
 *  each of constant color and of an approximate size.
 *        Copyright (C) 1996  Spencer Kimball
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gimp.h"

#define  MAXIMUM(a,b)    ((a) > (b) ? (a) : (b))
#define  MINIMUM(a,b)    ((a) < (b) ? (a) : (b))
#define  SQR(x)          ((x) * (x))
#define  HORIZONTAL      0
#define  VERTICAL        1
#define  OPAQUE          255
#define  SUPERSAMPLE     3
#define  MAG_THRESHOLD   7.5
#define  COUNT_THRESHOLD 0.1
#define  MAX_POINTS      12

#define  SQUARES  0
#define  HEXAGONS 1

#define  SMOOTH   0
#define  ROUGH    1

#define  BW       0
#define  FG_BG    1

typedef struct
{
  double x, y;
} Vertex;

typedef struct
{
  int npts;
  Vertex pts[MAX_POINTS];
} Polygon;

typedef struct
{
  double base_x, base_y;
  double norm_x, norm_y;
  double light;
} SpecVec;

typedef struct
{
  double tile_size;
  double tile_height;
  double tile_spacing;
  double light_dir;
  double randomness;
  int    antialiasing;
  int    tile_type;
  int    tile_surface;
  int    grout_color;
} MosaicVals;

/* Declare a local function.
 */
static void   scale_callback (int, void *, void *);
static void   toggle_callback (int, void *, void *);
static void   ok_callback (int, void *, void *);
static void   cancel_callback (int, void *, void *);
static void	  Validate();

/*  gradient finding machinery  */
static void   find_gradients (Image, Image, double);
static void   find_max_gradient (unsigned char *, unsigned char *,
				 int, int, int, int);

/*  gaussian & 1st derivative  */
static void   gaussian_deriv (unsigned char *, unsigned char *,
			      int, int, int, int, int, double, int *, int, int);
static void   make_curve (int *, int *, double, int);
static void   make_curve_d (int *, int *, double, int);

/*  grid creation and localization machinery  */
static void   grid_create_squares (Image);
static void   grid_create_hexagons (Image);
static void   grid_localize (Image);
static void   grid_render (Image, Image);
static void   split_poly (Polygon *, unsigned char *, unsigned char *, unsigned char *,
			  int, int, int, int, double *, int, int, double);
static void   clip_poly (double *, double *, Polygon *, Polygon *);
static void   clip_point (double *, double *, double, double, double, double, Polygon *);
static void   render_poly (Polygon *, unsigned char *, unsigned char *, unsigned char *,
			   int, int, int, int, int, int, double);
static void   find_poly_dir (Polygon *, unsigned char *, unsigned char *, unsigned char *,
			     double *, double *, int, int, int, int, int);
static void   find_poly_color (Polygon *, unsigned char *, unsigned char *, int, int,
			       int, int, int, int, double);
static void   scale_poly      (Polygon *, double, double, double);
static void   fill_poly_color (Polygon *, unsigned char *, unsigned char *,
			       int, int, int, int, int, int);
static void   calc_spec_vec (SpecVec *, int, int, int, int);
static double calc_spec_contrib (SpecVec *, int, double, double);
static void   convert_segment (int, int, int, int, int, int *, int *);
static void   polygon_add_point (Polygon *, double, double);
static int    polygon_find_center (Polygon *, double *, double *);
static void   polygon_translate (Polygon *, double, double);
static void   polygon_scale (Polygon *, double);
static int    polygon_extents (Polygon *, double *, double *, double *, double *);
static void   polygon_reset (Polygon *);

static void	  repair_alpha(Image);

static char *prog_name;
static int dialog_id;
static double light_x, light_y;
static double scale;
static unsigned char *h_grad, *v_grad, *m_grad;
static Vertex *grid;
static int grid_rows, grid_cols;
static int grid_row_pad, grid_col_pad;
static int grid_rowstride;
static unsigned char back[4], fore[4];
static SpecVec vecs[MAX_POINTS];
static MosaicVals mvals =
{
  15.0,        /* tile_size */
  4.0,         /* tile_height */
  1.0,         /* tile_spacing */
  3.0*M_PI_4,  /* light_dir */
  0.2,         /* randomness */
  1,           /* antialiasing */
  HEXAGONS,    /* tile_type */
  SMOOTH,      /* tile_surface */
  BW           /* grout_color */
};


int
main (argc, argv)
     int argc;
     char **argv;
{
  Image input, output;
  void *data;
  double std_dev = 1.0;
  long use_smooth;
  long use_rough;
  long use_squares;
  long use_hexagons;
  long use_bw;
  long use_fg_bg;
  long size_val;
  long height_val;
  long light_dir_val;
  long spacing_val;
  long randomness_val;
  long antialiasing_val;

  int antialiasing_id;
  int row_id;
  int col_id;
  int radio_id;
  int size_id;
  int height_id;
  int spacing_id;
  int light_dir_id;
  int randomness_id;
  int squares_id;
  int hexagons_id;
  int smooth_id;
  int rough_id;
  int frame_id;
  int bw_id;
  int fg_bg_id;
  
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
      
      if (input && output)
	{
	  data = gimp_get_params ();
	  if (data)
	{
	    mvals = *((MosaicVals*) data);
		Validate();
	}

	  switch (gimp_image_type (input))
	    {
	    case RGB_IMAGE:
	    case GRAY_IMAGE:
	      
	      size_val = (int) (mvals.tile_size * 10);
	      height_val = (int) (mvals.tile_height * 10);
	      spacing_val = (int) (mvals.tile_spacing * 10);
	      light_dir_val = (int) (mvals.light_dir * 180 / M_PI);
	      randomness_val = (int) (mvals.randomness * 100);
	      antialiasing_val = mvals.antialiasing;
	      use_squares = (mvals.tile_type == SQUARES);
	      use_hexagons = (mvals.tile_type == HEXAGONS);
	      use_smooth = (mvals.tile_surface == SMOOTH);
	      use_rough = (mvals.tile_surface == ROUGH);
	      use_bw = (mvals.grout_color == BW);
	      use_fg_bg = (mvals.grout_color == FG_BG);
	      
	      dialog_id = gimp_new_dialog ("Mosaic");
	      col_id = gimp_new_column_group(dialog_id, DEFAULT, NORMAL, "");

	      row_id = gimp_new_row_group(dialog_id, col_id, NORMAL, "");

	      frame_id = gimp_new_frame (dialog_id, row_id, "Options");
	      radio_id = gimp_new_row_group (dialog_id, frame_id, NORMAL, "");
	      antialiasing_id = gimp_new_check_button (dialog_id, radio_id, "Anti-Aliasing");
	      gimp_change_item (dialog_id, antialiasing_id, sizeof (long), &antialiasing_val);
	      gimp_add_callback (dialog_id, antialiasing_id, toggle_callback, &antialiasing_val);

	      frame_id = gimp_new_frame (dialog_id, row_id, "Tiling Primitive");
	      radio_id = gimp_new_row_group (dialog_id, frame_id, RADIO, "");

	      hexagons_id = gimp_new_radio_button (dialog_id, radio_id, "Hexagon");
	      gimp_change_item (dialog_id, hexagons_id, sizeof (long), &use_hexagons);
	      gimp_add_callback (dialog_id, hexagons_id, toggle_callback, &use_hexagons);

	      squares_id = gimp_new_radio_button (dialog_id, radio_id, "Square");
	      gimp_change_item (dialog_id, squares_id, sizeof (long), &use_squares);
	      gimp_add_callback (dialog_id, squares_id, toggle_callback, &use_squares);

	      frame_id = gimp_new_frame (dialog_id, row_id, "Tiling Surface");
	      radio_id = gimp_new_row_group (dialog_id, frame_id, RADIO, "");

	      smooth_id = gimp_new_radio_button (dialog_id, radio_id, "Smooth");
	      gimp_change_item (dialog_id, smooth_id, sizeof (long), &use_smooth);
	      gimp_add_callback (dialog_id, smooth_id, toggle_callback, &use_smooth);

	      rough_id = gimp_new_radio_button (dialog_id, radio_id, "Rough");
	      gimp_change_item (dialog_id, rough_id, sizeof (long), &use_rough);
	      gimp_add_callback (dialog_id, rough_id, toggle_callback, &use_rough);

	      frame_id = gimp_new_frame (dialog_id, row_id, "Light Source/Shadow Colors");
	      radio_id = gimp_new_row_group (dialog_id, frame_id, RADIO, "");
	      bw_id = gimp_new_radio_button (dialog_id, radio_id, "White/Black");
	      gimp_change_item (dialog_id, bw_id, sizeof (long), &use_bw);
	      gimp_add_callback (dialog_id, bw_id, toggle_callback, &use_bw);

	      fg_bg_id = gimp_new_radio_button (dialog_id, radio_id, "Foreground/Background");
	      gimp_change_item (dialog_id, fg_bg_id, sizeof (long), &use_fg_bg);
	      gimp_add_callback (dialog_id, fg_bg_id, toggle_callback, &use_fg_bg);

	      frame_id = gimp_new_frame (dialog_id, col_id, "Parameter Settings");
	      row_id = gimp_new_row_group(dialog_id, frame_id, NORMAL, "");

	      size_id = gimp_new_scale(dialog_id, row_id, 20, 500,
				       size_val, 1);
	      gimp_new_label(dialog_id, size_id, "Tile Size:");
	      gimp_add_callback(dialog_id, size_id, scale_callback,
				&size_val);
	      
	      height_id = gimp_new_scale(dialog_id, row_id, 10, 250,
					 height_val, 1);
	      gimp_new_label(dialog_id, height_id, "Tile Height:");
	      gimp_add_callback(dialog_id, height_id, scale_callback,
				&height_val);
	      
	      spacing_id = gimp_new_scale(dialog_id, row_id, 10, 250,
					  spacing_val, 1);
	      gimp_new_label(dialog_id, spacing_id, "Tile Spacing:");
	      gimp_add_callback(dialog_id, spacing_id, scale_callback,
				&spacing_val);
	      
	      light_dir_id = gimp_new_scale(dialog_id, row_id, 0, 360,
					    light_dir_val, 0);
	      gimp_new_label(dialog_id, light_dir_id, "Light Direction:");
	      gimp_add_callback(dialog_id, light_dir_id, scale_callback,
				&light_dir_val);
	      
	      randomness_id = gimp_new_scale(dialog_id, row_id, 0, 100,
					     randomness_val, 2);
	      gimp_new_label(dialog_id, randomness_id, "Randomness:");
	      gimp_add_callback(dialog_id, randomness_id, scale_callback,
				&randomness_val);

	      gimp_add_callback (dialog_id, gimp_ok_item_id (dialog_id), ok_callback, 0);
	      gimp_add_callback (dialog_id, gimp_cancel_item_id (dialog_id), cancel_callback, 0);
	      
	      if (gimp_show_dialog (dialog_id))
		{
		  /* Get filter parameters from dialog values  */
		  mvals.tile_size = (double) size_val / 10.0;
		  mvals.tile_height = (double) height_val / 10.0;
		  mvals.tile_spacing = (double) spacing_val / 10.0;
		  mvals.light_dir = (double) (light_dir_val * M_PI) / 180.0;
		  mvals.randomness = (double) randomness_val / 100.0;
		  mvals.antialiasing = antialiasing_val;
		  mvals.tile_type = (use_squares) ? SQUARES : HEXAGONS;
		  mvals.tile_surface = (use_smooth) ? SMOOTH : ROUGH;
		  mvals.grout_color = (use_bw) ? BW : FG_BG;
			Validate();

		  gimp_set_params (sizeof (MosaicVals), &mvals);

		  /* Find image gradients  */
		  gimp_init_progress ("Finding edges");
		  find_gradients (input, output, std_dev);

		  /* Create and render polygonal grid  */
		  gimp_init_progress ("Tiling image");

		  switch (mvals.tile_type)
		    {
		    case SQUARES:
		      grid_create_squares (input);
		      break;
		    case HEXAGONS:
		      grid_create_hexagons (input);
		      break;
		    default:
		      gimp_message ("Unknown tile type.\n");
		      gimp_quit ();
		      break;
		    }

		  grid_localize (input);
		  
		  if (use_bw)
		    {
		      fore[0] = fore[1] = fore[2] = 255;
		      back[0] = back[1] = back[2] = 0;
		    }
		  else
		    {
		      gimp_foreground_color (&fore[0], &fore[1], &fore[2]);
		      gimp_background_color (&back[0], &back[1], &back[2]);
		    }

		  light_x = -cos (mvals.light_dir);
		  light_y = sin (mvals.light_dir);
		  scale = (mvals.tile_spacing > mvals.tile_size / 2.0) ? 
		    0.5 : 1.0 - mvals.tile_spacing / mvals.tile_size;

		  grid_render (input, output);
		  // BOGUS, but it makes it usable
		  repair_alpha(output);
		  
		  gimp_display_image (output);
		  gimp_update_image (output);
		}
	      break;
	      
	    default:
	      gimp_message ("mosaic: cannot operate on indexed color images");
	      break;
	    }
	}
      
      /* Free input images.
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
  gimp_close_dialog (dialog_id, 1);
}

static void
cancel_callback (item_ID, client_data, call_data)
     int item_ID;
     void *client_data;
     void *call_data;
{
  gimp_close_dialog (dialog_id, 0);
}


static void
find_gradients (src, dest, std_dev)
     Image src, dest;
     double std_dev;
{
  unsigned char * s, * d;
  int bytes;
  int width, height;
  int rowstride;
  int i, j;
  int x1, y1, x2, y2;
  double gradient;
  unsigned char *gr, * dh, * dv;
  int hmax, vmax;
  int row, rows;
  int ith_row;

  gimp_image_area (src, &x1, &y1, &x2, &y2);
  width = (x2 - x1);
  height = (y2 - y1);
  bytes = gimp_image_channels (src);
  rowstride = bytes * gimp_image_width (src);
  s = (unsigned char *) gimp_image_data (src) + bytes * (y1 * gimp_image_width (src) + x1);
  d = (unsigned char *) gimp_image_data (dest) + bytes * (y1 * gimp_image_width (dest) + x1);
  
  /*  Calculate total number of rows to be processed  */
  rows = width * 2 + height * 2;
  ith_row = rows / 256;
  if (!ith_row) ith_row = 1;
  row = 0;

  /*  Get the horizontal derivative  */
  gaussian_deriv (s, d, rowstride, bytes, width, height, HORIZONTAL, std_dev, &row, rows, ith_row);
  h_grad = (unsigned char *) malloc (width * height);
  find_max_gradient (d, h_grad, rowstride, bytes, width, height);
  
  /*  Get the vertical derivative  */
  gaussian_deriv (s, d, rowstride, bytes, width, height, VERTICAL, std_dev, &row, rows, ith_row);
  v_grad = (unsigned char *) malloc (width * height);
  find_max_gradient (d, v_grad, rowstride, bytes, width, height);

  gimp_do_progress (1, 1);

  /*  fill in the gradient map  */
  m_grad = (unsigned char *) malloc (width * height);
  gr = m_grad;
  dh = h_grad;
  dv = v_grad;

  for (i = 0; i < height; i++)
    {
      for (j = 0; j < width; j++)
	{
	  hmax = dh[j] - 128;
	  vmax = dv[j] - 128;
	  
	  /*  Find the gradient  */
	  gradient = sqrt (SQR (hmax) + SQR (vmax));

	  if (!j || !i || (j == width - 1) || (i == height - 1))
	    gr[j] = MAG_THRESHOLD;
	  else
	    gr[j] = (unsigned char) gradient;
	}

      dh += width;
      dv += width;
      gr += width;
    }
}


static void
find_max_gradient (input_data, output_data, rowstride, bytes, width, height)
     unsigned char * input_data, * output_data;
     int rowstride;
     int bytes;
     int width, height;
{
  unsigned char * s, * d;
  int i, j;
  int b;
  int val;
  int max;

  /*  Find the maximum value amongst intensity channels  */
  for (i = 0; i < height; i++)
    {
      s = input_data;
      d = output_data;

      for (j = 0; j < width; j++)
	{
	  max = 0;
	  for (b = 0; b < bytes; b++)
	    {
	      val = (int) s[b] - 128;
	      if (abs (val) > abs (max))
		max = val;
	    }

	  *d++ = (val + 128);
	  s += bytes;
	}

      input_data += rowstride;
      output_data += width;
    }
}


/*********************************************/
/*   Functions for gaussian convolutions     */
/*********************************************/


static void
gaussian_deriv (input_data, output_data, rowstride, bytes, width, height,
		type, std_dev, prog, max_prog, ith_prog)
     unsigned char * input_data, * output_data;
     int rowstride;
     int bytes;
     int width, height;
     int type;
     double std_dev;
     int * prog;
     int max_prog, ith_prog;
{
  unsigned char *dest, *dp;
  unsigned char *src, *sp, *s;
  int *buf, *b;
  int chan;
  int i, row, col;
  int start, end;
  int curve_array [9];
  int sum_array [9];
  int * curve;
  int * sum;
  int val;
  int total;
  int length;
  int initial_p[4], initial_m[4];

  length = 3;    /*  static for speed  */

  /*  initialize  */
  curve = curve_array + length;
  sum = sum_array + length;
  buf = malloc (sizeof (int) * MAXIMUM (width, height) * bytes);

  if (type == VERTICAL)
    {
      make_curve_d (curve, sum, std_dev, length);
      total = sum[0] * -2;
    }
  else
    {
      make_curve (curve, sum, std_dev, length);
      total = sum[length] + curve[length];
    }

  src = input_data;
  dest = output_data;

  for (col = 0; col < width; col++)
    {
      sp = src;
      dp = dest;
      b = buf;

      src += bytes;
      dest += bytes;
      
      for (chan = 0; chan < bytes; chan++)
	{
	  initial_p[chan] = sp[chan];
	  initial_m[chan] = sp[(height-1) * rowstride + chan];
	}

      for (row = 0; row < height; row++)
	{
	  start = (row < length) ? -row : -length;
	  end = (height <= (row + length)) ? (height - row - 1) : length;

	  for (chan = 0; chan < bytes; chan++)
	    {
	      s = sp + (start * rowstride) + chan ;
	      val = 0;
	      i = start;

	      if (start != -length)
		val += initial_p[chan] * (sum[start] - sum[-length]);

	      while (i <= end)
		{
		  val += *s * curve[i++];
		  s += rowstride;
		}

	      if (end != length)
		val += initial_m[chan] * (sum[length] + curve[length] - sum[end+1]);
	  
	      *b++ = val / total;
	    }

	  sp += rowstride;
	}

      b = buf;
      if (type == VERTICAL)
	for (row = 0; row < height; row++)
	  {
	    for (chan = 0; chan < bytes; chan++)
	      {
		b[chan] = b[chan] + 128;
		if (b[chan] > 255)
		  dp[chan] = 255;
		else if (b[chan] < 0)
		  dp[chan] = 0;
		else 
		  dp[chan] = b[chan];
	      }
	    b += bytes;
	    dp += rowstride;
	  }
      else
	for (row = 0; row < height; row++)
	  {
	    for (chan = 0; chan < bytes; chan++)
	      {
		if (b[chan] > 255)
		  dp[chan] = 255;
		else if (b[chan] < 0)
		  dp[chan] = 0;
		else
		  dp[chan] = b[chan];
	      }
	    b += bytes;
	    dp += rowstride;
	  }

      if (! ((*prog)++ % ith_prog))
	gimp_do_progress (*prog, max_prog);
    }

  if (type == HORIZONTAL)
    {
      make_curve_d (curve, sum, std_dev, length);
      total = sum[0] * -2;
    }
  else
    {
      make_curve (curve, sum, std_dev, length);
      total = sum[length] + curve[length];
    }

  src = output_data;
  dest = output_data;

  for (row = 0; row < height; row++)
    {
      sp = src;
      dp = dest;
      b = buf;

      src += rowstride;
      dest += rowstride;
      
      for (chan = 0; chan < bytes; chan++)
	{
	  initial_p[chan] = sp[chan];
	  initial_m[chan] = sp[(width-1) * bytes + chan];
	}

      for (col = 0; col < width; col++)
	{
	  start = (col < length) ? -col : -length;
	  end = (width <= (col + length)) ? (width - col - 1) : length;

	  for (chan = 0; chan < bytes; chan++)
	    {
	      s = sp + (start * bytes) + chan;
	      val = 0;
	      i = start;

	      if (start != -length)
		val += initial_p[chan] * (sum[start] - sum[-length]);

	      while (i <= end)
		{
		  val += *s * curve[i++];
		  s += bytes;
		}

	      if (end != length)
		val += initial_m[chan] * (sum[length] + curve[length] - sum[end+1]);
	  
	      *b++ = val / total;
	    }

	  sp += bytes;
	}

      b = buf;
      if (type == HORIZONTAL)
	for (col = 0; col < width; col++)
	  {
	    for (chan = 0; chan < bytes; chan++)
	      {
		b[chan] = b[chan] + 128;
		if (b[chan] > 255)
		  dp[chan] = 255;
		else if (b[chan] < 0)
		  dp[chan] = 0;
		else
		  dp[chan] = b[chan];
	      }
	    b += bytes;
	    dp += bytes;
	  }
      else
	for (col = 0; col < width; col++)
	  {
	    for (chan = 0; chan < bytes; chan++)
	      {
		if (b[chan] > 255)
		  dp[chan] = 255;
		else if (b[chan] < 0)
		  dp[chan] = 0;
		else
		  dp[chan] = b[chan];
	      }
	    b += bytes;
	    dp += bytes;
	  }

      if (! ((*prog)++ % ith_prog))
	gimp_do_progress (*prog, max_prog);
    }
  
  free (buf);
}  
  
/*
 * The equations: g(r) = exp (- r^2 / (2 * sigma^2))
 *                   r = sqrt (x^2 + y ^2)
 */

static void
make_curve (curve, sum, sigma, length)
     int * curve;
     int * sum;
     double sigma;
     int length;
{
  double sigma2;
  int i;

  sigma2 = sigma * sigma;

  curve[0] = 255;
  for (i = 1; i <= length; i++)
    {
      curve[i] = (int) (exp (- (i * i) / (2 * sigma2)) * 255);
      curve[-i] = curve[i];
    }

  sum[-length] = 0;
  for (i = -length+1; i <= length; i++)
    sum[i] = sum[i-1] + curve[i-1];
}


/*
 * The equations: d_g(r) = -r * exp (- r^2 / (2 * sigma^2)) / sigma^2
 *                   r = sqrt (x^2 + y ^2)
 */

static void
make_curve_d (curve, sum, sigma, length)
     int * curve;
     int * sum;
     double sigma;
     int length;
{
  double sigma2;
  int i;

  sigma2 = sigma * sigma;

  curve[0] = 0;
  for (i = 1; i <= length; i++)
    {
      curve[i] = (int) ((i * exp (- (i * i) / (2 * sigma2)) / sigma2) * 255);
      curve[-i] = -curve[i];
    }

  sum[-length] = 0;
  sum[0] = 0;
  for (i = 1; i <= length; i++)
    {
      sum[-length + i] = sum[-length + i - 1] + curve[-length + i - 1];
      sum[i] = sum[i - 1] + curve[i - 1];
    }
}


/*********************************************/
/*   Functions for grid manipulation         */
/*********************************************/

static void
grid_create_squares (src)
     Image src;
{
  int rows, cols;
  int width, height;
  int i, j;
  int x1, y1, x2, y2;
  int size = (int) mvals.tile_size;
  Vertex *pt;

  gimp_image_area (src, &x1, &y1, &x2, &y2);
  width = x2 - x1;
  height = y2 - y1;
  rows = (height + size - 1) / size;
  cols = (width + size - 1) / size;

  grid = (Vertex *) malloc (sizeof (Vertex) * (cols + 2) * (rows + 2));
  grid += (cols + 2) + 1;

  for (i = -1; i <= rows; i++)
	{
    for (j = -1; j <= cols; j++)
      {
	pt = grid + (i * (cols + 2) + j);

	pt->x = x1 + j * size + size/2;
	pt->y = y1 + i * size + size/2;
      }
	}


  grid_rows = rows;
  grid_cols = cols;
  grid_row_pad = 1;
  grid_col_pad = 1;
  grid_rowstride = cols + 2;
}


static void
grid_create_hexagons (src)
     Image src;
{
  int rows, cols;
  int width, height;
  int i, j;
  int x1, y1, x2, y2;
  double hex_l1, hex_l2, hex_l3;
  double hex_width;
  double hex_height;
  Vertex *pt;

  gimp_image_area (src, &x1, &y1, &x2, &y2);
  width = x2 - x1;
  height = y2 - y1;
  hex_l1 = mvals.tile_size / 2.0;
  hex_l2 = (mvals.tile_size * sqrt (5.0)) / 2.0;
  hex_l3 = (hex_l2 - hex_l1) / 2.0;
  hex_width = (mvals.tile_size * sqrt (5.0) + mvals.tile_size / 2.0) / 2.0;
  hex_height = mvals.tile_size;
  rows = ((height + 2*hex_height - 1) / hex_height);
  cols = (int) ((width + hex_width - 1) / hex_width) * 4;

  grid = (Vertex *) malloc (sizeof (Vertex) * (cols + 6) * (rows + 2));
  grid += (cols + 6) + 2;

  for (i = -1; i < rows + 1; i++)
    {
      for (j = -2; j < cols + 4; j+=2)
	{
	  pt = grid + (i * (cols + 6) + j);
	  if ((j/2) % 2)
	    {
	      pt[0].x = x1 + (hex_width + mvals.tile_size / 4.0) * ((j-2)/4) + hex_l1;
	      pt[0].y = y1 + hex_height * i;

	      pt[1].x = pt[0].x + hex_l3;
	      pt[1].y = pt[0].y + hex_l1;
	    }
	  else
	    {
	      pt[0].x = pt[-1].x + hex_l1;
	      pt[0].y = pt[-1].y;

	      pt[1].x = pt[-2].x + hex_l2;
	      pt[1].y = pt[-2].y;
	    }
	}
    }
  grid_rows = rows;
  grid_cols = cols;
  grid_row_pad = 1;
  grid_col_pad = 2;
  grid_rowstride = cols + 6;
}


static void
grid_localize (src)
     Image src;
{
  int width, height;
  int i, j;
  int k, l;
  int x1, y1, x2, y2;
  int x3, y3, x4, y4;
  int size;
  int max_x, max_y;
  int max;
  unsigned char * data;
  double rand_localize;
  Vertex * pt;

  gimp_image_area (src, &x1, &y1, &x2, &y2);
  width = x2 - x1;
  height = y2 - y1;
  size = (int) mvals.tile_size;
  rand_localize = pow (mvals.randomness, 0.25);

  for (i = -grid_row_pad; i < grid_rows + grid_row_pad; i++)
  {
   for (j = -grid_col_pad; j < grid_cols + grid_col_pad; j++)
      {
	pt = grid + (i * grid_rowstride + j);

	max_x = pt->x + rand_localize * ((rand () % (size/2)) - size/4);
	max_y = pt->y + rand_localize * ((rand () % (size/2)) - size/4);

	x3 = pt->x - size/4;
	y3 = pt->y - size/4;
	x4 = x3 + size/2;
	y4 = y3 + size/2;
	    
	if (x3 < x1) x3 = x1;
	else if (x3 >= x2) x3 = (x2 - 1);
	if (y3 < y1) y3 = y1;
	else if (y3 >= y2) y3 = (y2 - 1);
	if (x4 >= x2) x4 = (x2 - 1);
	else if (x4 < x1) x4 = x1;
	if (y4 >= y2) y4 = (y2 - 1);
	else if (y4 < y1) y4 = y1;

	max = *(m_grad + (y3 - y1) * width + (x3 - x1));
	data = m_grad + width * (y3 - y1);
	    
	for (k = y3; k <= y4; k++)
	  {
	    for (l = x3; l <= x4; l++)
	      {
		if (data[l] > max)
		  {
		    max_y = k;
		    max_x = l;
		    max = data[(l - x1)];
		  }
	      }
	    data += width;
	  }

	pt->x = max_x;
	pt->y = max_y;
      }
	}
}


static void
grid_render (src, dest)
     Image src;
     Image dest;
{
  int i, j;
  int x1, y1, x2, y2;
  unsigned char *data;
  int bytes;
  int img_rs;
  int grad_rs;
  int size, frac_size;
  double vary;
  unsigned char col[4];
  double dir[2];
  double loc[2];
  double cx, cy;
  double magnitude;
  double distance;
  int count;
  int index;
  Polygon poly;

  gimp_image_area (src, &x1, &y1, &x2, &y2);
  bytes = gimp_image_channels (src);
  img_rs = bytes * gimp_image_width (src);
  grad_rs = (x2 - x1);

  /*  Fill the image with the background color  */
  data = gimp_image_data (dest);
  size = gimp_image_width (src) * gimp_image_height (src);

  while (size--)
    for (i = 0; i < bytes; i++)
      *data++ = back[i];

  size = ((grid_rows + grid_row_pad * 2) / grid_row_pad)
    * ((grid_cols + grid_col_pad * 2) / grid_col_pad);
  frac_size = (int) (size * mvals.randomness);
  count = 0;

  for (i = -grid_row_pad; i < grid_rows; i+=grid_row_pad)
	{
    for (j = -grid_col_pad; j < grid_cols; j+=grid_col_pad)
      {
	/*  determine the variation of tile color based on tile number  */
	vary = ((rand () % size) < frac_size) ? mvals.randomness : 0;

	index = i * grid_rowstride + j;
	switch (mvals.tile_type)
	  {
	  case SQUARES:
	    polygon_reset (&poly);
	    polygon_add_point (&poly,
			       grid[index].x,
			       grid[index].y);
	    polygon_add_point (&poly,
			       grid[index + 1].x,
			       grid[index + 1].y);
	    polygon_add_point (&poly,
			       grid[index + grid_rowstride + 1].x,
			       grid[index + grid_rowstride + 1].y);
	    polygon_add_point (&poly,
			       grid[index + grid_rowstride].x,
			       grid[index + grid_rowstride].y);
	    break;
	  case HEXAGONS:
	    polygon_reset (&poly);

	    if ((j/2) % 2)
	      {
		polygon_add_point (&poly,
				   grid[index + 1].x,
				   grid[index + 1].y);
		polygon_add_point (&poly,
				   grid[index + 2].x,
				   grid[index + 2].y);
		polygon_add_point (&poly,
				   grid[index + grid_rowstride + 3].x,
				   grid[index + grid_rowstride + 3].y);
		polygon_add_point (&poly,
				   grid[index + grid_rowstride + 2].x,
				   grid[index + grid_rowstride + 2].y);
		polygon_add_point (&poly,
				   grid[index + grid_rowstride + 1].x,
				   grid[index + grid_rowstride + 1].y);
		polygon_add_point (&poly,
				   grid[index + grid_rowstride].x,
				   grid[index + grid_rowstride].y);
	      }
	    else
	      {
		polygon_add_point (&poly,
				   grid[index].x,
				   grid[index].y);
		polygon_add_point (&poly,
				   grid[index + 1].x,
				   grid[index + 1].y);
		polygon_add_point (&poly,
				   grid[index + 2].x,
				   grid[index + 2].y);
		polygon_add_point (&poly,
				   grid[index + 3].x,
				   grid[index + 3].y);
		polygon_add_point (&poly,
				   grid[index + grid_rowstride + 2].x,
				   grid[index + grid_rowstride + 2].y);
		polygon_add_point (&poly,
				   grid[index + grid_rowstride + 1].x,
				   grid[index + grid_rowstride + 1].y);
	      };
	    break;
	  }
	/*  Determine direction of edges inside polygon, if any  */
	find_poly_dir (&poly, m_grad, h_grad, v_grad, dir, loc, x1, y1, x2, y2, grad_rs);
	magnitude = sqrt (SQR (dir[0] - 128) + SQR (dir[1] - 128));
	
	/*  Find the center of the polygon  */
	polygon_find_center (&poly, &cx, &cy);
	distance = sqrt (SQR (loc[0] - cx) + SQR (loc[1] - cy));
	
	/*  If the magnitude of direction inside the polygon is greater than
	 *  THRESHOLD, split the polygon into two new polygons
	 */
	if (magnitude > MAG_THRESHOLD && (2 * distance / mvals.tile_size) < 0.5)
	  {
	    split_poly (&poly,
			gimp_image_data (src),
			gimp_image_data (dest),
			col,
			x1, y1, x2, y2,
			dir, img_rs, bytes, vary);
	  }
	/*  Otherwise, render the original polygon
	 */
	else
	  {
	    render_poly (&poly,
			 gimp_image_data (src),
			 gimp_image_data (dest), col,
			 x1, y1, x2, y2,
			 img_rs, bytes, vary);
	  }

	gimp_do_progress (count++, size);
      }
	}

  gimp_do_progress (1, 1);
}


static void
render_poly (poly, src, dest, col, x1, y1, x2, y2, img_rs, bytes, vary)
     Polygon *poly;
     unsigned char * src;
     unsigned char * dest;
     unsigned char * col;
     int x1, y1, x2, y2;
     int img_rs;
     int bytes;
     double vary;
{
  double cx, cy;

  polygon_find_center (poly, &cx, &cy);

  find_poly_color (poly, src, col, x1, y1, x2, y2, img_rs, bytes, vary);
  scale_poly (poly, cx, cy, scale);
  fill_poly_color (poly, dest, col, x1, y1, x2, y2, img_rs, bytes);
}


static void
split_poly (poly, src, dest, col, x1, y1, x2, y2, dir, img_rs, bytes, vary)
     Polygon *poly;
     unsigned char * src;
     unsigned char * dest;
     unsigned char * col;
     int x1, y1, x2, y2;
     double * dir;
     int img_rs;
     int bytes;
     double vary;
{
  Polygon new_poly;
  double spacing;
  double cx, cy;
  double magnitude;
  double vec[2];
  double pt[2];

  spacing = mvals.tile_spacing / (2.0 * scale);

  polygon_find_center (poly, &cx, &cy);
  polygon_translate (poly, -cx, -cy);

  magnitude = sqrt (SQR (dir[0] - 128) + SQR (dir[1] - 128));
  vec[0] = -(dir[1] - 128) / magnitude;
  vec[1] = (dir[0] - 128) / magnitude;
  pt[0] = -vec[1] * spacing;
  pt[1] = vec[0] * spacing;

  polygon_reset (&new_poly);
  clip_poly (vec, pt, poly, &new_poly);
  polygon_translate (&new_poly, cx, cy);

  if (new_poly.npts)
    {
      find_poly_color (&new_poly, src, col, x1, y1, x2, y2, img_rs, bytes, vary);
      scale_poly (&new_poly, cx, cy, scale);
      fill_poly_color (&new_poly, dest, col, x1, y1, x2, y2, img_rs, bytes);
    }

  vec[0] = -vec[0];
  vec[1] = -vec[1];
  pt[0] = -pt[0];
  pt[1] = -pt[1];

  polygon_reset (&new_poly);
  clip_poly (vec, pt, poly, &new_poly);
  polygon_translate (&new_poly, cx, cy);

  if (new_poly.npts)
    {
      find_poly_color (&new_poly, src, col, x1, y1, x2, y2, img_rs, bytes, vary);
      scale_poly (&new_poly, cx, cy, scale);
      fill_poly_color (&new_poly, dest, col, x1, y1, x2, y2, img_rs, bytes);
    }
}


static void
clip_poly (dir, pt, poly, poly_new)
     double *dir;
     double *pt;
     Polygon *poly;
     Polygon *poly_new;
{
  int i;
  double x1, y1, x2, y2;

  for (i = 0; i < poly->npts; i++)
    {
      x1 = (i) ? poly->pts[i-1].x : poly->pts[poly->npts-1].x;
      y1 = (i) ? poly->pts[i-1].y : poly->pts[poly->npts-1].y;
      x2 = poly->pts[i].x;
      y2 = poly->pts[i].y;
      
      clip_point (dir, pt, x1, y1, x2, y2, poly_new);
    }
}


static void
clip_point (dir, pt, x1, y1, x2, y2, poly_new)
     double *dir;
     double *pt;
     double x1, y1;
     double x2, y2;
     Polygon *poly_new;
{
  double det, m11, m12, m21, m22;
  double side1, side2;
  double t;
  double vec[2];

  x1 -= pt[0]; x2 -= pt[0];
  y1 -= pt[1]; y2 -= pt[1];

  side1 = x1 * -dir[1] + y1 * dir[0];
  side2 = x2 * -dir[1] + y2 * dir[0];

  /*  If both points are to be clipped, ignore  */
  if (side1 < 0.0 && side2 < 0.0) 
    return;
  /*  If both points are non-clipped, set point  */
  else if (side1 >= 0.0 && side2 >= 0.0)
    {
      polygon_add_point (poly_new, x2 + pt[0], y2 + pt[1]);
      return;
    }
  /*  Otherwise, there is an intersection...  */
  else
    {
      vec[0] = x1 - x2;
      vec[1] = y1 - y2;
      det = dir[0] * vec[1] - dir[1] * vec[0];

      if (det == 0.0)
	{
	  polygon_add_point (poly_new, x2 + pt[0], y2 + pt[1]);
	  return;
	}

      m11 = vec[1] / det;
      m12 = -vec[0] / det;
      m21 = -dir[1] / det;
      m22 = dir[0] / det;
  
      t = m11 * x1 + m12 * y1;

      /*  If the first point is clipped, set intersection and point  */
      if (side1 < 0.0 && side2 > 0.0)
	{
	  polygon_add_point (poly_new, dir[0] * t + pt[0], dir[1] * t + pt[1]);
	  polygon_add_point (poly_new, x2 + pt[0], y2 + pt[1]);
	}
      else
	polygon_add_point (poly_new, dir[0] * t + pt[0], dir[1] * t + pt[1]);
    }
}


static void
find_poly_dir (poly, m_gr, h_gr, v_gr, dir, loc, x1, y1, x2, y2, rowstride)
     Polygon *poly;
     unsigned char *m_gr;
     unsigned char *h_gr;
     unsigned char *v_gr;
     double *dir;
     double *loc;
     int x1, y1, x2, y2;
     int rowstride;
{
  double dmin_x, dmin_y;
  double dmax_x, dmax_y;
  int xs, ys;
  int xe, ye;
  int min_x, min_y;
  int max_x, max_y;
  int size_x, size_y;
  int * max_scanlines;
  int * min_scanlines;
  unsigned char *dm, *dv, *dh;
  int count, total;
  int i, j;

  count = 0;
  total = 0;
  dir[0] = 0.0;
  dir[1] = 0.0;
  loc[0] = 0.0;
  loc[1] = 0.0;

  polygon_extents (poly, &dmin_x, &dmin_y, &dmax_x, &dmax_y);
  min_x = (int) dmin_x;
  min_y = (int) dmin_y;
  max_x = (int) dmax_x;
  max_y = (int) dmax_y;
  size_y = max_y - min_y;
  size_x = max_x - min_x;

  min_scanlines = (int *) malloc (sizeof (int) * size_y);
  max_scanlines = (int *) malloc (sizeof (int) * size_y);
  for (i = 0; i < size_y; i++)
    {
      min_scanlines[i] = max_x;
      max_scanlines[i] = min_x;
    }
  
  for (i = 0; i < poly->npts; i++)
    {
      xs = (int) ((i) ? poly->pts[i-1].x : poly->pts[poly->npts-1].x);
      ys = (int) ((i) ? poly->pts[i-1].y : poly->pts[poly->npts-1].y);
      xe = (int) poly->pts[i].x;
      ye = (int) poly->pts[i].y;

      convert_segment (xs, ys, xe, ye, min_y,
		       min_scanlines, max_scanlines);
    }

  for (i = 0; i < size_y; i++)
    {
      if ((i + min_y) >= y1 && (i + min_y) < y2)
	{
	  dm = m_gr + (i + min_y - y1) * rowstride - x1;
	  dh = h_gr + (i + min_y - y1) * rowstride - x1;
	  dv = v_gr + (i + min_y - y1) * rowstride - x1;
	  
	  for (j = min_scanlines[i]; j < max_scanlines[i]; j++)
	    {
	      if (j >= x1 && j < x2)
		{
		  if (dm[j] > MAG_THRESHOLD)
		    {
		      dir[0] += dh[j];
		      dir[1] += dv[j];
		      loc[0] += j;
		      loc[1] += i + min_y;
		      count++;
		    }
		  total++;
		}
	    }
	}
    }

  if (total)
    {
      if ((double) count / (double) total > COUNT_THRESHOLD)
	{
	  dir[0] /= count;
	  dir[1] /= count;
	  loc[0] /= count;
	  loc[1] /= count;
	}
      else
	{
	  dir[0] = 128.0;
	  dir[1] = 128.0;
	  loc[0] = 0.0;
	  loc[1] = 0.0;
	}
    }

  free (min_scanlines);
  free (max_scanlines);
}


static void
find_poly_color (poly, data, col, x1, y1, x2, y2, rowstride, bytes, color_var)
     Polygon *poly;
     unsigned char * data;
     unsigned char * col;
     int x1, y1, x2, y2;
     int rowstride;
     int bytes;
     double color_var;
{
  double dmin_x, dmin_y;
  double dmax_x, dmax_y;
  int xs, ys;
  int xe, ye;
  int min_x, min_y;
  int max_x, max_y;
  int size_x, size_y;
  int * max_scanlines;
  int * min_scanlines;
  int col_sum[4] = {0, 0, 0, 0};
  unsigned char * d;
  int b, count;
  int i, j, y;

  count = 0;
  color_var = (rand () % 2) ? color_var * 127 : -color_var * 127;

  polygon_extents (poly, &dmin_x, &dmin_y, &dmax_x, &dmax_y);
  min_x = (int) dmin_x;
  min_y = (int) dmin_y;
  max_x = (int) dmax_x;
  max_y = (int) dmax_y;

  size_y = max_y - min_y;
  size_x = max_x - min_x;

  min_scanlines = (int *) malloc (sizeof (int) * size_y);
  max_scanlines = (int *) malloc (sizeof (int) * size_y);
  for (i = 0; i < size_y; i++)
    {
      min_scanlines[i] = max_x;
      max_scanlines[i] = min_x;
    }
  
  for (i = 0; i < poly->npts; i++)
    {
      xs = (int) ((i) ? poly->pts[i-1].x : poly->pts[poly->npts-1].x);
      ys = (int) ((i) ? poly->pts[i-1].y : poly->pts[poly->npts-1].y);
      xe = (int) poly->pts[i].x;
      ye = (int) poly->pts[i].y;

      convert_segment (xs, ys, xe, ye, min_y,
		       min_scanlines, max_scanlines);
    }

  for (i = 0; i < size_y; i++)
    {
      y = i + min_y;
      if (y >= y1 && y < y2)
	{
	  d = data + y * rowstride;

	  for (j = min_scanlines[i]; j < max_scanlines[i]; j++)
	    {
	      if (j >= x1 && j < x2)
		{
		  for (b = 0; b < bytes; b++)
		    col_sum[b] += d[j*bytes + b];
	  
		  count++;
		}
	    }
	}
    }

  if (count)
    for (b = 0; b < bytes; b++)
      {
	col_sum[b] = (int) (col_sum[b] / count + color_var);
	if (col_sum[b] > 255)
	  col[b] = 255;
	else if (col_sum[b] < 0)
	  col[b] = 0;
	else
	  col[b] = col_sum[b];
      }

  free (min_scanlines);
  free (max_scanlines);
}


static void
scale_poly (poly, tx, ty, poly_scale)
     Polygon * poly;
     double tx, ty;
     double poly_scale;
{
  polygon_translate (poly, -tx, -ty);
  polygon_scale (poly, poly_scale);
  polygon_translate (poly, tx, ty);
}


static void
fill_poly_color (poly, data, col, x1, y1, x2, y2, rowstride, bytes)
     Polygon *poly;
     unsigned char * data;
     unsigned char * col;
     int x1, y1, x2, y2;
     int rowstride;
     int bytes;
{
  double dmin_x, dmin_y;
  double dmax_x, dmax_y;
  int xs, ys;
  int xe, ye;
  int min_x, min_y;
  int max_x, max_y;
  int size_x, size_y;
  int * max_scanlines;
  int * min_scanlines;
  int * vals;
  int val;
  int pixel;
  unsigned char * d;
  int b, i, j, k, x, y;
  double contrib;
  double xx, yy;
  int supersample;
  int supersample2;

  /*  Determine antialiasing  */
  if (mvals.antialiasing)
    {
      supersample = SUPERSAMPLE;
      supersample2 = SQR (supersample);
    }
  else
    {
      supersample = supersample2 = 1;
    }

  for (i = 0; i < poly->npts; i++)
    {
      xs = (int) ((i) ? poly->pts[i-1].x : poly->pts[poly->npts-1].x);
      ys = (int) ((i) ? poly->pts[i-1].y : poly->pts[poly->npts-1].y);
      xe = (int) poly->pts[i].x;
      ye = (int) poly->pts[i].y;
      calc_spec_vec (vecs+i, xs, ys, xe, ye);
    }

  polygon_extents (poly, &dmin_x, &dmin_y, &dmax_x, &dmax_y);
  min_x = (int) dmin_x;
  min_y = (int) dmin_y;
  max_x = (int) dmax_x;
  max_y = (int) dmax_y;

  size_y = (max_y - min_y) * supersample;
  size_x = (max_x - min_x) * supersample;

  min_scanlines = (int *) malloc (sizeof (int) * size_y);
  max_scanlines = (int *) malloc (sizeof (int) * size_y);
  for (i = 0; i < size_y; i++)
    {
      min_scanlines[i] = max_x * supersample;
      max_scanlines[i] = min_x * supersample;
    }
  
  for (i = 0; i < poly->npts; i++)
    {
      xs = (int) ((i) ? poly->pts[i-1].x : poly->pts[poly->npts-1].x);
      ys = (int) ((i) ? poly->pts[i-1].y : poly->pts[poly->npts-1].y);
      xe = (int) poly->pts[i].x;
      ye = (int) poly->pts[i].y;

      xs *= supersample;
      ys *= supersample;
      xe *= supersample;
      ye *= supersample;

      convert_segment (xs, ys, xe, ye, min_y * supersample,
		       min_scanlines, max_scanlines);
    }


  vals = (int *) malloc (sizeof (int) * size_x);
  for (i = 0; i < size_y; i++)
    {
      if (! (i % supersample))
	memset (vals, 0, sizeof (int) * size_x);

      yy = (double) i / (double) supersample + min_y;

      for (j = min_scanlines[i]; j < max_scanlines[i]; j++)
	{
	  x = j - min_x * supersample;
	  vals[x] += 255;
	}

      if (! ((i + 1) % supersample))
	{
	  y = (i / supersample) + min_y;

	  if (y >= y1 && y < y2)
	    {
	      d = data + y * rowstride + min_x * bytes;

	      for (j = 0; j < size_x; j += supersample)
		{
		  x = (j / supersample) + min_x;

		  if (x >= x1 && x < x2)
		    {
		      val = 0;
		      for (k = 0; k < supersample; k++)
			val += vals[j + k];
		      val /= supersample2;

		      if (val > 0)
			{
			  xx = (double) j / (double) supersample + min_x;
			  contrib = calc_spec_contrib (vecs, poly->npts, xx, yy);

			  for (b = 0; b < bytes; b++)
			    {
			      if (contrib < 0.0)
				pixel = col[b] + (int) ((col[b] - back[b]) * contrib);
			      else
				pixel = col[b] + (int) ((fore[b] - col[b]) * contrib);

			      d[b] = ((pixel * val) + (back[b] * (255 - val))) / 255;
			    }
			}
		    }

		  d += bytes;
		}
	    }
	}
    }

  free (vals);
  free (min_scanlines);
  free (max_scanlines);
}


static void
calc_spec_vec (vec, x1, y1, x2, y2)
     SpecVec * vec;
     int x1, y1, x2, y2;
{
  double r;

  vec->base_x = x1;  vec->base_y = y1;
  r = sqrt (SQR (x2 - x1) + SQR (y2 - y1));
  if (r > 0.0)
    {
      vec->norm_x = -(y2 - y1) / r;
      vec->norm_y = (x2 - x1) / r;
    }
  else
    {
      vec->norm_x = 0;
      vec->norm_y = 0;
    }

  vec->light = vec->norm_x * light_x + vec->norm_y * light_y;
}


static double
calc_spec_contrib (vecs, n, x, y)
     SpecVec * vecs;
     int n;
     double x, y;
{
  int i;
  double dist;
  double contrib = 0;
  double x_p, y_p;

  for (i = 0; i < n; i++)
    {
      x_p = x - vecs[i].base_x;
      y_p = y - vecs[i].base_y;

      dist = fabs (x_p * vecs[i].norm_x + y_p * vecs[i].norm_y);

      if (mvals.tile_surface == ROUGH)
	{
	  /*  If the surface is rough, randomly perturb the distance  */
	  dist -= dist * ((double) rand () / (double) RAND_MAX);
	}

      /*  If the distance to an edge is less than the tile_spacing, there
       *  will be no highlight as the tile blends to background here
       */
      if (dist < 1.0)
	contrib += vecs[i].light;
      else if (dist <= mvals.tile_height)
	contrib += vecs[i].light * (1.0 - (dist / mvals.tile_height));
    }

  return contrib / 4.0;
}


static void
convert_segment (x1, y1, x2, y2, offset, min, max)
     int x1, y1;
     int x2, y2;
     int offset;
     int * min, * max;
{
  int ydiff, y, tmp;
  float xinc, xstart;

  if (y1 > y2)
    { tmp = y2; y2 = y1; y1 = tmp; 
      tmp = x2; x2 = x1; x1 = tmp; }
  ydiff = (y2 - y1);

  if ( ydiff )
    {
      xinc = (float) (x2 - x1) / (float) ydiff;
      xstart = x1 + 0.5 * xinc;
      for (y = y1 ; y < y2; y++)
	{
	  if (xstart < min[y - offset])
	    min[y-offset] = xstart;
	  if (xstart > max[y - offset])
	    max[y-offset] = xstart;

	  xstart += xinc;
	}
    }
}


static void
polygon_add_point (poly, x, y)
     Polygon *poly;
     double x, y;
{
  if (poly->npts < 12)
    {
      poly->pts[poly->npts].x = x;
      poly->pts[poly->npts].y = y;
      poly->npts++;
    }
  else
    printf ("Unable to add additional point.\n");
}


static int
polygon_find_center (poly, cx, cy)
     Polygon * poly;
     double * cx, * cy;
{
  int i;

  if (!poly->npts)
    return 0;

  *cx = 0.0;
  *cy = 0.0;

  for (i = 0; i < poly->npts; i++)
    {
      *cx += poly->pts[i].x;
      *cy += poly->pts[i].y;
    }

  *cx /= poly->npts;
  *cy /= poly->npts;

  return 1;
}


static void
polygon_translate (poly, tx, ty)
     Polygon * poly;
     double tx, ty;
{
  int i;

  for (i = 0; i < poly->npts; i++)
    {
      poly->pts[i].x += tx;
      poly->pts[i].y += ty;
    }
}


static void
polygon_scale (poly, poly_scale)
     Polygon * poly;
     double poly_scale;
{
  int i;

  for (i = 0; i < poly->npts; i++)
    {
      poly->pts[i].x *= poly_scale;
      poly->pts[i].y *= poly_scale;
    }
}

static int
polygon_extents (poly, x1, y1, x2, y2)
     Polygon * poly;
     double *x1, *y1;
     double *x2, *y2;
{
  int i;

  if (!poly->npts)
    return 0;

  *x1 = *x2 = poly->pts[0].x;
  *y1 = *y2 = poly->pts[0].y;

  for (i = 1; i < poly->npts; i++)
    {
      if (poly->pts[i].x < *x1)
	*x1 = poly->pts[i].x;
      if (poly->pts[i].x > *x2)
	*x2 = poly->pts[i].x;
      if (poly->pts[i].y < *y1)
	*y1 = poly->pts[i].y;
      if (poly->pts[i].y > *y2)
	*y2 = poly->pts[i].y;
    }

  return 1;
}


static void
polygon_reset (poly)
     Polygon * poly;
{
  poly->npts = 0;
}


static void
repair_alpha(Image image)
{
	int x1, y1, x2, y2;
	long width, height;
	long channels, rowstride;
	unsigned char *row_p, *p;
	int row, col;

	channels = gimp_image_channels (image);
	if (channels < 4)
		return;

	gimp_image_area (image, &x1, &y1, &x2, &y2);
	width = gimp_image_width (image);
	height = gimp_image_height (image);
	rowstride = width * channels;

	row_p = gimp_image_data (image);
	for (row = y1; row < y2; row++)
	{
		p = row_p + x1 * channels;

		for (col = x1; col < x2; col++)
		{
			p[3] = 255;
			p += channels;
		}
		row_p += rowstride;
	}
}


static void	Validate()
{
	mvals.tile_size = MINIMUM(MAXIMUM(mvals.tile_size, 1.0), 50.0);
	mvals.tile_height = MINIMUM(MAXIMUM(mvals.tile_height, 1.0), 25.0);
	mvals.tile_spacing = MINIMUM(MAXIMUM(mvals.tile_spacing, 1.0), 25.0);
	mvals.light_dir = MINIMUM(MAXIMUM(mvals.light_dir, 0.0), 2 * M_PI);
	mvals.randomness = MINIMUM(MAXIMUM(mvals.randomness, 0.0), 1.0);
}

