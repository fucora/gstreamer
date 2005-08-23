/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *               2000,2005 Wim Taymans <wim@fluendo.com>
 *
 * gstosssrc.c: 
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
#include "config.h"
#endif
#include <sys/ioctl.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <sys/soundcard.h>

#include "gstosssrc.h"


static GstElementDetails gst_oss_src_details =
GST_ELEMENT_DETAILS ("Audio Source (OSS)",
    "Source/Audio",
    "Capture from a sound card via OSS",
    "Erik Walthinsen <omega@cse.ogi.edu>, " "Wim Taymans <wim@fluendo.com>");


enum
{
  PROP_0,
  PROP_DEVICE,
  PROP_DEVICE_NAME,
};

GST_BOILERPLATE (GstOssSrc, gst_oss_src, GstAudioSrc, GST_TYPE_AUDIO_SRC);

/*
GST_BOILERPLATE_WITH_INTERFACE (GstOssSrc, gst_oss_src, GstAudioSrc, GST_TYPE_AUDIO_SRC,
 GstMixer, GST_TYPE_MIXER, gst_oss_src_mixer);
GST_IMPLEMENT_OSS_MIXER_METHODS (GstOssSrc, gst_oss_src_mixer);
*/

static void gst_oss_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_oss_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);

static void gst_oss_src_dispose (GObject * object);

static GstCaps *gst_oss_src_getcaps (GstBaseSrc * bsrc);

static gboolean gst_oss_src_open (GstAudioSrc * asrc);
static gboolean gst_oss_src_close (GstAudioSrc * asrc);
static gboolean gst_oss_src_prepare (GstAudioSrc * asrc,
    GstRingBufferSpec * spec);
static gboolean gst_oss_src_unprepare (GstAudioSrc * asrc);
static guint gst_oss_src_read (GstAudioSrc * asrc, gpointer data, guint length);
static guint gst_oss_src_delay (GstAudioSrc * asrc);
static void gst_oss_src_reset (GstAudioSrc * asrc);



static GstStaticPadTemplate osssrc_src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "endianness = (int) { " G_STRINGIFY (G_BYTE_ORDER) " }, "
        "signed = (boolean) { TRUE, FALSE }, "
        "width = (int) 16, "
        "depth = (int) 16, "
        "rate = (int) [ 1, MAX ], " "channels = (int) [ 1, 2 ]; "
        "audio/x-raw-int, "
        "signed = (boolean) { TRUE, FALSE }, "
        "width = (int) 8, "
        "depth = (int) 8, "
        "rate = (int) [ 1, MAX ], " "channels = (int) [ 1, 2 ]")
    );


