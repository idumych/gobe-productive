/*
 * blackwhite 1.00 -- image filter plug-in for The GIMP
 * Copyright (C) 1996 Marc Bless
 *
 * E-mail: bless@ai-lab.fh-furtwangen.de
 * WWW:    www.ai-lab.fh-furtwangen.de/~bless
 *
 *
 * THIS FILE HAS BEEN MODIFIED FROM THE ORIGINAL - 8/4/99 Scott Lindsey
 *
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


#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#include "ImageManipAddon.h"
#include "BitmapAccessor.h"
#include "Config.h"

const char addonName[] = "Black/White";
const char addonInfo[] = "Sets color values below a threshold to 0 (black), color value above to 255 (white).  Original GIMP plugin by Marc Bless; ported by Scott Lindsey.";
const char addonCategory[] = "Color";
int32 addonVersion = 100;

void blackwhite(BitmapAccessor* sourceBitmap, BRect sel_rect, int32 channels, int32 offset, const Prefs* prefs);

status_t
Manipulate(
	BitmapAccessor *sourceBitmap,
	BMessage *ioExtension,
	bool checkOnly)
{
	Prefs prefs;
	int32 bytes, offset;

	switch(sourceBitmap->ColorSpace())
	{
		case B_RGBA32_BIG:
		case B_RGB32_BIG:
			bytes = 4;
			offset = 1;
			break;
		case B_RGBA32_LITTLE:
		case B_RGB32_LITTLE:
			bytes = 4;
			offset = 0;
			break;
		case B_RGB24_LITTLE:
		case B_RGB24_BIG:
			bytes = 3;
			offset = 0;
			break;
		
		default:
			return B_NOT_ALLOWED;
	}

	if (checkOnly)
		return B_OK;

	// If we can't find the selection rect extension, do the whole bitmap
	BRect sel_rect = sourceBitmap->Bounds();

	LoadPrefs(&prefs);
	GetSettings(ioExtension, &prefs, &sel_rect, NULL, NULL);

	blackwhite(sourceBitmap, sel_rect, bytes, offset, &prefs);
	
	return B_OK;
}

void blackwhite(BitmapAccessor* sourceBitmap, BRect sel, int32 channels, int32 offset, const Prefs* prefs)
{
	BRect r;
	
	int32 stride;
	
	int32 endx = (int32) sel.Width();
	int32 endy = (int32) sel.Height();
	
	r = sel;
	r.bottom = r.top;

	int32 amount = prefs->threshold;
	bool allpix = prefs->allpix;

	for (int32 y = 0; y <= endy; y++)
	{
		int32 stride;
		uchar* bits = (uchar*) sourceBitmap->AccessBits(r, &stride);

		for (int32 x = 0; x <= endx; x++)
		{
			int i, avg = 0;

			if (allpix)
			{
        		for (i = 0; i < 3; i++)
          			bits[i + offset] = (bits[i + offset] >= amount) ? 255 : 0;
			}
			else
			{
				for (i = 0; i < 3; i++)
					avg += bits[i + offset];
				avg /= 3;
				for (i = 0; i < 3; i++)
					bits[i + offset] = (avg >= amount) ? 255 : 0;
			}
			
			bits += channels;
		}
		r.top++;
		r.bottom = r.top;
	}
}
