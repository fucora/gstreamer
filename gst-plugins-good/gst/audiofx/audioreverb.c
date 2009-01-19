/* 
 * GStreamer
 * Copyright (C) 2009 Sebastian Dröge <sebastian.droege@collabora.co.uk>
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
 * SECTION:element-audioreverb
 *
 * <refsect2>
 * audioreverb adds an echo or revert effect to an audio stream. The echo
 * reverb, intensity and the percentage of feedback can be configured.
 * <para>
 * <programlisting>
 * gst-launch filesrc location="melo1.ogg" ! audioconvert ! audioreverb reverb=500000000 intensity=0.6 feedback=0.4 ! audioconvert ! autoaudiosink
 * gst-launch filesrc location="melo1.ogg" ! decodebin ! audioconvert ! audioreverb reverb=50000000 intensity=0.6 feedback=0.4 ! audioconvert ! autoaudiosink
 * </programlisting>
 * </para>
 * </refsect2>
 *
 * Since: 0.10.12
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include <gst/audio/audio.h>
#include <gst/audio/gstaudiofilter.h>
#include <gst/controller/gstcontroller.h>

#include "audioreverb.h"

#define GST_CAT_DEFAULT gst_audio_reverb_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

enum
{
  PROP_0,
  PROP_DELAY,
  PROP_INTENSITY,
  PROP_FEEDBACK
};

#define ALLOWED_CAPS \
    "audio/x-raw-float,"                                              \
    " width=(int) { 32, 64 }, "                                       \
    " endianness=(int)BYTE_ORDER,"                                    \
    " rate=(int)[1,MAX],"                                             \
    " channels=(int)[1,MAX]"

#define DEBUG_INIT(bla) \
  GST_DEBUG_CATEGORY_INIT (gst_audio_reverb_debug, "audioreverb", 0, "audioreverb element");

GST_BOILERPLATE_FULL (GstAudioReverb, gst_audio_reverb, GstAudioFilter,
    GST_TYPE_AUDIO_FILTER, DEBUG_INIT);

static void gst_audio_reverb_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_audio_reverb_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_audio_reverb_finalize (GObject * object);

static gboolean gst_audio_reverb_setup (GstAudioFilter * self,
    GstRingBufferSpec * format);
static gboolean gst_audio_reverb_stop (GstBaseTransform * base);
static GstFlowReturn gst_audio_reverb_transform_ip (GstBaseTransform * base,
    GstBuffer * buf);

static void gst_audio_reverb_transform_float (GstAudioReverb * self,
    gfloat * data, guint num_samples);
static void gst_audio_reverb_transform_double (GstAudioReverb * self,
    gdouble * data, guint num_samples);

/* GObject vmethod implementations */

static void
gst_audio_reverb_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstCaps *caps;

  gst_element_class_set_details_simple (element_class, "Audio reverb",
      "Filter/Effect/Audio",
      "Adds an echo or reverb effect to an audio stream",
      "Sebastian Dröge <sebastian.droege@collabora.co.uk>");

  caps = gst_caps_from_string (ALLOWED_CAPS);
  gst_audio_filter_class_add_pad_templates (GST_AUDIO_FILTER_CLASS (klass),
      caps);
  gst_caps_unref (caps);
}

static void
gst_audio_reverb_class_init (GstAudioReverbClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstBaseTransformClass *basetransform_class = (GstBaseTransformClass *) klass;
  GstAudioFilterClass *audioself_class = (GstAudioFilterClass *) klass;

  gobject_class->set_property = gst_audio_reverb_set_property;
  gobject_class->get_property = gst_audio_reverb_get_property;
  gobject_class->finalize = gst_audio_reverb_finalize;

  g_object_class_install_property (gobject_class, PROP_DELAY,
      g_param_spec_uint64 ("delay", "Delay",
          "Delay of the echo in nanoseconds", 1, G_MAXUINT64,
          1, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
          | GST_PARAM_CONTROLLABLE));

  g_object_class_install_property (gobject_class, PROP_INTENSITY,
      g_param_spec_float ("intensity", "Intensity",
          "Intensity of the echo", 0.0, 1.0,
          0.0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
          | GST_PARAM_CONTROLLABLE));

  g_object_class_install_property (gobject_class, PROP_FEEDBACK,
      g_param_spec_float ("feedback", "Feedback",
          "Amount of feedback", 0.0, 1.0,
          0.0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
          | GST_PARAM_CONTROLLABLE));

  audioself_class->setup = GST_DEBUG_FUNCPTR (gst_audio_reverb_setup);
  basetransform_class->transform_ip =
      GST_DEBUG_FUNCPTR (gst_audio_reverb_transform_ip);
  basetransform_class->stop = GST_DEBUG_FUNCPTR (gst_audio_reverb_stop);
}

