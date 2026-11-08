/* 
* Copyright (C) 1998 Gobe Software, Inc.
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

#include <Application.h>
#include <Alert.h>

#ifdef __cplusplus
extern "C" {
#endif
void GIMPMessage(void);
#ifdef __cplusplus
}
#endif

/* This gets executed when a plugin linked against this file is run with improper arguments */

void GIMPMessage(void)
{
	BApplication app("application/x-vnd.gobe-gimp-filter");
	BAlert* alert = new BAlert("GIMP Alert", "This is a GNU Image Manipulation Package (GIMP) add-on recompiled for use with Gobe Productive.\n", "OK");
	alert->Go();
}
