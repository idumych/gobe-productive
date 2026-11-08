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
* This file has been modified for use under BeOS, using Be's areas
* for shared memory.  Plugins linked against this library are intended
* for use with Gobe Productive, but should be usable with anything which
* implements the host side in a compliant fashion.  <Scott Lindsey 5/28/98>
*/
#include <OS.h>

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/times.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#define DEBUG 1
#include <Debug.h>

#include "dialog_types.h"
#include "gimp.h"
#include "msg.h"

extern void GIMPMessage(void);

typedef void (*GimpProc) (MsgP);
typedef struct _GimpDialog _GimpDialog, *GimpDialog;
typedef struct _GimpDialogItem _GimpDialogItem, *GimpDialogItem;
typedef struct _List *List;

struct _List {
	void *data;
	List next;
	List prev;
};

struct _Image {
	MsgImage stuff;
	ImageType type;
	void *data;
};

struct _GimpDialog {
	int dialog_ID;
	int displayed;
	int return_val;
	List items;
};

struct _GimpDialogItem {
	int item_ID;
	GimpItemCallbackProc callback;
	void *callback_data;
};

char *dialog_msg_name[] =
{
  "DIALOG_NEW",
  "DIALOG_SHOW",
  "DIALOG_UPDATE",
  "DIALOG_CLOSE",
  "DIALOG_NEW_ITEM",
  "DIALOG_SHOW_ITEM",
  "DIALOG_HIDE_ITEM",
  "DIALOG_CHANGE_ITEM",
  "DIALOG_DELETE_ITEM",
  "DIALOG_CALLBACK"
};

char *msg_name[] =
{
  "QUIT",
  
  "IMAGE_NEW",
  "IMAGE_DISPLAY",
  "IMAGE_INPUT",
  "IMAGE_OUTPUT",
  "IMAGE_COLOR",
  "IMAGE_UPDATE",
  
  "LOAD",
  "SAVE",

  "PARAMS",
  "PROGRESS",
  "MESSAGE",
  "DIALOG"
};



static int gimp_new_item (int, int, int, void *, long);

static void gimp_recv (void);
static void gimp_recv_info (void);
static void gimp_send (long, long, void *);
static void* gimp_read (long);
static void* gimp_read_info (long);
static int gimp_write (long, void*);

static void gimp_signal (int);
static void gimp_wait_one (void);
static void gimp_wait_ack (void);

static void gimp_handle_quit (MsgP);
static void gimp_load (MsgP);
static void gimp_save (MsgP);
static void gimp_dialog (MsgP);

static void gimp_add_dialog (int);
static void gimp_add_item_callback (int, int, GimpItemCallbackProc, void *);
static void gimp_delete_dialog (int);
static void gimp_delete_dialog_item (int, int);
static void gimp_call_item_callback (int, int, void *);
static GimpDialog gimp_find_dialog (int);
static GimpDialogItem gimp_find_item (int, int);

static List list_alloc (void);
static void list_free (List);
static void list_free_item (List);
static List list_prepend (List, void *);
static List list_remove (List, void *);

static void* xmalloc (long);
static void xfree (void *);
static void fatal (char *);
static void unexpected_message (int expected, int got);
static void unexpected_dialog_message(int gotmessage, int expectedtype, int gottype);

static GimpProc gimp_procs[] =
{
	gimp_handle_quit,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		gimp_load,
		gimp_save,
		NULL,
		NULL,
		NULL,
		gimp_dialog,
};

static int n_gimp_procs = sizeof (gimp_procs) / sizeof (GimpProc);

static GimpLoadSaveProc load_proc;
static GimpLoadSaveProc save_proc;

static int my_read;
static int my_write;
static int my_info;
static Msg recv_msg;
static Msg send_msg;
static Msg info_msg;

static List dialogs = NULL;
static List shared_memory = NULL;

static char *progname;

int
gimp_init (argc, argv)
	int argc;
char **argv;
{
	if ((argc == 5) && (strcmp (argv[1], "-gimp") == 0))
	{
		progname = argv[0];
		
		/* Handle some signals */
		//signal (SIGHUP, gimp_signal);
		//signal (SIGINT, gimp_signal);
		//signal (SIGQUIT, gimp_signal);
		//signal (SIGABRT, gimp_signal);
		//signal (SIGBUS, gimp_signal);
		//signal (SIGSEGV, gimp_signal);
		//signal (SIGPIPE, gimp_signal);
		//signal (SIGTERM, gimp_signal);
		
		my_read = atoi (argv[2]);
		my_write = atoi (argv[3]);
		my_info = atoi (argv[4]);
		
		load_proc = NULL;
		save_proc = NULL;
		
		recv_msg.info.type = 0;
		recv_msg.info.size = 0;
		recv_msg.data = 0;
		
		send_msg.info.type = 0;
		send_msg.info.size = 0;
		send_msg.data = 0;
		
		info_msg.info.type = 0;
		info_msg.info.size = 0;
		info_msg.data = 0;
		
		return 1;
	}
	else
	{
		GIMPMessage();
		return 0;
	}
}

