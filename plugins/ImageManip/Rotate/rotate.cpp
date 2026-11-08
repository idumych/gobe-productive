/* The GIMP -- an image manipulation program
 * Copyright (C) 1995 Spencer Kimball and Peter Mattis
 */

/* This file has been modified from the original; converted to the ImageManip API */
/* Scott Lindsey 8/1/99 */


/* This program is free software; you can redistribute it and/or modify
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
 *  This filter rotates an image, creating a new image.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "GIMPGlue.h"
#include "gimp.h"
#include "ImageManipAddon.h"
#include "BitmapAccessor.h"
#include "Config.h"

const char addonName[] = "Rotate";
const char addonInfo[] = "Arbitrarily rotate an image, optionally sharpening it. Ported from the GIMP by Scott Lindsey <wombat@gobe.com>.";
const char addonCategory[] = "Transform";
int32 addonVersion = 100;

/* define the precision specified in the angle measurement  */
#define ANGLE          0
#define SHARPEN        1

#define PRECISION      100
#define EPSILON        0.00001

/* Declare a local function.
 */
static Image rotate(Image input, float angle, bool sharpen);

/*  TempBuf definitions...  */
typedef struct _temp_buf TempBuf;

struct _temp_buf
{
  int             bytes;         /*  The necessary info  */
  int             width;
  int             height;
  int             x, y;          /*  origin of data source  */

  unsigned char * data;          /*  The data buffer     */
};


/*  The temp buffer functions  */
TempBuf *   temp_buf_new        (int, int, int, int, int, unsigned char *);
void        temp_buf_free       (TempBuf *);



status_t Convert(
	BitmapAccessor *sourceBitmap,
	BitmapAccessor *destBitmap,   /* call CreateBitmap() on this */
	BMessage *ioExtension,        /* can be NULL */
	bool checkOnly)
{
	Prefs prefs;
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

	LoadPrefs(&prefs);

	gimp_init_glue(sourceBitmap, destBitmap, ioExtension);

	if (ioExtension)
	{
		double angle;
		if (!ioExtension->FindDouble("rotate/angle", &angle))
			prefs.angle = angle;
		bool sharpen;
		if (!ioExtension->FindBool("rotate/sharpen", &sharpen))
			prefs.sharpen = sharpen;
	}

	Image input = new GIMPImage(sourceBitmap);
	
	rotate(input, prefs.angle, prefs.sharpen);

    delete input;
	
	return B_OK;
}



