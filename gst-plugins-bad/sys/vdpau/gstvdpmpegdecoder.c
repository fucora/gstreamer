/*
 * GStreamer
 * Copyright (C) 2009 Carl-Anton Ingmarsson <ca.ingmarsson@gmail.com>
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
 * SECTION:element-vdpaumpegdec
 *
 * FIXME:Describe vdpaumpegdec here.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v -m fakesrc ! vdpaumpegdec ! fakesink silent=TRUE
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gst/gst.h>
#include <string.h>

#include "mpegutil.h"
#include "gstvdpmpegdecoder.h"

GST_DEBUG_CATEGORY_STATIC (gst_vdp_mpeg_decoder_debug);
#define GST_CAT_DEFAULT gst_vdp_mpeg_decoder_debug

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0
};

/* the capabilities of the inputs and outputs.
 *
 * describe the real formats here.
 */
static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/mpeg, mpegversion = (int) [ 1, 2 ], "
        "systemstream = (boolean) false, parsed = (boolean) true")
    );

#define DEBUG_INIT(bla) \
GST_DEBUG_CATEGORY_INIT (gst_vdp_mpeg_decoder_debug, "vdpaumpegdec", 0, "VDPAU powered mpeg decoder");

GST_BOILERPLATE_FULL (GstVdpMpegDecoder, gst_vdp_mpeg_decoder,
    GstVdpDecoder, GST_TYPE_VDP_DECODER, DEBUG_INIT);

static void gst_vdp_mpeg_decoder_init_info (VdpPictureInfoMPEG1Or2 * vdp_info);
static void gst_vdp_mpeg_decoder_finalize (GObject * object);
static void gst_vdp_mpeg_decoder_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_vdp_mpeg_decoder_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static gboolean
gst_vdp_mpeg_decoder_set_caps (GstVdpDecoder * dec, GstCaps * caps)
{
  GstVdpMpegDecoder *mpeg_dec;
  GstStructure *structure;
  const GValue *value;
  GstBuffer *codec_data;
  MPEGSeqHdr hdr = { 0, };
  VdpDecoderProfile profile;
  GstVdpDevice *device;
  VdpStatus status;

  mpeg_dec = GST_VDP_MPEG_DECODER (dec);

  structure = gst_caps_get_structure (caps, 0);
  gst_structure_get_int (structure, "mpegversion", &mpeg_dec->version);
  if (mpeg_dec->version == 1)
    profile = VDP_DECODER_PROFILE_MPEG1;

  value = gst_structure_get_value (structure, "codec_data");
  codec_data = gst_value_get_buffer (value);
  mpeg_util_parse_sequence_hdr (&hdr, GST_BUFFER_DATA (codec_data),
      GST_BUFFER_DATA (codec_data) + GST_BUFFER_SIZE (codec_data));
  if (mpeg_dec->version != 1) {
    switch (hdr.profile) {
      case 5:
        profile = VDP_DECODER_PROFILE_MPEG2_SIMPLE;
        break;
      default:
        profile = VDP_DECODER_PROFILE_MPEG2_MAIN;
        break;
    }
  }

  memcpy (&mpeg_dec->vdp_info.intra_quantizer_matrix,
      &hdr.intra_quantizer_matrix, 64);
  memcpy (&mpeg_dec->vdp_info.non_intra_quantizer_matrix,
      &hdr.non_intra_quantizer_matrix, 64);

  device = dec->device;

  if (mpeg_dec->decoder != VDP_INVALID_HANDLE) {
    device->vdp_decoder_destroy (mpeg_dec->decoder);
    mpeg_dec->decoder = VDP_INVALID_HANDLE;
  }

  status = device->vdp_decoder_create (device->device, profile, dec->width,
      dec->height, 2, &mpeg_dec->decoder);
  if (status != VDP_STATUS_OK) {
    GST_ELEMENT_ERROR (mpeg_dec, RESOURCE, READ,
        ("Could not create vdpau decoder"),
        ("Error returned from vdpau was: %s",
            device->vdp_get_error_string (status)));
    return FALSE;
  }
  return TRUE;
}

typedef struct
{
  GstBuffer *buffer;
  VdpPictureInfoMPEG1Or2 vdp_info;
} GstVdpBFrame;