static void
gst_audio_reverb_init (GstAudioReverb * self, GstAudioReverbClass * klass)
{
  self->delay = 0;
  self->intensity = 0.0;
  self->feedback = 0.0;

  gst_base_transform_set_in_place (GST_BASE_TRANSFORM (self), TRUE);
}

static void
gst_audio_reverb_finalize (GObject * object)
{
  GstAudioReverb *self = GST_AUDIO_REVERB (object);

  g_free (self->buffer);
  self->buffer = NULL;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_audio_reverb_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstAudioReverb *self = GST_AUDIO_REVERB (object);

  switch (prop_id) {
    case PROP_DELAY:{
      guint rate, width, channels;

      GST_BASE_TRANSFORM_LOCK (self);
      self->delay = g_value_get_uint64 (value);

      rate = GST_AUDIO_FILTER (self)->format.rate;
      width = GST_AUDIO_FILTER (self)->format.width / 8;
      channels = GST_AUDIO_FILTER (self)->format.channels;

      if (self->buffer && rate > 0) {
        guint new_reverb =
            MAX (gst_util_uint64_scale (self->delay, rate, GST_SECOND), 1);
        guint new_size = new_reverb * width * channels;

        if (new_size > self->buffer_size) {
          guint i;
          guint8 *old_buffer = self->buffer;

          self->buffer_size = new_size;
          self->buffer = g_malloc0 (new_size);

          for (i = 0; i < self->buffer_size_frames; i++) {
            memcpy (&self->buffer[i * width * channels],
                &old_buffer[((i +
                            self->buffer_pos) % self->buffer_size_frames) *
                    width * channels], channels * width);
          }
          self->buffer_size_frames = self->delay_frames = new_reverb;
          self->buffer_pos = 0;
        }
      } else if (self->buffer) {
        g_free (self->buffer);
        self->buffer = NULL;
      }

      GST_BASE_TRANSFORM_UNLOCK (self);
    }
      break;
    case PROP_INTENSITY:{
      GST_BASE_TRANSFORM_LOCK (self);
      self->intensity = g_value_get_float (value);
      GST_BASE_TRANSFORM_UNLOCK (self);
    }
      break;
    case PROP_FEEDBACK:{
      GST_BASE_TRANSFORM_LOCK (self);
      self->feedback = g_value_get_float (value);
      GST_BASE_TRANSFORM_UNLOCK (self);
    }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_audio_reverb_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstAudioReverb *self = GST_AUDIO_REVERB (object);

  switch (prop_id) {
    case PROP_DELAY:
      GST_BASE_TRANSFORM_LOCK (self);
      g_value_set_uint64 (value, self->delay);
      GST_BASE_TRANSFORM_UNLOCK (self);
      break;
    case PROP_INTENSITY:
      GST_BASE_TRANSFORM_LOCK (self);
      g_value_set_float (value, self->intensity);
      GST_BASE_TRANSFORM_UNLOCK (self);
      break;
    case PROP_FEEDBACK:
      GST_BASE_TRANSFORM_LOCK (self);
      g_value_set_float (value, self->feedback);
      GST_BASE_TRANSFORM_UNLOCK (self);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* GstAudioFilter vmethod implementations */

static gboolean
gst_audio_reverb_setup (GstAudioFilter * base, GstRingBufferSpec * format)
{
  GstAudioReverb *self = GST_AUDIO_REVERB (base);
  gboolean ret = TRUE;

  if (format->type == GST_BUFTYPE_FLOAT && format->width == 32)
    self->process = (GstAudioReverbProcessFunc)
        gst_audio_reverb_transform_float;
  else if (format->type == GST_BUFTYPE_FLOAT && format->width == 64)
    self->process = (GstAudioReverbProcessFunc)
        gst_audio_reverb_transform_double;
  else
    ret = FALSE;

  g_free (self->buffer);
  self->buffer = NULL;
  self->buffer_pos = 0;
  self->buffer_size = 0;
  self->buffer_size_frames = 0;

  return ret;
}

static gboolean
gst_audio_reverb_stop (GstBaseTransform * base)
{
  GstAudioReverb *self = GST_AUDIO_REVERB (base);

  g_free (self->buffer);
  self->buffer = NULL;
  self->buffer_pos = 0;
  self->buffer_size = 0;
  self->buffer_size_frames = 0;

  return TRUE;
}

#define TRANSFORM_FUNC(name, type) \
static void \
gst_audio_reverb_transform_##name (GstAudioReverb * self, \
    type * data, guint num_samples) \
{ \
  type *buffer = (type *) self->buffer; \
  guint channels = GST_AUDIO_FILTER (self)->format.channels; \
  guint rate = GST_AUDIO_FILTER (self)->format.rate; \
  guint i, j; \
  guint reverb_index = self->buffer_size_frames - self->delay_frames; \
  gdouble reverb_off = ((((gdouble) self->delay) * rate) / GST_SECOND) - self->delay_frames; \
  \
  if (reverb_off < 0.0) \
    reverb_off = 0.0; \
  \
  num_samples /= channels; \
  \
  for (i = 0; i < num_samples; i++) { \
    guint echo0_index = ((reverb_index + self->buffer_pos) % self->buffer_size_frames) * channels; \
    guint echo1_index = ((reverb_index + self->buffer_pos +1) % self->buffer_size_frames) * channels; \
    guint rbout_index = (self->buffer_pos % self->buffer_size_frames) * channels; \
    for (j = 0; j < channels; j++) { \
      gdouble in = data[i*channels + j]; \
      gdouble echo0 = buffer[echo0_index + j]; \
      gdouble echo1 = buffer[echo1_index + j]; \
      gdouble echo = echo0 + (echo1-echo0)*reverb_off; \
      type out = in + self->intensity * echo; \
      \
      data[i*channels + j] = out; \
      \
      buffer[rbout_index + j] = in + self->feedback * echo; \
    } \
    self->buffer_pos = (self->buffer_pos + 1) % self->buffer_size_frames; \
  } \
}

TRANSFORM_FUNC (float, gfloat);
TRANSFORM_FUNC (double, gdouble);

/* GstBaseTransform vmethod implementations */
static GstFlowReturn
gst_audio_reverb_transform_ip (GstBaseTransform * base, GstBuffer * buf)
{
  GstAudioReverb *self = GST_AUDIO_REVERB (base);
  guint num_samples =
      GST_BUFFER_SIZE (buf) / (GST_AUDIO_FILTER (self)->format.width / 8);

  if (GST_CLOCK_TIME_IS_VALID (GST_BUFFER_TIMESTAMP (buf)))
    gst_object_sync_values (G_OBJECT (self), GST_BUFFER_TIMESTAMP (buf));

  if (self->buffer == NULL) {
    guint width, rate, channels;

    width = GST_AUDIO_FILTER (self)->format.width / 8;
    rate = GST_AUDIO_FILTER (self)->format.rate;
    channels = GST_AUDIO_FILTER (self)->format.channels;

    self->delay_frames =
        MAX (gst_util_uint64_scale (self->delay, rate, GST_SECOND), 1);

    self->buffer_size_frames = MAX (self->delay_frames, 1000);
    self->buffer_size = self->buffer_size_frames * width * channels;
    self->buffer = g_malloc0 (self->buffer_size);
    self->buffer_pos = 0;
  }

  self->process (self, GST_BUFFER_DATA (buf), num_samples);

  return GST_FLOW_OK;
}
