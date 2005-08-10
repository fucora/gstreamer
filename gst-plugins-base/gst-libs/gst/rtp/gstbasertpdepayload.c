/* GStreamer
 * Copyright (C) <2005> Philippe Khalaf <burger@speedy.org> 
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

#include "gstbasertpdepayload.h"

GST_DEBUG_CATEGORY (basertpdepayload_debug);
#define GST_CAT_DEFAULT (basertpdepayload_debug)

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_PROCESS_ONLY,
  ARG_QUEUEDELAY,
};

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp-noheader")
    );

static GstElementClass *parent_class = NULL;

static void gst_base_rtp_depayload_base_init (GstBaseRTPDepayloadClass * klass);
static void gst_base_rtp_depayload_class_init (GstBaseRTPDepayloadClass *
    klass);
static void gst_base_rtp_depayload_init (GstBaseRTPDepayload * filter,
    gpointer g_class);

static void gst_base_rtp_depayload_push (GstBaseRTPDepayload * filter,
    GstRTPBuffer * rtp_buf);

GType
gst_base_rtp_depayload_get_type (void)
{
  static GType plugin_type = 0;

  if (!plugin_type) {
    static const GTypeInfo plugin_info = {
      sizeof (GstBaseRTPDepayloadClass),
      (GBaseInitFunc) gst_base_rtp_depayload_base_init,
      NULL,
      (GClassInitFunc) gst_base_rtp_depayload_class_init,
      NULL,
      NULL,
      sizeof (GstBaseRTPDepayload),
      0,
      (GInstanceInitFunc) gst_base_rtp_depayload_init,
    };
    plugin_type = g_type_register_static (GST_TYPE_ELEMENT,
        "GstBaseRTPDepayload", &plugin_info, 0);
  }
  return plugin_type;
}

//static GstElementStateReturn gst_base_rtp_depayload_change_state (GstElement * element);
static void gst_base_rtp_depayload_finalize (GObject * object);
static void gst_base_rtp_depayload_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_base_rtp_depayload_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static gboolean gst_base_rtp_depayload_setcaps (GstPad * pad, GstCaps * caps);

static GstFlowReturn gst_base_rtp_depayload_chain (GstPad * pad,
    GstBuffer * in);

static GstFlowReturn gst_base_rtp_depayload_add_to_queue
    (GstBaseRTPDepayload * filter, GstRTPBuffer * in);

static void gst_base_rtp_depayload_set_gst_timestamp
    (GstBaseRTPDepayload * filter, guint32 timestamp, GstBuffer * buf);


static void
gst_base_rtp_depayload_base_init (GstBaseRTPDepayloadClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_factory));
}

static void
gst_base_rtp_depayload_class_init (GstBaseRTPDepayloadClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->set_property = gst_base_rtp_depayload_set_property;
  gobject_class->get_property = gst_base_rtp_depayload_get_property;

  g_object_class_install_property (gobject_class, ARG_QUEUEDELAY,
      g_param_spec_uint ("queue_delay", "Queue Delay",
          "Amount of ms to queue/buffer", 0, G_MAXUINT, 0, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, ARG_PROCESS_ONLY,
      g_param_spec_boolean ("process_only", "Process Only",
          "Directly send packets to processing", FALSE, G_PARAM_READWRITE));

  gobject_class->finalize = gst_base_rtp_depayload_finalize;

  klass->add_to_queue = gst_base_rtp_depayload_add_to_queue;
  klass->set_gst_timestamp = gst_base_rtp_depayload_set_gst_timestamp;

  GST_DEBUG_CATEGORY_INIT (basertpdepayload_debug, "basertpdepayload", 0,
      "Base class for RTP Depayloaders");
}

static void
gst_base_rtp_depayload_init (GstBaseRTPDepayload * filter, gpointer g_class)
{
  GstPadTemplate *pad_template;

  GST_DEBUG ("gst_base_rtp_depayload_init");

  pad_template =
      gst_element_class_get_pad_template (GST_ELEMENT_CLASS (g_class), "sink");
  g_return_if_fail (pad_template != NULL);
  filter->sinkpad = gst_pad_new_from_template (pad_template, "sink");
  gst_pad_set_setcaps_function (filter->sinkpad,
      gst_base_rtp_depayload_setcaps);
  gst_pad_set_chain_function (filter->sinkpad, gst_base_rtp_depayload_chain);
  gst_element_add_pad (GST_ELEMENT (filter), filter->sinkpad);

  pad_template =
      gst_element_class_get_pad_template (GST_ELEMENT_CLASS (g_class), "src");
  g_return_if_fail (pad_template != NULL);
  filter->srcpad = gst_pad_new_from_template (pad_template, "src");
  gst_element_add_pad (GST_ELEMENT (filter), filter->srcpad);

  // create out queue
  filter->queue = g_queue_new ();

  filter->queue_delay = RTP_QUEUEDELAY;

  // this one needs to be overwritten by child
  filter->clock_rate = 0;
}

static void
gst_base_rtp_depayload_finalize (GObject * object)
{
  // free our queue
  g_queue_free (GST_BASE_RTP_DEPAYLOAD (object)->queue);
  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_base_rtp_depayload_setcaps (GstPad * pad, GstCaps * caps)
{
  GstBaseRTPDepayload *filter;

//  GstStructure *structure;
//  int ret;

  filter = GST_BASE_RTP_DEPAYLOAD (gst_pad_get_parent (pad));
  g_return_val_if_fail (filter != NULL, FALSE);
  g_return_val_if_fail (GST_IS_BASE_RTP_DEPAYLOAD (filter), FALSE);

  /*
     structure = gst_caps_get_structure( caps, 0 );
     ret = gst_structure_get_int( structure, "clock_rate", &filter->clock_rate );
     if (!ret) {
     return FALSE;
     }
   */

  GstBaseRTPDepayloadClass *bclass = GST_BASE_RTP_DEPAYLOAD_GET_CLASS (filter);

  if (bclass->set_caps)
    return bclass->set_caps (filter, caps);
  else
    return TRUE;
}