#if 0
int
main (argc, argv)
     int argc;
     char **argv;
{
  Image input, output;
  char buf[64];
  int group_ID;
  int temp_ID;
  void *data;

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
      switch (gimp_image_type (input))
	{
	case RGB_IMAGE:
	case GRAY_IMAGE:
	  orig_image_type = gimp_image_type (input);
	  break;
	case INDEXED_IMAGE:
	  gimp_message ("rotate: cannot operate on indexed color images");
	  gimp_quit ();
	  break;
	default:
	  gimp_message ("rotate: cannot operate on unknown image types");
	  gimp_quit ();
	  break;
	}
      
      data = gimp_get_params ();
      if (data)
	{
	  params [ANGLE] = ((int32*) data) [ANGLE];
	  params [SHARPEN] = ((int32*) data) [SHARPEN];
	}
      else
	{
	  params [ANGLE] = 0;
	  params [SHARPEN] = 1;
	}

      dialog_ID = gimp_new_dialog ("Rotate");
      group_ID = gimp_new_row_group (dialog_ID, DEFAULT, NORMAL, "");

      temp_ID = gimp_new_column_group (dialog_ID, group_ID, NORMAL, "");
      gimp_new_label (dialog_ID, temp_ID, "Angle:");
      sprintf (buf, "%.2f", (float) params [ANGLE] / PRECISION);
      text_angle_ID = gimp_new_text (dialog_ID, temp_ID, buf);

      slider_angle_ID = gimp_new_scale (dialog_ID, group_ID,
					-180 * PRECISION, 180 * PRECISION, 
					params [ANGLE], 2);

      sharpen_ID = gimp_new_check_button (dialog_ID, group_ID, "Sharpen Image");
      gimp_change_item (dialog_ID, sharpen_ID, sizeof (int32), &params [SHARPEN]);

      gimp_add_callback (dialog_ID, text_angle_ID, text_callback, &params [ANGLE]);
      gimp_add_callback (dialog_ID, sharpen_ID, toggle_callback, &params [SHARPEN]);
      gimp_add_callback (dialog_ID, slider_angle_ID, scale_callback, &params [ANGLE]);
      gimp_add_callback (dialog_ID, gimp_ok_item_id (dialog_ID), ok_callback, 0);
      gimp_add_callback (dialog_ID, gimp_cancel_item_id (dialog_ID), cancel_callback, 0);

      if (gimp_show_dialog (dialog_ID))
	{
	  gimp_set_params (sizeof (int32) * 3, params);
	  
	  if (input)
	    {
	      output = rotate (input);
	      if (output)
		{
		  gimp_display_image (output);
		  gimp_update_image (output);
		}
	    }
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
text_callback (item_ID, client_data, call_data)
     int item_ID;
     void *client_data;
     void *call_data;
{
  float angle;

  angle = atof (call_data);

  /*  some bounds checking  */
  while (angle > 180.0)
    angle -= 360.0;
  while (angle < -180.0)
    angle += 360.0;

  *((int32 *) client_data) = (int32) (angle * PRECISION);

  gimp_change_item (dialog_ID, slider_angle_ID, sizeof (int32), &params [ANGLE]);
}

static void
toggle_callback (item_ID, client_data, call_data)
     int item_ID;
     void *client_data;
     void *call_data;
{
  *((int32*) client_data) = *((int32*) call_data);
}
 

static void
scale_callback (item_ID, client_data, call_data)
     int item_ID;
     void *client_data;
     void *call_data;
{
  char buf[64];
  *((int32*) client_data) = *((int32*) call_data);

  sprintf (buf, "%.2f", (float) params [ANGLE] / PRECISION);
  
  gimp_change_item (dialog_ID, text_angle_ID, strlen (buf) + 1, buf);
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

/*****************************************************************/
/*    Functions for implementing rotation...                     */
/*****************************************************************/


static TempBuf *
rotate_image_90_180_270 (Image image, float * degrees)
{
  TempBuf * newBuf;
  unsigned char * src, * s;
  unsigned char * dest, * d;
  int width, height;
  int new_width, new_height;
  int bytes, b;
  int rowstride_src, rowstride_dest;
  
  int x, y;

  /*
   *  This procedure rotates the input image by either 90, 180, or 270 degrees.
   *  This process is necessary because the standard 3 step raster rotation
   *  works best for rotations between -45 and 45 degrees.
   */

  width = gimp_image_width (image);
  height = gimp_image_height (image);
  bytes = gimp_image_channels (image);

  if (*degrees >= (-0.25 * M_PI) && *degrees <= (0.25 * M_PI))
    {
      new_width = width;
      new_height = height;
      newBuf = temp_buf_new (new_width, new_height, bytes, 0, 0, NULL);

      src = gimp_image_data (image);
      rowstride_src = gimp_image_stride(image);

      dest = newBuf->data;
      rowstride_dest = new_width * bytes;

      for (y = 0; y < new_height; y++)
	{
	  s = src;
	  d = dest;

	  for (x = 0; x < new_width; x++)
	    {
	      b = bytes;
	      while (b--)
		*d++ = *s++;
	    }

	  src += rowstride_src;
	  dest += rowstride_dest;
	}
    }
  else if (*degrees > (0.25 * M_PI) && *degrees < (0.75 * M_PI))
    {
      /*  for 90 degrees, x = y & y = x  */
      new_width = height;
      new_height = width;
      newBuf = temp_buf_new (new_width, new_height, bytes, 0, 0, NULL);

      src = gimp_image_data (image);
      rowstride_src = gimp_image_stride(image);
      src += rowstride_src * (height - 1);
      dest = newBuf->data;
      rowstride_dest = new_width * bytes;

      for (y = 0; y < new_height; y++)
	{
	  s = src;
	  d = dest;

	  for (x = 0; x < new_width; x++)
	    {
	      for (b = 0; b < bytes; b++)
		*d++ = s[b];
	      s -= rowstride_src;
	    }

	  src += bytes;
	  dest += rowstride_dest;
	}

      *degrees -= 0.5 * M_PI;
      
    }
  else if (*degrees >= (0.75 * M_PI) || *degrees <= (-0.75 * M_PI))
    {
      /*  for 180 degrees, x = -x & y = -y  */
      new_width = width;
      new_height = height;
      newBuf = temp_buf_new (new_width, new_height, bytes, 0, 0, NULL);

      src = gimp_image_data (image);
      rowstride_src = gimp_image_stride(image);
      src += rowstride_src * height - bytes;
      dest = newBuf->data;
      rowstride_dest = new_width * bytes;

      for (y = 0; y < new_height; y++)
	{
	  s = src;
	  d = dest;

	  for (x = 0; x < new_width; x++)
	    {
	      for (b = 0; b < bytes; b++)
		*d++ = s[b];

	      s -= bytes;
	    }

	  src -= rowstride_src;
	  dest += rowstride_dest;
	}
      if (*degrees < 0)
	*degrees += M_PI;
      else
	*degrees -= M_PI;
    }
  else if (*degrees > (-0.75 * M_PI) && *degrees < (-0.25 * M_PI))
    {
      /*  for 270 degrees, x = y & y = -x */
      new_width = height;
      new_height = width;
      newBuf = temp_buf_new (new_width, new_height, bytes, 0, 0, NULL);

      src = gimp_image_data (image);
      rowstride_src = gimp_image_stride(image);
      src += rowstride_src - bytes;
      dest = newBuf->data;
      rowstride_dest = new_width * bytes;

      for (y = 0; y < new_height; y++)
	{
	  s = src;
	  d = dest;

	  for (x = 0; x < new_width; x++)
	    {
	      for (b = 0; b < bytes; b++)
		*d++ = s[b];
	      s += rowstride_src;
	    }

	  src -= bytes;
	  dest += rowstride_dest;
	}

      *degrees += 0.5 * M_PI;
    }

  else
    return NULL;

  return newBuf;
}


static TempBuf *
offset_image (TempBuf * buf, int offset_x, int offset_y)
{
  TempBuf * newBuf;
  unsigned char * src;
  unsigned char * dest;
  int rowstride_src, rowstride_dest;
  int y;

  unsigned char color[4] = { 128, 128, 128, 0 };  /* a grey, transparent background */

  newBuf = temp_buf_new (buf->width + offset_x, buf->height + offset_y,
		      buf->bytes, 0, 0, color);


  rowstride_src = buf->width * buf->bytes;
  rowstride_dest = newBuf->width * newBuf->bytes;

  src = buf->data;
  dest = newBuf->data + newBuf->bytes * (newBuf->width * offset_y + offset_x);

  for (y = 0; y < buf->height; y++)
    {
      /*  copy a line of the src image to the new offset image  */
      memcpy (dest, src, rowstride_src);
      
      src += rowstride_src;
      dest += rowstride_dest;
    }
  return newBuf;
}


static Image
crop_image (TempBuf * buf, int x1, int y1, int x2, int y2)
{
  Image newImage;
  unsigned char * src;
  unsigned char * dest;
  int rowstride_src, rowstride_dest;
  int i;

  /*  create the new buffer based on this size  */
  newImage = gimp_new_image (0, (x2 - x1), (y2 - y1), DEFAULT_IMAGE);

  if (!newImage) return NULL;

  rowstride_src = buf->bytes * buf->width;

  src = buf->data + buf->bytes * (y1 * buf->width + x1);
  dest = gimp_image_data (newImage);
  rowstride_dest = gimp_image_stride (newImage);

  for (i = y1; i < y2; i++)
    {
      memcpy (dest, src, rowstride_dest);
      
      src += rowstride_src;
      dest += rowstride_dest;
    }

  return newImage;
}


static Image
crop_sheared_image (TempBuf * buf, float shear_x, float shear_y, int width, int height)
{
  float x[4], y[4];
  float x_min, x_max, y_min, y_max;
  int x_offset1, y_offset, x_offset2;
  int x1, y1, x2, y2;
  int i;

  x_offset1 = (int) ceil ((float) height * fabs (shear_x));
  y_offset  = (int) ceil ((float) (width + x_offset1) * fabs (shear_y));
  x_offset2 = (int) ceil ((float) (height + y_offset) * fabs (shear_x));

  x[0] = x[2] = (float) x_offset1 + x_offset2;
  y[0] = y[1] = (float) y_offset;
  x[1] = x[3] = (float) x[0] + width;
  y[2] = y[3] = (float) y[0] + height;

  for (i = 0; i < 4; i++)
    {
      if (shear_x > 0)
	x[i] -= shear_x * (height - (y[i] - y_offset));
      else
	x[i] += shear_x * (y[i] - y_offset);
      if (shear_y > 0)
	y[i] -= shear_y * (width + x_offset1 - (x[i] - x_offset2));
      else
	y[i] += shear_y * (x[i] - x_offset2);
      if (shear_x > 0)
	x[i] -= shear_x * (height + y_offset - y[i]);
      else
	x[i] += shear_x * y[i];
    }


  x_min = x_max = x[0];
  y_min = y_max = y[0];
  
  for (i = 1; i < 4; i++)
    {
      if (x[i] < x_min)
	x_min = x[i];
      else if (x[i] > x_max)
	x_max = x[i];
      if (y[i] < y_min)
	y_min = y[i];
      else if (y[i] > y_max)
	y_max = y[i];
    }

  x1 = (int) floor (x_min);
  y1 = (int) floor (y_min);
  x2 = (int) ceil (x_max);
  y2 = (int) ceil (y_max);

  return crop_image (buf, x1, y1, x2, y2);
}


static TempBuf *
sharpen_image (TempBuf * buf)
{
  TempBuf * newBuf;
  unsigned char * src, * s, * s0, * s1, * s2;
  unsigned char * dest, * d;
  int total;
  int b, bytes, bytesx2;
  int rowstride;
  int x, y;

  /*  check for the boundary cases  */
  if (buf->width < 2 || buf->height < 2)
    return NULL;

  /*  create the new buffer based on this size  */
  newBuf = temp_buf_new (buf->width, buf->height, buf->bytes, 0, 0, NULL);

  rowstride = buf->bytes * buf->width;

  bytes = buf->bytes;
  bytesx2 = bytes * 2;
  src = buf->data;
  dest = newBuf->data;

  /* copy the first scanline to the new image... */
  memcpy (dest, src, rowstride);

  /* increment the source and dest pointers... */
  s0 = src;
  src += rowstride;
  dest += rowstride;

  for (y = 1; y < buf->height - 1; y++)
    {
      s1 = src;
      s2 = src + rowstride;

      d = dest;
      s = src;

      /* handle the first pixel... */
      b = bytes;
      while (b--)
	*d++ = *s++;

      /* now, handle the center pixels */
      x = buf->width - 2;
      while (x--)
	{
	  for (b = 0; b < bytes; b++)
	    {
	      total =    - s0[b] -2  * s0[b + bytes]    - s0[b + bytesx2]
	              -2 * s1[b] +28 * s1[b + bytes] -2 * s1[b + bytesx2]
			 - s2[b] -2  * s2[b + bytes]    - s2[b + bytesx2];

	      total = ((total + 7) >> 4);
	      if (total < 0)
		*d++ = 0;
	      else
		if (total > 255)
		  *d++ = 255;
		else
		  *d++ = (unsigned char) total;
	    }
	  
	  s0 += bytes;
	  s1 += bytes;
	  s2 += bytes;
	  s += bytes;
	}

      /* handle the last pixel... */
      b = bytes;
      while (b--)
	*d++ = *s++;

      /* set the memory pointers */
      s0 = src;
      src += rowstride;
      dest += rowstride;
    }

  /* copy the last scanline to the new image... */
  memcpy (dest, src, rowstride);

  return newBuf;
}


static void
xshear_image (TempBuf * buf, int x_offset, int y_offset, float shear)
{
  unsigned char * src, * s;
  unsigned char * dest, * d;
  unsigned char * prev;
  int rowstride;
  int y, ymax;
  int x, xmax;
  float startx;
  float frac1, frac2;

  ymax = buf->height - y_offset;
  xmax = buf->width - x_offset;

  /*  shear the image by "shear" in the X direction  */
  if (shear > 0)
      startx = x_offset - shear * ymax;
  else
      startx = x_offset;

  rowstride = buf->bytes * buf->width;
  src = buf->data + buf->bytes * (y_offset * buf->width + x_offset);

  /*  advance the x shear one half a pixel down so we can
   *  calculate from the middle of each row
   */
  startx += shear / 2.0;

  /*  the destination should start at one row before the src
   *  so that we have room to actually shear into without
   *  overwriting pixels we will need to read
   */
  for (y = y_offset - 1; y < buf->height - 1; y++)
    {
      dest = buf->data + buf->bytes * (y * buf->width + (int) startx);

      frac1 = startx - floor (startx);
      frac2 = 1.0 - frac1;

      /*  prev should point to one pixel before the "src"  */
      prev = src - buf->bytes;

      s = src;
      d = dest;

      /*  shift the row over by amount shear  */
      x = xmax * buf->bytes;
      while (x--)
	*d++ = (unsigned char) ((*prev++) * frac1 + (*s++) * frac2);

      /*  fill the rest of the row with the background  */
      x = (buf->width - (int) startx - xmax) * buf->bytes;
      while (x--)
	*d++ = 0;

      startx += shear;
      src += rowstride;
    }

  /*  fill in the row which is left behind after we shift the image
   *  up one row as we shear it...
   */
  x = xmax * buf->bytes;
  src -= rowstride;
  while (x--)
    *src++ = 0;

}


static void
yshear_image (TempBuf * buf, int x_offset, int y_offset, float shear)
{
  unsigned char * src, * s;
  unsigned char * dest, * d;
  unsigned char * prev;
  int rowstride;
  int bytes, b;
  int y, ymax;
  int x, xmax;
  float starty;
  float frac1, frac2;

  ymax = buf->height - y_offset;
  xmax = buf->width - x_offset;

  /*  shear the image by "shear" in the Y direction  */
  if (shear > 0)
      starty = y_offset - shear * xmax;
  else
      starty = y_offset;

  rowstride = buf->bytes * buf->width;
  bytes = buf->bytes;
  src = buf->data + buf->bytes * (y_offset * buf->width + x_offset);

  /*  advance the y shear one half a pixel over so we can
   *  calculate from the middle of each column
   */
  starty += shear / 2.0;

  /*  the destination should start at one row before the src
   *  so that we have room to actually shear into without
   *  overwriting pixels we will need to read
   */
  for (x = x_offset - 1; x < buf->width - 1; x++)
    {
      dest = buf->data + buf->bytes * ((int) starty * buf->width + x);

      frac1 = starty - floor (starty);
      frac2 = 1.0 - frac1;

      /*  prev should point to one pixel before the "src"  */
      prev = src - rowstride;

      s = src;
      d = dest;

      /*  shift the column up by amount shear  */
      y = ymax;
      while (y--)
	{
	  for (b = 0; b < bytes; b++)
	    d[b] = (unsigned char) (prev[b] * frac1 + s[b] * frac2);
	  d += rowstride;
	  s += rowstride;
	  prev += rowstride;
	}

      /*  fill the rest of the column with the background  */
      y = buf->height - (int) starty - ymax;
      while (y--)
	{
	  for (b = 0; b < bytes; b++)
	    d[b] = 0;
	  d += rowstride;
	}

      starty += shear;
      src += bytes;
    }

  /*  fill in the column which is left behind after we shift the image
   *  over one column as we shear it...
   */
  y = ymax;
  src -= bytes;
  while (y--)
    {
      for (b = 0; b < bytes; b++)
	src[b] = 0;
      src += rowstride;
    }
}


Image
rotate (Image input, float angle, bool sharpen)
{
  Image output;
  TempBuf * image, * new_image;
  float shear_x, shear_y;
  int x_offset1, x_offset2, y_offset;
  int width, height;
  int current_seg = 1;
  int max_seg;
  
  while (angle > 180.0)
    angle -= 360.0;
  while (angle < -180.0)
    angle += 360.0;

  /*  convert angle measure to radians  */
  angle = M_PI * angle / 180.0;

  /*  Based on the angle, rotate by an integral multiple of 90 degrees  */
  new_image = rotate_image_90_180_270 (input, &angle);

  /*  calculate the number of segments in the progress bar  */
  if (fabs (angle) > EPSILON)
    {
      if (!sharpen)
	max_seg = 5;
      else
	max_seg = 6;
    }
  else
    max_seg = 2;

  gimp_do_progress (current_seg++, max_seg);

  width = new_image->width;
  height = new_image->height;
  shear_x = shear_y = 0.0;

  if (fabs (angle) > EPSILON)
    {
      /*  calculate the shear values  */
      shear_x = -tan (angle / 2.0);
      shear_y = sin (angle);

      /*  calculate the extra space we will need for shears... */
      x_offset1 = (int) ceil ((float) height * fabs (shear_x));
      y_offset  = (int) ceil ((float) (width + x_offset1) * fabs (shear_y));
      x_offset2 = (int) ceil ((float) (height + y_offset) * fabs (shear_x));
      
      /*  create a new offset image with the extra space  */
      image = new_image;
      new_image = offset_image (image, (x_offset1 + x_offset2) + 1, y_offset + 2);
      temp_buf_free (image);

      gimp_do_progress (current_seg++, max_seg);

      /*  shear the image in the X direction  */
      xshear_image (new_image, (x_offset1 + x_offset2) + 1, y_offset + 2, shear_x);

      gimp_do_progress (current_seg++, max_seg);

      /*  shear the image in the Y direction  */
      yshear_image (new_image, x_offset2, y_offset + 1, shear_y);

      gimp_do_progress (current_seg++, max_seg);

      /*  shear the image in the X direction  */
      xshear_image (new_image, x_offset2, 1, shear_x);

      gimp_do_progress (current_seg++, max_seg);
    
      /*  sharpen the final image  */
      if (sharpen)
	{
	  image = new_image;
	  if (!!(new_image = sharpen_image (image)))
	    temp_buf_free (image);
	  else
	    new_image = image;

	  gimp_do_progress (current_seg++, max_seg);
	}

    }
 
  /*  crop the image buffer based on calculated crop extents  */
  image = new_image;
  output = crop_sheared_image (image, shear_x, shear_y, width, height);
  temp_buf_free (image);

  gimp_do_progress (current_seg++, max_seg);

  return output;
}



/***************************************************************/
/*     Temp buf function definitions                           */
/***************************************************************/

TempBuf * 
temp_buf_new (int width, int height, int bytes, int x, int y, unsigned char * col)
{
  int32 i;
  int j;
  unsigned char * init, * data;
  TempBuf * temp;

  temp = (TempBuf *) malloc (sizeof (TempBuf));

  temp->width  = width;
  temp->height = height;
  temp->bytes  = bytes;
  temp->x      = x;
  temp->y      = y;
  temp->data   = (unsigned char *) malloc (sizeof (unsigned char) * width * height * bytes);

  /*  initialize the data  */
  if (col)
    {
      i = width * height;
      data = temp->data;
      
      while (i--)
	{
	  j = bytes;
	  init = col;
	  while (j--)
	    *data++ = *init++;
	}
    }
  
  return temp;
}


void
temp_buf_free (TempBuf * temp_buf)
{
  if (temp_buf->data)
    free (temp_buf->data);

  free (temp_buf);
}

