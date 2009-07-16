/* GStreamer
 *
 * Copyright (C) 2006-2009 Lutz Mueller <lutz@topfrose.de>
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
#include "gstcairorender.h"

#include <cairo.h>
#include <cairo-features.h>
#ifdef CAIRO_HAS_PS_SURFACE
#include <cairo-ps.h>
#endif
#ifdef CAIRO_HAS_PDF_SURFACE
#include <cairo-pdf.h>
#endif
#ifdef CAIRO_HAS_SVG_SURFACE
#include <cairo-svg.h>
#endif

#include <gst/video/video.h>

#include <string.h>

GST_DEBUG_CATEGORY_STATIC (cairo_render_debug);
#define GST_CAT_DEFAULT cairo_render_debug

static gboolean
gst_cairo_render_event (GstPad * pad, GstEvent * e)
{
  GstCairoRender *c = GST_CAIRO_RENDER (GST_PAD_PARENT (pad));

  switch (GST_EVENT_TYPE (e)) {
    case GST_EVENT_EOS:
      if (c->surface)
        cairo_surface_finish (c->surface);
      break;
    default:
      break;
  }
  return gst_pad_event_default (pad, e);
}

static cairo_status_t
write_func (void *closure, const unsigned char *data, unsigned int length)
{
  GstCairoRender *c = GST_CAIRO_RENDER (closure);
  GstBuffer *buf;
  GstFlowReturn r;

  buf = gst_buffer_new ();
  gst_buffer_set_data (buf, (guint8 *) data, length);
  gst_buffer_set_caps (buf, GST_PAD_CAPS (c->src));
  if ((r = gst_pad_push (c->src, buf)) != GST_FLOW_OK) {
    GST_DEBUG_OBJECT (c, "Could not pass on buffer: %s.",
        gst_flow_get_name (r));
    return CAIRO_STATUS_WRITE_ERROR;
  }
  return CAIRO_STATUS_SUCCESS;
}

static cairo_status_t
read_func (void *closure, unsigned char *data, unsigned int length)
{
  GstCairoRender *c = GST_CAIRO_RENDER (closure);
  GstBuffer *buf;

  if (gst_pad_pull_range (c->snk, c->offset, length, &buf) != GST_FLOW_OK)
    return CAIRO_STATUS_READ_ERROR;
  c->offset += GST_BUFFER_SIZE (buf);
  memcpy (data, GST_BUFFER_DATA (buf), length);
  gst_buffer_unref (buf);
  return CAIRO_STATUS_SUCCESS;
}

static cairo_status_t
read_buffer (void *closure, unsigned char *data, unsigned int length)
{
  GstBuffer *buf = GST_BUFFER (closure);

  if (GST_BUFFER_OFFSET (buf) + length > GST_BUFFER_SIZE (buf))
    return CAIRO_STATUS_READ_ERROR;
  memcpy (data, GST_BUFFER_DATA (buf) + GST_BUFFER_OFFSET (buf), length);
  GST_BUFFER_OFFSET (buf) += length;
  return CAIRO_STATUS_SUCCESS;
}

static gboolean
gst_cairo_render_push_surface (GstCairoRender * c, cairo_surface_t * surface)
{
  cairo_status_t s = 0;
  cairo_t *cr;

  if (!c->surface) {
    s = cairo_surface_write_to_png_stream (surface, write_func, c);
    cairo_surface_destroy (surface);
    if (s != CAIRO_STATUS_SUCCESS) {
      GST_DEBUG_OBJECT (c, "Could not create PNG stream: %s.",
          cairo_status_to_string (s));
      return FALSE;
    }
    return TRUE;
  }

  cr = cairo_create (c->surface);
  cairo_set_source_surface (cr, surface, 0, 0);
  cairo_paint (cr);
  cairo_show_page (cr);
  cairo_destroy (cr);
  cairo_surface_destroy (surface);
  return (TRUE);
}

static GstFlowReturn
gst_cairo_render_chain (GstPad * pad, GstBuffer * buf)
{
  GstCairoRender *c = GST_CAIRO_RENDER (GST_PAD_PARENT (pad));
  cairo_surface_t *s;
  gboolean success;

  if (c->png) {
    GST_BUFFER_OFFSET (buf) = 0;
    s = cairo_image_surface_create_from_png_stream (read_buffer, buf);
  } else
    s = cairo_image_surface_create_for_data (GST_BUFFER_DATA (buf),
        c->format, c->width, c->height, c->width);
  success = gst_cairo_render_push_surface (c, s);
  gst_buffer_unref (buf);
  return success ? GST_FLOW_OK : GST_FLOW_ERROR;
}

static gboolean
gst_cairo_render_activate_sink (GstPad * pad)
{
  GstCairoRender *c = GST_CAIRO_RENDER (GST_PAD_PARENT (pad));
  GstFormat format = GST_FORMAT_BYTES;

  if (c->png) {
    if (!gst_pad_check_pull_range (pad)) {
      GST_DEBUG_OBJECT (c, "We do not have random access. "
          "Let's hope each buffer we receive contains a whole png image.");
      return gst_pad_activate_push (pad, TRUE);
    }
    if (!gst_pad_query_peer_duration (c->snk, &format, &c->duration)) {
      GST_DEBUG_OBJECT (c, "Could not query duration. "
          "Let's hope each buffer we receive contains a whole png image.");
      return gst_pad_activate_push (pad, TRUE);
    }
    return gst_pad_activate_pull (pad, TRUE);
  }
  return gst_pad_activate_push (pad, TRUE);
}

static gboolean
gst_cairo_render_setcaps_sink (GstPad * pad, GstCaps * caps)
{
  GstCairoRender *c = GST_CAIRO_RENDER (GST_PAD_PARENT (pad));
  GstStructure *s = gst_caps_get_structure (caps, 0);
  const gchar *mime = gst_structure_get_name (s);
  gint fps_n = 0, fps_d = 1;
  gint w, h;

  GST_DEBUG_OBJECT (c, "Got caps (%s).", mime);
  if ((c->png = !strcmp (mime, "image/png")))
    return TRUE;

  /* Width and height */
  if (!gst_structure_get_int (s, "width", &c->width) ||
      !gst_structure_get_int (s, "height", &c->height)) {
    GST_ERROR_OBJECT (c, "Invalid caps");
    return FALSE;
  }

  /* Colorspace
   * FIXME: I couldn't figure out the right caps. The solution below
   * results in a black and white result which is better than nothing.
   * If you know how to fix this, please do it. */
  if (!strcmp (mime, "video/x-raw-yuv")) {
    c->format = CAIRO_FORMAT_A8;
  } else if (!strcmp (mime, "video/x-raw-rgb")) {
    c->format = CAIRO_FORMAT_RGB24;
  } else {
    GST_DEBUG_OBJECT (c, "Unknown mime type '%s'.", mime);
    return FALSE;
  }

  /* Framerate */
  gst_structure_get_fraction (s, "framerate", &fps_n, &fps_d);

  /* Create output caps */
  caps = gst_pad_get_allowed_caps (c->src);
  caps = gst_caps_make_writable (caps);
  gst_caps_truncate (caps);
  s = gst_caps_get_structure (caps, 0);
  mime = gst_structure_get_name (s);
  gst_structure_set (s, "height", G_TYPE_INT, c->height, "width", G_TYPE_INT,
      c->width, "framerate", GST_TYPE_FRACTION, fps_n, fps_d, NULL);

  if (c->surface) {
    cairo_surface_destroy (c->surface);
    c->surface = NULL;
  }

  w = c->width;
  h = c->height;

  GST_DEBUG_OBJECT (c, "Setting src caps %" GST_PTR_FORMAT, caps);
  gst_pad_set_caps (c->src, caps);

