
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
#include <stdio.h>
#include <stdlib.h>
#include "gimp.h"

/* Declare a local function.
 */
static void prep4tile (Image, Image);

/* prep4tile: transform borders colours, such that tiling image is seamless */

static char *prog_name;

typedef struct {
  long cdiam,ddiam;
} Data;

Data D;

/* Dialog stuff */
static int d,g,i;
static void call_me(int i,void *l,void *a)
{  *((long*)l)=*((long*)a);  }
static void close_d(int i,void *l,void *a)
{ gimp_close_dialog(d,(int)l); }

int
main (argc, argv)
     int argc;
     char **argv;
{
  Image input, output;
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
      /* This is a regular filter. What that means is that it operates
       *  on the input image. Output is put into the ouput image. The
       *  filter should not worry, or even care where these images come
       *  from. The only guarantee is that they are the same size and
       *  depth.
       */
      input = gimp_get_input_image (0);
      output = gimp_get_output_image (0);

      /* If both an input and output image were available, then do some
       *  work. Then update the output image.
       */
      if (input && output)
	{
	  if ((gimp_image_type (input) == RGB_IMAGE) ||
	      (gimp_image_type (input) == GRAY_IMAGE))
	    {
	      if((data=gimp_get_params()))
		D=*(Data*)data;
	      else {
		D.cdiam=20; D.ddiam=40; }

	      d=gimp_new_dialog("Prepare for Tile");
	      g=gimp_new_row_group(d,DEFAULT,NORMAL,"");
	      gimp_new_label(d,g,"Diameter for colour average");
 	      i=gimp_new_scale(d,g,1,40,D.cdiam,0);
	      gimp_add_callback(d,i,call_me,&D.cdiam);
	      gimp_new_label(d,g,"Diameter for effect");
	      i=gimp_new_scale(d,g,1,80,D.ddiam,0);
	      gimp_add_callback(d,i,call_me,&D.ddiam);

	      gimp_add_callback(d,gimp_ok_item_id(d),close_d,(void*)1);
	      gimp_add_callback(d,gimp_cancel_item_id(d),close_d,0);

	      if(gimp_show_dialog(d)) {
		gimp_set_params(sizeof(Data),&D);

		gimp_init_progress ("4tile");
		prep4tile(input, output);
		gimp_update_image (output);
	      }
	    }
	  else
	    gimp_message ("4tile: cannot operate on indexed color images");
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

static int
calculate_factor (unsigned char *data,int nch,int ch,int rs,int wd,int ht,
		  int x, int y,int xr,int yr)
{
  int i,j;
  long sum=0;
  for(j=y-yr;j<=y+yr;j++)
    for(i=x-xr;i<=x+xr;i++)
      sum=sum+data[(j+((j<0)?ht:(j>=ht)?-ht:0))*rs+ch+
		  (i+((i<0)?wd:(i>=wd)?-wd:0))*nch];
  return(sum/(2*xr+1)/(2*yr+1));
}

static void
prep4tile (input, output)
     Image input, output;
{
  long width, height;
  long channels, rowstride,rowstride1;
  unsigned char *dest,*d;
  unsigned char *source,*s;
  int *xt,*yl;
  short row, col;
  int x1, y1, x2, y2;
  int ii;

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
  rowstride1 = rowstride - (x2-x1)*channels;

  source = gimp_image_data (input);
  dest = (unsigned char*)gimp_image_data (output)+y1*rowstride;

  xt=(int*)malloc(sizeof(int)*(1+width));
  yl=(int*)malloc(sizeof(int)*(1+height));

  for(ii=0;ii<channels;ii++)
    {
      /* initialize arrays */
#define R D.cdiam
      for(row=y1;row<y2-1;row++)
	yl[row]=calculate_factor(source,channels,ii,rowstride,width-1,height-1,
				   0,row,R,R);
      for(col=x1;col<x2;col++)
	xt[col]=calculate_factor(source,channels,ii,rowstride,width-1,height-1,
				   col,0,R,R);

      d=dest+ii+x1*channels; s=source+y1*rowstride+ii+x1*channels;
      for (row=y1;row<y2;row++)
	{
	  for (col=x1;col<x2;col++)
	    {
	      d[0]=s[0];
#define R1 D.ddiam
	      if(col<R1+x1) d[0]+=(yl[row]-d[0])*(R1-(col-x1))/R1;
	      if(row<R1+y1) d[0]+=(xt[col]-d[0])*(R1-(row-y1))/R1;
	      if(col>x2-R1) d[0]+=(yl[row]-d[0])*(R1-(x2-col))/R1;
	      if(row>y2-R1) d[0]+=(xt[col]-d[0])*(R1-(y2-row))/R1;
	      d+=channels; s+=channels;
	    }	
	  d+=rowstride1; s+=rowstride1;

	  if (!(row &15) == 0)
	    gimp_do_progress (row+(y2-y1)*ii, (y2 - y1)*channels);
	}
    }
}

