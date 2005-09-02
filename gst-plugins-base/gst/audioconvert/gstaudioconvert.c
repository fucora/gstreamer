/* GStreamer
 * Copyright (C) 2003 Benjamin Otte <in7y118@public.uni-hamburg.de>
 * Copyright (C) 2005 Thomas Vander Stichele <thomas at apestaart dot org>
 * Copyright (C) 2005 Wim Taymans <wim at fluendo dot com>
 *
 * gstaudioconvert.c: Convert audio to different audio formats automatically
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

/*
 * design decisions:
 * - audioconvert converts buffers in a set of supported caps. If it supports
 *   a caps, it supports conversion from these caps to any other caps it
 *   supports. (example: if it does A=>B and A=>C, it also does B=>C)
 * - audioconvert does not save state between buffers. Every incoming buffer is
 *   converted and the converted buffer is pushed out.
 * conclusion:
 * audioconvert is not supposed to be a one-element-does-anything solution for
 * audio conversions.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include "gstaudioconvert.h"
#include "gstchannelmix.h"
#include "plugin.h"

GST_DEBUG_CATEGORY (audio_convert_debug);

/* int to float conversion: int2float(i) = 1 / (2^31-1) * i */
#define INT2FLOAT(i) (4.6566128752457969e-10 * ((gfloat)i))


/*** DEFINITIONS **************************************************************/

static GstElementDetails audio_convert_details = {
  "Audio Conversion",
  "Filter/Converter/Audio",
  "Convert audio to different formats",
  "Benjamin Otte <in7y118@public.uni-hamburg.de>",
};

/* type functions */
static void gst_audio_convert_dispose (GObject * obj);

/* gstreamer functions */
static gboolean gst_audio_convert_get_unit_size (GstBaseTransform * base,
    GstCaps * caps, guint * size);
static GstCaps *gst_audio_convert_transform_caps (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * caps);
static void gst_audio_convert_fixate_caps (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * caps, GstCaps * othercaps);
static gboolean gst_audio_convert_set_caps (GstBaseTransform * base,
    GstCaps * incaps, GstCaps * outcaps);
static GstFlowReturn gst_audio_convert_transform (GstBaseTransform * base,
    GstBuffer * inbuf, GstBuffer * outbuf);
static GstFlowReturn gst_audio_convert_transform_ip (GstBaseTransform * base,
    GstBuffer * buf);

/* AudioConvert signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_AGGRESSIVE
};

#define DEBUG_INIT(bla) \
  GST_DEBUG_CATEGORY_INIT (audio_convert_debug, "audioconvert", 0, "audio conversion element");

GST_BOILERPLATE_FULL (GstAudioConvert, gst_audio_convert, GstBaseTransform,
    GST_TYPE_BASE_TRANSFORM, DEBUG_INIT);

/*** GSTREAMER PROTOTYPES *****************************************************/

#define STATIC_CAPS \
GST_STATIC_CAPS ( \
  "audio/x-raw-float, " \
    "rate = (int) [ 1, MAX ], " \
    "channels = (int) [ 1, 8 ], " \
    "endianness = (int) BYTE_ORDER, " \
    "width = (int) 32, " \
    "buffer-frames = (int) [ 0, MAX ];" \
  "audio/x-raw-int, " \
    "rate = (int) [ 1, MAX ], " \
    "channels = (int) [ 1, 8 ], " \
    "endianness = (int) { LITTLE_ENDIAN, BIG_ENDIAN }, " \
    "width = (int) 32, " \
    "depth = (int) [ 1, 32 ], " \
    "signed = (boolean) { true, false }; " \
  "audio/x-raw-int, " \
    "rate = (int) [ 1, MAX ], " \
    "channels = (int) [ 1, 8 ], " \
    "endianness = (int) { LITTLE_ENDIAN, BIG_ENDIAN }, " \
    "width = (int) 16, " \
    "depth = (int) [ 1, 16 ], " \
    "signed = (boolean) { true, false }; " \
  "audio/x-raw-int, " \
    "rate = (int) [ 1, MAX ], " \
    "channels = (int) [ 1, 8 ], " \
    "endianness = (int) { LITTLE_ENDIAN, BIG_ENDIAN }, " \
    "width = (int) 8, " \
    "depth = (int) [ 1, 8 ], " \
    "signed = (boolean) { true, false } " \
)