#if CAIRO_HAS_PS_SURFACE
  if (!strcmp (mime, "application/postscript")) {
    c->surface = cairo_ps_surface_create_for_stream (write_func, c, w, h);
  } else
#endif
#if CAIRO_HAS_PDF_SURFACE
  if (!strcmp (mime, "application/x-pdf")) {
    c->surface = cairo_pdf_surface_create_for_stream (write_func, c, w, h);
  } else
#endif
#if CAIRO_HAS_SVG_SURFACE
  if (!strcmp (mime, "image/svg")) {
    c->surface = cairo_svg_surface_create_for_stream (write_func, c, w, h);
  } else
#endif
  {
    gst_caps_unref (caps);
    return FALSE;
  }

  gst_caps_unref (caps);

  return TRUE;
}

static void
gst_cairo_render_loop (gpointer data)
{
  GstCairoRender *c = GST_CAIRO_RENDER (data);

  if (c->offset >= c->duration) {
    gst_pad_pause_task (c->snk);
    gst_pad_send_event (c->snk, gst_event_new_eos ());
    return;
  }

  if (!gst_cairo_render_push_surface (c,
          cairo_image_surface_create_from_png_stream (read_func, c))) {
    GST_DEBUG_OBJECT (c, "Could not push image.");
    gst_pad_pause_task (c->snk);
    gst_pad_send_event (c->snk, gst_event_new_eos ());
  }
}

