/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstfakesink.c: 
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


#include <gstfakesink.h>


GstElementDetails gst_fakesink_details = {
  "Fake Sink",
  "Sink",
  "Black hole for data",
  VERSION,
  "Erik Walthinsen <omega@cse.ogi.edu>",
  "(C) 1999",
};


/* FakeSink signals and args */
enum {
  /* FILL ME */
  SIGNAL_HANDOFF,
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_NUM_SINKS,
  ARG_SILENT,
};

GST_PADTEMPLATE_FACTORY (fakesink_sink_factory,
  "sink%d",
  GST_PAD_SINK,
  GST_PAD_REQUEST,
  NULL                  /* no caps */
);


static void	gst_fakesink_class_init		(GstFakeSinkClass *klass);
static void	gst_fakesink_init		(GstFakeSink *fakesink);

static GstPad* 	gst_fakesink_request_new_pad 	(GstElement *element, GstPadTemplate *templ);

static void	gst_fakesink_set_property	(GObject *object, guint prop_id, 
						 const GValue *value, GParamSpec *pspec);
static void	gst_fakesink_get_property	(GObject *object, guint prop_id, 
						 GValue *value, GParamSpec *pspec);

static void	gst_fakesink_chain		(GstPad *pad, GstBuffer *buf);
static gboolean	gst_fakesink_event		(GstPad *pad, GstEventType event, guint64 timestamp, guint32 data);

static GstElementClass *parent_class = NULL;
static guint gst_fakesink_signals[LAST_SIGNAL] = { 0 };

GType
gst_fakesink_get_type (void) 
{
  static GType fakesink_type = 0;

  if (!fakesink_type) {
    static const GTypeInfo fakesink_info = {
      sizeof(GstFakeSinkClass),      NULL,
      NULL,
      (GClassInitFunc)gst_fakesink_class_init,
      NULL,
      NULL,
      sizeof(GstFakeSink),
      0,
      (GInstanceInitFunc)gst_fakesink_init,
    };
    fakesink_type = g_type_register_static (GST_TYPE_ELEMENT, "GstFakeSink", &fakesink_info, 0);
  }
  return fakesink_type;
}

static void
gst_fakesink_class_init (GstFakeSinkClass *klass) 
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_NUM_SINKS,
    g_param_spec_int ("num_sinks", "num_sinks", "num_sinks",
                      1, G_MAXINT, 1, G_PARAM_READABLE)); 
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_SILENT,
    g_param_spec_boolean ("silent", "silent", "silent",
                          FALSE, G_PARAM_READWRITE)); 

  gst_fakesink_signals[SIGNAL_HANDOFF] =
    g_signal_new ("handoff", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
                    G_STRUCT_OFFSET (GstFakeSinkClass, handoff), NULL, NULL,
                    g_cclosure_marshal_VOID__POINTER, G_TYPE_NONE, 1,
                    G_TYPE_POINTER);

  gobject_class->set_property = GST_DEBUG_FUNCPTR (gst_fakesink_set_property);
  gobject_class->get_property = GST_DEBUG_FUNCPTR (gst_fakesink_get_property);

  gstelement_class->request_new_pad = GST_DEBUG_FUNCPTR (gst_fakesink_request_new_pad);
}

static void 
gst_fakesink_init (GstFakeSink *fakesink) 
{
  GstPad *pad;
  pad = gst_pad_new ("sink", GST_PAD_SINK);
  gst_element_add_pad (GST_ELEMENT (fakesink), pad);
  gst_pad_set_chain_function (pad, GST_DEBUG_FUNCPTR (gst_fakesink_chain));
  gst_pad_set_event_function (pad, GST_DEBUG_FUNCPTR (gst_fakesink_event));

  fakesink->sinkpads = g_slist_prepend (NULL, pad);
  fakesink->numsinkpads = 1;
  fakesink->silent = FALSE;
}

static GstPad*
gst_fakesink_request_new_pad (GstElement *element, GstPadTemplate *templ)
{
  gchar *name;
  GstPad *sinkpad;
  GstFakeSink *fakesink;

  g_return_val_if_fail (GST_IS_FAKESINK (element), NULL);

  if (templ->direction != GST_PAD_SINK) {
    g_warning ("gstfakesink: request new pad that is not a SINK pad\n");
    return NULL;
  }

  fakesink = GST_FAKESINK (element);

  name = g_strdup_printf ("sink%d", fakesink->numsinkpads);

  sinkpad = gst_pad_new_from_template (templ, name);
  gst_element_add_pad (GST_ELEMENT (fakesink), sinkpad);

  fakesink->sinkpads = g_slist_prepend (fakesink->sinkpads, sinkpad);
  fakesink->numsinkpads++;

  return sinkpad;
}

static void
gst_fakesink_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstFakeSink *sink;

  /* it's not null if we got it, but it might not be ours */
  sink = GST_FAKESINK (object);

  switch (prop_id) {
    case ARG_SILENT:
      sink->silent = g_value_get_boolean (value);
      break;
    default:
      break;
  }
}

static void   
gst_fakesink_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstFakeSink *sink;
 
  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_FAKESINK (object));
 
  sink = GST_FAKESINK (object);
  
  switch (prop_id) {
    case ARG_NUM_SINKS:
      g_value_set_int (value, sink->numsinkpads);
      break;
    case ARG_SILENT:
      g_value_set_boolean (value, sink->silent);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/**
 * gst_fakesink_chain:
 * @pad: the pad this faksink is connected to
 * @buf: the buffer that has to be absorbed
 *
 * take the buffer from the pad and unref it without doing
 * anything with it.
 */
static void 
gst_fakesink_chain (GstPad *pad, GstBuffer *buf) 
{
  GstFakeSink *fakesink;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);

  fakesink = GST_FAKESINK (gst_pad_get_parent (pad));
  if (!fakesink->silent)
    g_print("fakesink: chain   ******* (%s:%s)< (%d bytes, %lld) \n",
		    GST_DEBUG_PAD_NAME (pad), GST_BUFFER_SIZE (buf), GST_BUFFER_TIMESTAMP (buf));
  
  g_signal_emit (G_OBJECT (fakesink), gst_fakesink_signals[SIGNAL_HANDOFF], 0,
                   buf);

  gst_buffer_unref (buf);
}

gboolean
gst_fakesink_factory_init (GstElementFactory *factory)
{
  gst_elementfactory_add_padtemplate (factory, GST_PADTEMPLATE_GET (fakesink_sink_factory));

  return TRUE;
}



static gboolean
gst_fakesink_event (GstPad *pad, GstEventType event, guint64 timestamp, guint32 data)
{
  GST_DEBUG (GST_CAT_EVENT, "fakesink has event %d on pad %s:%s\n",event,GST_DEBUG_PAD_NAME(pad));
  if (event == GST_EVENT_EOS) {
    GST_DEBUG(GST_CAT_EVENT, "have EOS\n");
  }
}