/* FIXME: put back 24 bit audio */
#if 0
"audio/x-raw-int, "
    "rate = (int) [ 1, MAX ], "
    "channels = (int) [ 1, 8 ], "
    "endianness = (int) { LITTLE_ENDIAN, BIG_ENDIAN }, "
    "width = (int) 24, "
    "depth = (int) [ 1, 24 ], " "signed = (boolean) { true, false }; "
#endif
static GstAudioChannelPosition *supported_positions;

static GstStaticCaps gst_audio_convert_static_caps = STATIC_CAPS;

static GstStaticPadTemplate gst_audio_convert_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    STATIC_CAPS);

static GstStaticPadTemplate gst_audio_convert_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    STATIC_CAPS);

/*** TYPE FUNCTIONS ***********************************************************/

static void
gst_audio_convert_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_audio_convert_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_audio_convert_sink_template));
  gst_element_class_set_details (element_class, &audio_convert_details);
}

static void
gst_audio_convert_class_init (GstAudioConvertClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  gint i;

  gobject_class->dispose = gst_audio_convert_dispose;

  supported_positions = g_new0 (GstAudioChannelPosition,
      GST_AUDIO_CHANNEL_POSITION_NUM);
  for (i = 0; i < GST_AUDIO_CHANNEL_POSITION_NUM; i++)
    supported_positions[i] = i;

  GST_BASE_TRANSFORM_CLASS (klass)->get_unit_size =
      GST_DEBUG_FUNCPTR (gst_audio_convert_get_unit_size);
  GST_BASE_TRANSFORM_CLASS (klass)->transform_caps =
      GST_DEBUG_FUNCPTR (gst_audio_convert_transform_caps);
  GST_BASE_TRANSFORM_CLASS (klass)->fixate_caps =
      GST_DEBUG_FUNCPTR (gst_audio_convert_fixate_caps);
  GST_BASE_TRANSFORM_CLASS (klass)->set_caps =
      GST_DEBUG_FUNCPTR (gst_audio_convert_set_caps);
  GST_BASE_TRANSFORM_CLASS (klass)->transform_ip =
      GST_DEBUG_FUNCPTR (gst_audio_convert_transform_ip);
  GST_BASE_TRANSFORM_CLASS (klass)->transform =
      GST_DEBUG_FUNCPTR (gst_audio_convert_transform);
}

static void
gst_audio_convert_init (GstAudioConvert * this, GstAudioConvertClass * g_class)
{
}

static void
gst_audio_convert_dispose (GObject * obj)
{
  GstAudioConvert *this = GST_AUDIO_CONVERT (obj);

  audio_convert_clean_context (&this->ctx);

  G_OBJECT_CLASS (parent_class)->dispose (obj);
}

/*** GSTREAMER FUNCTIONS ******************************************************/