void
gimp_install_load_save_handlers (load, save)
	GimpLoadSaveProc load, save;
	{
		load_proc = load;
		save_proc = save;
	}

void
gimp_main_loop ()
{
	while (1)
		gimp_recv ();
}

void
gimp_quit ()
{
	while (shared_memory)
	{
		delete_area((area_id) shared_memory->data);
		shared_memory = shared_memory->next;
	}

	gimp_send (QUIT, 0, 0);
	exit (0);
}

void*
gimp_get_params ()
{
	MsgParams params;
	MsgParams *ret;
	
	params.type = PARAMS_GET;
	params.size = 0;
	params.data = NULL;
	
	gimp_send (PARAMS, sizeof (MsgParams), &params);
	gimp_wait_ack();
	gimp_wait_one ();
	
	if (recv_msg.info.type == PARAMS)
	{
		ret = recv_msg.data;
		if (ret->size > 0)
		{
			ret->data = gimp_read (ret->size);
			return ret->data;
		}
	}
	else
	{
		unexpected_message(PARAMS, recv_msg.info.type);
	}
	
	return NULL;
}

void
gimp_set_params (size, data)
	long size;
void *data;
{
	MsgParams params;
	
	params.type = PARAMS_SET;
	params.size = size;
	params.data = data;
	
	gimp_send (PARAMS, sizeof (MsgParams), &params);
	gimp_write (params.size, params.data);
	gimp_wait_ack ();
	
	if (info_msg.info.type != PARAMS)
		unexpected_message(PARAMS, info_msg.info.type);
}

void
gimp_init_progress (label)
	char *label;
	{
		MsgProgress progress;
		
		progress.current = 0;
		progress.max = 1;
		strncpy (progress.label, label, 256);
		
		gimp_send (PROGRESS, sizeof (MsgProgress), &progress);
		gimp_wait_ack ();
		
		if (info_msg.info.type != PROGRESS)
			unexpected_message(PROGRESS, info_msg.info.type);
	}

void
gimp_do_progress (cur, max)
	int cur, max;
	{
		MsgProgress progress;
		
		progress.current = cur;
		progress.max = max;
		progress.label[0] = 0;
		
		gimp_send (PROGRESS, sizeof (MsgProgress), &progress);
		gimp_wait_ack ();
		
		if (info_msg.info.type != PROGRESS)
			unexpected_message(PROGRESS, info_msg.info.type);
	}

void
gimp_message (str)
	char *str;
	{
		MsgMessage message;
		
		strcpy (message.data, str);
		gimp_send (MESSAGE, sizeof (MsgMessage), &message);
		gimp_wait_ack ();
		
		if (info_msg.info.type != MESSAGE)
			unexpected_message(MESSAGE, info_msg.info.type);
	}

void
gimp_free_image (image)
	Image image;
	{
		if (image)
		{
			area_id id = area_for(image->data);
			shared_memory = list_remove (shared_memory, (void*) id);
			delete_area(id);
			xfree (image);
		}
	}

Image
gimp_new_image (name, width, height, type)
	char *name;
long width, height;
ImageType type;
{
	Image image;
	
	image = xmalloc (sizeof (struct _Image));
	
	if (name)
	{
		strcpy (image->stuff.name, name);
		image->stuff.data = 1;
	}
	else
		image->stuff.data = 0;
	image->stuff.width = width;
	image->stuff.height = height;
	image->stuff.channels = 0;
	
	switch (type)
	{
	case RGB_IMAGE:
		image->stuff.type = IMAGE_TYPE_RGB;
		image->stuff.channels = 4; /* Lie */
		break;
	case GRAY_IMAGE:
		image->stuff.type = IMAGE_TYPE_GRAY;
		image->stuff.channels = 1;
		break;
	case INDEXED_IMAGE:
		image->stuff.type = IMAGE_TYPE_INDEXED;
		image->stuff.channels = 1;
		break;
	case UNKNOWN_IMAGE:
		fatal ("unable to create image of type unknown");
		break;
	}
	
	gimp_send (IMAGE_NEW, sizeof (MsgImage), &image->stuff);
	gimp_wait_ack();
	gimp_wait_one ();
	
	if (recv_msg.info.type == IMAGE_NEW)
	{
		area_id id;

		memcpy (&image->stuff, recv_msg.data, sizeof (MsgImage));
		switch (image->stuff.type)
		{
		case IMAGE_TYPE_RGB:
			image->type = RGB_IMAGE;
			break;
		case IMAGE_TYPE_GRAY:
			image->type = GRAY_IMAGE;
			break;
		case IMAGE_TYPE_INDEXED:
			image->type = INDEXED_IMAGE;
			break;
		default:
			image->type = UNKNOWN_IMAGE;
			break;
		}

		id = clone_area("plugin aux", &image->data, B_ANY_ADDRESS, B_READ_AREA + B_WRITE_AREA, image->stuff.shmid);
		if (id < 0)
			fatal("unable to attach shared memory");
		
		shared_memory = list_prepend (shared_memory, (void*) id);
		
		return image;
	}
	else
	{
		unexpected_message(IMAGE_NEW, recv_msg.info.type);
	}
	
	return NULL;
}

