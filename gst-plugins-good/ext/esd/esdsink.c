/* GStreamer
 * Copyright (C) <2001> Richard Boulton <richard-gst@tartarus.org>
 *
 * Based on example.c:
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
#include "esdsink.h"
#include <esd.h>
#include <unistd.h>

/* elementfactory information */
static GstElementDetails esdsink_details = {
  "Esound audio sink",
  "Sink/Audio",
  "LGPL",
  "Plays audio to an esound server",
  VERSION,
  "Richard Boulton <richard-gst@tartarus.org>",
  "(C) 2001",
};

/* Signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_MUTE,
  ARG_HOST,
};

GST_PAD_TEMPLATE_FACTORY (sink_factory,
  "sink",					/* the name of the pads */
  GST_PAD_SINK,				/* type of the pad */
  GST_PAD_ALWAYS,			/* ALWAYS/SOMETIMES */
  GST_CAPS_NEW (
    "esdsink_sink",				/* the name of the caps */
    "audio/x-raw-int",				/* the mime type of the caps */
      /* Properties follow: */
        "endianness", GST_PROPS_INT (G_BYTE_ORDER),
	"signed",     GST_PROPS_LIST (
			GST_PROPS_BOOLEAN (TRUE),
			GST_PROPS_BOOLEAN (FALSE)
		      ),
        "width",      GST_PROPS_LIST (
			GST_PROPS_INT (8),
			GST_PROPS_INT (16)
		      ),
	"depth",      GST_PROPS_LIST (
			GST_PROPS_INT (8),
			GST_PROPS_INT (16)
		      ),
	"rate",       GST_PROPS_INT_RANGE (8000, 96000),
     	"channels",   GST_PROPS_LIST (
			GST_PROPS_INT (1),
			GST_PROPS_INT (2)
		      )
  )
);

static void			gst_esdsink_class_init		(GstEsdsinkClass *klass);
static void			gst_esdsink_init		(GstEsdsink *esdsink);

static gboolean			gst_esdsink_open_audio		(GstEsdsink *sink);
static void			gst_esdsink_close_audio		(GstEsdsink *sink);
static GstElementStateReturn	gst_esdsink_change_state	(GstElement *element);
static GstPadLinkReturn		gst_esdsink_sinkconnect		(GstPad *pad, GstCaps *caps);

static void                     gst_esdsink_set_clock           (GstElement *element, GstClock *clock);
static void			gst_esdsink_chain		(GstPad *pad, GstBuffer *buf);

static void			gst_esdsink_set_property	(GObject *object, guint prop_id, 
								 const GValue *value, GParamSpec *pspec);
static void			gst_esdsink_get_property	(GObject *object, guint prop_id, 
								 GValue *value, GParamSpec *pspec);

static GstElementClass *parent_class = NULL;
/*static guint gst_esdsink_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_esdsink_get_type (void)
{
  static GType esdsink_type = 0;

  if (!esdsink_type) {
    static const GTypeInfo esdsink_info = {
      sizeof(GstEsdsinkClass),      NULL,
      NULL,
      (GClassInitFunc)gst_esdsink_class_init,
      NULL,
      NULL,
      sizeof(GstEsdsink),
      0,
      (GInstanceInitFunc)gst_esdsink_init,
    };
    esdsink_type = g_type_register_static(GST_TYPE_ELEMENT, "GstEsdsink", &esdsink_info, 0);
  }
  return esdsink_type;
}

static void
gst_esdsink_class_init (GstEsdsinkClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = g_type_class_ref(GST_TYPE_ELEMENT);

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_MUTE,
    g_param_spec_boolean("mute","mute","mute",
                         TRUE,G_PARAM_READWRITE)); /* CHECKME */
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_HOST,
    g_param_spec_string("host","host","host",
                        NULL, G_PARAM_READWRITE)); /* CHECKME */

  gobject_class->set_property = gst_esdsink_set_property;
  gobject_class->get_property = gst_esdsink_get_property;

  gstelement_class->change_state = gst_esdsink_change_state;
  gstelement_class->set_clock    = gst_esdsink_set_clock;
  //gstelement_class->get_clock    = gst_esdsink_get_clock;
}

static void
gst_esdsink_init(GstEsdsink *esdsink)
{
  esdsink->sinkpad = gst_pad_new_from_template (
		  GST_PAD_TEMPLATE_GET (sink_factory), "sink");
  gst_element_add_pad(GST_ELEMENT(esdsink), esdsink->sinkpad);
  gst_pad_set_chain_function(esdsink->sinkpad, GST_DEBUG_FUNCPTR(gst_esdsink_chain));
  gst_pad_set_link_function(esdsink->sinkpad, gst_esdsink_sinkconnect);

  GST_FLAG_SET (esdsink, GST_ELEMENT_EVENT_AWARE);

  esdsink->mute = FALSE;
  esdsink->fd = -1;
  /* FIXME: get default from somewhere better than just putting them inline. */
  esdsink->negotiated = FALSE;
  esdsink->format = -1;
  esdsink->depth = -1;
  esdsink->channels = -1;
  esdsink->frequency = -1;
  esdsink->host = getenv ("ESPEAKER");
}

static GstPadLinkReturn
gst_esdsink_sinkconnect (GstPad *pad, GstCaps *caps)
{
  GstEsdsink *esdsink;
  gboolean sign;

  esdsink = GST_ESDSINK (gst_pad_get_parent (pad));

  if (!GST_CAPS_IS_FIXED (caps))
    return GST_PAD_LINK_DELAYED;

  gst_caps_get_int (caps, "depth", &esdsink->depth);
  gst_caps_get_int (caps, "signed", &sign);
  gst_caps_get_int (caps, "channels", &esdsink->channels);
  gst_caps_get_int (caps, "rate", &esdsink->frequency);

  /* only u8/s16 */
  if ((sign == FALSE && esdsink->depth != 8) ||
      (sign == TRUE  && esdsink->depth != 16)) {
    return GST_PAD_LINK_REFUSED;
  }

  gst_esdsink_close_audio (esdsink);
  if (gst_esdsink_open_audio (esdsink)) {
    esdsink->negotiated = TRUE;
    return GST_PAD_LINK_OK;
  }

  return GST_PAD_LINK_REFUSED;
}

