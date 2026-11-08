/*
 * fade 1.00 -- filter plug-in for The GIMP
 *
 * (c) 1996 by Marc Bless, bless@ai-lab.fh-furtwangen.de
 *                         www.ai-lab.fh-furtwangen.de/~bless
 *
 * The GIMP -- an image manipulation program
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
 * This file has been modified from the original.  A dialog label was added.
 * <Scott Lindsey 7/21/98>
 */


#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include "gimp.h"

#define sqr(x) ((x)*(x))
#define MAXDIST 65535		/* hope that there's no larger image! */

#define INVALID_IMAGE_TYPE	"Fade: cannot operate on indexed or unknown image types"
#define DIALOG_TITLE				"Fade"

static void fade (Image, Image);

static void cb_ok (int, void *, void *);
static void cb_cancel (int, void *, void *);
static void cb_toggle (int, void *, void *);
static void cb_scale  (int, void *, void *);
static void saveimage (void);
static void freshen (void);
static void init_dialog (void);

static char *prog_name;

static int  dialog_ID;
static long aapply;
static long border   = 1;
static long colR     = 0; 
static long colG     = 0;
static long colB     = 0;
static long colA	 = 255;

long width, height;

Image input, output;

static unsigned char *saved;

int main (int argc, char **argv)
{
  prog_name = argv[0];

  if (!gimp_init (argc, argv))
		exit (0);

  input = 0;
  output = 0;
      
  if (!(input = gimp_get_input_image (0)))
	{
		gimp_quit ();
		exit (0);
	}
	
  if (!(output = gimp_get_output_image (0)))
	{
		gimp_free_image (input);
		gimp_quit ();
		exit (0);
	}

	switch (gimp_image_type (input))
	{
	  case RGB_IMAGE:
	  case GRAY_IMAGE:

	    saveimage ();
			init_dialog ();

	    fade (input, output);
	    gimp_update_image (output);
	    
	    if (!gimp_show_dialog (dialog_ID))
	      freshen ();
	    else
	      fade (input, output);

	    gimp_update_image (output);
	    free (saved);
	    break;

	  case INDEXED_IMAGE:
	    gimp_message (INVALID_IMAGE_TYPE);
	    break;

	  default:
	    gimp_message (INVALID_IMAGE_TYPE);
	    break;
	}

	gimp_free_image (input);
	gimp_free_image (output);

  gimp_quit ();

  return 0;
}

static void init_dialog (void)
{
	int main_ID, border_ID, colR_ID, colG_ID, colB_ID, colA_ID, aapply_ID, label_ID;
  dialog_ID = gimp_new_dialog (DIALOG_TITLE);
  main_ID   = gimp_new_row_group (dialog_ID, DEFAULT, NORMAL, "");

	label_ID    = gimp_new_label (dialog_ID, main_ID, "Border size");
	border_ID   = gimp_new_scale (dialog_ID, main_ID, 1, 100, border, 0);
	colR_ID     = gimp_new_scale (dialog_ID, main_ID, 0, 255, colR, 0);
				  gimp_new_label (dialog_ID, colR_ID, "Red:");
	colG_ID     = gimp_new_scale (dialog_ID, main_ID, 0, 255, colG, 0);
				  gimp_new_label (dialog_ID, colG_ID, "Green:");
	colB_ID     = gimp_new_scale (dialog_ID, main_ID, 0, 255, colB, 0);
				  gimp_new_label (dialog_ID, colB_ID, "Blue:");
	colA_ID     = gimp_new_scale (dialog_ID, main_ID, 0, 255, colA, 0);
				  gimp_new_label (dialog_ID, colA_ID, "Alpha:");
	    
  aapply_ID = gimp_new_check_button (dialog_ID, main_ID, "Auto Apply");
  aapply = 1;
  gimp_change_item (dialog_ID, aapply_ID, sizeof (aapply), &aapply);
  gimp_add_callback (dialog_ID, aapply_ID, cb_toggle, &aapply);

	gimp_add_callback (dialog_ID, border_ID, cb_scale, &border);
	gimp_add_callback (dialog_ID, colR_ID, cb_scale, &colR);
	gimp_add_callback (dialog_ID, colG_ID, cb_scale, &colG);
	gimp_add_callback (dialog_ID, colB_ID, cb_scale, &colB);
	gimp_add_callback (dialog_ID, colA_ID, cb_scale, &colA);
  gimp_add_callback (dialog_ID, gimp_ok_item_id (dialog_ID), cb_ok, 0);
  gimp_add_callback (dialog_ID, gimp_cancel_item_id (dialog_ID), cb_cancel, 0);
}