static gboolean
gst_cairo_render_activatepull_sink (GstPad * pad, gboolean active)
{
  GstCairoRender *c = GST_CAIRO_RENDER (GST_PAD_PARENT (pad));

  if (active) {
    c->offset = 0;
    return gst_pad_start_task (pad, gst_cairo_render_loop, c);
  } else
    return gst_pad_stop_task (pad);
}

static GstElementDetails cairo_render_details =
GST_ELEMENT_DETAILS ("CAIRO encoder",
    "Codec/Encoder", "Encodes streams using CAIRO",
    "Lutz Mueller <lutz@topfrose.de>");

static GstStaticPadTemplate t_src = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS (
#if CAIRO_HAS_PDF_SURFACE
        "application/x-pdf, "
        "width = (int) [ 1, MAX], " "height = (int) [ 1, MAX] "
#endif
#if CAIRO_HAS_PDF_SURFACE && (CAIRO_HAS_PS_SURFACE || CAIRO_HAS_SVG_SURFACE || CAIRO_HAS_PNG_FUNCTIONS)
        ";"
#endif
#if CAIRO_HAS_PS_SURFACE
        "application/postscript, "
        "width = (int) [ 1, MAX], " "height = (int) [ 1, MAX] "
#endif
#if (CAIRO_HAS_PDF_SURFACE || CAIRO_HAS_PS_SURFACE) && (CAIRO_HAS_SVG_SURFACE || CAIRO_HAS_PNG_FUNCTIONS)
        ";"
#endif
#if CAIRO_HAS_SVG_SURFACE
        "application/svg, "
        "width = (int) [ 1, MAX], " "height = (int) [ 1, MAX] "
#endif
#if (CAIRO_HAS_PDF_SURFACE || CAIRO_HAS_PS_SURFACE || CAIRO_HAS_SVG_SURFACE) && CAIRO_HAS_PNG_FUNCTIONS
        ";"
#endif
#if CAIRO_HAS_PNG_FUNCTIONS
        "image/png"
#endif
    ));
static GstStaticPadTemplate t_snk = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("I420")
#if CAIRO_HAS_PNG_FUNCTIONS
        ";image/png"
#endif
    ));

GST_BOILERPLATE (GstCairoRender, gst_cairo_render, GstElement,
    GST_TYPE_ELEMENT);

static void
gst_cairo_render_init (GstCairoRender * c, GstCairoRenderClass * klass)
{
  /* The sink */
  c->snk =
      gst_pad_new_from_template (gst_static_pad_template_get (&t_snk), "sink");
  gst_pad_set_event_function (c->snk, gst_cairo_render_event);
  gst_pad_set_chain_function (c->snk, gst_cairo_render_chain);
  gst_pad_set_activate_function (c->snk, gst_cairo_render_activate_sink);
  gst_pad_set_activatepull_function (c->snk,
      gst_cairo_render_activatepull_sink);
  gst_pad_set_setcaps_function (c->snk, gst_cairo_render_setcaps_sink);
  gst_pad_use_fixed_caps (c->snk);
  gst_element_add_pad (GST_ELEMENT (c), c->snk);

  /* The source */
  c->src =
      gst_pad_new_from_template (gst_static_pad_template_get (&t_src), "src");
  gst_pad_use_fixed_caps (c->src);
  gst_element_add_pad (GST_ELEMENT (c), c->src);

  c->width = 0;
  c->height = 0;
}

static void
gst_cairo_render_base_init (gpointer g_class)
{
  GstElementClass *ec = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (ec, &cairo_render_details);
  gst_element_class_add_pad_template (ec, gst_static_pad_template_get (&t_snk));
  gst_element_class_add_pad_template (ec, gst_static_pad_template_get (&t_src));
}

static void
gst_cairo_render_finalize (GObject * object)
{
  GstCairoRender *c = GST_CAIRO_RENDER (object);

  if (c->surface) {
    cairo_surface_destroy (c->surface);
    c->surface = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_cairo_render_class_init (GstCairoRenderClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->finalize = gst_cairo_render_finalize;

  GST_DEBUG_CATEGORY_INIT (cairo_render_debug, "cairo_render", 0,
      "CAIRO encoder");
}
