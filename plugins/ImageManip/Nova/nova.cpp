/*
 * SuperNova plug-in
 * Copyright (C) 1997 Eiichi Takamori <taka@ma1.seikyou.ne.jp>,
 *		      Spencer Kimball, Federico Mena Quintero
 *
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <malloc.h>
#include <stdlib.h>
#include <algobase.h>
#include <Invoker.h>

#include "ImageManipAddon.h"
#include "BitmapAccessor.h"
#include "Config.h"

const char addonName[] = "Supernova";
const char addonInfo[] = "Produces an effect like a supernova burst\nPorted by Scott Lindsey; original GIMP plugin by Eiichi Takamori.";
const char addonCategory[] = "Effects";
int32 addonVersion = 100;

extern Prefs DefaultPrefs;
static void nova(BitmapAccessor *sourceBitmap, BRect sel, BInvoker* prog, const Prefs* prefs);
static void      rgb_to_hsl            (float    r,
					float    g,
					float    b,
					float *  h,
					float *  s,
					float *  l);
static void      hsl_to_rgb            (float    h,
					float    sl,
					float    l,
					float *  r,
					float *  g,
					float *  b);
static float gauss();

#define CLAMP(value, min_val, max_val)	max(min(value, max_val), min_val)

status_t
Manipulate(
	BitmapAccessor *sourceBitmap,
	BMessage *ioExtension,
	bool checkOnly)
{
	Prefs prefs;

	prefs = DefaultPrefs;

	switch(sourceBitmap->ColorSpace())
	{
		case B_RGBA32_LITTLE:
			break;
		
		default:
			return B_NOT_ALLOWED;
	}

	if (checkOnly)
		return B_OK;

	// If we can't find the selection rect extension, do the whole bitmap
	BRect sel_rect;
	if (!ioExtension || ioExtension->FindRect("selection_rect", &sel_rect) != B_OK)
		sel_rect = sourceBitmap->Bounds();
	BInvoker* prog = NULL;
	if (ioExtension)
		ioExtension->FindPointer("progress_invoker", (void**) &prog);
		
	nova(sourceBitmap, sel_rect, prog, &prefs);
	return B_OK;
}

static void nova(BitmapAccessor *sourceBitmap, BRect sel, BInvoker* prog, const Prefs* prefs)
{
	uchar *src_row;
	uchar *src;
	int x1, y1, x2, y2;
	int row, col;
	int x, y;
	int alpha, has_alpha, bpp;
	int progress, max_progress;
	 /****************/
	int xc, yc; /* center of image */
	float u, v;
	float l, l0;
	float w, w1, c;
	float *spoke;
	float nova_alpha;
	float src_alpha;
	float new_alpha;
	float compl_ratio;
	float ratio;
	float r, g, b, h, s, lu;
	float spokecol;
	int	  i, j;
	int	  color[3];
	int	  *spokecolor;
	int32   rowstride;
  

	/* initialize */
	
	new_alpha = 0.0;
	
	spoke = (float*) malloc(sizeof(float) * prefs->spokes);
	spokecolor = (int*) malloc(sizeof(int) * 3 * prefs->spokes);
	
	rgb_to_hsl(prefs->red/255.0, prefs->green/255.0, prefs->blue/255.0, &h, &s, &lu);
	
	for( i=0; i<prefs->spokes; i++ )
	{ 
		spoke[i] = gauss();
		h += prefs->hue / 360.0 * ((float) rand() / (float) RAND_MAX - 0.5) ;
		if (h < 0)
			h += 1.0;
		else if (h >= 1.0)
			h -= 1.0;
		hsl_to_rgb (h, s, lu, &r, &g, &b);		
		spokecolor[3*i+0] = (int) (255.0 * b);
		spokecolor[3*i+1] = (int) (255.0 * g);
		spokecolor[3*i+2] = (int) (255.0 * r);
	}
	
	bpp = 4;
	has_alpha = true;
	alpha = 3;
	x1 = (int) sel.left;
	x2 = (int) sel.right;
	y1 = (int) sel.top;
	y2 = (int) sel.bottom;
	
	
	/* Initialize progress */
	progress = 0;
	max_progress = (x2 - x1) * (y2 - y1);

	/*
	xc = (x1+x2)/2;
	yc = (y1+y2)/2; */
	xc = prefs->xcenter + x1 + (x2 - x1) / 2;
	yc = prefs->ycenter + y1 + (y2 - y1) / 2;
	l0 = (x2-xc)/4+1;	  /* standard length */
	
	
	for ( row = 0, y = y1; y <= y2; row++, y++)
	{
		src = (uchar*) sourceBitmap->AccessBits(BRect(x1, y, x2, y), &rowstride);
		
		for ( col = 0, x = x1; x <= x2; col++, x++)
		{
			u = (float) (x-xc) / prefs->radius;
			v = (float) (y-yc) / prefs->radius;
			l = sqrt( u*u + v*v );
			
			/* This algorithm is still under construction. */
			c = (atan2 (u, v) / (2 * M_PI) + .51) * prefs->spokes;
			i = (int) floor (c);
			c -= i;
			i %= prefs->spokes;
			w1 = spoke[i] * (1.0 - c) + spoke[(i + 1) % prefs->spokes] * c;
			w1 = w1 * w1;
			
			w = 1/(l+0.001)*0.9;
			
			nova_alpha = CLAMP (w, 0.0f, 1.0f);
			
			if (has_alpha)
			{
				src_alpha = (float) src[alpha] / 255.0;
				new_alpha = src_alpha + (1.0 - src_alpha) * nova_alpha;
				
				if (new_alpha != 0.0)
					ratio = nova_alpha / new_alpha;
				else
					ratio = 0.0;
			}
			else
				ratio = nova_alpha;
			
			compl_ratio = 1.0 - ratio;
			
			for (j = 0; j < alpha; j++)
			{
				spokecol = (float)spokecolor[3*i+j]*(1.0-c) + (float)spokecolor[3*((i + 1) % prefs->spokes)+j]*c;
				if (w > 1.0)
					color[j] = (int) CLAMP (spokecol * w, 0.0f, 255.0f);
				else
					color[j] = (int)(src[j] * compl_ratio + spokecol * ratio);
				
				c = CLAMP (w1 * w, 0.0f, 1.0f);
				color[j] = (int)(color[j] + 255 * c);
				
				src[j] = CLAMP (color[j], 0, 255);
			}
			
			if (has_alpha)
				src[alpha] = (uchar) (new_alpha * 255.0);
			
			src += bpp;
		}

		if (prog && row % 5 == 4)
		{
			BMessage msg(*prog->Message());
			msg.AddFloat("completion", (float) y / (y2 - y1));
			prog->Invoke(&msg);
		}
	}
	  /* Update progress */
	  //progress += src_rgn.w * src_rgn.h;
	 // gimp_progress_update ((float) progress / (float) max_progress);

	free((void*) spoke);
	free((void*) spokecolor);
}

