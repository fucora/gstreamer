/**
 * Weave frames, bottom-field-first.
 * Copyright (C) 2003 Billy Biggs <vektor@dumbterm.net>.
 * Copyright (C) 2008,2010 Sebastian Dröge <sebastian.droege@collabora.co.uk>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/*
 * Relicensed for GStreamer from GPL to LGPL with permit from Billy Biggs.
 * See: http://bugzilla.gnome.org/show_bug.cgi?id=163578
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "gstdeinterlacemethod.h"
#include <string.h>

#define GST_TYPE_DEINTERLACE_METHOD_WEAVE_BFF	(gst_deinterlace_method_weave_bff_get_type ())
#define GST_IS_DEINTERLACE_METHOD_WEAVE_BFF(obj)		(G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_DEINTERLACE_METHOD_WEAVE_BFF))
#define GST_IS_DEINTERLACE_METHOD_WEAVE_BFF_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_DEINTERLACE_METHOD_WEAVE_BFF))
#define GST_DEINTERLACE_METHOD_WEAVE_BFF_GET_CLASS(obj)	(G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_DEINTERLACE_METHOD_WEAVE_BFF, GstDeinterlaceMethodWeaveBFFClass))
#define GST_DEINTERLACE_METHOD_WEAVE_BFF(obj)		(G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_DEINTERLACE_METHOD_WEAVE_BFF, GstDeinterlaceMethodWeaveBFF))
#define GST_DEINTERLACE_METHOD_WEAVE_BFF_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_DEINTERLACE_METHOD_WEAVE_BFF, GstDeinterlaceMethodWeaveBFFClass))
#define GST_DEINTERLACE_METHOD_WEAVE_BFF_CAST(obj)	((GstDeinterlaceMethodWeaveBFF*)(obj))

GType gst_deinterlace_method_weave_bff_get_type (void);

typedef GstDeinterlaceSimpleMethod GstDeinterlaceMethodWeaveBFF;
typedef GstDeinterlaceSimpleMethodClass GstDeinterlaceMethodWeaveBFFClass;

static void
deinterlace_scanline_weave_packed (GstDeinterlaceSimpleMethod * self,
    guint8 * out, const GstDeinterlaceScanlineData * scanlines)
{
  oil_memcpy (out, scanlines->m1, self->parent.row_stride[0]);
}

static void
deinterlace_scanline_weave_planar_y (GstDeinterlaceSimpleMethod * self,
    guint8 * out, const GstDeinterlaceScanlineData * scanlines)
{
  oil_memcpy (out, scanlines->m1, self->parent.row_stride[0]);
}

static void
deinterlace_scanline_weave_planar_u (GstDeinterlaceSimpleMethod * self,
    guint8 * out, const GstDeinterlaceScanlineData * scanlines)
{
  oil_memcpy (out, scanlines->m1, self->parent.row_stride[1]);
}

static void
deinterlace_scanline_weave_planar_v (GstDeinterlaceSimpleMethod * self,
    guint8 * out, const GstDeinterlaceScanlineData * scanlines)
{
  oil_memcpy (out, scanlines->m1, self->parent.row_stride[2]);
}

static void
copy_scanline_packed (GstDeinterlaceSimpleMethod * self, guint8 * out,
    const GstDeinterlaceScanlineData * scanlines)
{
  /* FIXME: original code used m2 and m0 but this looks really bad */
  if (scanlines->bottom_field) {
    oil_memcpy (out, scanlines->bb2, self->parent.row_stride[0]);
  } else {
    oil_memcpy (out, scanlines->bb0, self->parent.row_stride[0]);
  }
}

static void
copy_scanline_planar_y (GstDeinterlaceSimpleMethod * self, guint8 * out,
    const GstDeinterlaceScanlineData * scanlines)
{
  /* FIXME: original code used m2 and m0 but this looks really bad */
  if (scanlines->bottom_field) {
    oil_memcpy (out, scanlines->bb2, self->parent.row_stride[0]);
  } else {
    oil_memcpy (out, scanlines->bb0, self->parent.row_stride[0]);
  }
}