#if 0
static GstClock *
gst_esdsink_get_clock (GstElement *element)
{
  GstEsdsink *esdsink;

  esdsink = GET_ESDSINK (element);

  return GST_CLOCK(esdsink->provided_clock);
}
#endif

static void
gst_esdsink_set_clock (GstElement *element, GstClock *clock)
{
  GstEsdsink *esdsink;

  esdsink = GST_ESDSINK (element);

  esdsink->clock = clock;
}

static void
gst_esdsink_chain (GstPad *pad, GstBuffer *buf)
{
  GstEsdsink *esdsink;

  esdsink = GST_ESDSINK (gst_pad_get_parent (pad));

  if (!esdsink->negotiated) {
    gst_element_error (GST_ELEMENT (esdsink), "not negotiated");
    goto done;
  }

  if (GST_IS_EVENT(buf)){
    GstEvent *event = GST_EVENT(buf);

    g_print("got event\n");

    switch(GST_EVENT_TYPE(event)){
      case GST_EVENT_EOS:
	break;
      case GST_EVENT_DISCONTINUOUS:
      {
	gint64 value;

	if (gst_event_discont_get_value (event, GST_FORMAT_TIME, &value)) {
	  if (!gst_clock_handle_discont (esdsink->clock, value)){
	    //gst_esdsink_clock_set_active (osssink->provided_clock, FALSE);
	  }
	  //esdsink->handled = 0;
	}
	//esdsink->resync = TRUE;
	break;
      }
      default:
	gst_pad_event_default(pad, event);
	break;
    }
    gst_event_unref(event);
    return;
  }

  if (GST_BUFFER_DATA (buf) != NULL) {
    if (!esdsink->mute && esdsink->fd >= 0) {
      GST_DEBUG ("esdsink: fd=%d data=%p size=%d",
		 esdsink->fd, GST_BUFFER_DATA (buf), GST_BUFFER_SIZE (buf));
      write (esdsink->fd, GST_BUFFER_DATA (buf), GST_BUFFER_SIZE (buf));
    }
  }
done:
  gst_buffer_unref (buf);
}

static void
gst_esdsink_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstEsdsink *esdsink;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_ESDSINK(object));
  esdsink = GST_ESDSINK(object);

  switch (prop_id) {
    case ARG_MUTE:
      esdsink->mute = g_value_get_boolean (value);
      break;
    case ARG_HOST:
      g_free(esdsink->host);
      if (g_value_get_string (value) == NULL)
	  esdsink->host = NULL;
      else
	  esdsink->host = g_strdup (g_value_get_string (value));
      break;
    default:
      break;
  }
}

static void
gst_esdsink_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstEsdsink *esdsink;

  esdsink = GST_ESDSINK(object);

  switch (prop_id) {
    case ARG_MUTE:
      g_value_set_boolean (value, esdsink->mute);
      break;
    case ARG_HOST:
      g_value_set_string (value, esdsink->host);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

gboolean
gst_esdsink_factory_init (GstPlugin *plugin)
{
  GstElementFactory *factory;

  factory = gst_element_factory_new("esdsink", GST_TYPE_ESDSINK,
				   &esdsink_details);
  g_return_val_if_fail(factory != NULL, FALSE);

  gst_element_factory_add_pad_template(factory,
      GST_PAD_TEMPLATE_GET (sink_factory));

  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));

  return TRUE;
}

static gboolean
gst_esdsink_open_audio (GstEsdsink *sink)
{
  /* Name used by esound for this connection. */
  const char * connname = "GStreamer";

  /* Bitmap describing audio format. */
  esd_format_t esdformat = ESD_STREAM | ESD_PLAY;

  g_return_val_if_fail (sink->fd == -1, FALSE);

  if (sink->depth == 16) esdformat |= ESD_BITS16;
  else if (sink->depth == 8) esdformat |= ESD_BITS8;
  else {
    GST_DEBUG ("esdsink: invalid bit depth (%d)", sink->depth);
    return FALSE;
  }

  if (sink->channels == 2) esdformat |= ESD_STEREO;
  else if (sink->channels == 1) esdformat |= ESD_MONO;
  else {
    GST_DEBUG ("esdsink: invalid number of channels (%d)", sink->channels);
    return FALSE;
  }

  GST_DEBUG ("esdsink: attempting to open connection to esound server");
  sink->fd = esd_play_stream_fallback(esdformat, sink->frequency, sink->host, connname);
  if ( sink->fd < 0 ) {
    GST_DEBUG ("esdsink: can't open connection to esound server");
    return FALSE;
  }

  return TRUE;
}

static void
gst_esdsink_close_audio (GstEsdsink *sink)
{
  if (sink->fd < 0) 
    return;

  close(sink->fd);
  sink->fd = -1;

  GST_DEBUG ("esdsink: closed sound device");
}

static GstElementStateReturn
gst_esdsink_change_state (GstElement *element)
{
  GstEsdsink *esdsink;

  esdsink = GST_ESDSINK (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
      break;
    case GST_STATE_READY_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_READY:
      gst_esdsink_close_audio (GST_ESDSINK (element));
      esdsink->negotiated = FALSE;
      break;
    case GST_STATE_READY_TO_NULL:
      break;
    default:
      break;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}