static GstFlowReturn
gst_vdp_mpeg_decoder_decode (GstVdpMpegDecoder * mpeg_dec,
    GstClockTime timestamp)
{
  GstVdpDecoder *dec;
  GstBuffer *buffer;
  GstVdpVideoBuffer *outbuf;
  VdpVideoSurface surface;
  GstVdpDevice *device;
  VdpBitstreamBuffer vbit[1];
  VdpStatus status;

  dec = GST_VDP_DECODER (mpeg_dec);

  buffer = gst_adapter_take_buffer (mpeg_dec->adapter,
      gst_adapter_available (mpeg_dec->adapter));

  if (mpeg_dec->vdp_info.picture_coding_type != B_FRAME) {
    if (mpeg_dec->vdp_info.backward_reference != VDP_INVALID_HANDLE) {
      GST_BUFFER_TIMESTAMP (mpeg_dec->b_buffer) = timestamp;
      gst_buffer_ref (mpeg_dec->b_buffer);
      gst_vdp_decoder_push_video_buffer (dec,
          GST_VDP_VIDEO_BUFFER (mpeg_dec->b_buffer));
    }

    if (mpeg_dec->vdp_info.forward_reference != VDP_INVALID_HANDLE) {
      gst_buffer_unref (mpeg_dec->f_buffer);
      mpeg_dec->vdp_info.forward_reference = VDP_INVALID_HANDLE;
    }

    mpeg_dec->vdp_info.forward_reference =
        mpeg_dec->vdp_info.backward_reference;
    mpeg_dec->f_buffer = mpeg_dec->b_buffer;

    mpeg_dec->vdp_info.backward_reference = VDP_INVALID_HANDLE;
  }

  outbuf = gst_vdp_video_buffer_new (dec->device, VDP_CHROMA_TYPE_420,
      dec->width, dec->height);
  if (mpeg_dec->vdp_info.forward_reference != VDP_INVALID_HANDLE)
    gst_vdp_video_buffer_add_reference (outbuf,
        GST_VDP_VIDEO_BUFFER (mpeg_dec->f_buffer));
  if (mpeg_dec->vdp_info.backward_reference != VDP_INVALID_HANDLE)
    gst_vdp_video_buffer_add_reference (outbuf,
        GST_VDP_VIDEO_BUFFER (mpeg_dec->b_buffer));

  surface = outbuf->surface;

  device = dec->device;

  vbit[0].struct_version = VDP_BITSTREAM_BUFFER_VERSION;
  vbit[0].bitstream = GST_BUFFER_DATA (buffer);
  vbit[0].bitstream_bytes = GST_BUFFER_SIZE (buffer);

  status = device->vdp_decoder_render (mpeg_dec->decoder, surface,
      (VdpPictureInfo *) & mpeg_dec->vdp_info, 1, vbit);
  gst_buffer_unref (buffer);
  mpeg_dec->vdp_info.slice_count = 0;

  if (status != VDP_STATUS_OK) {
    GST_ELEMENT_ERROR (mpeg_dec, RESOURCE, READ,
        ("Could not decode"),
        ("Error returned from vdpau was: %s",
            device->vdp_get_error_string (status)));

    gst_buffer_unref (GST_BUFFER (outbuf));

    return GST_FLOW_ERROR;
  }

  if (mpeg_dec->vdp_info.picture_coding_type == B_FRAME) {
    GST_BUFFER_TIMESTAMP (outbuf) = timestamp;
    gst_vdp_decoder_push_video_buffer (dec, GST_VDP_VIDEO_BUFFER (outbuf));
  } else {
    mpeg_dec->vdp_info.backward_reference = surface;
    mpeg_dec->b_buffer = GST_BUFFER (outbuf);
  }

  return GST_FLOW_OK;
}

