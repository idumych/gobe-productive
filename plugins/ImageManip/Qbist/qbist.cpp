/*
 * Written 1997 Jens Ch. Restemeier <jchrr@hrz.uni-bielefeld.de>
 * This program is based on an algorithm / article by
 * Jörn Loviscach.
 *
 * It appeared in c't 10/95, page 326 and is called 
 * "Ausgewürfelt - Moderne Kunst algorithmisch erzeugen".
 * (~modern art created with algorithms)
 * 
 * It generates one main formula (the middle button) and 8 variations of it.
 * If you select a variation it becomes the new main formula. If you
 * press "OK" the main formula will be applied to the image.
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
 */

/*
 * History:
 * 1.0 first release
 * 1.2 now handles RGB*
 * 1.5 fixed a small bug
 * 1.6 fixed a bug that was added by v1.5 :-(
 * 1.7 added patch from Art Haas to make it compile with HP-UX, a small clean-up
 * 1.8 Dscho added transform file load/save, bug-fixes 
 * 1.9 rewrote renderloop.
 * 1.9a fixed a bug.
 * 1.9b fixed MAIN()
 * 1.10 added optimizer
 *
 *
 * Ported to BeOS ImageManip by Scott Lindsey 8/6/99
 */

#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#include "ImageManipAddon.h"
#include "BitmapAccessor.h"
#include "Config.h"
#include "GIMPGlue.h"

const char addonName[] = "Qbist";
const char addonInfo[] = "Create images based on a random genetic formula from an article by JÃ¶rn Loviscach.\nOriginal GIMP plugin by Jens Ch. Restemeier.\nPorted by Scott Lindsey.";
const char addonCategory[] = "Render";
int32 addonVersion = 100;

typedef float vreg[4];

#define PROJECTION	0
#define SHIFT		1
#define SHIFTBACK	2
#define ROTATE		3
#define ROTATE2		4
#define MULTIPLY	5
#define SINE		6
#define CONDITIONAL	7
#define COMPLEMENT	8

class qbist_stuff
{
public:
	qbist_stuff()		{}

	void qbist(const Prefs* info, uchar *buffer, int yp, int num, int width, int height, int bpp);
	void check_last_modified(const Prefs* info, int p, int n);
	void optimize(Prefs info);

	int byteorder[4];

private:
	int used_trans_flag[MAX_TRANSFORMS];
	int used_reg_flag[NUM_REGISTERS];
};



void doit(BitmapAccessor* sourceBitmap, BRect sel_rect, BInvoker* prog, const Prefs* prefs);

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
		case B_RGBA32_LITTLE:
		case B_RGB32_LITTLE:
		case B_RGB24_LITTLE:
		case B_RGB32_BIG:
		case B_RGB24_BIG:
		case B_GRAY8:
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

	doit(sourceBitmap, sel_rect, prog, &prefs);
	
	return B_OK;
}

void doit(BitmapAccessor* sourceBitmap, BRect sel_rect, BInvoker* prog, const Prefs* info)
{
	int32 stride;
	int32 top = (int32) sel_rect.top;
	int32 bottom = (int32) sel_rect.bottom;
	int32 width = sel_rect.IntegerWidth() + 1;
	int32 height = sel_rect.IntegerHeight() + 1;
	int32 channels = (int32) sourceBitmap->BytesPerPixel();

	qbist_stuff stuff;

	switch(sourceBitmap->ColorSpace())
	{
		case B_RGBA32_BIG:
			stuff.byteorder[0] = 1;
			stuff.byteorder[1] = 2;
			stuff.byteorder[2] = 3;
			stuff.byteorder[3] = 0;
			break;
		case B_RGBA32_LITTLE:
			stuff.byteorder[0] = 2;
			stuff.byteorder[1] = 1;
			stuff.byteorder[2] = 0;
			stuff.byteorder[3] = 3;
			break;
		case B_RGB32_LITTLE:
		case B_RGB24_LITTLE:
			stuff.byteorder[0] = 2;
			stuff.byteorder[1] = 1;
			stuff.byteorder[2] = 0;
			stuff.byteorder[3] = -1;
			break;
		case B_RGB32_BIG:
		case B_RGB24_BIG:
			stuff.byteorder[0] = 1;
			stuff.byteorder[1] = 2;
			stuff.byteorder[2] = 3;
			stuff.byteorder[3] = -1;
			break;
		
		case B_GRAY8:
			stuff.byteorder[0] = 2;
			stuff.byteorder[1] = stuff.byteorder[2] = stuff.byteorder[3] = -1;
			break;
		
		default:
			return;
	}
	
	stuff.optimize(*info);

	gimp_progress_init("Rendering...");

	for (int32 row = top; row <= bottom; row++)
	{
		sel_rect.top = sel_rect.bottom = row;
		uchar* p = (uchar*) sourceBitmap->AccessBits(sel_rect, &stride);

		stuff.qbist(info, p, row - top, width, width, height, channels);
		if ((row % 5) == 0) 
			gimp_progress_update(prog, (row - top) / (float) height);
	}
	
}