static GstFlowReturn
gst_base_rtp_depayload_chain (GstPad * pad, GstBuffer * in)
{
  GstBaseRTPDepayload *filter;
  GstFlowReturn ret = GST_FLOW_OK;

  g_return_val_if_fail (GST_IS_PAD (pad), GST_FLOW_ERROR);
  g_return_val_if_fail (GST_BUFFER (in) != NULL, GST_FLOW_ERROR);

  filter = GST_BASE_RTP_DEPAYLOAD (GST_OBJECT_PARENT (pad));
  g_return_val_if_fail (GST_IS_BASE_RTP_DEPAYLOAD (filter), GST_FLOW_ERROR);

  g_return_val_if_fail (filter->clock_rate > 0, GST_FLOW_ERROR);

  // must supply RTPBuffers here
  g_return_val_if_fail (GST_IS_RTPBUFFER (in), GST_FLOW_ERROR);

  GstBaseRTPDepayloadClass *bclass = GST_BASE_RTP_DEPAYLOAD_GET_CLASS (filter);

  if (filter->process_only) {
    GST_DEBUG ("Pushing directly!");
    gst_base_rtp_depayload_push (filter, GST_RTPBUFFER (in));
  } else {
    if (bclass->add_to_queue)
      ret = bclass->add_to_queue (filter, GST_RTPBUFFER (in));
  }
  return ret;
}

static GstFlowReturn
gst_base_rtp_depayload_add_to_queue (GstBaseRTPDepayload * filter,
    GstRTPBuffer * in)
{
  GQueue *queue = filter->queue;

  // our first packet, just push it
  if (g_queue_is_empty (queue)) {
    g_queue_push_tail (queue, in);
  } else
    // not our first packet
  {
    // let us make sure it is not very late
    if (in->seqnum < GST_RTPBUFFER (g_queue_peek_head (queue))->seqnum) {
      // we need to drop this one
      GST_DEBUG ("Packet arrived to late, dropping");
      return GST_FLOW_OK;
    }
    // look for right place to insert it
    int i = 0;

    while (in->seqnum < GST_RTPBUFFER (g_queue_peek_nth (queue, i))->seqnum)
      i++;
    // now insert it at that place
    g_queue_push_nth (queue, in, i);
    GST_DEBUG ("Packet added to queue %d at pos %d timestamp %u sn %d",
        g_queue_get_length (queue), i, in->timestamp, in->seqnum);

    // if our queue is getting to big (more than RTP_QUEUEDELAY ms of data)
    // release heading buffers
    //GST_DEBUG("clockrate %d, queu_delay %d", filter->clock_rate, filter->queue_delay);
    gfloat q_size_secs = (gfloat) filter->queue_delay / 1000;
    guint maxtsunits = (gfloat) filter->clock_rate * q_size_secs;

    GST_DEBUG ("maxtsunit is %u", maxtsunits);
    GST_DEBUG ("ts %d %d %d %d", in->timestamp, in->seqnum,
        GST_RTPBUFFER (g_queue_peek_tail (queue))->timestamp,
        GST_RTPBUFFER (g_queue_peek_tail (queue))->seqnum);
    while (in->timestamp -
        GST_RTPBUFFER (g_queue_peek_tail (queue))->timestamp > maxtsunits) {
      GST_DEBUG ("Poping packet from queue");
      GstBaseRTPDepayloadClass *bclass =
          GST_BASE_RTP_DEPAYLOAD_GET_CLASS (filter);
      if (bclass->process) {
        GstRTPBuffer *in = g_queue_pop_tail (queue);

        gst_base_rtp_depayload_push (filter, GST_RTPBUFFER (in));
      }
    }
  }
  return GST_FLOW_OK;
}