static gboolean
gst_vdp_mpeg_decoder_parse_picture_coding (GstVdpMpegDecoder * mpeg_dec,
    guint8 * data, guint8 * end)
{
  GstVdpDecoder *dec;
  MPEGPictureExt pic_ext;
  VdpPictureInfoMPEG1Or2 *info;

  dec = GST_VDP_DECODER (mpeg_dec);
  info = &mpeg_dec->vdp_info;

  if (!mpeg_util_parse_picture_coding_extension (&pic_ext, data, end))
    return FALSE;

  memcpy (&mpeg_dec->vdp_info.f_code, &pic_ext.f_code, 4);

  info->intra_dc_precision = pic_ext.intra_dc_precision;
  info->picture_structure = pic_ext.picture_structure;
  info->top_field_first = pic_ext.top_field_first;
  info->frame_pred_frame_dct = pic_ext.frame_pred_frame_dct;
  info->concealment_motion_vectors = pic_ext.concealment_motion_vectors;
  info->q_scale_type = pic_ext.q_scale_type;
  info->intra_vlc_format = pic_ext.intra_vlc_format;
  info->alternate_scan = pic_ext.alternate_scan;

  return TRUE;
}

static gboolean
gst_vdp_mpeg_decoder_parse_sequence (GstVdpMpegDecoder * mpeg_dec,
    guint8 * data, guint8 * end)
{
  GstVdpDecoder *dec;
  MPEGSeqHdr hdr;

  dec = GST_VDP_DECODER (mpeg_dec);

  if (!mpeg_util_parse_sequence_hdr (&hdr, data, end))
    return FALSE;

  memcpy (&mpeg_dec->vdp_info.intra_quantizer_matrix,
      &hdr.intra_quantizer_matrix, 64);
  memcpy (&mpeg_dec->vdp_info.non_intra_quantizer_matrix,
      &hdr.non_intra_quantizer_matrix, 64);

  return TRUE;
}

static gboolean
gst_vdp_mpeg_decoder_parse_picture (GstVdpMpegDecoder * mpeg_dec,
    guint8 * data, guint8 * end)
{
  GstVdpDecoder *dec;
  MPEGPictureHdr pic_hdr;

  dec = GST_VDP_DECODER (mpeg_dec);

  if (!mpeg_util_parse_picture_hdr (&pic_hdr, data, end))
    return FALSE;

  if (pic_hdr.pic_type != I_FRAME
      && mpeg_dec->vdp_info.backward_reference == VDP_INVALID_HANDLE) {
    GST_DEBUG_OBJECT (mpeg_dec,
        "Drop frame since we haven't got an I_FRAME yet");
    return FALSE;
  }
  if (pic_hdr.pic_type == B_FRAME
      && mpeg_dec->vdp_info.forward_reference == VDP_INVALID_HANDLE) {
    GST_DEBUG_OBJECT (mpeg_dec,
        "Drop frame since we haven't got two non B_FRAMES yet");
    return FALSE;
  }

  mpeg_dec->vdp_info.picture_coding_type = pic_hdr.pic_type;

  if (mpeg_dec->version == 1) {
    mpeg_dec->vdp_info.full_pel_forward_vector =
        pic_hdr.full_pel_forward_vector;
    mpeg_dec->vdp_info.full_pel_backward_vector =
        pic_hdr.full_pel_backward_vector;
    memcpy (&mpeg_dec->vdp_info.f_code, &pic_hdr.f_code, 4);
  }

  return TRUE;
}

static gboolean
gst_vdp_mpeg_decoder_parse_gop (GstVdpMpegDecoder * mpeg_dec, guint8 * data,
    guint8 * end)
{
  MPEGPictureGOP gop;

  if (!mpeg_util_parse_picture_gop (&gop, data, end))
    return FALSE;

  mpeg_dec->broken_gop = gop.broken_gop;

  return TRUE;
}

static gboolean
gst_vdp_mpeg_decoder_parse_quant_matrix (GstVdpMpegDecoder * mpeg_dec,
    guint8 * data, guint8 * end)
{
  MPEGQuantMatrix qm;

  if (!mpeg_util_parse_quant_matrix (&qm, data, end))
    return FALSE;

  memcpy (&mpeg_dec->vdp_info.intra_quantizer_matrix,
      &qm.intra_quantizer_matrix, 64);
  memcpy (&mpeg_dec->vdp_info.non_intra_quantizer_matrix,
      &qm.non_intra_quantizer_matrix, 64);
  return TRUE;
}

static void
gst_vdp_mpeg_decoder_reset (GstVdpMpegDecoder * mpeg_dec)
{
  if (mpeg_dec->vdp_info.forward_reference != VDP_INVALID_HANDLE)
    gst_buffer_unref (mpeg_dec->f_buffer);
  if (mpeg_dec->vdp_info.backward_reference != VDP_INVALID_HANDLE)
    gst_buffer_unref (mpeg_dec->b_buffer);

  gst_vdp_mpeg_decoder_init_info (&mpeg_dec->vdp_info);

  gst_adapter_clear (mpeg_dec->adapter);
}