void qbist_stuff::qbist(const Prefs* info, uchar *buffer, int gy, int num, int width, int height, int bpp)
{
	ushort gx;
	vreg reg [NUM_REGISTERS];
	int i, j;
	ushort sr, cr, dr;

	if (num<=0) return;

	for(gx=0; gx<num; gx ++) {
		for(i=0; i<NUM_REGISTERS; i++) {
			if (used_reg_flag[i]) {
				reg[i][0] = ((float)gx) / ((float)(width));
				reg[i][1] = ((float)gy) / ((float)(height));
				reg[i][2] = ((float)i) / ((float)NUM_REGISTERS);
				reg[i][3] = (float)(gx) *  (float)gy / ((float)(width) * (float)(height));
			}
		}
		for(i=0;i<MAX_TRANSFORMS; i++) {
			sr=info->source[i];cr=info->control[i];dr=info->dest[i];
			
			if (used_trans_flag[i]) switch (info->transformSequence[i]) {
				case PROJECTION: {
					float scalarProd;
					scalarProd = (reg[sr][0]*reg[cr][0])+(reg[sr][1]*reg[cr][1])+(reg[sr][2]*reg[cr][2]);
					reg[dr][0] = scalarProd*reg[sr][0];
					reg[dr][1] = scalarProd*reg[sr][1];
					reg[dr][2] = scalarProd*reg[sr][2];
					reg[dr][3] = scalarProd*reg[sr][3];
					break;
				}
				case SHIFT: 
					reg[dr][0] = reg[sr][0]+reg[cr][0];
					if (reg[dr][0] >= 1.0) reg[dr][0] -= 1.0;
					reg[dr][1] = reg[sr][1]+reg[cr][1];
					if (reg[dr][1] >= 1.0) reg[dr][1] -= 1.0;
					reg[dr][2] = reg[sr][2]+reg[cr][2];
					if (reg[dr][2] >= 1.0) reg[dr][2] -= 1.0;
					reg[dr][3] = reg[sr][3]+reg[cr][3];
					if (reg[dr][3] >= 1.0) reg[dr][3] -= 1.0;
					break;
				case SHIFTBACK: 
					reg[dr][0] = reg[sr][0]-reg[cr][0];
					if (reg[dr][0] <= 0.0) reg[dr][0] += 1.0;
					reg[dr][1] = reg[sr][1]-reg[cr][1];
					if (reg[dr][1] <= 0.0) reg[dr][1] += 1.0;
					reg[dr][2] = reg[sr][2]-reg[cr][2];
					if (reg[dr][2] <= 0.0) reg[dr][2] += 1.0;
					reg[dr][3] = reg[sr][3]-reg[cr][3];
					if (reg[dr][3] <= 0.0) reg[dr][3] += 1.0;
					break;
				case ROTATE: 
					reg[dr][0] = reg[sr][1];
					reg[dr][1] = reg[sr][2];
					reg[dr][2] = reg[sr][0];
					break;
				case ROTATE2: 
					reg[dr][0] = reg[sr][2];
					reg[dr][1] = reg[sr][0];
					reg[dr][2] = reg[sr][1];
					break;
				case MULTIPLY: 
					reg[dr][0] = reg[sr][0]*reg[cr][0];
					reg[dr][1] = reg[sr][1]*reg[cr][1];
					reg[dr][2] = reg[sr][2]*reg[cr][2];
					reg[dr][3] = reg[sr][3]*reg[cr][3];
					break;
				case SINE: 
					reg[dr][0] = 0.5+(0.5*sin(20.0*reg[sr][0]*reg[cr][0]));
					reg[dr][1] = 0.5+(0.5*sin(20.0*reg[sr][1]*reg[cr][1]));
					reg[dr][2] = 0.5+(0.5*sin(20.0*reg[sr][2]*reg[cr][2]));
					reg[dr][3] = 0.5+(0.5*sin(20.0*reg[sr][3]*reg[cr][3]));
					break;
				case CONDITIONAL: 
					if ((reg[cr][0]+reg[cr][1]+reg[cr][2]) > 0.5)	{
						reg[dr][0] = reg[sr][0];
						reg[dr][1] = reg[sr][1];
						reg[dr][2] = reg[sr][2];
						reg[dr][3] = reg[sr][3];
					} else {
						reg[dr][0] = reg[cr][0];
						reg[dr][1] = reg[cr][1];
						reg[dr][2] = reg[cr][2];
						reg[dr][3] = reg[cr][3];
					}
					break;
				case COMPLEMENT: 
					reg[dr][0] = 1.0-reg[sr][0];
					reg[dr][1] = 1.0-reg[sr][1];
					reg[dr][2] = 1.0-reg[sr][2];
					reg[dr][3] = 1.0-reg[sr][3];
					break;
			}
		}

		for (i=0; i<bpp; i++)
		{
			j = byteorder[i];
			if (j == -1)
				continue;
			if (i == 3 && !info->usealpha)
				buffer[j]=255;
			else
			{ 
				// Don't clamp to the [0..255] range.
				// Let it wrap around. The original algorithm did,
				// and preset files depend on this.
				buffer[j]= (uchar) (255.0 * reg[0][i]);
			}
		}
		buffer+=bpp;
	}
}


