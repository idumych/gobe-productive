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
 */

/* This file has been modified slightly for filters compiled for use
 * with Gobe Productive under BeOS.		7/1/98
 */

#ifndef __GIMP_H__
#define __GIMP_H__


#ifdef __cplusplus
extern "C" {
#endif

#ifndef NULL
#define NULL 0
#endif  /*  NULL  */

/*
 * Type definitions.
 */

typedef void (*GimpLoadSaveProc) (char *);

/*
 * Initialization and low level routines
 */

int   gimp_init (int, char **);
void  gimp_install_load_save_handlers (GimpLoadSaveProc, GimpLoadSaveProc);
void  gimp_main_loop (void);
void  gimp_quit (void);

void* gimp_get_params (void);
void  gimp_set_params (long, void *);
void  gimp_init_progress (char *);
void  gimp_do_progress (int, int);
void  gimp_message (char *);

/*
 * Image handling
 */

/* An image is an opaque type */
typedef struct _Image *Image;

/* There are 3 different image types and the unknown type 
 * Do not create an image of type unknown.
 * If an images type is returned as unknown then there is most likely
 *  something wrong with the library and its communication with the GIMP.
 */
typedef enum {
  RGB_IMAGE = 0,
  GRAY_IMAGE,	/* Incompletely supported by Gobe Productive 1.0 */
  INDEXED_IMAGE,
  RGBA_IMAGE,	/* Not supported by Gobe Productive 1.0 */
  GRAYA_IMAGE,	/* Not supported by Gobe Productive 1.0 */
  UNKNOWN_IMAGE
} ImageType;

/* `gimp_free_image' should be called when a plug-in is done using an
 *  image. It simply detaches from the shared memory segment.
 *
 * `gimp_destroy_image' will detach from the shared memory segment and
 *  tell the GIMP to detach from it as well and to destroy the image.
 *  A plug-in can only destroy images it has created.
 *
 * `gimp_new_image' will return a newly created image. The image is not
 *  yet visible. (It has not been displayed). 
 *
 * `gimp_get_input_image' will return an input image corresponding to the
 *  given ID. If the ID is 0 then the returned image will be the default
 *  input image for the plug-in. (ie. The one the plug-in was called from).
 *  Note: the shared memory segment is attached read only so do no try to
 *        modify the image.
 *
 * `gimp_get_output_image' functions similarly to `gimp_get_input_image'
 *  except the shared memory segment is attached read-write. Note: the input
 *  and output images for a given ID are different pieces of memory. This means
 *  the output image can be written to and the input image will not be
 *  affected.
 *
 * `gimp_get_indexed_image' is an outdated function and is about to go.
 *
 * `gimp_display_image' tells the GIMP to display the given image in a new window.
 *
 * `gimp_update_image' tells the GIMP to redraw a given image in all of
 *  its displayed windows. This must be called after a `gimp_display_image' in
 *  order to get the image to redraw correctly.
 */

void      gimp_free_image (Image);
void      gimp_destroy_image (Image);
Image     gimp_new_image (char *, long, long, ImageType);
Image     gimp_get_input_image (long);
Image     gimp_get_output_image (long);

void      gimp_display_image (Image);
void      gimp_update_image (Image);

char*     gimp_image_name (Image);
long      gimp_image_width (Image);
long      gimp_image_height (Image);
long      gimp_image_channels (Image);
ImageType gimp_image_type (Image);
void      gimp_image_area (Image, int *, int *, int *, int *);

unsigned char*	  gimp_image_data (Image); /* Because some filters do skanky pointer math, expecting it to be treated as uchar*, automagically! */

void*     gimp_image_cmap (Image);
long      gimp_image_colors (Image);

void      gimp_set_image_colors (Image, void *, long);

void      gimp_foreground_color (unsigned char *, unsigned char *, unsigned char *);
void      gimp_background_color (unsigned char *, unsigned char *, unsigned char *);


/*
 * Dialog handling
 */

#define DEFAULT  0

#define NORMAL   0
#define RADIO    1

#define IMAGE_CONSTRAIN_RGB     1 << 0
#define IMAGE_CONSTRAIN_GRAY    1 << 1
#define IMAGE_CONSTRAIN_INDEXED 1 << 1
#define IMAGE_CONSTRAIN_ALL     0xFF

typedef void (*GimpItemCallbackProc) (int, void *, void *);

int   gimp_new_dialog (char *);
int   gimp_show_dialog (int);
void  gimp_update_dialog (int);
void  gimp_close_dialog (int, int);

int   gimp_ok_item_id (int);
int   gimp_cancel_item_id (int);

int   gimp_new_row_group (int, int, int, char *);
int   gimp_new_column_group (int, int, int, char *);
int   gimp_new_push_button (int, int, char *);
int   gimp_new_check_button (int, int, char *);
int   gimp_new_radio_button (int, int, char *);
int   gimp_new_image_menu (int, int, char, char *);
int   gimp_new_scale (int, int, long, long, long, long);
int   gimp_new_frame (int, int, char *);
int   gimp_new_label (int, int, char *);
int   gimp_new_text (int, int, char *);

void  gimp_change_item (int, int, long, void *);
void  gimp_show_item (int, int);
void  gimp_hide_item (int, int);
void  gimp_delete_item (int, int);

void  gimp_add_callback (int, int, GimpItemCallbackProc, void *);

#ifdef __cplusplus
}
#endif

#endif /* __GIMP_H__ */