Image
gimp_get_input_image (ID)
	long ID;
	{
		Image image;

		image = xmalloc (sizeof (struct _Image));
		
		image->stuff.width = image->stuff.height = -1;
		image->stuff.ID = ID;
		gimp_send (IMAGE_INPUT, sizeof (MsgImage), &image->stuff);
		gimp_wait_ack();
printf("got ack\n");
		gimp_wait_one ();
printf("got image\n");
		
		if (recv_msg.info.type == IMAGE_INPUT)
		{
			area_id id;

			memcpy (&image->stuff, recv_msg.data, sizeof (MsgImage));
			switch (image->stuff.type)
			{
			case IMAGE_TYPE_RGB:
				image->type = RGB_IMAGE;
				break;
			case IMAGE_TYPE_GRAY:
				image->type = GRAY_IMAGE;
				break;
			case IMAGE_TYPE_INDEXED:
				image->type = INDEXED_IMAGE;
				break;
			default:
				image->type = UNKNOWN_IMAGE;
				break;
			}
			
			id = clone_area("plugin input", &image->data, B_ANY_ADDRESS, B_READ_AREA, image->stuff.shmid);
			if (id < 0)
				fatal("unable to attach shared memory");
			
			shared_memory = list_prepend (shared_memory, (void*) id);
			
			return image;
		}
		else
		{
			unexpected_message(IMAGE_INPUT, recv_msg.info.type);
		}
		
		return NULL;
	}

Image
gimp_get_output_image (ID)
	long ID;
	{
		Image image;
		
		image = xmalloc (sizeof (struct _Image));
		
		image->stuff.ID = ID;
		gimp_send (IMAGE_OUTPUT, sizeof (MsgImage), &image->stuff);
		gimp_wait_ack();
		gimp_wait_one ();
		
		if (recv_msg.info.type == IMAGE_OUTPUT)
		{
			area_id id;

			memcpy (&image->stuff, recv_msg.data, sizeof (MsgImage));
			switch (image->stuff.type)
			{
			case IMAGE_TYPE_RGB:
				image->type = RGB_IMAGE;
				break;
			case IMAGE_TYPE_GRAY:
				image->type = GRAY_IMAGE;
				break;
			case IMAGE_TYPE_INDEXED:
				image->type = INDEXED_IMAGE;
				break;
			default:
				image->type = UNKNOWN_IMAGE;
				break;
			}


			id = clone_area("plugin output", &image->data, B_ANY_ADDRESS, B_READ_AREA + B_WRITE_AREA, image->stuff.shmid);
			if (id < 0)
				fatal("unable to attach shared memory");
			
			shared_memory = list_prepend (shared_memory, (void*) id);
			
			return image;
		}
		else
		{
			unexpected_message(IMAGE_OUTPUT, recv_msg.info.type);
		}
		
		return NULL;
	}

void
gimp_display_image (image)
	Image image;
	{
		if (image)
		{
			gimp_send (IMAGE_DISPLAY, sizeof (MsgImage), &image->stuff);
			gimp_wait_ack ();
			
			if (info_msg.info.type != IMAGE_DISPLAY)
				unexpected_message(IMAGE_DISPLAY, info_msg.info.type);
		}
	}

void
gimp_update_image (image)
	Image image;
	{
		if (image)
		{
			gimp_send (IMAGE_UPDATE, sizeof (MsgImage), &image->stuff);
			gimp_wait_ack ();
			
			if (info_msg.info.type != IMAGE_UPDATE)
				unexpected_message(IMAGE_UPDATE, info_msg.info.type);
		}
	}

char*
gimp_image_name (image)
	Image image;
	{
		if (image)
			return image->stuff.name;
		else
			return NULL;
	}

long
gimp_image_width (image)
	Image image;
	{
		if (image)
			return image->stuff.width;
		else
			return 0;
	}

long
gimp_image_height (image)
	Image image;
	{
		if (image)
			return image->stuff.height;
		else
			return 0;
	}

long
gimp_image_channels (image)
	Image image;
	{
		if (image)
			return image->stuff.channels;
		else
			return 0;
	}

ImageType
gimp_image_type (image)
	Image image;
	{
		if (image)
			return image->type;
		else
			return UNKNOWN_IMAGE;
	}

void
gimp_image_area (image, x1, y1, x2, y2)
	Image image;
int *x1, *y1, *x2, *y2;
{
	if (image)
	{
		*x1 = image->stuff.x1;
		*y1 = image->stuff.y1;
		*x2 = image->stuff.x2;
		*y2 = image->stuff.y2;
	}
	else
	{
		*x1 = 0;
		*y1 = 0;
		*x2 = 0;
		*y2 = 0;
	}
}

