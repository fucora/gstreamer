/* G-Streamer generic V4L2 element
 * Copyright (C) 2002 Ronald Bultje <rbultje@ronald.bitfreak.net>
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
#include <config.h>
#endif

#include "v4l2_calls.h"
#include "gstv4l2tuner.h"
#include "gstv4l2xoverlay.h"
#include "gstv4l2colorbalance.h"

/* elementfactory details */
static GstElementDetails gst_v4l2element_details = {
	"Generic video4linux2 Element",
	"Generic/Video",
	"LGPL",
	"Generic plugin for handling common video4linux2 calls",
	VERSION,
	"Ronald Bultje <rbultje@ronald.bitfreak.net>",
	"(C) 2002",
};

/* V4l2Element signals and args */
enum {
	/* FILL ME */
	SIGNAL_OPEN,
	SIGNAL_CLOSE,
	LAST_SIGNAL
};

enum {
	ARG_0,
	ARG_DEVICE,
	ARG_DEVICE_NAME,
	ARG_FLAGS
};


static void	gst_v4l2element_class_init	(GstV4l2ElementClass *klass);
static void	gst_v4l2element_init		(GstV4l2Element *v4lelement);
static void	gst_v4l2element_dispose		(GObject        *object);
static void	gst_v4l2element_set_property	(GObject        *object,
						 guint          prop_id,
						 const GValue   *value,
						 GParamSpec     *pspec);
static void	gst_v4l2element_get_property	(GObject        *object,
						 guint          prop_id,
						 GValue         *value,
						 GParamSpec     *pspec);
static GstElementStateReturn
		gst_v4l2element_change_state	(GstElement     *element);


static GstElementClass *parent_class = NULL;
static guint gst_v4l2element_signals[LAST_SIGNAL] = { 0 };


static gboolean
gst_v4l2_iface_supported (GstInterface *iface,
			  GType         iface_type)
{
	GstV4l2Element *v4l2element = GST_V4L2ELEMENT (iface);

	g_assert (iface_type == GST_TYPE_TUNER ||
		  iface_type == GST_TYPE_X_OVERLAY ||
		  iface_type == GST_TYPE_COLOR_BALANCE);

	if (v4l2element->video_fd == -1)
		return FALSE;

	if (iface_type == GST_TYPE_X_OVERLAY &&
	    !GST_V4L2_IS_OVERLAY(v4l2element))
		return FALSE;

	return TRUE;
}


static void
gst_v4l2_interface_init (GstInterfaceClass *klass)
{
	/* default virtual functions */
	klass->supported = gst_v4l2_iface_supported;
}


GType
gst_v4l2element_get_type (void)
{
	static GType v4l2element_type = 0;

	if (!v4l2element_type) {
		static const GTypeInfo v4l2element_info = {
			sizeof(GstV4l2ElementClass),
			NULL,
			NULL,
			(GClassInitFunc) gst_v4l2element_class_init,
			NULL,
			NULL,
			sizeof(GstV4l2Element),
			0,
			(GInstanceInitFunc) gst_v4l2element_init,
			NULL
		};
		static const GInterfaceInfo v4l2iface_info = {
			(GInterfaceInitFunc) gst_v4l2_interface_init,
			NULL,
			NULL,
		};
		static const GInterfaceInfo v4l2_tuner_info = {
			(GInterfaceInitFunc) gst_v4l2_tuner_interface_init,
			NULL,
			NULL,
		};
		static const GInterfaceInfo v4l2_xoverlay_info = {
			(GInterfaceInitFunc) gst_v4l2_xoverlay_interface_init,
			NULL,
			NULL,
		};
		static const GInterfaceInfo v4l2_colorbalance_info = {
			(GInterfaceInitFunc) gst_v4l2_color_balance_interface_init,
			NULL,
			NULL,
		};

		v4l2element_type =
			g_type_register_static(GST_TYPE_ELEMENT,
				"GstV4l2Element", &v4l2element_info, 0);

		g_type_add_interface_static (v4l2element_type,
					     GST_TYPE_INTERFACE,
					     &v4l2iface_info);
		g_type_add_interface_static (v4l2element_type,
					     GST_TYPE_TUNER,
					     &v4l2_tuner_info);
		g_type_add_interface_static (v4l2element_type,
					     GST_TYPE_X_OVERLAY,
					     &v4l2_xoverlay_info);
		g_type_add_interface_static (v4l2element_type,
					     GST_TYPE_COLOR_BALANCE,
					     &v4l2_colorbalance_info);
	}

	return v4l2element_type;
}