void create_info(Prefs *info)
{
	int k;
	for (k=0; k<MAX_TRANSFORMS; k++) {
		info->transformSequence[k]=rand() % NUM_TRANSFORMS;
		info->source[k]=rand() % NUM_REGISTERS;
		info->control[k]=rand() % NUM_REGISTERS;
		info->dest[k]=rand() % NUM_REGISTERS;
	}
	info->dest[rand() % MAX_TRANSFORMS]=0;
	info->inited = true;
}

void modify_info(Prefs *o_info, Prefs *n_info)
{
	int k, n; 
	memcpy(n_info, o_info, sizeof(Prefs)); 
	n=rand() % MAX_TRANSFORMS;
	for (k=0;k<n; k++) {
		switch (rand() % 4) {
			case 0: n_info->transformSequence[rand() % MAX_TRANSFORMS] = rand() % NUM_TRANSFORMS; break;
			case 1: n_info->source[rand() % MAX_TRANSFORMS]            = rand() % NUM_REGISTERS; break;
			case 2: n_info->control[rand() % MAX_TRANSFORMS]           = rand() % NUM_REGISTERS; break;
			case 3: n_info->dest[rand() % MAX_TRANSFORMS]              = rand() % NUM_REGISTERS; break;
		}
	}
}

void qbist_stuff::check_last_modified(const Prefs* info, int p, int n)
{
	p--;
	while ((p>=0) && (info->dest[p]!=n)) p--;
	if (p<0) 
		used_reg_flag[n]=1;
	else {
		used_trans_flag[p]=1;
		check_last_modified(info, p, info->source[p]);
		check_last_modified(info, p, info->control[p]);
	}
}

void qbist_stuff::optimize(Prefs info)
{
	int i;
	/* double-arg fix: */
	for (i=0; i<MAX_TRANSFORMS; i++) {
		used_trans_flag[i]=0;
		if (i<NUM_REGISTERS)
			used_reg_flag[i]=0;
		/* double-arg fix: */
		switch (info.transformSequence[i]) {
			case ROTATE: 
			case ROTATE2: 
			case COMPLEMENT: 
				info.control[i]=info.dest[i];
				break;
		}
	}
	/* check for last modified item */
	check_last_modified(&info, MAX_TRANSFORMS, 0);
}

