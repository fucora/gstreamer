/* Gnome-Streamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */


#ifndef __GST_META_VIDEORAW_H__
#define __GST_META_VIDEORAW_H__

#include <gst/gstmeta.h>

typedef struct _MetaVideoRaw MetaVideoRaw;

struct _MetaVideoRaw {
  GstMeta meta;

  /* formatting information */
  gint format;
  gint width;
  gint height;
};

#endif /* __GST_META_AUDIORAW_H__ */