unsigned char*
gimp_image_data (image)
	Image image;
	{
		if (image)
			return image->data;
		else
			return NULL;
	}

void*
gimp_image_cmap (image)
	Image image;
	{
		if (image && image->stuff.colors)
			return image->stuff.cmap;
		else
			return NULL;
	}

long
gimp_image_colors (image)
	Image image;
	{
		if (image)
			return image->stuff.colors;
		else
			return 0;
	}

void
gimp_set_image_colors (image, cmap, ncols)
	Image image;
void *cmap;
long ncols;
{
	if (image)
	{
		image->stuff.colors = ncols;
		memcpy (image->stuff.cmap, cmap, sizeof (unsigned char) * 3 * ncols);
	}
}

void
gimp_foreground_color (r, g, b)
	unsigned char *r, *g, *b;
	{
		MsgColor color;
		MsgColor *ret;
		
		color.type = COLOR_FOREGROUND;
		gimp_send (IMAGE_COLOR, sizeof (MsgColor), &color);
		gimp_wait_ack ();
		gimp_wait_one ();

		ret = recv_msg.data;
		if ((recv_msg.info.type == IMAGE_COLOR) && (ret->type == COLOR_FOREGROUND))
		{
			*r = ret->r;
			*g = ret->g;
			*b = ret->b;
		}
		else
		{
			unexpected_message(IMAGE_COLOR, recv_msg.info.type);
		}
	}

void
gimp_background_color (r, g, b)
	unsigned char *r, *g, *b;
	{
		MsgColor color;
		MsgColor *ret;
		
		color.type = COLOR_BACKGROUND;
		gimp_send (IMAGE_COLOR, sizeof (MsgColor), &color);
		gimp_wait_ack ();
		gimp_wait_one ();
		
		ret = recv_msg.data;
		if ((recv_msg.info.type == IMAGE_COLOR) && (ret->type == COLOR_BACKGROUND))
		{
			*r = ret->r;
			*g = ret->g;
			*b = ret->b;
		}
		else
		{
			unexpected_message(IMAGE_COLOR, recv_msg.info.type);
		}
	}


int
gimp_new_dialog (title)
	char *title;
	{
		MsgDialog dlg;
		MsgDialog *ret;
		
		dlg.type = DIALOG_NEW;
		dlg.dialog.dialog_ID = -1;
		strcpy (dlg.dialog.title, title);
		
		gimp_send (DIALOG, sizeof (MsgDialog), &dlg);
		gimp_wait_ack ();
		gimp_wait_one ();
		
		ret = recv_msg.data;
		if ((recv_msg.info.type == DIALOG) && (ret->type == DIALOG_NEW))
		{
			gimp_add_dialog (ret->dialog.dialog_ID);
			return ret->dialog.dialog_ID;
		}
		else
		{
			unexpected_dialog_message(recv_msg.info.type, DIALOG_NEW, ret->type);
		}
		
		return -1;
	}

int
gimp_show_dialog (dialog_ID)
	int dialog_ID;
	{
		MsgDialog dlg;
		MsgDialog *ret;
		GimpDialog dialog;
		
		dlg.type = DIALOG_SHOW;
		dlg.dialog.dialog_ID = dialog_ID;
		memset (dlg.dialog.title, 0, 32);
		
		gimp_send (DIALOG, sizeof (MsgDialog), &dlg);
		gimp_wait_ack ();
		gimp_wait_one ();
		
		ret = recv_msg.data;
		if ((recv_msg.info.type == DIALOG) && (ret->type == DIALOG_SHOW))
		{
			dialog = gimp_find_dialog (dialog_ID);
			if (dialog)
			{
				dialog->displayed = 1;
				while (dialog->displayed)
					gimp_wait_one ();
				
				return dialog->return_val;
			}
			
			return 0;
		}
		else
		{
			unexpected_dialog_message(recv_msg.info.type, DIALOG_SHOW, ret->type);
		}
		
		return 1;
	}

void
gimp_update_dialog (dialog_ID)
	int dialog_ID;
	{
		MsgDialog dlg;
		MsgDialog *ret;
		
		dlg.type = DIALOG_UPDATE;
		dlg.dialog.dialog_ID = dialog_ID;
		memset (dlg.dialog.title, 0, 32);
		
		gimp_send (DIALOG, sizeof (MsgDialog), &dlg);
		gimp_wait_ack ();
		
		ret = info_msg.data;
		if ((info_msg.info.type == DIALOG) && (ret->type == DIALOG_UPDATE))
		{
			return;
		}
		else
		{
			unexpected_dialog_message(info_msg.info.type, DIALOG_UPDATE, ret->type);
		}
	}

