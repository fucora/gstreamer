/* RTP DTMF muxer element for GStreamer
 *
 * gstrtpdtmfmux.c:
 *
 * Copyright (C) <2007> Nokia Corporation.
 *   Contact: Zeeshan Ali <zeeshan.ali@nokia.com>
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *               2000,2005 Wim Taymans <wim@fluendo.com>
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

/**
 * SECTION:element-rtpdtmfmux
 * @short_description: mixes RTP DTMF streams into other RTP streams
 * <refsect2>
 * <para>
 * The RTPDTMFMuxer mixes/muxes RTP DTMF stream(s) into other RTP
 * streams. It does exactly what it's parent (RTPMuxer) does, except
 * that it allows upstream peer elements to request exclusive access
 * to the stream, which is required by the RTP DTMF standards (see RFC
 * 2833, section 3.2, para 1 for details). The peer upstream element
 * requests the acquisition and release of a stream lock beginning
 * using custom downstream gstreamer events. To request the acquisition
 * of the lock, the peer element must send an event of type
 * GST_EVENT_CUSTOM_DOWNSTREAM_OOB, having a
 * structure of name "stream-lock" with only one boolean field:
 * "lock". If this field is set to TRUE, the request is for the 
 * acquisition of the lock, otherwise it is for release of the lock.
 * </para>
 * <para>For example, the following code in an upstream peer element
 * requests the acquisition of the stream lock:
 * </para>
 * <para>
 * <programlisting>
 * GstEvent *event;
 * GstStructure *structure;
 * GstPad *srcpad;
 *
 * ... /\* srcpad initialization goes here \*\/
 *
 * structure = gst_structure_new ("stream-lock",
 *                    "lock", G_TYPE_BOOLEAN, TRUE, NULL);
 * 
 * event = gst_event_new_custom (GST_EVENT_CUSTOM_DOWNSTREAM_OOB, structure);
 * gst_pad_push_event (dtmfsrc->srcpad, event);
 * </programlisting>
 * </para>
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gstrtpdtmfmux.h>
#include <string.h>

GST_DEBUG_CATEGORY_STATIC (gst_rtp_dtmf_mux_debug);
#define GST_CAT_DEFAULT gst_rtp_dtmf_mux_debug

/* elementfactory information */
static const GstElementDetails gst_rtp_dtmf_mux_details =
GST_ELEMENT_DETAILS ("RTP muxer",
    "Codec/Muxer",
    "mixes RTP DTMF streams into other RTP streams",
    "Zeeshan Ali <first.last@nokia.com>");

static void gst_rtp_dtmf_mux_base_init (gpointer g_class);
static void gst_rtp_dtmf_mux_class_init (GstRTPDTMFMuxClass * klass);
static void gst_rtp_dtmf_mux_finalize (GObject * object);

static gboolean gst_rtp_dtmf_mux_sink_event (GstPad * pad,
    GstEvent * event);
static GstFlowReturn gst_rtp_dtmf_mux_chain (GstPad * pad,
    GstBuffer * buffer);

static GstRTPMuxClass *parent_class = NULL;

GType
gst_rtp_dtmf_mux_get_type (void)
{
  static GType mux_type = 0;

  if (!mux_type) {
    static const GTypeInfo mux_info = {
      sizeof (GstRTPDTMFMuxClass),
      gst_rtp_dtmf_mux_base_init,
      NULL,
      (GClassInitFunc) gst_rtp_dtmf_mux_class_init,
      NULL,
      NULL,
      sizeof (GstRTPDTMFMux),
      0,
      (GInstanceInitFunc) NULL,
    };

    mux_type =
        g_type_register_static (GST_TYPE_RTP_MUX, "GstRTPDTMFMux",
        &mux_info, 0);
  }
  return mux_type;
}

static void
gst_rtp_dtmf_mux_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (element_class, &gst_rtp_dtmf_mux_details);
}

