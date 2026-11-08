#ifndef __MSG_H__
#define __MSG_H__
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
 * This file has been modified slightly for filters compiled for use
 * with Gobe Productive under BeOS.
 */

typedef enum {
  QUIT = 0,
  
  IMAGE_NEW,
  IMAGE_DISPLAY,
  IMAGE_INPUT,
  IMAGE_OUTPUT,
  IMAGE_COLOR,
  IMAGE_UPDATE,
  
  LOAD,
  SAVE,

  PARAMS,
  PROGRESS,
  MESSAGE,
  DIALOG
} MsgType;

typedef enum {
  PARAMS_GET = 0,
  PARAMS_SET
} MsgParamType;

typedef enum {
  IMAGE_TYPE_RGB = 1 << 0,
  IMAGE_TYPE_GRAY = 1 << 1,
  IMAGE_TYPE_INDEXED = 1 << 2,
  IMAGE_TYPE_ALL = 0xFFFF
} MsgImageType;

typedef enum {
  DIALOG_NEW = 0,
  DIALOG_SHOW,
  DIALOG_UPDATE,
  DIALOG_CLOSE,
  DIALOG_NEW_ITEM,
  DIALOG_SHOW_ITEM,
  DIALOG_HIDE_ITEM,
  DIALOG_CHANGE_ITEM,
  DIALOG_DELETE_ITEM,
  DIALOG_CALLBACK
} MsgDialogType;

typedef enum {
  COLOR_FOREGROUND = 0,
  COLOR_BACKGROUND
} MsgColorType;

typedef struct _MsgHeader        MsgHeader;
typedef struct _Msg              Msg, *MsgP;
typedef struct _MsgParams        MsgParams;
typedef struct _MsgProgress      MsgProgress;
typedef struct _MsgImage         MsgImage;
typedef struct _MsgColor         MsgColor;
typedef union  _MsgDialog        MsgDialog;
typedef struct _Dialog           Dialog;
typedef struct _DialogItem       DialogItem;
typedef struct _DialogCallback   DialogCallback;
typedef struct _MsgMessage       MsgMessage;

struct _MsgHeader {
  MsgType type;
  long    size;
};

struct _Msg {
  MsgHeader info;
  void      *data;
};

struct _MsgParams {
  MsgParamType type;
  long         size;
  void         *data;
};

struct _MsgProgress {
  long current;
  long max;
  char label[256];
};

struct _MsgImage {
  MsgImageType  type;
  char          name[256];
  int           ID, shmid;
  int           width, height, channels;
  int           x1, y1, x2, y2;
  long          data, colors;
  unsigned char cmap[768];
};

struct _MsgColor {
  MsgColorType  type;
  unsigned char r;
  unsigned char g;
  unsigned char b;
};

struct _Dialog {
  MsgDialogType type;
  int   dialog_ID;
  char  title[256];
};

struct _DialogItem {
  MsgDialogType type;
  int   dialog_ID;
  int   parent_ID;
  int   item_ID;
  int   item_type;
  char  data[256];
};

struct _DialogCallback {
  MsgDialogType type;
  int   dialog_ID;
  int   item_ID;
  char  data[256];
};

union _MsgDialog {
  MsgDialogType   type;
  Dialog          dialog;
  DialogItem      item;
  DialogCallback  callback;
};

struct _MsgMessage {
  char data[256];
};

#endif // __MSG_H__