/* convert the given GstCaps to our format */
static gboolean
gst_audio_convert_parse_caps (const GstCaps * caps, AudioConvertFmt * fmt)
{
  GstStructure *structure = gst_caps_get_structure (caps, 0);

  GST_DEBUG ("parse caps %p and %" GST_PTR_FORMAT, caps, caps);

  g_return_val_if_fail (gst_caps_is_fixed (caps), FALSE);
  g_return_val_if_fail (fmt != NULL, FALSE);

  /* cleanup old */
  audio_convert_clean_fmt (fmt);

  fmt->endianness = G_BYTE_ORDER;
  fmt->is_int =
      (strcmp (gst_structure_get_name (structure), "audio/x-raw-int") == 0);

  /* parse common fields */
  if (!gst_structure_get_int (structure, "channels", &fmt->channels))
    goto no_values;
  if (!(fmt->pos = gst_audio_get_channel_positions (structure)))
    goto no_values;
  if (!gst_structure_get_int (structure, "width", &fmt->width))
    goto no_values;
  if (!gst_structure_get_int (structure, "rate", &fmt->rate))
    goto no_values;

  if (fmt->is_int) {
    /* int specific fields */
    if (!gst_structure_get_boolean (structure, "signed", &fmt->sign))
      goto no_values;
    if (!gst_structure_get_int (structure, "depth", &fmt->depth))
      goto no_values;

    /* width != 8 can have an endianness field */
    if (fmt->width != 8) {
      if (!gst_structure_get_int (structure, "endianness", &fmt->endianness))
        goto no_values;
    }
    /* depth cannot be bigger than the width */
    if (fmt->depth > fmt->width)
      goto not_allowed;
  } else {
    /* float specific fields */
    if (!gst_structure_get_int (structure, "buffer-frames",
            &fmt->buffer_frames))
      goto no_values;
  }

  fmt->unit_size = (fmt->width * fmt->channels) / 8;

  return TRUE;

  /* ERRORS */
no_values:
  {
    GST_DEBUG ("could not get some values from structure");
    audio_convert_clean_fmt (fmt);
    return FALSE;
  }
not_allowed:
  {
    GST_DEBUG ("width > depth, not allowed - make us advertise correct fmt");
    audio_convert_clean_fmt (fmt);
    return FALSE;
  }
}

/* BaseTransform vmethods */
static gboolean
gst_audio_convert_get_unit_size (GstBaseTransform * base, GstCaps * caps,
    guint * size)
{
  AudioConvertFmt fmt = { 0 };

  g_return_val_if_fail (size, FALSE);

  if (!gst_audio_convert_parse_caps (caps, &fmt))
    goto parse_error;

  *size = fmt.unit_size;

  audio_convert_clean_fmt (&fmt);

  return TRUE;

parse_error:
  {
    return FALSE;
  }
}

/* audioconvert can convert anything except sample rate; so return template
 * caps with rate fixed */
/* FIXME:
 * it would be smart here to return the caps with the same width as the first
 */
static GstCaps *
gst_audio_convert_transform_caps (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * caps)
{
  int i;
  const GValue *rate;
  GstCaps *ret;
  GstStructure *structure;

  g_return_val_if_fail (GST_CAPS_IS_SIMPLE (caps), NULL);

  structure = gst_caps_get_structure (caps, 0);

  ret = gst_static_caps_get (&gst_audio_convert_static_caps);

  /* if rate not set, we return the template */
  if (!(rate = gst_structure_get_value (structure, "rate")))
    return ret;

  /* else, write rate in the template caps */
  ret = gst_caps_make_writable (ret);

  for (i = 0; i < gst_caps_get_size (ret); ++i) {
    structure = gst_caps_get_structure (ret, i);
    gst_structure_set_value (structure, "rate", rate);
  }
  return ret;
}

/* try to keep as many of the structure members the same by fixating the
 * possible ranges; this way we convert the least amount of things as possible
 */
