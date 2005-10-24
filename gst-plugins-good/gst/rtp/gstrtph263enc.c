/* GStreamer
 * Copyright (C) <2005> Wim Taymans <wim@fluendo.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <string.h>

#include <gst/rtp/gstrtpbuffer.h>

#include "gstrtph263enc.h"

#define GST_RFC2190A_HEADER_LEN 4

typedef struct _GstRFC2190AHeader
{
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
  unsigned int ebit:3;          /* End position */
  unsigned int sbit:3;          /* Start position */
  unsigned int p:1;             /* PB-frames mode */
  unsigned int f:1;             /* flag bit */

  unsigned int r1:1;            /* Reserved */
  unsigned int a:1;             /* Advanced Prediction */
  unsigned int s:1;             /* syntax based arithmetic coding */
  unsigned int u:1;             /* Unrestricted motion vector */
  unsigned int i:1;             /* Picture coding type */
  unsigned int src:3;           /* Source format */

  unsigned int trb:3;           /* Temporal ref for B frame */
  unsigned int dbq:2;           /* Differential Quantisation parameter */
  unsigned int r2:3;            /* Reserved */
#elif G_BYTE_ORDER == G_BIG_ENDIAN
  unsigned int f:1;             /* flag bit */
  unsigned int p:1;             /* PB-frames mode */
  unsigned int sbit:3;          /* Start position */
  unsigned int ebit:3;          /* End position */

  unsigned int src:3;           /* Source format */
  unsigned int i:1;             /* Picture coding type */
  unsigned int u:1;             /* Unrestricted motion vector */
  unsigned int s:1;             /* syntax based arithmetic coding */
  unsigned int a:1;             /* Advanced Prediction */
  unsigned int r1:1;            /* Reserved */

  unsigned int r2:3;            /* Reserved */
  unsigned int dbq:2;           /* Differential Quantisation parameter */
  unsigned int trb:3;           /* Temporal ref for B frame */
#else
#error "G_BYTE_ORDER should be big or little endian."
#endif
  guint8 tr;                    /* Temporal ref for P frame */
} GstRFC2190AHeader;


#define GST_RFC2190A_HEADER_F(buf) (((GstRFC2190AHeader *)(buf))->f)
#define GST_RFC2190A_HEADER_P(buf) (((GstRFC2190AHeader *)(buf))->p)
#define GST_RFC2190A_HEADER_SBIT(buf) (((GstRFC2190AHeader *)(buf))->sbit)
#define GST_RFC2190A_HEADER_EBIT(buf) (((GstRFC2190AHeader *)(buf))->ebit)
#define GST_RFC2190A_HEADER_SRC(buf) (((GstRFC2190AHeader *)(buf))->src)
#define GST_RFC2190A_HEADER_I(buf) (((GstRFC2190AHeader *)(buf))->i)
#define GST_RFC2190A_HEADER_U(buf) (((GstRFC2190AHeader *)(buf))->u)
#define GST_RFC2190A_HEADER_S(buf) (((GstRFC2190AHeader *)(buf))->s)
#define GST_RFC2190A_HEADER_A(buf) (((GstRFC2190AHeader *)(buf))->a)
#define GST_RFC2190A_HEADER_R1(buf) (((GstRFC2190AHeader *)(buf))->r1)
#define GST_RFC2190A_HEADER_R2(buf) (((GstRFC2190AHeader *)(buf))->r2)
#define GST_RFC2190A_HEADER_DBQ(buf) (((GstRFC2190AHeader *)(buf))->dbq)
#define GST_RFC2190A_HEADER_TRB(buf) (((GstRFC2190AHeader *)(buf))->trb)
#define GST_RFC2190A_HEADER_TR(buf) (((GstRFC2190AHeader *)(buf))->tr)


typedef struct _GstH263PictureLayer
{
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
  unsigned int psc1:16;

  unsigned int tr1:2;
  unsigned int psc2:6;

  unsigned int ptype_263:1;
  unsigned int ptype_start:1;
  unsigned int tr2:6;

  unsigned int ptype_umvmode:1;
  unsigned int ptype_pictype:1;
  unsigned int ptype_srcformat:3;
  unsigned int ptype_freeze:1;
  unsigned int ptype_camera:1;
  unsigned int ptype_split:1;

  unsigned int chaff:5;
  unsigned int ptype_pbmode:1;
  unsigned int ptype_apmode:1;
  unsigned int ptype_sacmode:1;
#elif G_BYTE_ORDER == G_BIG_ENDIAN
  unsigned int psc1:16;

  unsigned int psc2:6;
  unsigned int tr1:2;

  unsigned int tr2:6;
  unsigned int ptype_start:1;
  unsigned int ptype_263:1;

  unsigned int ptype_split:1;
  unsigned int ptype_camera:1;
  unsigned int ptype_freeze:1;
  unsigned int ptype_srcformat:3;
  unsigned int ptype_pictype:1;
  unsigned int ptype_umvmode:1;

  unsigned int ptype_sacmode:1;
  unsigned int ptype_apmode:1;
  unsigned int ptype_pbmode:1;
  unsigned int chaff:5;
#else
#error "G_BYTE_ORDER should be big or little endian."
#endif
} GstH263PictureLayer;