#define GST_TYPE_V4L2_DEVICE_FLAGS (gst_v4l2_device_get_type ())
GType
gst_v4l2_device_get_type (void)
{
	static GType v4l2_device_type = 0;

	if (v4l2_device_type == 0) {
		static const GFlagsValue values[] = {
			{ V4L2_CAP_VIDEO_CAPTURE, "CAPTURE",
			  "Device can capture" },
			{ V4L2_CAP_VIDEO_OUTPUT,  "PLAYBACK",
			  "Device can playback" },
			{ V4L2_CAP_VIDEO_OVERLAY, "OVERLAY",
			  "Device can do overlay" },
			{ V4L2_CAP_TUNER,         "TUNER",
			  "Device has a tuner" },
			{ V4L2_CAP_AUDIO,         "AUDIO",
			  "Device handles audio" },
			{ 0, NULL, NULL }
		};

		v4l2_device_type =
			g_flags_register_static ("GstV4l2DeviceTypeFlags",
						 values);
	}

	return v4l2_device_type;
}


static void
gst_v4l2element_class_init (GstV4l2ElementClass *klass)
{
	GObjectClass *gobject_class;
	GstElementClass *gstelement_class;

	gobject_class = (GObjectClass*)klass;
	gstelement_class = (GstElementClass*)klass;

	parent_class = g_type_class_ref(GST_TYPE_ELEMENT);

	g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_DEVICE,
		g_param_spec_string("device", "Device", "Device location",
			NULL, G_PARAM_READWRITE));
	g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_DEVICE_NAME,
		g_param_spec_string("device_name", "Device name",
			"Name of the device", NULL, G_PARAM_READABLE));
	g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_FLAGS,
		g_param_spec_flags("flags", "Flags", "Device type flags",
			GST_TYPE_V4L2_DEVICE_FLAGS, 0, G_PARAM_READABLE));

	/* signals */
	gst_v4l2element_signals[SIGNAL_OPEN] =
		g_signal_new("open", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
			G_STRUCT_OFFSET(GstV4l2ElementClass, open),
			NULL, NULL, g_cclosure_marshal_VOID__STRING,
			G_TYPE_NONE, 1, G_TYPE_STRING);
	gst_v4l2element_signals[SIGNAL_CLOSE] =
		g_signal_new("close", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
			G_STRUCT_OFFSET(GstV4l2ElementClass, close),
			NULL, NULL, g_cclosure_marshal_VOID__STRING,
			G_TYPE_NONE, 1, G_TYPE_STRING);

	gobject_class->set_property = gst_v4l2element_set_property;
	gobject_class->get_property = gst_v4l2element_get_property;
	gobject_class->dispose = gst_v4l2element_dispose;

	gstelement_class->change_state = gst_v4l2element_change_state;
}


static void
gst_v4l2element_init (GstV4l2Element *v4l2element)
{
	/* some default values */
	v4l2element->video_fd = -1;
	v4l2element->buffer = NULL;
	v4l2element->device = g_strdup("/dev/video");
	v4l2element->display = g_strdup(g_getenv("DISPLAY"));

	v4l2element->channels = NULL;
	v4l2element->norms = NULL;
	v4l2element->colors = NULL;

	v4l2element->overlay = gst_v4l2_xoverlay_new(v4l2element);
}