static GstFlowReturn
gst_vdp_mpeg_decoder_chain (GstPad * pad, GstBuffer * buffer)
{
  GstVdpMpegDecoder *mpeg_dec;
  guint8 *data, *end;
  guint32 sync_word = 0xffffffff;
  GstFlowReturn ret = GST_FLOW_OK;

  mpeg_dec = GST_VDP_MPEG_DECODER (GST_OBJECT_PARENT (pad));

  if (G_UNLIKELY (GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_DISCONT))) {
    GST_DEBUG_OBJECT (mpeg_dec, "Received discont buffer");
    gst_vdp_mpeg_decoder_reset (mpeg_dec);
    return GST_FLOW_OK;
  }

  data = GST_BUFFER_DATA (buffer);
  end = GST_BUFFER_DATA (buffer) + GST_BUFFER_SIZE (buffer);

  while ((data = mpeg_util_find_start_code (&sync_word, data, end))) {
    guint8 *packet_start;
    guint8 *packet_end;

    packet_start = data - 3;
    packet_end = mpeg_util_find_start_code (&sync_word, data, end);
    if (packet_end)
      packet_end -= 3;
    else
      packet_end = end;

    if (data[0] >= MPEG_PACKET_SLICE_MIN && data[0] <= MPEG_PACKET_SLICE_MAX) {
      GstBuffer *subbuf;

      GST_DEBUG_OBJECT (mpeg_dec, "MPEG_PACKET_SLICE");
      subbuf =
          gst_buffer_create_sub (buffer,
          packet_start - GST_BUFFER_DATA (buffer), packet_end - packet_start);
      gst_adapter_push (mpeg_dec->adapter, subbuf);
      mpeg_dec->vdp_info.slice_count++;
    }

    switch (data[0]) {
      case MPEG_PACKET_PICTURE:
        GST_DEBUG_OBJECT (mpeg_dec, "MPEG_PACKET_PICTURE");

        if (!gst_vdp_mpeg_decoder_parse_picture (mpeg_dec, packet_start,
                packet_end)) {
          return GST_FLOW_OK;
        }
        break;
      case MPEG_PACKET_SEQUENCE:
        GST_DEBUG_OBJECT (mpeg_dec, "MPEG_PACKET_SEQUENCE");
        gst_vdp_mpeg_decoder_parse_sequence (mpeg_dec, packet_start,
            packet_end);
        break;
      case MPEG_PACKET_EXTENSION:
        GST_DEBUG_OBJECT (mpeg_dec, "MPEG_PACKET_EXTENSION");
        switch (read_bits (data + 1, 0, 4)) {
          case MPEG_PACKET_EXT_PICTURE_CODING:
            GST_DEBUG_OBJECT (mpeg_dec, "MPEG_PACKET_EXT_PICTURE_CODING");
            gst_vdp_mpeg_decoder_parse_picture_coding (mpeg_dec, packet_start,
                packet_end);
            break;
          case MPEG_PACKET_EXT_QUANT_MATRIX:
            GST_DEBUG_OBJECT (mpeg_dec, "MPEG_PACKET_EXT_QUANT_MATRIX");
            gst_vdp_mpeg_decoder_parse_quant_matrix (mpeg_dec, packet_start,
                packet_end);
            break;
          default:
            break;
        }
        break;
      case MPEG_PACKET_GOP:
        GST_DEBUG_OBJECT (mpeg_dec, "MPEG_PACKET_GOP");
        gst_vdp_mpeg_decoder_parse_gop (mpeg_dec, packet_start, packet_end);
        break;
      default:
        break;
    }
  }

  if (mpeg_dec->vdp_info.slice_count > 0)
    ret = gst_vdp_mpeg_decoder_decode (mpeg_dec, GST_BUFFER_TIMESTAMP (buffer));

  return ret;
}

