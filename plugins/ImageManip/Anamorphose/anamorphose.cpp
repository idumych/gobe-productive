/**********************************************************************
 *  Conical Anamorphose Distortion Plug-In (Version 1.03a)
 *  Daniel Cotting (cotting@multimania.com)
 **********************************************************************
 *  Official homepages: http://www.multimania.com/cotting
 *                      http://cotting.citeweb.net
 **********************************************************************    
 */

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
 *
 *
 * MODIFIED FROM THE ORIGINAL - 7/24/99 Scott Lindsey
 */

#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#include "ImageManipAddon.h"
#include "BitmapAccessor.h"
#include "Config.h"
#include "GIMPGlue.h"

const char addonName[] = "Anamorphose";
const char addonInfo[] = "Apply an anamorphose effect (conic reflection). Original GIMP plugin by Daniel Cotting; ported by Scott Lindsey";
const char addonCategory[] = "Effects";
int32 addonVersion = 100;

static status_t drawanamorphose(BitmapAccessor* sourceBitmap, BRect sel_rect, BInvoker* prog, const Prefs* prefs);

status_t
Manipulate(
	BitmapAccessor *sourceBitmap,
	BMessage *ioExtension,
	bool checkOnly)
{
	Prefs prefs;

	switch(sourceBitmap->ColorSpace())
	{
		case B_RGBA32_BIG:
		case B_RGB32_BIG:
		case B_RGB24_BIG:
		case B_RGBA32_LITTLE:
		case B_RGB32_LITTLE:
		case B_RGB24_LITTLE:
		case B_GRAYSCALE_8_BIT:
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

	return drawanamorphose(sourceBitmap, sel_rect, prog, &prefs);
}

static status_t
drawanamorphose(BitmapAccessor* sourceBitmap, BRect sel, BInvoker* prog, const Prefs* prefs)
{
  status_t result = B_OK;
  GIMPImage srcPR(sourceBitmap);
  int32 width, height;
  int32 bytes;
  double abstand;
  double radius;
  double angle, alpha, gamma, abstandvonzentrum, distanzzukreis, pi ,seiteb, verhaltniss, a1,a2,a3,a4,faktor,a_tot;
  int32 row;
  int32 x1, y1, x2, y2, ix, iy;
  const uchar *src;
  uchar *dest;
  int32 i, col, succeeded;
  float regionwidth, regionheight, dx, dy, xsqr, ysqr;
  float a, b, asqr, bsqr, x, y;
  int32 pixelpos, pos;
  uchar bgr_red, bgr_blue, bgr_green, alphaval;
  color_space drawtype = sourceBitmap->ColorSpace();

  gimp_background_color(&bgr_red, &bgr_green, &bgr_blue);

	x1 = (int) sel.left;
	x2 = (int) sel.right;
	y1 = (int) sel.top;
	y2 = (int) sel.bottom;

  regionwidth = x2-x1 + 1;
  a = regionwidth/2;
  regionheight = y2-y1 + 1;
  b = regionheight/2;

  asqr = a*a;
  bsqr = b*b;

  radius=prefs->cone_radius;
  angle=prefs->base_angle;
  
  width = (int32) sourceBitmap->Bounds().Width() + 1;
  height = (int32) sourceBitmap->Bounds().Height() + 1;
  bytes = (int32) srcPR.bpp;

  int32 size = (int32) (regionwidth*regionheight*bytes);

  src = (const uchar*) malloc(size);
  dest = (uchar*) malloc(size);
  srcPR.GetRect((uchar*) src, x1, y1, regionwidth, regionheight);
  for(col = 0; col < regionwidth; col++) {
    dx = (float)col - a+0.5;
    xsqr = dx*dx;
    for(row = 0; row < regionheight; row++) {
      pixelpos = (int32) (col+row*regionwidth)*bytes;
      dy = -((float)row - b-0.5);
      ysqr = dy*dy;
      abstand=(sqrt(dx*dx+dy*dy));
      succeeded=1;
      if (abstand>radius){ 
	alpha=2.0*angle-90.0;
	gamma=90.0-angle;
	distanzzukreis=abstand-radius;
	pi=atan(1.0)*4.0;
	seiteb=sin(alpha/180.0*pi)/sin(gamma/180.0*pi)*distanzzukreis;
        abstandvonzentrum=radius-(sin(gamma/180.0*pi)*seiteb);
	if ((abstandvonzentrum<0) && (prefs->dedouble==0)) { 
	  succeeded=0;
	}else{  
	  verhaltniss=abstandvonzentrum/abstand;
          if (prefs->flip) dy = -dy;
          x = dx*verhaltniss+a;
	  y = dy*verhaltniss+b;
	  ix=(int)x;
	  iy=(int)y;
	  if (prefs->use_antialias) {
	     if ((ix - x)<0){
               if ((iy - y)<0){} else {iy-=1;}
	     } else{
               if ((iy - y)<0){ix-=1;} else {iy-=1;ix-=1;}
	     }
             a1=1/((ix-x)*(ix-x)+(iy-y)*(iy-y));   
             a2=1/((ix+1-x)*(ix+1-x)+(iy-y)*(iy-y));   
             a3=1/((ix-x)*(ix-x)+(iy+1-y)*(iy+1-y));   
             a4=1/((ix+1-x)*(ix+1-x)+(iy+1-y)*(iy+1-y));
             a_tot=a1+a2+a3+a4;
             faktor=1/a_tot;
             a1*=faktor;
             a2*=faktor;
             a3*=faktor;
             a4*=faktor;
	
             pos = (int32)(((int32)(iy)*regionwidth + (int32)(ix)) * bytes);

	     for (i = 0; i < bytes; i++) {
	       dest[pixelpos+i] = (int32)((double)(src[pos+i])*a1+(double)(src[pos+i+bytes])*a2+(double)(src[pos+i+(int32)(regionwidth*bytes)])*a3+(double)(src[pos+i+(int32)(regionwidth*bytes)+bytes])*a4);
	     }
	   } else {
	     pos = (int32)(((int32)(iy)*regionwidth + (int32)(ix)) * bytes);
	     for(i = 0; i < bytes; i++) {
	        dest[pixelpos+i] = src[pos+i];
	     }
	  }
	}
	} else {
	  succeeded=0;
	}
	if (succeeded==0){
	if(prefs->keep_surr) {
	  for(i = 0; i < bytes; i++) {
	    dest[pixelpos+i] = src[pixelpos+i];
	  }
	}
	else {
	  if(prefs->set_transparent) alphaval = 0;
	  else alphaval = 255;

	  switch(drawtype) {
	  case B_RGBA32_BIG:
	    dest[pixelpos+0] = alphaval;
	  case B_RGB32_BIG:
	    dest[pixelpos+1] = bgr_red;
	    dest[pixelpos+2] = bgr_green;
	    dest[pixelpos+3] = bgr_blue;
		break;
	  case B_RGB24_BIG:
	    dest[pixelpos+0] = bgr_red;
	    dest[pixelpos+1] = bgr_green;
	    dest[pixelpos+2] = bgr_blue;
		break;

	  case B_RGBA32_LITTLE:
	    dest[pixelpos+3] = alphaval;
	  case B_RGB32_LITTLE:
	  case B_RGB24_LITTLE:
	    dest[pixelpos+0] = bgr_blue;
	    dest[pixelpos+1] = bgr_green;
	    dest[pixelpos+2] = bgr_red;
	    break;

	  case B_GRAYSCALE_8_BIT:
	    dest[pixelpos+0] = bgr_red;
	    break;
	  default:
		break;
	  }
	}
	
      } 
    }

      
    if(((int32)(regionwidth-col) % 5) == 0 && !gimp_progress_update(prog, (double)col/(double)regionwidth))
	{
		result = B_CANCELED;
		break;
	}
  }

  srcPR.SetRect(dest, x1, y1, regionwidth, regionheight);
  free((void*) src);
  free(dest);

  return result;
}
