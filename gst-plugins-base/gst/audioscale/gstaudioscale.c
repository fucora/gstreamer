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
#include "config.h"
#endif
#include <string.h>
#include <math.h>

/*#define DEBUG_ENABLED */
#include <gstaudioscale.h>
#include <gst/audio/audio.h>
#include <gst/resample/resample.h>

/* elementfactory information */
static GstElementDetails audioscale_details = {
  "Audio scaler",
  "Filter/Audio",
  "LGPL",
  "Audio resampler",
  VERSION,
  "Wim Taymans <wim.taymans@chello.be>",
  "(C) 2000",
};

/* Audioscale signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_FILTERLEN,
  ARG_METHOD,
  /* FILL ME */
};

GST_PAD_TEMPLATE_FACTORY (sink_factory,
  "sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  gst_caps_new ("audioscale_sink",
		"audio/x-raw-int",
		  GST_AUDIO_INT_PAD_TEMPLATE_PROPS)
);

GST_PAD_TEMPLATE_FACTORY (src_factory,
  "src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  gst_caps_new ("audioscale_src",
		"audio/x-raw-int",
		  GST_AUDIO_INT_PAD_TEMPLATE_PROPS)
);

#define GST_TYPE_AUDIOSCALE_METHOD (gst_audioscale_method_get_type())
static GType
gst_audioscale_method_get_type (void)
{
  static GType audioscale_method_type = 0;
  static GEnumValue audioscale_methods[] = {
    { RESAMPLE_NEAREST,  "0", "Nearest" },
    { RESAMPLE_BILINEAR, "1", "Bilinear" },
    { RESAMPLE_SINC,     "2", "Sinc" },
    { 0, NULL, NULL },
  };
  if(!audioscale_method_type){
    audioscale_method_type = g_enum_register_static("GstAudioscaleMethod",
                                                    audioscale_methods);
  }
  return audioscale_method_type;
}

static void	gst_audioscale_class_init	(AudioscaleClass *klass);
static void	gst_audioscale_init		(Audioscale *audioscale);

static void	gst_audioscale_chain		(GstPad *pad, GstBuffer *buf);

static void gst_audioscale_set_property (GObject * object, guint prop_id,
					 const GValue * value, GParamSpec * pspec);
static void gst_audioscale_get_property (GObject * object, guint prop_id,
					 GValue * value, GParamSpec * pspec);

static GstElementClass *parent_class = NULL;

/*static guint gst_audioscale_signals[LAST_SIGNAL] = { 0 }; */

GType
audioscale_get_type (void)
{
  static GType audioscale_type = 0;

  if (!audioscale_type) {
    static const GTypeInfo audioscale_info = {
      sizeof(AudioscaleClass),      NULL,
      NULL,
      (GClassInitFunc)gst_audioscale_class_init,
      NULL,
      NULL,
      sizeof(Audioscale),
      0,
      (GInstanceInitFunc)gst_audioscale_init,
    };
    audioscale_type = g_type_register_static(GST_TYPE_ELEMENT, "Audioscale", &audioscale_info, 0);
  }
  return audioscale_type;
}

static void
gst_audioscale_class_init (AudioscaleClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_FILTERLEN,
	g_param_spec_int ("filter_length", "filter_length", "filter_length",
                          0, G_MAXINT, 16, G_PARAM_READWRITE|G_PARAM_CONSTRUCT));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_METHOD,
	g_param_spec_enum ("method", "method", "method", GST_TYPE_AUDIOSCALE_METHOD,
                           RESAMPLE_SINC, G_PARAM_READWRITE|G_PARAM_CONSTRUCT));

  parent_class = g_type_class_ref(GST_TYPE_ELEMENT);

  gobject_class->set_property = gst_audioscale_set_property;
  gobject_class->get_property = gst_audioscale_get_property;

}