static void
gst_v4l2element_dispose (GObject *object)
{
  GstV4l2Element *v4l2element = GST_V4L2ELEMENT(object);

  if (v4l2element->overlay) {
    gst_v4l2_xoverlay_free(v4l2element);
  }

  if (v4l2element->display) {
    g_free (v4l2element->display);
  }

  if (v4l2element->device) {
    g_free (v4l2element->device);
  }

  if (((GObjectClass *) parent_class)->dispose)
    ((GObjectClass *) parent_class)->dispose(object);
}

static void
gst_v4l2element_set_property (GObject      *object,
                              guint        prop_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
	GstV4l2Element *v4l2element;

	/* it's not null if we got it, but it might not be ours */
	g_return_if_fail(GST_IS_V4L2ELEMENT(object));
	v4l2element = GST_V4L2ELEMENT(object);

	switch (prop_id) {
		case ARG_DEVICE:
			if (!GST_V4L2_IS_OPEN(v4l2element)) {
				if (v4l2element->device)
					g_free(v4l2element->device);
				v4l2element->device = g_strdup(g_value_get_string(value));
			}
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
			break;
	}
}


static void
gst_v4l2element_get_property (GObject    *object,
                              guint      prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
	GstV4l2Element *v4l2element;

	/* it's not null if we got it, but it might not be ours */
	g_return_if_fail(GST_IS_V4L2ELEMENT(object));
	v4l2element = GST_V4L2ELEMENT(object);

	switch (prop_id) {
		case ARG_DEVICE:
			g_value_set_string(value, v4l2element->device);
			break;
		case ARG_DEVICE_NAME: {
			gchar *new = NULL;
			if (GST_V4L2_IS_OPEN(v4l2element))
				new = v4l2element->vcap.card;
			g_value_set_string(value, new);
			break;
		}
		case ARG_FLAGS: {
			guint flags = 0;
			if (GST_V4L2_IS_OPEN(v4l2element)) {
				flags |= v4l2element->vcap.capabilities &
					30007;
			}
			g_value_set_flags(value, flags);
			break;
		}
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
			break;
	}
}


static GstElementStateReturn
gst_v4l2element_change_state (GstElement *element)
{
	GstV4l2Element *v4l2element;

	g_return_val_if_fail(GST_IS_V4L2ELEMENT(element), GST_STATE_FAILURE);
  
	v4l2element = GST_V4L2ELEMENT(element);

	/* if going down into NULL state, close the device if it's open
	 * if going to READY, open the device (and set some options)
	 */
	switch (GST_STATE_TRANSITION(element)) {
		case GST_STATE_NULL_TO_READY:
			gst_v4l2_set_display(v4l2element);

			if (!gst_v4l2_open(v4l2element))
				return GST_STATE_FAILURE;

			gst_v4l2_xoverlay_open(v4l2element);

			/* emit a signal! whoopie! */
			g_signal_emit(G_OBJECT(v4l2element),
				gst_v4l2element_signals[SIGNAL_OPEN], 0,
				v4l2element->device);
			break;
		case GST_STATE_READY_TO_NULL:
			gst_v4l2_xoverlay_close(v4l2element);

			if (!gst_v4l2_close(v4l2element))
				return GST_STATE_FAILURE;

			/* emit yet another signal! wheehee! */
			g_signal_emit(G_OBJECT(v4l2element),
				gst_v4l2element_signals[SIGNAL_CLOSE], 0,
				v4l2element->device);
			break;
	}

	if (GST_ELEMENT_CLASS(parent_class)->change_state)
		return GST_ELEMENT_CLASS(parent_class)->change_state(element);

	return GST_STATE_SUCCESS;
}


gboolean
gst_v4l2element_factory_init (GstPlugin *plugin)
{
	GstElementFactory *factory;

	/* we can run without... But not yet. ;). */
	if (!gst_library_load ("xwindowlistener"))
		return FALSE;

	/* create an elementfactory for the v4l2element */
	factory = gst_element_factory_new("v4l2element", GST_TYPE_V4L2ELEMENT,
				&gst_v4l2element_details);
	g_return_val_if_fail(factory != NULL, FALSE);
	gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));

	return TRUE;
}
