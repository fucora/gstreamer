
#ifndef __GST_TEXT_OVERLAY_H__
#define __GST_TEXT_OVERLAY_H__

#include <gst/gst.h>
#include <gst/base/gstcollectpads.h>

G_BEGIN_DECLS

#define GST_TYPE_TEXT_OVERLAY           (gst_text_overlay_get_type())
#define GST_TEXT_OVERLAY(obj)           (G_TYPE_CHECK_INSTANCE_CAST((obj),\
                                        GST_TYPE_TEXT_OVERLAY, GstTextOverlay))
#define GST_TEXT_OVERLAY_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST((klass),\
                                        GST_TYPE_ULAW, GstTextOverlay))
#define GST_TEXT_OVERLAY_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj),\
                                        GST_TYPE_TEXT_OVERLAY, GstTextOverlayClass))
#define GST_IS_TEXT_OVERLAY(obj)        (G_TYPE_CHECK_INSTANCE_TYPE((obj),\
                                        GST_TYPE_TEXT_OVERLAY))
#define GST_IS_TEXT_OVERLAY_CLASS(obj)  (G_TYPE_CHECK_CLASS_TYPE((klass),\
                                        GST_TYPE_TEXT_OVERLAY))

typedef struct _GstTextOverlay      GstTextOverlay;
typedef struct _GstTextOverlayClass GstTextOverlayClass;

typedef enum _GstTextOverlayVAlign GstTextOverlayVAlign;
typedef enum _GstTextOverlayHAlign GstTextOverlayHAlign;

enum _GstTextOverlayVAlign {
    GST_TEXT_OVERLAY_VALIGN_BASELINE,
    GST_TEXT_OVERLAY_VALIGN_BOTTOM,
    GST_TEXT_OVERLAY_VALIGN_TOP
};

enum _GstTextOverlayHAlign {
    GST_TEXT_OVERLAY_HALIGN_LEFT,
    GST_TEXT_OVERLAY_HALIGN_CENTER,
    GST_TEXT_OVERLAY_HALIGN_RIGHT
};


struct _GstTextOverlay {
    GstElement            element;

    GstPad               *video_sinkpad;
    GstPad               *text_sinkpad;
    GstPad               *srcpad;

    GstCollectPads       *collect;
    GstCollectData       *video_collect_data;
    GstCollectData       *text_collect_data;

    gint                  width;
    gint                  height;
    gdouble               framerate;

    GstTextOverlayVAlign  valign;
    GstTextOverlayHAlign  halign;
    gint                  xpad;
    gint                  ypad;
    gint                  deltax;
    gint                  deltay;
    gchar		 *default_text;
    gboolean		  want_shading;

    guchar               *text_fill_image;
    guchar               *text_outline_image;
    gint                  font_height;
    gint                  text_x0, text_x1; /* start/end x position of text */
    gint                  text_dy;

    gboolean		  need_render;

    gchar                *font;
    gint                  slant;
    gint                  weight;
    gdouble               scale;
};

struct _GstTextOverlayClass {
  GstElementClass parent_class;
};

GType gst_text_overlay_get_type (void);

G_END_DECLS

#endif /* __GST_TEXT_OVERLAY_H */
