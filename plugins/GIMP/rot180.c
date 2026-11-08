
/* Rot180 1.0 -- image filter plug-in for The GIMP -- image manipulation program
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

/* This plug-in rotates the whole image by 180 degrees

*/

#include "gimp.h"

/* Declare a local function.
 */
static void rotate180 (Image, Image);

static char *prog_name;

int main (int argc, char **argv)
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

      if (input && output)
	{
	  rotate180 (input, output);
	  gimp_update_image (output);
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

static void rotate180 (Image input, Image output)
{
  long width, height;
  long channels, rowstride;
  unsigned char *src, *dest, *src_row, *dest_row;
  short x,y,c;
  int x1, y1, x2, y2;
  
  /* Get the input area. This is the bounding box of the selection in 
   *  the image (or the entire image if there is no selection).
   */
  gimp_image_area (input, &x1, &y1, &x2, &y2);

  /* Get the size of the input image. (This will/must be the same
   *  as the size of the output image.
   */
  width = gimp_image_width (input);
  height = gimp_image_height (input);
  channels = gimp_image_channels (input);

  rowstride = width * channels;

  src_row = gimp_image_data (input);
  dest_row = gimp_image_data (output);

  /* Advance the source and destination pointers to the selection
   */
  src_row += rowstride * y1 + (x1 * channels);
  dest_row += rowstride * (y2 - 1) + (x2 * channels);

	for (y = y1; y < y2; y++)
	{
		src     = src_row;
		dest    = dest_row; 

		for (x = x1; x < x2; x++)
		{
			*dest = *src++;
			
			if (channels > 1)
			{
				for (c = 1; c < channels; c++)
				{
					dest++;
					*dest = *src++;
				}
	
				dest -= (channels - 1);
		  	}
			dest -= channels;
		}
		src_row += rowstride;
		dest_row -= rowstride;
	}

  if (gimp_image_type(input) == INDEXED_IMAGE)
   gimp_set_image_colors(output,gimp_image_cmap(input),gimp_image_colors(input));

}