void
gimp_close_dialog (dialog_ID, val)
	int dialog_ID, val;
	{
		MsgDialog dlg;
		MsgDialog *ret;
		GimpDialog dialog;
		
		dlg.type = DIALOG_CLOSE;
		dlg.dialog.dialog_ID = dialog_ID;
		memset (dlg.dialog.title, 0, 32);
		
		gimp_send (DIALOG, sizeof (MsgDialog), &dlg);
		gimp_wait_ack ();
		
//		ret = recv_msg.data;
//		if ((recv_msg.info.type == DIALOG) && (ret->type == DIALOG_CLOSE))
//		{
			dialog = gimp_find_dialog (dialog_ID);
			if (dialog && dialog->displayed)
			{
				dialog->displayed = 0;
				dialog->return_val = val;
			}
			else
			{
				gimp_delete_dialog (dialog_ID);
			}
//		}
//		else
//		{
//			unexpected_dialog_message(recv_msg.info.type, DIALOG_CLOSE, ret->type);
//		}
	}

int
gimp_ok_item_id (dialog_ID)
	int dialog_ID;
	{
		return 1;
	}

int
gimp_cancel_item_id (dialog_ID)
	int dialog_ID;
	{
		return 2;
	}

int
gimp_new_row_group (dialog_ID, parent_ID, type, title)
	int dialog_ID, parent_ID, type;
char *title;
{
	if (type)
		return gimp_new_item (dialog_ID, parent_ID,
			GROUP_ROWS | GROUP_RADIO,
			title, strlen (title) + 1);
	else
		return gimp_new_item (dialog_ID, parent_ID,
			GROUP_ROWS,
			title, strlen (title) + 1);
}

int
gimp_new_column_group (dialog_ID, parent_ID, type, title)
	int dialog_ID, parent_ID, type;
char *title;
{
	if (type)
		return gimp_new_item (dialog_ID, parent_ID,
			GROUP_COLUMNS | GROUP_RADIO,
			title, strlen (title) + 1);
	else
		return gimp_new_item (dialog_ID, parent_ID,
			GROUP_COLUMNS,
			title, strlen (title) + 1);
}

int
gimp_new_push_button (dialog_ID, parent_ID, title)
	int dialog_ID, parent_ID;
char *title;
{
	return gimp_new_item (dialog_ID, parent_ID, ITEM_PUSH_BUTTON,
		title, strlen (title) + 1);
}

int
gimp_new_check_button (dialog_ID, parent_ID, title)
	int dialog_ID, parent_ID;
char *title;
{
	return gimp_new_item (dialog_ID, parent_ID, ITEM_CHECK_BUTTON,
		title, strlen (title) + 1);
}

int
gimp_new_radio_button (dialog_ID, parent_ID, title)
	int dialog_ID, parent_ID;
char *title;
{
	return gimp_new_item (dialog_ID, parent_ID, ITEM_RADIO_BUTTON,
		title, strlen (title) + 1);
}

int
gimp_new_image_menu (int dialog_ID, int parent_ID, char constraint, char* title)
{
	long data[32];
	char *p;
	int length;
	
	length = strlen (title) + 1;
	
	memcpy (data, title, (length > 120) ? 120 : length);
	data[31] = 0;
	
	p = (char*) &data[30];
	p[2] = 0;
	p[3] = constraint;
	
	return gimp_new_item (dialog_ID, parent_ID, ITEM_IMAGE_MENU, data, 128);
}

int
gimp_new_scale (dialog_ID, parent_ID, min, max, start, prec)
	int dialog_ID, parent_ID;
long min, max, start, prec;
{
	long data[4];
	
	data[0] = min;
	data[1] = max;
	data[2] = start;
	data[3] = prec;
	
	return gimp_new_item (dialog_ID, parent_ID, ITEM_SCALE, data, sizeof (data));	/* (the Gobe Productive port: 4 longs, whatever their size) */
}

int
gimp_new_frame (dialog_ID, parent_ID, title)
	int dialog_ID, parent_ID;
char *title;
{
	return gimp_new_item (dialog_ID, parent_ID, ITEM_FRAME,
		title, strlen (title) + 1);
}

int
gimp_new_label (dialog_ID, parent_ID, title)
	int dialog_ID, parent_ID;
char *title;
{
	return gimp_new_item (dialog_ID, parent_ID, ITEM_LABEL,
		title, strlen (title) + 1);
}

int
gimp_new_text (dialog_ID, parent_ID, text)
	int dialog_ID, parent_ID;
char *text;
{
	return gimp_new_item (dialog_ID, parent_ID, ITEM_TEXT,
		text, strlen (text) + 1);
}

void
gimp_change_item (dialog_ID, item_ID, size, data)
	int dialog_ID, item_ID;
long size;
void *data;
{
	MsgDialog dlg;
	MsgDialog *ret;
	
	dlg.type = DIALOG_CHANGE_ITEM;
	dlg.item.dialog_ID = dialog_ID;
	dlg.item.item_ID = item_ID;
	memcpy (dlg.item.data, data, size);
	
	gimp_send (DIALOG, sizeof (MsgDialog), &dlg);
	
//	while (1)
//	{
		gimp_wait_ack ();
//		
//		ret = recv_msg.data;
//		if ((recv_msg.info.type == DIALOG) && (ret->type == DIALOG_CHANGE_ITEM))
//			return;
//	}
}

