/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wim.taymans@chello.be>
 *
 * gstformat.h: Header for GstFormat types used in queries and
 *              seeking.
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


#ifndef __GST_FORMAT_H__
#define __GST_FORMAT_H__

#include <glib.h>

G_BEGIN_DECLS

typedef enum {
  GST_FORMAT_UNDEFINED 	=  0,
  GST_FORMAT_DEFAULT   	=  1,
  GST_FORMAT_BYTES   	=  2,
  GST_FORMAT_TIME 	=  3,
  GST_FORMAT_BUFFERS	=  4,
  GST_FORMAT_PERCENT	=  5,
  /* samples for audio, frames/fields for video */
  GST_FORMAT_UNITS 	=  6
} GstFormat;

typedef struct _GstFormatDefinition GstFormatDefinition;
struct _GstFormatDefinition 
{
  GstFormat  value;
  gchar     *nick;
  gchar     *description;
};

#ifdef G_HAVE_ISO_VARARGS
#define GST_FORMATS_FUNCTION(functionname, ...)      \
static const GstFormat*                              \
functionname (GstPad *pad)                           \
{                                                    \
  static const GstFormat formats[] = {               \
    __VA_ARGS__,                                     \
    0                                                \
  };                                                 \
  return formats;                                    \
}
#elif defined(G_HAVE_GNUC_VARARGS)
#define GST_FORMATS_FUNCTION(functionname, a...)     \
static const GstFormat*                              \
functionname (GstPad *pad)                           \
{                                                    \
  static const GstFormat formats[] = {               \
    a,                                               \
    0                                                \
  };                                                 \
  return formats;                                    \
}
#endif

void		_gst_format_initialize		(void);

/* register a new format */
GstFormat	gst_format_register		(const gchar *nick, 
						 const gchar *description);
GstFormat	gst_format_get_by_nick		(const gchar *nick);

/* query for format details */
const GstFormatDefinition*	
		gst_format_get_details		(GstFormat format);
const GList* 	gst_format_get_definitions 	(void);

G_END_DECLS

#endif /* __GST_FORMAT_H__ */
