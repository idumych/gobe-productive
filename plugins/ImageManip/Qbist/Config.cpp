/*
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
#include <algobase.h>
#include <MenuField.h>
#include <Box.h>
#include <Button.h>
#include <Bitmap.h>
#include <Directory.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <Path.h>
#include <FindDirectory.h>
#include <Window.h>
#include <string.h>

#include "Config.h"
#include "ImageManipAddon.h"
#include "BBitmapAccessor.h"

#define PLUGIN_PREFIX "qbist"
#define GAP 10
#define THUMBNAIL 79
#define H_MARGIN 20
#define V_MARGIN 10
#define THUMBNAIL_MSG	'THM0'


Prefs info[9];
bool info_refreshed;

static bool inited;



status_t
GetConfigMessage(
	BMessage *ioExtension,
	BMessage *ioCapability)
{
	if (!inited)
		LoadPrefs(&info[0]);
	
	// Let them know that we support these extensions
	if (ioCapability)
	{
		ioCapability->AddInt32("selection_rect", B_RECT_TYPE);
		ioCapability->AddInt32("progress_invoker", B_POINTER_TYPE);
	}
	
	if (ioExtension)
	{
		ioExtension->AddData("s_info", B_RAW_TYPE, &info[0], sizeof(Prefs));
		if (info_refreshed)
		{
			for (int i = 1; i < 9; i++)
			{
				ioExtension->AddData("s_info", B_RAW_TYPE, &info[i], sizeof(Prefs));
			}
		}
	}

	return B_OK;
}

// Here for compatibility with the standard main.cpp
void SavePrefs()
{
}


status_t MakeConfig(BMessage *ioExtension, BView **outView)
{
	BRect bounds(0, 0, GAP + 3 * (GAP + 2 * H_MARGIN + THUMBNAIL), GAP + 3 * (GAP + 2 * V_MARGIN + THUMBNAIL));
	ConfigView* config;
	*outView = config = new ConfigView(bounds, PLUGIN_PREFIX " configuration", B_WILL_DRAW, B_FOLLOW_NONE);
	config->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
	
	LoadPrefs(&info[0]);
	GetSettings(ioExtension, &info[0], NULL, NULL, NULL);
	BRect box(0, 0, 2 * H_MARGIN + THUMBNAIL, 2 * V_MARGIN + THUMBNAIL);

	int t = 0;

	for (int y = 0; y < 3; y++)
	{
		box.OffsetTo(GAP, GAP + y * (box.Height() + GAP));

		for (int x = 0; x < 3; x++)
		{
			BBox* bbox;
			config->AddChild(bbox = new BBox(box));

			bbox->AddChild(config->thumbnail[t] =
					new BitmapView(BRect(H_MARGIN, V_MARGIN, H_MARGIN + THUMBNAIL, V_MARGIN + THUMBNAIL), new BMessage((t + 5) % 9 + THUMBNAIL_MSG), config));
			t++;

			box.OffsetBy(box.Width() + GAP, 0);
		}
	}

	config->RefreshPreviews(false);

	font_height fh;
	BFont font;
	config->GetFont(&font);
	font.GetHeight(&fh);


	bounds = config->Bounds();
	bounds.right = (bounds.left = GAP);
	bounds.top = bounds.bottom;
	bounds.bottom = bounds.top;
	
	BCheckBox* cb = config->alpha = new BCheckBox(bounds, "alpha", "Alpha Transparency", new BMessage('ALPH'));
	config->AddChild(cb);
	cb->ResizeToPreferred();
	cb->SetValue(info[0].usealpha);
	
	bounds = cb->Frame();
	bounds.OffsetBy(bounds.Width() + GAP, 0);
	
	
	BButton* button = new BButton(bounds, "save", "Save…", new BMessage(B_SAVE_REQUESTED));
	config->AddChild(button);
	button->ResizeToPreferred();

	BDirectory* dir = GetPrefsDir();
	if (dir)
	{
		BMenu* menu = NULL;
		char name[B_FILE_NAME_LENGTH];
		BEntry entry;

		for (;;)
		{
			entry_ref ref;

			if (dir->GetNextEntry(&entry, true))
				break;
			
			if (entry.IsDirectory())
				continue;
			
			if (B_OK != entry.InitCheck() || !entry.Exists())
				continue;

			entry.GetName(name);
			int32 len = strlen(name) - 4;
			if (strcasecmp(".qbe", &name[len]))
				continue;

			name[len] = 0;

			if (!menu)
			{
				menu = new BMenu("");
				menu->SetRadioMode(true);
				menu->SetLabelFromMarked(true);
			}
			
			entry.GetRef(&ref);
			BMessage* msg = new BMessage('PRST');
			msg->AddRef("qbist", &ref);
			menu->AddItem(new BMenuItem(name, msg));
		}
		
		if (menu)
		{
			bounds = button->Frame();
			bounds.left = bounds.right + GAP;
			bounds.right = bounds.left + 200;
			bounds.bottom = bounds.top + fh.ascent + fh.descent + 9;

			BMenuField* popup = new BMenuField(bounds, "presets", "Load Preset", menu);
			popup->SetAlignment(B_ALIGN_RIGHT);
			popup->SetDivider(popup->StringWidth(popup->Label()) + 13);
			config->AddChild(popup);
			
		}
		config->ResizeBy(0, bounds.Height() + GAP);
		delete dir;
	}


	return B_OK;
}

#pragma mark -

ConfigView::~ConfigView()
{
	delete panel;
	get_system_info(&sysinfo);
	for (int i = 0; i < sysinfo.cpu_count; i++)
	{
		status_t result;
		
		wait_for_thread(thread[i], &result);
	}
}

void ConfigView::AttachedToWindow()
{
	AttachedToWindow(this);
}

void ConfigView::AttachedToWindow(BView* v)
{
	for (int x = 0; x < v->CountChildren(); x++)
	{
		BView* child = v->ChildAt(x);
		BInvoker* invoker = dynamic_cast<BInvoker*>(child);
		BMenuField* popup = dynamic_cast<BMenuField*>(ChildAt(x));
		if (invoker)
			invoker->SetTarget(this);

		if (popup)
			popup->Menu()->SetTargetForItems(this);

		AttachedToWindow(child);
	}
}


void ConfigView::RefreshPreviews(bool recalc)
{
	if (recalc || !info_refreshed)
	{
		for (int i=1; i < 9; i++)
		{
			modify_info(&info[0], &info[i]);
		}
	}

	info_refreshed = true;

	get_system_info(&sysinfo);

	thread[0] = find_thread(NULL);
	for (int i = 0; i < sysinfo.cpu_count; i++)
	{
		resume_thread(thread[i] = spawn_thread(PreviewThread, "qbist_preview", B_NORMAL_PRIORITY, this));
	}
}

void ConfigView::Preview(int32 which)
{
	BBitmap* bits = new BBitmap(BRect(0, 0, THUMBNAIL, THUMBNAIL), B_BITMAP_IS_AREA, B_RGBA32_LITTLE);
	BBitmapAccessor acc(bits);
	acc.SetDispose(false);

	BMessage ioExt;
	ioExt.AddData("s_info", B_RAW_TYPE, &info[which], sizeof(Prefs));
		
	Manipulate(&acc, &ioExt, false);
	
	thumbnail[(which + 4) % 9]->SetBitmap(bits);
}

int32 ConfigView::PreviewThread(void* p)
{
	ConfigView* v = static_cast<ConfigView*>(p);
	thread_id me = find_thread(NULL);

	for (int i = 0; i < v->sysinfo.cpu_count; i++)
	{
		if (me == v->thread[i])
		{
			int32 per = 9 / v->sysinfo.cpu_count;
			
			for (int j = 0; j < per; j++)
				v->Preview(j + i * per);

			if (!i)
			{
				for (int j = (per * v->sysinfo.cpu_count); j < 9; j++)
					v->Preview(j);
			}

			v->thread[i] = B_ERROR;
			break;
		}
	}

	return B_OK;
}


void ConfigView::MessageReceived(BMessage *msg)
{
	uint32 what = msg->what - THUMBNAIL_MSG;
	if (what < 9)
	{
		if (what > 0)
			info[0] = info[what];
		RefreshPreviews();
		return;
	}
	
	entry_ref ref;
	
	switch(msg->what)
	{
		case B_SAVE_REQUESTED:
		{
			if (msg->FindRef("directory", &ref))
			{
				if (panel)
				{
					panel->Show();
					break;
				}
				
				BDirectory* dir = GetPrefsDir();
				entry_ref ref;
				if (dir)
				{
					BEntry entry;
					dir->GetEntry(&entry);
					entry.GetRef(&ref);
				}
				panel = new BFilePanel(B_SAVE_PANEL, new BMessenger(this), dir ? &ref : NULL, 0, true, 0, 0, true, true);
				panel->Show();
				break;
			}
			
			char name[B_FILE_NAME_LENGTH];
			const char* s;
			if (msg->FindString("name", &s))
				break;
			
			BEntry entry(&ref);
			BPath path(&entry);
			int32 len = strlen(s);
			if (strcasecmp(".qbe", &s[len - 4]))
			{
				strncpy(name, s, B_FILE_NAME_LENGTH - 5);
				strcat(name, ".qbe");
				s = name;
			}
			
			path.Append(s);
			save_data(path.Path());
			break;
		}

		case 'ALPH':
		{
			bool value = (bool) alpha->Value();
			for (int i = 0; i < 9; i++)
				info[i].usealpha = value;
			break;
		}

		case 'PRST':
			if (!msg->FindRef("qbist", &ref))
				load_data(&ref);
			return;
	}

	BView::MessageReceived(msg);
}

void LoadPrefs(Prefs* thePrefs)
{
	if (!inited)
	{
		srand(real_time_clock());
		inited = true;
	}

	create_info(thePrefs);

	if (info[0].inited)
		*thePrefs = info[0];
}

void GetSettings(BMessage* ioExtension, Prefs* thePrefs, BRect* selection, BInvoker** preview, BInvoker** progress)
{
	if (preview)
		*preview = NULL;
	if (progress)
		*progress = NULL;
	
	if (!ioExtension)
		return;

	// Have to use temp variable, because FindXXX helpfully zeroes out your
	// parameter if the data isn't found.

	BRect r;
	if (selection && !ioExtension->FindRect("selection_rect", &r))
		*selection = r;

	const Prefs* tmp;
	ssize_t size = sizeof(Prefs);

	if (!ioExtension->FindData("s_info", B_RAW_TYPE, (const void**) &tmp, &size))
		*thePrefs = *tmp;
	
	if (thePrefs == &info[0])
	{
		for (int i = 1; i < 9; i++)
		{
			if (!ioExtension->FindData("s_info", B_RAW_TYPE, i, (const void**) &tmp, &size))
				info[i] = *tmp;
		}
	}

	if (progress)
		ioExtension->FindPointer("progress_invoker", (void**) progress);
}


#define LOBYTE(x) ((x)&0xff)
#define HIBYTE(x) ((x)>>8)
inline ushort GETMACUSHORT(FILE* f)
{
	ushort u;
	u = fgetc(f) << 8;
	u += fgetc(f);
	return u;
}

#define PUTMACUSHORT(u, f) fprintf(f, "%c%c", HIBYTE(u), LOBYTE(u));

bool ConfigView::load_data(entry_ref* ref)
{
	BEntry entry(ref);
	BPath path(&entry);

	int i;
	FILE *f;
	f=fopen(path.Path(), "rb"); 
	if (f==NULL)
		return(0);

	int j;
	for (j = 0; j < 9; j++)
	{
		if (feof(f))
			break;
		for(i=0;i<MAX_TRANSFORMS;i++)
			info[j].transformSequence[i]=GETMACUSHORT(f);
		for(i=0;i<MAX_TRANSFORMS;i++)
			info[j].source[i]=GETMACUSHORT(f);
		for(i=0;i<MAX_TRANSFORMS;i++)
			info[j].control[i]=GETMACUSHORT(f);
		for(i=0;i<MAX_TRANSFORMS;i++)
			info[j].dest[i]=GETMACUSHORT(f);
		
		// See if there's more in the file.  If not, then it's a traditional .qbe file
		// If so, it's the usealpha boolean.
		int c = fgetc(f);
		if (!feof(f))
			info[j].usealpha = c;
		else
			info[j].usealpha = false;
	}
	
	fclose(f);
	RefreshPreviews(j < 9);
	alpha->SetValue(info[0].usealpha);
	return(1);
}

void ConfigView::save_data(const char *name)
{
	int i=0;
	FILE *f;

	f=fopen(name, "wb");
	for (int j = 0; j < 9; j++)
	{
		for(i=0;i<MAX_TRANSFORMS;i++)
			PUTMACUSHORT(info[j].transformSequence[i], f);
		for(i=0;i<MAX_TRANSFORMS;i++)
			PUTMACUSHORT(info[j].source[i], f);
		for(i=0;i<MAX_TRANSFORMS;i++)
			PUTMACUSHORT(info[j].control[i], f);
		for(i=0;i<MAX_TRANSFORMS;i++)
			PUTMACUSHORT(info[j].dest[i], f);
		fprintf(f, "%c", info[j].usealpha);
	}
	fclose(f);
}

BDirectory* GetPrefsDir()
{
	BPath path;
	if (find_directory(B_COMMON_SETTINGS_DIRECTORY, &path, true))
		return NULL;
	
	BDirectory dir(path.Path());
	
	dir.CreateDirectory("qbist", NULL);
	
	BEntry entry;
	if (dir.FindEntry("qbist", &entry, true) || entry.InitCheck() || !entry.Exists() || !entry.IsDirectory())
		return NULL;
	
	return new BDirectory(&entry);
}

#pragma mark -

BitmapView::BitmapView(BRect r, BMessage* msg, ConfigView* cf)
	: BView(r, "bitmap", B_FOLLOW_TOP | B_FOLLOW_LEFT, B_WILL_DRAW),
	  BInvoker(msg, cf)
{
	config = cf;
	SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
	bitmap = NULL;
}

void BitmapView::Draw(BRect)
{
	if (bitmap)
		DrawBitmap(bitmap, B_ORIGIN);
}

void BitmapView::SetBitmap(BBitmap* bits)
{
	delete bitmap;
	bitmap = bits;
	if (Window())
	{
		if (!Window()->LockWithTimeout(2000000))
		{
			SetViewColor(B_TRANSPARENT_COLOR);
			Draw(Bounds());
			Window()->Unlock();
		}
	}
	else
		SetViewColor(B_TRANSPARENT_COLOR);
}