static void
copy_scanline_planar_u (GstDeinterlaceSimpleMethod * self, guint8 * out,
    const GstDeinterlaceScanlineData * scanlines)
{
  /* FIXME: original code used m2 and m0 but this looks really bad */
  if (scanlines->bottom_field) {
    oil_memcpy (out, scanlines->bb2, self->parent.row_stride[1]);
  } else {
    oil_memcpy (out, scanlines->bb0, self->parent.row_stride[1]);
  }
}

static void
copy_scanline_planar_v (GstDeinterlaceSimpleMethod * self, guint8 * out,
    const GstDeinterlaceScanlineData * scanlines)
{
  /* FIXME: original code used m2 and m0 but this looks really bad */
  if (scanlines->bottom_field) {
    oil_memcpy (out, scanlines->bb2, self->parent.row_stride[2]);
  } else {
    oil_memcpy (out, scanlines->bb0, self->parent.row_stride[2]);
  }
}

G_DEFINE_TYPE (GstDeinterlaceMethodWeaveBFF, gst_deinterlace_method_weave_bff,
    GST_TYPE_DEINTERLACE_SIMPLE_METHOD);

static void
gst_deinterlace_method_weave_bff_class_init (GstDeinterlaceMethodWeaveBFFClass *
    klass)
{
  GstDeinterlaceMethodClass *dim_class = (GstDeinterlaceMethodClass *) klass;
  GstDeinterlaceSimpleMethodClass *dism_class =
      (GstDeinterlaceSimpleMethodClass *) klass;

  dim_class->fields_required = 3;
  dim_class->name = "Progressive: Bottom Field First";
  dim_class->nick = "weavebff";
  dim_class->latency = 0;

  dism_class->interpolate_scanline_ayuv = deinterlace_scanline_weave_packed;
  dism_class->interpolate_scanline_yuy2 = deinterlace_scanline_weave_packed;
  dism_class->interpolate_scanline_yvyu = deinterlace_scanline_weave_packed;
  dism_class->interpolate_scanline_argb = deinterlace_scanline_weave_packed;
  dism_class->interpolate_scanline_abgr = deinterlace_scanline_weave_packed;
  dism_class->interpolate_scanline_rgba = deinterlace_scanline_weave_packed;
  dism_class->interpolate_scanline_bgra = deinterlace_scanline_weave_packed;
  dism_class->interpolate_scanline_rgb = deinterlace_scanline_weave_packed;
  dism_class->interpolate_scanline_bgr = deinterlace_scanline_weave_packed;
  dism_class->interpolate_scanline_planar_y =
      deinterlace_scanline_weave_planar_y;
  dism_class->interpolate_scanline_planar_u =
      deinterlace_scanline_weave_planar_u;
  dism_class->interpolate_scanline_planar_v =
      deinterlace_scanline_weave_planar_v;

  dism_class->copy_scanline_ayuv = copy_scanline_packed;
  dism_class->copy_scanline_yuy2 = copy_scanline_packed;
  dism_class->copy_scanline_yvyu = copy_scanline_packed;
  dism_class->copy_scanline_argb = copy_scanline_packed;
  dism_class->copy_scanline_abgr = copy_scanline_packed;
  dism_class->copy_scanline_rgba = copy_scanline_packed;
  dism_class->copy_scanline_bgra = copy_scanline_packed;
  dism_class->copy_scanline_rgb = copy_scanline_packed;
  dism_class->copy_scanline_bgr = copy_scanline_packed;
  dism_class->copy_scanline_planar_y = copy_scanline_planar_y;
  dism_class->copy_scanline_planar_u = copy_scanline_planar_u;
  dism_class->copy_scanline_planar_v = copy_scanline_planar_v;
}

static void
gst_deinterlace_method_weave_bff_init (GstDeinterlaceMethodWeaveBFF * self)
{
}