static void
gst_audio_convert_fixate_caps (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * caps, GstCaps * othercaps)
{
  GstStructure *ins, *outs;
  gint rate, endianness, depth, width, channels;
  gboolean signedness;

  g_return_if_fail (gst_caps_is_fixed (caps));

  GST_DEBUG_OBJECT (base, "trying to fixate othercaps %" GST_PTR_FORMAT
      " based on caps %" GST_PTR_FORMAT, othercaps, caps);

  ins = gst_caps_get_structure (caps, 0);
  outs = gst_caps_get_structure (othercaps, 0);

  if (gst_structure_get_int (ins, "channels", &channels)) {
    if (gst_structure_has_field (outs, "channels")) {
      gst_caps_structure_fixate_field_nearest_int (outs, "channels", channels);
    }
  }
  if (gst_structure_get_int (ins, "rate", &rate)) {
    if (gst_structure_has_field (outs, "rate")) {
      gst_caps_structure_fixate_field_nearest_int (outs, "rate", rate);
    }
  }
  if (gst_structure_get_int (ins, "endianness", &endianness)) {
    if (gst_structure_has_field (outs, "endianness")) {
      gst_caps_structure_fixate_field_nearest_int (outs, "endianness",
          endianness);
    }
  }
  if (gst_structure_get_int (ins, "width", &width)) {
    if (gst_structure_has_field (outs, "width")) {
      gst_caps_structure_fixate_field_nearest_int (outs, "width", width);
    }
  } else {
    /* this is not allowed */
  }

  if (gst_structure_get_int (ins, "depth", &depth)) {
    if (gst_structure_has_field (outs, "depth")) {
      gst_caps_structure_fixate_field_nearest_int (outs, "depth", depth);
    }
  } else {
    /* set depth as width */
    if (gst_structure_has_field (outs, "depth")) {
      gst_caps_structure_fixate_field_nearest_int (outs, "depth", width);
    }
  }

  if (gst_structure_get_boolean (ins, "signed", &signedness)) {
    if (gst_structure_has_field (outs, "signed")) {
      gst_caps_structure_fixate_field_boolean (outs, "signed", signedness);
    }
  }

  GST_DEBUG_OBJECT (base, "fixated othercaps to %" GST_PTR_FORMAT, othercaps);
}

static gboolean
gst_audio_convert_set_caps (GstBaseTransform * base, GstCaps * incaps,
    GstCaps * outcaps)
{
  AudioConvertFmt in_ac_caps = { 0 };
  AudioConvertFmt out_ac_caps = { 0 };
  GstAudioConvert *this = GST_AUDIO_CONVERT (base);

  GST_DEBUG_OBJECT (base, "incaps %" GST_PTR_FORMAT ", outcaps %"
      GST_PTR_FORMAT, incaps, outcaps);

  if (!gst_audio_convert_parse_caps (incaps, &in_ac_caps))
    return FALSE;
  if (!gst_audio_convert_parse_caps (outcaps, &out_ac_caps))
    return FALSE;

  if (!audio_convert_prepare_context (&this->ctx, &in_ac_caps, &out_ac_caps))
    goto no_converter;

  return TRUE;

no_converter:
  {
    return FALSE;
  }
}

static GstFlowReturn
gst_audio_convert_transform_ip (GstBaseTransform * base, GstBuffer * buf)
{
  /* nothing to do here */
  return GST_FLOW_OK;
}

static GstFlowReturn
gst_audio_convert_transform (GstBaseTransform * base, GstBuffer * inbuf,
    GstBuffer * outbuf)
{
  GstAudioConvert *this = GST_AUDIO_CONVERT (base);
  gboolean res;
  gint insize, outsize;
  gint samples;
  gpointer src, dst;

  /* get amount of samples to convert. */
  samples = GST_BUFFER_SIZE (inbuf) / this->ctx.in.unit_size;

  /* get in/output sizes, to see if the buffers we got are of correct
   * sizes */
  if (!(res = audio_convert_get_sizes (&this->ctx, samples, &insize, &outsize)))
    goto error;

  /* check in and outsize */
  if (GST_BUFFER_SIZE (inbuf) < insize)
    goto wrong_size;
  if (GST_BUFFER_SIZE (outbuf) < outsize)
    goto wrong_size;

  /* get src and dst data */
  src = GST_BUFFER_DATA (inbuf);
  dst = GST_BUFFER_DATA (outbuf);

  /* and convert the samples */
  if (!(res = audio_convert_convert (&this->ctx, src, dst,
              samples, gst_buffer_is_writable (inbuf))))
    goto error;

  return GST_FLOW_OK;

  /* ERRORS */
error:
  {
    return GST_FLOW_ERROR;
  }
wrong_size:
  {
    return GST_FLOW_ERROR;
  }
}