#define GST_H263_PICTURELAYER_PLSRC(buf) (((GstH263PictureLayer *)(buf))->ptype_srcformat)
#define GST_H263_PICTURELAYER_PLTYPE(buf) (((GstH263PictureLayer *)(buf))->ptype_pictype)
#define GST_H263_PICTURELAYER_PLUMV(buf) (((GstH263PictureLayer *)(buf))->ptype_umvmode)
#define GST_H263_PICTURELAYER_PLSAC(buf) (((GstH263PictureLayer *)(buf))->ptype_sacmode)
#define GST_H263_PICTURELAYER_PLAP(buf) (((GstH263PictureLayer *)(buf))->ptype_apmode)



/* elementfactory information */
static GstElementDetails gst_rtp_h263enc_details = {
  "RTP packet parser",
  "Codec/Encoder/Network",
  "Encodes H263 video in RTP packets (RFC 2190)",
  "Neil Stratford <neils@vipadia.com>"
};

static GstStaticPadTemplate gst_rtph263enc_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-h263")
    );

static GstStaticPadTemplate gst_rtph263enc_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "media = (string) \"video\", "
        "payload = (int) [ 96, 255 ], "
        "clock-rate = (int) 90000, " "encoding-name = (string) \"H263-1998\"")
    );

static void gst_rtph263enc_class_init (GstRtpH263EncClass * klass);
static void gst_rtph263enc_base_init (GstRtpH263EncClass * klass);
static void gst_rtph263enc_init (GstRtpH263Enc * rtph263enc);
static void gst_rtph263enc_finalize (GObject * object);

static gboolean gst_rtph263enc_setcaps (GstBaseRTPPayload * payload,
    GstCaps * caps);
static GstFlowReturn gst_rtph263enc_handle_buffer (GstBaseRTPPayload * payload,
    GstBuffer * buffer);

static GstBaseRTPPayloadClass *parent_class = NULL;

static GType
gst_rtph263enc_get_type (void)
{
  static GType rtph263enc_type = 0;

  if (!rtph263enc_type) {
    static const GTypeInfo rtph263enc_info = {
      sizeof (GstRtpH263EncClass),
      (GBaseInitFunc) gst_rtph263enc_base_init,
      NULL,
      (GClassInitFunc) gst_rtph263enc_class_init,
      NULL,
      NULL,
      sizeof (GstRtpH263Enc),
      0,
      (GInstanceInitFunc) gst_rtph263enc_init,
    };

    rtph263enc_type =
        g_type_register_static (GST_TYPE_BASE_RTP_PAYLOAD, "GstRtpH263Enc",
        &rtph263enc_info, 0);
  }
  return rtph263enc_type;
}

static void
gst_rtph263enc_base_init (GstRtpH263EncClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtph263enc_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtph263enc_sink_template));

  gst_element_class_set_details (element_class, &gst_rtp_h263enc_details);
}

static void
gst_rtph263enc_class_init (GstRtpH263EncClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseRTPPayloadClass *gstbasertppayload_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasertppayload_class = (GstBaseRTPPayloadClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_BASE_RTP_PAYLOAD);

  gobject_class->finalize = gst_rtph263enc_finalize;

  gstbasertppayload_class->set_caps = gst_rtph263enc_setcaps;
  gstbasertppayload_class->handle_buffer = gst_rtph263enc_handle_buffer;
}

static void
gst_rtph263enc_init (GstRtpH263Enc * rtph263enc)
{
  rtph263enc->adapter = gst_adapter_new ();
}