static void
gst_rtp_dtmf_mux_class_init (GstRTPDTMFMuxClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstRTPMuxClass *gstrtpmux_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstrtpmux_class = (GstRTPMuxClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->finalize = gst_rtp_dtmf_mux_finalize;
  gstrtpmux_class->chain_func = gst_rtp_dtmf_mux_chain;
  gstrtpmux_class->sink_event_func = gst_rtp_dtmf_mux_sink_event;
}

static void
gst_rtp_dtmf_mux_finalize (GObject * object)
{
  GstRTPDTMFMux *mux;

  mux = GST_RTP_DTMF_MUX (object);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static GstFlowReturn
gst_rtp_dtmf_mux_chain (GstPad * pad, GstBuffer * buffer)
{
  GstRTPDTMFMux *mux;
  gboolean drop = FALSE;
  GstFlowReturn ret;
  
  mux = GST_RTP_DTMF_MUX (gst_pad_get_parent (pad));

  GST_OBJECT_LOCK (mux);
  if (mux->special_pad != NULL &&
      mux->special_pad != pad) {
    drop = TRUE;
  }

  if (drop) {
    gst_buffer_unref (buffer);
    ret = GST_FLOW_OK;
    GST_OBJECT_UNLOCK (mux);
  }

  else {
    GST_OBJECT_UNLOCK (mux);
    if (parent_class->chain_func)
      ret = parent_class->chain_func (pad, buffer);
    else
      ret = GST_FLOW_ERROR;
  }
    
  gst_object_unref (mux);
  return ret;
}

static void
gst_rtp_dtmf_mux_lock_stream (GstRTPDTMFMux *mux, GstPad * pad)
{
  if (mux->special_pad != NULL) {
    GST_WARNING_OBJECT (mux,
            "Stream lock already acquired by pad %s",
            GST_ELEMENT_NAME (mux->special_pad));
  }

  else
    mux->special_pad = gst_object_ref (pad);
}

static void
gst_rtp_dtmf_mux_unlock_stream (GstRTPDTMFMux *mux, GstPad * pad)
{
  if (mux->special_pad == NULL) {
    GST_WARNING_OBJECT (mux,
            "Stream lock not acquired, can't release it");
  }

  else if (pad != mux->special_pad) {
    GST_WARNING_OBJECT (mux,
            "pad %s attempted to release Stream lock"
            " which was acquired by pad %s", GST_ELEMENT_NAME (pad),
            GST_ELEMENT_NAME (mux->special_pad));
  }

  else {
    gst_object_unref (mux->special_pad);
    mux->special_pad = NULL;
  }
}

static gboolean
gst_rtp_dtmf_mux_handle_stream_lock_event (GstRTPDTMFMux *mux, GstPad * pad,
        const GstStructure * event_structure)
{
  gboolean lock;

  if (!gst_structure_get_boolean (event_structure, "lock", &lock))
    return FALSE;

  GST_OBJECT_LOCK (mux);
  if (lock)
    gst_rtp_dtmf_mux_lock_stream (mux, pad);
  else
    gst_rtp_dtmf_mux_unlock_stream (mux, pad);
  GST_OBJECT_UNLOCK (mux);

  return TRUE;
}

static gboolean
gst_rtp_dtmf_mux_handle_downstream_event (GstRTPDTMFMux *mux, GstPad * pad, GstEvent * event)
{
  const GstStructure *structure;
  gboolean ret = FALSE;

  structure = gst_event_get_structure (event);
  /* FIXME: is this event generic enough to be given a generic name? */
  if (structure && gst_structure_has_name (structure, "stream-lock")) {
    ret = gst_rtp_dtmf_mux_handle_stream_lock_event (mux, pad, structure);
  }

  return ret;
}

static gboolean
gst_rtp_dtmf_mux_ignore_event (GstPad * pad, GstEvent * event)
{
  gboolean ret;

  if (parent_class->sink_event_func) {
    /* Give the parent a chance to handle the event first */
    ret = parent_class->sink_event_func (pad, event);
  }

  else
    ret = gst_pad_event_default (pad, event);

  return ret;
}

static gboolean
gst_rtp_dtmf_mux_sink_event (GstPad * pad, GstEvent * event)
{
  GstRTPDTMFMux *mux;
  GstEventType type;
  gboolean ret = FALSE;

  type = event ? GST_EVENT_TYPE (event) : GST_EVENT_UNKNOWN;

  mux = (GstRTPDTMFMux *) gst_pad_get_parent (pad);

  switch (type) {
    case GST_EVENT_CUSTOM_DOWNSTREAM_OOB:
      ret = gst_rtp_dtmf_mux_handle_downstream_event (mux, pad, event);
      break;
    default:
      ret = gst_rtp_dtmf_mux_ignore_event (pad, event);
      break;
  }

  gst_object_unref (mux);
  return ret;
}

gboolean
gst_rtp_dtmf_mux_plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_rtp_dtmf_mux_debug, "rtpdtmfmux", 0,
      "rtp dtmf muxer");

  return gst_element_register (plugin, "rtpdtmfmux", GST_RANK_NONE,
      GST_TYPE_RTP_DTMF_MUX);
}