static void
gst_oss_src_dispose (GObject * object)
{
  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_oss_src_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (element_class, &gst_oss_src_details);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&osssrc_src_factory));
}
static void
gst_oss_src_class_init (GstOssSrcClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSrcClass *gstbasesrc_class;
  GstBaseAudioSrcClass *gstbaseaudiosrc_class;
  GstAudioSrcClass *gstaudiosrc_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasesrc_class = (GstBaseSrcClass *) klass;
  gstbaseaudiosrc_class = (GstBaseAudioSrcClass *) klass;
  gstaudiosrc_class = (GstAudioSrcClass *) klass;

  gobject_class->dispose = GST_DEBUG_FUNCPTR (gst_oss_src_dispose);
  gobject_class->get_property = GST_DEBUG_FUNCPTR (gst_oss_src_get_property);
  gobject_class->set_property = GST_DEBUG_FUNCPTR (gst_oss_src_set_property);

  gstbasesrc_class->get_caps = GST_DEBUG_FUNCPTR (gst_oss_src_getcaps);

  gstaudiosrc_class->open = GST_DEBUG_FUNCPTR (gst_oss_src_open);
  gstaudiosrc_class->prepare = GST_DEBUG_FUNCPTR (gst_oss_src_prepare);
  gstaudiosrc_class->unprepare = GST_DEBUG_FUNCPTR (gst_oss_src_unprepare);
  gstaudiosrc_class->close = GST_DEBUG_FUNCPTR (gst_oss_src_close);
  gstaudiosrc_class->read = GST_DEBUG_FUNCPTR (gst_oss_src_read);
  gstaudiosrc_class->delay = GST_DEBUG_FUNCPTR (gst_oss_src_delay);
  gstaudiosrc_class->reset = GST_DEBUG_FUNCPTR (gst_oss_src_reset);

  g_object_class_install_property (gobject_class, PROP_DEVICE,
      g_param_spec_string ("device", "Device",
          "OSS device (usually /dev/dspN)", "/dev/dsp", G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_DEVICE_NAME,
      g_param_spec_string ("device-name", "Device name",
          "Human-readable name of the sound device", "", G_PARAM_READABLE));
}

static void
gst_oss_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstOssSrc *src;

  src = GST_OSS_SRC (object);

  switch (prop_id) {
    case PROP_DEVICE:
      if (src->device)
        g_free (src->device);
      src->device = g_strdup (g_value_get_string (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_oss_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstOssSrc *src;

  src = GST_OSS_SRC (object);

  switch (prop_id) {
    case PROP_DEVICE:
      g_value_set_string (value, src->device);
      break;
    case PROP_DEVICE_NAME:
      g_value_set_string (value, src->device_name);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_oss_src_init (GstOssSrc * osssrc)
{
  GST_DEBUG ("initializing osssrc");

  osssrc->device = g_strdup ("/dev/dsp");
  osssrc->element = g_object_new (GST_TYPE_OSSELEMENT, NULL);
}

static GstCaps *
gst_oss_src_getcaps (GstBaseSrc * bsrc)
{
  GstOssSrc *osssrc;
  GstOssElement *element;
  GstCaps *caps;

  osssrc = GST_OSS_SRC (bsrc);
  element = osssrc->element;

  gst_osselement_probe_caps (element);

  if (element->probed_caps == NULL) {
    caps =
        gst_caps_copy (gst_pad_get_pad_template_caps (GST_BASE_SRC_PAD (bsrc)));
  } else {
    caps = gst_caps_ref (element->probed_caps);
  }

  return caps;
}

static gint
ilog2 (gint x)
{
  /* well... hacker's delight explains... */
  x = x | (x >> 1);
  x = x | (x >> 2);
  x = x | (x >> 4);
  x = x | (x >> 8);
  x = x | (x >> 16);
  x = x - ((x >> 1) & 0x55555555);
  x = (x & 0x33333333) + ((x >> 2) & 0x33333333);
  x = (x + (x >> 4)) & 0x0f0f0f0f;
  x = x + (x >> 8);
  x = x + (x >> 16);
  return (x & 0x0000003f) - 1;
}

#define SET_PARAM(_oss, _label, _name, _val) 	\
G_STMT_START {					\
  int _tmp = _val;				\
  if (ioctl(_oss->fd, _name, &_tmp) == -1) {	\
    perror(_label);				\
    return FALSE;				\
  }						\
  GST_DEBUG_OBJECT (_oss, _label " %d", _tmp);	\
} G_STMT_END

#define GET_PARAM(oss, label, name, val) 	\
G_STMT_START {					\
  if (ioctl(oss->fd, name, val) == -1) {	\
    perror(label);				\
    return FALSE;				\
  }						\
} G_STMT_END

static gint
gst_oss_src_get_format (GstBufferFormat fmt)
{
  gint result;

  switch (fmt) {
    case GST_MU_LAW:
      result = AFMT_MU_LAW;
      break;
    case GST_A_LAW:
      result = AFMT_A_LAW;
      break;
    case GST_IMA_ADPCM:
      result = AFMT_IMA_ADPCM;
      break;
    case GST_U8:
      result = AFMT_U8;
      break;
    case GST_S16_LE:
      result = AFMT_S16_LE;
      break;
    case GST_S16_BE:
      result = AFMT_S16_BE;
      break;
    case GST_S8:
      result = AFMT_S8;
      break;
    case GST_U16_LE:
      result = AFMT_U16_LE;
      break;
    case GST_U16_BE:
      result = AFMT_U16_BE;
      break;
    case GST_MPEG:
      result = AFMT_MPEG;
      break;
    default:
      result = 0;
      break;
  }
  return result;
}

static gboolean
gst_oss_src_open (GstAudioSrc * asrc)
{
  GstOssSrc *oss;
  int mode;

  oss = GST_OSS_SRC (asrc);

  mode = O_RDONLY;
  mode |= O_NONBLOCK;

  oss->fd = open (oss->device, mode, 0);
  if (oss->fd == -1) {
    perror (oss->device);
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_oss_src_close (GstAudioSrc * asrc)
{
  close (GST_OSS_SRC (asrc)->fd);
  return TRUE;
}

static gboolean
gst_oss_src_prepare (GstAudioSrc * asrc, GstRingBufferSpec * spec)
{
  GstOssSrc *oss;
  struct audio_buf_info info;
  int mode;
  int tmp;

  oss = GST_OSS_SRC (asrc);

  mode = fcntl (oss->fd, F_GETFL);
  mode &= ~O_NONBLOCK;
  if (fcntl (oss->fd, F_SETFL, mode) == -1) {
    perror (oss->device);
    return FALSE;
  }

  tmp = gst_oss_src_get_format (spec->format);
  if (tmp == 0)
    goto wrong_format;

  tmp = ilog2 (spec->segsize);
  tmp = ((spec->segtotal & 0x7fff) << 16) | tmp;
  GST_DEBUG ("set segsize: %d, segtotal: %d, value: %08x", spec->segsize,
      spec->segtotal, tmp);

  SET_PARAM (oss, "SETFRAGMENT", SNDCTL_DSP_SETFRAGMENT, tmp);

  SET_PARAM (oss, "RESET", SNDCTL_DSP_RESET, 0);

  SET_PARAM (oss, "SETFMT", SNDCTL_DSP_SETFMT, tmp);
  if (spec->channels == 2)
    SET_PARAM (oss, "STEREO", SNDCTL_DSP_STEREO, 1);
  SET_PARAM (oss, "CHANNELS", SNDCTL_DSP_CHANNELS, spec->channels);
  SET_PARAM (oss, "SPEED", SNDCTL_DSP_SPEED, spec->rate);

  GET_PARAM (oss, "GETISPACE", SNDCTL_DSP_GETISPACE, &info);

  spec->segsize = info.fragsize;
  spec->segtotal = info.fragstotal;
  spec->bytes_per_sample = 4;
  oss->bytes_per_sample = 4;
  memset (spec->silence_sample, 0, spec->bytes_per_sample);

  GST_DEBUG ("got segsize: %d, segtotal: %d, value: %08x", spec->segsize,
      spec->segtotal, tmp);

  return TRUE;

wrong_format:
  {
    GST_DEBUG ("wrong format %d\n", spec->format);
    return FALSE;
  }
}

static gboolean
gst_oss_src_unprepare (GstAudioSrc * asrc)
{
  /* could do a SNDCTL_DSP_RESET, but the OSS manual recommends a close/open */

  if (!gst_oss_src_close (asrc))
    goto couldnt_close;

  if (!gst_oss_src_open (asrc))
    goto couldnt_reopen;

  return TRUE;

couldnt_close:
  {
    GST_DEBUG ("Could not close the audio device");
    return FALSE;
  }
couldnt_reopen:
  {
    GST_DEBUG ("Could not reopen the audio device");
    return FALSE;
  }
}

static guint
gst_oss_src_read (GstAudioSrc * asrc, gpointer data, guint length)
{
  return read (GST_OSS_SRC (asrc)->fd, data, length);
}

static guint
gst_oss_src_delay (GstAudioSrc * asrc)
{
  GstOssSrc *oss;
  gint delay = 0;
  gint ret;

  oss = GST_OSS_SRC (asrc);

#ifdef SNDCTL_DSP_GETODELAY
  ret = ioctl (oss->fd, SNDCTL_DSP_GETODELAY, &delay);
#else
  ret = -1;
#endif
  if (ret < 0) {
    audio_buf_info info;

    ret = ioctl (oss->fd, SNDCTL_DSP_GETOSPACE, &info);

    delay = (ret < 0 ? 0 : (info.fragstotal * info.fragsize) - info.bytes);
  }
  return delay / oss->bytes_per_sample;
}

static void
gst_oss_src_reset (GstAudioSrc * asrc)
{
  GstOssSrc *oss;

  //gint ret;

  oss = GST_OSS_SRC (asrc);

  /* deadlocks on my machine... */
  //ret = ioctl (oss->fd, SNDCTL_DSP_RESET, 0);
}