static void cb_toggle (int item_ID, void *client_data, void *call_data)
{
  *((long*) client_data) = *((long*) call_data);

  if (aapply)
  {
    fade (input, output);
    gimp_update_image (output);
  }
  else
  {
    freshen ();
    gimp_update_image (output);
  }
}

static void cb_scale (int item_ID, void *client_data, void *call_data)
{
	if (aapply && (*((long*) client_data) != *((long*) call_data)))
	{
	  *((long*) client_data) = *((long*) call_data);
		fade (input, output);
		gimp_update_image (output);
	}
}

static void saveimage (void)
{
  saved = (unsigned char *) malloc (gimp_image_width (input) *
  				gimp_image_height (input) *
				  gimp_image_channels (input));
  memcpy (saved, gimp_image_data (input),
  		 		gimp_image_width(input) *
		 			gimp_image_height (input) *
		 			gimp_image_channels (input));
}

static void freshen (void)
{
  memcpy (gimp_image_data (output), saved, 
					gimp_image_width (input) *
  				gimp_image_height (input) *
					gimp_image_channels (input));
}

static void cb_ok (int item_ID, void *client_data, void *call_data)
{
  gimp_close_dialog (dialog_ID, 1);
}

static void cb_cancel (int item_ID, void *client_data, void *call_data)
{
  gimp_close_dialog (dialog_ID, 0);
}

static void fade (Image linput, Image loutput)
{
  long channels, rowstride;
  unsigned char *src_row, *dest_row;
  unsigned char *src, *dest;
  short row, col;
  int x1, y1, x2, y2;
  int i;
	long dist;
	int val;
  
  gimp_image_area (linput, &x1, &y1, &x2, &y2);

  width     = gimp_image_width (linput);
  height    = gimp_image_height (linput);
  channels  = gimp_image_channels (linput);
  rowstride = width * channels;

  src_row   = saved;
  dest_row  = gimp_image_data (loutput);

  src_row  += rowstride * y1 + x1 * channels;
  dest_row += rowstride * y1 + x1 * channels;

  for (row = y1; row < y2; row++)
  {
    src  = src_row;
    dest = dest_row;

    for (col = x1; col < x2; col++)
    {
			dist = MAXDIST;
			if (row - y1 <= border) dist = row - y1; 
			if ((y2 - row <= border) && (y2 - row < dist)) dist = y2 - row; 
			if ((col - x1 <= border) && (col - x1 < dist)) dist = col - x1;
			if ((x2 - col <= border) && (x2 - col < dist)) dist = x2 - col; 
			if (dist < MAXDIST)
			{
				val     = (*src++ * dist + colR * (border - dist)) / border;
				*dest++ = (val > 255) ? 255 : val;
				if (channels >= 3)
				{
					val     = (*src++ * dist + colG * (border - dist)) / border;
					*dest++ = (val > 255) ? 255 : val;
					val     = (*src++ * dist + colB * (border - dist)) / border;
					*dest++ = (val > 255) ? 255 : val;
				}
				if (channels == 4)
				{
					val     = (*src++ * dist + colA * (border - dist)) / border;
					*dest++ = (val > 255) ? 255 : val;
				}
			}
			else
				for (i = 0; i < channels; i++)
	  			*dest++ = *src++; 
    }

    src_row  += rowstride;
    dest_row += rowstride;
  }
}

