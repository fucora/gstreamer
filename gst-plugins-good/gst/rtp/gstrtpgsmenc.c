/* GStreamer
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <stdlib.h>
#include <string.h>
#include <gst/rtp/gstrtpbuffer.h>

#include "gstrtpgsmenc.h"

/* elementfactory information */
static GstElementDetails gst_rtpgsmenc_details = {
  "RTP GSM Audio Encoder",
  "Codec/Encoder/Network",
  "Encodes GSM audio into a RTP packet",
  "Zeeshan Ali <zeenix@gmail.com>"
};

static GstStaticPadTemplate gst_rtpgsmenc_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-gsm, " "rate = (int) 8000, " "channels = (int) 1")
    );

static GstStaticPadTemplate gst_rtpgsmenc_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "media = (string) \"audio\", "
        "payload = (int) [ 96, 255 ], "
        "clock-rate = (int) 8000, " "encoding-name = (string) \"GSM\"")
    );

static gboolean gst_rtpgsmenc_setcaps (GstBaseRTPPayload * payload,
    GstCaps * caps);
static GstFlowReturn gst_rtpgsmenc_handle_buffer (GstBaseRTPPayload * payload,
    GstBuffer * buffer);

GST_BOILERPLATE (GstRTPGSMEnc, gst_rtpgsmenc, GstBaseRTPPayload,
    GST_TYPE_BASE_RTP_PAYLOAD);

static void
gst_rtpgsmenc_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtpgsmenc_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtpgsmenc_src_template));
  gst_element_class_set_details (element_class, &gst_rtpgsmenc_details);
}

static void
gst_rtpgsmenc_class_init (GstRTPGSMEncClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseRTPPayloadClass *gstbasertppayload_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasertppayload_class = (GstBaseRTPPayloadClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_BASE_RTP_PAYLOAD);

  gstbasertppayload_class->set_caps = gst_rtpgsmenc_setcaps;
  gstbasertppayload_class->handle_buffer = gst_rtpgsmenc_handle_buffer;
}

static void
gst_rtpgsmenc_init (GstRTPGSMEnc * rtpgsmenc, GstRTPGSMEncClass * klass)
{
  GST_BASE_RTP_PAYLOAD (rtpgsmenc)->clock_rate = 8000;
  GST_BASE_RTP_PAYLOAD_PT (rtpgsmenc) = GST_RTP_PAYLOAD_GSM;
}

static gboolean
gst_rtpgsmenc_setcaps (GstBaseRTPPayload * payload, GstCaps * caps)
{
  const char *stname;
  GstStructure *structure;

  structure = gst_caps_get_structure (caps, 0);

  stname = gst_structure_get_name (structure);

  if (0 == strcmp ("audio/x-gsm", stname)) {
    gst_basertppayload_set_options (payload, "audio", FALSE, "GSM", 8000);
  } else {
    return FALSE;
  }

  gst_basertppayload_set_outcaps (payload, NULL);

  return TRUE;
}

static GstFlowReturn
gst_rtpgsmenc_handle_buffer (GstBaseRTPPayload * basepayload,
    GstBuffer * buffer)
{
  GstRTPGSMEnc *rtpgsmenc;
  guint size, payload_len;
  GstBuffer *outbuf;
  guint8 *payload, *data;
  GstClockTime timestamp;
  GstFlowReturn ret;

  rtpgsmenc = GST_RTP_GSM_ENC (basepayload);

  size = GST_BUFFER_SIZE (buffer);
  timestamp = GST_BUFFER_TIMESTAMP (buffer);

  /* FIXME, only one GSM frame per RTP packet for now */
  payload_len = size;

  outbuf = gst_rtpbuffer_new_allocate (payload_len, 0, 0);
  /* FIXME, assert for now */
  g_assert (payload_len <= GST_BASE_RTP_PAYLOAD_MTU (rtpgsmenc));

  /* copy timestamp */
  GST_BUFFER_TIMESTAMP (outbuf) = timestamp;

  /* get payload */
  payload = gst_rtpbuffer_get_payload (outbuf);

  data = GST_BUFFER_DATA (buffer);

  /* copy data in payload */
  memcpy (&payload[0], data, size);

  gst_buffer_unref (buffer);

  GST_DEBUG ("gst_rtpgsmenc_chain: pushing buffer of size %d",
      GST_BUFFER_SIZE (outbuf));

  ret = gst_basertppayload_push (basepayload, outbuf);

  return ret;
}

gboolean
gst_rtpgsmenc_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rtpgsmenc",
      GST_RANK_NONE, GST_TYPE_RTP_GSM_ENC);
}