static GstCaps *
gst_audioscale_getcaps (GstPad *pad, GstCaps *caps)
{
  Audioscale *audioscale;
  GstCaps *peercaps;

  audioscale = GST_AUDIOSCALE (gst_pad_get_parent (pad));

  if (pad == audioscale->srcpad){
    peercaps = gst_pad_get_allowed_caps (audioscale->sinkpad);
  }else{
    peercaps = gst_pad_get_allowed_caps (audioscale->srcpad);
  }

  if(peercaps == GST_CAPS_NONE){
    return GST_CAPS_NONE;
  }

  caps = gst_caps_copy (peercaps);
#if 1
  /* we do this hack, because the audioscale lib doesn't handle
   * rate conversions larger than a factor of 2 */
  if(gst_caps_has_property_typed(caps, "rate", GST_PROPS_INT_RANGE_TYPE)){
    int rate_min, rate_max;

    gst_props_entry_get_int_range (gst_props_get_entry(caps->properties, "rate"),
	&rate_min, &rate_max);
    gst_caps_set (caps, "rate", GST_PROPS_INT_RANGE((rate_min+1)/2,
	  rate_max*2));
  }else{
    int rate;

    gst_caps_get_int (caps, "rate", &rate);
    gst_caps_set (caps, "rate", GST_PROPS_INT_RANGE((rate+1)/2,rate*2));
  }
#else
  gst_caps_set (caps, "rate", GST_PROPS_INT_RANGE(4000,96000));
#endif

  return caps;
}

static GstPadLinkReturn
gst_audioscale_sink_link (GstPad * pad, GstCaps * caps)
{
  Audioscale *audioscale;
  resample_t *r;
  GstCaps *caps1;
  GstCaps *caps2;
  GstCaps *peercaps;
  gint rate;
  int ret;

  audioscale = GST_AUDIOSCALE (gst_pad_get_parent (pad));
  r = audioscale->resample;

  if (!GST_CAPS_IS_FIXED (caps)){
    return GST_PAD_LINK_DELAYED;
  }

  ret = gst_pad_try_set_caps (audioscale->srcpad, caps);

  if(ret == GST_PAD_LINK_OK || ret == GST_PAD_LINK_DONE){
    audioscale->passthru = TRUE;
    return ret;
  }

  audioscale->passthru = FALSE;

  gst_caps_get_int (caps, "rate",     &rate);
  gst_caps_get_int (caps, "channels", &r->channels);

  r->i_rate = rate;
  resample_reinit(r);

  peercaps = gst_pad_get_allowed_caps (audioscale->srcpad);

  caps1 = gst_caps_copy (caps);
#if 1
  /* we do this hack, because the audioscale lib doesn't handle
   * rate conversions larger than a factor of 2 */
  if(gst_caps_has_property_typed(caps1, "rate", GST_PROPS_INT_RANGE_TYPE)){
    int rate_min, rate_max;

    gst_props_entry_get_int_range (gst_props_get_entry(caps1->properties, "rate"),
	&rate_min, &rate_max);
    gst_caps_set (caps1, "rate", GST_PROPS_INT_RANGE((rate_min+1)/2,
	  rate_max*2));
  }else{
    gst_caps_get_int (caps1, "rate", &rate);
    gst_caps_set (caps1, "rate", GST_PROPS_INT_RANGE((rate+1)/2,rate*2));
  }
#else
  gst_caps_set (caps1, "rate", GST_PROPS_INT_RANGE(4000,96000));
#endif
  caps2 = gst_caps_intersect(caps1, peercaps);
  gst_caps_unref(caps1);

  if(caps2 == GST_CAPS_NONE){
    return GST_PAD_LINK_REFUSED;
  }

  if (GST_CAPS_IS_FIXED (caps2)) {
    ret = gst_pad_try_set_caps (audioscale->srcpad, caps2);
    gst_caps_get_int (caps, "rate",     &rate);
    r->o_rate = rate;
    audioscale->targetfrequency = rate;
    resample_reinit(r);
    return ret;
  }
  
  gst_caps_unref (caps2);
  return GST_PAD_LINK_DELAYED;
}

static void *
gst_audioscale_get_buffer (void *priv, unsigned int size)
{
  Audioscale * audioscale = priv;

  audioscale->outbuf = gst_buffer_new();
  GST_BUFFER_SIZE(audioscale->outbuf) = size;
  GST_BUFFER_DATA(audioscale->outbuf) = g_malloc(size);
  GST_BUFFER_TIMESTAMP(audioscale->outbuf) = audioscale->offset * GST_SECOND / audioscale->targetfrequency;
  audioscale->offset += size / sizeof(gint16) / audioscale->resample->channels;

  return GST_BUFFER_DATA(audioscale->outbuf);
}