void
gimp_show_item (dialog_ID, item_ID)
	int dialog_ID, item_ID;
	{
		MsgDialog dlg;
		MsgDialog *ret;
		
		dlg.type = DIALOG_SHOW_ITEM;
		dlg.item.dialog_ID = dialog_ID;
		dlg.item.item_ID = item_ID;
		
		gimp_send (DIALOG, sizeof (MsgDialog), &dlg);
		gimp_wait_ack ();
		
		ret = info_msg.data;
		if ((info_msg.info.type == DIALOG) && (ret->type == DIALOG_SHOW_ITEM))
			return;
		else
		{
			unexpected_dialog_message(info_msg.info.type, DIALOG_SHOW_ITEM, ret->type);
		}
	}

void
gimp_hide_item (dialog_ID, item_ID)
	int dialog_ID, item_ID;
	{
		MsgDialog dlg;
		MsgDialog *ret;
		
		dlg.type = DIALOG_HIDE_ITEM;
		dlg.item.dialog_ID = dialog_ID;
		dlg.item.item_ID = item_ID;
		
		gimp_send (DIALOG, sizeof (MsgDialog), &dlg);
		gimp_wait_ack ();
		
		ret = info_msg.data;
		if ((info_msg.info.type == DIALOG) && (ret->type == DIALOG_HIDE_ITEM))
			return;
		else
		{
			unexpected_dialog_message(info_msg.info.type, DIALOG_HIDE_ITEM, ret->type);
		}
	}

void
gimp_delete_item (dialog_ID, item_ID)
	int dialog_ID, item_ID;
	{
		MsgDialog dlg;
		MsgDialog *ret;
		
		dlg.type = DIALOG_DELETE_ITEM;
		dlg.item.dialog_ID = dialog_ID;
		dlg.item.item_ID = item_ID;
		
		gimp_send (DIALOG, sizeof (MsgDialog), &dlg);
		gimp_wait_ack ();
		
		ret = info_msg.data;
		if ((info_msg.info.type == DIALOG) && (ret->type == DIALOG_DELETE_ITEM))
		{
			gimp_delete_dialog_item (dialog_ID, item_ID);
		}
		else
		{
			unexpected_dialog_message(info_msg.info.type, DIALOG_DELETE_ITEM, ret->type);
		}
	}

void
gimp_add_callback (dialog_ID, item_ID, callback, callback_data)
	int dialog_ID, item_ID;
GimpItemCallbackProc callback;
void *callback_data;
{
	gimp_add_item_callback (dialog_ID, item_ID, callback, callback_data);
}

static int
gimp_new_item (dialog_ID, parent_ID, type, data, data_size)
	int dialog_ID, parent_ID, type;
void *data;
long data_size;
{
	MsgDialog dlg;
	MsgDialog *ret;
	
	dlg.type = DIALOG_NEW_ITEM;
	dlg.item.dialog_ID = dialog_ID;
	dlg.item.parent_ID = parent_ID;
	dlg.item.item_type = type;
	
	if (data)
		memcpy (dlg.item.data, data, data_size);
	else
		memset (dlg.item.data, 0, 32);
	
	gimp_send (DIALOG, sizeof (MsgDialog), &dlg);
	gimp_wait_ack();
	gimp_wait_one ();
	
	ret = recv_msg.data;
	if ((recv_msg.info.type == DIALOG) && (ret->type == DIALOG_NEW_ITEM))
	{
		return ret->item.item_ID;
	}
	else
	{
		unexpected_message(DIALOG, recv_msg.info.type);
	}
	
	return -1;
}

static void
gimp_recv ()
{
	int err, size;
	
	size = sizeof (MsgHeader);
	while (size)
	{
		err = read (my_read,
			&recv_msg.info,
			sizeof (MsgHeader));
		size -= err;
	}
	
	if (recv_msg.info.size)
	{
		xfree (recv_msg.data);
		recv_msg.data = gimp_read (recv_msg.info.size);
	}
	
	if (recv_msg.info.type < n_gimp_procs)
	{
		if (gimp_procs[recv_msg.info.type])
			(* (gimp_procs[recv_msg.info.type])) (&recv_msg);
	}
	else
	{
		fatal ("unknown message received");
	}
}

static void
gimp_recv_info ()
{
	int err, size;
	
	size = sizeof (MsgHeader);
	while (size)
	{
		err = read (my_info,
			&info_msg.info,
			sizeof (MsgHeader));
		size -= err;
	}
	
	if (info_msg.info.size)
	{
		xfree (info_msg.data);
		info_msg.data = gimp_read_info (info_msg.info.size);
	}
}


static void
gimp_send (type, size, data)
	long type, size;
void *data;
{
	int err;
	send_msg.info.type = type;
	send_msg.info.size = size;
	send_msg.data = data;

	if (getenv("GIMP_DEBUG"))
		printf("PLUGIN> SEND %s\n", msg_name[send_msg.info.type]);

	
	err = gimp_write (sizeof (MsgHeader), &send_msg.info);
	err = gimp_write (send_msg.info.size, send_msg.data);
}