static void
gst_base_rtp_depayload_push (GstBaseRTPDepayload * filter,
    GstRTPBuffer * rtp_buf)
{
  GstBaseRTPDepayloadClass *bclass = GST_BASE_RTP_DEPAYLOAD_GET_CLASS (filter);
  GstBuffer *out_buf;

  // let's send it out to processing
  out_buf = bclass->process (filter, GST_RTPBUFFER (rtp_buf));
  if (out_buf) {
    // set the caps
    gst_buffer_set_caps (GST_BUFFER (out_buf),
        gst_pad_get_caps (filter->srcpad));
    // set the timestamp
    // I am assuming here that the timestamp of the last RTP buffer
    // is the same as the timestamp wanted on the collector
    // maybe i should add a way to override this timestamp from the
    // depayloader child class
    bclass->set_gst_timestamp (filter, rtp_buf->timestamp, out_buf);
    // push it
    GST_DEBUG ("Pushing buffer size %d, timestamp %u",
        GST_BUFFER_SIZE (out_buf), GST_BUFFER_TIMESTAMP (out_buf));
    gst_pad_push (filter->srcpad, GST_BUFFER (out_buf));
    GST_DEBUG ("Pushed buffer");
  }
}

static void
gst_base_rtp_depayload_set_gst_timestamp (GstBaseRTPDepayload * filter,
    guint32 timestamp, GstBuffer * buf)
{
  static gboolean first = TRUE;

  // rtp timestamps are based on the clock_rate
  // gst timesamps are in nanoseconds
  GST_DEBUG ("calculating ts : timestamp : %u, clockrate : %u", timestamp,
      filter->clock_rate);
  guint64 ts = ((timestamp * GST_SECOND) / filter->clock_rate);

  GST_BUFFER_TIMESTAMP (buf) = ts;
  //GST_BUFFER_TIMESTAMP (buf) =
  //    (guint64)(((timestamp * GST_SECOND) / filter->clock_rate));
  GST_DEBUG ("calculated ts %"
      GST_TIME_FORMAT, GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)));

  // if this is the first buf send a discont
  if (first) {
    // send discont
    GstEvent *event = gst_event_new_newsegment (1.0, GST_FORMAT_TIME,
        ts, GST_CLOCK_TIME_NONE, 0);

    gst_pad_push_event (filter->srcpad, event);
    first = FALSE;
    GST_DEBUG ("Pushed discont on this first buffer");
  }
}

static void
gst_base_rtp_depayload_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstBaseRTPDepayload *filter;

  g_return_if_fail (GST_IS_BASE_RTP_DEPAYLOAD (object));
  filter = GST_BASE_RTP_DEPAYLOAD (object);

  switch (prop_id) {
    case ARG_QUEUEDELAY:
      filter->queue_delay = g_value_get_uint (value);
      break;
    case ARG_PROCESS_ONLY:
      filter->process_only = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_base_rtp_depayload_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstBaseRTPDepayload *filter;

  g_return_if_fail (GST_IS_BASE_RTP_DEPAYLOAD (object));
  filter = GST_BASE_RTP_DEPAYLOAD (object);

  switch (prop_id) {
    case ARG_QUEUEDELAY:
      g_value_set_uint (value, filter->queue_delay);
      break;
    case ARG_PROCESS_ONLY:
      g_value_set_boolean (value, filter->process_only);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