static void
gst_audioscale_init (Audioscale *audioscale)
{
  resample_t *r;

  audioscale->sinkpad = gst_pad_new_from_template (
	GST_PAD_TEMPLATE_GET (sink_factory), "sink");
  gst_element_add_pad(GST_ELEMENT(audioscale),audioscale->sinkpad);
  gst_pad_set_chain_function(audioscale->sinkpad,gst_audioscale_chain);
  gst_pad_set_link_function (audioscale->sinkpad, gst_audioscale_sink_link);
  gst_pad_set_getcaps_function (audioscale->sinkpad, gst_audioscale_getcaps);

  audioscale->srcpad = gst_pad_new_from_template (
	GST_PAD_TEMPLATE_GET (src_factory), "src");

  gst_element_add_pad(GST_ELEMENT(audioscale),audioscale->srcpad);
  gst_pad_set_getcaps_function (audioscale->srcpad, gst_audioscale_getcaps);

  r = g_new0(resample_t,1);
  audioscale->resample = r;

  r->priv = audioscale;
  r->get_buffer = gst_audioscale_get_buffer;
  r->method = RESAMPLE_SINC;
  r->channels = 0;
  r->filter_length = 16;
  r->i_rate = -1;
  r->o_rate = -1;
  r->format = RESAMPLE_S16;
  /*r->verbose = 1; */

  resample_init(r);

  /* we will be reinitialized when the G_PARAM_CONSTRUCTs hit */
}

static void
gst_audioscale_chain (GstPad *pad, GstBuffer *buf)
{
  Audioscale *audioscale;
  guchar *data;
  gulong size;

  g_return_if_fail(pad != NULL);
  g_return_if_fail(GST_IS_PAD(pad));
  g_return_if_fail(buf != NULL);

  audioscale = GST_AUDIOSCALE (gst_pad_get_parent (pad));
  if (audioscale->passthru){
    gst_pad_push (audioscale->srcpad, buf);
    return;
  }

  data = GST_BUFFER_DATA(buf);
  size = GST_BUFFER_SIZE(buf);

  GST_DEBUG (
	     "gst_audioscale_chain: got buffer of %ld bytes in '%s'\n",
	     size, gst_element_get_name (GST_ELEMENT (audioscale)));


  resample_scale (audioscale->resample, data, size);

  gst_pad_push (audioscale->srcpad, audioscale->outbuf);

  gst_buffer_unref (buf);
}

static void
gst_audioscale_set_property (GObject * object, guint prop_id,
			     const GValue * value, GParamSpec * pspec)
{
  Audioscale *src;
  resample_t *r;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_AUDIOSCALE(object));
  src = GST_AUDIOSCALE(object);
  r = src->resample;

  switch (prop_id) {
    case ARG_FILTERLEN:
      r->filter_length = g_value_get_int (value);
      GST_DEBUG_OBJECT (GST_ELEMENT(src), "new filter length %d\n", r->filter_length);
      break;
    case ARG_METHOD:
      r->method = g_value_get_enum (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  resample_reinit (r);
}

static void
gst_audioscale_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  Audioscale *src;
  resample_t *r;

  src = GST_AUDIOSCALE (object);
  r = src->resample;

  switch (prop_id) {
    case ARG_FILTERLEN:
      g_value_set_int (value, r->filter_length);
      break;
    case ARG_METHOD:
      g_value_set_enum (value, r->method);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
  GstElementFactory *factory;

  /* create an elementfactory for the audioscale element */
  factory = gst_element_factory_new ("audioscale", GST_TYPE_AUDIOSCALE, &audioscale_details);
  g_return_val_if_fail(factory != NULL, FALSE);
  gst_element_factory_add_pad_template (factory, 
		  GST_PAD_TEMPLATE_GET (src_factory));
  gst_element_factory_add_pad_template (factory, 
		  GST_PAD_TEMPLATE_GET (sink_factory));
  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));

  /* load support library */
  if (!gst_library_load ("gstresample"))
    return FALSE;

  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "audioscale",
  plugin_init
};