static void*
gimp_read (size)
	long size;
	{
		unsigned char *data;
		unsigned char *tmp;
		long bytes_read;
		
		data = xmalloc (size);
		tmp = data;
		
		while (size)
		{
			bytes_read = read (my_read, tmp, size);
			tmp += bytes_read;
			size -= bytes_read;
		}
		
		return data;
	}


static void*
gimp_read_info (size)
	long size;
	{
		unsigned char *data;
		unsigned char *tmp;
		long bytes_read;
		
		data = xmalloc (size);
		tmp = data;
		
		while (size)
		{
			bytes_read = read (my_info, tmp, size);
			tmp += bytes_read;
			size -= bytes_read;
		}
		
		return data;
	}

static int
gimp_write (size, data)
	long size;
void *data;
{
	return (write (my_write, data, size));
}

static int caught_fatal_sig = 0;

static void
gimp_signal (sig_num)
	int sig_num;
	{
		if (caught_fatal_sig)
			/*    raise (sig_num);*/
		kill (getpid (), sig_num);
		caught_fatal_sig = 1;
		
		switch (sig_num)
		{
		case SIGHUP:
			fprintf (stderr, "\n%s: sighup caught\n", progname);
			break;
		case SIGINT:
			fprintf (stderr, "\n%s: sigint caught\n", progname);
			break;
		case SIGQUIT:
			fprintf (stderr, "\n%s: sigquit caught\n", progname);
			break;
		case SIGABRT:
			fprintf (stderr, "\n%s: sigabrt caught\n", progname);
			break;
#ifndef __BEOS__ /* Be just defines SIGBUS to be SIGSEGV */
		case SIGBUS:
			fprintf (stderr, "\n%s: sigbus caught\n", progname);
			break;
#endif
		case SIGSEGV:
			fprintf (stderr, "\n%s: sigsegv caught\n", progname);
			break;
		case SIGPIPE:
			fprintf (stderr, "\n%s: sigpipe caught\n", progname);
			break;
		case SIGTERM:
			fprintf (stderr, "\n%s: sigterm caught\n", progname);
			break;
		default:
			fprintf (stderr, "\n%s: unknown signal caught\n", progname);
			break;
		}
		
		DEBUGGER("Signal caught");
		
		gimp_quit ();
	}

static void
gimp_wait_one ()
{
	/*  recv_msg.info.type = QUIT;
	while (recv_msg.info.type == QUIT) */
	gimp_recv ();
	if (getenv("GIMP_DEBUG"))
		printf("PLUGIN> RECV %s\n", msg_name[recv_msg.info.type]);
}

static void
gimp_wait_ack ()
{
	gimp_recv_info();
	if (getenv("GIMP_DEBUG"))
	{
		printf("PLUGIN> ACK %s", msg_name[info_msg.info.type]);
		if (info_msg.info.type == DIALOG)
			printf(" (%s %d)", dialog_msg_name[((MsgDialog*) info_msg.data)->type], ((MsgDialog*) info_msg.data)->item.item_ID);
		printf("\n");
	}
}

static void
gimp_handle_quit (msg)
	MsgP msg;
	{
		gimp_quit ();
	}

static void
gimp_load (msg)
	MsgP msg;
	{
		char *str;
		
		if (load_proc)
		{
			str = strdup (msg->data);
			(* load_proc) (str);
			xfree (str);
		}
	}

static void
gimp_save (msg)
	MsgP msg;
	{
		char *str;
		
		if (save_proc)
		{
			str = strdup (msg->data);
			(* save_proc) (str);
			xfree (str);
		}
	}

static void
gimp_dialog (msg)
	MsgP msg;
	{
		MsgDialog *dlg;
		
		dlg = msg->data;
		if (dlg->type == DIALOG_CALLBACK)
		{
			gimp_call_item_callback (dlg->callback.dialog_ID,
				dlg->callback.item_ID,
				dlg->callback.data);
		}
	}

static void
gimp_add_dialog (dialog_ID)
	int dialog_ID;
	{
		GimpDialog dlg;
		
		dlg = xmalloc (sizeof (_GimpDialog));
		
		dlg->dialog_ID = dialog_ID;
		dlg->displayed = 0;
		dlg->return_val = 0;
		dlg->items = NULL;
		
		dialogs = list_prepend (dialogs, dlg);
	}

static void
gimp_add_item_callback (dialog_ID, item_ID, callback, callback_data)
	int dialog_ID, item_ID;
GimpItemCallbackProc callback;
void *callback_data;
{
	GimpDialog dlg;
	GimpDialogItem item;
	
	dlg = gimp_find_dialog (dialog_ID);
	if (dlg)
	{
		item = xmalloc (sizeof (_GimpDialogItem));
		
		item->item_ID = item_ID;
		item->callback = callback;
		item->callback_data = callback_data;
		
		dlg->items = list_prepend (dlg->items, item);
	}
}