static float gauss()
{
  float sum=0;
  int i;
  for(i=0; i<6; i++)
    sum+=(float)rand()/RAND_MAX;
  return sum/6;
}


/*
 * RGB-HSL transforms.
 * Ken Fishkin, Pixar Inc., January 1989.
 */

/*
 * given r,g,b on [0 ... 1],
 * return (h,s,l) on [0 ... 1]
 */

static void
rgb_to_hsl (float  r,
	    float  g,
	    float  b,
	    float *h,
	    float *s,
	    float *l)
{
  float v;
  float m;
  float vm;
  float r2, g2, b2;

  v = MAX(r,g);
  v = MAX(v,b);
  m = MIN(r,g);
  m = MIN(m,b);

  if ((*l = (m + v) / 2.0) <= 0.0)
    return;
  if ((*s = vm = v - m) > 0.0)
    {
      *s /= (*l <= 0.5) ? (v + m ) :
	(2.0 - v - m) ;
    }
  else
    return;

  r2 = (v - r) / vm;
  g2 = (v - g) / vm;
  b2 = (v - b) / vm;

  if (r == v)
    *h = (g == m ? 5.0 + b2 : 1.0 - g2);
  else if (g == v)
    *h = (b == m ? 1.0 + r2 : 3.0 - b2);
  else
    *h = (r == m ? 3.0 + g2 : 5.0 - r2);

  *h /= 6;
}

/*
 * given h,s,l on [0..1],
 * return r,g,b on [0..1]
 */

static void
hsl_to_rgb (float  h,
	    float  sl,
	    float  l,
	    float *r,
	    float *g,
	    float *b)
{
  float v;

  v = (l <= 0.5) ? (l * (1.0 + sl)) : (l + sl - l * sl);
  if (v <= 0)
    {
      *r = *g = *b = 0.0;
    }
  else
    {
      float m;
      float sv;
      int sextant;
      float fract, vsf, mid1, mid2;

      m = l + l - v;
      sv = (v - m ) / v;
      h *= 6.0;
      sextant = (int) h;
      fract = h - sextant;
      vsf = v * sv * fract;
      mid1 = m + vsf;
      mid2 = v - vsf;
      switch (sextant)
	{
	case 0: *r = v; *g = mid1; *b = m; break;
	case 1: *r = mid2; *g = v; *b = m; break;
	case 2: *r = m; *g = v; *b = mid1; break;
	case 3: *r = m; *g = mid2; *b = v; break;
	case 4: *r = mid1; *g = m; *b = v; break;
	case 5: *r = v; *g = m; *b = mid2; break;
	}
    }
}


