/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wim.taymans@chello.be>
 *
 * gstevent.h: Header for GstEvent subsystem
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


#ifndef __GST_EVENT_H__
#define __GST_EVENT_H__

#include <gst/gsttypes.h>
#include <gst/gstdata.h>
#include <gst/gstformat.h>
#include <gst/gstobject.h>

G_BEGIN_DECLS

typedef enum {
  GST_EVENT_UNKNOWN,
  GST_EVENT_EOS,
  GST_EVENT_FLUSH,
  GST_EVENT_EMPTY,
  GST_EVENT_DISCONTINUOUS,
  GST_EVENT_NEW_MEDIA,
  GST_EVENT_QOS,
  GST_EVENT_SEEK,
  GST_EVENT_SEEK_SEGMENT,
  GST_EVENT_SEGMENT_DONE,
  GST_EVENT_SIZE,
  GST_EVENT_RATE,
  GST_EVENT_FILLER
} GstEventType;

extern GType _gst_event_type;

#define GST_TYPE_EVENT		(_gst_event_type)
#define GST_EVENT(event)	((GstEvent*)(event))
#define GST_IS_EVENT(event)	(GST_DATA_TYPE(event) == GST_TYPE_EVENT)

#define GST_EVENT_TYPE(event)		(GST_EVENT(event)->type)
#define GST_EVENT_TIMESTAMP(event)	(GST_EVENT(event)->timestamp)
#define GST_EVENT_SRC(event)		(GST_EVENT(event)->src)

#define GST_SEEK_FORMAT_SHIFT	0
#define GST_SEEK_METHOD_SHIFT	16
#define GST_SEEK_FLAGS_SHIFT	20
#define GST_SEEK_FORMAT_MASK	0x0000ffff
#define GST_SEEK_METHOD_MASK	0x000f0000
#define GST_SEEK_FLAGS_MASK	0xfff00000

typedef enum {
  GST_EVENT_FLAG_NONE = 0
} GstEventFlag;

typedef struct
{
  GstEventType 	type;
  GstEventFlag 	flags;
} GstEventMask;

/* seek events, extends GstEventFlag */
typedef enum {
  GST_SEEK_METHOD_CUR		= (1 << (GST_SEEK_METHOD_SHIFT + 0)),
  GST_SEEK_METHOD_SET		= (1 << (GST_SEEK_METHOD_SHIFT + 1)),
  GST_SEEK_METHOD_END		= (1 << (GST_SEEK_METHOD_SHIFT + 2)),

  GST_SEEK_FLAG_FLUSH		= (1 << (GST_SEEK_FLAGS_SHIFT + 0)),
  GST_SEEK_FLAG_ACCURATE	= (1 << (GST_SEEK_FLAGS_SHIFT + 1)),
  GST_SEEK_FLAG_KEY_UNIT	= (1 << (GST_SEEK_FLAGS_SHIFT + 2)),
  GST_SEEK_FLAG_SEGMENT_LOOP	= (1 << (GST_SEEK_FLAGS_SHIFT + 3))
} GstSeekType;

typedef enum {
  GST_SEEK_CERTAIN,
  GST_SEEK_FUZZY
} GstSeekAccuracy;

typedef struct
{
  GstFormat 	format;
  gint64	value;
} GstFormatValue;

#define GST_EVENT_SEEK_TYPE(event)		(GST_EVENT(event)->event_data.seek.type)
#define GST_EVENT_SEEK_FORMAT(event)		(GST_EVENT_SEEK_TYPE(event) & GST_SEEK_FORMAT_MASK)
#define GST_EVENT_SEEK_METHOD(event)		(GST_EVENT_SEEK_TYPE(event) & GST_SEEK_METHOD_MASK)
#define GST_EVENT_SEEK_FLAGS(event)		(GST_EVENT_SEEK_TYPE(event) & GST_SEEK_FLAGS_MASK)
#define GST_EVENT_SEEK_OFFSET(event)		(GST_EVENT(event)->event_data.seek.offset)
#define GST_EVENT_SEEK_ENDOFFSET(event)		(GST_EVENT(event)->event_data.seek.endoffset)
#define GST_EVENT_SEEK_ACCURACY(event)		(GST_EVENT(event)->event_data.seek.accuracy)

#define GST_EVENT_DISCONT_NEW_MEDIA(event)	(GST_EVENT(event)->event_data.discont.new_media)
#define GST_EVENT_DISCONT_OFFSET(event,i)	(GST_EVENT(event)->event_data.discont.offsets[i])
#define GST_EVENT_DISCONT_OFFSET_LEN(event)	(GST_EVENT(event)->event_data.discont.noffsets)

#define GST_EVENT_SIZE_FORMAT(event)		(GST_EVENT(event)->event_data.size.format)
#define GST_EVENT_SIZE_VALUE(event)		(GST_EVENT(event)->event_data.size.value)

#define GST_EVENT_RATE_VALUE(event)		(GST_EVENT(event)->event_data.rate.value)

struct _GstEvent {
  GstData data;

  GstEventType  type;
  guint64	timestamp;
  GstObject	*src;

  union {
    struct {
      GstSeekType 	type;
      gint64      	offset;
      gint64      	endoffset;
      GstSeekAccuracy 	accuracy;
    } seek;
    struct {
      GstFormatValue 	offsets[8];
      gint      	noffsets;
      gboolean		new_media;
    } discont;
    struct {
      GstFormat 	format;
      gint64      	value;
    } size;
    struct {
      gdouble      	value;
    } rate;
  } event_data;
};

void 		_gst_event_initialize 		(void);

void		gst_event_print_stats		(void);
	
GstEvent*	gst_event_new	        	(GstEventType type);

/* refcounting */
#define         gst_event_ref(ev)             	gst_data_ref (GST_DATA (ev))
#define         gst_event_ref_by_count(ev,c)  	gst_data_ref_by_count (GST_DATA (ev), c)
#define         gst_event_unref(ev)           	gst_data_unref (GST_DATA (ev))
/* copy buffer */
#define         gst_event_copy(ev)         	GST_EVENT (gst_data_copy (GST_DATA (ev)))

/* seek event */
GstEvent*	gst_event_new_seek		(GstSeekType type, gint64 offset);
GstEvent*	gst_event_new_segment_seek	(GstSeekType type, gint64 start, gint64 stop);

/* size events */
GstEvent*	gst_event_new_size		(GstFormat format, gint64 value);

/* discontinous event */
GstEvent*	gst_event_new_discontinuous	(gboolean new_media,
						 GstFormat format1, ...);
gboolean	gst_event_discont_get_value	(GstEvent *event, GstFormat format, gint64 *value);

#define		gst_event_new_filler()		gst_event_new(GST_EVENT_FILLER)

/* flush events */
#define		gst_event_new_flush()		gst_event_new(GST_EVENT_FLUSH)

G_END_DECLS

#endif /* __GST_EVENT_H__ */