static void
gimp_delete_dialog (dialog_ID)
	int dialog_ID;
	{
		GimpDialog dlg;
		List tmp;
		
		dlg = gimp_find_dialog (dialog_ID);
		dialogs = list_remove (dialogs, dlg);
		
		tmp = dlg->items;
		while (tmp)
		{
			xfree (tmp->data);
			tmp = tmp->next;
		}
		
		list_free (dlg->items);
		xfree (dlg);
	}

static void
gimp_delete_dialog_item (dialog_ID, item_ID)
	int dialog_ID, item_ID;
	{
		GimpDialog dlg;
		GimpDialogItem item;
		
		dlg = gimp_find_dialog (dialog_ID);
		if (dlg)
		{
			item = gimp_find_item (dialog_ID, item_ID);
			if (item)
			{
				dlg->items = list_remove (dlg->items, item);
				xfree (item);
			}
		}
	}

static void
gimp_call_item_callback (dialog_ID, item_ID, call_data)
	int dialog_ID, item_ID;
void *call_data;
{
	GimpDialogItem item;
	
	item = gimp_find_item (dialog_ID, item_ID);
	if (item)
	{
		if (item->callback)
			(* item->callback) (item_ID, item->callback_data, call_data);
	}
}

static GimpDialog
gimp_find_dialog (dialog_ID)
	int dialog_ID;
	{
		GimpDialog dlg;
		List tmp;
		
		tmp = dialogs;
		while (tmp)
		{
			dlg = tmp->data;
			if (dlg->dialog_ID == dialog_ID)
				return dlg;
			
			tmp = tmp->next;
		}
		
		return NULL;
	}

static GimpDialogItem
gimp_find_item (dialog_ID, item_ID)
	int dialog_ID, item_ID;
	{
		GimpDialog dlg;
		GimpDialogItem item;
		List tmp;
		
		dlg = gimp_find_dialog (dialog_ID);
		if (dlg)
		{
			tmp = dlg->items;
			while (tmp)
			{
				item = tmp->data;
				if (item->item_ID == item_ID)
					return item;
				
				tmp = tmp->next;
			}
		}
		
		return NULL;
	}

static List
list_alloc ()
{
	List new_list;
	
	new_list = xmalloc (sizeof (struct _List));
	new_list->data = NULL;
	new_list->prev = NULL;
	new_list->next = NULL;
	
	return new_list;
}

static void
list_free (list)
	List list;
	{
		List tmp_list;
		
		while (list)
		{
			tmp_list = list;
			list = list->next;
			xfree (tmp_list);
		}
	}

static void
list_free_item (list)
	List list;
	{
		xfree (list);
	}

static List
list_prepend (list, data)
	List list;
void *data;
{
	List new_list;
	
	new_list = list_alloc ();
	new_list->data = data;
	
	if (list)
		list->prev = new_list;
	new_list->next = list;
	
	return new_list;
}

static List
list_remove (list, data)
	List list;
void *data;
{
	List tmp_list;
	
	if (!list)
		return NULL;
	
	tmp_list = list;
	while (tmp_list)
	{
		if (tmp_list->data == data)
		{
			if (tmp_list->prev)
				tmp_list->prev->next = tmp_list->next;
			if (tmp_list->next)
				tmp_list->next->prev = tmp_list->prev;
			
			if (list == tmp_list)
				list = list->next;
			
			list_free_item (tmp_list);
			
			break;
		}
		
		tmp_list = tmp_list->next;
	}
	
	return list;
}

static void*
xmalloc (size)
	long size;
	{
		void *mem;
		
		mem = malloc (size);
		if (!mem)
		{
			fatal ("unable to allocate memory");
		}
		
		return mem;
	}

static void
xfree (mem)
	void *mem;
	{
		if (mem)
			free (mem);
	}


static void
unexpected_message (int expected, int got)
{
	char msg[256], gots[256], *exs;
	
	exs = msg_name[expected];
	if (got >= QUIT && got <= DIALOG)
		sprintf(gots, "%s", msg_name[got]);
	else
		sprintf(gots, "%d", got);
	
	sprintf(msg, "unexpected message (got %s, but expected %s)", gots, exs);
	
	fatal(msg);
}

static void
unexpected_dialog_message(int gotmessage, int expectedtype, int gottype)
{
	char msg[256], gots[256], *exs;

	if (gotmessage != DIALOG)
		unexpected_message(DIALOG, gotmessage);
	
	exs = dialog_msg_name[expectedtype];
	if (gottype >= DIALOG_NEW && gottype <= DIALOG_CALLBACK)
		sprintf(gots, "%s", dialog_msg_name[gottype]);
	else
		sprintf(gots, "%d", gottype);
	
	sprintf(msg, "unexpected dialog message (got %s, but expected %s)", gots, exs);
	
	fatal(msg);
}


static void
fatal (error_str)
	char *error_str;
	{
		fprintf (stderr, "%s: %s\n", progname, error_str);
		gimp_quit ();
	}
