/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstaggregator.h: Header for GstAggregator element
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


#ifndef __GST_AGGREGATOR_H__
#define __GST_AGGREGATOR_H__

#include <gst/gst.h>


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

extern GstElementDetails gst_aggregator_details;

typedef enum {
  AGGREGATOR_LOOP 		= 1,
  AGGREGATOR_LOOP_PEEK,
  AGGREGATOR_LOOP_SELECT,
  AGGREGATOR_CHAIN,
} GstAggregatorSchedType;

#define GST_TYPE_AGGREGATOR \
  (gst_aggregator_get_type())
#define GST_AGGREGATOR(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_AGGREGATOR,GstAggregator))
#define GST_AGGREGATOR_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_AGGREGATOR,GstAggregatorClass))
#define GST_IS_AGGREGATOR(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_AGGREGATOR))
#define GST_IS_AGGREGATOR_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_AGGREGATOR))

typedef struct _GstAggregator 		GstAggregator;
typedef struct _GstAggregatorClass 	GstAggregatorClass;

struct _GstAggregator {
  GstElement element;

  GstPad *srcpad;

  gboolean silent;
  GstAggregatorSchedType sched;

  gint numsinkpads;
  GList *sinkpads;
};

struct _GstAggregatorClass {
  GstElementClass parent_class;
};

GType 	gst_aggregator_get_type	(void);

gboolean 	gst_aggregator_factory_init 	(GstElementFactory *factory);

#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __GST_AGGREGATOR_H__ */