static gboolean
gst_vdp_mpeg_decoder_sink_event (GstPad * pad, GstEvent * event)
{
  GstVdpMpegDecoder *mpeg_dec;
  GstVdpDecoder *dec;
  gboolean res;

  mpeg_dec = GST_VDP_MPEG_DECODER (GST_OBJECT_PARENT (pad));
  dec = GST_VDP_DECODER (mpeg_dec);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_STOP:
    {
      GST_DEBUG_OBJECT (mpeg_dec, "flush stop");

      gst_vdp_mpeg_decoder_reset (mpeg_dec);
      res = gst_pad_push_event (dec->src, event);

      break;
    }
    default:
      res = gst_pad_event_default (pad, event);
  }

  return res;
}

static GstStateChangeReturn
gst_vdp_mpeg_decoder_change_state (GstElement * element,
    GstStateChange transition)
{
  GstVdpMpegDecoder *mpeg_dec;
  GstVdpDecoder *dec;
  GstStateChangeReturn ret;

  mpeg_dec = GST_VDP_MPEG_DECODER (element);
  dec = GST_VDP_DECODER (mpeg_dec);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      dec->device = gst_vdp_get_device (dec->display_name);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_vdp_mpeg_decoder_reset (mpeg_dec);

      dec->device->vdp_decoder_destroy (mpeg_dec->decoder);
      mpeg_dec->decoder = VDP_INVALID_HANDLE;

      g_object_unref (dec->device);
      dec->device = NULL;
      break;
    default:
      break;
  }

  return ret;
}

/* GObject vmethod implementations */

static void
gst_vdp_mpeg_decoder_base_init (gpointer gclass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (gclass);

  gst_element_class_set_details_simple (element_class,
      "VDPAU Mpeg Decoder",
      "Decoder",
      "decode mpeg stream with vdpau",
      "Carl-Anton Ingmarsson <ca.ingmarsson@gmail.com>");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_factory));
}

/* initialize the vdpaumpegdecoder's class */
static void
gst_vdp_mpeg_decoder_class_init (GstVdpMpegDecoderClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstVdpDecoderClass *vdpaudec_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  vdpaudec_class = (GstVdpDecoderClass *) klass;

  gobject_class->finalize = gst_vdp_mpeg_decoder_finalize;
  gobject_class->set_property = gst_vdp_mpeg_decoder_set_property;
  gobject_class->get_property = gst_vdp_mpeg_decoder_get_property;

  vdpaudec_class->set_caps = gst_vdp_mpeg_decoder_set_caps;

  gstelement_class->change_state = gst_vdp_mpeg_decoder_change_state;
}

static void
gst_vdp_mpeg_decoder_init_info (VdpPictureInfoMPEG1Or2 * vdp_info)
{
  vdp_info->forward_reference = VDP_INVALID_HANDLE;
  vdp_info->backward_reference = VDP_INVALID_HANDLE;
  vdp_info->slice_count = 0;
  vdp_info->picture_structure = 3;
  vdp_info->picture_coding_type = 0;
  vdp_info->intra_dc_precision = 0;
  vdp_info->frame_pred_frame_dct = 1;
  vdp_info->concealment_motion_vectors = 0;
  vdp_info->intra_vlc_format = 0;
  vdp_info->alternate_scan = 0;
  vdp_info->q_scale_type = 0;
  vdp_info->top_field_first = 1;
}

static void
gst_vdp_mpeg_decoder_init (GstVdpMpegDecoder * mpeg_dec,
    GstVdpMpegDecoderClass * gclass)
{
  GstVdpDecoder *dec;

  dec = GST_VDP_DECODER (mpeg_dec);

  mpeg_dec->decoder = VDP_INVALID_HANDLE;
  gst_vdp_mpeg_decoder_init_info (&mpeg_dec->vdp_info);

  mpeg_dec->broken_gop = FALSE;

  mpeg_dec->adapter = gst_adapter_new ();

  gst_pad_set_chain_function (dec->sink, gst_vdp_mpeg_decoder_chain);
  gst_pad_set_event_function (dec->sink, gst_vdp_mpeg_decoder_sink_event);
}

static void
gst_vdp_mpeg_decoder_finalize (GObject * object)
{
  GstVdpMpegDecoder *mpeg_dec = (GstVdpMpegDecoder *) object;

  g_object_unref (mpeg_dec->adapter);
}

static void
gst_vdp_mpeg_decoder_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_vdp_mpeg_decoder_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
