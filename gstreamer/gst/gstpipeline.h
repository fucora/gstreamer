/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstpipeline.h: Header for GstPipeline element
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


#ifndef __GST_PIPELINE_H__
#define __GST_PIPELINE_H__

#include <gst/gstbin.h>

G_BEGIN_DECLS

#define GST_TYPE_PIPELINE 		(gst_pipeline_get_type ())
#define GST_PIPELINE(obj) 		(G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_PIPELINE, GstPipeline))
#define GST_IS_PIPELINE(obj) 		(G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_PIPELINE))
#define GST_PIPELINE_CLASS(klass) 	(G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_PIPELINE, GstPipelineClass))
#define GST_IS_PIPELINE_CLASS(klass) 	(G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_PIPELINE))
#define GST_PIPELINE_GET_CLASS(obj) 	(G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_PIPELINE, GstPipelineClass))

typedef struct _GstPipeline GstPipeline;
typedef struct _GstPipelineClass GstPipelineClass;

typedef enum {
  /* this pipeline works with a fixed clock */
  GST_PIPELINE_FLAG_FIXED_CLOCK        = GST_BIN_FLAG_LAST,

  /* padding */
  GST_PIPELINE_FLAG_LAST               = GST_BIN_FLAG_LAST + 4
} GstPipelineFlags;

struct _GstPipeline {
  GstBin 	 bin;

  /*< public >*/ /* with LOCK */
  GstClock      *fixed_clock;	/* fixed clock if any */
  GstClockTime   stream_time;
  GstClockTime   delay;
  GstClockTime   play_timeout;

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};

struct _GstPipelineClass {
  GstBinClass parent_class;

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};

GType		gst_pipeline_get_type		(void);
GstElement*	gst_pipeline_new		(const gchar *name);

GstBus*		gst_pipeline_get_bus		(GstPipeline *pipeline);

void		gst_pipeline_set_new_stream_time  (GstPipeline *pipeline, GstClockTime time);
GstClockTime	gst_pipeline_get_last_stream_time (GstPipeline *pipeline);

void            gst_pipeline_use_clock          (GstPipeline *pipeline, GstClock *clock);
void            gst_pipeline_set_clock          (GstPipeline *pipeline, GstClock *clock);
GstClock*       gst_pipeline_get_clock          (GstPipeline *pipeline);
void            gst_pipeline_auto_clock         (GstPipeline *pipeline);

G_END_DECLS

#endif /* __GST_PIPELINE_H__ */