static void
gst_rtph263enc_finalize (GObject * object)
{
  GstRtpH263Enc *rtph263enc;

  rtph263enc = GST_RTP_H263_ENC (object);

  g_object_unref (rtph263enc->adapter);
  rtph263enc->adapter = NULL;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_rtph263enc_setcaps (GstBaseRTPPayload * payload, GstCaps * caps)
{
  gst_basertppayload_set_options (payload, "video", TRUE, "H263-1998", 90000);
  gst_basertppayload_set_outcaps (payload, NULL);

  return TRUE;
}

static guint
gst_rtph263enc_gobfiner (guint8 * data, guint len, guint curpos)
{
  guint16 test = 0xffff;
  guint i;

  /* If we are past the end, stop */
  if (curpos >= len)
    return 0;

  /* start at curpos and loop looking for next gob start */
  for (i = curpos; i < len; i++) {
    test = (test << 8) | data[i];
    if ((test == 0) && (i > curpos + 4)) {
      return i - 3;
    }
  }
  return len;
}

static GstFlowReturn
gst_rtph263enc_flush (GstRtpH263Enc * rtph263enc)
{
  guint avail;
  GstBuffer *outbuf;
  GstFlowReturn ret = 0;
  gboolean fragmented;
  guint8 *data, *header;
  GstH263PictureLayer *piclayer;
  guint8 *payload;
  guint payload_len, total_len;
  guint curpos, nextgobpos;

  avail = gst_adapter_available (rtph263enc->adapter);
  if (avail == 0)
    return GST_FLOW_OK;

  fragmented = FALSE;

  /* Get a pointer to all the data for the frame */
  data = (guint8 *) gst_adapter_peek (rtph263enc->adapter, avail);

  /* Start at the begining and loop looking for gobs */
  curpos = 0;

  /* Picture header */
  piclayer = (GstH263PictureLayer *) data;

  while ((nextgobpos = gst_rtph263enc_gobfiner (data, avail, curpos)) > 0) {

    payload_len = nextgobpos - curpos;
    total_len = payload_len + GST_RFC2190A_HEADER_LEN;

    outbuf = gst_rtpbuffer_new_allocate (total_len, 0, 0);

    header = gst_rtpbuffer_get_payload (outbuf);
    payload = header + GST_RFC2190A_HEADER_LEN;

    /* Build the headers */
    GST_RFC2190A_HEADER_F (header) = 0;
    GST_RFC2190A_HEADER_P (header) = 0;
    GST_RFC2190A_HEADER_SBIT (header) = 0;
    GST_RFC2190A_HEADER_EBIT (header) = 0;

    GST_RFC2190A_HEADER_SRC (header) = GST_H263_PICTURELAYER_PLSRC (piclayer);
    GST_RFC2190A_HEADER_I (header) = GST_H263_PICTURELAYER_PLTYPE (piclayer);
    GST_RFC2190A_HEADER_U (header) = GST_H263_PICTURELAYER_PLUMV (piclayer);
    GST_RFC2190A_HEADER_S (header) = GST_H263_PICTURELAYER_PLSAC (piclayer);
    GST_RFC2190A_HEADER_A (header) = GST_H263_PICTURELAYER_PLAP (piclayer);
    GST_RFC2190A_HEADER_R1 (header) = 0;
    GST_RFC2190A_HEADER_R2 (header) = 0;
    GST_RFC2190A_HEADER_DBQ (header) = 0;
    GST_RFC2190A_HEADER_TRB (header) = 0;
    GST_RFC2190A_HEADER_TR (header) = 0;

    /* last fragment gets the marker bit set */
    gst_rtpbuffer_set_marker (outbuf, nextgobpos < avail ? 0 : 1);

    memcpy (payload, data + curpos, payload_len);

    GST_BUFFER_TIMESTAMP (outbuf) = rtph263enc->first_ts;

    ret = gst_basertppayload_push (GST_BASE_RTP_PAYLOAD (rtph263enc), outbuf);

    curpos = nextgobpos;
  }

  /* Flush the whole packet */
  gst_adapter_flush (rtph263enc->adapter, avail);

  return ret;
}

static GstFlowReturn
gst_rtph263enc_handle_buffer (GstBaseRTPPayload * payload, GstBuffer * buffer)
{
  GstRtpH263Enc *rtph263enc;
  GstFlowReturn ret;
  guint size;

  rtph263enc = GST_RTP_H263_ENC (payload);

  size = GST_BUFFER_SIZE (buffer);
  rtph263enc->first_ts = GST_BUFFER_TIMESTAMP (buffer);

  /* we always encode and flush a full picture */
  gst_adapter_push (rtph263enc->adapter, buffer);
  ret = gst_rtph263enc_flush (rtph263enc);

  return ret;
}

gboolean
gst_rtph263enc_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rtph263enc",
      GST_RANK_NONE, GST_TYPE_RTP_H263_ENC);
}
